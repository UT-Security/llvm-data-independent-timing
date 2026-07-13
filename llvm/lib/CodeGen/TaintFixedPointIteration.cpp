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
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/CodeGen/MachineFunctionAnalysis.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TaintAnalysis.h"
#include "llvm/CodeGen/TaintSummaryInfo.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
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

  for (const auto &MBB : MF) {
    // Get the entry state for this basic block
    auto It = TR.IN.find(&MBB);
    TaintState S = (It != TR.IN.end()) ? It->second : TaintState{};

    for (const auto &MI : MBB) {
      if (!MI.isCall()) {
        // Propagate taint through this instruction to maintain accurate state.
        propagateTaintMI(MI, S, TRI, &TSI, &M, AA);
        continue;
      }

      // Find the callee
      const Function *Callee = findCalledFunction(M, MI);
      if (!Callee || Callee->isDeclaration()) {
        propagateTaintMI(MI, S, TRI, &TSI, &M, AA);
        continue;
      }

      // Get the callee's MachineFunction so we can read its liveins
      auto *CalleeMFA = FAM.getCachedResult<MachineFunctionAnalysis>(
          *const_cast<Function *>(Callee));
      if (!CalleeMFA) {
        propagateTaintMI(MI, S, TRI, &TSI, &M, AA);
        continue;
      }
      MachineFunction *CalleeMF = &CalleeMFA->getMF();

      // Walk callee's liveins and map each physical register to its argument
      // index via hardware encoding (not livein list order, which may differ).
      FunctionTaintSummary CalleeSummary = TSI.getSummary(*Callee);
      for (const auto &[PhysReg, VirtReg] : CalleeMF->getRegInfo().liveins()) {
        unsigned ArgIdx = TRI->getEncodingValue(PhysReg);
        if (S.isTainted(PhysReg)) {
          if (!CalleeSummary.TaintedArgIndices.contains(ArgIdx)) {
            CalleeSummary.TaintedArgIndices.insert(ArgIdx);
            Changed = true;
            LLVM_DEBUG(dbgs() << "  caller " << MF.getName() << " -> callee "
                              << Callee->getName() << ": arg " << ArgIdx
                              << " now tainted (via " << printReg(PhysReg, TRI)
                              << ")\n");
          }
        }
        if (S.isPointeeTainted(PhysReg)) {
          if (!CalleeSummary.PointeeTaintedArgIndices.contains(ArgIdx)) {
            CalleeSummary.PointeeTaintedArgIndices.insert(ArgIdx);
            Changed = true;
            LLVM_DEBUG(dbgs() << "  caller " << MF.getName() << " -> callee "
                              << Callee->getName() << ": arg " << ArgIdx
                              << " now pointee-tainted (via "
                              << printReg(PhysReg, TRI) << ")\n");
          }
        }
      }

      if (Changed)
        TSI.storeSummary(*Callee, CalleeSummary);

      propagateTaintMI(MI, S, TRI, &TSI, &M, AA);
    }
  }
  return Changed;
}

