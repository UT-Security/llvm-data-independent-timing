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
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/SparseBitVector.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/CodeGen/TaintSummaryInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include <climits>
#include <memory>
#include <optional>

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

/// The coverage contract at a call boundary (docs/design/dit-callee-contract.md).
///
/// `inherit` (shipped): a secret-passing call is a Need, so the caller holds DIT
/// across it and the callee inherits protection; a callee this pass cannot see
/// runs covered by that inheritance, and `AlwaysEnteredWithDIT` is derived from
/// the same guarantee.
///
/// `callee`: every function protects its own secrets. A call is never a Need for
/// its arguments (a `bl` has no data-dependent timing), whether the caller's
/// region spans a call is a cost decision the corridor pricing makes, and a
/// secret reaching a callee this build cannot see - another TU, an indirect
/// target, a library - is an OBLIGATION the info-loss report lists for the
/// developer to fill with a seed, not something the caller covers on the
/// callee's behalf. What it buys: seeding becomes monotone (instrumenting a
/// callee can never strip a caller that was relying on inheritance), no function
/// needs to know its entry DIT state, and whether a callee clears at exit becomes
/// a pure cost question. A secret handed to a libc mover (memcpy, memset) is
/// an obligation like any other, filled by linking a hardened mover: its loads
/// and stores of the secret are the data-value channel DIT covers.
enum class DITContract { Inherit, Callee };
extern cl::opt<DITContract> TaintDITContract;
inline bool ditCalleeContract() {
  return TaintDITContract == DITContract::Callee;
}

/// TU-wide tail-call disable, ON by default whenever hardening runs.
///
/// A tail call is an exit with no epilogue, so an instrumented function that
/// takes one can never clear PSTATE.DIT again: everything downstream runs
/// protected and selective placement silently degenerates to blanket coverage.
/// It is not hypothetical - libsodium's `randombytes_buf` exits through an
/// indirect tail call, so a program that only calls `sodium_init()` runs with
/// DIT set for its whole life and pays the entire always-on penalty at zero
/// secret fraction (docs/design/dit-tailcall-gap.md §7).
///
/// Set =0 to get tail calls back. That is a MEASUREMENT hatch: on hardware with
/// a serializing `MSR DIT` the restored clears are real switches and cost +8.89
/// points at f = 9.4%, so the trade has to stay measurable. It is refused under
/// -taint-dit-abi, where a surviving tail call is an ABI violation rather than a
/// cost.
extern cl::opt<bool> TaintNoTailCalls;

/// Treat a call argument that is the address of a tainted frame object as
/// passing a secret, in both directions (the mod-set gate's call-site test and
/// the caller->callee parameter marking). Closes the caller->callee half of the
/// frame-address gap; DEFAULT OFF pending measurement, see
/// docs/design/p1b-frame-provenance.md §4.
extern cl::opt<bool> TaintFrameAddrArgs;

/// Track WHICH object an incoming pointer argument points at, so a callee's
/// arg-pointee mod-set can be applied to that object instead of collapsing to a
/// whole-caller clobber (B1). DEFAULT OFF pending measurement - it REDUCES
/// over-approximation, which is the direction that can lose a secret if the
/// provenance is wrong. See docs/design/frame-address-gap.md.
extern cl::opt<bool> TaintArgProvenance;

/// Treat a call argument that points at a tainted ARG POINTEE as passing a
/// secret (B2) - the consumption half, and the one that actually closes the
/// leak. Additive, so it costs switches; measure it separately from B1. A no-op
/// unless TaintArgProvenance also named the object in the first place.
extern cl::opt<bool> TaintArgPointeeArgs;

/// Model the memory effect of libc movers (memcpy/memmove/mempcpy) instead of
/// treating them as opaque external calls that clobber all caller memory.
extern cl::opt<bool> TaintLibcModel;

/// Stamp the hardening-wide tail-call disable (\see TaintNoTailCalls) on every
/// definition in \p M, and return how many functions were stamped.
///
/// MUST be called at CODEGEN time, never before the IR optimization pipeline.
/// The `disable-tail-calls` attribute is read by TailRecursionElimination as
/// well as by ISel, so stamping it early would turn tail RECURSION into O(n)
/// stack frames in every function of the TU. Applied after the optimizer, TRE
/// runs untouched - self-recursion still becomes a loop, which is strictly
/// better for DIT than a tail call - while ISel, the only other consumer, still
/// sees the attribute and forms nothing.
unsigned applyTaintTailCallDisable(Module &M);

