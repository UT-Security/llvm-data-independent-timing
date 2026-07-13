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
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachinePassManager.h"
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

cl::opt<TaintBarrierKind> llvm::TaintBarrierMode(
    "taint-barrier-mode",
    cl::desc("Protection inserted around tainted code (with -taint-insert-isb)"),
    cl::init(TaintBarrierKind::ISB),
    cl::values(clEnumValN(TaintBarrierKind::ISB, "isb",
                          "Per-region ISB/DSB speculation barriers (default)"),
               clEnumValN(TaintBarrierKind::DIT, "dit",
                          "Function-granularity PSTATE.DIT data-independent "
                          "timing mode")));

cl::opt<std::string> llvm::TaintCallsiteReportFile(
    "taint-callsite-report",
    cl::desc("Output file for call sites passing secret data to callees the "
             "analysis cannot instrument (external declarations, indirect "
             "calls)"),
    cl::value_desc("file"));

static cl::opt<unsigned> TaintRegionMergeGap(
    "taint-region-merge-gap",
    cl::desc("Merge barrier-protected taint regions in the same basic block "
             "when separated by at most this many clean instructions"),
    cl::init(2));

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

static std::optional<MemoryLocation>
getMemoryLocationFromMMO(const MachineMemOperand &MMO) {
  const Value *V = MMO.getValue();
  if (!V)
    return std::nullopt;

  LocationSize LS = MMO.getSize();
  if (LS.hasValue() && LS.isPrecise() && !LS.isScalable())
    return MemoryLocation(V, LS, MMO.getAAInfo());

  return MemoryLocation::getBeforeOrAfter(V, MMO.getAAInfo());
}

/// True if the load described by MMO may read memory written by any of the
/// tainted unknown-memory stores in TaintedPtrs. Without AA, or without a
/// queryable location, any tainted store is assumed to reach the load.
static bool unknownMemMayTaintLoad(const MachineMemOperand &MMO,
                                   const DenseSet<const Value *> &TaintedPtrs,
                                   AAResults *AA) {
  if (TaintedPtrs.empty())
    return false;

  if (!AA)
    return true;

  std::optional<MemoryLocation> LoadLoc = getMemoryLocationFromMMO(MMO);
  if (!LoadLoc)
    return true;

  for (const Value *StorePtr : TaintedPtrs)
    if (!AA->isNoAlias(*LoadLoc, MemoryLocation::getBeforeOrAfter(StorePtr)))
      return true;

  return false;
}

static bool anyRegUseOfKind(TaintKind K, const MachineInstr &MI,
                            const TaintState &S) {
  for (const MachineOperand &MO : MI.uses())
    if (MO.isReg() && S.test(K, MO.getReg()))
      return true;
  return false;
}

