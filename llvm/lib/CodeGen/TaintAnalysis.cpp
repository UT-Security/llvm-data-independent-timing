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
#include "llvm/CodeGen/PseudoSourceValue.h"
#include "llvm/CodeGen/TaintSummaryInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include <set>

using namespace llvm;

#define DEBUG_TYPE "taint-analysis"

AnalysisKey TaintAnalysis::Key;

// Shared command-line option (declared extern in TaintAnalysis.h).
// Must use llvm:: to match the extern declaration inside namespace llvm.
cl::opt<std::string>
    llvm::TaintOutputFile("taint-output",
                          cl::desc("Output file for tainted instructions (TSV)"),
                          cl::value_desc("file"));

/// Unified cell extraction from a MachineMemOperand.
/// Returns the base kind (Stack/Global/Unknown), offset, and optional size.
struct CellInfo {
  enum Kind { Stack, Global, Unknown };
  Kind K = Unknown;
  int FI = 0;
  const GlobalVariable *GV = nullptr;
  int64_t Offset = 0;
  std::optional<uint64_t> Size; // nullopt if unknown/scalable
};

static CellInfo getCellFromMMO(const MachineMemOperand &MMO) {
  CellInfo CI;
  CI.Offset = MMO.getOffset();

  LocationSize LS = MMO.getSize();
  if (LS.hasValue() && LS.isPrecise() && !LS.isScalable())
    CI.Size = LS.getValue().getFixedValue();

  if (auto *FSV = dyn_cast_or_null<FixedStackPseudoSourceValue>(
          MMO.getPseudoValue())) {
    CI.K = CellInfo::Stack;
    CI.FI = FSV->getFrameIndex();
    return CI;
  }
  if (const Value *V = MMO.getValue()) {
    if (auto *GV = dyn_cast<GlobalVariable>(getUnderlyingObject(V))) {
      CI.K = CellInfo::Global;
      CI.GV = GV;
      return CI;
    }
  }
  return CI; // Unknown
}