static bool functionReturnsTainted(MachineFunction &MF, const TaintResult &TR,
                                   TaintSummaryInfo &TSI, Module &M,
                                   AAResults *AA) {
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();

  for (const auto &MBB : MF) {
    auto It = TR.IN.find(&MBB);
    TaintState S = (It != TR.IN.end()) ? It->second : TaintState{};

    for (const auto &MI : MBB) {
      if (MI.isReturn() && anyTaintedRegUse(MI, S))
        return true;
      propagateTaintMI(MI, S, TRI, &TSI, &M, AA);
    }
  }

  return false;
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
  if (!TaintOutputFile.empty()) {
    SmallString<256> TracePath(TaintOutputFile);
    std::string TraceExt = sys::path::extension(TracePath).str();
    sys::path::replace_extension(TracePath, "");
    TracePath += "_trace";
    TracePath += TraceExt;
    std::error_code EC;
    TraceOS = std::make_unique<raw_fd_ostream>(TracePath, EC, sys::fs::OF_None);
    if (EC) {
      errs() << "Error creating trace file: " << EC.message() << "\n";
      TraceOS.reset();
    }
  }

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
        *TraceOS << "\n--- " << F.getName() << " ---\n";
        *TraceOS << "  tainted_args:";
        if (CurrentSummary.TaintedArgIndices.empty())
          *TraceOS << " (none)";
        else
          for (unsigned Idx : CurrentSummary.TaintedArgIndices)
            *TraceOS << " " << Idx;
        *TraceOS << "\n";
        *TraceOS << "  pointee_tainted_args:";
        if (CurrentSummary.PointeeTaintedArgIndices.empty())
          *TraceOS << " (none)";
        else
          for (unsigned Idx : CurrentSummary.PointeeTaintedArgIndices)
            *TraceOS << " " << Idx;
        *TraceOS << "\n";

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
        *TraceOS << "  result: " << TR.Merged.countRegs() << " tainted regs "
                 << "(data=" << TR.Merged.countDataRegs()
                 << ", pointee=" << TR.Merged.countPointeeRegs()
                 << ", address=" << TR.Merged.countAddressRegs() << "), "
                 << TR.Merged.countCells() << " tainted cells";
        if (TR.Merged.UnknownMemTainted)
          *TraceOS << ", UnknownMemTainted";
        *TraceOS << "\n";
        // Print tainted register names
        *TraceOS << "  tainted_regs:";
        for (const auto &RegID : TR.Merged.TaintedRegs)
          *TraceOS << " " << printReg(Register(RegID), TRI);
        *TraceOS << "\n";
        *TraceOS << "  pointee_tainted_regs:";
        for (const auto &RegID : TR.Merged.PointeeTaintedRegs)
          *TraceOS << " " << printReg(Register(RegID), TRI);
        *TraceOS << "\n";
        *TraceOS << "  address_tainted_regs:";
        for (const auto &RegID : TR.Merged.AddressTaintedRegs)
          *TraceOS << " " << printReg(Register(RegID), TRI);
        *TraceOS << "\n";
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

      // Build new summary
      FunctionTaintSummary NewSummary;
      NewSummary.TaintedArgIndices = CurrentSummary.TaintedArgIndices;
      NewSummary.PointeeTaintedArgIndices =
          CurrentSummary.PointeeTaintedArgIndices;
      NewSummary.ReturnsTainted = ReturnsTainted;

      // Check if return-taint status changed
      FunctionTaintSummary OldSummary = TSI.getSummary(F);
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
  // conservative default (false). Used by insertTaintBarriers to elide
  // after-call DIT re-asserts; must run before barrier insertion below.
  if (TaintInsertISB && TaintBarrierMode == TaintBarrierKind::DIT) {
    for (Function &F : M) {
      if (F.isDeclaration())
        continue;
      auto *MFA = FAM.getCachedResult<MachineFunctionAnalysis>(F);
      if (!MFA)
        continue;
      bool Instrumented = false;
      auto It = Results.find(&F);
      if (It != Results.end() && !It->second.Merged.empty()) {
        AAResults *AA = &FAM.getResult<AAManager>(F);
        Instrumented = functionHasTaintedRuns(MFA->getMF(), It->second, &TSI, AA);
      }
      FunctionTaintSummary S = TSI.getSummary(F);
      S.PreservesDIT = !Instrumented;
      TSI.storeSummary(F, S);
    }

    bool PreservesChanged = true;
    while (PreservesChanged) {
      PreservesChanged = false;
      for (Function &F : M) {
        if (F.isDeclaration())
          continue;
        auto *MFA = FAM.getCachedResult<MachineFunctionAnalysis>(F);
        if (!MFA)
          continue;
        FunctionTaintSummary S = TSI.getSummary(F);
        if (!S.PreservesDIT)
          continue;
        bool Preserves = true;
        for (const auto &MBB : MFA->getMF()) {
          for (const auto &MI : MBB) {
            if (!MI.isCall())
              continue;
            const Function *Callee = findCalledFunction(M, MI);
            if (!Callee || !TSI.getSummary(*Callee).PreservesDIT) {
              Preserves = false;
              break;
            }
          }
          if (!Preserves)
            break;
        }
        if (!Preserves) {
          S.PreservesDIT = false;
          TSI.storeSummary(F, S);
          PreservesChanged = true;
        }
      }
    }
    LLVM_DEBUG({
      for (Function &F : M)
        if (!F.isDeclaration())
          dbgs() << "  PreservesDIT(" << F.getName()
                 << ") = " << TSI.getSummary(F).PreservesDIT << "\n";
    });
  }

  // Step 3c: Call-site escape report. Secrets handed to callees the analysis
  // cannot instrument (external declarations, indirect calls) are residual
  // hazards in every barrier mode: no toggle or barrier placed in the caller
  // protects the secret while the callee executes.
  if (!TaintCallsiteReportFile.empty()) {
    std::error_code EC;
    raw_fd_ostream CallsiteOS(TaintCallsiteReportFile, EC, sys::fs::OF_None);
    if (EC) {
      errs() << "Error creating taint callsite report file: " << EC.message()
             << "\n";
    } else {
      for (Function &F : M) {
        if (F.isDeclaration())
          continue;
        auto *MFA = FAM.getCachedResult<MachineFunctionAnalysis>(F);
        if (!MFA)
          continue;
        auto It = Results.find(&F);
        if (It == Results.end())
          continue;
        MachineFunction &MF = MFA->getMF();
        const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
        AAResults *AA = &FAM.getResult<AAManager>(F);
        for (const auto &MBB : MF) {
          auto InIt = It->second.IN.find(&MBB);
          TaintState S = (InIt != It->second.IN.end()) ? InIt->second
                                                       : TaintState{};
          for (const auto &MI : MBB) {
            if (MI.isCall()) {
              bool ArgTainted = anyTaintedCallArgument(MI, S);
              bool ArgPointeeTainted = anyPointeeTaintedCallArgument(MI, S);
              if (ArgTainted || ArgPointeeTainted) {
                const Function *Callee = findCalledFunction(M, MI);
                bool InstrumentableCallee = Callee && !Callee->isDeclaration();
                if (!InstrumentableCallee) {
                  CallsiteOS << "ESCAPE " << (Callee ? "external" : "indirect")
                             << " callee="
                             << (Callee ? Callee->getName() : "<indirect>")
                             << " caller=" << F.getName()
                             << " bb=" << MBB.getNumber();
                  if (const DebugLoc &DL = MI.getDebugLoc())
                    CallsiteOS << " line=" << DL.getLine();
                  if (ArgTainted)
                    CallsiteOS << " tainted-args";
                  if (ArgPointeeTainted)
                    CallsiteOS << " pointee-tainted-args";
                  CallsiteOS << "\n";
                }
              }
            }
            propagateTaintMI(MI, S, TRI, &TSI, &M, AA);
          }
        }
      }
    }
  }

  // Step 4: Export tainted instructions to file
  if (!TaintOutputFile.empty()) {
    // Truncate files first (we write all functions in one pass)
    {
      std::error_code EC;
      raw_fd_ostream OS(TaintOutputFile, EC, sys::fs::OF_None);
      if (EC)
        errs() << "Error creating taint output file: " << EC.message() << "\n";
      // File closed here (truncated to zero)
    }

    SmallString<256> SrcPath(TaintOutputFile);
    std::string Ext = sys::path::extension(SrcPath).str();
    sys::path::replace_extension(SrcPath, "");
    SrcPath += "_src";
    SrcPath += Ext;
    {
      std::error_code EC;
      raw_fd_ostream OS(SrcPath, EC, sys::fs::OF_None);
      if (EC)
        errs() << "Error creating source output file: " << EC.message() << "\n";
    }

    SmallString<256> StatsPath(TaintOutputFile);
    {
      std::string StatsExt = sys::path::extension(StatsPath).str();
      sys::path::replace_extension(StatsPath, "");
      StatsPath += "_stats";
      StatsPath += StatsExt;
    }

    // Collect per-function stats for sorting.
    SmallVector<FunctionTaintStats, 32> AllStats;

    // Now append each function's results
    for (Function &F : M) {
      if (F.isDeclaration())
        continue;
      auto *MFA = FAM.getCachedResult<MachineFunctionAnalysis>(F);
      if (!MFA)
        continue;
      MachineFunction *MF = &MFA->getMF();

      auto It = Results.find(&F);
      if (It == Results.end() || It->second.Merged.empty())
        continue;

      std::error_code EC;
      raw_fd_ostream OS(TaintOutputFile, EC, sys::fs::OF_Append);
      if (EC) {
        errs() << "Error opening taint output file: " << EC.message() << "\n";
        continue;
      }

      std::error_code SrcEC;
      raw_fd_ostream SrcOS(SrcPath, SrcEC, sys::fs::OF_Append);
      raw_ostream *SrcPtr = SrcEC ? nullptr : &SrcOS;

      FunctionTaintStats Stats;
      AAResults *AA = &FAM.getResult<AAManager>(F);
      exportTaintedInstructions(*MF, It->second, &TSI, OS, SrcPtr, &Stats, AA);
      if (!Stats.Output.empty())
        AllStats.push_back(std::move(Stats));
    }

    // Sort by taint ratio, highest first.
    llvm::sort(AllStats,
               [](const FunctionTaintStats &A, const FunctionTaintStats &B) {
                 return A.TaintRatio > B.TaintRatio;
               });

    // Write sorted stats to file.
    {
      std::error_code EC;
      raw_fd_ostream StatsOS(StatsPath, EC, sys::fs::OF_None);
      if (EC)
        errs() << "Error creating stats output file: " << EC.message() << "\n";
      else
        for (const auto &S : AllStats)
          StatsOS << S.Output;
    }

    LLVM_DEBUG(dbgs() << "Exported tainted instructions to " << TaintOutputFile
                      << "\n");
  }

  std::unique_ptr<raw_fd_ostream> RegionsOS;
  if (!TaintRegionsOutputFile.empty()) {
    std::error_code EC;
    RegionsOS = std::make_unique<raw_fd_ostream>(TaintRegionsOutputFile, EC,
                                                 sys::fs::OF_None);
    if (EC) {
      errs() << "Error creating taint regions output file: " << EC.message()
             << "\n";
      RegionsOS.reset();
    }
  }

  std::unique_ptr<raw_fd_ostream> SourceRegionsOS;
  if (!TaintSourceRegionsOutputFile.empty()) {
    std::error_code EC;
    SourceRegionsOS = std::make_unique<raw_fd_ostream>(
        TaintSourceRegionsOutputFile, EC, sys::fs::OF_None);
    if (EC) {
      errs() << "Error creating taint source regions output file: "
             << EC.message() << "\n";
      SourceRegionsOS.reset();
    }
  }

  unsigned BarriersInserted = 0;
  unsigned RegionsReported = 0;
  unsigned SourceRegionsReported = 0;
  if (TaintInsertISB || RegionsOS || SourceRegionsOS) {
    for (Function &F : M) {
      if (F.isDeclaration())
        continue;
      auto *MFA = FAM.getCachedResult<MachineFunctionAnalysis>(F);
      if (!MFA)
        continue;

      auto It = Results.find(&F);
      if (It == Results.end() || It->second.Merged.empty())
        continue;

      if (TaintInsertISB) {
        AAResults *AA = &FAM.getResult<AAManager>(F);
        BarriersInserted += insertTaintBarriers(MFA->getMF(), It->second, &TSI,
                                                RegionsOS.get(), AA);
      } else if (RegionsOS) {
        AAResults *AA = &FAM.getResult<AAManager>(F);
        RegionsReported += exportTaintBarrierRegions(MFA->getMF(), It->second,
                                                     &TSI, *RegionsOS, AA);
      }

      if (SourceRegionsOS) {
        AAResults *AA = &FAM.getResult<AAManager>(F);
        SourceRegionsReported += exportTaintSourceRegions(
            MFA->getMF(), It->second, &TSI, *SourceRegionsOS, AA);
      }
    }

    if (TaintInsertISB) {
      LLVM_DEBUG(dbgs() << "Inserted ISB barriers around " << BarriersInserted
                        << " tainted instruction(s)\n");
      if (TraceOS)
        *TraceOS << "Inserted ISB barriers around " << BarriersInserted
                 << " tainted instruction(s)\n";
    } else if (TraceOS && RegionsOS) {
      *TraceOS << "Reported barrier-protected regions covering "
               << RegionsReported << " tainted instruction(s)\n";
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