/// Command-line option for the call-site residual (escape) report: call sites
/// passing tainted/pointee-tainted arguments to callees the analysis cannot
/// instrument (external declarations, indirect calls).
extern cl::opt<std::string> TaintCallsiteReportFile;
/// Every site where the analysis LOST information about the secret, what it did
/// about it, what that cost, and the annotation that would restore precision.
extern cl::opt<std::string> TaintInfoLossReportFile;

/// Ownership for the callee contract's obligation report: a file naming the
/// functions THIS BUILD defines, one per line (utils/taint_owned_symbols.sh
/// over the build's objects). An unseen callee not in it is external code the
/// developer does not own - libc, another library - and is reported as an
/// `external-call` record (Info: out of scope for the seed loop, the taint
/// still propagates through the call) instead of an obligation with a seed
/// line. Without the file every named callee is an obligation, as before.
/// Only the report classification changes; codegen is identical.
extern cl::opt<std::string> TaintOwnedSymbolsFile;

/// The set that file names, loaded once per process; null when no file was
/// given or it could not be read. Shared by the obligation report and by
/// cross-TU DIT cloning, which must agree on what "a callee we define" means.
const StringSet<> *taintOwnedSymbols();

/// A file naming EXTERNAL functions that never write PSTATE.DIT, one per line
/// (`#` comments): libc's leaf routines - the movers, the string functions, the
/// allocators, the syscall wrappers. `utils/dit_preserving_libc.txt` is the
/// glibc set. A call to one from DIT-on code returns with the mode exactly as
/// it went in, so no re-assert is emitted after it, and an in-TU function whose
/// only calls are to such symbols keeps its PreservesDIT bit. Trusted ONLY for
/// a callee this module does not define and the owned list (when given) does
/// not name: a function of ours clears at its own exit under the callee
/// contract, and the file never overrides that. Not for anything that takes a
/// callback (qsort, pthread_once, atexit): the callback may be ours. Says
/// nothing about coverage - a mover handed a secret is still an obligation.
extern cl::opt<std::string> TaintDITPreservingSymbolsFile;

/// The set that file names, loaded once per process; null when no file was
/// given or it could not be read.
const StringSet<> *taintDITPreservingSymbols();

/// True when `Name` is a symbol the preserving file names, this module does
/// not define, and the owned list does not name: it hands PSTATE.DIT back as
/// it found it. By NAME, not Function: the `bl memcpy` that lowers an
/// `llvm.memcpy` intrinsic carries an external-symbol operand and the module
/// often has no Function for it at all, so a Function-based test misses the
/// exact calls this is for.
bool taintExternalSymbolPreservesDIT(StringRef Name, const Module &M);

/// The same for a call instruction: its direct callee's symbol, if any. A call
/// to such a symbol neither needs a re-assert after it nor retracts its
/// caller's PreservesDIT.
bool taintExternalCallPreservesDIT(const MachineInstr &MI, const Module &M);

/// A `<name>.dit` twin of a seeded function (`-taint-dit-clone-seeded`, or the
/// older `-taint-dit-clone-list`): entered with PSTATE.DIT already set by
/// construction - only a DIT-on call site is ever redirected to it - so it
/// emits no entry enable and no exit clear, and leaves DIT set for its caller.
/// What it does owe its caller is that guarantee: after any call of its own
/// that may clear DIT it re-asserts, even when nothing in its body is secret.
bool isDITClone(const Function *F);

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

/// The two may-facts the analysis tracks about a VALUE, wherever that value
/// sits - in a register or in a memory cell. They are independent dimensions
/// of one abstract value (TaintVal), and everything the analysis moves - a
/// copy, an ALU result, a store, a load - moves the whole value: a store
/// writes the stored register's value into the cell, a load reads the cell's
/// value back into the defined register. That is what makes the memory side
/// ONE mechanism instead of one per kind (docs/design/taint-domain.md).
///
/// There is deliberately no third kind. The old `Address` channel
/// ("secret-dependent address") was, under every rule the analysis has, a
/// subset of `Data`: it was only ever set on an ALU result whose inputs were
/// Data-tainted, and cleared wherever Data was cleared. Every consumer that
/// tested it therefore tested Data twice, and removing it changes no output
/// (docs/design/taint-domain.md S2).
enum class TaintKind {
  Data,    ///< The value itself is secret.
  Pointee, ///< The value is a pointer into memory that may hold a secret.
};

/// The abstract value: one bit per TaintKind. Bottom is (false, false); the
/// join is OR in both dimensions, because both are may-facts.
struct TaintVal {
  bool Data = false;
  bool Pointee = false;

