//===- llvm/CodeGen/TaintAnalysis.h -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the TaintAnalysis pass which identifies tainted registers
// in MIR based on "tainted" attributes set on IR function arguments.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_TAINTANALYSIS_H
#define LLVM_CODEGEN_TAINTANALYSIS_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SparseBitVector.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/CodeGen/TaintSummaryInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>

namespace llvm {

// Forward declarations
class TaintSummaryInfo;
class Module;
class TargetInstrInfo;
class TargetRegisterInfo;
class MachineInstr;
class GlobalVariable;
class AAResults;
class Value;

/// Cell keys: (base, (offset, size_in_bytes))
using StackCell = std::pair<int, std::pair<int64_t, uint64_t>>;
using GlobalCell =
    std::pair<const GlobalVariable *, std::pair<int64_t, uint64_t>>;

/// Command-line option for taint output file (shared across passes).
extern cl::opt<std::string> TaintOutputFile;

/// Command-line option for barrier protected-region output file.
extern cl::opt<std::string> TaintRegionsOutputFile;

/// Command-line option for source-line protected-region output file.
extern cl::opt<std::string> TaintSourceRegionsOutputFile;

/// Master switch: insert PSTATE.DIT mode switches around tainted code.
/// DIT (data-independent timing) is the project's only protection mechanism.
/// The ISB/DSB speculation-barrier mode that used to sit behind
/// -taint-barrier-mode was a placeholder for this toggle and was removed on
/// 2026-07-14; speculation defense is not in scope (see docs/handoff.md).
/// Without this flag, codegen is unchanged and only the report files are
/// produced.
extern cl::opt<bool> TaintInsertDIT;

/// Callee-saved PSTATE.DIT (docs/design/dit-abi.md). Read DIT at entry into a
/// frame slot reserved before PrologEpilogInserter, restore it at every return,
/// and emit nothing at call sites. Read by BOTH the placement code and the
/// pre-PEI slot reservation, which must not reserve a slot the placement will
/// never use.
extern cl::opt<bool> TaintDITAbi;

/// Command-line option for the call-site residual (escape) report: call sites
/// passing tainted/pointee-tainted arguments to callees the analysis cannot
/// instrument (external declarations, indirect calls).
extern cl::opt<std::string> TaintCallsiteReportFile;

/// Command-line option for the DIT-uncovered report (gap G2): tainted
/// instructions PSTATE.DIT does not actually protect - divide/sqrt (not
/// DIT-listed), secret-dependent memory addresses (cache/TLB timing), and
/// secret-dependent branches (control-flow timing). Counting these as protected
/// is silent false assurance; the report surfaces them for audit.
extern cl::opt<std::string> TaintUncoveredReportFile;

/// Output file for DIT accounting: per function, how many instructions must run
/// with PSTATE.DIT set versus how many actually do.
extern cl::opt<std::string> TaintDITPrecisionReportFile;

/// Command-line option for the DIT re-assert report: every call site where the
/// pass could not prove the callee leaves PSTATE.DIT alone and therefore
/// re-asserted `MSR DIT, #1` after the call.
///
/// These sites are SOUND, not hazards - the re-assert restores protection
/// unconditionally, whatever the callee did. They are reported because they are
/// the cost: a re-assert is ~30 cycles and cannot be hoisted out of a loop, so a
/// call in a hot secret loop pays it per iteration (measured on SQLCipher:
/// libtomcrypt drives AES one 16-byte block per call through a function-pointer
/// table, 256 calls per 4 KB page). The report is the audit trail for why a
/// hardened build toggles as often as it does, and the list of places the
/// proposed runtime-MRS mode would help. That mode is DESIGNED BUT NOT
/// IMPLEMENTED -- there is no flag for it yet; see
/// docs/design/dit-callee-ownership.md.
///
/// TRUNCATED per compiler invocation, like the other taint reports. A multi-TU
/// build pointing every TU at one path therefore keeps only the LAST TU's sites;
/// give each TU its own file and concatenate. The alternative, appending, silently
/// multiplies every site by the number of builds into that path and inflates the
/// toggle cost the report exists to measure.
extern cl::opt<std::string> TaintDITReassertReportFile;

/// Command-line option for the memory-clobber report: every call site that
/// makes the caller treat memory as secret (sets ExternalMemClobbered / a
/// whole-global). These are the sources of cross-function memory taint - the
/// points where a "taint explosion" originates - so they can be pinpointed and
/// audited. Distinct from the escape report (which is about secrets leaving to
/// callees we cannot instrument).
/// Sites where the PSTATE.DIT callee-saved OBLIGATION degrades to the weaker
/// GUARANTEE, because control leaves the frame without running its epilogue:
/// an EH unwind, a `longjmp` out of a `setjmp` this function performed, or a
/// `musttail` that survived the TU-wide tail-call disable. All three leave DIT
/// SET, so none can strip a caller's protection - the residual is dwell, not
/// exposure. They are reported rather than fixed because none is repairable
/// from inside the function. See docs/design/dit-abi.md §2.2.
extern cl::opt<std::string> TaintNonlocalReportFile;

extern cl::opt<std::string> TaintClobberReportFile;
extern cl::opt<std::string> TaintDITJoinReportFile;
extern cl::opt<std::string> TaintFrameRefReportFile;

/// Selects which of TaintState's register bitvectors an operation applies to.
enum class TaintKind {
  Data,    ///< The value itself is secret.
  Pointee, ///< The value is a pointer to secret memory.
  Address, ///< The value may be used as a secret-dependent memory address.
};

/// TaintState holds the result of taint analysis for a MachineFunction.
/// TaintedRegs tracks secret data values. PointeeTaintedRegs tracks pointer
/// values whose pointee memory is secret. AddressTaintedRegs tracks values that
/// may be used as secret-dependent memory addresses.
struct TaintState {
  SparseBitVector<> TaintedRegs;
  SparseBitVector<> PointeeTaintedRegs;
  SparseBitVector<> AddressTaintedRegs;
  bool OutgoingArgSecret = false;
  bool NonArgSourcedTaint = false;

