//===- AArch64DITVerifier.cpp - final check on PSTATE.DIT placement -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Verifies, on the FINAL MIR, that every instruction requiring PSTATE.DIT
// actually executes with it set.
//
// The taint pass already verifies its own output, but that check runs inside
// insertTaintDITSwitches, over the MIR it has just produced, and roughly a
// dozen machine passes run afterwards. That blind spot is not hypothetical: the
// PostRA scheduler was observed hoisting `MSR DIT, #0` above a secret-dependent
// multiply, and the emit-time verifier could not see it. Fixing the reordering
// (an implicit $dit def/use, see AArch64InstrInfo::pinToTimingMode) removes that
// specific cause, but a downstream pass can still duplicate a protected
// instruction into an unprotected context, or synthesise a new one. This pass
// runs last so that any such damage is a build failure rather than a silent
// leak.
//
// It is cheap because the fix supplies the marker: an instruction that must run
// protected carries an implicit use of $dit, so there is no need to re-run the
// taint analysis here.
//
//===----------------------------------------------------------------------===//

#include "AArch64.h"
#include "AArch64InstrInfo.h"
#include "AArch64Subtarget.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "aarch64-dit-verifier"
#define PASS_NAME "AArch64 PSTATE.DIT placement verifier"

namespace {

class AArch64DITVerifier : public MachineFunctionPass {
public:
  static char ID;
  AArch64DITVerifier() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override;

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().setNoVRegs();
  }

  StringRef getPassName() const override { return PASS_NAME; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};

} // end anonymous namespace

char AArch64DITVerifier::ID = 0;

INITIALIZE_PASS(AArch64DITVerifier, DEBUG_TYPE, PASS_NAME, false, false)

FunctionPass *llvm::createAArch64DITVerifierPass() {
  return new AArch64DITVerifier();
}

/// Does MI require PSTATE.DIT? Marked by pinToTimingMode().
static bool requiresDIT(const MachineInstr &MI) {
  return MI.readsRegister(AArch64::DIT, /*TRI=*/nullptr) &&
         !MI.definesRegister(AArch64::DIT, /*TRI=*/nullptr);
}

/// The switches the taint pass emits: `MSR DIT, #imm` carrying an implicit def
/// of $dit. Returns the new state, or nullopt if MI is not such a switch.
static std::optional<bool> ditSwitchState(const MachineInstr &MI,
                                          const AArch64InstrInfo *TII) {
  if (!MI.definesRegister(AArch64::DIT, /*TRI=*/nullptr))
    return std::nullopt;
  return TII->getTimingModeSwitch(MI);
}

bool AArch64DITVerifier::runOnMachineFunction(MachineFunction &MF) {
  const auto *TII = static_cast<const AArch64InstrInfo *>(
      MF.getSubtarget().getInstrInfo());

  // Cheap early-out: nothing to check unless something was pinned.
  bool AnyPinned = false;
  for (const MachineBasicBlock &MBB : MF) {
    for (const MachineInstr &MI : MBB)
      if (requiresDIT(MI)) {
        AnyPinned = true;
        break;
      }
    if (AnyPinned)
      break;
  }
  if (!AnyPinned)
    return false;

  // A function the taint pass decided is always entered with DIT already set
  // emits no entry enable -- the caller owns the state -- so it starts on.
  const bool EntryOn = MF.getFunction().hasFnAttribute("dit-entered-on");

  // Forward 1-bit dataflow, AND-meet at joins: DIT must arrive set on EVERY
  // path. Initialise optimistically so a loop carrying DIT in from outside
  // converges to on rather than being pinned false by its backedge.
  //
  // Calls are treated as TRANSPARENT. Whether a callee clears DIT is decided by
  // the taint pass from its summaries, which also emits the re-asserts; it is
  // not something a downstream pass changes, and assuming otherwise here would
  // reject the correct elision of a re-assert after a preserving callee. This
  // pass exists to catch damage done AFTER placement, not to re-litigate it.
  DenseMap<const MachineBasicBlock *, bool> OnOut, OnIn;
  for (const MachineBasicBlock &MBB : MF)
    OnOut[&MBB] = true;

  bool Changed = true;
  while (Changed) {
    Changed = false;
    for (const MachineBasicBlock &MBB : MF) {
      bool In = MBB.pred_empty() ? EntryOn : true;
      for (const MachineBasicBlock *P : MBB.predecessors())
        In &= OnOut[P];
      OnIn[&MBB] = In;

      bool Cur = In;
      for (const MachineInstr &MI : MBB)
        if (auto S = ditSwitchState(MI, TII))
          Cur = *S;
      if (OnOut[&MBB] != Cur) {
        OnOut[&MBB] = Cur;
        Changed = true;
      }
    }
  }

  for (const MachineBasicBlock &MBB : MF) {
    bool Cur = OnIn[&MBB];
    for (const MachineInstr &MI : MBB) {
      if (requiresDIT(MI) && !Cur) {
        std::string Buf;
        raw_string_ostream OS(Buf);
        OS << "PSTATE.DIT placement is unsound in '" << MF.getName()
           << "': an instruction that must execute with DIT set reaches "
           << "bb." << MBB.getNumber() << " with it clear.\n"
           << "  " << MI << "\n"
           << "The taint pass verified its own output, so this was introduced "
              "by a later machine pass moving, duplicating or synthesising "
              "code. This is a leaked secret, not a missed optimisation.";
        report_fatal_error(StringRef(Buf), /*GenCrashDiag=*/false);
      }
      if (auto S = ditSwitchState(MI, TII))
        Cur = *S;
    }
  }
  return false;
}