  static TaintVal data() { return {true, false}; }
  static TaintVal pointee() { return {false, true}; }

  bool get(TaintKind K) const { return K == TaintKind::Data ? Data : Pointee; }
  void set(TaintKind K, bool V) {
    if (K == TaintKind::Data)
      Data = V;
    else
      Pointee = V;
  }
  bool any() const { return Data || Pointee; }
  TaintVal &operator|=(TaintVal O) {
    Data |= O.Data;
    Pointee |= O.Pointee;
    return *this;
  }
  bool operator==(TaintVal O) const {
    return Data == O.Data && Pointee == O.Pointee;
  }
  bool operator!=(TaintVal O) const { return !(*this == O); }
};

/// A memory object the analysis can NAME. Post-prologepilog there are three:
///
///   Frame(FI)   an object in THIS function's frame - `$sp + imm` (P1b).
///   Global(GV)  a global variable.
///   Arg(k)      the object this function's incoming pointer argument k points
///               at. The caller named it; we cannot, but we can say "the same
///               object argument k was given" and let OUR caller resolve it one
///               level up (B1). It is an ABSTRACT object in this function's
///               memory state: the callee cannot see the caller's bytes, only
///               what it has been told or has itself written through the
///               pointer.
///
/// Used in two roles with OPPOSITE soundness polarity, and the code keeps them
/// apart on purpose:
///   * as the key of a memory cell (MemCell) - a MAY fact, "this object may
///     hold a secret here", which unions on join;
///   * as the provenance of a register (TaintState::PointerBases) - a MUST
///     fact, "this register points into exactly this object", which
///     INTERSECTS on join, because it licenses a callee's write to be applied
///     to one object instead of to all of memory, and attributing that write
///     to the wrong object is the one direction that under-taints.
struct TaintObject {
  enum Kind : uint8_t { Frame, Global, Arg };
  Kind K = Frame;
  int Index = 0; ///< frame index when Frame, argument number when Arg
  const GlobalVariable *GV = nullptr; ///< when Global

  static TaintObject frame(int FI) { return {Frame, FI, nullptr}; }
  static TaintObject global(const GlobalVariable *G) { return {Global, 0, G}; }
  static TaintObject arg(unsigned ArgNo) { return {Arg, (int)ArgNo, nullptr}; }

  bool operator==(const TaintObject &O) const {
    return K == O.K && Index == O.Index && GV == O.GV;
  }
  bool operator!=(const TaintObject &O) const { return !(*this == O); }
};

/// A byte range of a nameable object. Size 0 is the unknown-extent sentinel:
/// it is recorded by a store whose size the analysis cannot see, and it reads
/// as covering the whole object (see TaintState::rangesOverlap).
struct MemCell {
  TaintObject Obj;
  int64_t Off = 0;
  uint64_t Size = 0;

  bool operator==(const MemCell &O) const {
    return Obj == O.Obj && Off == O.Off && Size == O.Size;
  }
  bool operator!=(const MemCell &O) const { return !(*this == O); }
};

template <> struct DenseMapInfo<TaintObject> {
  // Real frame indices are small (fixed objects are small negatives), so the
  // two extreme values are free to serve as the map's sentinels.
  static inline TaintObject getEmptyKey() {
    return {TaintObject::Frame, INT_MIN, nullptr};
  }
  static inline TaintObject getTombstoneKey() {
    return {TaintObject::Frame, INT_MIN + 1, nullptr};
  }
  static unsigned getHashValue(const TaintObject &O) {
    return static_cast<unsigned>(
        hash_combine(static_cast<unsigned>(O.K), O.Index, O.GV));
  }
  static bool isEqual(const TaintObject &A, const TaintObject &B) {
    return A == B;
  }
};

template <> struct DenseMapInfo<MemCell> {
  static inline MemCell getEmptyKey() {
    return {DenseMapInfo<TaintObject>::getEmptyKey(), 0, 0};
  }
  static inline MemCell getTombstoneKey() {
    return {DenseMapInfo<TaintObject>::getTombstoneKey(), 0, 0};
  }
  static unsigned getHashValue(const MemCell &C) {
    return static_cast<unsigned>(hash_combine(
        DenseMapInfo<TaintObject>::getHashValue(C.Obj), C.Off, C.Size));
  }
  static bool isEqual(const MemCell &A, const MemCell &B) { return A == B; }
};

