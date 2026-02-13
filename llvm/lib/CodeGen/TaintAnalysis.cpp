//===- TaintAnalysis.cpp - Taint Analysis Pass in Backend ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the TaintAnalysis pass which identifies tainted
// registers at the MIR level based on IR function argument attributes.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/TaintAnalysis.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "taint-analysis"

AnalysisKey TaintAnalysis::Key;

static cl::opt<std::string>
    TaintOutputFile("taint-output",
                    cl::desc("Output file for tainted instructions (TSV)"),
                    cl::value_desc("file"));

static MemLoc getMemLocFromMMO(const MachineMemOperand &MMO) {
  MemLoc L;
  if (const Value *V =
          MMO.getValue()) { // base address of memory access
                            // :contentReference[oaicite:2]{index=2}
    L.K = MemLoc::IRValue;
    L.V = V;
    return L;
  }
  return L; // Unknown
}

static SmallVector<MemLoc, 2> getMemLocs(const MachineInstr &MI) {
  SmallVector<MemLoc, 2> Locs;
  for (MachineMemOperand *MMO : MI.memoperands()) {
    if (MMO)
      Locs.push_back(getMemLocFromMMO(*MMO));
  }
  return Locs;
}

static bool anyTaintedRegUse(const llvm::MachineInstr &MI,
                             const llvm::TaintState &S) {
  for (const llvm::MachineOperand &MO : MI.uses()) {
    if (MO.isReg()) {
      llvm::Register R = MO.getReg();
      if (R.isValid() && S.isTainted(R))
        return true;
    }
  }
  return false;
}

static void taintAllRegDefs(const llvm::MachineInstr &MI, llvm::TaintState &S,
                            const TargetRegisterInfo *TRI) {
  for (const llvm::MachineOperand &MO : MI.defs()) {
    if (MO.isReg()) {
      llvm::Register R = MO.getReg();
      if (R.isValid()) {
        S.setTainted(R);
        LLVM_DEBUG(dbgs() << "      set " << printReg(MO.getReg(), TRI)
                          << " as tainted\n");
      }
    }
  }
}

static void propagateTaintMI(const llvm::MachineInstr &MI, TaintState &S,
                             const llvm::TargetRegisterInfo *TRI) {
  // Reg -> Reg
  if (anyTaintedRegUse(MI, S)) {
    taintAllRegDefs(MI, S, TRI);
  }

  SmallVector<MemLoc, 2> Mems = getMemLocs(MI);
  bool HasMem = !Mems.empty();

  if (MI.mayStore() && anyTaintedRegUse(MI, S)) {
    if (!HasMem) {
      S.UnknownMemTainted = true;
      dbgs() << "      taint unknown mem (store)\n";
    } else {
      for (const MemLoc &L : Mems) {
        S.setTaintedMem(L);
        LLVM_DEBUG({
          if (L.K == MemLoc::IRValue)
            dbgs() << "      taint mem @" << L.V->getName() << " (store)\n";
          else
            dbgs() << "      taint unknown mem (store)\n";
        });
      }
    }
  }

  if (MI.mayLoad()) {
    bool LoadFromTainted = false;
    if (!HasMem) {
      LoadFromTainted = S.UnknownMemTainted;
    } else {
      for (const MemLoc &L : Mems)
        LoadFromTainted |= S.isTaintedMem(L);
    }

    if (LoadFromTainted) {
      LLVM_DEBUG(dbgs() << "      taint defs (load)\n");
      taintAllRegDefs(MI, S, TRI);
    }
  }
}

static TaintState propagateTaintMBB(const MachineBasicBlock &MBB,
                                    const TaintState &In,
                                    const TargetRegisterInfo *TRI) {
  TaintState Out = In;
  for (const auto &MI : MBB) {
    propagateTaintMI(MI, Out, TRI);
  }
  return Out;
}

