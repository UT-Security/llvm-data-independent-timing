//===- TaintAnalysis.cpp - Taint Analysis Pass in Backend ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/TaintAnalysis.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/LineIterator.h"
#include "llvm/Support/MemoryBuffer.h"

using namespace llvm;

#define DEBUG_TYPE "taint-analysis"

AnalysisKey TaintAnalysis::Key;


TaintInfo TaintAnalysis::run(MachineFunction &MF,
                             MachineFunctionAnalysisManager &MFAM) {
  LLVM_DEBUG(dbgs() << "TaintAnalysis run on: " << MF.getName() << "\n");


  return TaintInfo {};
}

PreservedAnalyses TaintAnalysisPass::run(MachineFunction &MF,
                                         MachineFunctionAnalysisManager &MFAM) {
  auto PA = getMachineFunctionPassPreservedAnalyses();

  auto TI = MFAM.getResult<TaintAnalysis>(MF);

  return PA;
}
