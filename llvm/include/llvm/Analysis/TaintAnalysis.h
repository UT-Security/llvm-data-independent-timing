//===- TaintAnalysis.h - Interprocedural Taint Analysis --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines an interprocedural taint analysis based on the BAP
// interval checker approach.
//
// TAINT LATTICE:
//   Notaint < Taint
//   - join(a, b): Taint if EITHER is tainted (least upper bound)
//   - meet(a, b): Taint only if BOTH are tainted (greatest lower bound)
//
// PROPAGATION RULES:
//   - Binary ops: join taint of both operands
//   - Unary ops/casts: preserve taint
//   - PHI nodes: join taint of all incoming values
//   - Select: join condition + both values
//   - GEP: join base pointer + all indices
//   - Load: taint from pointer operand (conservative)
//   - Call: if any tainted arg, return may be tainted
//
// SENSITIVE DETECTION:
//   - Branch on tainted condition -> timing leak
//   - Load/Store with tainted address -> cache timing leak
//   - Variable-time ops (div/rem) on tainted data
//
// CSV Format: FunctionName,ArgumentIndex
// Example: read,0 means argument 0 of function 'read' is a taint source
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_TAINTANALYSIS_H
#define LLVM_ANALYSIS_TAINTANALYSIS_H

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include <memory>

namespace llvm {

class Argument;
class CallGraph;
class CallInst;
class Function;
class Instruction;
class Module;
class Value;

//===----------------------------------------------------------------------===//
// TaintLattice - Two-value lattice: Notaint < Taint
//===----------------------------------------------------------------------===//

/// Simple taint lattice following BAP's checker_taint.ml
struct TaintLattice {
  enum Value { Notaint = 0, Taint = 1 };

  /// Bottom of lattice (not tainted)
  static Value bot() { return Notaint; }

  /// Top of lattice (tainted)
  static Value top() { return Taint; }

  /// Least upper bound - tainted if EITHER is tainted
  static Value join(Value a, Value b) {
    return (a == Taint || b == Taint) ? Taint : Notaint;
  }

  /// Greatest lower bound - tainted only if BOTH are tainted
  static Value meet(Value a, Value b) {
    return (a == Taint && b == Taint) ? Taint : Notaint;
  }

  /// Binary operations propagate taint via join
  static Value binop(Value a, Value b) { return join(a, b); }

  /// Unary operations preserve taint
  static Value unop(Value a) { return a; }

  /// Constants are never tainted
  static Value constant() { return Notaint; }
};

//===----------------------------------------------------------------------===//
// SensitiveReason - Why an instruction is flagged as sensitive
//===----------------------------------------------------------------------===//

enum class SensitiveReason {
  TaintedBranchCondition,    // Branch on tainted condition (timing leak)
  TaintedLoadAddress,        // Load from tainted address (cache timing)
  TaintedStoreAddress,       // Store to tainted address (cache timing)
  TaintedDivision,           // Division by tainted value (variable time)
  TaintedValue               // General tainted value
};

/// TaintInfo - Results of taint analysis for a module.
///
/// Uses forward dataflow with taint lattice (Notaint < Taint).
/// Detects sensitive instructions that may leak timing information.
///
/// Taint sources are specified via a CSV file containing function names
/// and argument indices.
class TaintInfo {
public:
  /// Information about a potentially leaking instruction
  struct PotentiallyLeakingInfo {
    const Value *V;                    // The value (instruction)
    const Argument *SourceArg;         // The source argument it depends on (null if cannot prove safe for other reasons)
    const Instruction *Inst;           // If V is an instruction
    unsigned LineNumber;               // Debug line number (0 if unavailable)

    PotentiallyLeakingInfo(const Value *V, const Argument *SourceArg);
  };

  /// Information about a sensitive (potentially leaking) instruction
  struct SensitiveInstInfo {
    const Instruction *Inst;
    SensitiveReason Reason;
    unsigned LineNumber;

    SensitiveInstInfo(const Instruction *I, SensitiveReason R);
  };

  /// Construct TaintInfo and perform analysis on the module.
  /// Reads taint sources from the CSV file specified via -taint-sources-file.
  TaintInfo(Module &M, CallGraph &CG);

  /// Query if a value is tainted.
  bool isTainted(const Value *V) const;

  /// Get all potentially leaking instructions with their information.
  const SmallVector<PotentiallyLeakingInfo, 64> &getPotentiallyLeakingInfo() const {
    return PotentiallyLeakingInsts;
  }

  /// Get all identified taint source arguments.
  const SmallVector<const Argument *, 16> &getSourceArguments() const {
    return SourceArguments;
  }

  /// Get all sensitive instructions detected.
  const SmallVector<SensitiveInstInfo, 32> &getSensitiveInstructions() const {
    return SensitiveInsts;
  }

  /// Get taint value for a Value (using lattice)
  TaintLattice::Value getTaint(const llvm::Value *V) const;

  //===--------------------------------------------------------------------===//
  // Function Summary API (for MIR pass)
  //===--------------------------------------------------------------------===//

  /// Check if a specific argument of a function is tainted.
  /// Returns true if the argument receives tainted data from any call site.
  bool isArgTainted(const Function *F, unsigned ArgIdx) const;