static bool anyTaintedRegUse(const MachineInstr &MI, const TaintState &S) {
  return anyRegUseOfKind(TaintKind::Data, MI, S);
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

/// Propagate a taint change of kind K to all simple aliases of R (W↔X),
/// skipping register tuples to avoid cascade.
static void updateWithAliases(TaintKind K, Register R, TaintState &S,
                              const TargetRegisterInfo *TRI, bool Set) {
  S.update(K, R, Set);
  if (!R.isPhysical())
    return;
  MCPhysReg Phys = R.asMCReg();
  // DOWN: e.g. $x0 → $w0, $w0_hi (skip if R is a tuple)
  if (isSinglePhysReg(Phys, TRI))
    for (MCPhysReg SR : TRI->subregs(Phys))
      S.update(K, SR, Set);
  // UP: e.g. $w0 → $x0 (skip tuple superregs)
  for (MCPhysReg Super : TRI->superregs(Phys))
    if (isSinglePhysReg(Super, TRI)) {
      S.update(K, Super, Set);
      for (MCPhysReg SR : TRI->subregs(Super))
        S.update(K, SR, Set);
    }
}

/// Set or clear taint of kind K on every register defined by MI.
static void updateAllRegDefs(TaintKind K, const MachineInstr &MI, TaintState &S,
                             const TargetRegisterInfo *TRI, bool Set) {
  for (const MachineOperand &MO : MI.defs()) {
    if (!MO.isReg() || !MO.getReg().isValid())
      continue;
    updateWithAliases(K, MO.getReg(), S, TRI, Set);
    if (K == TaintKind::Data)
      LLVM_DEBUG(dbgs() << (Set ? "      set " : "      clear ")
                        << printReg(MO.getReg(), TRI)
                        << (Set ? " as tainted\n" : " (untainted def)\n"));
  }
}

/// How many of MI's leading register uses hold the value it writes to memory.
/// std::nullopt means the shape is unknown and every register use must be
/// treated as part of the stored value.
static std::optional<unsigned> getStoredValueRegCount(const MachineInstr &MI) {
  const MachineBasicBlock *MBB = MI.getParent();
  if (!MBB)
    return std::nullopt;
  return MBB->getParent()->getSubtarget().getInstrInfo()->getNumStoredValueRegs(
      MI);
}

/// True if the value MI stores to memory carries taint of kind K. Only the
/// value operands are considered — the address operands are a different sink.
/// Anything we cannot classify is over-approximated: a spurious barrier costs
/// performance, a missing one costs the secret.
static bool anyTaintedStoreDataRegUse(TaintKind K, const MachineInstr &MI,
                                      const TaintState &S) {
  if (!MI.mayStore())
    return false;

  std::optional<unsigned> StoredRegsRemaining = getStoredValueRegCount(MI);
  if (!StoredRegsRemaining)
    return anyRegUseOfKind(K, MI, S);

  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg() || !MO.isUse() || MO.isImplicit())
      continue;

    if (S.test(K, MO.getReg()))
      return true;

    if (--*StoredRegsRemaining == 0)
      break;
  }

  return false;
}

static bool isABIResultRegDef(const MachineOperand &MO,
                              const TargetRegisterInfo *TRI) {
  if (!MO.isReg() || !MO.isDef() || MO.isDead())
    return false;

  Register R = MO.getReg();
  if (!R.isValid() || !R.isPhysical())
    return false;

  // AArch64 assigns scalar/pointer return values to registers with hardware
  // encoding 0 (W0/X0, and likewise S0/D0/Q0 for FP/vector returns). This is
  // more robust than relying on LLVM's generated enum values for W0/X0.
  return TRI->getEncodingValue(R.asMCReg()) == 0;
}

static bool anyTaintedCallArgument(const MachineInstr &MI,
                                   const TaintState &S) {
  return MI.isCall() && anyTaintedRegUse(MI, S);
}

static void clearCallResultDefs(const MachineInstr &MI, TaintState &S,
                                const TargetRegisterInfo *TRI) {
  for (const MachineOperand &MO : MI.all_defs()) {
    if (!isABIResultRegDef(MO, TRI))
      continue;
    Register R = MO.getReg();
    for (TaintKind K :
         {TaintKind::Data, TaintKind::Pointee, TaintKind::Address})
      updateWithAliases(K, R, S, TRI, /*Set=*/false);
  }
}

