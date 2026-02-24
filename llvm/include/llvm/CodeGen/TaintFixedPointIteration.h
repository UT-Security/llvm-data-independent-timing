//===- TaintFixedPointIteration.h - Interprocedural Taint -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Module-level pass that runs interprocedural taint analysis via fixed-point
// iteration. This is the main entry point: it orchestrates intraprocedural
// analysis, summary collection, caller-to-callee argument propagation,
// callee-to-caller return propagation, and result export.
//
// Usage:
//   llc -passes='taint-interproc' -taint-output=out.txt input.pe.mir
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_TAINTFIXEDPOINTITERATION_H
#define LLVM_CODEGEN_TAINTFIXEDPOINTITERATION_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class TaintInterprocPass : public PassInfoMixin<TaintInterprocPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_TAINTFIXEDPOINTITERATION_H