  /// Register -> the frame object it points into (P1b). Records WHICH object a
  /// frame-derived pointer refers to, which is what lets a callee's arg-pointee
  /// mod-set be applied to the one object the caller passed rather than to the
  /// whole frame.
  /// Absent = unknown, and every consumer must fall back to its conservative
  /// path on absent. Excluded from empty()/countRegs(): it is provenance, not
  /// taint.
  SmallDenseMap<unsigned, int, 8> FrameRefs{};
  DenseSet<StackCell> TaintedStackCells;
  DenseSet<StackCell> PointeeTaintedStackCells;
  DenseSet<GlobalCell> TaintedGlobalCells;
  DenseSet<const Value *> TaintedUnknownMemValues;
  DenseSet<const Value *> PointeeTaintedUnknownMemValues;
  bool UnknownMemTainted = false;

  /// Globals a callee wrote a secret into, at unknown offset. Whole-object
  /// (any load of the global is secret): a callee's mod-set carries no offset.
  DenseSet<const GlobalVariable *> TaintedWholeGlobals;

  /// A call may have written a secret into memory the analysis cannot pin down
  /// (through a pointer argument, to the heap, or via an unknown callee). Once
  /// set, every subsequent stack / global / heap load in this function must be
  /// treated as secret - this is the caller-side landing point of a callee's
  /// FunctionMemEffects TOP. Distinct from UnknownMemTainted (which the analysis
  /// keeps deliberately local for heap-store precision) so this coarser,
  /// call-induced poison does not perturb the existing heap-store behavior.
  bool ExternalMemClobbered = false;

private:
  /// Merge Src into Dst in one shot. Reserving up front matters: join is the
  /// hot path of the intraprocedural fixed point, and inserting element by
  /// element into a growing DenseSet rehashes repeatedly. See
  /// docs/design/scalability.md.
  template <typename SetT> static void mergeSet(SetT &Dst, const SetT &Src) {
    if (Src.empty())
      return;
    Dst.reserve(Dst.size() + Src.size());
    for (const auto &E : Src)
      Dst.insert(E);
  }

public:
  /// True if this state carries no taint at all - the dataflow lattice bottom.
  /// Every component check is O(1), so this is a cheap guard on join.
  bool isBottom() const {
    return TaintedRegs.empty() && PointeeTaintedRegs.empty() &&
           AddressTaintedRegs.empty() && !OutgoingArgSecret &&
           !NonArgSourcedTaint && TaintedStackCells.empty() &&
           PointeeTaintedStackCells.empty() && TaintedGlobalCells.empty() &&
           TaintedUnknownMemValues.empty() &&
           PointeeTaintedUnknownMemValues.empty() && !UnknownMemTainted &&
           TaintedWholeGlobals.empty() && !ExternalMemClobbered;
  }