/// TaintState is the dataflow state of the analysis at one program point: the
/// abstract value (TaintVal) of every register and of every memory cell the
/// analysis can name, plus the coarse facts it keeps about memory it cannot.
///
///   registers   Reg -> TaintVal          two bitvectors, one per TaintKind
///   provenance  Reg -> TaintObject       MUST: which object a pointer targets
///   memory      MemCell -> TaintVal      one map for frame, global and
///                                        arg-pointee cells
///   heap        IR Value -> TaintVal     stores the analysis could locate
///                                        only by IR pointer
///   TOP bits    UnknownMemTainted, ExternalMemClobbered
///   flags       OutgoingArgSecret, NonArgSourcedTaint
///
/// Join is per component: taint UNIONS (registers, cells, heap, TOP bits,
/// flags), provenance INTERSECTS. See docs/design/taint-domain.md.
struct TaintState {
  SparseBitVector<> TaintedRegs;        ///< Data
  SparseBitVector<> PointeeTaintedRegs; ///< Pointee
  bool OutgoingArgSecret = false;
  bool NonArgSourcedTaint = false;

  /// Register -> WHICH object the pointer it holds refers to (Frame or Arg;
  /// a global's address is never tracked as a base). Naming the object is what
  /// lets a callee's arg-pointee mod-set be applied to the one object the
  /// caller passed instead of collapsing to a whole-caller clobber. GCC's
  /// `ipa-modref` carries the same pair as a `parm_index` that is either a
  /// real parameter or a negative sentinel; see
  /// docs/design/frame-address-gap.md.
  ///
  /// Absent = unknown, and every consumer must fall back to its conservative
  /// path on absent. ONE map, so a def kills provenance of both kinds at once.
  /// Excluded from isBottom()/countRegs(): it is provenance, not taint.
  SmallDenseMap<unsigned, TaintObject, 8> PointerBases{};

  /// Every memory cell the analysis can name and knows to hold a secret or a
  /// pointer to one. A cell is written by a store (strongly when the store's
  /// extent is known, weakly otherwise) and read by a load of any overlapping
  /// range; the two kinds travel together because the cell holds a VALUE.
  ///
  /// Arg(k) cells are whole-object sentinels only (offset 0, size 0): "the
  /// object my caller passed for argument k holds a secret" (B1). At function
  /// exit they are re-exported as
  /// FunctionMemEffects::WritesSecretThroughArgPointee, which is what makes
  /// the naming compose up the call graph.
  DenseMap<MemCell, TaintVal> Cells{};

  /// Stores the analysis could not attach to a nameable object but could
  /// still locate by IR pointer, so a later load can be screened against them
  /// with alias analysis. Data is never cleared from an entry (a later public
  /// store to the same IR pointer proves nothing about which bytes it hit);
  /// Pointee is, because the entry then records a pointer spill and a
  /// non-pointer store to the slot overwrote it.
  DenseMap<const Value *, TaintVal> UnknownMemValues{};
  /// A secret was stored somewhere the analysis could not locate at all.
  bool UnknownMemTainted = false;

  /// Globals a callee wrote a secret into, at unknown offset. Whole-object
  /// (any load of the global is secret): a callee's mod-set carries no offset.
  /// Kept apart from Cells because it is INHERITED taint, not this function's
  /// own - empty() deliberately ignores it, and computeFunctionMemEffects
  /// re-exports it as a set.
  DenseSet<const GlobalVariable *> TaintedWholeGlobals{};
  /// Globals that hold a POINTER TO SECRET MEMORY as of this point: a
  /// pointee-tainted value was stored into them, or a secret was stored
  /// through a pointer loaded from them. Taint (unions on join); exported as
  /// FunctionMemEffects::WritesPointeeToGlobal and folded module-wide.
  DenseSet<const GlobalVariable *> PointeeGlobals{};

  /// A call may have written a secret into memory the analysis cannot pin down
  /// (through a pointer argument, to the heap, or via an unknown callee). Once
  /// set, every subsequent stack / global / heap load in this function must be
  /// treated as secret - this is the caller-side landing point of a callee's
  /// FunctionMemEffects TOP. Distinct from UnknownMemTainted (which the analysis
  /// keeps deliberately local for heap-store precision) so this coarser,
  /// call-induced poison does not perturb the existing heap-store behavior.
  bool ExternalMemClobbered = false;

private:
  /// Merge Src into Dst by OR-ing values. Reserving up front matters: join is
  /// the hot path of the intraprocedural fixed point, and inserting element by
  /// element into a growing map rehashes repeatedly. See
  /// docs/design/scalability.md.
  template <typename MapT> static void mergeVals(MapT &Dst, const MapT &Src) {
    if (Src.empty())
      return;
    Dst.reserve(Dst.size() + Src.size());
    for (const auto &KV : Src)
      Dst[KV.first] |= KV.second;
  }

public:
  /// True if this state carries no taint at all - the dataflow lattice bottom.
  /// Every component check is O(1), so this is a cheap guard on join.
  bool isBottom() const {
    return TaintedRegs.empty() && PointeeTaintedRegs.empty() &&
           !OutgoingArgSecret && !NonArgSourcedTaint && Cells.empty() &&
           UnknownMemValues.empty() && !UnknownMemTainted &&
           TaintedWholeGlobals.empty() && PointeeGlobals.empty() &&
           !ExternalMemClobbered;
  }

