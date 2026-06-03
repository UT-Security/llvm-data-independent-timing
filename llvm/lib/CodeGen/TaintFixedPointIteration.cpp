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
                                       FunctionAnalysisManager &FAM) {
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  bool Changed = false;

  for (const auto &MBB : MF) {
    // Get the entry state for this basic block
    auto It = TR.IN.find(&MBB);
    TaintState S = (It != TR.IN.end()) ? It->second : TaintState{};

    for (const auto &MI : MBB) {
      // Propagate taint through this instruction to maintain accurate state
      propagateTaintMI(MI, S, TRI, &TSI, &M);

      if (!MI.isCall())
        continue;

      // Find the callee
      const Function *Callee = findCalledFunction(M, MI);
      if (!Callee || Callee->isDeclaration())
        continue;

      // Get the callee's MachineFunction so we can read its liveins
      auto *CalleeMFA = FAM.getCachedResult<MachineFunctionAnalysis>(
          *const_cast<Function *>(Callee));
      if (!CalleeMFA)
        continue;
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
      }

      if (Changed)
        TSI.storeSummary(*Callee, CalleeSummary);
    }
  }
  return Changed;
}

PreservedAnalyses TaintInterprocPass::run(Module &M,
                                          ModuleAnalysisManager &MAM) {
  // Step 1: Get FunctionAnalysisManager — in the new PM, MachineFunctions
  // are stored per-function in the FAM (via MachineFunctionAnalysis),
  // NOT in MachineModuleInfo.
  auto &FAM = MAM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();

  // Step 2: Create TaintSummaryInfo — shared database of per-function summaries
  TaintSummaryInfo TSI;

  // Seed TSI from IR "tainted" attributes (set by taint-annotate pass)
  for (Function &F : M) {
    if (F.isDeclaration())
      continue;
    SmallSet<unsigned, 8> TaintedArgs;
    for (const Argument &Arg : F.args()) {
      if (Arg.hasAttribute("tainted"))
        TaintedArgs.insert(Arg.getArgNo());
    }
    if (!TaintedArgs.empty()) {
      FunctionTaintSummary Summary;
      Summary.TaintedArgIndices = TaintedArgs;
      TSI.storeSummary(F, Summary);
      LLVM_DEBUG(dbgs() << "Seed: " << F.getName() << " has "
                        << TaintedArgs.size() << " tainted arg(s)\n");
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
        }
        if (!Any)
          *TraceOS << " (none)";
        *TraceOS << "\n";
      }

      // Run intraprocedural analysis with TSI.
      // TSI provides: (1) extra tainted-arg seeds from previous iterations,
      //               (2) callee return summaries for call handling
      TaintAnalysis TA;
      TaintResult TR = TA.run(*MF, &TSI);
      Results[&F] = TR;

      // Log merged taint result
      if (TraceOS) {
        *TraceOS << "  result: " << TR.Merged.countRegs() << " tainted regs, "
                 << TR.Merged.countCells() << " tainted cells";
        if (TR.Merged.UnknownMemTainted)
          *TraceOS << ", UnknownMemTainted";
        *TraceOS << "\n";
        // Print tainted register names
        *TraceOS << "  tainted_regs:";
        for (const auto &RegID : TR.Merged.TaintedRegs)
          *TraceOS << " " << printReg(Register(RegID), TRI);
        *TraceOS << "\n";
      }

      // Propagate UnknownMemTainted to the module-level flag in TSI.
      // If any function stores tainted data to heap, all functions need
      // to know so their heap loads are correctly tainted.
      if (TR.Merged.UnknownMemTainted) {
        if (TSI.setUnknownMemTainted()) {
          Changed = true;
          LLVM_DEBUG(dbgs() << "  " << F.getName()
                            << ": set module-level UnknownMemTainted\n");
          if (TraceOS)
            *TraceOS << "  ** set module-level UnknownMemTainted **\n";
        }
      }

      // Check if return register (X0/W0) is tainted.
      // Use the callee's liveins to find the first livein (return register
      // is typically the first physical register in AArch64).
      // TODO: use calling convention info instead of hardcoded register IDs
      bool ReturnsTainted =
          TR.Merged.isTainted(1) || TR.Merged.isTainted(2); // X0 or W0

      // Build new summary
      FunctionTaintSummary NewSummary;
      NewSummary.TaintedArgIndices = CurrentSummary.TaintedArgIndices;
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
      if (propagateArgTaintToCallees(*MF, TR, TSI, M, FAM)) {
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
      exportTaintedInstructions(*MF, It->second, &TSI, OS, SrcPtr, &Stats);
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
        BarriersInserted += insertTaintBarriers(MFA->getMF(), It->second, &TSI,
                                                RegionsOS.get());
      } else if (RegionsOS) {
        RegionsReported += exportTaintBarrierRegions(MFA->getMF(), It->second,
                                                     &TSI, *RegionsOS);
      }

      if (SourceRegionsOS)
        SourceRegionsReported += exportTaintSourceRegions(
            MFA->getMF(), It->second, &TSI, *SourceRegionsOS);
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
