//===- TaintSummaryInfo.cpp - Interprocedural Taint Summaries ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the TaintSummaryInfo analysis pass, which provides
// module-level storage for interprocedural taint analysis summaries.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/TaintSummaryInfo.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "taint-summary"

// Analysis key for the new pass manager
AnalysisKey TaintSummaryAnalysis::Key;

TaintSummaryInfo TaintSummaryAnalysis::run(Module &M,
                                            ModuleAnalysisManager &MAM) {
  // Return a new TaintSummaryInfo instance
  // The actual summaries will be filled in by TaintSummaryCollector passes
  return TaintSummaryInfo();
}

PreservedAnalyses TaintSummaryPrinterPass::run(Module &M,
                                                ModuleAnalysisManager &MAM) {
  auto &TSI = MAM.getResult<TaintSummaryAnalysis>(M);

  if (TSI.empty()) {
    OS << "No taint summaries available\n";
    return PreservedAnalyses::all();
  }

  OS << "Taint Summary Information:\n";
  OS << "==========================\n\n";

  for (Function &F : M) {
    if (!TSI.hasSummary(F))
      continue;

    auto Summary = TSI.getSummary(F);
    OS << "Function: " << F.getName() << "\n";

    if (Summary.IsConservative) {
      OS << "  [CONSERVATIVE] All arguments may taint return\n";
    } else {
      OS << "  Tainted arguments: ";
      if (Summary.TaintedArgIndices.empty()) {
        OS << "none";
      } else {
        bool First = true;
        for (unsigned ArgIdx : Summary.TaintedArgIndices) {
          if (!First)
            OS << ", ";
          OS << ArgIdx;
          First = false;
        }
      }
      OS << "\n";

      OS << "  Returns tainted: " << (Summary.ReturnsTainted ? "yes" : "no")
         << "\n";
    }
    OS << "\n";
  }

  return PreservedAnalyses::all();
}