// Public: declared in TaintAnalysis.h — used by TaintInterprocPass
bool llvm::anyTaintedRegUse(const MachineInstr &MI, const TaintState &S) {
  for (const MachineOperand &MO : MI.uses()) {
    if (MO.isReg()) {
      Register R = MO.getReg();
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

// Public: declared in TaintAnalysis.h — used by TaintInterprocPass
const Function *llvm::findCalledFunction(Module &M, const MachineInstr &MI) {
  if (!MI.isCall())
    return nullptr;

  // Iterate through operands to find the callee
  for (const MachineOperand &MO : MI.operands()) {
    // Direct call to a global function
    if (MO.isGlobal()) {
      if (const Function *F = dyn_cast<Function>(MO.getGlobal())) {
        return F;
      }
    }
    // Direct call to an external symbol (e.g., library function)
    if (MO.isSymbol()) {
      StringRef SymName = MO.getSymbolName();
      return M.getFunction(SymName);
    }
  }

  // Indirect call (BLR) or couldn't determine callee
  return nullptr;
}

// Public: declared in TaintAnalysis.h — used by TaintInterprocPass for export
void llvm::propagateTaintMI(const MachineInstr &MI, TaintState &S,
                            const TargetRegisterInfo *TRI,
                            const TaintSummaryInfo *TSI, Module *M) {
  // Reg -> Reg
  if (anyTaintedRegUse(MI, S)) {
    taintAllRegDefs(MI, S, TRI);
  }

  // Store handling: track taint into stack/global cells precisely.
  if (MI.mayStore()) {
    bool DataTainted = anyTaintedRegUse(MI, S);
    for (MachineMemOperand *MMO : MI.memoperands()) {
      if (!MMO)
        continue;
      CellInfo CI = getCellFromMMO(*MMO);
      if (CI.K == CellInfo::Stack) {
        if (CI.Size) {
          // Exact cell: strong update.
          if (DataTainted) {
            S.setTaintedStackCell(CI.FI, CI.Offset, *CI.Size);
            LLVM_DEBUG(dbgs() << "      taint stack cell FI=" << CI.FI
                              << " off=" << CI.Offset << " sz=" << *CI.Size
                              << " (store)\n");
          } else {
            S.clearTaintedStackCell(CI.FI, CI.Offset, *CI.Size);
            LLVM_DEBUG(dbgs() << "      clear stack cell FI=" << CI.FI
                              << " off=" << CI.Offset << " sz=" << *CI.Size
                              << " (store untainted)\n");
          }
        } else if (DataTainted) {
          // Unknown size: insert sentinel (size=0), no strong update.
          S.setTaintedStackCell(CI.FI, CI.Offset, 0);
          LLVM_DEBUG(dbgs() << "      taint stack cell FI=" << CI.FI
                            << " off=" << CI.Offset
                            << " sz=unknown (store)\n");
        }
      } else if (CI.K == CellInfo::Global) {
        if (CI.Size) {
          if (DataTainted) {
            S.setTaintedGlobalCell(CI.GV, CI.Offset, *CI.Size);
            LLVM_DEBUG(dbgs() << "      taint global cell " << CI.GV->getName()
                              << " off=" << CI.Offset << " sz=" << *CI.Size
                              << " (store)\n");
          } else {
            S.clearTaintedGlobalCell(CI.GV, CI.Offset, *CI.Size);
            LLVM_DEBUG(dbgs() << "      clear global cell "
                              << CI.GV->getName() << " off=" << CI.Offset
                              << " sz=" << *CI.Size
                              << " (store untainted)\n");
          }
        } else if (DataTainted) {
          S.setTaintedGlobalCell(CI.GV, CI.Offset, 0);
          LLVM_DEBUG(dbgs() << "      taint global cell " << CI.GV->getName()
                            << " off=" << CI.Offset
                            << " sz=unknown (store)\n");
        }
      } else if (DataTainted) {
        // Unknown/heap: conservative.
        S.UnknownMemTainted = true;
        LLVM_DEBUG(dbgs() << "      taint unknown mem (non-stack "
                             "non-global store)\n");
      }
    }
    if (MI.memoperands_empty() && DataTainted) {
      S.UnknownMemTainted = true;
      LLVM_DEBUG(dbgs() << "      taint unknown mem (store, no MMO)\n");
    }
  }

  // Load handling: stack and globals use cell-level precision; unknown is
  // conservative.
  if (MI.mayLoad()) {
    bool ShouldTaint = false;
    bool AllKnown = !MI.memoperands_empty();

    for (MachineMemOperand *MMO : MI.memoperands()) {
      if (!MMO) {
        AllKnown = false;
        continue;
      }
      CellInfo CI = getCellFromMMO(*MMO);
      if (CI.K == CellInfo::Stack) {
        bool Tainted =
            CI.Size ? S.isTaintedStackCell(CI.FI, CI.Offset, *CI.Size)
                    : S.anyTaintedStackCellForFI(CI.FI);
        if (Tainted) {
          ShouldTaint = true;
          LLVM_DEBUG(dbgs() << "      load from tainted stack cell FI="
                            << CI.FI << " off=" << CI.Offset << "\n");
        } else {
          LLVM_DEBUG(dbgs() << "      load from untainted stack cell FI="
                            << CI.FI << " off=" << CI.Offset << "\n");
        }
      } else if (CI.K == CellInfo::Global) {
        if (S.UnknownMemTainted) {
          ShouldTaint = true;
          LLVM_DEBUG(dbgs() << "      load from global " << CI.GV->getName()
                            << " (poisoned by unknown mem)\n");
        } else {
          bool Tainted =
              CI.Size ? S.isTaintedGlobalCell(CI.GV, CI.Offset, *CI.Size)
                      : S.anyTaintedGlobalCellForGV(CI.GV);
          if (Tainted) {
            ShouldTaint = true;
            LLVM_DEBUG(dbgs() << "      load from tainted global cell "
                              << CI.GV->getName() << " off=" << CI.Offset
                              << "\n");
          } else {
            LLVM_DEBUG(dbgs() << "      load from untainted global cell "
                              << CI.GV->getName() << " off=" << CI.Offset
                              << "\n");
          }
        }
      } else {
        // Unknown: conservative.
        AllKnown = false;
      }
    }

    // Unknown memory or no MMOs: conservative (taint).
    if (!AllKnown)
      ShouldTaint = true;

    if (ShouldTaint)
      taintAllRegDefs(MI, S, TRI);
  }

  // NEW: Handle function calls for interprocedural taint propagation
  // This is the KEY addition for making the analysis interprocedural!
  if (MI.isCall() && TSI && M) {
    LLVM_DEBUG(dbgs() << "      handling call instruction\n");

    // Step 1: Find the callee function
    const Function *Callee = findCalledFunction(*M, MI);

    if (Callee && !Callee->isDeclaration()) {
      // Direct call to a known function with a body
      FunctionTaintSummary Summary = TSI->getSummary(*Callee);

      // Step 2: Check if we're passing any tainted arguments
      // AArch64 uses X0-X7 for integer arguments (W0-W7 for 32-bit)
      // TODO: Get registers from calling convention instead of hardcoding
      bool PassingTaintedArg = false;
      for (unsigned RegID : {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}) {
        if (S.isTainted(RegID)) {
          PassingTaintedArg = true;
          LLVM_DEBUG(dbgs() << "        passing tainted arg in reg " << RegID << "\n");
          break;
        }
      }

      // Step 3: If passing tainted arg AND callee returns tainted, taint return reg
      if (PassingTaintedArg && Summary.ReturnsTainted) {
        // X0 is return register (W0 for 32-bit)
        S.setTainted(1);  // X0 - TODO: Get from calling convention
        S.setTainted(2);  // W0
        LLVM_DEBUG(dbgs() << "        call to " << Callee->getName()
                         << " returns tainted value\n");
      }
    } else {
      // Conservative: External function or indirect call
      // If any arg register is tainted, assume return is tainted
      bool HasTaintedArg = false;
      for (unsigned RegID : {1, 2}) {  // X0, W0 (first arg)
        if (S.isTainted(RegID)) {
          HasTaintedArg = true;
          break;
        }
      }

      if (HasTaintedArg) {
        S.setTainted(1);  // X0
        S.setTainted(2);  // W0
        if (Callee)
          LLVM_DEBUG(dbgs() << "        conservative: external call to "
                           << Callee->getName() << " taints return\n");
        else
          LLVM_DEBUG(dbgs() << "        conservative: indirect call taints return\n");
      }
    }
  }
}

static TaintState propagateTaintMBB(const MachineBasicBlock &MBB,
                                    const TaintState &In,
                                    const TargetRegisterInfo *TRI,
                                    const TaintSummaryInfo *TSI = nullptr,
                                    Module *M = nullptr) {
  TaintState Out = In;
  for (const auto &MI : MBB) {
    propagateTaintMI(MI, Out, TRI, TSI, M);
  }
  return Out;
}

/// Core implementation: runs intraprocedural taint analysis on a single
/// MachineFunction.  TSI (may be nullptr) provides:
///   - extra tainted-arg indices discovered by the module pass (caller→callee)
///   - callee return-taint summaries (callee→caller, via propagateTaintMI)
TaintResult TaintAnalysis::run(MachineFunction &MF,
                               const TaintSummaryInfo *TSI) {
  const Function &F = MF.getFunction();
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  Module *M = const_cast<Module *>(F.getParent());

  if (TSI) {
    LLVM_DEBUG(dbgs() << "TaintAnalysis: interprocedural mode (TSI)\n");
  } else {
    LLVM_DEBUG(dbgs() << "TaintAnalysis: intraprocedural mode (no TSI)\n");
  }

  LLVM_DEBUG(dbgs() << "\nTaintAnalysis: analyzing function " << F.getName()
                    << "\n");

  // Collect which argument indices are tainted.
  // Two sources: (1) IR "tainted" attributes, (2) interprocedural TSI.
  SmallVector<unsigned, 4> TaintedArgIndices;

  // Source 1: IR attributes (set by taint-annotate pass on the caller funcs)
  for (const Argument &Arg : F.args()) {
    if (Arg.hasAttribute("tainted")) {
      TaintedArgIndices.push_back(Arg.getArgNo());
      LLVM_DEBUG(dbgs() << "  IR arg " << Arg.getArgNo()
                        << " has 'tainted' attribute\n");
    }
  }

  // Source 2: Interprocedural summaries — the module pass stores
  // "identity arg 0 is tainted" in TSI when it sees caller_simple
  // passing a tainted register to identity's first argument.
  if (TSI && TSI->hasSummary(F)) {
    FunctionTaintSummary Summary = TSI->getSummary(F);
    for (unsigned Idx : Summary.TaintedArgIndices) {
      if (!llvm::is_contained(TaintedArgIndices, Idx)) {
        TaintedArgIndices.push_back(Idx);
        LLVM_DEBUG(dbgs() << "  TSI arg " << Idx
                          << " tainted (interprocedural)\n");
      }
    }
  }

  if (TaintedArgIndices.empty()) {
    LLVM_DEBUG(dbgs() << "  No tainted arguments found\n");
    return TaintResult{TaintState{},
                       DenseMap<const MachineBasicBlock *, TaintState>{}};
  }

  TaintState Seed;

  // Map tainted argument indices to virtual registers via liveins.
  // LiveIn ordering matches argument ordering (calling convention).
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
  SmallVector<const MachineBasicBlock *, 32> WorkQ;
  SmallPtrSet<const MachineBasicBlock *, 32> InQ;

  auto push = [&](const MachineBasicBlock *B) {
    if (InQ.insert(B).second)
      WorkQ.push_back(B);
  };

  push(&MF.front());

  while (!WorkQ.empty()) {
    const MachineBasicBlock *B = WorkQ.pop_back_val();
    InQ.erase(B);

    LLVM_DEBUG(dbgs() << "    " << printMBBReference(*B) << "\n");

    TaintState NewIn;
    if (B == &MF.front()) {
      NewIn = Seed;
    } else {
      bool First = true;
      for (const MachineBasicBlock *P : B->predecessors()) {
        if (First) {
          NewIn = OUT[P];
          First = false;
        } else {
          NewIn.join(OUT[P]);
        }
      }
      if (First)
        NewIn = TaintState{};
    }

    bool InChanged = (NewIn != IN[B]);
    if (InChanged)
      IN[B] = std::move(NewIn);

    TaintState NewOut = propagateTaintMBB(*B, IN[B], TRI, TSI, M);

    if (NewOut != OUT[B]) {
      OUT[B] = std::move(NewOut);
      for (const MachineBasicBlock *Succ : B->successors())
        push(Succ);
    } else if (InChanged) {
      for (const MachineBasicBlock *Succ : B->successors())
        push(Succ);
    }
  }

  TaintState Result;
  for (auto &MBB : MF)
    Result.join(OUT[&MBB]);

  LLVM_DEBUG(dbgs() << "Total tainted regs: " << Result.countRegs()
                    << ", tainted cells: " << Result.countCells()
                    << ", total: " << Result.count() << "\n");

  return TaintResult{std::move(Result), std::move(IN)};
}

/// Pass-manager entry point.  Extracts TSI from the MFAM proxy (if the
/// module-level TaintSummaryAnalysis has already been computed) and
/// delegates to the core run(MF, TSI).
TaintResult TaintAnalysis::run(MachineFunction &MF,
                               MachineFunctionAnalysisManager &MFAM) {
  const TaintSummaryInfo *TSI = nullptr;
  Module *M = const_cast<Module *>(MF.getFunction().getParent());
  if (auto *Proxy =
          MFAM.getCachedResult<ModuleAnalysisManagerMachineFunctionProxy>(MF)) {
    TSI = Proxy->getCachedResult<TaintSummaryAnalysis>(*M);
  }
  return run(MF, TSI);
}

//===----------------------------------------------------------------------===//
// exportTaintedInstructions — shared helper for writing results to file
//===----------------------------------------------------------------------===//

/// Replays taint propagation instruction-by-instruction using the per-BB
/// IN states from TaintResult, and writes every tainted instruction to OS.
/// If SrcOS is non-null, also writes tainted C source lines there.
void llvm::exportTaintedInstructions(MachineFunction &MF,
                                     const TaintResult &TR,
                                     const TaintSummaryInfo *TSI,
                                     raw_ostream &OS, raw_ostream *SrcOS,
                                     FunctionTaintStats *Stats) {
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  Module *M = const_cast<Module *>(MF.getFunction().getParent());

  std::set<std::tuple<std::string, unsigned, std::string>> TaintedSourceLines;

  // Stats accumulators.
  unsigned TotalInstr = 0, TaintedInstr = 0;
  SmallVector<unsigned, 64> TaintedPositions;

  struct BBStats {
    StringRef Name;
    unsigned Tainted = 0;
    unsigned Untainted = 0;
  };
  SmallVector<BBStats, 16> PerBB;

  struct Run {
    bool IsTainted;
    unsigned Length;
  };
  SmallVector<Run, 32> Runs;

  // Instruction type categories.
  enum InstrCat { Load, Store, ALU, Branch, Call, Copy, Debug, Other, NumCats };
  static const char *CatNames[] = {"Load",  "Store", "ALU",   "Branch",
                                   "Call",  "Copy",  "Debug", "Other"};
  unsigned TaintedByCat[NumCats] = {};
  unsigned UntaintedByCat[NumCats] = {};

  auto classifyMI = [](const MachineInstr &MI) -> InstrCat {
    if (MI.isDebugInstr())
      return Debug;
    if (MI.isCall())
      return Call;
    if (MI.isBranch() || MI.isReturn())
      return Branch;
    if (MI.mayLoad() && MI.mayStore())
      return Store; // read-modify-write counts as store
    if (MI.mayLoad())
      return Load;
    if (MI.mayStore())
      return Store;
    if (MI.isCopy() || MI.isMoveImmediate())
      return Copy;
    if (MI.isPseudo())
      return Other;
    return ALU;
  };

  OS << "# Function: " << MF.getName() << "\n";

  for (const auto &MBB : MF) {
    auto It = TR.IN.find(&MBB);
    TaintState S = (It != TR.IN.end()) ? It->second : TaintState{};

    if (Stats)
      PerBB.push_back({MBB.getName(), 0, 0});

    for (const auto &MI : MBB) {
      bool UsesTainted = anyTaintedRegUse(MI, S);

      propagateTaintMI(MI, S, TRI, TSI, M);

      bool DefsTainted = false;
      for (const MachineOperand &MO : MI.defs()) {
        if (MO.isReg() && MO.getReg().isValid() && S.isTainted(MO.getReg())) {
          DefsTainted = true;
          break;
        }
      }

      bool IsTainted = UsesTainted || DefsTainted;

      if (Stats) {
        ++TotalInstr;
        InstrCat Cat = classifyMI(MI);
        if (IsTainted) {
          ++TaintedInstr;
          TaintedPositions.push_back(TotalInstr - 1);
          PerBB.back().Tainted++;
          TaintedByCat[Cat]++;
        } else {
          PerBB.back().Untainted++;
          UntaintedByCat[Cat]++;
        }
        if (Runs.empty() || Runs.back().IsTainted != IsTainted)
          Runs.push_back({IsTainted, 1});
        else
          Runs.back().Length++;
      }

      if (IsTainted) {
        OS << MF.getName() << "\t" << MBB.getName() << "\t";
        MI.print(OS, /*IsStandalone=*/true);

        if (SrcOS) {
          if (const DebugLoc &DL = MI.getDebugLoc()) {
            if (DILocation *Loc = DL.get()) {
              std::string Filename = Loc->getFilename().str();
              unsigned Line = Loc->getLine();
              if (Line > 0 && !Filename.empty())
                TaintedSourceLines.insert(
                    std::make_tuple(Filename, Line, MF.getName().str()));
            }
          }
        }
      }
    }
  }

  if (SrcOS && !TaintedSourceLines.empty()) {
    *SrcOS << "# Function: " << MF.getName() << "\n";
    for (const auto &[Filename, Line, FuncName] : TaintedSourceLines)
      *SrcOS << Filename << ":" << Line << "\t" << FuncName << "\n";
  }

  // Buffer per-function stats into the FunctionTaintStats struct.
  if (Stats && TotalInstr > 0) {
    std::string Buf;
    raw_string_ostream St(Buf);

    St << "========================================="
          "=======================================\n";
    St << "Function: " << MF.getName() << "\n";
    St << "========================================="
          "=======================================\n\n";

    // Summary.
    unsigned UntaintedInstr = TotalInstr - TaintedInstr;
    double Ratio = (double)TaintedInstr / TotalInstr;
    St << "--- Summary ---\n";
    St << "  Tainted instructions:   " << TaintedInstr << "\n";
    St << "  Untainted instructions: " << UntaintedInstr << "\n";
    St << "  Total instructions:     " << TotalInstr << "\n";
    St << format("  Taint ratio:            %.3f\n", Ratio);

    // Instruction type breakdown.
    St << "\n--- Instruction type breakdown ---\n";
    St << format("  %-10s %8s %10s %6s\n", "Type", "Tainted", "Untainted",
                 "Total");
    for (unsigned C = 0; C < NumCats; ++C) {
      unsigned T = TaintedByCat[C] + UntaintedByCat[C];
      if (T == 0)
        continue;
      St << format("  %-10s %8u %10u %6u\n", CatNames[C], TaintedByCat[C],
                   UntaintedByCat[C], T);
    }

    // Distance between tainted instructions.
    St << "\n--- Distance between tainted instructions ---\n";
    if (TaintedPositions.size() >= 2) {
      unsigned MinGap = UINT_MAX, MaxGap = 0;
      double SumGap = 0;
      for (unsigned I = 1; I < TaintedPositions.size(); ++I) {
        unsigned Gap = TaintedPositions[I] - TaintedPositions[I - 1] - 1;
        MinGap = std::min(MinGap, Gap);
        MaxGap = std::max(MaxGap, Gap);
        SumGap += Gap;
      }
      double AvgGap = SumGap / (TaintedPositions.size() - 1);
      St << "  Min gap: " << MinGap << "\n";
      St << "  Max gap: " << MaxGap << "\n";
      St << format("  Avg gap: %.2f\n", AvgGap);
    } else {
      St << "  N/A (fewer than 2 tainted instructions)\n";
    }

    // Per-basic-block breakdown.
    St << "\n--- Per-basic-block breakdown ---\n";
    St << format("  %-20s %8s %10s %6s %6s\n", "BB", "Tainted", "Untainted",
                 "Total", "Ratio");
    for (const auto &B : PerBB) {
      unsigned Total = B.Tainted + B.Untainted;
      double R = Total ? (double)B.Tainted / Total : 0.0;
      std::string Name = B.Name.empty() ? "<unnamed>" : B.Name.str();
      St << format("  %-20s %8u %10u %6u %.3f\n", Name.c_str(), B.Tainted,
                   B.Untainted, Total, R);
    }

    // Taint density regions.
    St << "\n--- Taint density regions (run-length) ---\n";
    St << format("  %6s  %-10s %6s\n", "Region", "Type", "Length");
    unsigned LongestTainted = 0, LongestUntainted = 0, TaintedClusters = 0;
    for (unsigned I = 0; I < Runs.size(); ++I) {
      const char *Tag = Runs[I].IsTainted ? "tainted" : "untainted";
      St << format("  %6u  %-10s %6u\n", I + 1, Tag, Runs[I].Length);
      if (Runs[I].IsTainted) {
        LongestTainted = std::max(LongestTainted, Runs[I].Length);
        ++TaintedClusters;
      } else {
        LongestUntainted = std::max(LongestUntainted, Runs[I].Length);
      }
    }
    St << "  Longest tainted run:   " << LongestTainted << "\n";
    St << "  Longest untainted run: " << LongestUntainted << "\n";
    St << "  Number of tainted clusters: " << TaintedClusters << "\n\n";

    Stats->Output = St.str();
    Stats->TaintRatio = Ratio;
  }
}

//===----------------------------------------------------------------------===//
// TaintAnalysisPass — standalone MachineFunction pass (uses the helper above)
//===----------------------------------------------------------------------===//

PreservedAnalyses TaintAnalysisPass::run(MachineFunction &MF,
                                         MachineFunctionAnalysisManager &MFAM) {
  auto &TR = MFAM.getResult<TaintAnalysis>(MF);

  Module *M = const_cast<Module *>(MF.getFunction().getParent());
  const TaintSummaryInfo *TSI = nullptr;
  if (auto *Proxy =
          MFAM.getCachedResult<ModuleAnalysisManagerMachineFunctionProxy>(MF)) {
    TSI = Proxy->getCachedResult<TaintSummaryAnalysis>(*M);
  }

  LLVM_DEBUG({
    if (!TR.Merged.empty())
      dbgs() << "TaintAnalysisPass: " << MF.getName() << " has "
             << TR.Merged.count() << " tainted register(s)\n";
  });

  if (!TaintOutputFile.empty() && !TR.Merged.empty()) {
    std::error_code EC;
    raw_fd_ostream OS(TaintOutputFile, EC, sys::fs::OF_Append);
    if (EC) {
      errs() << "Error opening taint output file: " << EC.message() << "\n";
    } else {
      SmallString<256> SrcPath(TaintOutputFile);
      std::string Ext = sys::path::extension(SrcPath).str();
      sys::path::replace_extension(SrcPath, "");
      SrcPath += "_src";
      SrcPath += Ext;

      std::error_code SrcEC;
      raw_fd_ostream SrcOS(SrcPath, SrcEC, sys::fs::OF_Append);
      raw_ostream *SrcPtr = SrcEC ? nullptr : &SrcOS;
      if (SrcEC)
        errs() << "Error opening source output file: " << SrcEC.message()
               << "\n";

      SmallString<256> StatsPath(TaintOutputFile);
      std::string StatsExt = sys::path::extension(StatsPath).str();
      sys::path::replace_extension(StatsPath, "");
      StatsPath += "_stats";
      StatsPath += StatsExt;

      FunctionTaintStats Stats;
      exportTaintedInstructions(MF, TR, TSI, OS, SrcPtr, &Stats);

      if (!Stats.Output.empty()) {
        std::error_code StatsEC;
        raw_fd_ostream StatsOS(StatsPath, StatsEC, sys::fs::OF_Append);
        if (StatsEC)
          errs() << "Error opening stats output file: " << StatsEC.message()
                 << "\n";
        else
          StatsOS << Stats.Output;
      }
    }
  }

  return getMachineFunctionPassPreservedAnalyses();
}
