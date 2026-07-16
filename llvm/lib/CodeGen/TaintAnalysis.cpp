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
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
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
#include "llvm/Support/ErrorHandling.h"
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

cl::opt<bool> llvm::TaintInsertDIT(
    "taint-insert-dit",
    cl::desc("Insert PSTATE.DIT mode switches around tainted code"),
    cl::init(false));

cl::opt<std::string> llvm::TaintCallsiteReportFile(
    "taint-callsite-report",
    cl::desc("Output file for call sites passing secret data to callees the "
             "analysis cannot instrument (external declarations, indirect "
             "calls)"),
    cl::value_desc("file"));

cl::opt<std::string> llvm::TaintUncoveredReportFile(
    "taint-uncovered-report",
    cl::desc("Output file for tainted instructions PSTATE.DIT does not protect "
             "(divide/sqrt, secret-dependent addresses, secret branches) — "
             "silent false assurance otherwise (gap G2)"),
    cl::value_desc("file"));

static cl::opt<unsigned> TaintRegionMergeGap(
    "taint-region-merge-gap",
    cl::desc("Merge barrier-protected taint regions in the same basic block "
             "when separated by at most this many clean instructions"),
    cl::init(2));

// Track B: PSTATE.DIT placement granularity. `function` is the shipped
// whole-function policy; `region` is the WIP cost-model-driven region placement
// (utils/taint_dit_placement.md §5.6). Increment (a) implements the
// anticipation-coarse scaffolding of `region`.
namespace {
enum class DITPlacementMode { Function, Region };
} // namespace
static cl::opt<DITPlacementMode> TaintDITPlacement(
    "taint-dit-placement", cl::desc("PSTATE.DIT placement granularity"),
    cl::init(DITPlacementMode::Function),
    cl::values(clEnumValN(DITPlacementMode::Function, "function",
                          "whole-function: DIT on for any tainted function "
                          "(default, shipped)"),
               clEnumValN(DITPlacementMode::Region, "region",
                          "cost-model region placement (WIP, increment a: "
                          "anticipation-coarse)")));

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

/// Complement of anyTaintedStoreDataRegUse: is any ADDRESS operand of a store
/// (the register uses AFTER the leading stored-value registers) tainted of kind
/// K? Lets the G2 diagnostic tell a secret store ADDRESS (uncovered by DIT —
/// cache/TLB timing) from secret store DATA (which DIT covers, and which is
/// address-tainted by the over-approximation, so a naive whole-instruction check
/// would false-positive on it). Returns false when the value/address split is
/// unknown, to avoid a false "uncovered" report.
static bool anyTaintedStoreAddressRegUse(TaintKind K, const MachineInstr &MI,
                                         const TaintState &S) {
  if (!MI.mayStore())
    return false;
  std::optional<unsigned> ValueRegs = getStoredValueRegCount(MI);
  if (!ValueRegs)
    return false;
  unsigned Skip = *ValueRegs;
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg() || !MO.isUse() || MO.isImplicit())
      continue;
    if (Skip > 0) {
      --Skip; // skip a stored-value register
      continue;
    }
    if (S.test(K, MO.getReg()))
      return true;
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

