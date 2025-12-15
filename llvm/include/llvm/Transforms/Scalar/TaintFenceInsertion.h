//===- TaintFenceInsertion.h - Insert fences around tainted instructions --===//
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

#ifndef LLVM_TRANSFORMS_SCALAR_TAINTFENCEINSERTION_H
#define LLVM_TRANSFORMS_SCALAR_TAINTFENCEINSERTION_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Module;

/// Pass that inserts fence instructions around tainted instructions.
///
/// This transformation pass uses taint analysis to identify instructions
/// that operate on secret data, then inserts memory fences before and
/// after those instructions to prevent timing side-channel attacks.
///
/// Usage:
///   opt -passes='taint-fence-insertion' \
///       -taint-sources-file=sources.csv \
///       input.ll -o output.ll
class TaintFenceInsertionPass : public PassInfoMixin<TaintFenceInsertionPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_TAINTFENCEINSERTION_H
