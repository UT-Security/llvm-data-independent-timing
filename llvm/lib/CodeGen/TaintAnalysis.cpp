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
#include "llvm/Support/Debug.h"

#include <optional>

using namespace llvm;

#define DEBUG_TYPE "taint-analysis"

AnalysisKey TaintAnalysis::Key;

static std::optional<int> getFrameIndexIfAny(const llvm::MachineInstr &MI) {
  for (const llvm::MachineOperand &MO : MI.operands()) {
    if (MO.isFI())
      return MO.getIndex();
  }
  return std::nullopt;
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

  std::optional<int> FI = getFrameIndexIfAny(MI);
  if (!FI)
    return;

  if (MI.mayStore()) {
    if (anyTaintedRegUse(MI, S)) {
      LLVM_DEBUG(dbgs() << "      set FI#" << *FI << " (store)\n");
      S.setTaintedFI(*FI);
    }
  }

  if (MI.mayLoad()) {
    if (S.isTaintedFI(*FI)) {
      LLVM_DEBUG(llvm::dbgs()
                 << "      taint defs (load from FI#" << *FI << ")\n");
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

TaintState TaintAnalysis::run(MachineFunction &MF,
                              MachineFunctionAnalysisManager &MFAM) {
  const Function &F = MF.getFunction();
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();

  LLVM_DEBUG(dbgs() << "TaintAnalysis: analyzing function " << F.getName()
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
    return TaintState{};
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
                          << printReg(VirtReg, TRI)
                          << " as tainted (from arg " << LiveInIdx << ", phys "
                          << printReg(PhysReg, TRI) << ")\n");
      } else {
        Seed.setTainted(PhysReg);
        LLVM_DEBUG(dbgs() << "  Marked physical register "
                          << printReg(PhysReg, TRI)
                          << " as tainted (from arg " << LiveInIdx << ")\n");
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

    LLVM_DEBUG(dbgs() << "    " << B->getName() << "\n");

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
                    << ", total: " << Result.count() << "\n\n");

  return Result;
}

PreservedAnalyses TaintAnalysisPass::run(MachineFunction &MF,
                                         MachineFunctionAnalysisManager &MFAM) {
  auto &TI = MFAM.getResult<TaintAnalysis>(MF);

  LLVM_DEBUG({
    if (!TI.empty()) {
      dbgs() << "TaintAnalysisPass: " << MF.getName() << " has " << TI.count()
             << " tainted register(s)\n";
    }
  });

  // Analysis pass doesn't modify the IR
  return getMachineFunctionPassPreservedAnalyses();
}
