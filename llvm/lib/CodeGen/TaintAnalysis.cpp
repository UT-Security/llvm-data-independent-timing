//===- TaintAnalysis.cpp - Taint Analysis Pass in Backend ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the TaintAnalysis pass which identifies tainted
// registers at the MIR level based on IR function argument attributes.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/TaintAnalysis.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/Debug.h"

#include <optional>

using namespace llvm;

#define DEBUG_TYPE "taint-analysis"

AnalysisKey TaintAnalysis::Key;

static std::optional<int> getFrameIndexIfAny(const llvm::MachineInstr &MI) {
  for (const llvm::MachineOperand &MO : MI.operands()) {
    if (MO.isFI())
      return MO.getIndex();
  }
  return std::nullopt;
}

static bool anyTaintedRegUse(const llvm::MachineInstr &MI,
                             const llvm::TaintInfo &Result) {
  for (const llvm::MachineOperand &MO : MI.uses()) {
    if (MO.isReg()) {
      llvm::Register R = MO.getReg();
      if (R.isValid() && Result.isTainted(R))
        return true;
    }
  }
  return false;
}

static void taintAllRegDefs(const llvm::MachineInstr &MI, llvm::TaintInfo &Result, const TargetRegisterInfo *TRI) {
  for (const llvm::MachineOperand &MO : MI.defs()) {
    if (MO.isReg()) {
      llvm::Register R = MO.getReg();
      if (R.isValid()) {
        Result.setTainted(R);
        LLVM_DEBUG(dbgs() << "      set " << printReg(MO.getReg(), TRI)
                   << " as tainted\n");
      }
    }
  }
}

TaintInfo TaintAnalysis::run(MachineFunction &MF,
                             MachineFunctionAnalysisManager &MFAM) {
  TaintInfo Result;
  const Function &F = MF.getFunction();
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();

  LLVM_DEBUG(dbgs() << "TaintAnalysis: analyzing function " << F.getName()
                    << "\n");

  // Collect which argument indices are tainted based on IR attributes
  SmallVector<unsigned, 4> TaintedArgIndices;
  for (const Argument &Arg : F.args()) {
    if (Arg.hasAttribute("tainted")) {
      TaintedArgIndices.push_back(Arg.getArgNo());
      LLVM_DEBUG(dbgs() << "  IR arg " << Arg.getArgNo()
                        << " has 'tainted' attribute\n");
    }
  }

  if (TaintedArgIndices.empty()) {
    LLVM_DEBUG(dbgs() << "  No tainted arguments found\n");
    return Result;
  }

  // Map tainted argument indices to virtual registers via liveins.
  //
  // For simple scalar arguments, the order of liveins corresponds to argument
  // order (based on calling convention). This is a simplification that works
  // for basic integer/pointer arguments.
  unsigned LiveInIdx = 0;
  for (const auto &[PhysReg, VirtReg] : MRI.liveins()) {
    if (llvm::is_contained(TaintedArgIndices, LiveInIdx)) {
      if (VirtReg.isValid()) {
        Result.setTainted(VirtReg);
        LLVM_DEBUG(dbgs() << "  Marked virtual register "
                          << printReg(VirtReg, TRI)
                          << " as tainted (from arg " << LiveInIdx << ", phys "
                          << printReg(PhysReg, TRI) << ")\n");
      } else {
        Result.setTainted(PhysReg);
        LLVM_DEBUG(dbgs() << "  Marked physical register "
                          << printReg(PhysReg, TRI)
                          << " as tainted (from arg " << LiveInIdx << ")\n");
      }
    }
    ++LiveInIdx;
  }

  LLVM_DEBUG(dbgs() << "  Reverse Post Order Traversal: \n");
  ReversePostOrderTraversal<MachineBasicBlock *> RPOT(&MF.front());
  for (const auto &MBB: RPOT) {
    LLVM_DEBUG(dbgs() << "    " << MBB->getFullName() << "\n");
    for (const auto &MI: *MBB) {
      bool isTaintedMI = anyTaintedRegUse(MI, Result);

      if (isTaintedMI)
        taintAllRegDefs(MI, Result, TRI);

      // Stack-slot taint propagation via FrameIndex
      std::optional<int> FI = getFrameIndexIfAny(MI);
      if (!FI)
        continue;

      if (MI.mayStore()) {
        if (anyTaintedRegUse(MI, Result)) {
          Result.setTaintedFI(*FI);
          LLVM_DEBUG(dbgs() << "      taint FI#" << *FI
                     << " due to store from tainted reg(s)\n");
        }
      }

      if (MI.mayLoad())
        if (Result.isTaintedFI(*FI))
          taintAllRegDefs(MI, Result, TRI);
    }
  }

  LLVM_DEBUG(dbgs() << "  Total tainted registers: " << Result.count() << "\n");

  return Result;
}

PreservedAnalyses TaintAnalysisPass::run(MachineFunction &MF,
                                         MachineFunctionAnalysisManager &MFAM) {
  auto &TI = MFAM.getResult<TaintAnalysis>(MF);

  LLVM_DEBUG({
    if (!TI.empty()) {
      dbgs() << "TaintAnalysisPass: " << MF.getName() << " has " << TI.count()
             << " tainted register(s)\n";
    }
  });

  // Analysis pass doesn't modify the IR
  return getMachineFunctionPassPreservedAnalyses();
}