  bool operator==(const TaintState &O) const {
    return TaintedRegs == O.TaintedRegs &&
           PointeeTaintedRegs == O.PointeeTaintedRegs &&
           AddressTaintedRegs == O.AddressTaintedRegs &&
           OutgoingArgSecret == O.OutgoingArgSecret &&
           NonArgSourcedTaint == O.NonArgSourcedTaint &&
           FrameRefs == O.FrameRefs &&
           TaintedStackCells == O.TaintedStackCells &&
           PointeeTaintedStackCells == O.PointeeTaintedStackCells &&
           TaintedGlobalCells == O.TaintedGlobalCells &&
           TaintedUnknownMemValues == O.TaintedUnknownMemValues &&
           PointeeTaintedUnknownMemValues == O.PointeeTaintedUnknownMemValues &&
           UnknownMemTainted == O.UnknownMemTainted &&
           TaintedWholeGlobals == O.TaintedWholeGlobals &&
           ExternalMemClobbered == O.ExternalMemClobbered;
  }

  bool operator!=(const TaintState &O) const { return !(*this == O); }

  void join(const TaintState &O) {
    // Joining bottom cannot change anything. Worth the check because a forward
    // taint analysis over a large CFG spends most joins merging empty state.
    if (O.isBottom())
      return;
    TaintedRegs |= O.TaintedRegs;
    PointeeTaintedRegs |= O.PointeeTaintedRegs;
    AddressTaintedRegs |= O.AddressTaintedRegs;
    OutgoingArgSecret |= O.OutgoingArgSecret;
    NonArgSourcedTaint |= O.NonArgSourcedTaint;
    // Frame provenance INTERSECTS on merge: a register only points at a known
    // object if every incoming path agrees which one. Disagreement, or presence
    // on only one path, drops to unknown - the conservative direction, since
    // unknown means "fall back to the whole-frame clobber".
    if (!FrameRefs.empty()) {
      SmallVector<unsigned, 8> Drop;
      for (const auto &KV : FrameRefs) {
        auto It = O.FrameRefs.find(KV.first);
        if (It == O.FrameRefs.end() || It->second != KV.second)
          Drop.push_back(KV.first);
      }
      for (unsigned R : Drop)
        FrameRefs.erase(R);
    }
    mergeSet(TaintedStackCells, O.TaintedStackCells);
    mergeSet(PointeeTaintedStackCells, O.PointeeTaintedStackCells);
    mergeSet(TaintedGlobalCells, O.TaintedGlobalCells);
    mergeSet(TaintedUnknownMemValues, O.TaintedUnknownMemValues);
    mergeSet(PointeeTaintedUnknownMemValues, O.PointeeTaintedUnknownMemValues);
    UnknownMemTainted |= O.UnknownMemTainted;
    mergeSet(TaintedWholeGlobals, O.TaintedWholeGlobals);
    ExternalMemClobbered |= O.ExternalMemClobbered;
  }

  // Whole-object / call-clobber accessors (memory-effects component).
  void setTaintedWholeGlobal(const GlobalVariable *GV) {
    if (GV)
      TaintedWholeGlobals.insert(GV);
  }
  bool isWholeGlobalTainted(const GlobalVariable *GV) const {
    return GV && TaintedWholeGlobals.contains(GV);
  }
  /// Globals a callee wrote a secret into, inherited via caller-side mod-set
  /// application. Read by computeFunctionMemEffects to re-export the effect so
  /// it survives more than one call level.
  const DenseSet<const GlobalVariable *> &wholeTaintedGlobals() const {
    return TaintedWholeGlobals;
  }
  void setExternalMemClobbered() { ExternalMemClobbered = true; }
  bool isExternalMemClobbered() const { return ExternalMemClobbered; }

