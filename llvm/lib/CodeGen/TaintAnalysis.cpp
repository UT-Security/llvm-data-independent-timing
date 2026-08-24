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
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineBranchProbabilityInfo.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/Support/TypeSize.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachinePassManager.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/PseudoSourceValue.h"
#include "llvm/CodeGen/TaintSummaryInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/Instructions.h"
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

cl::opt<std::string> llvm::TaintDITPrecisionReportFile(
    "taint-dit-precision-report",
    cl::desc("Output file for DIT accounting: per function, how many "
             "instructions must run with DIT set vs how many actually do. "
             "collateral = public code paying DIT's cost for nothing; "
             "precision = need/underdit is the number placement should "
             "maximize (see docs/design/dit-precision.md)"),
    cl::value_desc("file"));

cl::opt<std::string> llvm::TaintDITReassertReportFile(
    "taint-dit-reassert-report",
    cl::desc("Output file listing every call site where the callee could not be "
             "proven to leave PSTATE.DIT alone, so DIT was re-asserted after the "
             "call. These sites are sound (the re-assert restores protection "
             "unconditionally); they are the per-call toggle cost, and the list "
             "of sites the proposed (not yet implemented) runtime-MRS mode would "
             "eliminate -- see docs/design/dit-callee-ownership.md"),
    cl::value_desc("file"));

cl::opt<std::string> llvm::TaintUncoveredReportFile(
    "taint-uncovered-report",
    cl::desc("Output file for tainted instructions PSTATE.DIT does not protect "
             "(divide/sqrt, secret-dependent addresses, secret branches) — "
             "silent false assurance otherwise (gap G2)"),
    cl::value_desc("file"));

cl::opt<std::string> llvm::TaintClobberReportFile(
    "taint-clobber-report",
    cl::desc("Output file for call sites that make the caller treat memory as "
             "secret (ExternalMemClobbered / whole-global) — the sources of "
             "cross-function memory taint, i.e. where a taint explosion "
             "originates"),
    cl::value_desc("file"));

cl::opt<std::string> llvm::TaintFrameRefReportFile(
    "taint-frameref-report",
    cl::desc("Output file for frame-address resolution: for every register "
             "computed as base+imm off SP/FP, whether it resolves to a specific "
             "frame object. The go/no-go measurement for P1b — per-object pointee "
             "taint is only worth building if most frame addresses resolve."),
    cl::value_desc("file"));

cl::opt<std::string> llvm::TaintDITJoinReportFile(
    "taint-dit-join-report",
    cl::desc("Output file for Off->On placement boundaries, split by whether the "
             "join is MIXED (some predecessors already DIT-on). A mixed join emits "
             "one enable at the block start that executes on EVERY incoming path, "
             "redundantly on the already-on ones — the cost edge bundling or edge "
             "splitting would remove."),
    cl::value_desc("file"));

// The call-site mod-set gate, and its two companion rules, are ON. A callee's
// memory clobber is applied only at call sites that actually pass a secret
// (instead of at every call site of that callee), restricted to callees that
// have at least one tainted argument and whose taint is argument-sourced; the
// same rule applies to the `ReturnsTainted` register summary. Together they are
// the pass's answer to context-insensitive mod-sets, which is not optional
// engineering: without the gate, `secp256k1_ecdsa_verify` — public data only —
// carries 17 `MSR DIT` and Bitcoin Core's ConnectBlockAllEcdsa costs +51.20%,
// so the default configuration would lose to blanket DIT by 50 points on the
// most realistic workload measured. With it that becomes +0.67% at no loss of
// coverage (gem5 ditSuppressed 103.1% of a hand oracle).
//
// THE UNSOUNDNESS IS REAL AND BOUNDED, and this flag is the way out of it. The
// gate assumes a callee's secret arrived through its arguments; a callee that
// obtains one from a global or an earlier call is no longer covered by the
// caller's reloads. `flowprobe` confirmed four such channels by reading
// PSTATE.DIT at the consumer (callee returns a pointer into a secret buffer;
// secret in a global read by a sibling with no call edge; secret stored through
// a pointer by inline asm; secret moved through a NEON register tuple). The
// defensible claim is "preserves coverage for argument-carried taint", which is
// how libsecp256k1 and every library measured carry theirs. Pass
// -taint-no-modset-gate to give that up for whole-memory conservatism.
// `WritesSecretToGlobal` is never gated: it is already per-global rather than a
// flood, and it is the "secret arrived through a global" case the gate is
// unsound for. See docs/design/context-insensitivity.md, source-condition.md.
static cl::opt<bool> TaintNoModsetGate(
    "taint-no-modset-gate",
    cl::desc(
        "Disable the call-site mod-set gate: apply a callee's memory "
        "clobber at every call site of that callee, not only where a secret "
        "is passed. Maximally conservative and much slower — the escape "
        "hatch for taint that does not travel by arguments."),
    cl::init(false));

// Track B: PSTATE.DIT placement granularity. `region` (the DEFAULT) is fine-grain
// cost-model placement — DIT covers only the secret-dependent regions, leaving clean
// preambles off and hoisting enables out of loops (docs/design/dit-placement.md §5.6;
// tuned by -taint-dit-switch-cyc / -taint-dit-dwell-per-instr / -taint-dit-loop-hoist).
// `function` is the coarse whole-function policy (DIT on entry-to-return for any
// tainted function). Region placement carries a soundness verifier and falls back to
// function granularity per-function if it cannot prove coverage, so it is always safe.
namespace {
enum class DITPlacementMode { Function, Region };
} // namespace
static cl::opt<DITPlacementMode> TaintDITPlacement(
    "taint-dit-placement", cl::desc("PSTATE.DIT placement granularity"),
    cl::init(DITPlacementMode::Region),
    cl::values(clEnumValN(DITPlacementMode::Function, "function",
                          "whole-function: DIT on entry-to-return for any tainted "
                          "function (coarse)"),
               clEnumValN(DITPlacementMode::Region, "region",
                          "fine-grain cost-model region placement (default)")));

// Track B increment (c): the admission test (docs/design/dit-placement.md
// §5.6). A DIT mode switch (MSR DIT) costs `switch-cyc` cycles; DIT dwell costs
// `dwell-per-instr` cycles per covered instruction. An interior Off corridor
// between two On regions is merged (kept DIT-on straight through) when the
// toggle pair it removes costs at least the dwell it would add, both weighted
// by MachineBlockFrequencyInfo. Together these knobs are the frequency-aware
// replacement for the static `-taint-region-merge-gap` proxy.
//
// The switch cost DEFAULTS TO 30 cycles. At freq 1 a corridor of N instructions
// merges iff `switch-cyc * 2 >= dwell-per-instr * N`, so 30 against a dwell of
// 1 puts the static crossover at ~60 clean instructions (the measured P≈40-64
// fine-grain crossover). Only reached under -taint-dit-placement=region.
//
// WHY NOT 0. A zero switch cost makes the left side of the admission test
// identically zero, so only an empty corridor ever merges — the default would
// be asserting that emitting a switch is free, which no measurement supports.
// On the libsodium composite (native M5, 20 paired reps per point, both public
// lanes), moving from 0 to 30 is worth -8.96/-6.08/-4.01/-1.99 points at 200 B
// through 1400 B, slower in 0 of 20 reps at every one of them, and it converts
// the 512 B verdict against blanket DIT from a loss slower in 20/20 reps into a
// win. Above ~5 us of work per region it goes inert, which is expected: there is
// little left to merge. Data `~/Documents/dit-crossover/out/native/confirm_*.jsonl`,
// write-up docs/results/dit-switch-cyc-confirmation.md.
//
// !! WHAT THIS PARAMETER ACTUALLY PRICES, because the name misleads. It is NOT
// the cost of the PSTATE.DIT mode switch. Against the NOP control
// (-taint-dit-nop-switches, every `MSR DIT` emitted as `HINT #0` at an identical
// address, so no DIT executes at all) the real builds are indistinguishable:
// across 24 measurements the def-minus-NOP term runs -0.33 to +0.97 points
// against machine drift of up to 1.15, and is often NEGATIVE because `HINT #0`
// is itself ~0.25% slower than a real op. The NOP arms also reproduce the
// switch-cyc win almost exactly (at 512 B, -4.65 real against -4.65 NOP). So
// what 30 buys is FEWER INSTRUCTIONS INSERTED INTO HOT LOOPS, and the mode
// switch itself is unresolvable at every region size measured.
//
// THE TRAP THAT FOLLOWS: do not lower this for hardware with a renamed
// (non-serializing) `MSR DIT`. Renaming makes the mode switch cheaper, and the
// mode switch is not the term being paid — the instruction's presence in the
// loop is, and renaming does not remove it. The published 22.6 cyc serializing
// / 9.7 cyc renamed figures are real but they are gem5 numbers for the switch
// alone; they do not govern this default.
//
// DO NOT BOTHER RAISING IT. Measured 2026-08-24 on libsodium: this knob is a
// three-step staircase, not a dial. switch-cyc=30 and =100 produce BYTE-IDENTICAL
// objects (397 switches), and every value from 300 through 100,000 produces one
// identical object (395). At a ratio of 100,000:1 the compiler emits the same
// code as at 300:1, so everything the admission test can merge is already merged
// at 30, and timing the one pair that differs is a null result (+0.32% pooled,
// 68/120 reps, ~1.5 sigma). The switches that survive are not interior corridors
// at all - they are entry enables, exit clears and post-call re-asserts, which
// only callee ownership reaches.
//
// The error is ASYMMETRIC, which is why erring high is right: merging a corridor
// keeps DIT on across it, which costs dwell (cheap — 0.0039 cyc per suppressed
// op — and fail-SAFE, since it widens coverage and never narrows it), while
// failing to merge leaves a switch pair in the instruction stream, which the
// measurements above say is the term that actually costs. Err high.
static cl::opt<double> TaintDitSwitchCyc(
    "taint-dit-switch-cyc",
    cl::desc(
        "Cost in cycles of one PSTATE.DIT mode switch (MSR DIT), used by the "
        "region-placement admission test. Default 30 (the measured cost on "
        "serializing-switch hardware); 0 makes toggles free so the test never "
        "merges, which is the finest-grain placement and measurably wrong"),
    cl::init(30.0));