  /// Check if a function's return value carries taint.
  bool doesReturnTaint(const Function *F) const;

  /// Get the set of tainted argument indices for a function.
  /// Returns a BitVector where bit i is set if argument i is tainted.
  BitVector getTaintedArgs(const Function *F) const;

  /// Get list of all functions that have any tainted arguments.
  SmallVector<const Function *, 16> getFunctionsWithTaintedArgs() const;

  //===--------------------------------------------------------------------===//
  // Output Methods
  //===--------------------------------------------------------------------===//

  /// Print analysis results (list of potentially leaking instructions).
  void print(raw_ostream &OS) const;

  /// Write sensitive source lines to a file (compact format)
  void writeSensitiveLinesToFile(raw_ostream &OS) const;

  /// Write function taint summaries to output stream
  void writeFunctionSummaries(raw_ostream &OS) const;

private:
  /// Load taint sources from CSV file.
  /// Returns true on success, false on error.
  bool loadSourcesFromCSV(StringRef FilePath);

  /// Run the taint analysis algorithm.
  void runAnalysis();

  /// Identify taint source arguments based on CSV configuration.
  void identifySources();

  /// Propagate taint from a value to its users.
  void propagateTaint(const Value *V, const Argument *SourceArg,
                      SmallVectorImpl<const Value *> &Worklist);

  /// Handle taint propagation through function calls.
  void handleCall(const CallInst *CI, const Argument *SourceArg,
                  SmallVectorImpl<const Value *> &Worklist);

  /// Compute taint for an instruction based on its operands (BAP-style)
  TaintLattice::Value computeInstructionTaint(const Instruction *I) const;

  /// Check if instruction is sensitive and record it
  void checkSensitiveInstruction(const Instruction *I);

  Module &M;
  CallGraph &CG;

  /// Map from function name to set of tainted argument indices (from CSV)
  StringMap<SmallVector<unsigned, 4>> FunctionArgSources;

  /// Set of all tainted values (for deduplication across secrets)
  DenseSet<const Value *> TaintedValues;

  /// Map from tainted value to its source argument
  DenseMap<const Value *, const Argument *> ValueToSource;

  /// Per-function tainted instructions: Function -> Set of tainted instructions
  DenseMap<const Function *, DenseSet<const Instruction *>> FunctionTaintedInsts;

  /// Instructions that we cannot prove are safe (potentially leaking)
  SmallVector<PotentiallyLeakingInfo, 64> PotentiallyLeakingInsts;

  /// Sensitive instructions that may leak timing information
  SmallVector<SensitiveInstInfo, 32> SensitiveInsts;

  /// Identified taint source arguments
  SmallVector<const Argument *, 16> SourceArguments;

  //===--------------------------------------------------------------------===//
  // Function Taint Summaries (for MIR pass consumption)
  //===--------------------------------------------------------------------===//

  /// Per-function: which arguments are tainted (computed during analysis)
  /// Key: Function*, Value: BitVector where bit i = arg i is tainted
  DenseMap<const Function *, BitVector> FunctionTaintedArgs;

  /// Per-function: does the return value carry taint?
  DenseMap<const Function *, bool> FunctionReturnsTaint;

  /// Compute and store function summaries after propagation
  void computeFunctionSummaries();
};

/// Analysis pass that computes interprocedural taint information (PRUNING).
///
/// This module-level analysis uses a conservative "pruning" approach:
/// - Assumes ALL instructions could leak sensitive data
/// - Uses forward taint analysis to prove which instructions are safe
/// - Reports instructions that cannot be proven safe
///
/// Usage:
///   opt -passes='print<taint-analysis>' -taint-sources-file=sources.csv \
///       -disable-output input.ll
class TaintAnalysis : public AnalysisInfoMixin<TaintAnalysis> {
  friend AnalysisInfoMixin<TaintAnalysis>;
  static AnalysisKey Key;

public:
  using Result = TaintInfo;

  /// Run taint analysis on a module.
  TaintInfo run(Module &M, ModuleAnalysisManager &MAM);
};

/// Printer pass for taint analysis results.
class TaintAnalysisPrinterPass : public PassInfoMixin<TaintAnalysisPrinterPass> {
  raw_ostream &OS;

public:
  explicit TaintAnalysisPrinterPass(raw_ostream &OS) : OS(OS) {}

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
};

//===----------------------------------------------------------------------===//
// TaintAnalysisWrapperPass - Legacy Pass Manager Bridge
//===----------------------------------------------------------------------===//

/// Legacy pass wrapper that holds TaintInfo results.
/// This allows MIR passes to access IR-level taint analysis.
class TaintAnalysisWrapperPass : public ImmutablePass {
  std::unique_ptr<TaintInfo> TI;

public:
  static char ID;

  TaintAnalysisWrapperPass();

  /// Get the taint analysis results.
  TaintInfo *getTaintInfo() { return TI.get(); }
  const TaintInfo *getTaintInfo() const { return TI.get(); }

  /// Set the taint analysis results (called by IR pass).
  void setTaintInfo(std::unique_ptr<TaintInfo> T) { TI = std::move(T); }

  /// Check if taint info is available.
  bool hasTaintInfo() const { return TI != nullptr; }
};

} // end namespace llvm

#endif // LLVM_ANALYSIS_TAINTANALYSIS_H