  /// A secret has been stored into the OUTGOING argument area and has not yet
  /// been consumed by a call. AAPCS64 passes arguments beyond the eighth (and
  /// large aggregates) in memory: the caller stores them to `$sp + imm` and the
  /// argument registers may then be overwritten before the call, so by the time
  /// the call is reached NO register holds the secret. Without this bit the
  /// analysis concluded that such a call passes nothing secret, and the callee
  /// was analysed as clean - a real leak, not merely imprecision
  /// (docs/design/stack-arguments.md).
  /// Some taint in this function did NOT enter through its own parameters: it
  /// was read from a tainted global, arrived from another TU's unknown memory,
  /// or was produced by a call this function did not hand a secret to. This is
  /// the source condition the mod-set call-site gate needs: a caller's arguments
  /// can only account for a callee's secret if that secret came from parameters
  /// in the first place. Monotone - set, never cleared, merged with OR.
  void setNonArgSourcedTaint() { NonArgSourcedTaint = true; }
  bool hasNonArgSourcedTaint() const { return NonArgSourcedTaint; }

  void setOutgoingArgSecret() { OutgoingArgSecret = true; }
  void clearOutgoingArgSecret() { OutgoingArgSecret = false; }
  bool isOutgoingArgSecret() const { return OutgoingArgSecret; }

  /// The frame object R points into, if the analysis knows it (P1b).
  std::optional<int> getFrameRef(Register R) const {
    auto It = FrameRefs.find(R.id());
    return It == FrameRefs.end() ? std::nullopt : std::optional<int>(It->second);
  }
  void setFrameRef(Register R, int FI) { FrameRefs[R.id()] = FI; }
  void clearFrameRef(Register R) { FrameRefs.erase(R.id()); }

  /// The register bitvector holding taint of kind K.
  SparseBitVector<> &regs(TaintKind K) {
    switch (K) {
    case TaintKind::Data:
      return TaintedRegs;
    case TaintKind::Pointee:
      return PointeeTaintedRegs;
    case TaintKind::Address:
      return AddressTaintedRegs;
    }
    llvm_unreachable("unhandled TaintKind");
  }
  const SparseBitVector<> &regs(TaintKind K) const {
    return const_cast<TaintState *>(this)->regs(K);
  }

  /// Check whether R carries taint of kind K.
  bool test(TaintKind K, Register R) const {
    return R.isValid() && regs(K).test(R.id());
  }

  /// Set or clear taint of kind K on R.
  void update(TaintKind K, Register R, bool Set) {
    if (!R.isValid())
      return;
    if (Set)
      regs(K).set(R.id());
    else
      regs(K).reset(R.id());
  }

  /// Check if a register is tainted.
  bool isTainted(Register R) const { return test(TaintKind::Data, R); }

  /// Check if a register points to tainted memory.
  bool isPointeeTainted(Register R) const {
    return test(TaintKind::Pointee, R);
  }

  /// Check if a register carries secret-dependent address taint.
  bool isAddressTainted(Register R) const {
    return test(TaintKind::Address, R);
  }

  /// Do byte ranges [AOff,AOff+ASz) and [BOff,BOff+BSz) overlap? Size 0 is the
  /// unknown-extent sentinel (recorded by an unknown-size store) and is treated
  /// as covering the whole object - the safe direction.
  static bool rangesOverlap(int64_t AOff, uint64_t ASz, int64_t BOff,
                            uint64_t BSz) {
    if (ASz == 0 || BSz == 0)
      return true;
    return AOff < BOff + (int64_t)BSz && BOff < AOff + (int64_t)ASz;
  }

  // Stack cell methods
  void setTaintedStackCell(int FI, int64_t Off, uint64_t Sz) {
    TaintedStackCells.insert({FI, {Off, Sz}});
  }
  void clearTaintedStackCell(int FI, int64_t Off, uint64_t Sz) {
    TaintedStackCells.erase({FI, {Off, Sz}});
  }
  bool isTaintedStackCell(int FI, int64_t Off, uint64_t Sz) const {
    return TaintedStackCells.contains({FI, {Off, Sz}});
  }
  /// READ-side test: does any tainted cell of this object overlap the accessed
  /// byte range? Exact (FI,Off,Sz) matching was an UNDER-TAINT: spilling 8
  /// secret bytes and reloading the low 4 (`STRXui` then `LDRWui` on the same
  /// slot) missed the cell entirely and handed the secret back as public. The
  /// CLEAR path deliberately stays exact-match - widening a clear would drop
  /// taint a partial public store did not actually overwrite.
  bool isTaintedStackCellOverlapping(int FI, int64_t Off, uint64_t Sz) const {
    if (TaintedStackCells.contains({FI, {Off, Sz}}))
      return true; // fast path: identical access shape
    for (const auto &C : TaintedStackCells)
      if (C.first == FI &&
          rangesOverlap(Off, Sz, C.second.first, C.second.second))
        return true;
    return false;
  }

