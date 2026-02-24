//===- llvm/CodeGen/TaintAnalysis.h -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the TaintAnalysis pass which identifies tainted registers
// in MIR based on "tainted" attributes set on IR function arguments.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_TAINTANALYSIS_H
#define LLVM_CODEGEN_TAINTANALYSIS_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SparseBitVector.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/Support/CommandLine.h"

namespace llvm {

// Forward declarations
class TaintSummaryInfo;
class Module;
class TargetRegisterInfo;
class MachineInstr;
class GlobalVariable;

/// Cell keys: (base, (offset, size_in_bytes))
using StackCell = std::pair<int, std::pair<int64_t, uint64_t>>;
using GlobalCell =
    std::pair<const GlobalVariable *, std::pair<int64_t, uint64_t>>;

/// Command-line option for taint output file (shared across passes).
extern cl::opt<std::string> TaintOutputFile;

/// TaintState holds the result of taint analysis for a MachineFunction.
/// It tracks which virtual registers are considered tainted, and which
/// memory cells (stack/global at specific offset+size) are tainted.
struct TaintState {
  SparseBitVector<> TaintedRegs;
  DenseSet<StackCell> TaintedStackCells;
  DenseSet<GlobalCell> TaintedGlobalCells;
  bool UnknownMemTainted = false;

public:
  bool operator==(const TaintState &O) const {
    return TaintedRegs == O.TaintedRegs &&
           TaintedStackCells == O.TaintedStackCells &&
           TaintedGlobalCells == O.TaintedGlobalCells &&
           UnknownMemTainted == O.UnknownMemTainted;
  }

  bool operator!=(const TaintState &O) const { return !(*this == O); }

  void join(const TaintState &O) {
    TaintedRegs |= O.TaintedRegs;
    for (const auto &C : O.TaintedStackCells)
      TaintedStackCells.insert(C);
    for (const auto &C : O.TaintedGlobalCells)
      TaintedGlobalCells.insert(C);
    UnknownMemTainted |= O.UnknownMemTainted;
  }

  /// Check if a register is tainted.
  bool isTainted(Register R) const {
    return R.isValid() && TaintedRegs.test(R.id());
  }

  /// Mark a register as tainted.
  void setTainted(Register R) {
    if (R.isValid())
      TaintedRegs.set(R.id());
  }

  // Stack cell methods
  void setTaintedStackCell(int FI, int64_t Off, uint64_t Sz) {
    TaintedStackCells.insert({FI, {Off, Sz}});
  }
  void clearTaintedStackCell(int FI, int64_t Off, uint64_t Sz) {
    TaintedStackCells.erase({FI, {Off, Sz}});
  }
  bool isTaintedStackCell(int FI, int64_t Off, uint64_t Sz) const {
    return TaintedStackCells.contains({FI, {Off, Sz}});
  }
  /// Fallback: any cell for this FI tainted? O(n) scan.
  bool anyTaintedStackCellForFI(int FI) const {
    for (const auto &C : TaintedStackCells)
      if (C.first == FI)
        return true;
    return false;
  }

  // Global cell methods
  void setTaintedGlobalCell(const GlobalVariable *GV, int64_t Off,
                            uint64_t Sz) {
    TaintedGlobalCells.insert({GV, {Off, Sz}});
  }
  void clearTaintedGlobalCell(const GlobalVariable *GV, int64_t Off,
                              uint64_t Sz) {
    TaintedGlobalCells.erase({GV, {Off, Sz}});
  }
  bool isTaintedGlobalCell(const GlobalVariable *GV, int64_t Off,
                           uint64_t Sz) const {
    return TaintedGlobalCells.contains({GV, {Off, Sz}});
  }
  bool anyTaintedGlobalCellForGV(const GlobalVariable *GV) const {
    for (const auto &C : TaintedGlobalCells)
      if (C.first == GV)
        return true;
    return false;
  }

  bool emptyRegs() const { return TaintedRegs.empty(); }
  bool empty() const {
    return emptyRegs() && TaintedStackCells.empty() &&
           TaintedGlobalCells.empty() && !UnknownMemTainted;
  }

  unsigned countRegs() const { return TaintedRegs.count(); }
  unsigned countCells() const {
    return (unsigned)(TaintedStackCells.size() + TaintedGlobalCells.size());
  }
  unsigned count() const { return countRegs() + countCells(); }
};

/// TaintResult holds both the merged taint state and per-BB entry states.
/// The per-BB IN map is needed by the export pass to replay taint propagation
/// instruction-by-instruction and determine which specific instructions are
/// tainted.
struct TaintResult {
  TaintState Merged;
  DenseMap<const MachineBasicBlock *, TaintState> IN;
};

/// TaintAnalysis is a MachineFunction analysis that computes which registers
/// are tainted based on IR function argument attributes.
class TaintAnalysis : public AnalysisInfoMixin<TaintAnalysis> {
  friend AnalysisInfoMixin<TaintAnalysis>;
  static AnalysisKey Key;

public:
  using Result = TaintResult;

  /// Standard pass-manager entry point. Extracts TSI from MFAM proxy.
  LLVM_ABI Result run(MachineFunction &MF,
                      MachineFunctionAnalysisManager &MFAM);

  /// Direct entry point for interprocedural analysis.
  /// Bypasses pass manager; TSI is provided directly by the module pass.
  LLVM_ABI Result run(MachineFunction &MF, const TaintSummaryInfo *TSI);
};

/// TaintAnalysisPass is a MachineFunction pass that runs TaintAnalysis
/// and can be used to trigger the analysis in the pass pipeline.
class TaintAnalysisPass : public PassInfoMixin<TaintAnalysisPass> {
public:
  PreservedAnalyses run(MachineFunction &MF,
                        MachineFunctionAnalysisManager &MFAM);

  MachineFunctionProperties getRequiredProperties() const {
    return MachineFunctionProperties();
  }
};

//===----------------------------------------------------------------------===//
// Shared helpers used by both TaintAnalysisPass and TaintInterprocPass
//===----------------------------------------------------------------------===//

/// Propagate taint through a single machine instruction.
void propagateTaintMI(const MachineInstr &MI, TaintState &S,
                      const TargetRegisterInfo *TRI,
                      const TaintSummaryInfo *TSI = nullptr,
                      Module *M = nullptr);

/// Check if any register use of MI is tainted.
bool anyTaintedRegUse(const MachineInstr &MI, const TaintState &S);

/// Find the called function from a call instruction.
/// Returns nullptr for indirect calls.
const Function *findCalledFunction(Module &M, const MachineInstr &MI);

/// Holds buffered per-function taint statistics for sorting before output.
struct FunctionTaintStats {
  std::string Output;     // Formatted stats text
  double TaintRatio = 0.0;
};

/// Export tainted instructions for a single MachineFunction to the output
/// streams. OS receives the TSV instruction dump; SrcOS (if non-null)
/// receives the source-line summary; Stats (if non-null) receives
/// buffered per-function taint composition statistics.
void exportTaintedInstructions(MachineFunction &MF, const TaintResult &TR,
                               const TaintSummaryInfo *TSI,
                               raw_ostream &OS, raw_ostream *SrcOS,
                               FunctionTaintStats *Stats = nullptr);

} // namespace llvm

#endif // LLVM_CODEGEN_TAINTANALYSIS_H
