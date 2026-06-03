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
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/PseudoSourceValue.h"
#include "llvm/CodeGen/TaintSummaryInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>
#include <optional>
#include <set>

using namespace llvm;

#define DEBUG_TYPE "taint-analysis"

AnalysisKey TaintAnalysis::Key;

// Shared command-line option (declared extern in TaintAnalysis.h).
// Must use llvm:: to match the extern declaration inside namespace llvm.
cl::opt<std::string> llvm::TaintOutputFile(
    "taint-output", cl::desc("Output file for tainted instructions (TSV)"),
    cl::value_desc("file"));

cl::opt<std::string> llvm::TaintRegionsOutputFile(
    "taint-regions-output",
    cl::desc("Output file for barrier-protected taint regions"),
    cl::value_desc("file"));

cl::opt<std::string> llvm::TaintSourceRegionsOutputFile(
    "taint-source-regions-output",
    cl::desc("Output file for source-line taint regions"),
    cl::value_desc("file"));

cl::opt<bool> llvm::TaintInsertISB(
    "taint-insert-isb",
    cl::desc("Insert target instruction barriers around tainted instructions"),
    cl::init(false));

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

  if (auto *FSV =
          dyn_cast_or_null<FixedStackPseudoSourceValue>(MMO.getPseudoValue())) {
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

/// A register represents a single physical register if all its
/// non-artificial subregs share the same hardware encoding.
/// True for $x0 (subregs: $w0 enc=0, $w0_hi enc=0xFFFF/artificial).
/// False for $w8_w9 (subregs: $w8 enc=8, $w9 enc=9 — different regs).
/// False for $x0_x1 (subregs: $x0 enc=0, $x1 enc=1 — different regs).
static bool isSinglePhysReg(MCPhysReg R, const TargetRegisterInfo *TRI) {
  unsigned ParentEnc = TRI->getEncodingValue(R);
  for (MCPhysReg SR : TRI->subregs(R)) {
    unsigned SubEnc = TRI->getEncodingValue(SR);
    if (SubEnc == 0xFFFF) // Skip artificial registers (e.g., W0_HI)
      continue;
    if (SubEnc != ParentEnc)
      return false;
  }
  return true;
}

/// Propagate a taint change to all simple aliases of R (W↔X),
/// skipping register tuples to avoid cascade.
static void setTaintWithAliases(Register R, TaintState &S,
                                const TargetRegisterInfo *TRI) {
  S.setTainted(R);
  if (!R.isPhysical())
    return;
  MCPhysReg Phys = R.asMCReg();
  // DOWN: e.g. $x0 → $w0, $w0_hi (skip if R is a tuple)
  if (isSinglePhysReg(Phys, TRI))
    for (MCPhysReg SR : TRI->subregs(Phys))
      S.setTainted(SR);
  // UP: e.g. $w0 → $x0 (skip tuple superregs)
  for (MCPhysReg Super : TRI->superregs(Phys))
    if (isSinglePhysReg(Super, TRI)) {
      S.setTainted(Super);
      for (MCPhysReg SR : TRI->subregs(Super))
        S.setTainted(SR);
    }
}

static void clearTaintWithAliases(Register R, TaintState &S,
                                  const TargetRegisterInfo *TRI) {
  S.clearTainted(R);
  if (!R.isPhysical())
    return;
  MCPhysReg Phys = R.asMCReg();
  if (isSinglePhysReg(Phys, TRI))
    for (MCPhysReg SR : TRI->subregs(Phys))
      S.clearTainted(SR);
  for (MCPhysReg Super : TRI->superregs(Phys))
    if (isSinglePhysReg(Super, TRI)) {
      S.clearTainted(Super);
      for (MCPhysReg SR : TRI->subregs(Super))
        S.clearTainted(SR);
    }
}

static void taintAllRegDefs(const llvm::MachineInstr &MI, llvm::TaintState &S,
                            const TargetRegisterInfo *TRI) {
  for (const llvm::MachineOperand &MO : MI.defs()) {
    if (MO.isReg()) {
      llvm::Register R = MO.getReg();
      if (R.isValid()) {
        setTaintWithAliases(R, S, TRI);
        LLVM_DEBUG(dbgs() << "      set " << printReg(MO.getReg(), TRI)
                          << " as tainted\n");
      }
    }
  }
}

static void clearAllRegDefs(const llvm::MachineInstr &MI, llvm::TaintState &S,
                            const TargetRegisterInfo *TRI) {
  for (const llvm::MachineOperand &MO : MI.defs()) {
    if (MO.isReg()) {
      llvm::Register R = MO.getReg();
      if (R.isValid()) {
        clearTaintWithAliases(R, S, TRI);
        LLVM_DEBUG(dbgs() << "      clear " << printReg(MO.getReg(), TRI)
                          << " (untainted def)\n");
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
  // Reg → Reg: skip for pure loads/stores (address use is DIT sink).
  // Keep for ALU and RMW (mayLoad && mayStore) for conservatism.
  bool IsPureLoad = MI.mayLoad() && !MI.mayStore();
  bool IsPureStore = !MI.mayLoad() && MI.mayStore();
  if (!IsPureLoad && !IsPureStore) {
    if (anyTaintedRegUse(MI, S))
      taintAllRegDefs(MI, S, TRI);
    else
      clearAllRegDefs(MI, S, TRI);
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
                            << " off=" << CI.Offset << " sz=unknown (store)\n");
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
            LLVM_DEBUG(dbgs() << "      clear global cell " << CI.GV->getName()
                              << " off=" << CI.Offset << " sz=" << *CI.Size
                              << " (store untainted)\n");
          }
        } else if (DataTainted) {
          S.setTaintedGlobalCell(CI.GV, CI.Offset, 0);
          LLVM_DEBUG(dbgs() << "      taint global cell " << CI.GV->getName()
                            << " off=" << CI.Offset << " sz=unknown (store)\n");
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

  // Load handling: stack and globals use cell-level precision; unknown/heap
  // uses pointer taint as a proxy (no cell tracking for heap).
  // Also check the module-level UnknownMemTainted flag from TSI for
  // cross-function heap safety (some other function may have stored
  // tainted data to heap that we load here).
  if (MI.mayLoad()) {
    bool ShouldTaint = false;
    bool HeapPoisoned =
        S.UnknownMemTainted || (TSI && TSI->hasUnknownMemTainted());

    for (MachineMemOperand *MMO : MI.memoperands()) {
      if (!MMO) {
        // No MMO info: use pointer taint or heap-poisoned flag as fallback.
        if (HeapPoisoned || anyTaintedRegUse(MI, S))
          ShouldTaint = true;
        continue;
      }
      CellInfo CI = getCellFromMMO(*MMO);
      if (CI.K == CellInfo::Stack) {
        bool Tainted = CI.Size
                           ? S.isTaintedStackCell(CI.FI, CI.Offset, *CI.Size)
                           : S.anyTaintedStackCellForFI(CI.FI);
        if (Tainted) {
          ShouldTaint = true;
          LLVM_DEBUG(dbgs() << "      load from tainted stack cell FI=" << CI.FI
                            << " off=" << CI.Offset << "\n");
        } else {
          LLVM_DEBUG(dbgs() << "      load from untainted stack cell FI="
                            << CI.FI << " off=" << CI.Offset << "\n");
        }
      } else if (CI.K == CellInfo::Global) {
        if (HeapPoisoned) {
          ShouldTaint = true;
          LLVM_DEBUG(dbgs() << "      load from global " << CI.GV->getName()
                            << " (poisoned by unknown mem)\n");
        } else {
          bool Tainted = CI.Size
                             ? S.isTaintedGlobalCell(CI.GV, CI.Offset, *CI.Size)
                             : S.anyTaintedGlobalCellForGV(CI.GV);
          if (Tainted) {
            ShouldTaint = true;
            LLVM_DEBUG(dbgs()
                       << "      load from tainted global cell "
                       << CI.GV->getName() << " off=" << CI.Offset << "\n");
          } else {
            LLVM_DEBUG(dbgs()
                       << "      load from untainted global cell "
                       << CI.GV->getName() << " off=" << CI.Offset << "\n");
          }
        }
      } else {
        // Unknown/heap: taint if the pointer register is tainted (proxy for
        // "this pointer was derived from secret data, so the pointed-to data
        // is likely secret"), or if any function stored tainted data to heap.
        if (HeapPoisoned || anyTaintedRegUse(MI, S)) {
          ShouldTaint = true;
          LLVM_DEBUG(dbgs() << "      load from tainted unknown/heap mem\n");
        } else {
          LLVM_DEBUG(dbgs() << "      load from untainted unknown/heap mem\n");
        }
      }
    }

    // No MMOs at all: use pointer taint or heap-poisoned flag.
    if (MI.memoperands_empty()) {
      if (HeapPoisoned || anyTaintedRegUse(MI, S))
        ShouldTaint = true;
    }

    if (ShouldTaint)
      taintAllRegDefs(MI, S, TRI);
    else
      clearAllRegDefs(MI, S, TRI);
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

      // If callee's summary says it returns tainted, taint the return register
      // unconditionally. The callee's ReturnsTainted already accounts for all
      // taint sources (args, heap loads, globals) — no need to gate on whether
      // the caller is passing tainted args.
      if (Summary.ReturnsTainted) {
        S.setTainted(1); // X0 - TODO: Get from calling convention
        S.setTainted(2); // W0
        LLVM_DEBUG(dbgs() << "        call to " << Callee->getName()
                          << " returns tainted value\n");
      }
    } else {
      // Conservative: External function or indirect call
      // If any arg register is tainted, assume return is tainted
      bool HasTaintedArg = false;
      for (unsigned RegID : {1, 2}) { // X0, W0 (first arg)
        if (S.isTainted(RegID)) {
          HasTaintedArg = true;
          break;
        }
      }

      if (HasTaintedArg) {
        S.setTainted(1); // X0
        S.setTainted(2); // W0
        if (Callee)
          LLVM_DEBUG(dbgs() << "        conservative: external call to "
                            << Callee->getName() << " taints return\n");
        else
          LLVM_DEBUG(dbgs()
                     << "        conservative: indirect call taints return\n");
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

static bool hasTaintedRegDef(const MachineInstr &MI, const TaintState &S) {
  for (const MachineOperand &MO : MI.defs())
    if (MO.isReg() && MO.getReg().isValid() && S.isTainted(MO.getReg()))
      return true;
  return false;
}

static bool isTaintedAfterPropagation(const MachineInstr &MI,
                                      const TaintState &S, bool UsesTainted) {
  return UsesTainted || hasTaintedRegDef(MI, S);
}

static bool isBarrierInsertionCandidate(const MachineInstr &MI) {
  return !MI.isDebugInstr() && !MI.isMetaInstruction() && !MI.isPosition();
}

namespace {

struct TaintedRun {
  MachineInstr *First = nullptr;
  MachineInstr *Last = nullptr;
  unsigned Count = 0;
  unsigned FirstIndex = 0;
  unsigned LastIndex = 0;
};

} // namespace

static SmallVector<TaintedRun, 64>
collectTaintedRuns(MachineFunction &MF, const TaintResult &TR,
                   const TaintSummaryInfo *TSI, unsigned &TaintedInstrCount) {
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  Module *M = const_cast<Module *>(MF.getFunction().getParent());

  SmallVector<TaintedRun, 64> TaintedRuns;
  TaintedInstrCount = 0;
  unsigned InstrIndex = 0;

  for (auto &MBB : MF) {
    auto It = TR.IN.find(&MBB);
    TaintState S = (It != TR.IN.end()) ? It->second : TaintState{};
    TaintedRun CurrentRun;

    auto FinishRun = [&]() {
      if (CurrentRun.First)
        TaintedRuns.push_back(CurrentRun);
      CurrentRun = TaintedRun{};
    };

    for (auto &MI : MBB) {
      bool UsesTainted = anyTaintedRegUse(MI, S);
      propagateTaintMI(MI, S, TRI, TSI, M);

      if (!isBarrierInsertionCandidate(MI))
        continue;

      ++InstrIndex;
      if (isTaintedAfterPropagation(MI, S, UsesTainted)) {
        if (!CurrentRun.First) {
          CurrentRun.First = &MI;
          CurrentRun.FirstIndex = InstrIndex;
        }
        CurrentRun.Last = &MI;
        CurrentRun.LastIndex = InstrIndex;
        ++CurrentRun.Count;
        ++TaintedInstrCount;
      } else {
        FinishRun();
      }
    }

    FinishRun();
  }

  return TaintedRuns;
}

static void printTaintedRuns(MachineFunction &MF,
                             ArrayRef<TaintedRun> TaintedRuns,
                             raw_ostream &OS) {
  unsigned RegionNo = 0;
  for (const TaintedRun &Run : TaintedRuns) {
    ++RegionNo;
    OS << "# Region " << RegionNo << " (" << MF.getName() << ")\n";

    for (auto It = Run.First->getIterator(),
              End = std::next(Run.Last->getIterator());
         It != End; ++It) {
      if (!isBarrierInsertionCandidate(*It))
        continue;
      OS << "    ";
      It->print(OS, /*IsStandalone=*/true);
    }
    OS << "\n";
  }
}

struct SourceRegion {
  std::string Filename;
  unsigned StartLine = 0;
  unsigned EndLine = 0;
  bool HasExitBarrier = false;
};

static std::optional<std::string> getResolvedDebugFilename(const DebugLoc &DL) {
  if (!DL)
    return std::nullopt;

  DILocation *Loc = DL.get();
  if (!Loc || Loc->getLine() == 0 || Loc->getFilename().empty())
    return std::nullopt;

  StringRef Filename = Loc->getFilename();
  if (sys::path::is_absolute(Filename))
    return Filename.str();

  SmallString<256> Path(Loc->getDirectory());
  if (!Path.empty())
    sys::path::append(Path, Filename);
  else
    Path = Filename;
  return std::string(Path);
}

static std::optional<SourceRegion> getSourceRegion(const TaintedRun &Run) {
  SourceRegion Region;
  Region.HasExitBarrier = !Run.Last->isTerminator();

  for (auto It = Run.First->getIterator(),
            End = std::next(Run.Last->getIterator());
       It != End; ++It) {
    if (!isBarrierInsertionCandidate(*It))
      continue;

    std::optional<std::string> Filename =
        getResolvedDebugFilename(It->getDebugLoc());
    if (!Filename)
      continue;

    unsigned Line = It->getDebugLoc().getLine();
    if (Region.Filename.empty()) {
      Region.Filename = *Filename;
      Region.StartLine = Line;
      Region.EndLine = Line;
      continue;
    }

    if (Region.Filename != *Filename)
      return std::nullopt;

    Region.StartLine = std::min(Region.StartLine, Line);
    Region.EndLine = std::max(Region.EndLine, Line);
  }

  if (Region.Filename.empty())
    return std::nullopt;
  return Region;
}

static unsigned printTaintSourceRegions(ArrayRef<TaintedRun> TaintedRuns,
                                        StringRef FunctionName,
                                        raw_ostream &OS) {
  unsigned Emitted = 0;
  for (const TaintedRun &Run : TaintedRuns) {
    std::optional<SourceRegion> Region = getSourceRegion(Run);
    if (!Region)
      continue;

    OS << Region->Filename << "\t" << Region->StartLine << "\t"
       << Region->EndLine << "\t" << FunctionName << "\t"
       << (Region->HasExitBarrier ? "DSB" : "none") << "\n";
    ++Emitted;
  }
  return Emitted;
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

  if (TaintedArgIndices.empty() && !TSI) {
    // Intraprocedural mode with no tainted args: nothing to analyze.
    // In interprocedural mode (TSI != nullptr), we must still analyze
    // because the function may receive taint via callee return values.
    LLVM_DEBUG(dbgs() << "  No tainted arguments found (intra mode)\n");
    return TaintResult{TaintState{},
                       DenseMap<const MachineBasicBlock *, TaintState>{}};
  }

  TaintState Seed;

  // Map tainted argument indices to registers via liveins.
  // Use the hardware register encoding to determine the argument index
  // (AArch64: X0/W0=0, X1/W1=1, ..., X7/W7=7), NOT the livein list order
  // which may differ from argument order in post-regalloc MIR.
  for (const auto &[PhysReg, VirtReg] : MRI.liveins()) {
    unsigned ArgIdx = TRI->getEncodingValue(PhysReg);
    if (llvm::is_contained(TaintedArgIndices, ArgIdx)) {
      if (VirtReg.isValid()) {
        Seed.setTainted(VirtReg);
        LLVM_DEBUG(dbgs() << "  Marked virtual register "
                          << printReg(VirtReg, TRI) << " as tainted (from arg "
                          << ArgIdx << ", phys " << printReg(PhysReg, TRI)
                          << ")\n");
      } else {
        setTaintWithAliases(PhysReg, Seed, TRI);
        LLVM_DEBUG(dbgs() << "  Marked physical register "
                          << printReg(PhysReg, TRI) << " as tainted (from arg "
                          << ArgIdx << ")\n");
      }
    }
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
void llvm::exportTaintedInstructions(MachineFunction &MF, const TaintResult &TR,
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
  static const char *CatNames[] = {"Load", "Store", "ALU",   "Branch",
                                   "Call", "Copy",  "Debug", "Other"};
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

      bool IsTainted = isTaintedAfterPropagation(MI, S, UsesTainted);

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

      // Print every instruction with taint state for per-instruction analysis.
      OS << MF.getName() << "\t" << MBB.getName() << "\t"
         << (IsTainted ? "TAINTED" : "clean") << "\t";
      MI.print(OS, /*IsStandalone=*/true);
      // Print tainted registers after the instruction.
      OS << "\t# tainted_regs:";
      for (const auto &RegID : S.TaintedRegs)
        OS << " " << printReg(Register(RegID), TRI);
      OS << "\n";

      if (IsTainted && SrcOS) {
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

unsigned llvm::exportTaintBarrierRegions(MachineFunction &MF,
                                         const TaintResult &TR,
                                         const TaintSummaryInfo *TSI,
                                         raw_ostream &OS) {
  unsigned TaintedInstrCount = 0;
  SmallVector<TaintedRun, 64> TaintedRuns =
      collectTaintedRuns(MF, TR, TSI, TaintedInstrCount);
  printTaintedRuns(MF, TaintedRuns, OS);
  return TaintedInstrCount;
}

unsigned llvm::exportTaintSourceRegions(MachineFunction &MF,
                                        const TaintResult &TR,
                                        const TaintSummaryInfo *TSI,
                                        raw_ostream &OS) {
  unsigned TaintedInstrCount = 0;
  SmallVector<TaintedRun, 64> TaintedRuns =
      collectTaintedRuns(MF, TR, TSI, TaintedInstrCount);
  return printTaintSourceRegions(TaintedRuns, MF.getName(), OS);
}

unsigned llvm::insertTaintBarriers(MachineFunction &MF, const TaintResult &TR,
                                   const TaintSummaryInfo *TSI,
                                   raw_ostream *RegionsOS) {
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  unsigned TaintedInstrCount = 0;
  SmallVector<TaintedRun, 64> TaintedRuns =
      collectTaintedRuns(MF, TR, TSI, TaintedInstrCount);

  if (RegionsOS)
    printTaintedRuns(MF, TaintedRuns, *RegionsOS);

  for (const TaintedRun &Run : TaintedRuns) {
    MachineBasicBlock &MBB = *Run.First->getParent();
    DebugLoc StartDL = Run.First->getDebugLoc();

    TII->insertInstructionBarrier(MBB, Run.First->getIterator(), StartDL);
    if (!Run.Last->isTerminator())
      TII->insertDataBarrier(MBB, std::next(Run.Last->getIterator()),
                             Run.Last->getDebugLoc());
  }

  return TaintedInstrCount;
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

  unsigned BarriersInserted = 0;
  if (!TR.Merged.empty() &&
      (TaintInsertISB || !TaintRegionsOutputFile.empty() ||
       !TaintSourceRegionsOutputFile.empty())) {
    std::unique_ptr<raw_fd_ostream> RegionsOS;
    if (!TaintRegionsOutputFile.empty()) {
      std::error_code EC;
      RegionsOS = std::make_unique<raw_fd_ostream>(TaintRegionsOutputFile, EC,
                                                   sys::fs::OF_Append);
      if (EC) {
        errs() << "Error opening taint regions output file: " << EC.message()
               << "\n";
        RegionsOS.reset();
      }
    }

    std::unique_ptr<raw_fd_ostream> SourceRegionsOS;
    if (!TaintSourceRegionsOutputFile.empty()) {
      std::error_code EC;
      SourceRegionsOS = std::make_unique<raw_fd_ostream>(
          TaintSourceRegionsOutputFile, EC, sys::fs::OF_Append);
      if (EC) {
        errs() << "Error opening taint source regions output file: "
               << EC.message() << "\n";
        SourceRegionsOS.reset();
      }
    }

    if (TaintInsertISB)
      BarriersInserted = insertTaintBarriers(MF, TR, TSI, RegionsOS.get());
    else if (RegionsOS)
      exportTaintBarrierRegions(MF, TR, TSI, *RegionsOS);

    if (SourceRegionsOS)
      exportTaintSourceRegions(MF, TR, TSI, *SourceRegionsOS);
  }

  if (BarriersInserted)
    return PreservedAnalyses::none();
  return getMachineFunctionPassPreservedAnalyses();
}
