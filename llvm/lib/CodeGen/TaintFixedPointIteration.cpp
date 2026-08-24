//===-- TaintFixedPointIteration.cpp - Interprocedural Taint Analysis -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// Module-level pass that runs interprocedural taint analysis.
///
/// This is the main entry point for taint analysis.  It:
/// 1. Iterates over all functions in the module
/// 2. Runs intraprocedural taint analysis on each (via TaintAnalysis)
/// 3. At call sites, propagates argument taint into callees (caller→callee)
/// 4. Collects return-value taint summaries (callee→caller)
/// 5. Repeats until fixed-point (handles recursive/mutual recursion)
/// 6. Exports tainted instructions to file
///
/// Usage:
///   llc -passes='taint-interproc' -taint-output=out.txt input.pe.mir
///
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/TaintFixedPointIteration.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/CodeGen/MachineFunctionAnalysis.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TaintAnalysis.h"
#include "llvm/CodeGen/TaintSummaryInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>

using namespace llvm;

#define DEBUG_TYPE "taint-interproc"

/// Walk every call instruction in MF and propagate argument taint into
/// the callee's summary.
///
/// For each call site we:
///   1. Replay taint state up to the call instruction (using TR.IN + propagate)
///   2. For each physical argument register that is tainted at the call,
///      determine the corresponding argument index using the callee's liveins
///   3. Store the tainted index in TSI so the callee gets analyzed with
///      those args on the next iteration
///
/// Returns true if any callee summary was updated (i.e. new arg was tainted).
static bool propagateArgTaintToCallees(MachineFunction &MF,
                                       const TaintResult &TR,
                                       TaintSummaryInfo &TSI, Module &M,
                                       FunctionAnalysisManager &FAM,
                                       AAResults *AA) {
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  bool Changed = false;

  // Uses the Pre hook: the taint of the callee's argument registers has to be
  // read in the state entering the call, before the call clears its result
  // registers.
  replayTaint(
      MF, TR, &TSI, AA, /*Post=*/{},
      [&](MachineInstr &MI, const TaintState &S) {
        if (!MI.isCall())
          return true;

        const Function *Callee = findCalledFunction(M, MI);
        if (!Callee || Callee->isDeclaration())
          return true;

        // Get the callee's MachineFunction so we can read its liveins
        auto *CalleeMFA = FAM.getCachedResult<MachineFunctionAnalysis>(
            *const_cast<Function *>(Callee));
        if (!CalleeMFA)
          return true;
        MachineFunction *CalleeMF = &CalleeMFA->getMF();

        // Walk callee's liveins and map each physical register to its argument
        // index via hardware encoding (not livein list order, which may differ).
        FunctionTaintSummary CalleeSummary = TSI.getSummary(*Callee);
        bool SummaryChanged = false;

        // A secret handed over in the outgoing argument area. No livein register
        // carries it, so the loop below cannot see it and the callee would be
        // analysed as clean — the leak documented in
        // docs/design/stack-arguments.md.
        if (S.isOutgoingArgSecret() && !CalleeSummary.StackArgTainted) {
          CalleeSummary.StackArgTainted = true;
          SummaryChanged = true;
          LLVM_DEBUG(dbgs() << "  caller " << MF.getName() << " -> callee "
                            << Callee->getName()
                            << ": receives a secret in a STACK argument\n");
        }
        for (const auto &[PhysReg, VirtReg] : CalleeMF->getRegInfo().liveins()) {
          unsigned ArgIdx = TRI->getEncodingValue(PhysReg);
          // AAPCS64 passes incoming arguments only in X0-X7 / V0-V7 (encodings
          // 0-7). x30 (LR), x29 (FP) and sp are livein of every function but
          // never carry an argument value, so a caller's taint on them is NOT a
          // secret passed to this callee. Skipping them is the safe direction (a
          // caller never passes data in x30/x29/sp) and stops a tainted *scratch*
          // use of x30 in the caller — e.g. the register allocator reusing x30
          // after the return address is spilled — from spuriously seeding the
          // callee's return-address register as secret. See
          // taint-analysis-lr-not-arg.mir.
          if (ArgIdx > 7)
            continue;
          // A Data-tainted register in a POINTER parameter means "pointer to a
          // secret", not "the address is itself a secret value". Recording it as
          // Data makes every value the callee computes from that pointer secret —
          // including further addresses — and that is what turns one poisoned
          // function into a poisoned subtree: on libsecp256k1 a single caller
          // spread Data-taint through the output pointers of five shared group
          // helpers, and those helpers are on the verification path.
          //
          // The callee's IR parameter type settles which reading is right, and it
          // is available here. The residual is a genuinely secret-valued pointer
          // (an address computed from a secret), which is secret-dependent
          // ADDRESSING — already outside what PSTATE.DIT covers and already
          // reported separately by -taint-uncovered-report as `secret-address`.
          const bool PtrParam = ArgIdx < Callee->arg_size() &&
                                Callee->getArg(ArgIdx)->getType()->isPointerTy();
          if (S.isTainted(PhysReg) && PtrParam &&
              CalleeSummary.PointeeTaintedArgIndices.insert(ArgIdx).second) {
            SummaryChanged = true;
            LLVM_DEBUG(dbgs() << "  caller " << MF.getName() << " -> callee "
                              << Callee->getName() << ": arg " << ArgIdx
                              << " now pointee-tainted (pointer param, via "
                              << printReg(PhysReg, TRI) << ")\n");
          }
          if (S.isTainted(PhysReg) && !PtrParam &&
              CalleeSummary.TaintedArgIndices.insert(ArgIdx).second) {
            SummaryChanged = true;
            LLVM_DEBUG(dbgs() << "  caller " << MF.getName() << " -> callee "
                              << Callee->getName() << ": arg " << ArgIdx
                              << " now tainted (via " << printReg(PhysReg, TRI)
                              << ")\n");
          }
          // KNOWN GAP: passing `&local_secret` in still transfers nothing when
          // the pointer register itself was never pointee-tainted, because
          // post-prologepilog the address is a bare `$sp + imm`. The
          // whole-frame fallback that used to bridge this cost +44 points
          // against the mod-set gate and has been removed; the fix is
          // per-object provenance on the caller->callee direction (P1b covers
          // the callee->caller half). See docs/design/p1b-frame-provenance.md.
          if (S.isPointeeTainted(PhysReg) &&
              CalleeSummary.PointeeTaintedArgIndices.insert(ArgIdx).second) {
            SummaryChanged = true;
            LLVM_DEBUG(dbgs() << "  caller " << MF.getName() << " -> callee "
                              << Callee->getName() << ": arg " << ArgIdx
                              << " now pointee-tainted (via "
                              << printReg(PhysReg, TRI) << ")\n");
          }
        }

        if (SummaryChanged) {
          TSI.storeSummary(*Callee, CalleeSummary);
          Changed = true;
        }
        return true;
      });

  return Changed;
}

