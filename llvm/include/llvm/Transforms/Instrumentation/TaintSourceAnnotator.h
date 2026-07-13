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

#include <string>

namespace llvm {
class Function;
class FunctionPass;
class Module;

struct TaintSourceAnnotatorPass : public PassInfoMixin<TaintSourceAnnotatorPass> {
  /// Explicit taint-source file path. When empty, the pass falls back to the
  /// -taint-src command-line option. This lets callers (e.g. clang's
  /// -ftaint-harden) drive the pass without setting global cl::opt state.
  std::string TaintSourcesPath;

  TaintSourceAnnotatorPass() = default;
  explicit TaintSourceAnnotatorPass(std::string Path)
      : TaintSourcesPath(std::move(Path)) {}

  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
  // static bool isRequired() { return true; }
};

} // namespace llvm

#endif /* LLVM_TRANSFORMS_INSTRUMENTATION_TAINTSOURCEANNOTATOR_H */