static cl::opt<double> TaintDitDwellPerInstr(
    "taint-dit-dwell-per-instr",
    cl::desc("DIT dwell cost in cycles per covered instruction, used by the "
             "region-placement admission test to decide whether to merge a "
             "clean corridor between two DIT regions"),
    cl::init(1.0));

// DEFAULT is true = each need-loop is coarsened On and the enable is hoisted to
// the loop preheader: one toggle, whole loop covered. Set =0 for BLOCK-MINIMAL
// coverage — On(b)=HasNeed(b) only, so DIT wraps just the blocks that actually
// contain a secret instruction and a loop's public scaffolding (loop control,
// index math) that shares no block with a secret op runs unprotected, at the
// cost of a per-iteration enable/disable around a need-block reached by a
// backedge.
//
// WHY NOT BLOCK-MINIMAL. Per-iteration toggling in a hot loop is what the cost
// model exists to avoid, and it is the difference between reaching the hand
// oracle and not: on CPython and SQLite hosts, hoisting takes the pass from
// +0.91%/+0.35% to -0.08%/-0.02%, i.e. indistinguishable from the oracle and
// from zero. Block-minimal placement is also what produced SQLCipher's 888,967
// executed switches against a hand oracle's 3,051 (291x) in a compression loop.
// Where it does not help it is within ±0.5 pp, and hoisting is fail-SAFE: the
// loop is covered for longer, never for less. Raising -taint-dit-switch-cyc
// re-coarsens via the admission test either way. Region mode only.
static cl::opt<bool> TaintDitLoopHoist(
    "taint-dit-loop-hoist",
    cl::desc(
        "Coarsen need-loops On and hoist the DIT enable to the loop preheader "
        "(default true: one toggle, whole loop covered). =0 selects "
        "block-minimal coverage — only need-containing blocks are DIT-on, "
        "with per-iteration toggles."),
    cl::init(true));

/// Unified cell extraction from a MachineMemOperand.
/// Returns the base kind (Stack/Global/Unknown), offset, and optional size.
struct CellInfo {
  enum Kind { Stack, Global, Arg, OutgoingArg, Unknown };
  Kind K = Unknown;
  int FI = 0;
  const GlobalVariable *GV = nullptr;
  unsigned ArgNo = 0; // valid when K == Arg (P1 argument-provenance)
  int64_t Offset = 0;
  std::optional<uint64_t> Size; // nullopt if unknown/scalable
};

/// Maps a (base register, byte offset) pair back to the frame object it lands
/// in.
///
/// This is the capability P1b needs and the pass has never had.
/// Post-prologepilog
/// `&local` is `$x0 = ADDXri $sp, 232` — no FrameIndex operand, no MMO — so the
/// analysis could tell that a register held *a* frame address but not *which*
/// object it pointed at. Everything downstream had to fall back to whole-frame
/// reasoning, under which a callee's arg-pointee mod-set collapses to a
/// whole-caller ExternalMemClobbered.
///
/// The frame layout is fixed by the time this pass runs, and
/// TargetFrameLowering can state each object's offset from its base register,
/// so the map is just built once and queried by range containment. Interior
/// offsets (`&buf[8]`) resolve to the containing object, which is what a
/// pointee query wants.
class FrameObjectMap {
  struct Entry {
    Register Base;
    int64_t Start;
    int64_t Size;
    int FI;
  };
  SmallVector<Entry, 16> Entries;

public:
  explicit FrameObjectMap(const MachineFunction &MF) {
    const MachineFrameInfo &MFI = MF.getFrameInfo();
    const TargetFrameLowering *TFL = MF.getSubtarget().getFrameLowering();
    if (!TFL)
      return;
    // getFrameIndexReference needs a finalised frame. In a real pipeline this
    // pass runs after prologue/epilogue insertion so it always is, but MIR
    // loaded directly by a test never ran PEI. Leaving the map empty makes every
    // consumer take its pre-P1b conservative path, which is the correct
    // degradation rather than an abort.
    if (!MFI.isCalleeSavedInfoValid())
      return;
    for (int FI = MFI.getObjectIndexBegin(), E = MFI.getObjectIndexEnd(); FI < E;
         ++FI) {
      if (MFI.isDeadObjectIndex(FI))
        continue;
      int64_t Sz = MFI.getObjectSize(FI);
      if (Sz <= 0)
        continue;
      Register Base;
      StackOffset SO = TFL->getFrameIndexReference(MF, FI, Base);
      // Scalable offsets (SVE) cannot be compared against a constant immediate.
      if (SO.getScalable() != 0 || !Base.isValid())
        continue;
      Entries.push_back({Base, SO.getFixed(), Sz, FI});
    }
  }

  /// The object containing Base+Off, or nullopt when nothing does — an offset
  /// into padding, a spill area this pass does not model, or a frame whose
  /// objects were all skipped above. Callers MUST treat nullopt as "unknown" and
  /// fall back to their existing conservative behaviour.
  std::optional<int> resolve(Register Base, int64_t Off) const {
    for (const Entry &E : Entries)
      if (E.Base == Base && Off >= E.Start && Off < E.Start + E.Size)
        return E.FI;
    return std::nullopt;
  }

  bool empty() const { return Entries.empty(); }
  size_t size() const { return Entries.size(); }
};

/// If MI computes `base + imm` into a register, return (base, imm).
/// Uses TargetInstrInfo::isAddImmediate so no target opcode is named here.
static std::optional<std::pair<Register, int64_t>>
matchAddImm(const MachineInstr &MI, const TargetInstrInfo *TII) {
  if (MI.getNumOperands() < 1 || !MI.getOperand(0).isReg() ||
      !MI.getOperand(0).isDef() || !MI.getOperand(0).getReg().isValid())
    return std::nullopt;
  if (auto RI = TII->isAddImmediate(MI, MI.getOperand(0).getReg()))
    return std::make_pair(RI->Reg, RI->Imm);
  return std::nullopt;
}

/// Find the frame object backing AI. Returns nullopt when the alloca has no
/// frame object (promoted to registers, or optimized away), in which case the
/// caller leaves the cell Unknown — the over-approximating direction.
static std::optional<int> findFrameIndexForAlloca(const MachineFunction &MF,
                                                  const AllocaInst *AI) {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  for (int FI = MFI.getObjectIndexBegin(), E = MFI.getObjectIndexEnd(); FI != E;
       ++FI) {
    if (MFI.isDeadObjectIndex(FI))
      continue;
    if (MFI.getObjectAllocation(FI) == AI)
      return FI;
  }
  return std::nullopt;
}

