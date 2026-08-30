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

  /// Mark seeded functions noinline and stop, without stamping the taint
  /// attributes.
  ///
  /// The seed is an attribute on a PARAMETER, so it lives exactly as long as
  /// its function. Stamping happens late (OptimizerLast) so the attributes
  /// reach MIR lowering with argument numbering the middle-end can no longer
  /// disturb -- but by then the inliner may already have folded the function
  /// away, taking the only record that a secret enters there with it. The pass
  /// therefore also runs at PipelineStart in this mode, purely to keep seeded
  /// functions alive long enough to be stamped.
  bool PreserveFunctionsOnly = false;

  TaintSourceAnnotatorPass() = default;
  explicit TaintSourceAnnotatorPass(std::string Path,
                                    bool PreserveFunctionsOnly = false)
      : TaintSourcesPath(std::move(Path)),
        PreserveFunctionsOnly(PreserveFunctionsOnly) {}

  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
  // static bool isRequired() { return true; }
};

} // namespace llvm

#endif /* LLVM_TRANSFORMS_INSTRUMENTATION_TAINTSOURCEANNOTATOR_H */
