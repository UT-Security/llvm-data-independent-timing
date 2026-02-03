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

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SparseBitVector.h"
#include "llvm/CodeGen/MachinePassManager.h"
#include "llvm/CodeGen/Register.h"

namespace llvm {

/// TaintInfo holds the result of taint analysis for a MachineFunction.
/// It tracks which virtual registers are considered tainted.
struct TaintState {
  SparseBitVector<> TaintedRegs;
  DenseSet<int> TaintedFrameIdx;

public:
  bool operator==(const TaintState &O) const {
    return TaintedRegs == O.TaintedRegs && TaintedFrameIdx == O.TaintedFrameIdx;
  }

  bool operator!=(const TaintState &O) const { return !(*this == O); }

  void join(const TaintState &O) {
    TaintedRegs |= O.TaintedRegs;

    for (const auto &FI : O.TaintedFrameIdx) {
      TaintedFrameIdx.insert(FI);
    }
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

  bool isTaintedFI(int FI) const { return TaintedFrameIdx.contains(FI); }
  void setTaintedFI(int FI) { TaintedFrameIdx.insert(FI); }

  bool emptyRegs() const { return TaintedRegs.empty(); }
  bool emptyFIs() const { return TaintedFrameIdx.empty(); }
  bool empty() const { return emptyRegs() && emptyFIs(); }

  unsigned countRegs() const { return TaintedRegs.count(); }
  unsigned countFIs() const { return (unsigned)TaintedFrameIdx.size(); }
  unsigned count() const { return countRegs() + countFIs(); }
};

/// TaintAnalysis is a MachineFunction analysis that computes which registers
/// are tainted based on IR function argument attributes.
class TaintAnalysis : public AnalysisInfoMixin<TaintAnalysis> {
  friend AnalysisInfoMixin<TaintAnalysis>;
  static AnalysisKey Key;

public:
  using Result = TaintState;
  LLVM_ABI Result run(MachineFunction &MF,
                      MachineFunctionAnalysisManager &MFAM);
};

/// TaintAnalysisPass is a MachineFunction pass that runs TaintAnalysis
/// and can be used to trigger the analysis in the pass pipeline.
class TaintAnalysisPass : public PassInfoMixin<TaintAnalysisPass> {
public:
  PreservedAnalyses run(MachineFunction &MF,
                        MachineFunctionAnalysisManager &MFAM);

  MachineFunctionProperties getRequiredProperties() const {
    return MachineFunctionProperties().setIsSSA();
  }
};

} // namespace llvm

#endif // LLVM_CODEGEN_TAINTANALYSIS_H
