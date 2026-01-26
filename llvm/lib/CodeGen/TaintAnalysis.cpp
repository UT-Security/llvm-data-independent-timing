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

using namespace llvm;

#define DEBUG_TYPE "taint-analysis"

PreservedAnalyses TaintAnalysisPass::run(MachineFunction &MF,
                                         MachineFunctionAnalysisManager &MFAM) {
  auto PA = getMachineFunctionPassPreservedAnalyses();

  LLVM_DEBUG(dbgs() << "TaintAnalysisPass: " << MF.getName() << "\n");

  return PA;
}
