//===- llvm/CodeGen/TaintAnalysis.h -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_TAINTANALYSIS_H
#define LLVM_CODEGEN_TAINTANALYSIS_H

#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/CodeGen/MachinePassManager.h"

namespace llvm {

struct TaintSource {
  std::string FuncName;
  llvm::SmallSet<unsigned, 4> TaintedArgs;
};

enum class Taint: uint8_t {
  Clean = 0,
  Tainted = 1,
};

class TaintInfo {
  SparseBitVector<> TaintedVRegs;
};

class TaintAnalysis : public AnalysisInfoMixin<TaintAnalysis> {
  friend AnalysisInfoMixin<TaintAnalysis>;
  static AnalysisKey Key;

public:
  using Result = TaintInfo;
  LLVM_ABI Result run(MachineFunction &MF,
                      MachineFunctionAnalysisManager &MFAM);
private:
  llvm::StringMap<TaintSource> TaintSources;
};

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