static bool functionReturnsTainted(MachineFunction &MF, const TaintResult &TR,
                                   TaintSummaryInfo &TSI, Module &M,
                                   AAResults *AA) {
  bool ReturnsTainted = false;

  replayTaint(MF, TR, &TSI, AA,
              [&](MachineInstr &MI, const TaintFacts &F, const TaintState &) {
                if (!MI.isReturn() || !F.UsesData)
                  return true;
                ReturnsTainted = true;
                return false; // Stop the walk.
              });

  return ReturnsTainted;
}

/// Visit every function that has both a MachineFunction and a converged taint
/// result — i.e. everything the post-convergence steps (DIT summaries, reports,
/// barrier insertion) operate on.
static void forEachAnalyzed(
    Module &M, FunctionAnalysisManager &FAM,
    DenseMap<const Function *, TaintResult> &Results,
    function_ref<void(Function &, MachineFunction &, const TaintResult &,
                      AAResults *)>
        Fn) {
  for (Function &F : M) {
    if (F.isDeclaration())
      continue;
    auto *MFA = FAM.getCachedResult<MachineFunctionAnalysis>(F);
    if (!MFA)
      continue;
    auto It = Results.find(&F);
    if (It == Results.end())
      continue;
    Fn(F, MFA->getMF(), It->second, &FAM.getResult<AAManager>(F));
  }
}

