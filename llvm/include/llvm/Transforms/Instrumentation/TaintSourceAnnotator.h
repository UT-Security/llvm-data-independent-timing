//===- Transforms/Instrumentation/TaintSourceAnnotator.h - TSA Pass ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_TAINTSOURCEANNOTATOR_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_TAINTSOURCEANNOTATOR_H

#include "llvm/IR/PassManager.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
class Function;
class FunctionPass;
class Module;

struct TaintSourceAnnotatorPass : public PassInfoMixin<TaintSourceAnnotatorPass> {
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
  // static bool isRequired() { return true; }
};

} // namespace llvm

#endif /* LLVM_TRANSFORMS_INSTRUMENTATION_TAINTSOURCEANNOTATOR_H */
