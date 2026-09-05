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
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TaintAnalysis.h"
#include "llvm/CodeGen/TaintSummaryInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/Debug.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/LineIterator.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>

using namespace llvm;

#define DEBUG_TYPE "taint-interproc"

/// Walk every call instruction in MF and propagate argument taint into
/// the callee's summary.
///
/// True if this state has a secret somewhere in memory the analysis is tracking
/// - a tainted or pointee-tainted stack cell, or a call-induced clobber. Used
/// only to decide whether a MEMORY information-loss record is worth emitting;
/// it deliberately does NOT feed taint propagation, because the whole-frame
/// version of that reasoning cost +44 points against the mod-set gate and was
/// removed (docs/design/frame-addr-fallback.md).
static bool frameMayHoldSecret(const TaintState &S) {
  // The UNKNOWN entries matter as much as the resolved cells, and omitting
  // them was this predicate's first bug: at -O2 a user local's MMO underlying
  // object is frequently not a resolvable frame cell, so the secret lands in
  // UnknownMemValues instead of a frame cell (the case
  // frame-addr-fallback.md records as "they fall through to Unknown"). Checking
  // only the resolved cells therefore missed libsodium's argon2id entirely -
  // `argon2_hash` stores the password pointer into a stack-allocated
  // argon2_context and passes its address to `argon2_ctx`, and no record was
  // emitted. A secret in memory the analysis CANNOT pin down is strictly more
  // reason to warn than one it can.
  //
  // Globals are deliberately excluded: a callee can reach a global without the
  // caller passing anything, so a frame-address argument is not the mechanism
  // and the record would not be actionable.
  return S.isExternalMemClobbered() || S.UnknownMemTainted ||
         S.anyFrameCell() || !S.UnknownMemValues.empty();
}

