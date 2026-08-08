//===- TaintSummaryInfo.h - Interprocedural Taint Summaries ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines TaintSummaryInfo, which stores interprocedural taint
// analysis summaries for functions. Each summary captures which arguments
// are tainted and whether the function returns tainted values.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_TAINTSUMMARYINFO_H
#define LLVM_CODEGEN_TAINTSUMMARYINFO_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"

namespace llvm {

/// Coarse, alias-case-free memory-effects (mod-set) summary: which
/// caller-visible memory a function may have written a secret into by the time
/// it returns. This is the callee->caller-through-memory transfer the register
/// summary cannot express; without it a callee that writes a secret into
/// caller-visible memory leaves the caller's reload untainted (the open
/// unsoundness fixed by this component). See utils/taint_memory_summary_research.md.
///
/// The design is deliberately blunt in P0 (2026-07-15): no per-argument or
/// per-offset precision, weak updates only. Every truncation over-approximates
/// to WritesSecretToUnknown (TOP), which is the sound direction for a hardener.
/// Provenance-based arg-i / escaped-object precision is the P1 refinement.
struct FunctionMemEffects {
  /// Specific globals the function may write a secret into. Kept precise so a
  /// direct in-TU call that writes one global does not poison every global.
  SmallPtrSet<const GlobalVariable *, 4> WritesSecretToGlobal;

  /// Indices of pointer arguments through whose pointee the function may write a
  /// secret (P1 argument-provenance mod-set, utils/taint_memory_summary_research.md
  /// §11/P1). Precise alternative to WritesSecretToUnknown for the canonical
  /// callee->caller-through-memory write (a store through a pointer parameter).
  /// Populated for DIRECT stores whose destination resolves (via the MMO's
  /// underlying IR Value) to a function Argument; a store whose provenance cannot
  /// be resolved still escalates to WritesSecretToUnknown.
  ///
  /// NOTE (staging): as of P1a this set is *recorded* precisely but *applied*
  /// bluntly — the call site still treats a non-empty set as a full
  /// ExternalMemClobbered, so behavior is byte-identical to blunt-TOP P0. P1b
  /// makes the application precise: taint only the pointee of the pointer the
  /// caller actually passed for argument i, instead of poisoning all of its
  /// memory.
  SmallSet<unsigned, 4> WritesSecretThroughArgPointee;

  /// TOP: the function may have written a secret to memory the analysis cannot
  /// pin down — to the heap, through an unresolvable pointer, or transitively via
  /// a call it makes to an unknown/unknown-writing callee. Mandatory default for
  /// external declarations and indirect calls that receive a secret.
  bool WritesSecretToUnknown = false;

  bool operator==(const FunctionMemEffects &O) const {
    return WritesSecretToUnknown == O.WritesSecretToUnknown &&
           WritesSecretToGlobal == O.WritesSecretToGlobal &&
           WritesSecretThroughArgPointee == O.WritesSecretThroughArgPointee;
  }
  bool operator!=(const FunctionMemEffects &O) const { return !(*this == O); }
};

/// Summary of taint behavior for a single function.
struct FunctionTaintSummary {
  /// Indices of tainted arguments (0-7 for X0-X7/W0-W7 in AArch64).
  SmallSet<unsigned, 8> TaintedArgIndices;

  /// Indices of pointer arguments whose pointee memory is tainted.
  SmallSet<unsigned, 8> PointeeTaintedArgIndices;

  /// Whether the function returns a tainted value (X0/W0).
  bool ReturnsTainted = false;

  /// Which caller-visible memory the function may write a secret into.
  FunctionMemEffects MemEffects;

  /// Conservative flag: if true, assume all arguments taint return.
  /// Used for external functions, indirect calls, etc.
  bool IsConservative = false;

  /// Whether the function is guaranteed to leave PSTATE.DIT unchanged on every
  /// path to every exit (it is not DIT-instrumented itself and only makes
  /// direct calls to preserving callees). Default false = conservative
  /// (externals, indirect targets, instrumented functions). Computed after the
  /// taint fixed point converges; used to elide after-call DIT re-asserts.
  bool PreservesDIT = false;

  /// Whether EVERY entry to this function already has PSTATE.DIT set, so the
  /// function did not turn DIT on and must not turn it off ("only the frame
  /// that set DIT may clear it"). True only when the function cannot be reached
  /// except through a secret-passing call: local linkage, address never taken,
  /// at least one in-TU call site, and every such call site passes a secret -
  /// which the Scenario-B coverage invariant (step 3c) already verifies runs
  /// with DIT=1. Default false = conservative (the function owns DIT and clears
  /// it on exit, today's behavior).
  ///
  /// Consumers: such a function keeps its redundant entry enable but emits no
  /// disable, and its callers skip the after-call re-assert. Note the direction
  /// of failure - a function wrongly marked true merely leaves DIT set (a dwell
  /// cost); wrongly marked false costs only the switches we have today. Eliding
  /// the entry ENABLE would be the unsafe direction and is deliberately not done
  /// here; that needs a must-analysis.
  bool AlwaysEnteredWithDIT = false;

  bool operator==(const FunctionTaintSummary &Other) const {
    return TaintedArgIndices == Other.TaintedArgIndices &&
           PointeeTaintedArgIndices == Other.PointeeTaintedArgIndices &&
           ReturnsTainted == Other.ReturnsTainted &&
           MemEffects == Other.MemEffects &&
           IsConservative == Other.IsConservative &&
           PreservesDIT == Other.PreservesDIT &&
           AlwaysEnteredWithDIT == Other.AlwaysEnteredWithDIT;
  }

  bool operator!=(const FunctionTaintSummary &Other) const {
    return !(*this == Other);
  }
};

/// Stores taint summaries for all functions in a module.
/// This enables interprocedural taint analysis by allowing passes to
/// query the taint behavior of callees.
class TaintSummaryInfo {
  /// Map from Function to its taint summary.
  DenseMap<const Function *, FunctionTaintSummary> Summaries;

  /// Optional module-level flag for unknown/heap memory. The current
  /// fixed-point pass intentionally keeps unknown-memory taint local because a
  /// module-wide heap poison is too imprecise for public heap inputs.
  bool ModuleUnknownMemTainted = false;

public:
  TaintSummaryInfo() = default;

  /// Set the module-level unknown-mem-tainted flag.
  /// Returns true if the flag changed (was previously false).
  bool setUnknownMemTainted() {
    if (!ModuleUnknownMemTainted) {
      ModuleUnknownMemTainted = true;
      return true;
    }
    return false;
  }

  /// Check the module-level unknown-mem-tainted flag.
  bool hasUnknownMemTainted() const { return ModuleUnknownMemTainted; }

  /// Store a taint summary for a function.
  void storeSummary(const Function &F, FunctionTaintSummary Summary) {
    Summaries[&F] = Summary;
  }

  /// Get the taint summary for a function.
  /// Returns an empty summary if the function has no stored summary.
  FunctionTaintSummary getSummary(const Function &F) const {
    auto It = Summaries.find(&F);
    if (It != Summaries.end())
      return It->second;
    return FunctionTaintSummary{}; // Empty summary
  }

  /// Check if a function has a stored summary.
  bool hasSummary(const Function &F) const {
    return Summaries.find(&F) != Summaries.end();
  }

  /// Clear all summaries.
  void clear() { Summaries.clear(); }

  /// Get the number of functions with summaries.
  size_t size() const { return Summaries.size(); }

  /// Check if there are any summaries stored.
  bool empty() const { return Summaries.empty(); }
};

} // namespace llvm

#endif // LLVM_CODEGEN_TAINTSUMMARYINFO_H