static void taintCallResultDefs(const MachineInstr &MI, TaintState &S,
                                const TargetRegisterInfo *TRI) {
  for (const MachineOperand &MO : MI.all_defs()) {
    if (!isABIResultRegDef(MO, TRI))
      continue;
    updateWithAliases(TaintKind::Data, MO.getReg(), S, TRI, /*Set=*/true);
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

/// Propagate taint through a single machine instruction. Internal to this file:
/// every consumer reaches it through replayTaint.
static void propagateTaintMI(const MachineInstr &MI, TaintState &S,
                             const TargetRegisterInfo *TRI,
                             const TaintSummaryInfo *TSI, Module *M,
                             AAResults *AA) {
  // Reg → Reg: skip for pure loads/stores (address use is DIT sink).
  // Keep for ALU and RMW (mayLoad && mayStore) for conservatism.
  bool IsPureLoad = MI.mayLoad() && !MI.mayStore();
  bool IsPureStore = !MI.mayLoad() && MI.mayStore();
  bool IsCall = MI.isCall();
  if (!IsPureLoad && !IsPureStore && !IsCall) {
    bool UsesData = anyRegUseOfKind(TaintKind::Data, MI, S);
    bool UsesPointee = anyRegUseOfKind(TaintKind::Pointee, MI, S);
    bool UsesAddress = anyRegUseOfKind(TaintKind::Address, MI, S);

    updateAllRegDefs(TaintKind::Data, MI, S, TRI, UsesData);
    updateAllRegDefs(TaintKind::Pointee, MI, S, TRI, UsesPointee);
    updateAllRegDefs(TaintKind::Address, MI, S, TRI, UsesData || UsesAddress);
  }

  // Store handling: track taint into stack/global cells precisely.
  if (MI.mayStore()) {
    bool DataTainted = anyTaintedStoreDataRegUse(TaintKind::Data, MI, S);
    bool PointeeDataTainted =
        anyTaintedStoreDataRegUse(TaintKind::Pointee, MI, S);
    for (MachineMemOperand *MMO : MI.memoperands()) {
      if (!MMO)
        continue;
      CellInfo CI = getCellFromMMO(*MMO);

      // Write one cell. A known size means we know exactly what the store
      // overwrote, so the update is strong (taint or clear). An unknown size
      // proves nothing about what was overwritten, so it can only ever add
      // taint — recorded under a size-0 sentinel cell.
      auto storeCell = [&](bool Tainted, const char *Label, auto Set,
                           auto Clear) {
        if (!CI.Size && !Tainted)
          return;
        if (Tainted)
          Set(CI.Size.value_or(0));
        else
          Clear(*CI.Size);

        LLVM_DEBUG({
          dbgs() << "      " << (Tainted ? "taint " : "clear ") << Label << " ";
          if (CI.K == CellInfo::Stack)
            dbgs() << "FI=" << CI.FI;
          else
            dbgs() << CI.GV->getName();
          dbgs() << " off=" << CI.Offset << " sz=";
          if (CI.Size)
            dbgs() << *CI.Size;
          else
            dbgs() << "unknown";
          dbgs() << (Tainted ? " (store)\n" : " (store untainted)\n");
        });
      };

      if (CI.K == CellInfo::Stack) {
        storeCell(
            DataTainted, "stack cell",
            [&](uint64_t Sz) { S.setTaintedStackCell(CI.FI, CI.Offset, Sz); },
            [&](uint64_t Sz) { S.clearTaintedStackCell(CI.FI, CI.Offset, Sz); });
        storeCell(PointeeDataTainted, "pointee stack cell",
                  [&](uint64_t Sz) {
                    S.setPointeeTaintedStackCell(CI.FI, CI.Offset, Sz);
                  },
                  [&](uint64_t Sz) {
                    S.clearPointeeTaintedStackCell(CI.FI, CI.Offset, Sz);
                  });
      } else if (CI.K == CellInfo::Global) {
        storeCell(
            DataTainted, "global cell",
            [&](uint64_t Sz) { S.setTaintedGlobalCell(CI.GV, CI.Offset, Sz); },
            [&](uint64_t Sz) { S.clearTaintedGlobalCell(CI.GV, CI.Offset, Sz); });
      } else {
        // Unknown/heap: keep queryable locations precise enough for AA, and
        // fall back to the opaque poison bit when MIR lacks IR memory info.
        if (DataTainted) {
          if (AA && MMO->getValue()) {
            S.setTaintedUnknownMemValue(MMO->getValue());
            LLVM_DEBUG({
              dbgs() << "      taint unknown mem value ";
              MMO->getValue()->printAsOperand(dbgs(), false);
              dbgs() << " (non-stack non-global store)\n";
            });
          } else {
            S.UnknownMemTainted = true;
            LLVM_DEBUG(dbgs() << "      taint unknown mem (non-stack "
                                 "non-global store, opaque)\n");
          }
        }
        if (PointeeDataTainted && MMO->getValue()) {
          S.setPointeeTaintedUnknownMemValue(MMO->getValue());
          LLVM_DEBUG({
            dbgs() << "      taint pointee unknown mem value ";
            MMO->getValue()->printAsOperand(dbgs(), false);
            dbgs() << " (pointer spill)\n";
          });
        } else if (MMO->getValue()) {
          S.clearPointeeTaintedUnknownMemValue(MMO->getValue());
        }
      }
    }
    if (MI.memoperands_empty() && DataTainted) {
      S.UnknownMemTainted = true;
      LLVM_DEBUG(dbgs() << "      taint unknown mem (store, no MMO)\n");
    }
  }

  // Load handling: stack and globals use cell-level precision. Unknown/heap
  // loads use pointee taint, opaque unknown-memory poison, and an AA noalias
  // filter over tainted unknown-memory stores that still have IR pointer info.
  if (MI.mayLoad()) {
    bool ShouldTaint = false;
    bool ShouldPointeeTaint = false;
    bool HeapPoisoned =
        S.UnknownMemTainted || (TSI && TSI->hasUnknownMemTainted());

    for (MachineMemOperand *MMO : MI.memoperands()) {
      if (!MMO) {
        // No MMO info: use pointee taint or heap-poisoned flag as fallback.
        if (HeapPoisoned || !S.TaintedUnknownMemValues.empty() ||
            anyRegUseOfKind(TaintKind::Pointee, MI, S))
          ShouldTaint = true;
        continue;
      }
      CellInfo CI = getCellFromMMO(*MMO);
      if (CI.K == CellInfo::Stack) {
        bool Tainted = CI.Size
                           ? S.isTaintedStackCell(CI.FI, CI.Offset, *CI.Size)
                           : S.anyTaintedStackCellForFI(CI.FI);
        bool PointeeTainted =
            CI.Size ? S.isPointeeTaintedStackCell(CI.FI, CI.Offset, *CI.Size)
                    : S.anyPointeeTaintedStackCellForFI(CI.FI);
        if (Tainted) {
          ShouldTaint = true;
          LLVM_DEBUG(dbgs() << "      load from tainted stack cell FI=" << CI.FI
                            << " off=" << CI.Offset << "\n");
        }
        if (PointeeTainted) {
          ShouldPointeeTaint = true;
          LLVM_DEBUG(dbgs() << "      load from pointee-tainted stack cell FI="
                            << CI.FI << " off=" << CI.Offset << "\n");
        }
        if (!Tainted && !PointeeTainted) {
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
        // Unknown/heap: pointee-tainted bases identify secret memory. Secret
        // data used as an address is handled as an address-sensitive sink by
        // the reporting/barrier logic, not as proof that loaded data is secret.
        if (unknownMemMayTaintLoad(*MMO, S.PointeeTaintedUnknownMemValues,
                                   AA)) {
          ShouldPointeeTaint = true;
          LLVM_DEBUG(dbgs()
                     << "      load from pointee-tainted unknown mem\n");
        }
        if (HeapPoisoned || anyRegUseOfKind(TaintKind::Pointee, MI, S) ||
            unknownMemMayTaintLoad(*MMO, S.TaintedUnknownMemValues, AA)) {
          ShouldTaint = true;
          LLVM_DEBUG(dbgs() << "      load from tainted unknown/heap mem\n");
        } else {
          LLVM_DEBUG(dbgs()
                     << "      load from untainted unknown/heap mem"
                     << " (AA noalias or no tainted unknown stores)\n");
        }
      }
    }

    // No MMOs at all: use pointer taint or heap-poisoned flag.
    if (MI.memoperands_empty()) {
      if (HeapPoisoned || !S.TaintedUnknownMemValues.empty() ||
          anyRegUseOfKind(TaintKind::Pointee, MI, S))
        ShouldTaint = true;
      if (!S.PointeeTaintedUnknownMemValues.empty())
        ShouldPointeeTaint = true;
    }

    updateAllRegDefs(TaintKind::Data, MI, S, TRI, ShouldTaint);
    updateAllRegDefs(TaintKind::Pointee, MI, S, TRI, ShouldPointeeTaint);
    updateAllRegDefs(TaintKind::Address, MI, S, TRI, /*Set=*/false);
  }

  // NEW: Handle function calls for interprocedural taint propagation
  // This is the KEY addition for making the analysis interprocedural!
  if (MI.isCall() && TSI && M) {
    LLVM_DEBUG(dbgs() << "      handling call instruction\n");

    bool HasTaintedArg = anyTaintedCallArgument(MI, S);
    clearCallResultDefs(MI, S, TRI);

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
        taintCallResultDefs(MI, S, TRI);
        LLVM_DEBUG(dbgs() << "        call to " << Callee->getName()
                          << " returns tainted value\n");
      }
    } else {
      // Conservative: External function or indirect call
      // If any argument register is tainted, assume return is tainted.
      if (HasTaintedArg) {
        taintCallResultDefs(MI, S, TRI);
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
                                    Module *M = nullptr,
                                    AAResults *AA = nullptr) {
  TaintState Out = In;
  for (const auto &MI : MBB) {
    propagateTaintMI(MI, Out, TRI, TSI, M, AA);
  }
  return Out;
}

static bool hasTaintedRegDef(const MachineInstr &MI, const TaintState &S) {
  for (const MachineOperand &MO : MI.defs())
    if (MO.isReg() && MO.getReg().isValid() && S.isTainted(MO.getReg()))
      return true;
  return false;
}

// Public: declared in TaintAnalysis.h — shared by the barrier and export paths.
bool llvm::isTaintedInstruction(const MachineInstr &MI, const TaintFacts &F) {
  bool IsMemAccess = MI.mayLoad() || MI.mayStore();
  bool LoadsSecretPointee = MI.mayLoad() && F.UsesPointee;
  bool AddressSensitive = IsMemAccess && (F.UsesAddress || F.UsesData);
  return F.UsesData || F.DefsData || LoadsSecretPointee || AddressSensitive;
}

// Public: declared in TaintAnalysis.h — the single replay used by every
// consumer of a converged TaintResult, so none of them can drift out of step
// with propagateTaintMI.
void llvm::replayTaint(
    MachineFunction &MF, const TaintResult &TR, const TaintSummaryInfo *TSI,
    AAResults *AA,
    function_ref<bool(MachineInstr &, const TaintFacts &, const TaintState &)>
        Post,
    function_ref<bool(MachineInstr &, const TaintState &)> Pre) {
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  Module *M = const_cast<Module *>(MF.getFunction().getParent());

  for (MachineBasicBlock &MBB : MF) {
    auto It = TR.IN.find(&MBB);
    TaintState S = (It != TR.IN.end()) ? It->second : TaintState{};

    for (MachineInstr &MI : MBB) {
      if (Pre && !Pre(MI, S))
        return;

      TaintFacts F;
      if (Post) {
        F.UsesData = anyRegUseOfKind(TaintKind::Data, MI, S);
        F.UsesPointee = anyRegUseOfKind(TaintKind::Pointee, MI, S);
        F.UsesAddress = anyRegUseOfKind(TaintKind::Address, MI, S);
      }

      propagateTaintMI(MI, S, TRI, TSI, M, AA);

      if (Post) {
        F.DefsData = hasTaintedRegDef(MI, S);
        if (!Post(MI, F, S))
          return;
      }
    }
  }
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
                   const TaintSummaryInfo *TSI, unsigned &TaintedInstrCount,
                   AAResults *AA = nullptr) {
  SmallVector<TaintedRun, 64> TaintedRuns;
  TaintedInstrCount = 0;
  unsigned InstrIndex = 0;

  TaintedRun CurrentRun;
  unsigned CleanGap = 0;
  const MachineBasicBlock *CurMBB = nullptr;

  auto FinishRun = [&]() {
    if (CurrentRun.First)
      TaintedRuns.push_back(CurrentRun);
    CurrentRun = TaintedRun{};
    CleanGap = 0;
  };

  replayTaint(
      MF, TR, TSI, AA,
      [&](MachineInstr &MI, const TaintFacts &F, const TaintState &) {
        // A run never spans a block boundary: close the open one on entry to a
        // new block.
        if (MI.getParent() != CurMBB) {
          FinishRun();
          CurMBB = MI.getParent();
        }

        if (!isBarrierInsertionCandidate(MI))
          return true;

        ++InstrIndex;
        if (isTaintedInstruction(MI, F)) {
          if (!CurrentRun.First) {
            CurrentRun.First = &MI;
            CurrentRun.FirstIndex = InstrIndex;
          }
          CurrentRun.Last = &MI;
          CurrentRun.LastIndex = InstrIndex;
          ++CurrentRun.Count;
          ++TaintedInstrCount;
          CleanGap = 0;
        } else if (CurrentRun.First) {
          ++CleanGap;
          if (CleanGap > TaintRegionMergeGap)
            FinishRun();
        }
        return true;
      });

  FinishRun();
  return TaintedRuns;
}

// Public: declared in TaintAnalysis.h — same predicate insertTaintBarriers
// uses to decide whether a function gets DIT instrumentation.
bool llvm::functionHasTaintedRuns(MachineFunction &MF, const TaintResult &TR,
                                  const TaintSummaryInfo *TSI, AAResults *AA) {
  unsigned TaintedInstrCount = 0;
  return !collectTaintedRuns(MF, TR, TSI, TaintedInstrCount, AA).empty();
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
                               const TaintSummaryInfo *TSI, AAResults *AA) {
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
  SmallVector<unsigned, 4> PointeeTaintedArgIndices;

  // Source 1: IR attributes (set by taint-annotate pass on the caller funcs)
  for (const Argument &Arg : F.args()) {
    if (Arg.hasAttribute("tainted")) {
      TaintedArgIndices.push_back(Arg.getArgNo());
      LLVM_DEBUG(dbgs() << "  IR arg " << Arg.getArgNo()
                        << " has 'tainted' attribute\n");
    }
    if (Arg.hasAttribute("tainted-pointee")) {
      PointeeTaintedArgIndices.push_back(Arg.getArgNo());
      LLVM_DEBUG(dbgs() << "  IR arg " << Arg.getArgNo()
                        << " has 'tainted-pointee' attribute\n");
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
    for (unsigned Idx : Summary.PointeeTaintedArgIndices) {
      if (!llvm::is_contained(PointeeTaintedArgIndices, Idx)) {
        PointeeTaintedArgIndices.push_back(Idx);
        LLVM_DEBUG(dbgs() << "  TSI arg " << Idx
                          << " pointee-tainted (interprocedural)\n");
      }
    }
  }

  if (TaintedArgIndices.empty() && PointeeTaintedArgIndices.empty() && !TSI) {
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
  auto seedArg = [&](TaintKind K, Register PhysReg, Register VirtReg,
                     unsigned ArgIdx, const char *What) {
    updateWithAliases(K, PhysReg, Seed, TRI, /*Set=*/true);
    if (VirtReg.isValid()) {
      updateWithAliases(K, VirtReg, Seed, TRI, /*Set=*/true);
      LLVM_DEBUG(dbgs() << "  Marked virtual register " << printReg(VirtReg, TRI)
                        << " as " << What << " (from arg " << ArgIdx << ", phys "
                        << printReg(PhysReg, TRI) << ")\n");
    } else {
      LLVM_DEBUG(dbgs() << "  Marked physical register "
                        << printReg(PhysReg, TRI) << " as " << What
                        << " (from arg " << ArgIdx << ")\n");
    }
  };

  for (const auto &[PhysReg, VirtReg] : MRI.liveins()) {
    unsigned ArgIdx = TRI->getEncodingValue(PhysReg);
    if (llvm::is_contained(TaintedArgIndices, ArgIdx))
      seedArg(TaintKind::Data, PhysReg, VirtReg, ArgIdx, "tainted");
    if (llvm::is_contained(PointeeTaintedArgIndices, ArgIdx))
      seedArg(TaintKind::Pointee, PhysReg, VirtReg, ArgIdx, "pointee-tainted");
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

    TaintState NewOut = propagateTaintMBB(*B, IN[B], TRI, TSI, M, AA);

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
                    << " (data=" << Result.countDataRegs()
                    << ", pointee=" << Result.countPointeeRegs()
                    << ", address=" << Result.countAddressRegs() << ")"
                    << ", tainted cells: " << Result.countCells()
                    << ", total: " << Result.count() << "\n");

  return TaintResult{std::move(Result), std::move(IN)};
}

/// Pass-manager entry point. This is the intraprocedural mode: interprocedural
/// summaries only exist inside TaintInterprocPass, which drives run(MF, TSI, AA)
/// directly with the TSI it owns.
TaintResult TaintAnalysis::run(MachineFunction &MF,
                               MachineFunctionAnalysisManager &MFAM) {
  AAResults *AA =
      &MFAM.getResult<FunctionAnalysisManagerMachineFunctionProxy>(MF)
           .getManager()
           .getResult<AAManager>(MF.getFunction());
  return run(MF, /*TSI=*/nullptr, AA);
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
                                     FunctionTaintStats *Stats,
                                     AAResults *AA) {
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();

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

  // Every block gets a stats row, including ones the replay never visits
  // because they hold no instructions.
  DenseMap<const MachineBasicBlock *, unsigned> BBRow;
  if (Stats)
    for (const MachineBasicBlock &MBB : MF) {
      BBRow[&MBB] = PerBB.size();
      PerBB.push_back({MBB.getName(), 0, 0});
    }

  replayTaint(
      MF, TR, TSI, AA,
      [&](MachineInstr &MI, const TaintFacts &F, const TaintState &S) {
        const MachineBasicBlock &MBB = *MI.getParent();
        bool IsTainted = isTaintedInstruction(MI, F);

        if (Stats) {
          ++TotalInstr;
          InstrCat Cat = classifyMI(MI);
          BBStats &Row = PerBB[BBRow[&MBB]];
          if (IsTainted) {
            ++TaintedInstr;
            TaintedPositions.push_back(TotalInstr - 1);
            Row.Tainted++;
            TaintedByCat[Cat]++;
          } else {
            Row.Untainted++;
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
        OS << " # pointee_tainted_regs:";
        for (const auto &RegID : S.PointeeTaintedRegs)
          OS << " " << printReg(Register(RegID), TRI);
        OS << " # address_tainted_regs:";
        for (const auto &RegID : S.AddressTaintedRegs)
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
        return true;
      });

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
                                         raw_ostream &OS, AAResults *AA) {
  unsigned TaintedInstrCount = 0;
  SmallVector<TaintedRun, 64> TaintedRuns =
      collectTaintedRuns(MF, TR, TSI, TaintedInstrCount, AA);
  printTaintedRuns(MF, TaintedRuns, OS);
  return TaintedInstrCount;
}

unsigned llvm::exportTaintSourceRegions(MachineFunction &MF,
                                        const TaintResult &TR,
                                        const TaintSummaryInfo *TSI,
                                        raw_ostream &OS, AAResults *AA) {
  unsigned TaintedInstrCount = 0;
  SmallVector<TaintedRun, 64> TaintedRuns =
      collectTaintedRuns(MF, TR, TSI, TaintedInstrCount, AA);
  return printTaintSourceRegions(TaintedRuns, MF.getName(), OS);
}

unsigned llvm::insertTaintBarriers(MachineFunction &MF, const TaintResult &TR,
                                   const TaintSummaryInfo *TSI,
                                   raw_ostream *RegionsOS, AAResults *AA) {
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  unsigned TaintedInstrCount = 0;
  SmallVector<TaintedRun, 64> TaintedRuns =
      collectTaintedRuns(MF, TR, TSI, TaintedInstrCount, AA);

  if (RegionsOS)
    printTaintedRuns(MF, TaintedRuns, *RegionsOS);

  if (TaintBarrierMode == TaintBarrierKind::DIT) {
    // Function granularity: run the whole function in data-independent-timing
    // mode when it contains any tainted instruction. Per-region toggles would
    // clear an enclosing region's DIT when a tainted callee's exit switch runs
    // inside a caller's still-open region.
    if (!TaintedRuns.empty()) {
      MachineBasicBlock &Entry = MF.front();
      TII->insertTimingModeSwitch(Entry, Entry.begin(), DebugLoc(),
                                  /*Enable=*/true);
      // The DIT scope ends on any exit, including tail calls; a tainted
      // callee re-enables it for itself. After a non-tail call the callee may
      // have cleared DIT on its own exit (PSTATE.DIT has no callee-saved
      // convention), so re-assert it to keep the rest of this function
      // protected — unless the callee's summary proves it preserves DIT
      // (in-TU, not instrumented, only preserving calls).
      Module *M = const_cast<Module *>(MF.getFunction().getParent());
      for (MachineBasicBlock &MBB : MF)
        for (MachineInstr &MI : MBB) {
          if (MI.isReturn()) {
            TII->insertTimingModeSwitch(MBB, MI.getIterator(),
                                        MI.getDebugLoc(), /*Enable=*/false);
          } else if (MI.isCall()) {
            const Function *Callee = findCalledFunction(*M, MI);
            if (TSI && Callee && TSI->getSummary(*Callee).PreservesDIT)
              continue;
            TII->insertTimingModeSwitch(MBB, std::next(MI.getIterator()),
                                        MI.getDebugLoc(), /*Enable=*/true);
          }
        }
    }
    return TaintedInstrCount;
  }

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
// Report files
//===----------------------------------------------------------------------===//

SmallString<256> llvm::deriveReportPath(StringRef Base, StringRef Suffix) {
  SmallString<256> Path(Base);
  std::string Ext = sys::path::extension(Path).str();
  sys::path::replace_extension(Path, "");
  Path += Suffix;
  Path += Ext;
  return Path;
}

std::unique_ptr<raw_fd_ostream>
llvm::openTaintReport(StringRef Path, StringRef What, bool Append) {
  if (Path.empty())
    return nullptr;

  std::error_code EC;
  auto OS = std::make_unique<raw_fd_ostream>(
      Path, EC, Append ? sys::fs::OF_Append : sys::fs::OF_None);
  if (EC) {
    errs() << "Error opening " << What << " file: " << EC.message() << "\n";
    return nullptr;
  }
  return OS;
}

//===----------------------------------------------------------------------===//
// TaintAnalysisPass — standalone MachineFunction pass (uses the helper above)
//===----------------------------------------------------------------------===//

PreservedAnalyses TaintAnalysisPass::run(MachineFunction &MF,
                                         MachineFunctionAnalysisManager &MFAM) {
  auto &TR = MFAM.getResult<TaintAnalysis>(MF);
  AAResults *AA =
      &MFAM.getResult<FunctionAnalysisManagerMachineFunctionProxy>(MF)
           .getManager()
           .getResult<AAManager>(MF.getFunction());

  // Intraprocedural: summaries only exist inside TaintInterprocPass.
  const TaintSummaryInfo *TSI = nullptr;

  LLVM_DEBUG({
    if (!TR.Merged.empty())
      dbgs() << "TaintAnalysisPass: " << MF.getName() << " has "
             << TR.Merged.count() << " tainted register(s)\n";
  });

  if (!TR.Merged.empty()) {
    if (auto OS = openTaintReport(TaintOutputFile, "taint output",
                                  /*Append=*/true)) {
      auto SrcOS = openTaintReport(deriveReportPath(TaintOutputFile, "_src"),
                                   "source output", /*Append=*/true);

      FunctionTaintStats Stats;
      exportTaintedInstructions(MF, TR, TSI, *OS, SrcOS.get(), &Stats, AA);

      if (!Stats.Output.empty())
        if (auto StatsOS =
                openTaintReport(deriveReportPath(TaintOutputFile, "_stats"),
                                "stats output", /*Append=*/true))
          *StatsOS << Stats.Output;
    }
  }

  unsigned BarriersInserted = 0;
  if (!TR.Merged.empty()) {
    auto RegionsOS = openTaintReport(TaintRegionsOutputFile,
                                     "taint regions output", /*Append=*/true);
    auto SourceRegionsOS =
        openTaintReport(TaintSourceRegionsOutputFile,
                        "taint source regions output", /*Append=*/true);

    if (TaintInsertISB)
      BarriersInserted = insertTaintBarriers(MF, TR, TSI, RegionsOS.get(), AA);
    else if (RegionsOS)
      exportTaintBarrierRegions(MF, TR, TSI, *RegionsOS, AA);

    if (SourceRegionsOS)
      exportTaintSourceRegions(MF, TR, TSI, *SourceRegionsOS, AA);
  }

  if (BarriersInserted)
    return PreservedAnalyses::none();
  return getMachineFunctionPassPreservedAnalyses();
}