/// True if `Reg`, as it reaches `Call`, was computed from the frame base - i.e.
/// it is the address of a local, the `$sp + imm` that prologepilog leaves behind
/// after erasing the FrameIndex.
///
/// Walks back to the defining instruction within the call's own block and asks
/// whether that instruction reads SP or FP. Deliberately gives up (returns
/// false) when the def is not in this block or the budget runs out: this drives
/// a DIAGNOSTIC, so a missed record costs a line of report while a guessed one
/// costs the reader's trust in every other line.
static bool argIsFrameAddress(const MachineInstr &Call, Register Reg,
                              const TargetRegisterInfo *TRI) {
  const MachineFunction &MF = *Call.getMF();
  const Register FP = TRI->getFrameRegister(MF);
  const Register SP =
      MF.getSubtarget().getTargetLowering()->getStackPointerRegisterToSaveRestore();
  unsigned Budget = 64;
  // getPrevNode() rather than a reverse iterator: MachineInstr::getReverseIterator
  // and MachineBasicBlock::rend() are different iterator families (ilist_iterator
  // vs MachineInstrBundleIterator) and will not compare.
  for (const MachineInstr *Prev = Call.getPrevNode(); Prev && Budget;
       Prev = Prev->getPrevNode(), --Budget) {
    const MachineInstr &Def = *Prev;
    if (!Def.definesRegister(Reg, TRI))
      continue;
    for (const MachineOperand &MO : Def.uses()) {
      if (!MO.isReg() || !MO.getReg().isValid())
        continue;
      if ((SP.isValid() && TRI->regsOverlap(MO.getReg(), SP)) ||
          (FP.isValid() && TRI->regsOverlap(MO.getReg(), FP)))
        return true;
    }
    return false; // defined here, but not from the frame base
  }
  return false;
}

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
                                       TaintMFContext Ctx, AAResults *AA) {
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
        MachineFunction *CalleeMF =
            Ctx.GetMF(*const_cast<Function *>(Callee));
        if (!CalleeMF)
          return true;

        // Walk callee's liveins and map each physical register to its argument
        // index via hardware encoding (not livein list order, which may differ).
        FunctionTaintSummary CalleeSummary = TSI.getSummary(*Callee);
        bool SummaryChanged = false;

        // A secret handed over in the outgoing argument area. No livein register
        // carries it, so the loop below cannot see it and the callee would be
        // analysed as clean - the leak documented in
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
          // use of x30 in the caller - e.g. the register allocator reusing x30
          // after the return address is spilled - from spuriously seeding the
          // callee's return-address register as secret. See
          // taint-analysis-lr-not-arg.mir.
          if (ArgIdx > 7)
            continue;
          // A Data-tainted register in a POINTER parameter means "pointer to a
          // secret", not "the address is itself a secret value". Recording it as
          // Data makes every value the callee computes from that pointer secret -
          // including further addresses - and that is what turns one poisoned
          // function into a poisoned subtree: on libsecp256k1 a single caller
          // spread Data-taint through the output pointers of five shared group
          // helpers, and those helpers are on the verification path.
          //
          // The callee's IR parameter type settles which reading is right, and it
          // is available here. The residual is a genuinely secret-valued pointer
          // (an address computed from a secret), which is secret-dependent
          // ADDRESSING - already outside what PSTATE.DIT covers and already
          // reported separately by -taint-uncovered-report as `secret-address`.
          // A DECLASSIFIED parameter absorbs taint and reports none: the
          // callee has asserted that whatever arrives here is public. Skipping
          // the propagation, rather than filtering later, also keeps the
          // callee's SUMMARY clean, so nothing downstream re-derives it.
          if (ArgIdx < Callee->arg_size() &&
              Callee->getArg(ArgIdx)->hasAttribute("declassified")) {
            LLVM_DEBUG(dbgs() << "  caller " << MF.getName() << " -> callee "
                              << Callee->getName() << ": arg " << ArgIdx
                              << " is DECLASSIFIED, taint stops here\n");
            continue;
          }

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
          // The caller->callee half of the frame-address gap: passing
          // `&local_secret` in. Post-prologepilog the address is a bare
          // `$sp + imm`, so the pointer register carries no taint of its own
          // and nothing transfers - the secret is in a MEMORY CELL, not a
          // register.
          //
          // Under -taint-frame-addr-args, use P1b's per-object frame provenance
          // to bridge it: if this argument register is known to point at a
          // frame object that holds a secret, the callee's parameter really is
          // a pointer to secret memory. Monotone, so the fixed point still
          // converges: the mark can only be added, and it is what lets the
          // callee's own analysis discover the write-back its mod-set then
          // re-exports.
          //
          // DEFAULT OFF, deliberately. The whole-frame form of this cost +44
          // points against the mod-set gate; the per-object form measured
          // 408 -> 628 switches on libsecp256k1 with 12 false positives back in
          // `ecdsa_verify` (docs/design/p1b-frame-provenance.md §4). What is new
          // is the other side of the ledger: with it off, the gem5 shadow-taint
          // oracle measures 97.61% of libhydrogen's secret-carrying operations
          // committing with PSTATE.DIT clear under a natural seed
          // (paper_experiments/08-seed-ground-truth).
          if (PtrParam)
            if (auto B = S.getPointerBase(PhysReg)) {
              const bool IsFrame = B->K == TaintObject::Frame;
              const bool PointsAtSecret =
                  (IsFrame ? TaintFrameAddrArgs : TaintArgPointeeArgs) &&
                  S.objectHoldsSecret(*B);
              if (PointsAtSecret &&
                  CalleeSummary.PointeeTaintedArgIndices.insert(ArgIdx).second) {
                SummaryChanged = true;
                LLVM_DEBUG(dbgs()
                           << "  caller " << MF.getName() << " -> callee "
                           << Callee->getName() << ": arg " << ArgIdx
                           << " now pointee-tainted ("
                           << (IsFrame ? "frame object " : "our own arg ")
                           << B->Index << " holds a secret, via "
                           << printReg(PhysReg, TRI) << ")\n");
              }
            }
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

// The same walk for the other fact: is the returned register a pointer to
// secret memory at any return? (`RET implicit $x0` reads x0, so UsesPointee at
// the return is exactly "x0 is pointee-tainted".)
static bool functionReturnsPointeeTainted(MachineFunction &MF,
                                          const TaintResult &TR,
                                          TaintSummaryInfo &TSI, Module &M,
                                          AAResults *AA) {
  bool Returns = false;
  replayTaint(MF, TR, &TSI, AA,
              [&](MachineInstr &MI, const TaintFacts &F, const TaintState &) {
                if (!MI.isReturn() || !F.UsesPointee)
                  return true;
                Returns = true;
                return false;
              });
  return Returns;
}

/// Visit every function that has both a MachineFunction and a converged taint
/// result - i.e. everything the post-convergence steps (DIT summaries, reports,
/// barrier insertion) operate on.
static void forEachAnalyzed(
    Module &M, TaintMFContext Ctx,
    DenseMap<const Function *, TaintResult> &Results,
    function_ref<void(Function &, MachineFunction &, const TaintResult &,
                      AAResults *)>
        Fn) {
  for (Function &F : M) {
    if (F.isDeclaration())
      continue;
    MachineFunction *MF = Ctx.GetMF(F);
    if (!MF)
      continue;
    auto It = Results.find(&F);
    if (It == Results.end())
      continue;
    Fn(F, *MF, It->second, &Ctx.FAM.getResult<AAManager>(F));
  }
}

// Step 1 used to be "get the FunctionAnalysisManager, because the new PM keeps
// MachineFunctions there and not in MachineModuleInfo". That is still true of
// the new PM, but it is no longer this function's business: the caller supplies
// a lookup, so the legacy codegen pipeline -- where MachineFunctions DO live in
// MachineModuleInfo -- can run exactly this analysis without serializing the
// module to MIR text and back. See TaintMFContext.
void llvm::runTaintInterproc(Module &M, TaintMFContext Ctx) {
  // Step 2: Create TaintSummaryInfo - shared database of per-function summaries
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

      MachineFunction *MF = Ctx.GetMF(F);
      if (!MF) {
        LLVM_DEBUG(dbgs() << "  [skip] " << F.getName()
                          << ": no MachineFunction\n");
        continue;
      }
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
      AAResults *AA = &Ctx.FAM.getResult<AAManager>(F);
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
                 << ", pointee=" << TR.Merged.countPointeeRegs() << "), "
                 << TR.Merged.countCells() << " tainted cells";
        if (TR.Merged.UnknownMemTainted)
          *TraceOS << ", UnknownMemTainted";
        *TraceOS << "\n";
        dumpRegs("tainted_regs", TaintKind::Data);
        dumpRegs("pointee_tainted_regs", TaintKind::Pointee);
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
      bool ReturnsPointeeTainted =
          functionReturnsPointeeTainted(*MF, TR, TSI, M, AA);

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
      NewSummary.ReturnsPointeeTainted = ReturnsPointeeTainted;
      NewSummary.MemEffects = MemEffects;
      NewSummary.StackArgTainted =
          CurrentSummary.StackArgTainted || OldSummary.StackArgTainted;

      // A global written with a secret is secret for the WHOLE module, not just
      // along the call edges out of this function. Done inside the fixed point
      // rather than as a post-pass because it feeds back: marking a global
      // secret can taint a load in a sibling, which can taint a store into a
      // further global. The set only grows, so this cannot prevent convergence.
      for (const GlobalVariable *GV : MemEffects.WritesPointeeToGlobal) {
        if (TSI.addPointeeGlobal(GV)) {
          Changed = true;
          LLVM_DEBUG(dbgs() << "  module pointee-global: " << GV->getName()
                            << " (by " << F.getName() << ")\n");
        }
      }
      for (const GlobalVariable *GV : MemEffects.WritesSecretToGlobal) {
        if (TSI.addSecretGlobal(GV)) {
          Changed = true;
          LLVM_DEBUG(dbgs() << "  module-secret global: " << GV->getName()
                            << " (written by " << F.getName() << ")\n");
          if (TraceOS)
            *TraceOS << "  ** module-secret global: " << GV->getName()
                     << " **\n";
        }
      }

      if (NewSummary != OldSummary) {
        TSI.storeSummary(F, NewSummary);
        Changed = true;
        LLVM_DEBUG(dbgs() << "  " << F.getName() << ": summary changed"
                          << " (returns_tainted=" << ReturnsTainted
                          << " returns_pointee=" << ReturnsPointeeTainted << ")\n");
        if (TraceOS)
          *TraceOS << "  ** summary changed: returns_tainted=" << ReturnsTainted
                   << " **\n";
      } else {
        LLVM_DEBUG(dbgs() << "  " << F.getName() << ": unchanged\n");
      }

      // Propagate argument taint to callees.
      // If caller_simple passes a tainted X0 to identity(), this adds
      // arg 0 to identity's TaintedArgIndices in TSI.
      if (propagateArgTaintToCallees(*MF, TR, TSI, M, Ctx, AA)) {
        Changed = true;
        if (TraceOS)
          *TraceOS << "  ** propagated arg taint to callee(s) **\n";
      }
    }
  }

  LLVM_DEBUG(dbgs() << "\nTaint analysis converged after " << Iteration
                    << " iteration(s)\n");
  LLVM_DEBUG(dbgs() << "Total function summaries: " << TSI.size() << "\n");

  // Which functions EXECUTE a tainted instruction. THIS is the gate on
  // instrumentation and export - not `TR.Merged.empty()`, which every consumer
  // below used to test first. Merged is the join of the block EXIT states, so
  // a function whose only secret is consumed and its register redefined before
  // every exit read as empty and was skipped: `ldr x0, [secret]; bl consume;
  // ret`, or a seeded `f(long s) { return consume(s); }`, where `consume`
  // returns a public value and so redefines x0. The load and the
  // secret-passing call are both Needs and ran with PSTATE.DIT clear, and the
  // Scenario-B check in step 3c did not fire because it asks THIS question,
  // not that one (clang/test/CodeGen/taint-instrument-gate.c).
  //
  // Computed once: the three places that used to ask it per function each ran
  // their own replay, so an instrumented function paid three and a clean one
  // paid none; now every function pays exactly one.
  DenseMap<const Function *, bool> HasTaintedRuns;
  forEachAnalyzed(M, Ctx, Results,
                  [&](Function &F, MachineFunction &MF, const TaintResult &TR,
                      AAResults *AA) {
                    HasTaintedRuns[&F] =
                        functionHasTaintedRuns(MF, TR, &TSI, AA);
                  });

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
    forEachAnalyzed(M, Ctx, Results,
                    [&](Function &F, MachineFunction &MF, const TaintResult &TR,
                        AAResults *AA) {
                      FunctionTaintSummary S = TSI.getSummary(F);
                      S.InstrumentedForDIT = HasTaintedRuns.lookup(&F);
                      S.PreservesDIT = !S.InstrumentedForDIT;
                      TSI.storeSummary(F, S);
                    });

    // Then retract: a function preserves DIT only if every function it calls
    // does too. Iterate to a greatest fixed point.
    bool PreservesChanged = true;
    while (PreservesChanged) {
      PreservesChanged = false;
      forEachAnalyzed(M, Ctx, Results,
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
  //
  // Under the callee contract the bit is OFF. Its premise is exactly the term the
  // contract removes: a secret-passing call is no longer a Need, so nothing
  // guarantees DIT is on at such a call, and "every call site passes a secret"
  // no longer implies "entered with DIT on". Every instrumented function then
  // owns its own switches, which is the contract. PreservesDIT and the `.dit`
  // clones remain: neither depends on the caller's state.
  if (TaintInsertDIT && !ditCalleeContract()) {
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
    forEachAnalyzed(M, Ctx, Results,
                    [&](Function &, MachineFunction &, const TaintResult &,
                        AAResults *) { ++Analyzed; });
    const bool CoverageComplete = (Analyzed == Defined);
    LLVM_DEBUG(if (!CoverageComplete) dbgs()
               << "  AlwaysEnteredWithDIT disabled: " << Analyzed << "/"
               << Defined << " defined functions analyzed\n");

    SmallPtrSet<const Function *, 16> Candidates;
    forEachAnalyzed(M, Ctx, Results,
        [&](Function &F, MachineFunction &MF, const TaintResult &TR,
            AAResults *AA) {
          // An uninstrumented function emits no switches, so the bit would be
          // moot; requiring instrumentation also keeps the consumer in
          // TaintAnalysis.cpp simple.
          if (!F.hasLocalLinkage() || F.hasAddressTaken() ||
              !HasTaintedRuns.lookup(&F))
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
    forEachAnalyzed(M, Ctx, Results,
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
  //  (A) ESCAPE audit - the secret is passed to a callee the analysis cannot
  //      instrument (external declaration or indirect target). Unlike the old
  //      ISB/DSB model, this is NOT an unprotected hazard: PSTATE.DIT is
  //      inherited, so the callee runs with DIT=1 (see docs/design/dit-placement.md
  //      G3). The line is an audit record of where secrets leave the TU, not a
  //      list of unprotected sites.
  //
  //  (B) Coverage invariant - for the secret to be protected DURING the call,
  //      the call must execute with DIT=1. Under function granularity that is
  //      guaranteed (a tainted call argument makes the enclosing function have
  //      a tainted run, so it is DIT-instrumented and entry set DIT=1). We do
  //      not assume this - we verify it, so the future region work cannot
  //      silently break it. A violation is a leaked secret: report UNCOVERED
  //      and, in an assertions build, assert.
  {
    auto CallsiteOSPtr =
        openTaintReport(TaintCallsiteReportFile, "taint callsite report");
    raw_fd_ostream *CallsiteOS = CallsiteOSPtr.get();
    // APPEND, unlike the callsite report above. That one truncates per clang
    // invocation, which is why a whole-library build leaves only the last TU's
    // lines - on libsodium, an empty file while seven functions had warned on
    // stderr. This report is a build-wide worklist, so it accumulates and each
    // record names its source file.
    auto LossOSPtr =
        openTaintReport(TaintInfoLossReportFile, "taint information-loss report",
                        /*Append=*/true);
    raw_fd_ostream *LossOS = LossOSPtr.get();
    // Callee contract: secret-passing call sites whose callee this build does
    // not cover. Summarised on stderr once per TU below, since one line per
    // site would drown the tail-call warning this report exists for.
    unsigned Obligations = 0;
    StringSet<> ObligationCallees;
    unsigned IndirectObligations = 0;
    // Ownership. With -taint-owned-symbols, an unseen callee that this build
    // does not define is external code the developer does not own: filed as
    // `external-call`, out of scope for the seed loop, counted separately.
    // Taint still propagates through the call exactly as before.
    StringSet<> OwnedSymbols;
    bool HaveOwned = false;
    if (!TaintOwnedSymbolsFile.empty()) {
      auto Buf = MemoryBuffer::getFile(TaintOwnedSymbolsFile);
      if (!Buf) {
        errs() << "taint: cannot read owned-symbols file "
               << TaintOwnedSymbolsFile << ": " << Buf.getError().message()
               << "\n";
      } else {
        HaveOwned = true;
        for (line_iterator LI(**Buf, /*SkipBlanks=*/true, '#'); !LI.is_at_eof();
             ++LI)
          OwnedSymbols.insert(LI->trim());
      }
    }
    unsigned ExternalSites = 0;
    StringSet<> ExternalCallees;
    forEachAnalyzed(M, Ctx, Results,
        [&](Function &F, MachineFunction &MF, const TaintResult &TR,
            AAResults *AA) {
          const bool FnInstrumented = HasTaintedRuns.lookup(&F);
          const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
          replayTaint(
              MF, TR, &TSI, AA, /*Post=*/{},
              /*Pre=*/[&](MachineInstr &MI, const TaintState &State) {
                // Fix B: only a secret genuinely *passed* to the callee matters
                // - a secret merely live/clobbered across the call is not
                // something an ABI-compliant callee can read. Read the state
                // ENTERING the call (the Pre hook): arguments are set up before
                // the call, and the call then clears/sets result registers, so
                // the leaving state would misread a tainted *return* value in x0
                // as a passed argument. (propagateArgTaintToCallees uses Pre for
                // the same reason.)
                CallArgTaint Arg = taintedCallArguments(MI, State, TRI);

                // (M) MEMORY under-taint. Arriving here with no tainted
                // argument does NOT mean nothing was passed. The analysis runs
                // post-prologepilog, where a local's address is a bare
                // `$sp + imm` with no FrameIndex and no memory operand, so
                // `f(&local_secret)` transfers no register taint at all and the
                // callee is analysed clean. Register taint and memory-cell
                // taint are two universes joined by exactly one bridge -
                // pointee taint seeded on a pointer ARGUMENT - and taking the
                // address of a local is not on that bridge.
                //
                // This is the documented open gap at the KNOWN GAP comment in
                // propagateArgTaintToCallees. It is reported here rather than
                // fixed because the whole-frame fallback that used to bridge it
                // (`-taint-frame-addr-args`) made nearly every call site look
                // secret-passing, which stopped the mod-set gate firing:
                // +45.32% against +0.66% with the gate alone. See
                // docs/design/frame-addr-fallback.md.
                //
                // Until then the loss must at least be VISIBLE. Every other
                // record in this report is an over-approximation - the callee
                // inherits DIT and runs protected - so a reader who sees a
                // clean report reasonably concludes coverage is complete. This
                // one is the opposite direction: the callee may run with the
                // mode clear and nothing else will say so. Measured instance:
                // libsodium's argon2id, where the password reaches
                // `argon2_ctx` inside a stack-allocated `argon2_context` and
                // the entire hashing kernel - `argon2_initialize`,
                // `argon2_fill_memory_blocks`, `argon2_fill_segment_ref` -
                // carries zero switches and appears nowhere in any report.
                // For EVERY call, not only one that passes no secret: a call can hand
                // one secret in a register and another by frame address
                // (`ge25519_p3_tobytes(sig, &R)`), and the second was invisible while
                // this fired only for secret-free calls - libsodium's seed loop
                // converged with fe25519_invert uncovered, 42,312 ops per two
                // signatures, for exactly that reason. The per-register check below
                // already skips what the normal path transferred.
                if (MI.isCall() && LossOS && frameMayHoldSecret(State)) {
                  const Function *MemCallee = findCalledFunction(M, MI);
                  for (const MachineOperand &MO : MI.uses()) {
                    if (!MO.isReg() || MO.isDef() || !MO.getReg().isValid() ||
                        !MO.getReg().isPhysical())
                      continue;
                    if (TRI->getEncodingValue(MO.getReg()) > 7)
                      continue; // not an argument-passing register
                    if (State.isPointeeTainted(MO.getReg()) ||
                        State.isTainted(MO.getReg()))
                      continue; // already transferred by the normal path
                    if (!argIsFrameAddress(MI, MO.getReg(), TRI))
                      continue;
                    // When provenance names the frame object and its cells hold nothing,
                    // and no blunt clobber could have filled it behind the analysis's
                    // back, this argument is an output buffer, not the secret: skip it.
                    // Otherwise list it - every such argument, one record each, because
                    // the first one is as likely to be the output state (`&rng`) as the
                    // staged key (`&keydata`), and the repair line must name the right one.
                    if (auto Base = State.getPointerBase(MO.getReg()))
                      if (Base->K == TaintObject::Frame &&
                          !State.objectHoldsSecret(TaintObject::frame(Base->Index)) &&
                          !State.isExternalMemClobbered() && !State.UnknownMemTainted)
                        continue;
                    const unsigned ArgIdx = TRI->getEncodingValue(MO.getReg());
                    // Already seeded on this argument - in its own TU (the annotator's
                    // stamp on the declaration here) or in this one - so the callee
                    // covers itself and there is nothing to propose. Without this the
                    // seed loop never converges: the record re-proposes every frame-
                    // address seed it already has (libsodium stalled at 36 lines).
                    auto SeededOnArg = [&](unsigned Idx) {
                      if (!MemCallee)
                        return false;
                      if (!MemCallee->isDeclaration())
                        return Idx < MemCallee->arg_size() &&
                               (MemCallee->getArg(Idx)->hasAttribute("tainted") ||
                                MemCallee->getArg(Idx)->hasAttribute("tainted-pointee"));
                      if (!MemCallee->hasFnAttribute("taint-seeded-elsewhere"))
                        return false;
                      StringRef V = MemCallee->getFnAttribute("taint-seeded-elsewhere")
                                        .getValueAsString();
                      StringRef D, P;
                      std::tie(D, P) = V.split(',');
                      unsigned MD = 0, MP = 0;
                      D.getAsInteger(10, MD);
                      P.getAsInteger(10, MP);
                      return (((MD | MP) >> Idx) & 1u) != 0;
                    };
                    if (SeededOnArg(ArgIdx))
                      continue;
                    // An argument register is not always a parameter: an aggregate
                    // passed by value spans two registers, so x2 may be the tail of
                    // parameter 1. A seed line naming a parameter the callee does not
                    // have is fatal to the next build (argon2_fill_segment_ref's
                    // position struct proposed `,2` for a two-parameter function), so
                    // only propose an index the callee has; otherwise name the register
                    // and leave the parameter to the reader.
                    const bool IdxIsParam = !MemCallee || ArgIdx < MemCallee->arg_size();
                    const std::string MemRepair =
                        IdxIsParam
                            ? (Twine("seed the callee on the argument that receives the "
                                     "frame address, i.e. `") +
                               (MemCallee ? MemCallee->getName() : StringRef("<callee>")) +
                               "," + Twine(ArgIdx) +
                               ",pointee`, or re-enable per-object frame provenance "
                               "(docs/design/p1b-frame-provenance.md)")
                                  .str()
                            : (Twine("register x") + Twine(ArgIdx) +
                               " carries the frame address but maps to no parameter of its "
                               "own (part of an aggregate passed by value); seed the parameter "
                               "it belongs to, or re-enable per-object frame provenance")
                                  .str();
                    reportInfoLoss(
                        LossOS, TaintLossSeverity::Unsound, "memory", F,
                        MemCallee ? MemCallee->getName() : StringRef("<indirect>"),
                        MI.getDebugLoc(),
                        (Twine("a frame address is passed as argument ") + Twine(ArgIdx) +
                         " while this frame holds a secret, but the pointer register "
                         "carries no pointee taint, so NOTHING is transferred and the "
                         "callee is analysed clean")
                            .str(),
                        "if the callee reads the secret through that pointer it "
                        "runs with PSTATE.DIT clear and no other record will "
                        "say so - this is an UNDER-approximation, unlike every "
                        "other record in this report",
                        MemRepair);
                    // (no break: one record per frame-address argument)
                  }
                }

                if (!MI.isCall() || !Arg.any())
                  return true;

                // (B) The enclosing function must be DIT-instrumented, else the
                // secret executes through the call with DIT off.
                // Inherit contract only: under the callee contract the call is not a
                // Need and an otherwise clean caller is legitimately uninstrumented -
                // the callee covers itself, or is an obligation below.
                if (TaintInsertDIT && !FnInstrumented && !ditCalleeContract()) {
                  errs() << "taint: UNCOVERED secret-passing call in "
                         << F.getName()
                         << " but the function is not DIT-instrumented "
                            "(Scenario-B invariant violated)\n";
                  assert(false && "secret-passing call outside DIT coverage");
                }

                // (A) ESCAPE audit - only callees we cannot instrument.
                const Function *Callee = findCalledFunction(M, MI);
                // An in-TU callee is reachable by the analysis - nothing lost.
                // The two writers below are gated SEPARATELY: this test used to
                // also bail when the callsite report was unrequested, which
                // silenced the information-loss record too and made the loudest
                // failure on libsodium depend on an unrelated flag.
                if (Callee && !Callee->isDeclaration())
                  return true;
                // A libcall for an intrinsic (memcpy, memset) carries no Function but a
                // perfectly good symbol: name it, and give it a seed line, rather than
                // filing it as indirect.
                const StringRef CalleeName =
                    Callee ? Callee->getName() : getCalleeSymbolName(MI);
                const bool Named = !CalleeName.empty();

                if (CallsiteOS) {
                  *CallsiteOS << "ESCAPE "
                              << (Named ? "external" : "indirect") << " callee="
                              << (Named ? CalleeName : StringRef("<indirect>"))
                              << " caller=" << F.getName()
                              << " bb=" << MI.getParent()->getNumber();
                  if (const DebugLoc &DL = MI.getDebugLoc())
                    *CallsiteOS << " line=" << DL.getLine();
                  if (Arg.Data)
                    *CallsiteOS << " tainted-args";
                  if (Arg.Pointee)
                    *CallsiteOS << " pointee-tainted-args";
                  *CallsiteOS << (ditCalleeContract()
                                      ? (Named && HaveOwned &&
                                                 !OwnedSymbols.contains(CalleeName)
                                             ? " (external: out of scope)\n"
                                         : isLibcMover(Callee, MI)
                                             ? " (UNCOVERED: libc mover, link a hardened one)\n"
                                             : " (UNCOVERED: callee contract)\n")
                                      : " (covered by inherited DIT)\n");
                }

                // Same site, stated as a consequence with a repair. The seed
                // index comes from the argument register that actually carried
                // the secret, so the line can be pasted straight into the
                // taint-source file; a stack-passed secret sets no bit and gets
                // no suggestion rather than a guessed one.
                // Suppress entirely when the seed file already covers EXACTLY
                // the arguments carrying taint here: the callee is placed where
                // it is defined and there is nothing for the user to do. But
                // compare argument-by-argument, not by name - a partially
                // seeded callee (key seeded, message not) is still a real gap,
                // and it is reported below listing only what is MISSING.
                unsigned SeedD = 0, SeedP = 0;
                if (Callee && Callee->hasFnAttribute("taint-seeded-elsewhere")) {
                  StringRef V =
                      Callee->getFnAttribute("taint-seeded-elsewhere").getValueAsString();
                  StringRef D, P;
                  std::tie(D, P) = V.split(',');
                  D.getAsInteger(10, SeedD);
                  P.getAsInteger(10, SeedP);
                }
                // A pointee seed also covers a by-value use of the same index:
                // both make the analysis treat that parameter as carrying a
                // secret, which is all this check is about.
                const unsigned Covered = SeedD | SeedP;
                const unsigned MissD = Arg.DataMask & ~Covered;
                const unsigned MissP = Arg.PointeeMask & ~Covered;
                if (Callee && (SeedD || SeedP) && !MissD && !MissP)
                  return true;   // fully seeded elsewhere - nothing to report


                // Emit EVERY argument that carried taint, not just the first.
                // crypto_sign passes four pointee-tainted arguments and the key
                // is the LAST of them; suggesting only the lowest index points
                // the user at the output buffer and silently omits the secret.
                std::string Repair;
                if (Named) {
                  SmallString<160> R("seed the TU that defines it:");
                  for (unsigned i = 0; i < 8; ++i) {
                    // Register i is not always parameter i: an aggregate passed by value
                    // spans two registers, and a seed naming a parameter the callee does
                    // not have is fatal to the next build (argon2_fill_segment_ref's
                    // position struct produced `,2` for a two-parameter function).
                    if (Callee && i >= Callee->arg_size())
                      continue;
                    const bool P = MissP & (1u << i);
                    const bool D = MissD & (1u << i);
                    if (!P && !D)
                      continue;
                    R += "\n                  ";
                    R += CalleeName;
                    R += ",";
                    R += Twine(i).str();
                    if (P)
                      R += ",pointee";
                  }
                  if (R.size() > 28)   // something was appended
                    Repair = std::string(R);
                }
                if (ditCalleeContract() && Named && HaveOwned &&
                    !OwnedSymbols.contains(CalleeName)) {
                  // External: not ours to seed. Say what the call does to the secret
                  // and leave the repair empty - a hardened mover or a hardened libc is
                  // the developer's call, not an obligation.
                  ++ExternalSites;
                  ExternalCallees.insert(CalleeName);
                  reportInfoLoss(
                      LossOS, TaintLossSeverity::Info, "external-call", F, CalleeName,
                      MI.getDebugLoc(),
                      "the secret is passed to code this build does not define; out of "
                      "scope for the seed loop (the taint still propagates through the "
                      "call)",
                      isLibcMover(Callee, MI)
                          ? StringRef("its loads and stores of the secret run with whatever "
                                      "mode libc finds; a hardened mover linked ahead of "
                                      "libc would cover them")
                      : isLibcAllocator(Callee, MI)
                          ? StringRef("an allocator's control flow depends on the size or "
                                      "pointer it is handed, which DIT does not cover; the "
                                      "fix is upstream")
                          : StringRef("runs unprotected unless the build that defines it "
                                      "is hardened"),
                      "");
                  return true;
                }
                if (ditCalleeContract()) {
                  // The obligation. Under this contract the caller holds nothing on the
                  // callee's behalf, so until the TU that defines the callee is seeded
                  // the callee's secret work runs with whatever DIT state it happens to
                  // find, which this build does not guarantee. The seed line is the
                  // repair; for an indirect site the pass cannot name the targets.
                  ++Obligations;
                  if (Named)
                    ObligationCallees.insert(CalleeName);
                  else
                    ++IndirectObligations;
                  reportInfoLoss(
                      LossOS, TaintLossSeverity::Uncovered,
                      Named ? "uncovered-callee" : "uncovered-indirect", F,
                      Named ? CalleeName : StringRef("<indirect>"),
                      MI.getDebugLoc(),
                      Named ? "the secret is passed to a callee this build cannot see, "
                               "and under the callee contract nothing covers it on the "
                               "callee's behalf"
                             : "the secret is passed through a function pointer the pass "
                               "cannot resolve, and under the callee contract nothing "
                               "covers the target on its behalf",
                      "the callee's secret work runs UNPROTECTED unless the build that "
                      "defines it is hardened with its own seed",
                      isLibcMover(Callee, MI)
                          ? StringRef("link a hardened mover ahead of libc: a memcpy "
                                      "built with -ftaint-harden and seeded on its "
                                      "source pointee (memset: on its value) enables "
                                      "DIT around its own loop; libc's runs with "
                                      "whatever mode it finds")
                      : isLibcAllocator(Callee, MI)
                          ? StringRef("no seed can fill this one: the secret is a "
                                      "size or a pointer handed to the allocator, "
                                      "whose control flow depends on it and which DIT "
                                      "does not cover; stop deriving the size or the "
                                      "pointer from the secret (mbedTLS: the "
                                      "leading-zero limb trim)")
                      : Named ? StringRef(Repair)
                               : StringRef("seed every function this pointer can "
                                           "reach, on the argument that receives the "
                                           "secret (grep the assignments to the "
                                           "pointer; the pass cannot name the "
                                           "targets)"));
                  return true;
                }
                reportInfoLoss(
                      LossOS, TaintLossSeverity::Moderate,
                      Named ? "cross-tu" : "indirect", F,
                      Named ? CalleeName : StringRef("<indirect>"),
                      MI.getDebugLoc(),
                      "the secret is passed to a callee this pass cannot see; DIT "
                      "is enabled so the callee inherits protection",
                      "no placement happens inside the callee - it runs entirely "
                      "protected, and any narrowing it could have done is lost",
                      Repair);
                return true;
              });
        });
    // Loud once per TU, like the tail-call warning: a build whose obligation
    // list is non-empty has secret work it does not cover, by design, and
    // silence has to mean the list is empty.
    if (TaintInsertDIT && Obligations) {
      errs() << "taint: " << sys::path::filename(M.getSourceFileName()) << ": "
             << Obligations << " secret-passing call site(s) reach "
             << ObligationCallees.size() << " callee(s)";
      if (IndirectObligations)
        errs() << " and " << IndirectObligations << " indirect target(s)";
      errs() << " this build does not cover (callee contract); "
                "-taint-info-loss-report lists them with the seed lines\n";
    }
    if (TaintInsertDIT && ExternalSites)
      errs() << "taint: " << sys::path::filename(M.getSourceFileName()) << ": "
             << ExternalSites << " secret-passing call site(s) reach "
             << ExternalCallees.size()
             << " external callee(s) this build does not define (out of "
                "scope; -taint-info-loss-report lists them as external-call)\n";
  }

  // Step 3c-2: Memory-clobber report - the *sources* of cross-function memory
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
    forEachAnalyzed(M, Ctx, Results,
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
  // assurance - surface them for audit / constant-time rewriting.
  if (auto UncoveredOSPtr =
          openTaintReport(TaintUncoveredReportFile, "taint uncovered report")) {
    raw_fd_ostream &UncoveredOS = *UncoveredOSPtr;
    forEachAnalyzed(M, Ctx, Results,
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

    forEachAnalyzed(M, Ctx, Results,
        [&](Function &F, MachineFunction &MF, const TaintResult &TR,
            AAResults *AA) {
          if (!HasTaintedRuns.lookup(&F))
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
    forEachAnalyzed(M, Ctx, Results,
                    [&](Function &F, MachineFunction &MF, const TaintResult &TR,
                        AAResults *AA) {
                      if (!HasTaintedRuns.lookup(&F))
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
}

PreservedAnalyses TaintInterprocPass::run(Module &M,
                                          ModuleAnalysisManager &MAM) {
  auto &FAM = MAM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
  // In the new PM a MachineFunction is the cached result of
  // MachineFunctionAnalysis; absent means the function was never codegen'd.
  auto GetMF = [&FAM](Function &F) -> MachineFunction * {
    auto *MFA = FAM.getCachedResult<MachineFunctionAnalysis>(F);
    return MFA ? &MFA->getMF() : nullptr;
  };
  runTaintInterproc(M, TaintMFContext{GetMF, FAM});
  return PreservedAnalyses::all();
}

bool llvm::moduleHasTaintSources(const Module &M) {
  for (const Function &F : M) {
    if (F.isDeclaration())
      continue;
    for (const Argument &Arg : F.args())
      if (Arg.hasAttribute("tainted") || Arg.hasAttribute("tainted-pointee"))
        return true;
  }
  return false;
}

namespace {
/// Runs the interprocedural taint analysis inside a legacy codegen pipeline,
/// where MachineFunctions belong to MachineModuleInfo rather than to a
/// FunctionAnalysisManager.
class TaintInterprocLegacyPass : public ModulePass {
  MachineModuleInfo &MMI;
  FunctionAnalysisManager &FAM;

public:
  static char ID;
  TaintInterprocLegacyPass(MachineModuleInfo &MMI, FunctionAnalysisManager &FAM)
      : ModulePass(ID), MMI(MMI), FAM(FAM) {}

  bool runOnModule(Module &M) override {
    // Non-creating lookup: null means this function was never codegen'd, which
    // the analysis skips.
    auto GetMF = [this](Function &F) -> MachineFunction * {
      return MMI.getMachineFunction(F);
    };
    runTaintInterproc(M, TaintMFContext{GetMF, FAM});
    return true;
  }

  StringRef getPassName() const override {
    return "Interprocedural taint hardening";
  }
};
} // namespace

char TaintInterprocLegacyPass::ID = 0;

namespace {
/// Reserve the callee-saved-DIT carrier slot, before PrologEpilogInserter.
///
/// This runs BEFORE the taint analysis, so it cannot know which functions will
/// be instrumented. It deliberately over-provisions: every function in a module
/// that has taint sources gets a slot, and the ones that turn out not to need it
/// simply never reference theirs.
///
/// Over-provisioning is what keeps the DIT ABI off a two-pass compile. The
/// alternative is to learn the instrumented set first and recompile with it
/// known, which LLVM's register-allocation maintainers have declined for this
/// shape of problem (discourse.llvm.org/t/21516). The cost is bounded and
/// instruction-free: an unused slot is 8 bytes of frame (16 after alignment) and
/// no code at all, since nothing loads or stores it.
class TaintDITSlotReserve : public MachineFunctionPass {
public:
  static char ID;
  TaintDITSlotReserve() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override {
    return "Reserve the PSTATE.DIT carrier frame slot";
  }

  bool runOnMachineFunction(MachineFunction &MF) override {
    // Gate on the ABI flag as well as DIT insertion. Reserving a slot the
    // placement code will never touch would grow every frame in a hardened build
    // for nothing: measured on libsodium, 168 of 371 functions that end up with
    // no DIT instruction at all still pay 16 bytes of frame for their unused
    // slot, so the gate is what keeps a non-ABI hardened build free of it.
    if (!TaintInsertDIT || !TaintDITAbi)
      return false;
    if (!moduleHasTaintSources(*MF.getFunction().getParent()))
      return false;
    const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
    return TII->createTimingModeSaveSlot(MF).has_value();
  }
};
} // end anonymous namespace

char TaintDITSlotReserve::ID = 0;

MachineFunctionPass *llvm::createTaintDITSlotReservePass() {
  return new TaintDITSlotReserve();
}

ModulePass *llvm::createTaintInterprocLegacyPass(MachineModuleInfo &MMI,
                                                 FunctionAnalysisManager &FAM) {
  return new TaintInterprocLegacyPass(MMI, FAM);
}