  /// Fallback: any cell for this FI tainted? O(n) scan.
  bool anyTaintedStackCellForFI(int FI) const {
    for (const auto &C : TaintedStackCells)
      if (C.first == FI)
        return true;
    return false;
  }
  void setPointeeTaintedStackCell(int FI, int64_t Off, uint64_t Sz) {
    PointeeTaintedStackCells.insert({FI, {Off, Sz}});
  }
  void clearPointeeTaintedStackCell(int FI, int64_t Off, uint64_t Sz) {
    PointeeTaintedStackCells.erase({FI, {Off, Sz}});
  }
  bool isPointeeTaintedStackCell(int FI, int64_t Off, uint64_t Sz) const {
    return PointeeTaintedStackCells.contains({FI, {Off, Sz}});
  }
  /// READ-side overlap test for pointee taint (see isTaintedStackCellOverlapping).
  bool isPointeeTaintedStackCellOverlapping(int FI, int64_t Off,
                                            uint64_t Sz) const {
    if (PointeeTaintedStackCells.contains({FI, {Off, Sz}}))
      return true;
    for (const auto &C : PointeeTaintedStackCells)
      if (C.first == FI &&
          rangesOverlap(Off, Sz, C.second.first, C.second.second))
        return true;
    return false;
  }

  bool anyPointeeTaintedStackCellForFI(int FI) const {
    for (const auto &C : PointeeTaintedStackCells)
      if (C.first == FI)
        return true;
    return false;
  }

  // Global cell methods
  void setTaintedGlobalCell(const GlobalVariable *GV, int64_t Off,
                            uint64_t Sz) {
    TaintedGlobalCells.insert({GV, {Off, Sz}});
  }
  void clearTaintedGlobalCell(const GlobalVariable *GV, int64_t Off,
                              uint64_t Sz) {
    TaintedGlobalCells.erase({GV, {Off, Sz}});
  }
  bool isTaintedGlobalCell(const GlobalVariable *GV, int64_t Off,
                           uint64_t Sz) const {
    return TaintedGlobalCells.contains({GV, {Off, Sz}});
  }
  /// READ-side overlap test for globals (see isTaintedStackCellOverlapping).
  bool isTaintedGlobalCellOverlapping(const GlobalVariable *GV, int64_t Off,
                                      uint64_t Sz) const {
    if (TaintedGlobalCells.contains({GV, {Off, Sz}}))
      return true;
    for (const auto &C : TaintedGlobalCells)
      if (C.first == GV &&
          rangesOverlap(Off, Sz, C.second.first, C.second.second))
        return true;
    return false;
  }

  bool anyTaintedGlobalCellForGV(const GlobalVariable *GV) const {
    for (const auto &C : TaintedGlobalCells)
      if (C.first == GV)
        return true;
    return false;
  }

  void setTaintedUnknownMemValue(const Value *V) {
    if (V)
      TaintedUnknownMemValues.insert(V);
  }
  void setPointeeTaintedUnknownMemValue(const Value *V) {
    if (V)
      PointeeTaintedUnknownMemValues.insert(V);
  }
  void clearPointeeTaintedUnknownMemValue(const Value *V) {
    if (V)
      PointeeTaintedUnknownMemValues.erase(V);
  }

  bool emptyRegs() const {
    return TaintedRegs.empty() && PointeeTaintedRegs.empty() &&
           AddressTaintedRegs.empty();
  }
  bool empty() const {
    return emptyRegs() && TaintedStackCells.empty() &&
           PointeeTaintedStackCells.empty() &&
           TaintedGlobalCells.empty() && TaintedUnknownMemValues.empty() &&
           PointeeTaintedUnknownMemValues.empty() &&
           !UnknownMemTainted;
  }

