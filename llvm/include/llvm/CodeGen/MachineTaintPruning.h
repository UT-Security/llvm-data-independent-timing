//===- MachineTaintPruning.h - MIR-level Taint Analysis ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines a MachineFunction pass that performs taint-based pruning
// at the machine instruction level. It uses function taint summaries from the
// IR-level TaintAnalysis pass to identify potentially leaking instructions.
//
// The pass performs:
// 1. Initialize taint from function argument summaries
// 2. Propagate taint through machine instructions (registers/stack)
// 3. Apply opcode-based pruning to identify sensitive operations
// 4. Report or mitigate instructions that may leak timing information
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINETAINTPRUNING_H
#define LLVM_CODEGEN_MACHINETAINTPRUNING_H

#include "llvm/ADT/DenseSet.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/Register.h"

namespace llvm {

class TaintInfo;

class MachineTaintPruning : public MachineFunctionPass {
public:
  static char ID;

  MachineTaintPruning();

  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  StringRef getPassName() const override { return "Machine Taint Pruning"; }

private:
  /// Initialize taint from IR-level function summaries.
  /// Marks argument-passing registers as tainted based on calling convention.
  void initializeTaintFromSummary(MachineFunction &MF);

  /// Propagate taint through machine instructions.
  /// Uses forward dataflow: if any source operand is tainted, dest is tainted.
  void propagateTaint(MachineFunction &MF);

  /// Check if an instruction could leak timing information.
  /// Applies opcode-based filtering.
  bool couldLeak(const MachineInstr &MI) const;

  /// Report a potentially leaking instruction.
  void reportLeakingInstr(const MachineInstr &MI) const;

  /// Set of tainted physical/virtual registers.
  DenseSet<Register> TaintedRegs;

  /// Set of tainted stack frame indices.
  DenseSet<int> TaintedFrameIndices;

  /// Pointer to current MachineFunction (for diagnostics).
  const MachineFunction *CurMF = nullptr;
};

/// Create a MachineTaintPruning pass.
FunctionPass *createMachineTaintPruningPass();

/// Initialize the MachineTaintPruning pass.
void initializeMachineTaintPruningPass(PassRegistry &);

} // end namespace llvm

#endif // LLVM_CODEGEN_MACHINETAINTPRUNING_H