// A call "passes a secret" if any argument register holds a secret value
// (data-tainted) OR points to secret memory (pointee-tainted). The pointee case
// is essential: memcpy(dst, secret_src, n) passes a public pointer whose pointee
// is secret — data taint alone misses it, so the callee could copy the secret
// into caller-visible memory with no TOP mod-set applied (missing-barrier leak).
static bool anyTaintedCallArgument(const MachineInstr &MI,
                                   const TaintState &S) {
  return MI.isCall() && (anyRegUseOfKind(TaintKind::Data, MI, S) ||
                         anyRegUseOfKind(TaintKind::Pointee, MI, S));
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
    // ExternalMemClobbered folds in here: after a call whose callee may have
    // written a secret to unknown memory (a mod-set TOP), every heap and global
    // load is secret. Stack loads are handled separately below — the existing
    // heap poison never covered them, and that is exactly where the callee->
    // caller-through-memory leak lived (the reload of a buffer a callee wrote).
    bool HeapPoisoned = S.UnknownMemTainted || S.isExternalMemClobbered() ||
                        (TSI && TSI->hasUnknownMemTainted());

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
        // A call that clobbered unknown memory (mod-set TOP) may have written
        // through a pointer into this frame — blunt P0 poisons every stack load
        // after such a call. Provenance-based escaped-object precision (only
        // poison stack objects whose address escaped) is the P1 refinement.
        bool Tainted = S.isExternalMemClobbered() ||
                       (CI.Size
                            ? S.isTaintedStackCell(CI.FI, CI.Offset, *CI.Size)
                            : S.anyTaintedStackCellForFI(CI.FI));
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
          bool Tainted =
              S.isWholeGlobalTainted(CI.GV) ||
              (CI.Size ? S.isTaintedGlobalCell(CI.GV, CI.Offset, *CI.Size)
                       : S.anyTaintedGlobalCellForGV(CI.GV));
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

      // Apply the callee's memory-effects (mod-set): what secret it may have
      // written into caller-visible memory. This is the callee->caller-through-
      // memory transfer. Applied unconditionally — the summary is context-
      // insensitive (computed over the callee's joined tainted-arg set), so a
      // call site not passing the taint is over-approximated, the sound way.
      const FunctionMemEffects &ME = Summary.MemEffects;
      for (const GlobalVariable *GV : ME.WritesSecretToGlobal)
        S.setTaintedWholeGlobal(GV);
      if (ME.WritesSecretToUnknown) {
        S.setExternalMemClobbered();
        LLVM_DEBUG(dbgs() << "        call to " << Callee->getName()
                          << " writes secret to unknown memory (mod-set TOP)\n");
      }
    } else {
      // External declaration or indirect call: the analysis cannot see what it
      // does to memory. If it receives a secret (in any argument register),
      // assume TOP — it may have written that secret anywhere caller-visible.
      // This is blunt-TOP P0; a libc model table and IR memory(...) attributes
      // would refine it (research §11 vii/viii), deferred pending measurement.
      if (HasTaintedArg) {
        taintCallResultDefs(MI, S, TRI);
        S.setExternalMemClobbered();
        if (Callee)
          LLVM_DEBUG(dbgs() << "        conservative: external call to "
                            << Callee->getName()
                            << " taints return + clobbers memory (TOP)\n");
        else
          LLVM_DEBUG(dbgs() << "        conservative: indirect call taints "
                               "return + clobbers memory (TOP)\n");
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
  // A call that hands a secret to its callee must run with DIT enabled so the
  // callee inherits it (Scenario B, taint_dit_placement.md G3). A data-carrying
  // call is already covered by F.UsesData; a call that passes only a *pointer to
  // secret memory* (e.g. memcpy(dst, secret_src, n), where the pointer value is
  // public but its pointee is secret) is not, and would otherwise leave the
  // enclosing function uninstrumented — so the callee would run with DIT off.
  bool PassesPointeeSecretToCall = MI.isCall() && F.UsesPointee;
  return F.UsesData || F.DefsData || LoadsSecretPointee || AddressSensitive ||
         PassesPointeeSecretToCall;
}

// Public: declared in TaintAnalysis.h — the G2 diagnostic classifier.
const char *llvm::classifyDITUncovered(const MachineInstr &MI,
                                       const TaintFacts &F, const TaintState &S,
                                       const TargetInstrInfo &TII) {
  if (!isTaintedInstruction(MI, F))
    return nullptr;

  // Address- and control-flow hazards are checked FIRST: DIT covers neither, and
  // they are orthogonal to whether the instruction's own data-value timing is in
  // the covered set (a load is data-value-covered yet its address still leaks; a
  // branch is not in the covered set at all but its hazard is the direction, not
  // its latency — so it must be labelled secret-branch, not not-dit-covered).

  // Load: every register a pure load uses is an address operand (the loaded
  // value is a def), so a secret in any of them is a secret ADDRESS — cache/TLB
  // timing, which DIT does not cover. A load of secret *data* through a clean
  // pointer is UsesPointee (not UsesData/UsesAddress) and IS covered.
  if (MI.mayLoad() && !MI.mayStore() && (F.UsesData || F.UsesAddress))
    return "secret-address";

  // Store: check ONLY the address operands. Secret store *data* is DIT-covered
  // (and is address-tainted by the over-approximation, so a whole-instruction
  // UsesAddress check would false-positive on it — the reason this uses the
  // value/address split). A raw uncomputed secret used directly as a store
  // address is the one under-flagged case (getStoredValueRegCount unknown ->
  // not flagged); computed/address-tainted store addresses are caught.
  if (MI.mayStore() &&
      (anyTaintedStoreAddressRegUse(TaintKind::Data, MI, S) ||
       anyTaintedStoreAddressRegUse(TaintKind::Address, MI, S)))
    return "secret-address";

  // Secret-dependent branch: control-flow timing, not covered.
  if (MI.isBranch() && F.UsesData)
    return "secret-branch";

  // A call or return is a control transfer, not a data-value-timing site. A
  // call's secret argument is protected in the callee by inherited DIT (Scenario
  // B) and audited by the call-site ESCAPE report; a return just hands its value
  // to the caller (tracked by taint propagation). Neither is a not-dit-covered
  // instruction. (A secret-dependent conditional branch was already caught above
  // as secret-branch.)
  if (MI.isCall() || MI.isReturn())
    return nullptr;

  // The instruction's own data-value timing: is it in the Arm DIT covered set?
  // isDITProtected is a membership list (utils/taint_dit_spec.md) — false covers
  // the documented divide/sqrt exclusions AND anything not provably covered. The
  // printed opcode identifies which (e.g. SDIVXr).
  if (!TII.isDITProtected(MI))
    return "not-dit-covered";

  return nullptr;
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

// Public: declared in TaintAnalysis.h — same predicate insertTaintDITSwitches
// uses to decide whether a function gets DIT instrumentation.
bool llvm::functionHasTaintedRuns(MachineFunction &MF, const TaintResult &TR,
                                  const TaintSummaryInfo *TSI, AAResults *AA) {
  unsigned TaintedInstrCount = 0;
  return !collectTaintedRuns(MF, TR, TSI, TaintedInstrCount, AA).empty();
}

FunctionMemEffects llvm::computeFunctionMemEffects(MachineFunction &MF,
                                                   const TaintResult &TR,
                                                   const TaintSummaryInfo *TSI,
                                                   AAResults *AA) {
  FunctionMemEffects ME;
  const MachineFrameInfo &MFI = MF.getFrameInfo();

  // Transitive re-export (Post: reads the state LEAVING each instruction). A
  // callee this function calls may itself write a secret into caller-visible
  // memory; that effect lands in this function's state as ExternalMemClobbered
  // and/or inherited whole-tainted globals, and must be re-exported so it
  // survives more than one call level. Reading the *leaving* state is required
  // for a tail call: it sets the clobber in its own post-state and has no
  // successor instruction whose entering state could observe it.
  auto reexport = [&](MachineInstr &, const TaintFacts &, const TaintState &S) {
    if (S.isExternalMemClobbered())
      ME.WritesSecretToUnknown = true;
    for (const GlobalVariable *GV : S.wholeTaintedGlobals())
      ME.WritesSecretToGlobal.insert(GV);
    return true;
  };

  // Read the state ENTERING each store (store-data taint is a use of the
  // incoming state), classify the destination, and record only what is
  // visible to the caller. Every case the analysis cannot pin down goes to
  // WritesSecretToUnknown (TOP), the sound direction for a hardener.
  replayTaint(
      MF, TR, TSI, AA, /*Post=*/reexport,
      [&](MachineInstr &MI, const TaintState &S) {
        if (!MI.mayStore())
          return true;
        if (!anyTaintedStoreDataRegUse(TaintKind::Data, MI, S) &&
            !anyTaintedStoreDataRegUse(TaintKind::Pointee, MI, S))
          return true;

        if (MI.memoperands_empty()) {
          ME.WritesSecretToUnknown = true; // unknown destination
          return true;
        }
        for (MachineMemOperand *MMO : MI.memoperands()) {
          if (!MMO) {
            ME.WritesSecretToUnknown = true;
            continue;
          }
          CellInfo CI = getCellFromMMO(*MMO);
          if (CI.K == CellInfo::Stack) {
            // A non-fixed frame object is this function's own private stack —
            // the caller cannot see it, so it is NOT a caller-visible effect.
            // A fixed object is an incoming stack/byval argument slot, which
            // the caller CAN see; blunt P0 maps that to TOP (mapping it to the
            // specific argument is the provenance-based P1 refinement).
            if (MFI.isFixedObjectIndex(CI.FI))
              ME.WritesSecretToUnknown = true;
          } else if (CI.K == CellInfo::Global) {
            ME.WritesSecretToGlobal.insert(CI.GV);
          } else {
            // Unknown/heap — includes a store through a pointer argument, which
            // is the canonical callee->caller-through-memory write.
            ME.WritesSecretToUnknown = true;
          }
        }
        return true;
      });

  return ME;
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

    // Column 5 marks whether the region has an exit point (it does not, when
    // the run extends into a terminator). "exit" replaced the old "DSB" when
    // the ISB/DSB mode was removed on 2026-07-14.
    OS << Region->Filename << "\t" << Region->StartLine << "\t"
       << Region->EndLine << "\t" << FunctionName << "\t"
       << (Region->HasExitBarrier ? "exit" : "none") << "\n";
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

//===----------------------------------------------------------------------===//
// Track B: cost-model region placement (utils/taint_dit_placement.md §5.6)
//===----------------------------------------------------------------------===//

// Whole-function granularity: MSR DIT #1 at entry, #0 before every return
// (isReturn is tested before isCall, so a tail call is treated as a return and
// never gets a re-assert appended after it — a terminator), #1 re-asserted after
// every non-tail, non-DIT-preserving call. The shipped policy, and the safe
// fallback when region placement cannot prove coverage.
static void emitFunctionGranularityDIT(MachineFunction &MF,
                                       const TaintSummaryInfo *TSI) {
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  Module *M = const_cast<Module *>(MF.getFunction().getParent());
  MachineBasicBlock &Entry = MF.front();
  TII->insertTimingModeSwitch(Entry, Entry.begin(), DebugLoc(),
                              /*Enable=*/true);
  for (MachineBasicBlock &MBB : MF)
    for (MachineInstr &MI : MBB) {
      if (MI.isReturn()) {
        TII->insertTimingModeSwitch(MBB, MI.getIterator(), MI.getDebugLoc(),
                                    /*Enable=*/false);
      } else if (MI.isCall()) {
        const Function *Callee = findCalledFunction(*M, MI);
        if (TSI && Callee && TSI->getSummary(*Callee).PreservesDIT)
          continue;
        TII->insertTimingModeSwitch(MBB, std::next(MI.getIterator()),
                                    MI.getDebugLoc(), /*Enable=*/true);
      }
    }
}

// An instruction that must execute with PSTATE.DIT=1. Coverable-tainted
// data-processing / loads / stores (DIT protects their data-value timing —
// including a secret-address load's data value, the LVP channel), PLUS any
// secret-passing call (the callee inherits DIT — Scenario B; note isDITProtected
// is false for a call, so the call term is explicit). Divides/sqrt, secret
// branches, and returns are NOT needs — DIT cannot protect them (they are
// residuals, see classifyDITUncovered). §5.6 Correction 1.
static bool needsDIT(const MachineInstr &MI, const TaintFacts &F,
                     const TargetInstrInfo &TII) {
  if (!isTaintedInstruction(MI, F))
    return false;
  return TII.isDITProtected(MI) || MI.isCall();
}

// A call whose callee may clear PSTATE.DIT on its own exit (no callee-saved
// convention). After it, DIT must be re-asserted if the region continues.
static bool clobbersDIT(const MachineInstr &MI, const TaintSummaryInfo *TSI,
                        Module &M) {
  if (!MI.isCall())
    return false;
  const Function *Callee = findCalledFunction(M, MI);
  return !(TSI && Callee && TSI->getSummary(*Callee).PreservesDIT);
}

// Increment (a): anticipation-coarse region placement. Builds the ANTIN backward
// lattice (a need is anticipated on every forward path), enables DIT on entering
// each anticipated region from an unanticipated (or entry) edge, re-asserts after
// every clobber inside the region, and disables on region exits and before
// returns. Degenerates to whole-function granularity when all blocks anticipate
// (byte-identical), and narrows trailing (unanticipated) clean blocks. Preamble
// narrowing and loop-hoisting are increment (b). See §5.6.
static unsigned insertTaintDITRegions(MachineFunction &MF,
                                      const TaintResult &TR,
                                      const TaintSummaryInfo *TSI,
                                      AAResults *AA) {
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  Module &M = *const_cast<Module *>(MF.getFunction().getParent());

  // Local facts: which instructions are Needs, and which blocks contain one.
  DenseMap<const MachineBasicBlock *, bool> HasNeed;
  DenseSet<const MachineInstr *> NeedSet;
  unsigned NeedCount = 0;
  replayTaint(MF, TR, TSI, AA,
              [&](MachineInstr &MI, const TaintFacts &F, const TaintState &) {
                if (needsDIT(MI, F, *TII)) {
                  HasNeed[MI.getParent()] = true;
                  NeedSet.insert(&MI);
                  ++NeedCount;
                }
                return true;
              });
  if (NeedCount == 0)
    return 0;

  // Increment (b): loop-aware DIT-on block set (utils/taint_dit_placement.md
  // §5.6). On(b) = HasNeed(b) OR b is in a loop that (transitively) contains a
  // Need. This excludes a clean preamble (Off) and makes the WHOLE outermost
  // need-loop On, so the Off→On boundary is the loop preheader (enable executed
  // once) rather than the loop header (re-entered every iteration by the
  // backedge). Loop info is built locally — no MFAM plumbing needed here.
  MachineDominatorTree MDT(MF);
  MachineLoopInfo MLI(MDT);

  // Loops that (transitively, via block membership incl. sub-loops) contain a
  // Need. Marking every loop enclosing a need-block hoists the region to the
  // outermost need-loop.
  DenseSet<const MachineLoop *> NeedLoops;
  for (MachineBasicBlock &MBB : MF)
    if (HasNeed.lookup(&MBB))
      for (MachineLoop *L = MLI.getLoopFor(&MBB); L; L = L->getParentLoop())
        NeedLoops.insert(L);
  auto On = [&](const MachineBasicBlock *B) -> bool {
    if (HasNeed.lookup(B))
      return true;
    for (MachineLoop *L = MLI.getLoopFor(B); L; L = L->getParentLoop())
      if (NeedLoops.count(L))
        return true;
    return false;
  };

  // Emit. Enable at each Off→On boundary (hoisted to the preheader when the
  // On-entry block is a loop header, so the enable is not re-executed on the
  // backedge); re-assert after non-terminator clobbers; disable before non-Need
  // returns and on entering an Off block from an On predecessor.
  unsigned Toggles = 0;
  bool NeedFallback = false;
  for (MachineBasicBlock &MBB : MF) {
    if (On(&MBB)) {
      bool OnAtEntry = !MBB.pred_empty();
      for (MachineBasicBlock *P : MBB.predecessors())
        OnAtEntry &= On(P);
      if (!OnAtEntry) {
        // Off→On boundary. If MBB is a loop header, hoist the enable to the
        // preheader (executed once). No unique preheader ⇒ cannot hoist without
        // an edge split (restricted post-PEI) ⇒ fall back to function
        // granularity for this function rather than emit a per-iteration toggle.
        MachineLoop *L = MLI.getLoopFor(&MBB);
        if (L && L->getHeader() == &MBB) {
          MachineBasicBlock *PH = L->getLoopPreheader();
          if (!PH) {
            NeedFallback = true;
            break;
          }
          TII->insertTimingModeSwitch(*PH, PH->getFirstTerminator(),
                                      DebugLoc(), /*Enable=*/true);
        } else {
          TII->insertTimingModeSwitch(MBB, MBB.begin(), DebugLoc(),
                                      /*Enable=*/true);
        }
        ++Toggles;
      }
      // Re-assert after each clobber that is NOT a terminator (a non-preserving
      // tail call is a clobber AND a terminator: nothing follows it here, and
      // inserting after a terminator is invalid MIR).
      SmallVector<MachineInstr *, 8> Clobbers;
      for (MachineInstr &MI : MBB)
        if (clobbersDIT(MI, TSI, M) && !MI.isTerminator())
          Clobbers.push_back(&MI);
      for (MachineInstr *C : Clobbers) {
        TII->insertTimingModeSwitch(MBB, std::next(C->getIterator()),
                                    C->getDebugLoc(), /*Enable=*/true);
        ++Toggles;
      }
      // Disable before the block's return (found by scan — it need not be the
      // last slot). Skip if the return is itself a Need (a secret-passing tail
      // call): DIT must stay ON through it so the callee inherits it.
      for (MachineInstr &MI : MBB) {
        if (!MI.isReturn())
          continue;
        if (!NeedSet.count(&MI)) {
          TII->insertTimingModeSwitch(MBB, MI.getIterator(), MI.getDebugLoc(),
                                      /*Enable=*/false);
          ++Toggles;
        }
        break;
      }
    } else {
      // Off block: disable on entering from an On predecessor. An On→Off edge's
      // Off side is a loop exit (outside the loop), entered once per exit — no
      // hoisting needed.
      bool AnyPredOn = false;
      for (MachineBasicBlock *P : MBB.predecessors())
        AnyPredOn |= On(P);
      if (AnyPredOn) {
        TII->insertTimingModeSwitch(MBB, MBB.begin(), DebugLoc(),
                                    /*Enable=*/false);
        ++Toggles;
      }
    }
  }

  if (NeedFallback) {
    for (MachineBasicBlock &MBB : MF) {
      SmallVector<MachineInstr *, 8> ToErase;
      for (MachineInstr &MI : MBB)
        if (TII->getTimingModeSwitch(MI))
          ToErase.push_back(&MI);
      for (MachineInstr *I : ToErase)
        I->eraseFromParent();
    }
    errs() << "taint: DIT region placement: need-loop without a preheader in "
           << MF.getName() << "; fell back to function granularity\n";
    emitFunctionGranularityDIT(MF, TSI);
    return NeedCount;
  }
  LLVM_DEBUG(dbgs() << "  DIT region placement in " << MF.getName() << ": "
                    << Toggles << " toggle(s) over " << NeedCount
                    << " need(s)\n");

  // Soundness verifier (§5.6 hard gate): forward 1-bit "DIT on" dataflow over the
  // EMITTED MIR (AND-meet at joins, clobber ⇒ off), asserting DIT is on at every
  // Need. A missed domination is a leaked secret ⇒ fatal, not silent.
  // AND-meet (DIT-on required from every predecessor) ⇒ initialize the fixed
  // point OPTIMISTICALLY to true, else a loop whose DIT is carried in (no enable
  // inside the loop) would never converge to on (the backedge would start false
  // and the meet would pin it there — a false "uncovered" report). The entry
  // boundary is off, enforced by `In = !pred_empty()` below.
  DenseMap<const MachineBasicBlock *, bool> OnOut, OnIn;
  for (MachineBasicBlock &MBB : MF)
    OnOut[&MBB] = true;
  auto stepBlock = [&](MachineBasicBlock &MBB, bool In) -> bool {
    bool Cur = In;
    for (MachineInstr &MI : MBB) {
      if (auto Sw = TII->getTimingModeSwitch(MI))
        Cur = *Sw;
      else if (clobbersDIT(MI, TSI, M))
        Cur = false;
    }
    return Cur;
  };
  bool Changed = true;
  while (Changed) {
    Changed = false;
    for (MachineBasicBlock &MBB : MF) {
      bool In = !MBB.pred_empty();
      for (MachineBasicBlock *P : MBB.predecessors())
        In &= OnOut[P];
      OnIn[&MBB] = In;
      bool Out = stepBlock(MBB, In);
      if (Out != OnOut[&MBB]) {
        OnOut[&MBB] = Out;
        Changed = true;
      }
    }
  }
  // Final check: replay (which supplies per-MI TaintFacts) tracking the DIT-on
  // state seeded from OnIn at each block boundary; every Need must be on.
  // replayTaint visits each block's instructions contiguously in layout order,
  // so a block-change reset is sufficient.
  const MachineBasicBlock *CurBlk = nullptr;
  bool CurOn = false;
  bool Sound = true;
  replayTaint(MF, TR, TSI, AA,
              [&](MachineInstr &MI, const TaintFacts &F, const TaintState &) {
                if (MI.getParent() != CurBlk) {
                  CurBlk = MI.getParent();
                  CurOn = OnIn.lookup(CurBlk);
                }
                if (auto Sw = TII->getTimingModeSwitch(MI)) {
                  CurOn = *Sw;
                  return true;
                }
                if (needsDIT(MI, F, *TII) && !CurOn)
                  Sound = false;
                if (clobbersDIT(MI, TSI, M))
                  CurOn = false;
                return true;
              });

  if (!Sound) {
    // The verifier could not prove every Need is DIT-covered. Rather than abort
    // the whole TU, degrade this function to the always-safe whole-function
    // policy: erase the region switches we inserted (the input had none, so
    // every DIT switch present is ours) and re-emit function granularity.
    for (MachineBasicBlock &MBB : MF) {
      SmallVector<MachineInstr *, 8> ToErase;
      for (MachineInstr &MI : MBB)
        if (TII->getTimingModeSwitch(MI))
          ToErase.push_back(&MI);
      for (MachineInstr *I : ToErase)
        I->eraseFromParent();
    }
    errs() << "taint: DIT region placement could not prove coverage in "
           << MF.getName() << "; fell back to function granularity\n";
    emitFunctionGranularityDIT(MF, TSI);
  }
  return NeedCount;
}

unsigned llvm::insertTaintDITSwitches(MachineFunction &MF,
                                      const TaintResult &TR,
                                      const TaintSummaryInfo *TSI,
                                      raw_ostream *RegionsOS, AAResults *AA) {
  unsigned TaintedInstrCount = 0;
  SmallVector<TaintedRun, 64> TaintedRuns =
      collectTaintedRuns(MF, TR, TSI, TaintedInstrCount, AA);

  // The runs no longer drive placement (DIT is function-granularity), but they
  // still drive the region reports — and they are the input the cost-model-
  // driven region placement will consume. See utils/taint_dit_cost_model.md.
  if (RegionsOS)
    printTaintedRuns(MF, TaintedRuns, *RegionsOS);

  if (TaintedRuns.empty())
    return TaintedInstrCount;

  // Track B: cost-model region placement (WIP). Opt-in; default stays
  // function-granularity below, so shipped codegen is untouched. Both modes
  // report the same tainted-instruction count.
  if (TaintDITPlacement == DITPlacementMode::Region) {
    insertTaintDITRegions(MF, TR, TSI, AA);
    return TaintedInstrCount;
  }

  // Function granularity: run the whole function in data-independent-timing
  // mode when it contains any tainted instruction. Per-region toggles would
  // clear an enclosing region's DIT when a tainted callee's exit switch runs
  // inside a caller's still-open region.
  emitFunctionGranularityDIT(MF, TSI);

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

    if (TaintInsertDIT)
      BarriersInserted =
          insertTaintDITSwitches(MF, TR, TSI, RegionsOS.get(), AA);
    else if (RegionsOS)
      exportTaintBarrierRegions(MF, TR, TSI, *RegionsOS, AA);

    if (SourceRegionsOS)
      exportTaintSourceRegions(MF, TR, TSI, *SourceRegionsOS, AA);
  }

  if (BarriersInserted)
    return PreservedAnalyses::none();
  return getMachineFunctionPassPreservedAnalyses();
}
