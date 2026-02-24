//===- TaintSummaryInfo.h - Interprocedural Taint Summaries ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines TaintSummaryInfo, which stores interprocedural taint
// analysis summaries for functions. Each summary captures which arguments
// are tainted and whether the function returns tainted values.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_TAINTSUMMARYINFO_H
#define LLVM_CODEGEN_TAINTSUMMARYINFO_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {

class Module;

/// Summary of taint behavior for a single function.
struct FunctionTaintSummary {
  /// Indices of tainted arguments (0-7 for X0-X7/W0-W7 in AArch64).
  SmallSet<unsigned, 8> TaintedArgIndices;

  /// Whether the function returns a tainted value (X0/W0).
  bool ReturnsTainted = false;

  /// Conservative flag: if true, assume all arguments taint return.
  /// Used for external functions, indirect calls, etc.
  bool IsConservative = false;

  bool operator==(const FunctionTaintSummary &Other) const {
    return TaintedArgIndices == Other.TaintedArgIndices &&
           ReturnsTainted == Other.ReturnsTainted &&
           IsConservative == Other.IsConservative;
  }

  bool operator!=(const FunctionTaintSummary &Other) const {
    return !(*this == Other);
  }
};

/// Stores taint summaries for all functions in a module.
/// This enables interprocedural taint analysis by allowing passes to
/// query the taint behavior of callees.
class TaintSummaryInfo {
  /// Map from Function to its taint summary.
  DenseMap<const Function *, FunctionTaintSummary> Summaries;

public:
  TaintSummaryInfo() = default;

  /// Store a taint summary for a function.
  void storeSummary(const Function &F, FunctionTaintSummary Summary) {
    Summaries[&F] = Summary;
  }

  /// Get the taint summary for a function.
  /// Returns an empty summary if the function has no stored summary.
  FunctionTaintSummary getSummary(const Function &F) const {
    auto It = Summaries.find(&F);
    if (It != Summaries.end())
      return It->second;
    return FunctionTaintSummary{}; // Empty summary
  }

  /// Check if a function has a stored summary.
  bool hasSummary(const Function &F) const {
    return Summaries.find(&F) != Summaries.end();
  }

  /// Clear all summaries.
  void clear() { Summaries.clear(); }

  /// Get the number of functions with summaries.
  size_t size() const { return Summaries.size(); }

  /// Check if there are any summaries stored.
  bool empty() const { return Summaries.empty(); }

  /// Required by the pass manager. Taint summaries are never invalidated
  /// because they only grow monotonically during fixed-point iteration.
  bool invalidate(Module &, const PreservedAnalyses &,
                  ModuleAnalysisManager::Invalidator &) {
    return false; // Never invalidated
  }
};

/// Analysis pass that provides TaintSummaryInfo for a module.
class TaintSummaryAnalysis : public AnalysisInfoMixin<TaintSummaryAnalysis> {
  friend AnalysisInfoMixin<TaintSummaryAnalysis>;
  static AnalysisKey Key;

public:
  using Result = TaintSummaryInfo;
  TaintSummaryInfo run(Module &M, ModuleAnalysisManager &MAM);
};

/// Printer pass for taint summaries.
class TaintSummaryPrinterPass : public PassInfoMixin<TaintSummaryPrinterPass> {
  raw_ostream &OS;

public:
  explicit TaintSummaryPrinterPass(raw_ostream &OS) : OS(OS) {}
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_TAINTSUMMARYINFO_H