  unsigned countRegs() const {
    return TaintedRegs.count() + PointeeTaintedRegs.count() +
           AddressTaintedRegs.count();
  }
  unsigned countDataRegs() const { return TaintedRegs.count(); }
  unsigned countPointeeRegs() const { return PointeeTaintedRegs.count(); }
  unsigned countAddressRegs() const { return AddressTaintedRegs.count(); }
  unsigned countCells() const {
    return (unsigned)(TaintedStackCells.size() +
                      PointeeTaintedStackCells.size() +
                      TaintedGlobalCells.size() +
                      TaintedUnknownMemValues.size() +
                      PointeeTaintedUnknownMemValues.size());
  }
  unsigned count() const { return countRegs() + countCells(); }
};

/// TaintResult holds both the merged taint state and per-BB entry states.
/// The per-BB IN map is needed by the export pass to replay taint propagation
/// instruction-by-instruction and determine which specific instructions are
/// tainted.
struct TaintResult {
  TaintState Merged;
  DenseMap<const MachineBasicBlock *, TaintState> IN;
};

/// TaintAnalysis is a MachineFunction analysis that computes which registers
/// are tainted based on IR function argument attributes.
class TaintAnalysis : public AnalysisInfoMixin<TaintAnalysis> {
  friend AnalysisInfoMixin<TaintAnalysis>;
  static AnalysisKey Key;

public:
  using Result = TaintResult;

  /// Standard pass-manager entry point. Extracts TSI from MFAM proxy.
  LLVM_ABI Result run(MachineFunction &MF,
                      MachineFunctionAnalysisManager &MFAM);

  /// Direct entry point for interprocedural analysis.
  /// Bypasses pass manager; TSI is provided directly by the module pass.
  LLVM_ABI Result run(MachineFunction &MF, const TaintSummaryInfo *TSI,
                      AAResults *AA = nullptr);
};

/// TaintAnalysisPass is a MachineFunction pass that runs TaintAnalysis
/// and can be used to trigger the analysis in the pass pipeline.
class TaintAnalysisPass : public PassInfoMixin<TaintAnalysisPass> {
public:
  PreservedAnalyses run(MachineFunction &MF,
                        MachineFunctionAnalysisManager &MFAM);