static CellInfo getCellFromMMO(const MachineMemOperand &MMO,
                               const MachineFunction *MF) {
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
  // The OUTGOING argument area: `STRXui $x0, $sp, 0 :: (store (s64) into stack)`.
  // AAPCS64 passes arguments past the eighth (and large aggregates) here, and the
  // argument registers are typically overwritten before the call — so a secret
  // written here is invisible to any register-based test at the call site. It is
  // NOT a FixedStackPseudoSourceValue (that kind covers INCOMING args and spill
  // slots) and carries no IR Value, so before this case it fell through to
  // Unknown and only set the opaque clobber bit.
  if (const PseudoSourceValue *PSV = MMO.getPseudoValue()) {
    if (PSV->kind() == PseudoSourceValue::Stack) {
      CI.K = CellInfo::OutgoingArg;
      return CI;
    }
  }
  if (const Value *V = MMO.getValue()) {
    const Value *UO = getUnderlyingObject(V);
    if (auto *GV = dyn_cast<GlobalVariable>(UO)) {
      CI.K = CellInfo::Global;
      CI.GV = GV;
      return CI;
    }
    // A source-level local. These arrive as ordinary IR pointers (%ir.nonce,
    // %ir.add.ptr6), NOT as FixedStackPseudoSourceValue — that pseudo-value
    // covers spill slots and fixed (incoming-argument) objects. Without this
    // case every user local fell through to Unknown, so the "cell-level stack
    // precision" the design claims covered spills but not the buffers secrets
    // actually live in, and every store into a local made the function's
    // mod-set TOP.
    if (auto *AI = dyn_cast<AllocaInst>(UO)) {
      if (!MF)
        return CI; // no frame info to resolve against: stay Unknown
      std::optional<int> FI = findFrameIndexForAlloca(*MF, AI);
      if (!FI)
        return CI;
      CI.K = CellInfo::Stack;
      CI.FI = *FI;
      // Offset within the object is trustworthy only when the access pointer is
      // the alloca plus a CONSTANT offset. Under a variable index (a[i]) the
      // access could touch any part of the object, so drop to whole-object
      // (Size = nullopt): the load path then reads it with
      // anyTaintedStackCellForFI and the store path records it under the size-0
      // sentinel. Keeping a bogus precise (offset,size) would let a store and a
      // load of the same object miss each other — an UNDER-taint.
      const DataLayout &DL = MF->getFunction().getDataLayout();
      APInt Off(DL.getIndexTypeSizeInBits(V->getType()), 0);
      const Value *Base =
          V->stripAndAccumulateConstantOffsets(DL, Off,
                                               /*AllowNonInbounds=*/true);
      if (Base == UO)
        CI.Offset += Off.getSExtValue();
      else
        CI.Size = std::nullopt;
      return CI;
    }
    // P1 argument provenance: a store/load whose pointer bottoms out at a
    // function Argument is the canonical callee->caller-through-memory access.
    // Record which argument so the mod-set can name it precisely instead of TOP.
    if (auto *A = dyn_cast<Argument>(UO)) {
      CI.K = CellInfo::Arg;
      CI.ArgNo = A->getArgNo();
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
  for (const MachineOperand &MO : MI.uses()) {
    // MI.uses() starts after the EXPLICIT defs, so it still spans implicit
    // DEFS (on AArch64 a 32-bit result carries `implicit-def $xN`, and calls
    // carry the $lr clobber). Counting those as uses made an instruction that
    // merely re-defines a tainted register look like it READ one: e.g.
    // `dead $w0 = MOVi32imm 1, implicit-def $x0` with $x0 tainted set its own
    // defs as tainted instead of clearing them — a self-sustaining loop taint
    // could never leave. taintedCallArguments already guards this way.
    if (!MO.isReg() || MO.isDef())
      continue;
    if (S.test(K, MO.getReg()))
      return true;
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
// Fix B: a secret is *passed* to a callee only if a tainted register appears as
// a genuine argument-register USE operand of the call. AAPCS64 passes arguments
// in X0-X7 / V0-V7 (encodings 0-7); a register merely live across the call
// (caller-saved) or the call's implicit-def LR clobber is NOT an argument an
// ABI-compliant callee can read as input, so it must not count as a passed
// secret. Pointee taint on an argument pointer DOES count — a public pointer to
// secret memory is a real reach (channel 2). Soundness rests on the callee being
// ABI-compliant; a callee that scavenges caller-saved registers would need
// in-process code execution, which defeats DIT regardless. See
// taint-analysis-call-arg-passed.mir.
CallArgTaint llvm::taintedCallArguments(const MachineInstr &MI,
                                        const TaintState &S,
                                        const TargetRegisterInfo *TRI) {
  CallArgTaint R;
  if (!MI.isCall())
    return R;
  for (const MachineOperand &MO : MI.uses()) {
    // MI.uses() spans implicit-defs too (e.g. the LR clobber); skip defs.
    if (!MO.isReg() || MO.isDef() || !MO.getReg().isValid() ||
        !MO.getReg().isPhysical())
      continue;
    if (TRI->getEncodingValue(MO.getReg()) > 7)
      continue; // not an argument-passing register
    if (S.test(TaintKind::Data, MO.getReg()))
      R.Data = true;
    if (S.test(TaintKind::Pointee, MO.getReg()))
      R.Pointee = true;
    // Fallback (-taint-frame-addr-args): the argument is an address into a
  }
  // Stack-passed arguments. AAPCS64 puts arguments past the eighth (and large
  // aggregates) in the outgoing argument area, and the argument registers are
  // usually overwritten before the call, so the loop above sees nothing. Any
  // secret stored there since the last call is an argument of THIS call.
  //
  // Reported as Data, not Pointee: what was stored is the argument value itself.
  // A pointer-to-secret stored there also lands here, which over-approximates
  // Data for that case — the safe direction, and it still makes the call count
  // as secret-passing, which is what every consumer of this predicate asks.
  if (S.isOutgoingArgSecret())
    R.Data = true;
  return R;
}

static bool anyTaintedCallArgument(const MachineInstr &MI, const TaintState &S,
                                   const TargetRegisterInfo *TRI) {
  return taintedCallArguments(MI, S, TRI).any();
}

// True when this callee's memory clobber may be suppressed at a call site that
// passes no secret. Only a callee that has a tainted argument qualifies: if the
// analysis never found a secret arriving through this function's parameters,
// then whatever secret its mod-set records came from a global, from state it
// carries across calls, or from a callee's return value — none of which this
// caller's arguments can speak for. A PROXY, not a proof: a callee can take a
// secret argument AND read a secret global, and this does not catch that. The
// sound rule is an origin bit computed in the fixed point ("all of this
// function's taint originates from its own arguments"); -taint-no-modset-gate
// is the way out until then. True when the summary records any caller-visible
// secret write at all.
static bool ME_HasEffect(const FunctionMemEffects &ME) {
  return ME.WritesSecretToUnknown || !ME.WritesSecretToGlobal.empty() ||
         !ME.WritesSecretThroughArgPointee.empty();
}

static bool calleeClobberIsGateable(const FunctionTaintSummary &S) {
  // THE SOURCE CONDITION. Suppressing a callee's clobber at a call site that
  // passes no secret is valid only if the callee's secret could have come from
  // a caller at all. Two necessary conditions, both cheap:
  //
  //   1. none of its recorded effects is non-argument-sourced — it did not read
  //      the secret out of a global, out of another TU, or out of a call it
  //      made without passing a secret itself (transitively);
  //   2. it names at least one tainted parameter, so there is an argument for
  //      the caller's arguments to speak for.
  //
  // (1) is the real condition and subsumes (2) in principle; (2) is retained
  // because it is free, independently sound, and catches a callee whose effects
  // are empty-but-conservative — and it measured byte-identical on every
  // library screened, so it costs nothing. Neither is per-effect: see
  // FunctionMemEffects::NonArgSourced for what that costs.
  if (S.MemEffects.NonArgSourced)
    return false;
  return !S.TaintedArgIndices.empty() || !S.PointeeTaintedArgIndices.empty();
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
                             AAResults *AA,
                             const FrameObjectMap *FOM = nullptr) {
  // P1b frame provenance. Every def kills the defined register's provenance;
  // an add-immediate off SP/FP establishes it; a COPY inherits it. Deliberately
  // narrow: no arithmetic on an existing frame pointer is followed, because a
  // computed offset could leave the object and mis-attributing provenance is the
  // one direction that under-taints.
  if (FOM && !FOM->empty()) {
    const TargetInstrInfo *TIIp = MI.getMF()->getSubtarget().getInstrInfo();
    for (const MachineOperand &MO : MI.all_defs())
      if (MO.isReg() && MO.getReg().isValid())
        S.clearFrameRef(MO.getReg());
    if (MI.isCopy() && MI.getOperand(1).isReg()) {
      if (auto FI = S.getFrameRef(MI.getOperand(1).getReg()))
        S.setFrameRef(MI.getOperand(0).getReg(), *FI);
    } else if (auto AI = matchAddImm(MI, TIIp)) {
      const MachineFunction *MFp = MI.getMF();
      Register FP = TRI->getFrameRegister(*MFp);
      Register SP = MFp->getSubtarget()
                        .getTargetLowering()
                        ->getStackPointerRegisterToSaveRestore();
      Register Dst = MI.getOperand(0).getReg();
      bool DstIsBase = (SP.isValid() && Dst == SP) || (FP.isValid() && Dst == FP);
      if (!DstIsBase &&
          ((SP.isValid() && AI->first == SP) || (FP.isValid() && AI->first == FP)))
        if (auto FI = FOM->resolve(AI->first, AI->second))
          S.setFrameRef(Dst, *FI);
    }
  }

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
      CellInfo CI = getCellFromMMO(*MMO, MI.getMF());

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
      } else if (CI.K == CellInfo::OutgoingArg) {
        // Arm the bit for the next call. Deliberately one-directional: an
        // untainted store to the outgoing area does NOT disarm it, because the
        // area holds several arguments at different offsets and clearing on any
        // one of them would lose a secret written at another. The bit is cleared
        // where it is consumed — at the call — so it cannot leak past it.
        if (DataTainted || PointeeDataTainted) {
          S.setOutgoingArgSecret();
          LLVM_DEBUG(dbgs() << "      secret stored into the OUTGOING argument "
                               "area (stack-passed argument)\n");
        }
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
    // A load consumes CROSS-function memory poison as well as this function's
    // own stores: a callee's mod-set applied as ExternalMemClobbered /
    // whole-global, and another TU's unknown-mem taint. This is the sound
    // direction and the single choke point where the
    // callee->caller-through-memory transfer is observed.
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
      CellInfo CI = getCellFromMMO(*MMO, MI.getMF());
      if (CI.K == CellInfo::Stack) {
        // A call that clobbered unknown memory (mod-set TOP) may have written
        // through a pointer into this frame — blunt P0 poisons every stack load
        // after such a call. Provenance-based escaped-object precision (only
        // poison stack objects whose address escaped) is the P1 refinement.
        bool Tainted =
            S.isExternalMemClobbered() ||
            (CI.Size
                 ? S.isTaintedStackCellOverlapping(CI.FI, CI.Offset, *CI.Size)
                 : S.anyTaintedStackCellForFI(CI.FI));
        bool PointeeTainted =
            CI.Size ? S.isPointeeTaintedStackCellOverlapping(CI.FI, CI.Offset,
                                                            *CI.Size)
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
          if (TSI && TSI->hasUnknownMemTainted())
            S.setNonArgSourcedTaint(); // another TU's secret, not our caller's
          LLVM_DEBUG(dbgs() << "      load from global " << CI.GV->getName()
                            << " (poisoned by unknown mem)\n");
        } else {
          bool Tainted = S.isWholeGlobalTainted(CI.GV) ||
                         (CI.Size ? S.isTaintedGlobalCellOverlapping(
                                        CI.GV, CI.Offset, *CI.Size)
                                  : S.anyTaintedGlobalCellForGV(CI.GV));
          if (Tainted) {
            ShouldTaint = true;
            // Source condition: this taint came out of a global, not out of a
            // parameter, so no caller's arguments can account for it. Applies
            // even when THIS function tainted the global from its own parameter
            // — attributing that would need per-global source tracking, and the
            // over-approximation errs toward applying the clobber.
            S.setNonArgSourcedTaint();
            LLVM_DEBUG(dbgs()
                       << "      load from tainted global cell "
                       << CI.GV->getName() << " off=" << CI.Offset
                       << " (NON-ARG-SOURCED taint)\n");
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

    bool HasTaintedArg = anyTaintedCallArgument(MI, S, TRI);
    // Consumed: this call took the outgoing area as its arguments. The next call
    // stores its own, so leaving the bit set would make every later call in the
    // block look secret-passing.
    S.clearOutgoingArgSecret();
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
      // Does taint entering from THIS callee trace back to our own parameters?
      // Only if we handed it a secret AND its own taint is parameter-sourced.
      // Otherwise the secret originated below us and no caller of ours can
      // account for it — the transitive half of the source condition.
      // "We passed it a secret" is not by itself evidence that the taint coming
      // back is parameter-sourced: the secret WE passed may itself have reached
      // us from a global or from a call we did not supply. If this function
      // already holds non-argument-sourced taint, anything it hands a callee
      // could be that taint, so the return cannot be attributed to our
      // parameters either.
      const bool CalleeTaintIsOurs = HasTaintedArg &&
                                     !Summary.MemEffects.NonArgSourced &&
                                     !S.hasNonArgSourcedTaint();

      // A callee whose taint is argument-sourced can only hand back a secret to
      // a caller that gave it one. A non-argument-sourced callee returns taint
      // regardless — its secret did not come through its parameters.
      const bool ReturnApplies =
          Summary.MemEffects.NonArgSourced || HasTaintedArg;
      if (Summary.ReturnsTainted && ReturnApplies) {
        taintCallResultDefs(MI, S, TRI);
        if (!CalleeTaintIsOurs)
          S.setNonArgSourcedTaint();
        LLVM_DEBUG(dbgs() << "        call to " << Callee->getName()
                          << " returns tainted value"
                          << (CalleeTaintIsOurs ? "" : " (NON-ARG-SOURCED)")
                          << "\n");
      }

      // Apply the callee's memory-effects (mod-set): what secret it may have
      // written into caller-visible memory (the callee->caller-through-memory
      // transfer). The summary itself is context-insensitive and must stay
      // COMPLETE, because its own transitive re-export
      // (computeFunctionMemEffects) reads these clobber/global bits and the
      // reports surface them.
      //
      // The call-site gate makes the APPLICATION context-sensitive: the clobber
      // is suppressed at call sites that pass no secret. That deliberately
      // reaches the transitive re-export too — a caller that absorbs no clobber
      // re-exports none — which is what stops the false-positive cascade rather
      // than merely hiding its last hop. Consequently the gate changes
      // SUMMARIES, not just codegen. WritesSecretToGlobal is never gated: it is
      // already per-global rather than a flood, and it is the "secret arrived
      // through a global" case the gate is unsound for. -taint-no-modset-gate
      // restores the every-call-site behaviour.
      const FunctionMemEffects &ME = Summary.MemEffects;
      for (const GlobalVariable *GV : ME.WritesSecretToGlobal)
        S.setTaintedWholeGlobal(GV);
      if ((ME_HasEffect(Summary.MemEffects)) && !CalleeTaintIsOurs)
        S.setNonArgSourcedTaint();

      const bool Gateable =
          !TaintNoModsetGate && calleeClobberIsGateable(Summary);
      if (ME.WritesSecretToUnknown) {
        if (!Gateable || HasTaintedArg) {
          S.setExternalMemClobbered();
          LLVM_DEBUG(dbgs()
                     << "        call to " << Callee->getName()
                     << " writes secret to unknown memory (mod-set TOP)\n");
        } else {
          LLVM_DEBUG(dbgs() << "        call to " << Callee->getName()
                            << " has mod-set TOP but this call site passes no "
                               "secret: clobber suppressed (callsite-gated)\n");
        }
      }
      // P1a: the callee writes a secret through one of its pointer arguments. The
      // precise fix (P1b) taints only the pointee of the pointer THIS caller passed;
      // until then it is a blunt clobber. (Its flood is suppressed at consumption in
      // annotation-driven mode, like the TOP case above.)
      if (!ME.WritesSecretThroughArgPointee.empty()) {
        if (!Gateable || HasTaintedArg) {
          // P1b: taint the object the caller actually passed for argument i,
          // instead of collapsing to a whole-caller clobber. Falls back to the
          // blunt clobber for any index whose pointer provenance is unknown, so
          // this is never less conservative than P1a.
          bool AllResolved = FOM && !FOM->empty();
          SmallVector<int, 4> Objs;
          if (AllResolved) {
            const MachineFunction *MFa = MI.getMF();
            for (unsigned Idx : ME.WritesSecretThroughArgPointee) {
              std::optional<int> Found;
              for (const MachineOperand &MO : MI.uses()) {
                if (!MO.isReg() || MO.isDef() || !MO.getReg().isValid() ||
                    !MO.getReg().isPhysical())
                  continue;
                if (TRI->getEncodingValue(MO.getReg()) != Idx)
                  continue;
                if (auto FI = S.getFrameRef(MO.getReg()))
                  Found = FI;
                break;
              }
              if (!Found) {
                AllResolved = false;
                break;
              }
              Objs.push_back(*Found);
            }
            (void)MFa;
          }
          if (AllResolved) {
            const MachineFrameInfo &MFI = MI.getMF()->getFrameInfo();
            for (int FI : Objs) {
              int64_t Sz = MFI.getObjectSize(FI);
              if (Sz > 0)
                S.setTaintedStackCell(FI, 0, (uint64_t)Sz);
              else
                S.setExternalMemClobbered();
            }
            LLVM_DEBUG(dbgs()
                       << "        call to " << Callee->getName()
                       << " writes secret through a pointer arg (P1b: tainted "
                       << Objs.size() << " caller object(s) precisely)\n");
          } else {
          S.setExternalMemClobbered();
          LLVM_DEBUG(dbgs()
                     << "        call to " << Callee->getName()
                     << " writes secret through a pointer arg (P1a: blunt "
                        "clobber, provenance unknown)\n");
          }
        } else {
          LLVM_DEBUG(dbgs()
                     << "        call to " << Callee->getName()
                     << " writes secret through a pointer arg but this call "
                        "site passes no secret: clobber suppressed "
                        "(callsite-gated)\n");
        }
      }
    } else {
      // External declaration or indirect call: the analysis cannot see what it
      // does to memory. If it receives a secret (in any argument register),
      // assume TOP — it may have written that secret anywhere caller-visible.
      // This is blunt-TOP P0; a libc model table and IR memory(...) attributes
      // would refine it (research §11 vii/viii), deferred pending measurement.
      if (HasTaintedArg) {
        // We DID pass the secret in, so the taint coming back is ours. Nothing
        // to record for the source condition.
        // Return value (register effect) + memory clobber, both applied in both
        // modes. Annotation-driven mode suppresses the clobber's FLOOD at load
        // consumption (below), not here, so the mod-set stays complete.
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
                                    AAResults *AA = nullptr,
                                    const FrameObjectMap *FOM = nullptr) {
  TaintState Out = In;
  for (const auto &MI : MBB) {
    propagateTaintMI(MI, Out, TRI, TSI, M, AA, FOM);
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
  // callee inherits it (Scenario B, docs/design/dit-placement.md G3). A data-carrying
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
  // isDITProtected is a membership list (docs/reference/dit-spec.md) — false covers
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
  // Built once: the replay must reconstruct the same frame provenance the
  // fixed point computed, or P1b's precise application would differ between
  // analysis and replay.
  auto FOM = std::make_unique<FrameObjectMap>(MF);

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

      propagateTaintMI(MI, S, TRI, TSI, M, AA, FOM.get());

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
          // Report granularity only: these runs drive -taint-regions-output and
          // -taint-source-regions-output, not DIT placement, which partitions
          // BLOCKS and prices its own merges with the frequency-weighted
          // admission test (-taint-dit-switch-cyc /
          // -taint-dit-dwell-per-instr). Splitting a run never changes whether
          // any run exists, so this cannot affect whether a function is
          // instrumented.
          static constexpr unsigned RegionReportMergeGap = 2;
          if (CleanGap > RegionReportMergeGap)
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
    // The source condition, re-exported with the effects it qualifies. One bit
    // for the whole mod-set: if any taint in this function came from somewhere
    // other than its parameters, none of its effects may be gated away.
    if (S.hasNonArgSourcedTaint())
      ME.NonArgSourced = true;
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
          CellInfo CI = getCellFromMMO(*MMO, MI.getMF());
          if (CI.K == CellInfo::Stack) {
            // A non-fixed frame object is this function's own private stack —
            // the caller cannot see it, so it is NOT a caller-visible effect.
            // A fixed object is an incoming stack/byval argument slot, which
            // the caller CAN see; blunt P0 maps that to TOP (mapping it to the
            // specific argument is a further provenance refinement).
            if (MFI.isFixedObjectIndex(CI.FI))
              ME.WritesSecretToUnknown = true;
          } else if (CI.K == CellInfo::Global) {
            ME.WritesSecretToGlobal.insert(CI.GV);
          } else if (CI.K == CellInfo::Arg) {
            // P1a: a store whose destination pointer bottoms out at pointer
            // argument i — the canonical callee->caller-through-memory write.
            // Record the precise provenance instead of TOP. Soundness in P1a is
            // still guaranteed by the caller, which applies a non-empty arg-set
            // as a full clobber (blunt) until P1b consumes it precisely.
            ME.WritesSecretThroughArgPointee.insert(CI.ArgNo);
          } else {
            // Unknown/heap — a store through a pointer the analysis cannot trace
            // to an argument or global. Sound direction: TOP.
            ME.WritesSecretToUnknown = true;
          }
        }
        return true;
      });

  LLVM_DEBUG({
    if (!ME.WritesSecretThroughArgPointee.empty() ||
        !ME.WritesSecretToGlobal.empty() || ME.WritesSecretToUnknown) {
      dbgs() << "  mem-effects[" << MF.getName() << "]:";
      // Emit arg indices in sorted order for deterministic test output.
      SmallVector<unsigned, 4> Args(ME.WritesSecretThroughArgPointee.begin(),
                                    ME.WritesSecretThroughArgPointee.end());
      llvm::sort(Args);
      for (unsigned A : Args)
        dbgs() << " arg" << A;
      // SmallPtrSet iteration order is unspecified — sort by name so the dump is
      // deterministic across runs (and lit-testable with multiple globals).
      SmallVector<StringRef, 4> Globals;
      for (const GlobalVariable *GV : ME.WritesSecretToGlobal)
        Globals.push_back(GV->getName());
      llvm::sort(Globals);
      for (StringRef G : Globals)
        dbgs() << " @" << G;
      if (ME.WritesSecretToUnknown)
        dbgs() << " UNKNOWN(TOP)";
      dbgs() << "\n";
    }
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

  // P1b go/no-go instrumentation. Counts how often a frame address computed as
  // base+imm can be tied to a specific frame object. Costs nothing when unset.
  if (!TaintFrameRefReportFile.empty()) {
    if (auto OS = openTaintReport(TaintFrameRefReportFile,
                                  "frame-ref report", /*Append=*/true)) {
      FrameObjectMap FOM(MF);
      const TargetInstrInfo *TIIr = MF.getSubtarget().getInstrInfo();
      Register FP = TRI->getFrameRegister(MF);
      Register SP = MF.getSubtarget()
                        .getTargetLowering()
                        ->getStackPointerRegisterToSaveRestore();
      unsigned Tot = 0, Res = 0;
      for (const MachineBasicBlock &MBB : MF)
        for (const MachineInstr &MI : MBB) {
          auto AI = matchAddImm(MI, TIIr);
          if (!AI)
            continue;
          Register Base = AI->first;
          if (!((SP.isValid() && Base == SP) || (FP.isValid() && Base == FP)))
            continue;
          // Skip stack adjustments: `sub sp, sp, #N` in the prologue matches the
          // add-immediate shape but computes no frame address. Only a def into
          // some OTHER register is a pointer into the frame.
          Register Dst = MI.getOperand(0).getReg();
          if ((SP.isValid() && Dst == SP) || (FP.isValid() && Dst == FP))
            continue;
          ++Tot;
          auto FI = FOM.resolve(Base, AI->second);
          if (FI)
            ++Res;
          *OS << "FRAMEREF " << (FI ? "resolved" : "UNRESOLVED")
              << " func=" << F.getName() << " base="
              << printReg(Base, TRI) << " off=" << AI->second
              << " fi=" << (FI ? std::to_string(*FI) : std::string("-"))
              << "\n";
        }
      *OS << "FRAMEREF-SUMMARY func=" << F.getName() << " objects=" << FOM.size()
          << " frame_addrs=" << Tot << " resolved=" << Res << "\n";
    }
  }

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
  // P1b frame provenance, built once for the whole fixed point.
  FrameObjectMap FOM(MF);

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

  // A secret arrived in a STACK argument. AAPCS64 materialises incoming stack
  // arguments as FIXED frame objects (negative frame indices), which is where
  // this function will load them from. We cannot say WHICH fixed object holds
  // it without re-running ABI argument assignment at the MIR level, so seed them
  // all: an over-approximation confined to this function, in the safe direction,
  // and one that only fires for functions a caller actually handed a stack
  // secret. See docs/design/stack-arguments.md.
  if (TSI && TSI->hasSummary(F) && TSI->getSummary(F).StackArgTainted) {
    const MachineFrameInfo &MFI = MF.getFrameInfo();
    unsigned Seeded = 0;
    for (int FI = MFI.getObjectIndexBegin(); FI < 0; ++FI) {
      if (!MFI.isFixedObjectIndex(FI) || MFI.isDeadObjectIndex(FI))
        continue;
      int64_t Sz = MFI.getObjectSize(FI);
      if (Sz <= 0)
        continue;
      Seed.setTaintedStackCell(FI, 0, (uint64_t)Sz);
      ++Seeded;
    }
    LLVM_DEBUG(dbgs() << "  Seeded " << Seeded
                      << " incoming fixed frame object(s) as tainted (secret "
                         "arrived in a stack argument)\n");
  }

  for (const auto &[PhysReg, VirtReg] : MRI.liveins()) {
    unsigned ArgIdx = TRI->getEncodingValue(PhysReg);
    // Only real argument registers (X0-X7 / V0-V7, encodings 0-7) carry incoming
    // taint. x30/x29/sp are livein of every function but never argument-passing,
    // so never seed them even if a stale index leaked into the summary — this
    // mirrors the producer-side guard in propagateArgTaintToCallees.
    if (ArgIdx > 7)
      continue;
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

  // Visit blocks in reverse post-order. This is a forward analysis, so RPO
  // reaches every predecessor before its successor wherever the CFG allows,
  // which collapses the number of times a block has to be re-evaluated. A LIFO
  // worklist is catastrophic on a bytecode dispatch loop: QuickJS's
  // JS_CallInternal has 1890 blocks and a computed-goto hub with 209
  // predecessors, and re-joining all of them on every visit made the TU
  // uncompilable. See docs/design/scalability.md.
  //
  // RPOT only enumerates reachable blocks. That matches the old behaviour,
  // which likewise only ever pushed successors reachable from entry; IN/OUT for
  // unreachable blocks stay bottom either way.
  SmallVector<const MachineBasicBlock *, 32> RPOBlocks;
  DenseMap<const MachineBasicBlock *, unsigned> RPONum;
  {
    ReversePostOrderTraversal<const MachineFunction *> RPOT(&MF);
    for (const MachineBasicBlock *B : RPOT) {
      RPONum[B] = RPOBlocks.size();
      RPOBlocks.push_back(B);
    }
  }

  // Ordered by RPO index, so pop always yields the earliest pending block.
  std::set<unsigned> WorkQ;
  // Blocks whose OUT has been computed at least once.
  SmallPtrSet<const MachineBasicBlock *, 32> Visited;

  auto push = [&](const MachineBasicBlock *B) {
    auto It = RPONum.find(B);
    if (It != RPONum.end())
      WorkQ.insert(It->second);
  };

  push(&MF.front());

  while (!WorkQ.empty()) {
    auto It = WorkQ.begin();
    const MachineBasicBlock *B = RPOBlocks[*It];
    WorkQ.erase(It);

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

    // The block transfer function is a pure function of IN and the block's
    // instructions, so an unchanged IN cannot produce a different OUT. Skipping
    // the recompute matters enormously on a dispatch loop: the computed-goto hub
    // in QuickJS's JS_CallInternal has 209 predecessors and gets re-popped
    // constantly, and re-running the transfer over thousands of instructions to
    // arrive at the same OUT was the dominant cost. First visit still has to run
    // it, because OUT has not been computed yet even though IN matches the
    // zero-initialised entry. See docs/design/scalability.md.
    bool FirstVisit = Visited.insert(B).second;
    if (!InChanged && !FirstVisit)
      continue;

    TaintState NewOut = propagateTaintMBB(*B, IN[B], TRI, TSI, M, AA, &FOM);

    if (NewOut != OUT[B]) {
      OUT[B] = std::move(NewOut);
      for (const MachineBasicBlock *Succ : B->successors())
        push(Succ);
    } else if (FirstVisit) {
      // OUT is unchanged, so successors' INs are unchanged and would normally
      // not need revisiting - but on the first visit they have never been
      // evaluated at all, and a CFG whose state is bottom everywhere would
      // otherwise never propagate past the entry block.
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
// Track B: cost-model region placement (docs/design/dit-placement.md §5.6)
//===----------------------------------------------------------------------===//

// Whole-function granularity: MSR DIT #1 at entry, #0 before every return,
// #1 re-asserted after every non-tail, non-DIT-preserving call. The shipped
// policy, and the safe fallback when region placement cannot prove coverage.
//
// A TAIL CALL gets NEITHER switch. It is both isReturn() and isCall() (on
// AArch64, TCRETURN*), so it is the function's exit — but control transfers to
// a callee that may consume this function's secret arguments, and the frame is
// already gone, so there is no instruction after it at which DIT could be
// restored. Clearing DIT here would disable protection exactly at the hand-off:
// that was a real under-taint, reached from libsodium's `crypto_sign`, which
// tail-calls `crypto_sign_ed25519` in another TU and so ran the whole signing
// operation with DIT=0. Leaving DIT set is the safe direction: an in-TU
// instrumented callee re-asserts at its own entry and clears before its own
// return, and an uninstrumented one at least inherits protection. The residual
// is a DIT leak past an uninstrumented tail callee — a cost, not a hole.
// See docs/design/dit-tailcall-gap.md.
// Will this callee hand PSTATE.DIT back the way it received it? Two ways:
// it never touches DIT at all (PreservesDIT), or it is entered with DIT already
// on and therefore never clears it (AlwaysEnteredWithDIT - the ownership rule).
// Either way the caller's after-call re-assert is dead code.
// Under -taint-dit-relaxed-ownership: a local-linkage, address-not-taken,
// instrumented function does not clear DIT before returning. Local linkage is
// what bounds the escape - an externally-visible function keeps its clear, so
// DIT can never leave the hardened library through this rule. Address-not-taken
// keeps it to call edges the compiler can see.
// A `<name>.dit` copy created by the annotator for DIT-on call sites. It is
// entered with DIT already set BY CONSTRUCTION -- the only calls to it are the
// ones redirectCallsToDITClones() creates, and those are all inside a DIT-on
// region -- so it emits no mode switches at all: no entry enable, no exit clear.
// That is what makes cloning reach the oracle where relaxed ownership cannot:
// relaxed ownership can drop the clear, but eliding the ENABLE on a shared
// helper would be a static assumption about the caller, and here it is not an
// assumption but a property of who is allowed to call this symbol.
static bool isDITClone(const Function *F) {
  return F && F->hasFnAttribute("taint-dit-clone");
}

// The clone for `F`, if the annotator made one.
static Function *ditCloneFor(const Function *F, Module &M) {
  if (!F || isDITClone(F))
    return nullptr;
  return M.getFunction((F->getName() + ".dit").str());
}

// Redirect every call in `OnBlocks` whose callee has a clone to that clone.
// Runs BEFORE switch emission so the later clobbersDIT() queries already see
// the clone (which never clears DIT) and skip the after-call re-assert.
static unsigned redirectCallsToDITClones(
    MachineFunction &MF, Module &M,
    function_ref<bool(const MachineBasicBlock *)> IsOn) {
  unsigned N = 0;
  for (MachineBasicBlock &MBB : MF) {
    if (!IsOn(&MBB))
      continue;
    for (MachineInstr &MI : MBB) {
      if (!MI.isCall())
        continue;
      const Function *Callee = findCalledFunction(M, MI);
      Function *Clone = ditCloneFor(Callee, M);
      if (!Clone)
        continue;
      for (MachineOperand &MO : MI.operands())
        if (MO.isGlobal() && MO.getGlobal() == Callee) {
          MO.ChangeToGA(Clone, MO.getOffset(), MO.getTargetFlags());
          ++N;
          break;
        }
    }
  }
  return N;
}

static bool calleeLeavesDITSet(const Function *Callee,
                               const TaintSummaryInfo *TSI) {
  if (!TSI || !Callee)
    return false; // external or indirect: assume it clears
  const FunctionTaintSummary &S = TSI->getSummary(*Callee);
  return S.PreservesDIT || S.AlwaysEnteredWithDIT || isDITClone(Callee);
}

// Record a call site whose callee could not be proven to leave PSTATE.DIT alone,
// so a re-assert follows the call. SOUND, not a hazard: the re-assert restores
// protection whatever the callee did. It is reported because it is the cost - at
// ~30 cycles, un-hoistable out of a loop, a call inside a hot secret loop pays it
// every iteration. Reasons, in the order they are checked:
//
//   indirect      no callee symbol at all (a function-pointer call). The case
//                 that cannot be fixed by any per-callee analysis, and the one
//                 -taint-dit-preserve-abi exists for.
//   external      callee is a declaration - another TU, so no summary. Taint is
//                 TU-scoped, which makes a cross-TU direct call behave exactly
//                 like an indirect one here.
//   clears-on-exit  an in-TU instrumented callee that owns DIT and clears it
//                 before returning.
static void reportDITReassert(raw_ostream *OS, const MachineFunction &MF,
                              const MachineInstr &MI, const Function *Callee,
                              const TaintSummaryInfo *TSI) {
  if (!OS)
    return;
  // An in-TU callee is only "clears-on-exit" if it is actually instrumented.
  // One that emits no DIT at all can still be non-preserving, because step 3b
  // retracts PreservesDIT through ITS callees -- `static void h(void){ ext(); }`
  // is the canonical shape. Calling that clears-on-exit sends an auditor looking
  // for a clear that does not exist and hides the real cause, which is the
  // cross-TU call inside it.
  const char *Reason;
  if (!Callee)
    Reason = "indirect";
  else if (Callee->isDeclaration())
    Reason = "external";
  else if (TSI && TSI->getSummary(*Callee).InstrumentedForDIT)
    Reason = "clears-on-exit";
  else
    Reason = "propagates-unresolvable"; // in-TU, emits no DIT of its own
  *OS << "REASSERT " << Reason
      << " callee=" << (Callee ? Callee->getName() : "<indirect>")
      << " caller=" << MF.getName() << " bb=" << MI.getParent()->getNumber();
  if (const DebugLoc &DL = MI.getDebugLoc())
    *OS << " line=" << DL.getLine();
  *OS << " (sound: DIT re-asserted after the call)\n";
}

// `OwnsDIT` is false for a function entered with DIT already set. Such a
// function did not turn DIT on, so it must not turn it off: it keeps the
// (redundant) entry enable but emits no disable before its returns. Eliding the
// ENABLE too would be the unsafe direction - see FunctionTaintSummary.
static void emitFunctionGranularityDIT(MachineFunction &MF,
                                       const TaintSummaryInfo *TSI,
                                       bool OwnsDIT,
                                       raw_ostream *ReassertOS = nullptr) {
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  Module *M = const_cast<Module *>(MF.getFunction().getParent());
  MachineBasicBlock &Entry = MF.front();
  // A DIT clone is entered with DIT already on (only DIT-on regions call it), so
  // it emits NO entry enable. This is the one place the enable is safely elided:
  // not a static assumption about callers, but a property of the symbol --
  // nothing else can name `foo.dit`, and the annotator only clones
  // local-linkage, address-not-taken functions so no pointer can reach it.
  const bool Clone = isDITClone(&MF.getFunction());
  if (!Clone)
    TII->insertTimingModeSwitch(Entry, Entry.begin(), DebugLoc(),
                                /*Enable=*/true);
  // Inside a clone DIT is on throughout, so its own calls can use clones too.
  if (Clone)
    redirectCallsToDITClones(MF, *M,
                             [](const MachineBasicBlock *) { return true; });
  for (MachineBasicBlock &MBB : MF)
    for (MachineInstr &MI : MBB) {
      // Tail call: see the comment above. Emit nothing.
      if (MI.isReturn() && MI.isCall())
        continue;
      if (MI.isReturn()) {
        if (!OwnsDIT)
          continue; // not ours to clear - the caller's region still needs it
        TII->insertTimingModeSwitch(MBB, MI.getIterator(), MI.getDebugLoc(),
                                    /*Enable=*/false);
      } else if (MI.isCall()) {
        const Function *Callee = findCalledFunction(*M, MI);
        if (calleeLeavesDITSet(Callee, TSI))
          continue;
        reportDITReassert(ReassertOS, MF, MI, Callee, TSI);
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
  return !calleeLeavesDITSet(findCalledFunction(M, MI), TSI);
}

// Forward 1-bit "DIT on" dataflow over the EMITTED MIR: a mode switch sets the
// state, a DIT-clobbering call clears it, joins AND-meet (DIT must arrive on
// from every predecessor). Returns the on-entry state of each block.
//
// AND-meet ⇒ initialize OPTIMISTICALLY to true, else a loop whose DIT is carried
// in (no enable inside the loop) would never converge to on — the backedge would
// start false and the meet would pin it there, reporting a false "uncovered".
// The entry boundary is off, enforced by `In = !pred_empty()`.
//
// Shared by the region-placement soundness verifier and the DIT accounting below
// so the two can never disagree about which instructions run with DIT set.
static DenseMap<const MachineBasicBlock *, bool>
computeDITOnEntry(MachineFunction &MF, const TaintSummaryInfo *TSI, Module &M,
                  const TargetInstrInfo *TII) {
  DenseMap<const MachineBasicBlock *, bool> OnOut, OnIn;
  for (MachineBasicBlock &MBB : MF)
    OnOut[&MBB] = true;
  const bool EntryOn = isDITClone(&MF.getFunction());
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
      // Entry boundary: off for a normal function, ON for a clone, whose
      // calling convention is "DIT is already set".
      bool In = MBB.pred_empty() ? EntryOn : true;
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
  return OnIn;
}

//===----------------------------------------------------------------------===//
// DIT accounting: how much of the DIT-covered code actually needs to be
//===----------------------------------------------------------------------===//

namespace {
// Counts over the EMITTED code of one function. `Need` is the set placement
// exists to cover; `UnderDIT` is what it actually covers. Collateral =
// UnderDIT - Need is the public code paying DIT's cost for nothing, and
// Need/UnderDIT is the precision a placement policy should maximize.
struct DITAccounting {
  uint64_t Need = 0;      // instructions that MUST run with DIT=1
  uint64_t UnderDIT = 0;  // instructions that DO run with DIT=1 (excl. switches)
  uint64_t Total = 0;     // all real instructions (excl. switches)
  uint64_t Switches = 0;  // MSR DIT instructions emitted
  // Same three, weighted by 10^min(loop depth, 4). A block inside a hot loop
  // costs far more than a straight-line block, and the unweighted ratio hides
  // exactly the case that matters — convolve's 14 toggles look cheap statically
  // and cost 7.16x when executed per pixel.
  uint64_t WNeed = 0, WUnderDIT = 0, WTotal = 0;

  void add(const DITAccounting &O) {
    Need += O.Need; UnderDIT += O.UnderDIT; Total += O.Total;
    Switches += O.Switches;
    WNeed += O.WNeed; WUnderDIT += O.WUnderDIT; WTotal += O.WTotal;
  }
};
} // namespace

// Walk the emitted function once, tracking DIT-on state, and tally the four
// counts. Runs AFTER placement so it measures what was actually emitted, and is
// policy-agnostic — region and function granularity are directly comparable.
static DITAccounting computeDITAccounting(MachineFunction &MF,
                                          const TaintResult &TR,
                                          const TaintSummaryInfo *TSI,
                                          AliasAnalysis *AA,
                                          const MachineLoopInfo &MLI) {
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  Module &M = *const_cast<Module *>(MF.getFunction().getParent());
  DenseMap<const MachineBasicBlock *, bool> OnIn =
      computeDITOnEntry(MF, TSI, M, TII);

  DITAccounting A;
  const MachineBasicBlock *CurBlk = nullptr;
  bool CurOn = false;
  uint64_t W = 1;

  replayTaint(MF, TR, TSI, AA,
              [&](MachineInstr &MI, const TaintFacts &F, const TaintState &) {
                if (MI.getParent() != CurBlk) {
                  CurBlk = MI.getParent();
                  CurOn = OnIn.lookup(CurBlk);
                  unsigned D = std::min(MLI.getLoopDepth(CurBlk), 4u);
                  W = 1;
                  for (unsigned i = 0; i < D; ++i)
                    W *= 10;
                }
                // A mode switch is overhead, not covered work: count it apart
                // and let it change the state.
                if (auto Sw = TII->getTimingModeSwitch(MI)) {
                  CurOn = *Sw;
                  ++A.Switches;
                  return true;
                }
                if (MI.isDebugInstr() || MI.isCFIInstruction() ||
                    MI.isPosition())
                  return true;

                ++A.Total;
                A.WTotal += W;
                bool IsNeed = needsDIT(MI, F, *TII);
                if (IsNeed) {
                  ++A.Need;
                  A.WNeed += W;
                }
                if (CurOn) {
                  ++A.UnderDIT;
                  A.WUnderDIT += W;
                }
                if (clobbersDIT(MI, TSI, M))
                  CurOn = false;
                return true;
              });
  return A;
}

// Module-wide running totals, emitted per function into the precision report so
// a consumer can sum them (utils/taint_dit_precision.py).
static void reportDITAccounting(MachineFunction &MF, const DITAccounting &A,
                                raw_ostream &OS) {
  auto pct = [](uint64_t N, uint64_t D) -> double {
    return D ? (100.0 * (double)N / (double)D) : 0.0;
  };
  OS << MF.getName() << " need=" << A.Need << " underdit=" << A.UnderDIT
     << " collateral=" << (A.UnderDIT >= A.Need ? A.UnderDIT - A.Need : 0)
     << " total=" << A.Total << " switches=" << A.Switches
     << " precision=" << format("%.1f", pct(A.Need, A.UnderDIT))
     << " coverage=" << format("%.1f", pct(A.UnderDIT, A.Total))
     << " wneed=" << A.WNeed << " wunderdit=" << A.WUnderDIT
     << " wtotal=" << A.WTotal
     << " wprecision=" << format("%.1f", pct(A.WNeed, A.WUnderDIT)) << "\n";
}

// Write one line of DIT accounting for this function, if the report was asked
// for. Runs after placement, so it measures emitted code.
static void emitDITPrecisionReport(MachineFunction &MF, const TaintResult &TR,
                                   const TaintSummaryInfo *TSI,
                                   AliasAnalysis *AA) {
  if (TaintDITPrecisionReportFile.empty())
    return;
  auto OS = openTaintReport(TaintDITPrecisionReportFile, "DIT precision report",
                            /*Append=*/true);
  if (!OS)
    return;
  MachineDominatorTree MDT(MF);
  MachineLoopInfo MLI(MDT);
  reportDITAccounting(MF, computeDITAccounting(MF, TR, TSI, AA, MLI), *OS);
}

// Insertion point at a block's start, PAST leading EH/GC labels, CFI, and debug.
// A mode switch must not displace a landing-pad EH_LABEL (the exception tables
// key the landing-pad PC on it) or sit among frame CFI.
static MachineBasicBlock::iterator
regionEntryInsertPt(MachineBasicBlock &MBB) {
  auto It = MBB.begin(), E = MBB.end();
  while (It != E && (It->isEHLabel() || It->isGCLabel() ||
                     It->isCFIInstruction() || It->isDebugInstr()))
    ++It;
  return It;
}

// True if MBB lies on a cycle (reachable from one of its own successors). Used
// to detect an irreducible cycle MachineLoopInfo does not model: a plain
// begin() enable there would be re-executed every iteration (per-iteration
// toggle) — such a function falls back to whole-function granularity instead.
static bool blockInCycle(const MachineBasicBlock *MBB) {
  SmallPtrSet<const MachineBasicBlock *, 16> Seen;
  SmallVector<const MachineBasicBlock *, 16> WL(MBB->succ_begin(),
                                                MBB->succ_end());
  while (!WL.empty()) {
    const MachineBasicBlock *B = WL.pop_back_val();
    if (B == MBB)
      return true;
    if (!Seen.insert(B).second)
      continue;
    WL.append(B->succ_begin(), B->succ_end());
  }
  return false;
}

// Region placement could not prove/achieve coverage for this function: erase the
// region switches we inserted (the input had none, so every DIT switch present
// is ours) and re-emit the always-safe whole-function policy. Never aborts the
// TU. Shared by the no-hoist and verifier-failure paths.
static void fallbackToFunctionGranularity(MachineFunction &MF,
                                          const TaintSummaryInfo *TSI,
                                          StringRef Reason,
                                          raw_ostream *ReassertOS = nullptr) {
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  for (MachineBasicBlock &MBB : MF) {
    SmallVector<MachineInstr *, 8> ToErase;
    for (MachineInstr &MI : MBB)
      if (TII->getTimingModeSwitch(MI))
        ToErase.push_back(&MI);
    for (MachineInstr *I : ToErase)
      I->eraseFromParent();
  }
  errs() << "taint: DIT region placement fell back to function granularity in "
         << MF.getName() << " (" << Reason << ")\n";
  // Only DIT-owning functions currently reach region placement at all, so this
  // is `true` in practice - but derive it rather than hardcode it, so the
  // ownership rule cannot be lost if that routing ever changes.
  emitFunctionGranularityDIT(
      MF, TSI,
      /*OwnsDIT=*/
      !(TSI && TSI->getSummary(MF.getFunction()).AlwaysEnteredWithDIT) &&
          !isDITClone(&MF.getFunction()),
      ReassertOS);
}

// Increment (c): the admission test (docs/design/dit-placement.md §5.6). Given the
// increment-(b) On/Off block partition, look at each interior Off *corridor* — a
// maximal CFG-connected group of Off blocks flanked by On on both sides — and
// decide whether keeping it DIT-off is worth the toggle pair that bounds it, or
// whether to merge it into coverage (extend `OnBlocks` over it). The comparison is
// made emit-accurately: the placement below puts every switch at a block boundary,
// so the frequency-weighted count of the switches emit would insert if the corridor
// is Off vs. On is compared against the DIT dwell over the corridor.
//
//   ToggleSaved = c_sw * [ Σ_{b∈C, has On-pred} freq(b)      // disables at b's entry
//                        + Σ_{s∉C, On-succ of C} freq(s) ]   // enables at s's entry
//   ToggleAdded = c_sw * [ Σ_{non-term clobber in C} freq(blk)   // re-asserts, and
//                        + Σ_{b∈C with a return}    freq(b)  ]   // disable-before-ret
//   merge iff  ToggleSaved - ToggleAdded  >=  dwell_per_instr * Σ_{b∈C} freq(b)*|b|
//
// Block (not edge) frequency is the accurate weight here precisely because emit
// places each switch at a block entry / before a return, so a switch executes at
// its block's frequency; summing the disable and enable sides (rather than max) and
// subtracting the re-asserts/disable-before-return that emit inserts *inside* a
// merged corridor is what keeps region mode from ever costing more toggles than it
// saves. This is the frequency-aware replacement for the static
// `-taint-region-merge-gap`: cold/long gaps merge, hot/short gaps stay split, and
// the P≈40-64 crossover falls out of the tunable. Merging only EXTENDS coverage, so
// it can never leave a Need uncovered — the soundness verifier still passes.
// One-sided corridors (a leading preamble or trailing epilogue — no toggle pair to
// save) are left Off. A clobber inside a corridor no longer disqualifies it: its
// re-assert is costed in ToggleAdded, so a corridor merges iff it is still a net
// toggle win.
static void admitOffCorridors(MachineFunction &MF, Module &M,
                              DenseSet<const MachineBasicBlock *> &OnBlocks,
                              const TaintSummaryInfo *TSI,
                              const MachineLoopInfo &MLI) {
  auto On = [&](const MachineBasicBlock *B) { return OnBlocks.count(B) != 0; };

  // Nothing to do unless some block is Off — skip the block-frequency dataflow in
  // the degenerate whole-function-covered case (region == function granularity).
  if (llvm::all_of(MF, [&](const MachineBasicBlock &B) { return On(&B); }))
    return;

  MachineBranchProbabilityInfo MBPI;
  MachineBlockFrequencyInfo MBFI(MF, MBPI, MLI);
  auto Freq = [&](const MachineBasicBlock *B) {
    return MBFI.getBlockFreqRelativeToEntryBlock(B);
  };

  // Cost of one MSR DIT switch. Tunable via -taint-dit-switch-cyc; defaults to 0
  // (free toggles ⇒ never merge ⇒ finest-grain groups). The measured cost on the
  // M4 is ~30 cyc/switch (docs/results/dit-cost-model.md).
  const double SwitchCyc = TaintDitSwitchCyc;

  // Partition the Off blocks into maximal CFG-connected components. Two Off blocks
  // that are CFG-adjacent are in the same component, so distinct components are
  // never adjacent — and every Off neighbor of a corridor block is in the same
  // corridor, so merging a corridor removes exactly its On<->corridor boundary
  // switches (no new On->Off boundary is created downstream). Evaluating each
  // component against the original On/Off partition and extending OnBlocks
  // afterward is order-independent.
  SmallPtrSet<const MachineBasicBlock *, 16> Seen;
  SmallVector<SmallVector<const MachineBasicBlock *, 8>, 8> ToMerge;
  for (MachineBasicBlock &Root : MF) {
    if (On(&Root) || Seen.count(&Root))
      continue;
    SmallVector<const MachineBasicBlock *, 8> Comp;
    SmallVector<const MachineBasicBlock *, 8> WL{&Root};
    Seen.insert(&Root);
    while (!WL.empty()) {
      const MachineBasicBlock *B = WL.pop_back_val();
      Comp.push_back(B);
      auto flood = [&](const MachineBasicBlock *N) {
        if (!On(N) && Seen.insert(N).second)
          WL.push_back(N);
      };
      for (const MachineBasicBlock *S : B->successors())
        flood(S);
      for (const MachineBasicBlock *P : B->predecessors())
        flood(P);
    }

    // Switches removed by merging (the On<->corridor boundary), and switches added
    // inside the now-covered corridor (re-asserts after clobbers, disables before
    // returns) — mirroring exactly what the emit loop below inserts.
    bool HasOnPred = false, HasOnSucc = false;
    double SavedFreq = 0.0, AddedFreq = 0.0, Dwell = 0.0;
    SmallPtrSet<const MachineBasicBlock *, 8> OnSuccs;
    for (const MachineBasicBlock *B : Comp) {
      double Fb = Freq(B);
      unsigned NumReal = 0;
      bool HasReturn = false;
      for (const MachineInstr &MI : *B) {
        if (!MI.isMetaInstruction())
          ++NumReal;
        if (clobbersDIT(MI, TSI, M) && !MI.isTerminator())
          AddedFreq += Fb; // emit re-asserts DIT after this clobber
        if (MI.isReturn())
          HasReturn = true;
      }
      if (HasReturn)
        AddedFreq += Fb; // emit disables DIT before the block's return
      Dwell += Fb * NumReal;
      if (llvm::any_of(B->predecessors(), On)) {
        HasOnPred = true;
        SavedFreq += Fb; // emit's disable at this block's entry disappears
      }
      for (const MachineBasicBlock *S : B->successors())
        if (On(S))
          OnSuccs.insert(S); // emit's enable at S's entry disappears
    }
    HasOnSucc = !OnSuccs.empty();
    for (const MachineBasicBlock *S : OnSuccs)
      SavedFreq += Freq(S);

    // Only an interior corridor (an actual toggle pair to save) qualifies.
    if (!HasOnPred || !HasOnSucc)
      continue;

    double ToggleNet = SwitchCyc * (SavedFreq - AddedFreq);
    double DwellCost = TaintDitDwellPerInstr * Dwell;
    LLVM_DEBUG(dbgs() << "  admission corridor in " << MF.getName() << ": "
                      << Comp.size() << " blk, toggleNet=" << ToggleNet
                      << " dwell=" << DwellCost
                      << (ToggleNet >= DwellCost ? " => MERGE\n" : " => split\n"));
    if (ToggleNet >= DwellCost)
      ToMerge.push_back(std::move(Comp));
  }

  for (const auto &Comp : ToMerge)
    for (const MachineBasicBlock *B : Comp)
      OnBlocks.insert(B);
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
                                      AAResults *AA,
                                      raw_ostream *ReassertOS = nullptr) {
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  Module &M = *const_cast<Module *>(MF.getFunction().getParent());

  // Re-assert sites are BUFFERED, not written as they are emitted: a later
  // fallback erases every switch this function placed and re-runs whole-function
  // placement, which reports again. Writing eagerly would leave lines for
  // switches that never reach the object and double-count the calls that
  // reappear, inflating the toggle cost the report exists to measure.
  SmallVector<const MachineInstr *, 8> PendingReassertSites;

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

  // Increment (b): loop-aware DIT-on block set (docs/design/dit-placement.md
  // §5.6). On(b) = HasNeed(b) OR b is in a loop that (transitively) contains a
  // Need. This excludes a clean preamble (Off) and makes the WHOLE outermost
  // need-loop On, so the Off→On boundary is the loop preheader (enable executed
  // once) rather than the loop header (re-entered every iteration by the
  // backedge). Loop info is built locally — no MFAM plumbing needed here.
  MachineDominatorTree MDT(MF);
  MachineLoopInfo MLI(MDT);

  // Loops that (transitively, via block membership incl. sub-loops) contain a
  // Need. Marking every loop enclosing a need-block hoists the region to the
  // outermost need-loop. Skipped in block-minimal mode (-taint-dit-loop-hoist=0),
  // where only need-containing blocks are covered.
  DenseSet<const MachineLoop *> NeedLoops;
  if (TaintDitLoopHoist)
    for (MachineBasicBlock &MBB : MF)
      if (HasNeed.lookup(&MBB))
        for (MachineLoop *L = MLI.getLoopFor(&MBB); L; L = L->getParentLoop())
          NeedLoops.insert(L);
  // Precompute the On block set once (one nest-walk per block) so later queries
  // are O(1) lookups rather than repeated getLoopFor/getParentLoop walks. In
  // block-minimal mode NeedLoops is empty, so On(b) reduces to HasNeed(b).
  DenseSet<const MachineBasicBlock *> OnBlocks;
  for (MachineBasicBlock &MBB : MF) {
    bool IsOn = HasNeed.lookup(&MBB);
    for (MachineLoop *L = MLI.getLoopFor(&MBB); L && !IsOn; L = L->getParentLoop())
      IsOn = NeedLoops.count(L);
    if (IsOn)
      OnBlocks.insert(&MBB);
  }

  // Increment (c): the admission test. Merge interior Off corridors whose bounding
  // toggle pair costs more than the DIT dwell it saves (frequency-weighted). This
  // only EXTENDS OnBlocks (coverage grows), so every Need stays covered and the
  // soundness verifier below still passes; on a dwell≈0 core it coarsens toward
  // function granularity, on a DIT-sensitive core it stays narrow. §5.6.
  admitOffCorridors(MF, M, OnBlocks, TSI, MLI);

  auto On = [&](const MachineBasicBlock *B) { return OnBlocks.count(B) != 0; };

  // Diagnostic for the placement-shape question: how many Off->On boundaries are
  // MIXED joins (some predecessors already DIT-on). Built here rather than reusing
  // admitOffCorridors' analyses so the report costs nothing when the flag is unset.
  std::unique_ptr<raw_ostream> JoinOS;
  MachineBranchProbabilityInfo JoinMBPI;
  std::unique_ptr<MachineBlockFrequencyInfo> JoinMBFI;
  if (!TaintDITJoinReportFile.empty()) {
    JoinOS = openTaintReport(TaintDITJoinReportFile, "DIT join report",
                             /*Append=*/true);
    JoinMBFI = std::make_unique<MachineBlockFrequencyInfo>(MF, JoinMBPI, MLI);
  }
  auto BlockFreq = [&](const MachineBasicBlock *B) -> double {
    return JoinMBFI ? JoinMBFI->getBlockFreqRelativeToEntryBlock(B) : 0.0;
  };

  // Redirect calls made from DIT-on blocks to the callee's clone, BEFORE
  // emitting switches, so the clobber scan below already sees a callee that
  // never clears DIT and therefore needs no after-call re-assert.
  redirectCallsToDITClones(MF, M, On);

  // Emit. Enable at each Off→On boundary (hoisted out of loops); re-assert after
  // non-terminator clobbers; disable before non-Need returns and on entering an
  // Off block from an On predecessor. Insertions at a block start go PAST leading
  // EH labels / CFI so they cannot displace a landing-pad label.
  unsigned Toggles = 0;
  bool NeedFallback = false;
  for (MachineBasicBlock &MBB : MF) {
    if (On(&MBB)) {
      bool OnAtEntry = !MBB.pred_empty();
      unsigned OnPreds = 0, OffPreds = 0;
      for (MachineBasicBlock *P : MBB.predecessors()) {
        OnAtEntry &= On(P);
        (On(P) ? OnPreds : OffPreds)++;
      }
      // A MIXED join (OnPreds > 0 && OffPreds > 0) is where this placement is
      // structurally weaker than an edge-bundled or edge-split one: the enable
      // goes at the block start, so it executes on the already-on paths too.
      // Quantified by -taint-dit-join-report; see docs/design/dit-placement.md.
      if (!OnAtEntry && JoinOS)
        *JoinOS << "JOIN " << (OnPreds ? "mixed" : "clean")
                << " func=" << MF.getName() << " bb=" << MBB.getNumber()
                << " on_preds=" << OnPreds << " off_preds=" << OffPreds
                << " freq=" << BlockFreq(&MBB) << "\n";
      if (!OnAtEntry) {
        // Off→On boundary.
        MachineLoop *L = TaintDitLoopHoist ? MLI.getLoopFor(&MBB) : nullptr;
        if (L && L->getHeader() == &MBB) {
          // Natural loop header: hoist the enable out of the loop so it is not
          // re-executed on the backedge.
          if (MachineBasicBlock *PH = L->getLoopPreheader()) {
            TII->insertTimingModeSwitch(*PH, PH->getFirstTerminator(),
                                        DebugLoc(), /*Enable=*/true);
            ++Toggles;
          } else {
            // No unique preheader (header reached from ≥2 external edges). Place
            // the enable at the end of each external (non-loop) predecessor —
            // each is entered once, so still no per-iteration toggle, and no
            // whole-function fallback.
            for (MachineBasicBlock *P : MBB.predecessors())
              if (!L->contains(P)) {
                TII->insertTimingModeSwitch(*P, P->getFirstTerminator(),
                                            DebugLoc(), /*Enable=*/true);
                ++Toggles;
              }
          }
        } else if (TaintDitLoopHoist && blockInCycle(&MBB)) {
          // On-entry block in a cycle MachineLoopInfo does not model
          // (irreducible): a begin() enable would be per-iteration and cannot be
          // hoisted. Fall back to whole-function granularity for this function.
          // Only a concern when hoisting; block-minimal mode intends per-iteration
          // toggles and places the enable at the block entry below.
          NeedFallback = true;
          break;
        } else {
          // Block entry. In block-minimal mode this is a need-block reached by a
          // backedge, so the enable (and the matching disable on the On→Off exit)
          // is per-iteration — the deliberate cost of covering the fewest
          // instructions. Sound either way: the verifier below is the hard gate.
          TII->insertTimingModeSwitch(MBB, regionEntryInsertPt(MBB), DebugLoc(),
                                      /*Enable=*/true);
          ++Toggles;
        }
      }
      // Re-assert after each clobber that is NOT a terminator (a non-preserving
      // tail call is a clobber AND a terminator: nothing follows it here, and
      // inserting after a terminator is invalid MIR).
      SmallVector<MachineInstr *, 8> Clobbers;
      for (MachineInstr &MI : MBB)
        if (clobbersDIT(MI, TSI, M) && !MI.isTerminator())
          Clobbers.push_back(&MI);
      for (MachineInstr *C : Clobbers) {
        PendingReassertSites.push_back(C);
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
        TII->insertTimingModeSwitch(MBB, regionEntryInsertPt(MBB), DebugLoc(),
                                    /*Enable=*/false);
        ++Toggles;
      }
    }
  }

  if (NeedFallback) {
    fallbackToFunctionGranularity(MF, TSI, "irreducible need-loop, cannot hoist",
                                  ReassertOS);
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
  DenseMap<const MachineBasicBlock *, bool> OnIn =
      computeDITOnEntry(MF, TSI, M, TII);
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
    // The fallback erases these switches and reports its own sites.
    fallbackToFunctionGranularity(MF, TSI, "verifier: uncovered need",
                                  ReassertOS);
    return NeedCount;
  }

  // Placement stuck: the buffered sites are the ones actually emitted.
  for (const MachineInstr *C : PendingReassertSites)
    reportDITReassert(ReassertOS, MF, *C, findCalledFunction(M, *C), TSI);
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
  // driven region placement will consume. See docs/results/dit-cost-model.md.
  if (RegionsOS)
    printTaintedRuns(MF, TaintedRuns, *RegionsOS);

  if (TaintedRuns.empty())
    return TaintedInstrCount;

  // The DIT ownership rule: a function entered with DIT already set did not turn
  // it on and must not turn it off. Region placement narrows coverage by
  // CLEARING DIT around clean stretches, so for such a function narrowing is not
  // merely unprofitable, it is illegal - every one of those clears would strip
  // the caller's protection while the caller's secret is live in the frame.
  // Whole-function coverage (minus the return clears) is therefore the only
  // correct placement here, so route past the region emitter entirely rather
  // than teaching it a second set of rules.
  const bool OwnsDIT =
      !(TSI && TSI->getSummary(MF.getFunction()).AlwaysEnteredWithDIT) &&
      !isDITClone(&MF.getFunction());

  // Default mode's audit trail: every call site we could not prove leaves DIT
  // alone, and therefore re-asserted after. Sound but not free, so the list is
  // the cost, not a hazard. Append, because this runs once per function.
  auto ReassertOSPtr = openTaintReport(TaintDITReassertReportFile,
                                       "DIT re-assert report", /*Append=*/true);
  raw_ostream *ReassertOS = ReassertOSPtr.get();

  // Track B: cost-model region placement (WIP). Opt-in; default stays
  // function-granularity below, so shipped codegen is untouched. Both modes
  // report the same tainted-instruction count.
  if (TaintDITPlacement == DITPlacementMode::Region && OwnsDIT) {
    insertTaintDITRegions(MF, TR, TSI, AA, ReassertOS);
    emitDITPrecisionReport(MF, TR, TSI, AA);
    return TaintedInstrCount;
  }
  // Make the routing visible: under the default `region` policy a non-owning
  // function silently lands on whole-function coverage, which is correct but
  // changes what an objdump of a hardened build looks like.
  LLVM_DEBUG(if (TaintDITPlacement == DITPlacementMode::Region && !OwnsDIT)
                 dbgs()
             << "  " << MF.getName()
             << ": AlwaysEnteredWithDIT, using whole-function coverage "
                "(region placement would clear DIT it does not own)\n");

  // Function granularity: run the whole function in data-independent-timing
  // mode when it contains any tainted instruction. Per-region toggles would
  // clear an enclosing region's DIT when a tainted callee's exit switch runs
  // inside a caller's still-open region.
  emitFunctionGranularityDIT(MF, TSI, OwnsDIT, ReassertOS);
  emitDITPrecisionReport(MF, TR, TSI, AA);

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