  bool operator==(const TaintState &O) const {
    return TaintedRegs == O.TaintedRegs &&
           PointeeTaintedRegs == O.PointeeTaintedRegs &&
           OutgoingArgSecret == O.OutgoingArgSecret &&
           NonArgSourcedTaint == O.NonArgSourcedTaint &&
           PointerBases == O.PointerBases && Cells == O.Cells &&
           UnknownMemValues == O.UnknownMemValues &&
           UnknownMemTainted == O.UnknownMemTainted &&
           TaintedWholeGlobals == O.TaintedWholeGlobals &&
           PointeeGlobals == O.PointeeGlobals &&
           ExternalMemClobbered == O.ExternalMemClobbered;
  }

  bool operator!=(const TaintState &O) const { return !(*this == O); }

  void join(const TaintState &O) {
    // Provenance INTERSECTS on merge: a register only points at a known object
    // if every incoming path agrees which one. Disagreement, or presence on
    // only one path, drops to unknown - the conservative direction, since
    // unknown means "fall back to the whole-frame clobber".
    //
    // This runs BEFORE the bottom check below, and must. isBottom() ignores
    // provenance (it is not taint), so a predecessor that carries no secret
    // yet can still name a different object for the same register - `p = &a`
    // on one arm, `p = &b` on the other, before any secret exists. Returning
    // early kept the first arm's answer, a callee's write through `p` was
    // then applied precisely to that one object, and a read of the other
    // came back public (clang/test/CodeGen/taint-provenance-join.c). The
    // optimistic treatment a loop backedge needs is the caller's job: it
    // skips predecessors it has not visited yet, not predecessors that are
    // bottom.
    if (!PointerBases.empty()) {
      SmallVector<unsigned, 8> Drop;
      for (const auto &KV : PointerBases) {
        auto It = O.PointerBases.find(KV.first);
        if (It == O.PointerBases.end() || It->second != KV.second)
          Drop.push_back(KV.first);
      }
      for (unsigned R : Drop)
        PointerBases.erase(R);
    }
    // Joining bottom cannot change any TAINT. Worth the check because a forward
    // taint analysis over a large CFG spends most joins merging empty state.
    if (O.isBottom())
      return;
    TaintedRegs |= O.TaintedRegs;
    PointeeTaintedRegs |= O.PointeeTaintedRegs;
    OutgoingArgSecret |= O.OutgoingArgSecret;
    NonArgSourcedTaint |= O.NonArgSourcedTaint;
    // Everything else is taint and UNIONS: any path reaching this point with
    // the location secret makes it secret here.
    mergeVals(Cells, O.Cells);
    mergeVals(UnknownMemValues, O.UnknownMemValues);
    UnknownMemTainted |= O.UnknownMemTainted;
    if (!O.TaintedWholeGlobals.empty()) {
      TaintedWholeGlobals.reserve(TaintedWholeGlobals.size() +
                                  O.TaintedWholeGlobals.size());
      for (const GlobalVariable *GV : O.TaintedWholeGlobals)
        TaintedWholeGlobals.insert(GV);
    }
    if (!O.PointeeGlobals.empty()) {
      PointeeGlobals.reserve(PointeeGlobals.size() + O.PointeeGlobals.size());
      for (const GlobalVariable *GV : O.PointeeGlobals)
        PointeeGlobals.insert(GV);
    }
    ExternalMemClobbered |= O.ExternalMemClobbered;
  }

  //===--------------------------------------------------------------------===//
  // Registers
  //===--------------------------------------------------------------------===//