  MachineFunctionProperties getRequiredProperties() const {
    return MachineFunctionProperties();
  }
};

//===----------------------------------------------------------------------===//
// Shared helpers used by both TaintAnalysisPass and TaintInterprocPass
//===----------------------------------------------------------------------===//

/// Facts about one instruction, gathered during a taint replay. These are the
/// only questions consumers ask about the state surrounding an instruction, so
/// carrying them lets the replay avoid copying a whole TaintState per
/// instruction.
struct TaintFacts {
  bool UsesData = false;    ///< Reads a secret value (state entering MI).
  bool UsesPointee = false; ///< Reads a pointer to secret memory.
  bool UsesAddress = false; ///< Reads a secret-dependent address.
  bool DefsData = false;    ///< Defines a secret value (state leaving MI).
};

/// Replay the converged taint state through MF instruction by instruction,
/// seeding each block from TR.IN exactly as the analysis did.
///
/// Post, if given, sees the state leaving each instruction; Pre, if given, sees
/// the state entering it. Either may return false to stop the walk early.
void replayTaint(
    MachineFunction &MF, const TaintResult &TR, const TaintSummaryInfo *TSI,
    AAResults *AA,
    function_ref<bool(MachineInstr &, const TaintFacts &, const TaintState &)>
        Post,
    function_ref<bool(MachineInstr &, const TaintState &)> Pre = {});

/// True if MI is secret-dependent and therefore belongs inside a barrier region.
bool isTaintedInstruction(const MachineInstr &MI, const TaintFacts &F);

/// G2 diagnostic: if MI is a tainted instruction that PSTATE.DIT does NOT
/// protect, return the reason ("divide-or-sqrt", "secret-address",
/// "secret-branch"); otherwise nullptr. Distinguishes a secret store *address*
/// (uncovered) from secret store *data* (covered) via the value/address operand
/// split, so it does not false-positive on secret data written to a public slot.
const char *classifyDITUncovered(const MachineInstr &MI, const TaintFacts &F,
                                 const TaintState &S,
                                 const TargetInstrInfo &TII);

/// Taint carried by a call's *passed arguments* - the secret an ABI-compliant
/// callee can actually read. Only argument-register use operands (AAPCS64:
/// X0-X7 / V0-V7, encodings 0-7) count; a register merely live or clobbered
/// across the call is not an argument and is excluded. `Pointee` covers a
/// public pointer argument whose pointee is secret (a real reach), which must
/// stay distinct from `Data` so callers can keep protecting it.
struct CallArgTaint {
  bool Data = false;    ///< a secret passed by value in an argument register
  bool Pointee = false; ///< an argument pointer whose pointee is secret
  bool any() const { return Data || Pointee; }
};

/// Compute which kinds of secret a call passes to its callee. Empty for
/// non-calls. This is the "secret is *passed*" test (fix B): a secret merely
/// live across the call does not count, because an ABI-compliant callee cannot
/// read caller-saved / non-argument registers as inputs.
CallArgTaint taintedCallArguments(const MachineInstr &MI, const TaintState &S,
                                  const TargetRegisterInfo *TRI);

/// Find the called function from a call instruction.
/// Returns nullptr for indirect calls.
const Function *findCalledFunction(Module &M, const MachineInstr &MI);

/// Derive a sibling report path: ("out.txt", "_src") -> "out_src.txt".
SmallString<256> deriveReportPath(StringRef Base, StringRef Suffix);

/// Open a taint report file, truncating it unless Append is set. Returns null
/// both when Path is empty (the report was not requested) and when the file
/// cannot be opened, so callers can simply test the result; open failures are
/// reported on stderr.
std::unique_ptr<raw_fd_ostream> openTaintReport(StringRef Path, StringRef What,
                                                bool Append = false);

/// True if the function contains at least one coalesced tainted run, i.e.
/// insertTaintDITSwitches would instrument it. Used to compute the PreservesDIT
/// summary bit before instrumentation.
bool functionHasTaintedRuns(MachineFunction &MF, const TaintResult &TR,
                            const TaintSummaryInfo *TSI,
                            AAResults *AA = nullptr);

/// Compute the memory-effects (mod-set) summary for MF from its converged taint
/// result: which caller-visible memory it may have written a secret into. Runs
/// inside the interprocedural fixed point (callee summaries feed caller
/// application), so it must be recomputed until the summary stops changing.
FunctionMemEffects computeFunctionMemEffects(MachineFunction &MF,
                                             const TaintResult &TR,
                                             const TaintSummaryInfo *TSI,
                                             AAResults *AA = nullptr);

/// Holds buffered per-function taint statistics for sorting before output.
struct FunctionTaintStats {
  std::string Output; // Formatted stats text
  double TaintRatio = 0.0;
};

/// Export tainted instructions for a single MachineFunction to the output
/// streams. OS receives the TSV instruction dump; SrcOS (if non-null)
/// receives the source-line summary; Stats (if non-null) receives
/// buffered per-function taint composition statistics.
void exportTaintedInstructions(MachineFunction &MF, const TaintResult &TR,
                               const TaintSummaryInfo *TSI, raw_ostream &OS,
                               raw_ostream *SrcOS,
                               FunctionTaintStats *Stats = nullptr,
                               AAResults *AA = nullptr);

/// Export the coalesced tainted instruction regions that would be protected by
/// hardening barriers. Returns the number of tainted instructions in those
/// regions.
unsigned exportTaintBarrierRegions(MachineFunction &MF, const TaintResult &TR,
                                   const TaintSummaryInfo *TSI,
                                   raw_ostream &OS, AAResults *AA = nullptr);

/// Export source-line ranges corresponding to coalesced tainted instruction
/// regions. Returns the number of source regions emitted.
unsigned exportTaintSourceRegions(MachineFunction &MF, const TaintResult &TR,
                                  const TaintSummaryInfo *TSI, raw_ostream &OS,
                                  AAResults *AA = nullptr);

/// Instrument MF with PSTATE.DIT mode switches if it contains any tainted
/// instruction: MSR DIT, #1 at entry, MSR DIT, #0 before every return, and a
/// re-assert after each non-tail call whose callee is not proven DIT-preserving.
/// Placement is function-granularity - see docs/design/dit-placement.md.
/// If RegionsOS is non-null, also prints the coalesced tainted regions (which
/// are reported but do not currently drive placement).
/// Returns the number of tainted instructions protected.
unsigned insertTaintDITSwitches(MachineFunction &MF, const TaintResult &TR,
                                const TaintSummaryInfo *TSI,
                                raw_ostream *RegionsOS = nullptr,
                                AAResults *AA = nullptr);

} // namespace llvm

#endif // LLVM_CODEGEN_TAINTANALYSIS_H