TaintResult TaintAnalysis::run(MachineFunction &MF,
                               MachineFunctionAnalysisManager &MFAM) {
  const Function &F = MF.getFunction();
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();

  LLVM_DEBUG(dbgs() << "\nTaintAnalysis: analyzing function " << F.getName()
                    << "\n");

  // Collect which argument indices are tainted based on IR attributes
  SmallVector<unsigned, 4> TaintedArgIndices;
  for (const Argument &Arg : F.args()) {
    if (Arg.hasAttribute("tainted")) {
      TaintedArgIndices.push_back(Arg.getArgNo());
      LLVM_DEBUG(dbgs() << "  IR arg " << Arg.getArgNo()
                        << " has 'tainted' attribute\n");
    }
  }

  if (TaintedArgIndices.empty()) {
    LLVM_DEBUG(dbgs() << "  No tainted arguments found\n");
    return TaintResult{TaintState{},
                       DenseMap<const MachineBasicBlock *, TaintState>{}};
  }

  TaintState Seed;

  // Map tainted argument indices to virtual registers via liveins.
  //
  // For simple scalar arguments, the order of liveins corresponds to argument
  // order (based on calling convention). This is a simplification that works
  // for basic integer/pointer arguments.
  unsigned LiveInIdx = 0;
  for (const auto &[PhysReg, VirtReg] : MRI.liveins()) {
    if (llvm::is_contained(TaintedArgIndices, LiveInIdx)) {
      if (VirtReg.isValid()) {
        Seed.setTainted(VirtReg);
        LLVM_DEBUG(dbgs() << "  Marked virtual register "
                          << printReg(VirtReg, TRI) << " as tainted (from arg "
                          << LiveInIdx << ", phys " << printReg(PhysReg, TRI)
                          << ")\n");
      } else {
        Seed.setTainted(PhysReg);
        LLVM_DEBUG(dbgs() << "  Marked physical register "
                          << printReg(PhysReg, TRI) << " as tainted (from arg "
                          << LiveInIdx << ")\n");
      }
    }
    ++LiveInIdx;
  }

  DenseMap<const MachineBasicBlock *, TaintState> IN, OUT;

  for (auto &MBB : MF) {
    IN[&MBB] = TaintState{};
    OUT[&MBB] = TaintState{};
  }

  IN[&MF.front()] = Seed;
  SmallVector<const llvm::MachineBasicBlock *, 32> WorkQ;
  SmallPtrSet<const llvm::MachineBasicBlock *, 32> InQ;

  auto push = [&](const llvm::MachineBasicBlock *B) {
    if (InQ.insert(B).second)
      WorkQ.push_back(B);
  };

  push(&MF.front());

  while (!WorkQ.empty()) {
    const llvm::MachineBasicBlock *B = WorkQ.pop_back_val();
    InQ.erase(B);

    LLVM_DEBUG(dbgs() << "    " << printMBBReference(*B) << "\n");

    // Recompute IN[B] from preds, but preserve seed for entry.
    TaintState NewIn;
    if (B == &MF.front()) {
      NewIn = Seed;
    } else {
      bool First = true;
      for (const llvm::MachineBasicBlock *P : B->predecessors()) {
        if (First) {
          NewIn = OUT[P];
          First = false;
        } else {
          NewIn.join(OUT[P]);
        }
      }
      if (First) {
        // Unreachable block: keep empty IN
        NewIn = TaintState{};
      }
    }

    // If IN changed, update and recompute OUT
    bool InChanged = (NewIn != IN[B]);
    if (InChanged)
      IN[B] = std::move(NewIn);

    // Compute OUT from IN
    TaintState NewOut = propagateTaintMBB(*B, IN[B], TRI);

    if (NewOut != OUT[B]) {
      OUT[B] = std::move(NewOut);
      for (const llvm::MachineBasicBlock *S : B->successors())
        push(S);
    } else if (InChanged) {
      // IN changed but OUT did not (rare but possible); still safe to push
      // succs
      for (const llvm::MachineBasicBlock *S : B->successors())
        push(S);
    }
  }

  TaintState Result;

  for (auto &MBB : MF) {
    Result.join(OUT[&MBB]);
  }

  LLVM_DEBUG(dbgs() << "Total tainted regs: " << Result.countRegs()
                    << ", tainted FIs: " << Result.countFIs()
                    << ", total: " << Result.count() << "\n");

  return TaintResult{std::move(Result), std::move(IN)};
}

PreservedAnalyses TaintAnalysisPass::run(MachineFunction &MF,
                                         MachineFunctionAnalysisManager &MFAM) {
  auto &TR = MFAM.getResult<TaintAnalysis>(MF);

  LLVM_DEBUG({
    if (!TR.Merged.empty()) {
      dbgs() << "TaintAnalysisPass: " << MF.getName() << " has "
             << TR.Merged.count() << " tainted register(s)\n";
    }
  });

  // Export tainted instructions to file if requested.
  if (!TaintOutputFile.empty() && !TR.Merged.empty()) {
    const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();

    // Open file in append mode so multiple functions accumulate.
    std::error_code EC;
    raw_fd_ostream OS(TaintOutputFile, EC, sys::fs::OF_Append);
    if (EC) {
      errs() << "Error opening taint output file: " << EC.message() << "\n";
    } else {
      OS << "# Function: " << MF.getName() << "\n";

      for (const auto &MBB : MF) {
        // Start from the entry state for this BB.
        auto It = TR.IN.find(&MBB);
        TaintState S = (It != TR.IN.end()) ? It->second : TaintState{};

        for (const auto &MI : MBB) {
          bool UsesTainted = anyTaintedRegUse(MI, S);

          // Propagate taint through this instruction.
          propagateTaintMI(MI, S, TRI);

          // Check if any def became tainted.
          bool DefsTainted = false;
          for (const MachineOperand &MO : MI.defs()) {
            if (MO.isReg() && MO.getReg().isValid() &&
                S.isTainted(MO.getReg())) {
              DefsTainted = true;
              break;
            }
          }

          if (UsesTainted || DefsTainted) {
            OS << MF.getName() << "\t" << MBB.getName() << "\t";
            MI.print(OS, /*IsStandalone=*/true);
          }
        }
      }
    }
  }

  // Analysis pass doesn't modify the IR
  return getMachineFunctionPassPreservedAnalyses();
}