  /// The register bitvector holding taint of kind K.
  SparseBitVector<> &regs(TaintKind K) {
    return K == TaintKind::Data ? TaintedRegs : PointeeTaintedRegs;
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

  //===--------------------------------------------------------------------===//
  // Provenance
  //===--------------------------------------------------------------------===//

  /// The object R points at, if the analysis knows it.
  std::optional<TaintObject> getPointerBase(Register R) const {
    auto It = PointerBases.find(R.id());
    return It == PointerBases.end() ? std::nullopt
                                    : std::optional<TaintObject>(It->second);
  }
  /// The frame object R points into, if the analysis knows it AND it is a frame
  /// object (P1b). Deliberately narrow: a caller that can only act on a frame
  /// index must not be handed an argument number.
  std::optional<int> getFrameRef(Register R) const {
    auto It = PointerBases.find(R.id());
    if (It == PointerBases.end() || It->second.K != TaintObject::Frame)
      return std::nullopt;
    return It->second.Index;
  }
  void setFrameRef(Register R, int FI) {
    PointerBases[R.id()] = TaintObject::frame(FI);
  }
  void setPointerBase(Register R, TaintObject B) { PointerBases[R.id()] = B; }
  void setArgRef(Register R, unsigned ArgNo) {
    PointerBases[R.id()] = TaintObject::arg(ArgNo);
  }
  /// Kills provenance of BOTH kinds - one map, so a def cannot leave a stale
  /// base of the other kind behind.
  void clearFrameRef(Register R) { PointerBases.erase(R.id()); }

  //===--------------------------------------------------------------------===//
  // Memory cells
  //===--------------------------------------------------------------------===//

  /// Do byte ranges [AOff,AOff+ASz) and [BOff,BOff+BSz) overlap? Size 0 is the
  /// unknown-extent sentinel (recorded by an unknown-size store) and is treated
  /// as covering the whole object - the safe direction.
  static bool rangesOverlap(int64_t AOff, uint64_t ASz, int64_t BOff,
                            uint64_t BSz) {
    if (ASz == 0 || BSz == 0)
      return true;
    return AOff < BOff + (int64_t)BSz && BOff < AOff + (int64_t)ASz;
  }

  /// WEAK update: cell C may (additionally) hold V. Never clears anything.
  void taintCell(const MemCell &C, TaintVal V) {
    if (V.any())
      Cells[C] |= V;
  }

  /// STRONG update, for a store whose extent is known: cell C now holds
  /// exactly V, so a public store clears the exact cell it overwrote. The
  /// clear is deliberately exact-match: widening it would drop taint a partial
  /// public store never actually overwrote. (READS test overlap, see below.)
  void assignCell(const MemCell &C, TaintVal V) {
    if (V.any())
      Cells[C] = V;
    else
      Cells.erase(C);
  }

  /// What a read of [Off, Off+Size) of Obj may observe: the join of every cell
  /// of Obj that overlaps the range. Size 0 asks about the whole object.
  ///
  /// READ-side overlap, not exact match: spilling 8 secret bytes and reloading
  /// the low 4 (`STRXui` then `LDRWui` on the same slot) used to miss the cell
  /// entirely and hand the secret back as public - an UNDER-taint
  /// (docs/design/spill-soundness-bugs.md).
  TaintVal readCell(const TaintObject &Obj, int64_t Off, uint64_t Size) const {
    TaintVal V;
    if (Size) {
      // Fast path: identical access shape.
      auto It = Cells.find(MemCell{Obj, Off, Size});
      if (It != Cells.end()) {
        V |= It->second;
        if (V.Data && V.Pointee)
          return V;
      }
    }
    for (const auto &KV : Cells)
      if (KV.first.Obj == Obj &&
          rangesOverlap(Off, Size, KV.first.Off, KV.first.Size))
        V |= KV.second;
    return V;
  }

  /// Does any cell of Obj hold a secret?
  bool objectHoldsSecret(const TaintObject &Obj) const {
    return readCell(Obj, 0, 0).Data;
  }

  /// Any cell of this function's own frame, of either kind.
  bool anyFrameCell() const {
    for (const auto &KV : Cells)
      if (KV.first.Obj.K == TaintObject::Frame)
        return true;
    return false;
  }

  /// A whole frame object became secret (a callee wrote it, or a stack
  /// argument arrived in it). Weak: nothing else in the object is cleared.
  void taintFrameObject(int FI, uint64_t Sz) {
    taintCell(MemCell{TaintObject::frame(FI), 0, Sz}, TaintVal::data());
  }
  bool frameObjectHoldsSecret(int FI) const {
    return objectHoldsSecret(TaintObject::frame(FI));
  }

  /// The pointee of incoming pointer argument ArgNo holds a secret (B1).
  /// Whole-object granularity: the call site cannot know which bytes of the
  /// passed object a callee wrote, so it records the object.
  void setTaintedArgPointee(unsigned ArgNo) {
    taintCell(MemCell{TaintObject::arg(ArgNo), 0, 0}, TaintVal::data());
  }
  bool isTaintedArgPointee(unsigned ArgNo) const {
    return objectHoldsSecret(TaintObject::arg(ArgNo));
  }
  /// Every argument whose pointee object holds a secret, in increasing order.
  SmallVector<unsigned, 4> taintedArgPointees() const {
    SmallVector<unsigned, 4> Args;
    for (const auto &KV : Cells)
      if (KV.first.Obj.K == TaintObject::Arg && KV.second.Data)
        Args.push_back((unsigned)KV.first.Obj.Index);
    llvm::sort(Args);
    Args.erase(llvm::unique(Args), Args.end());
    return Args;
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
  void setPointeeGlobal(const GlobalVariable *GV) {
    if (GV)
      PointeeGlobals.insert(GV);
  }
  bool isPointeeGlobal(const GlobalVariable *GV) const {
    return GV && PointeeGlobals.contains(GV);
  }
  const DenseSet<const GlobalVariable *> &pointeeGlobals() const {
    return PointeeGlobals;
  }
  void setExternalMemClobbered() { ExternalMemClobbered = true; }
  bool isExternalMemClobbered() const { return ExternalMemClobbered; }

  //===--------------------------------------------------------------------===//
  // Heap / unknown memory
  //===--------------------------------------------------------------------===//

  void taintUnknownMem(const Value *V, TaintKind K) {
    if (V)
      UnknownMemValues[V].set(K, true);
  }
  /// A non-pointer store to V overwrote whatever pointer was spilled there.
  void clearUnknownMemPointee(const Value *V) {
    if (!V)
      return;
    auto It = UnknownMemValues.find(V);
    if (It == UnknownMemValues.end())
      return;
    It->second.Pointee = false;
    if (!It->second.any())
      UnknownMemValues.erase(It);
  }
  bool anyUnknownMem(TaintKind K) const {
    for (const auto &KV : UnknownMemValues)
      if (KV.second.get(K))
        return true;
    return false;
  }

  /// Phase 2 ("unknown means tainted", docs/design/taint-domain.md S5): does
  /// this state hold ANY memory-resident taint of kind K? The unknown-load
  /// flips use it as CIO's `make_top = Taint` fallback: a load whose object
  /// cannot be resolved may be reading any of it. Deliberately coarse - that
  /// is the experiment.
  bool anyMemTaint(TaintKind K) const {
    if (K == TaintKind::Data &&
        (UnknownMemTainted || ExternalMemClobbered ||
         !TaintedWholeGlobals.empty()))
      return true;
    for (const auto &KV : Cells)
      if (KV.second.get(K))
        return true;
    return anyUnknownMem(K);
  }

  //===--------------------------------------------------------------------===//
  // Flags
  //===--------------------------------------------------------------------===//

  /// A secret has been stored into the OUTGOING argument area and has not yet
  /// been consumed by a call. AAPCS64 passes arguments beyond the eighth (and
  /// large aggregates) in memory: the caller stores them to `$sp + imm` and the
  /// argument registers may then be overwritten before the call, so by the time
  /// the call is reached NO register holds the secret. Without this bit the
  /// analysis concluded that such a call passes nothing secret, and the callee
  /// was analysed as clean - a real leak, not merely imprecision
  /// (docs/design/stack-arguments.md).
  void setOutgoingArgSecret() { OutgoingArgSecret = true; }
  void clearOutgoingArgSecret() { OutgoingArgSecret = false; }
  bool isOutgoingArgSecret() const { return OutgoingArgSecret; }

  /// Some taint in this function did NOT enter through its own parameters: it
  /// was read from a tainted global, arrived from another TU's unknown memory,
  /// or was produced by a call this function did not hand a secret to. This is
  /// the source condition the mod-set call-site gate needs: a caller's arguments
  /// can only account for a callee's secret if that secret came from parameters
  /// in the first place. Monotone - set, never cleared, merged with OR.
  void setNonArgSourcedTaint() { NonArgSourcedTaint = true; }
  bool hasNonArgSourcedTaint() const { return NonArgSourcedTaint; }

  //===--------------------------------------------------------------------===//
  // Summary predicates
  //===--------------------------------------------------------------------===//

  bool emptyRegs() const {
    return TaintedRegs.empty() && PointeeTaintedRegs.empty();
  }
  /// No taint this function's OWN analysis produced: no tainted register, no
  /// named cell, no located or opaque heap store. Deliberately narrower than
  /// isBottom(): it ignores what was merely INHERITED from a callee (a
  /// whole-global clobber, ExternalMemClobbered) and the two side flags. This
  /// is the gate on export and instrumentation, and it is checked on the JOIN
  /// OF BLOCK EXITS, so a function whose only secret is consumed and cleared
  /// before every block exit reads as empty even though it executes tainted
  /// instructions - docs/design/taint-domain.md S5, preserved as is.
  bool empty() const {
    return emptyRegs() && Cells.empty() && UnknownMemValues.empty() &&
           !UnknownMemTainted;
  }

  unsigned countRegs() const {
    return TaintedRegs.count() + PointeeTaintedRegs.count();
  }
  unsigned countDataRegs() const { return TaintedRegs.count(); }
  unsigned countPointeeRegs() const { return PointeeTaintedRegs.count(); }
  unsigned countCells() const {
    return (unsigned)(Cells.size() + UnknownMemValues.size());
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

/// A libc mover (memcpy, memmove, mempcpy, memset), matched the way
/// getLibcMoveModel matches: a declaration or a bare libcall symbol. Under the
/// callee contract a secret passed to one IS an obligation: its loads and
/// stores of the secret bytes are exactly the data-value channel DIT covers
/// (docs/reference/dit-spec.md lists loads and stores; the gem5 oracle counts
/// them), and libc's copy runs with whatever mode it finds. The repair is a
/// hardened mover linked ahead of libc, seeded on the source pointee, which
/// the record names. This predicate only labels the ESCAPE line.
bool isLibcMover(const Function *Callee, const MachineInstr &MI);

/// The symbol a call targets when it has no Function: a libcall ISel emitted
/// for an intrinsic (`memcpy`, `memset`) references the bare external symbol,
/// so `findCalledFunction` returns null although the callee is perfectly well
/// named. Empty for a genuinely indirect call.
StringRef getCalleeSymbolName(const MachineInstr &MI);

/// A libc allocator (malloc, calloc, realloc, free, aligned_alloc, posix_memalign),
/// matched the same way. A secret reaching one is a size or a pointer derived
/// from a secret: DIT cannot cover an allocator's size-dependent control flow,
/// and no seed can fill it, so the obligation record says the repair is
/// upstream - stop deriving the size or the pointer from the secret.
bool isLibcAllocator(const Function *Callee, const MachineInstr &MI);

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
  /// WHICH argument registers carried it, as a bit per x0..x7. Only used to
  /// print an actionable seed line in the information-loss report - a report
  /// that says "annotate this" is worth little if the user has to work out the
  /// index themselves. Stack-passed secrets set no bit (their index is not
  /// recoverable here), so a repair suggestion is omitted rather than guessed.
  uint8_t DataMask = 0;
  uint8_t PointeeMask = 0;
  bool any() const { return Data || Pointee; }
};

/// How badly a loss of taint information hurts, judged by CONSEQUENCE rather
/// than by cause: the same "we cannot see the callee" fact is a footnote at an
/// ordinary call and a disaster at a tail call.
enum class TaintLossSeverity {
  Info,     ///< precision lost, no coverage consequence
  Moderate, ///< over-approximated, but DIT stays scoped to this function
  Severe,   ///< DIT enabled and provably never cleared - degenerates to blanket
  /// The analysis lost the secret and does NOT know it, so coverage may be
  /// ABSENT rather than merely wider than necessary. Every other severity here
  /// describes an OVER-approximation - the callee inherits DIT, or the whole
  /// function does - and is therefore safe but costly. This one is the opposite
  /// direction and is the only kind that can leak: nothing downstream re-adds
  /// the protection, because nothing downstream knows it is missing. Kept
  /// distinct from Severe so a report reader and a build gate can tell "we
  /// protected too much" from "we may have protected nothing".
  Unsound,
  /// The callee contract's obligation: a secret reaches a callee this build
  /// cannot see, and under `-taint-dit-contract=callee` nothing covers it on the
  /// callee's behalf. Coverage is ABSENT by design until the developer seeds the
  /// TU that defines the callee; the repair line is the seed. Known, not lost -
  /// which is what separates it from Unsound. Summarised on stderr once per TU
  /// by the writer rather than once per record.
  Uncovered,
};

/// One record in the information-loss report. See `-taint-info-loss-report`.
void reportInfoLoss(raw_ostream *OS, TaintLossSeverity Sev, StringRef Kind,
                    const Function &Where, StringRef CalleeName,
                    const DebugLoc &DL, StringRef Action, StringRef Cost,
                    StringRef Repair);

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