PreservedAnalyses TaintInterprocPass::run(Module &M,
                                          ModuleAnalysisManager &MAM) {
  // Step 1: Get FunctionAnalysisManager — in the new PM, MachineFunctions
  // are stored per-function in the FAM (via MachineFunctionAnalysis),
  // NOT in MachineModuleInfo.
  auto &FAM = MAM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();

  // Step 2: Create TaintSummaryInfo — shared database of per-function summaries
  TaintSummaryInfo TSI;

  // Seed TSI from IR taint attributes (set by taint-annotate pass)
  for (Function &F : M) {
    if (F.isDeclaration())
      continue;
    SmallSet<unsigned, 8> TaintedArgs;
    SmallSet<unsigned, 8> PointeeTaintedArgs;
    for (const Argument &Arg : F.args()) {
      if (Arg.hasAttribute("tainted"))
        TaintedArgs.insert(Arg.getArgNo());
      if (Arg.hasAttribute("tainted-pointee"))
        PointeeTaintedArgs.insert(Arg.getArgNo());
    }
    if (!TaintedArgs.empty() || !PointeeTaintedArgs.empty()) {
      FunctionTaintSummary Summary;
      Summary.TaintedArgIndices = TaintedArgs;
      Summary.PointeeTaintedArgIndices = PointeeTaintedArgs;
      TSI.storeSummary(F, Summary);
      LLVM_DEBUG(dbgs() << "Seed: " << F.getName() << " has "
                        << TaintedArgs.size() << " tainted arg(s), "
                        << PointeeTaintedArgs.size()
                        << " pointee-tainted arg(s)\n");
    }
  }

  // Open trace file (derives path from TaintOutputFile: out_trace.txt)
  std::unique_ptr<raw_fd_ostream> TraceOS;
  if (!TaintOutputFile.empty())
    TraceOS = openTaintReport(deriveReportPath(TaintOutputFile, "_trace"),
                              "trace");

  // Step 3: Fixed-point iteration
  //
  // Each iteration does two things for every function with tainted args:
  //   (a) Run intraprocedural taint analysis (with TSI for call handling)
  //   (b) At each call site, propagate arg taint into callee summaries
  //
  // Convergence is guaranteed: taint only grows (monotonic), the lattice
  // (functions × args × {clean,tainted}) is finite.
  bool Changed = true;
  unsigned Iteration = 0;
  const unsigned MaxIterations = 100;

  // Store TaintResults per function (needed for export after convergence)
  DenseMap<const Function *, TaintResult> Results;

  while (Changed) {
    Changed = false;
    ++Iteration;

    if (Iteration > MaxIterations) {
      errs() << "ERROR: Taint analysis did not converge after " << MaxIterations
             << " iterations.\n"
             << "This indicates a bug (non-monotonic updates or "
             << "incorrect equality check).\n";
      report_fatal_error("Taint fixed-point iteration failed to converge");
    }

    LLVM_DEBUG(dbgs() << "\n=== Taint fixed-point iteration " << Iteration
                      << " ===\n");
    if (TraceOS)
      *TraceOS << "\n=== Iteration " << Iteration << " ===\n";

    for (Function &F : M) {
      if (F.isDeclaration())
        continue;

      auto *MFA = FAM.getCachedResult<MachineFunctionAnalysis>(F);
      if (!MFA) {
        LLVM_DEBUG(dbgs() << "  [skip] " << F.getName()
                          << ": no MachineFunction\n");
        continue;
      }
      MachineFunction *MF = &MFA->getMF();
      const TargetRegisterInfo *TRI = MF->getSubtarget().getRegisterInfo();

      // Get current summary (may have tainted args from IR attrs or
      // interprocedural propagation, or may be empty for functions that
      // only receive taint via callee return values).
      FunctionTaintSummary CurrentSummary = TSI.getSummary(F);

      // Log function entry with tainted arg seeds
      if (TraceOS) {
        auto dumpIdx = [&](const char *Label, const SmallSet<unsigned, 8> &Idx) {
          *TraceOS << "  " << Label << ":";
          if (Idx.empty())
            *TraceOS << " (none)";
          for (unsigned I : Idx)
            *TraceOS << " " << I;
          *TraceOS << "\n";
        };

        *TraceOS << "\n--- " << F.getName() << " ---\n";
        dumpIdx("tainted_args", CurrentSummary.TaintedArgIndices);
        dumpIdx("pointee_tainted_args", CurrentSummary.PointeeTaintedArgIndices);

        // Show which physical registers will be seeded via liveins
        *TraceOS << "  seeded_regs:";
        bool Any = false;
        for (const auto &[PhysReg, VirtReg] : MF->getRegInfo().liveins()) {
          unsigned ArgIdx = TRI->getEncodingValue(PhysReg);
          if (CurrentSummary.TaintedArgIndices.contains(ArgIdx)) {
            *TraceOS << " " << printReg(PhysReg, TRI) << "(arg" << ArgIdx
                     << ")";
            Any = true;
          }
          if (CurrentSummary.PointeeTaintedArgIndices.contains(ArgIdx)) {
            *TraceOS << " " << printReg(PhysReg, TRI) << "(pointee_arg"
                     << ArgIdx << ")";
            Any = true;
          }
        }
        if (!Any)
          *TraceOS << " (none)";
        *TraceOS << "\n";
      }

      // Run intraprocedural analysis with TSI.
      // TSI provides: (1) extra tainted-arg seeds from previous iterations,
      //               (2) callee return summaries for call handling
      TaintAnalysis TA;
      AAResults *AA = &FAM.getResult<AAManager>(F);
      TaintResult TR = TA.run(*MF, &TSI, AA);
      Results[&F] = TR;

      // Log merged taint result
      if (TraceOS) {
        auto dumpRegs = [&](const char *Label, TaintKind K) {
          *TraceOS << "  " << Label << ":";
          for (unsigned RegID : TR.Merged.regs(K))
            *TraceOS << " " << printReg(Register(RegID), TRI);
          *TraceOS << "\n";
        };

        *TraceOS << "  result: " << TR.Merged.countRegs() << " tainted regs "
                 << "(data=" << TR.Merged.countDataRegs()
                 << ", pointee=" << TR.Merged.countPointeeRegs()
                 << ", address=" << TR.Merged.countAddressRegs() << "), "
                 << TR.Merged.countCells() << " tainted cells";
        if (TR.Merged.UnknownMemTainted)
          *TraceOS << ", UnknownMemTainted";
        *TraceOS << "\n";
        dumpRegs("tainted_regs", TaintKind::Data);
        dumpRegs("pointee_tainted_regs", TaintKind::Pointee);
        dumpRegs("address_tainted_regs", TaintKind::Address);
      }

      // Keep unknown-memory taint intraprocedural. Promoting any tainted store
      // to unknown/heap memory into a module-wide load poison is too coarse:
      // it makes independent public heap reads, such as convolution kernel
      // coefficient loads, data-tainted in later fixed-point iterations.
      if (TR.Merged.UnknownMemTainted) {
        LLVM_DEBUG(dbgs() << "  " << F.getName()
                          << ": UnknownMemTainted kept local\n");
        if (TraceOS)
          *TraceOS << "  ** UnknownMemTainted kept local **\n";
      }

      // Check actual return instructions instead of hardcoding generated
      // physical-register enum values for W0/X0.
      bool ReturnsTainted = functionReturnsTainted(*MF, TR, TSI, M, AA);

      // Memory-effects (mod-set): which caller-visible memory this function may
      // write a secret into. Recomputed each iteration; it reads callee mem
      // effects applied during (a), so it converges with the register summary.
      FunctionMemEffects MemEffects =
          computeFunctionMemEffects(*MF, TR, &TSI, AA);

      // Build new summary.
      //
      // NB every monotone field set ELSEWHERE in this loop must be carried
      // forward here. NewSummary is default-constructed, so a field that
      // propagateArgTaintToCallees sets on F (rather than F setting on itself)
      // is wiped on the next visit, re-set on the one after, and the fixed point
      // never converges. Read OldSummary first so the carry sees the freshest
      // value, including anything a caller stored since CurrentSummary was taken.
      FunctionTaintSummary OldSummary = TSI.getSummary(F);
      FunctionTaintSummary NewSummary;
      NewSummary.TaintedArgIndices = CurrentSummary.TaintedArgIndices;
      NewSummary.PointeeTaintedArgIndices =
          CurrentSummary.PointeeTaintedArgIndices;
      NewSummary.ReturnsTainted = ReturnsTainted;
      NewSummary.MemEffects = MemEffects;
      NewSummary.StackArgTainted =
          CurrentSummary.StackArgTainted || OldSummary.StackArgTainted;

      if (NewSummary != OldSummary) {
        TSI.storeSummary(F, NewSummary);
        Changed = true;
        LLVM_DEBUG(dbgs() << "  " << F.getName() << ": summary changed"
                          << " (returns_tainted=" << ReturnsTainted << ")\n");
        if (TraceOS)
          *TraceOS << "  ** summary changed: returns_tainted=" << ReturnsTainted
                   << " **\n";
      } else {
        LLVM_DEBUG(dbgs() << "  " << F.getName() << ": unchanged\n");
      }

      // Propagate argument taint to callees.
      // If caller_simple passes a tainted X0 to identity(), this adds
      // arg 0 to identity's TaintedArgIndices in TSI.
      if (propagateArgTaintToCallees(*MF, TR, TSI, M, FAM, AA)) {
        Changed = true;
        if (TraceOS)
          *TraceOS << "  ** propagated arg taint to callee(s) **\n";
      }
    }
  }

  LLVM_DEBUG(dbgs() << "\nTaint analysis converged after " << Iteration
                    << " iteration(s)\n");
  LLVM_DEBUG(dbgs() << "Total function summaries: " << TSI.size() << "\n");

  if (TraceOS) {
    *TraceOS << "\n=== Converged after " << Iteration << " iteration(s) ===\n";
    *TraceOS << "Total function summaries: " << TSI.size() << "\n";
  }

  // Step 3b: Compute the PreservesDIT summary bit (greatest fixed point).
  // A function preserves PSTATE.DIT iff it will not be DIT-instrumented (no
  // tainted runs) and every call it makes is direct to a preserving in-TU
  // callee. Tail calls count: the tail-callee runs inside the caller's frame
  // from its own caller's perspective. Externals/indirect targets keep the
  // conservative default (false). Used by insertTaintDITSwitches to elide
  // after-call DIT re-asserts; must run before instrumentation below.
  if (TaintInsertDIT) {
    // Optimistic seed: a function preserves DIT unless it is itself
    // instrumented.
    forEachAnalyzed(M, FAM, Results,
                    [&](Function &F, MachineFunction &MF, const TaintResult &TR,
                        AAResults *AA) {
                      FunctionTaintSummary S = TSI.getSummary(F);
                      S.InstrumentedForDIT =
                          !TR.Merged.empty() &&
                          functionHasTaintedRuns(MF, TR, &TSI, AA);
                      S.PreservesDIT = !S.InstrumentedForDIT;
                      TSI.storeSummary(F, S);
                    });

    // Then retract: a function preserves DIT only if every function it calls
    // does too. Iterate to a greatest fixed point.
    bool PreservesChanged = true;
    while (PreservesChanged) {
      PreservesChanged = false;
      forEachAnalyzed(
          M, FAM, Results,
          [&](Function &F, MachineFunction &MF, const TaintResult &,
              AAResults *) {
            FunctionTaintSummary S = TSI.getSummary(F);
            if (!S.PreservesDIT)
              return;

            for (const MachineBasicBlock &MBB : MF)
              for (const MachineInstr &MI : MBB) {
                if (!MI.isCall())
                  continue;
                const Function *Callee = findCalledFunction(M, MI);
                if (Callee && TSI.getSummary(*Callee).PreservesDIT)
                  continue;
                S.PreservesDIT = false;
                TSI.storeSummary(F, S);
                PreservesChanged = true;
                return;
              }
          });
    }
    LLVM_DEBUG({
      for (Function &F : M)
        if (!F.isDeclaration())
          dbgs() << "  PreservesDIT(" << F.getName()
                 << ") = " << TSI.getSummary(F).PreservesDIT << "\n";
    });
  }

  // Step 3b-2: Compute AlwaysEnteredWithDIT - the DIT ownership bit.
  //
  // THE RULE: only the frame that turned PSTATE.DIT on may turn it off. Today
  // every instrumented function clears DIT before returning, so a callee reached
  // from an enclosing DIT-on region destroys its caller's state; the caller
  // repairs it with an after-call re-assert. That makes the switch count scale
  // with the CALL count instead of the number of secret regions - measured at
  // 1,792-4,352 switches per 4 KB page on SQLCipher, the same order as the
  // encryption itself. It is also a soundness gap under region placement, where
  // a callee's INTERNAL region ends clear DIT while the caller's secret is still
  // live and nothing repairs it until after the call returns.
  //
  // A function is entered with DIT already on when it cannot be reached except
  // through a secret-passing call. We require, conservatively:
  //   * local linkage        - no caller outside this TU,
  //   * address never taken  - no indirect call can reach it,
  //   * >= 1 in-TU call site - otherwise it is dead or an entry point,
  //   * EVERY in-TU call site passes a secret.
  //
  // The last condition is what ties this to DIT actually being on. What actually
  // enforces it is the `|| MI.isCall()` term in needsDIT (TaintAnalysis.cpp),
  // which makes a secret-passing call a Need, plus the region-placement
  // soundness verifier, which falls back to whole-function coverage rather than
  // leave a Need uncovered. Whole-function placement gets it trivially. Step 3c
  // is a weaker check than it looks -- it only asserts the ENCLOSING function is
  // instrumented, and it runs before any switch is placed, so it knows nothing
  // about region boundaries. If needsDIT ever stops treating a secret-passing
  // call as a Need, THIS BIT BECOMES UNSOUND; that term is load-bearing here.
  //
  // Retraction-shaped, like PreservesDIT above: seed the candidates optimistically
  // and disqualify on evidence. A call site we fail to observe therefore cannot
  // silently qualify a function - only observed secret-passing sites keep it.
  if (TaintInsertDIT) {
    // The retraction below can only disqualify a callee from call sites it
    // actually walks, and forEachAnalyzed silently skips any function without a
    // cached MachineFunction or taint result. A skipped caller could hold the
    // one non-secret-passing call site that should have disqualified a callee,
    // and marking it anyway means it stops clearing DIT and leaks the mode into
    // ordinary public code. Cheaper to verify coverage once and stand down
    // entirely than to reason about which callee a gap could have affected.
    unsigned Defined = 0, Analyzed = 0;
    for (const Function &F : M)
      if (!F.isDeclaration())
        ++Defined;
    forEachAnalyzed(M, FAM, Results,
                    [&](Function &, MachineFunction &, const TaintResult &,
                        AAResults *) { ++Analyzed; });
    const bool CoverageComplete = (Analyzed == Defined);
    LLVM_DEBUG(if (!CoverageComplete) dbgs()
               << "  AlwaysEnteredWithDIT disabled: " << Analyzed << "/"
               << Defined << " defined functions analyzed\n");

    SmallPtrSet<const Function *, 16> Candidates;
    forEachAnalyzed(
        M, FAM, Results,
        [&](Function &F, MachineFunction &MF, const TaintResult &TR,
            AAResults *AA) {
          // An uninstrumented function emits no switches, so the bit would be
          // moot; requiring instrumentation also keeps the consumer in
          // TaintAnalysis.cpp simple.
          if (!F.hasLocalLinkage() || F.hasAddressTaken() ||
              !functionHasTaintedRuns(MF, TR, &TSI, AA))
            return;

          // RETRACT THROUGH TAIL CALLS. The bit records an ENTRY property ("DIT
          // was already on when I was called"), but its consumer,
          // calleeLeavesDITSet, needs an EXIT property ("I return with DIT
          // still on") in order to drop the caller's re-assert. Those coincide
          // only because a marked function emits no clear -- and a TAIL CALL
          // breaks that, because it IS this function's exit and there is no
          // instruction after it at which state could be restored. Control
          // reaches the tail callee's own `MSR DIT, #0`, whose `ret` returns
          // straight to OUR caller, which has already elided its re-assert. The
          // caller then runs its secret code with DIT=0.
          //
          // Requiring PreservesDIT of the tail callee (i.e. it does not touch
          // DIT at all) is deliberately stronger than necessary -- a tail callee
          // that is itself marked also refrains from clearing -- but that is
          // mutual recursion between the two bits, and this is the direction
          // where being wrong costs the secret rather than a few cycles.
          // Test: taint-analysis-dit-tailcall-ownership.mir.
          for (const MachineBasicBlock &MBB : MF)
            for (const MachineInstr &MI : MBB) {
              if (!MI.isCall() || !MI.isReturn())
                continue; // not a tail call
              const Function *TailCallee = findCalledFunction(M, MI);
              if (!TailCallee || !TSI.getSummary(*TailCallee).PreservesDIT)
                return; // cannot guarantee we return with DIT still set
            }

          Candidates.insert(&F);
        });

    SmallPtrSet<const Function *, 16> Seen;
    forEachAnalyzed(
        M, FAM, Results,
        [&](Function &, MachineFunction &MF, const TaintResult &TR,
            AAResults *AA) {
          if (Candidates.empty())
            return;
          const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
          replayTaint(MF, TR, &TSI, AA, /*Post=*/{},
                      /*Pre=*/[&](MachineInstr &MI, const TaintState &State) {
                        if (!MI.isCall())
                          return true;
                        const Function *Callee = findCalledFunction(M, MI);
                        if (!Callee || !Candidates.contains(Callee))
                          return true;
                        // Pre-state, for the same reason step 3c uses it: the
                        // arguments are set up before the call, so the leaving
                        // state would misread a tainted RETURN value as a
                        // passed argument.
                        if (taintedCallArguments(MI, State, TRI).any())
                          Seen.insert(Callee);
                        else
                          Candidates.erase(Callee); // reached with DIT possibly off
                        return true;
                      });
        });

    for (Function &F : M) {
      if (!CoverageComplete || F.isDeclaration() ||
          !Candidates.contains(&F) || !Seen.contains(&F))
        continue;
      FunctionTaintSummary S = TSI.getSummary(F);
      S.AlwaysEnteredWithDIT = true;
      TSI.storeSummary(F, S);
    }
    LLVM_DEBUG({
      for (Function &F : M)
        if (!F.isDeclaration())
          dbgs() << "  AlwaysEnteredWithDIT(" << F.getName()
                 << ") = " << TSI.getSummary(F).AlwaysEnteredWithDIT << "\n";
    });
  }

  // Step 3c: Call-site secret audit + Scenario-B coverage verification.
  //
  // Two distinct things happen at a call that receives a secret:
  //
  //  (A) ESCAPE audit — the secret is passed to a callee the analysis cannot
  //      instrument (external declaration or indirect target). Unlike the old
  //      ISB/DSB model, this is NOT an unprotected hazard: PSTATE.DIT is
  //      inherited, so the callee runs with DIT=1 (see docs/design/dit-placement.md
  //      G3). The line is an audit record of where secrets leave the TU, not a
  //      list of unprotected sites.
  //
  //  (B) Coverage invariant — for the secret to be protected DURING the call,
  //      the call must execute with DIT=1. Under function granularity that is
  //      guaranteed (a tainted call argument makes the enclosing function have
  //      a tainted run, so it is DIT-instrumented and entry set DIT=1). We do
  //      not assume this — we verify it, so the future region work cannot
  //      silently break it. A violation is a leaked secret: report UNCOVERED
  //      and, in an assertions build, assert.
  {
    auto CallsiteOSPtr =
        openTaintReport(TaintCallsiteReportFile, "taint callsite report");
    raw_fd_ostream *CallsiteOS = CallsiteOSPtr.get();
    forEachAnalyzed(
        M, FAM, Results,
        [&](Function &F, MachineFunction &MF, const TaintResult &TR,
            AAResults *AA) {
          bool FnInstrumented = functionHasTaintedRuns(MF, TR, &TSI, AA);
          const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
          replayTaint(
              MF, TR, &TSI, AA, /*Post=*/{},
              /*Pre=*/[&](MachineInstr &MI, const TaintState &State) {
                // Fix B: only a secret genuinely *passed* to the callee matters
                // — a secret merely live/clobbered across the call is not
                // something an ABI-compliant callee can read. Read the state
                // ENTERING the call (the Pre hook): arguments are set up before
                // the call, and the call then clears/sets result registers, so
                // the leaving state would misread a tainted *return* value in x0
                // as a passed argument. (propagateArgTaintToCallees uses Pre for
                // the same reason.)
                CallArgTaint Arg = taintedCallArguments(MI, State, TRI);
                if (!MI.isCall() || !Arg.any())
                  return true;

                // (B) The enclosing function must be DIT-instrumented, else the
                // secret executes through the call with DIT off.
                if (TaintInsertDIT && !FnInstrumented) {
                  errs() << "taint: UNCOVERED secret-passing call in "
                         << F.getName()
                         << " but the function is not DIT-instrumented "
                            "(Scenario-B invariant violated)\n";
                  assert(false && "secret-passing call outside DIT coverage");
                }

                // (A) ESCAPE audit — only callees we cannot instrument.
                const Function *Callee = findCalledFunction(M, MI);
                if ((Callee && !Callee->isDeclaration()) || !CallsiteOS)
                  return true;

                *CallsiteOS << "ESCAPE "
                            << (Callee ? "external" : "indirect") << " callee="
                            << (Callee ? Callee->getName() : "<indirect>")
                            << " caller=" << F.getName()
                            << " bb=" << MI.getParent()->getNumber();
                if (const DebugLoc &DL = MI.getDebugLoc())
                  *CallsiteOS << " line=" << DL.getLine();
                if (Arg.Data)
                  *CallsiteOS << " tainted-args";
                if (Arg.Pointee)
                  *CallsiteOS << " pointee-tainted-args";
                *CallsiteOS << " (covered by inherited DIT)\n";
                return true;
              });
        });
  }

  // Step 3c-2: Memory-clobber report — the *sources* of cross-function memory
  // taint. Every call site listed here makes the caller treat memory as secret
  // (sets ExternalMemClobbered, or a whole-global taint), which then poisons
  // every subsequent load in sound mode. These are the points where a "taint
  // explosion" originates, so they can be pinpointed and audited. Reasons:
  //   external-arg / indirect-arg : a callee we cannot instrument receives a
  //       *passed* secret (data or pointee arg) -> blunt TOP (whole memory).
  //   modset-top                  : an in-TU callee's mod-set is TOP (it does
  //       something to memory we could not pin down) -> whole memory.
  //   modset-argptr               : an in-TU callee writes a secret through a
  //       pointer arg; still applied bluntly (P1a) -> whole memory.
  //   modset-global               : an in-TU callee writes a secret into a
  //       specific global (precise: only that global is poisoned).
  // The mod-set reasons fire regardless of what THIS caller passes, because a
  // callee mod-set is context-insensitive (matches propagateTaintMI).
  if (auto ClobberOSPtr =
          openTaintReport(TaintClobberReportFile, "taint clobber report")) {
    raw_fd_ostream &ClobberOS = *ClobberOSPtr;
    forEachAnalyzed(
        M, FAM, Results,
        [&](Function &F, MachineFunction &MF, const TaintResult &TR,
            AAResults *AA) {
          const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
          replayTaint(
              MF, TR, &TSI, AA, /*Post=*/{},
              /*Pre=*/[&](MachineInstr &MI, const TaintState &State) {
                if (!MI.isCall())
                  return true;
                const Function *Callee = findCalledFunction(M, MI);
                CallArgTaint Arg = taintedCallArguments(MI, State, TRI);
                const char *Reason = nullptr;
                std::string Detail;
                if (Callee && !Callee->isDeclaration()) {
                  const FunctionMemEffects &ME =
                      TSI.getSummary(*Callee).MemEffects;
                  if (ME.WritesSecretToUnknown) {
                    Reason = "modset-top";
                  } else if (!ME.WritesSecretThroughArgPointee.empty()) {
                    Reason = "modset-argptr";
                  } else if (!ME.WritesSecretToGlobal.empty()) {
                    Reason = "modset-global";
                    for (const GlobalVariable *GV : ME.WritesSecretToGlobal)
                      Detail += (Detail.empty() ? " globals=" : ",") +
                                GV->getName().str();
                  }
                } else if (Arg.any()) {
                  Reason = Callee ? "external-arg" : "indirect-arg";
                  if (Arg.Data)
                    Detail += " data";
                  if (Arg.Pointee)
                    Detail += " pointee";
                }
                if (!Reason)
                  return true;
                ClobberOS << "CLOBBER " << Reason
                          << " callee=" << (Callee ? Callee->getName()
                                                   : StringRef("<indirect>"))
                          << " caller=" << F.getName()
                          << " bb=" << MI.getParent()->getNumber();
                if (const DebugLoc &DL = MI.getDebugLoc())
                  ClobberOS << " line=" << DL.getLine();
                ClobberOS << Detail << "\n";
                return true;
              });
        });
  }

  // Step 3d: DIT-uncovered report (gap G2). A function running in DIT mode is
  // NOT protected on every tainted instruction: divide/sqrt are not DIT-listed,
  // a secret-dependent memory ADDRESS leaks through cache/TLB timing (DIT covers
  // the data value, not the address), and a secret-dependent BRANCH leaks
  // through control-flow timing. Counting these as protected is silent false
  // assurance — surface them for audit / constant-time rewriting.
  if (auto UncoveredOSPtr =
          openTaintReport(TaintUncoveredReportFile, "taint uncovered report")) {
    raw_fd_ostream &UncoveredOS = *UncoveredOSPtr;
    forEachAnalyzed(
        M, FAM, Results,
        [&](Function &F, MachineFunction &MF, const TaintResult &TR,
            AAResults *AA) {
          const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
          replayTaint(
              MF, TR, &TSI, AA,
              [&](MachineInstr &MI, const TaintFacts &Facts,
                  const TaintState &S) {
                const char *Reason = classifyDITUncovered(MI, Facts, S, *TII);
                if (!Reason)
                  return true;
                UncoveredOS << "UNCOVERED " << Reason
                            << " func=" << F.getName()
                            << " bb=" << MI.getParent()->getNumber();
                if (const DebugLoc &DL = MI.getDebugLoc())
                  UncoveredOS << " line=" << DL.getLine();
                UncoveredOS << " : ";
                MI.print(UncoveredOS, /*IsStandalone=*/true);
                return true;
              });
        });
  }

  // Truncate the DIT re-assert report before the per-function passes append to
  // it, matching TaintOutputFile below. Without this a second compilation into
  // the same path, or a multi-file build, lists every call site N times and the
  // toggle cost reads N-fold inflated.
  if (TaintInsertDIT)
    openTaintReport(TaintDITReassertReportFile, "DIT re-assert report");

  // Step 4: Export tainted instructions to file
  if (!TaintOutputFile.empty()) {
    SmallString<256> SrcPath = deriveReportPath(TaintOutputFile, "_src");
    SmallString<256> StatsPath = deriveReportPath(TaintOutputFile, "_stats");

    // Truncate first: the per-function loop below appends.
    openTaintReport(TaintOutputFile, "taint output");
    openTaintReport(SrcPath, "source output");

    // Collect per-function stats for sorting.
    SmallVector<FunctionTaintStats, 32> AllStats;

    forEachAnalyzed(
        M, FAM, Results,
        [&](Function &, MachineFunction &MF, const TaintResult &TR,
            AAResults *AA) {
          if (TR.Merged.empty())
            return;

          auto OS = openTaintReport(TaintOutputFile, "taint output",
                                    /*Append=*/true);
          if (!OS)
            return;
          auto SrcOS =
              openTaintReport(SrcPath, "source output", /*Append=*/true);

          FunctionTaintStats Stats;
          exportTaintedInstructions(MF, TR, &TSI, *OS, SrcOS.get(), &Stats, AA);
          if (!Stats.Output.empty())
            AllStats.push_back(std::move(Stats));
        });

    // Sort by taint ratio, highest first.
    llvm::sort(AllStats,
               [](const FunctionTaintStats &A, const FunctionTaintStats &B) {
                 return A.TaintRatio > B.TaintRatio;
               });

    if (auto StatsOS = openTaintReport(StatsPath, "stats output"))
      for (const auto &S : AllStats)
        *StatsOS << S.Output;

    LLVM_DEBUG(dbgs() << "Exported tainted instructions to " << TaintOutputFile
                      << "\n");
  }

  std::unique_ptr<raw_fd_ostream> RegionsOS =
      openTaintReport(TaintRegionsOutputFile, "taint regions output");
  std::unique_ptr<raw_fd_ostream> SourceRegionsOS =
      openTaintReport(TaintSourceRegionsOutputFile, "taint source regions "
                                                    "output");

  unsigned ProtectedInstrs = 0;
  unsigned RegionsReported = 0;
  unsigned SourceRegionsReported = 0;
  if (TaintInsertDIT || RegionsOS || SourceRegionsOS) {
    forEachAnalyzed(M, FAM, Results,
                    [&](Function &, MachineFunction &MF, const TaintResult &TR,
                        AAResults *AA) {
                      if (TR.Merged.empty())
                        return;

                      if (TaintInsertDIT)
                        ProtectedInstrs += insertTaintDITSwitches(
                            MF, TR, &TSI, RegionsOS.get(), AA);
                      else if (RegionsOS)
                        RegionsReported += exportTaintBarrierRegions(
                            MF, TR, &TSI, *RegionsOS, AA);

                      if (SourceRegionsOS)
                        SourceRegionsReported += exportTaintSourceRegions(
                            MF, TR, &TSI, *SourceRegionsOS, AA);
                    });

    if (TaintInsertDIT) {
      LLVM_DEBUG(dbgs() << "Enabled PSTATE.DIT over " << ProtectedInstrs
                        << " tainted instruction(s)\n");
      if (TraceOS)
        *TraceOS << "Enabled PSTATE.DIT over " << ProtectedInstrs
                 << " tainted instruction(s)\n";
    } else if (TraceOS && RegionsOS) {
      *TraceOS << "Reported protected regions covering " << RegionsReported
               << " tainted instruction(s)\n";
    }

    if (TraceOS && RegionsOS)
      *TraceOS << "Taint regions report: " << TaintRegionsOutputFile << "\n";
    if (TraceOS && SourceRegionsOS)
      *TraceOS << "Taint source regions report: "
               << TaintSourceRegionsOutputFile << " (" << SourceRegionsReported
               << " source region(s))\n";
  }

  // The pass mutates cached MachineFunctions, not the IR module. Preserve the
  // analysis cache so a following MIR printer sees the modified functions.
  return PreservedAnalyses::all();
}
