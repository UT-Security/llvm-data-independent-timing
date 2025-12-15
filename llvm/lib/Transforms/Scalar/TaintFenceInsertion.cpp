//===- TaintFenceInsertion.cpp - Insert fences around tainted instructions ===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass inserts fence instructions before and after instructions that
// operate on tainted (secret) data to mitigate timing side-channels.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/TaintFenceInsertion.h"
#include "llvm/Analysis/TaintAnalysis.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

// Declared in TaintAnalysis.cpp
extern cl::opt<std::string> TaintOutputFile;
extern cl::opt<std::string> TaintSummaryFile;

#define DEBUG_TYPE "taint-fence-insertion"

PreservedAnalyses TaintFenceInsertionPass::run(Module &M,
                                                ModuleAnalysisManager &MAM) {
  // Get taint analysis results
  TaintInfo &TI = MAM.getResult<TaintAnalysis>(M);

  // Write sensitive lines to file if output file is specified
  if (!TaintOutputFile.empty()) {
    std::error_code EC;
    raw_fd_ostream OutFile(TaintOutputFile, EC, sys::fs::OF_Append);
    if (EC) {
      errs() << "Error opening taint output file: " << EC.message() << "\n";
    } else {
      TI.writeSensitiveLinesToFile(OutFile);
    }
  }

  // Write function taint summaries to file if specified
  if (!TaintSummaryFile.empty()) {
    std::error_code EC;
    raw_fd_ostream SummaryFile(TaintSummaryFile, EC, sys::fs::OF_None);
    if (EC) {
      errs() << "Error opening taint summary file: " << EC.message() << "\n";
    } else {
      TI.writeFunctionSummaries(SummaryFile);
    }
  }

  // Get the set of tainted values
  const auto &SensitiveInsts = TI.getSensitiveInstructions();

  if (SensitiveInsts.empty()) {
    LLVM_DEBUG(dbgs() << "TaintFenceInsertion: No tainted instructions found\n");
    return PreservedAnalyses::all();
  }

  LLVM_DEBUG(dbgs() << "TaintFenceInsertion: Found " << SensitiveInsts.size()
                    << " tainted instructions\n");

  unsigned FencesInserted = 0;

  // Collect instructions to fence (we need to collect first to avoid
  // iterator invalidation)
  SmallVector<Instruction *, 64> InstsToFence;
  for (const auto &Info : SensitiveInsts) {
    // We need a non-const pointer to insert instructions
    Instruction *I = const_cast<Instruction *>(Info.Inst);
    InstsToFence.push_back(I);
  }

  // Insert fences around each tainted instruction
  for (Instruction *I : InstsToFence) {
    // Skip PHI nodes and terminators - can't insert fences around them
    if (isa<PHINode>(I) || I->isTerminator())
      continue;

    IRBuilder<> Builder(I);

    // Insert fence BEFORE the tainted instruction
    // Using SequentiallyConsistent ordering for strongest guarantees
    Builder.CreateFence(AtomicOrdering::SequentiallyConsistent);
    FencesInserted++;

    // Insert fence AFTER the tainted instruction
    // Position builder after the instruction
    if (I->getNextNode()) {
      Builder.SetInsertPoint(I->getNextNode());
    } else {
      // Instruction is at end of block, insert before terminator
      Builder.SetInsertPoint(I->getParent()->getTerminator());
    }
    Builder.CreateFence(AtomicOrdering::SequentiallyConsistent);
    FencesInserted++;

    LLVM_DEBUG(dbgs() << "  Inserted fences around: " << *I << "\n");
  }

  LLVM_DEBUG(dbgs() << "TaintFenceInsertion: Inserted " << FencesInserted
                    << " fence instructions\n");

  // We modified the IR
  PreservedAnalyses PA;
  PA.preserveSet<CFGAnalyses>();
  return PA;
}
