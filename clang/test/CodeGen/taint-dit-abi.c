// The callee-saved PSTATE.DIT ABI, end to end. docs/design/dit-abi.md
//
// REQUIRES: aarch64-registered-target

// Piece 1: -ftaint-harden implies a TU-wide tail-call disable, because a tail
// call is an exit with no epilogue and the callee could not restore DIT there.
// The per-function form is unavailable: which functions get instrumented is only
// known after a post-PEI MIR pass, long after ISel has formed the tail calls.
// RUN: %clang_cc1 -triple aarch64-apple-macosx -O2 -emit-llvm \
// RUN:     -ftaint-harden=%S/Inputs/dit-abi-secret.txt %s -o - \
// RUN:   | FileCheck %s --check-prefix=NOTAIL
// NOTAIL: "disable-tail-calls"="true"

// And it is absent without the flag, so codegen is untouched when off.
// RUN: %clang_cc1 -triple aarch64-apple-macosx -O2 -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=NOFLAG
// NOFLAG-NOT: disable-tail-calls

// Piece 3, the callee half. Today's placement clears DIT unconditionally at the
// exit and therefore has to re-assert after every call it cannot prove
// preserving. Under the ABI the callee restores what it found, so CALL SITES
// EMIT NOTHING - including indirect and cross-TU ones, which no per-callee
// analysis could ever have cleared.
//
// RUN: %clang_cc1 -triple aarch64-apple-macosx -O2 -S \
// RUN:     -ftaint-harden=%S/Inputs/dit-abi-secret.txt \
// RUN:     -mllvm -taint-dit-placement=function %s -o - \
// RUN:   | FileCheck %s --check-prefix=TODAY
//
// RUN: %clang_cc1 -triple aarch64-apple-macosx -O2 -S \
// RUN:     -ftaint-harden=%S/Inputs/dit-abi-secret.txt \
// RUN:     -mllvm -taint-dit-placement=function -mllvm -taint-dit-abi %s -o - \
// RUN:   | FileCheck %s --check-prefix=ABI

// Region placement is the shipped default and takes a DIFFERENT exit form.
// RUN: %clang_cc1 -triple aarch64-apple-macosx -O2 -S \
// RUN:     -ftaint-harden=%S/Inputs/dit-abi-secret.txt \
// RUN:     -mllvm -taint-dit-abi %s -o - \
// RUN:   | FileCheck %s --check-prefix=REGION

// REGION-LABEL: _two_calls:
// The read must precede the enable that region placement puts at the very top of
// the entry block, and the store must follow the prologue because it is a frame
// access - SP is still the caller's before then. Two constraints, two insertion
// points; a single one writes above the caller's stack pointer.
// REGION:      mrs x[[C:[0-9]+]], {{DIT|S3_3_C4_C2_5}}
// REGION-NEXT: msr {{#26|DIT}}, #1
// REGION:      sub sp, sp
// REGION:      str x[[C]], [sp
//
// No call site emits anything.
// REGION:      bl {{_?}}sink_a
// REGION-NOT:  msr
// REGION:      bl {{_?}}sink_b
//
// The exit is the UNCONDITIONAL `msr DIT, Xt`, not the guarded clear function
// placement uses. A region body can leave DIT in either state - a return inside
// an Off block is never enabled - so a restore that can only clear would return
// DIT lower than entry for a function entered with it set. Guarding an ENABLE is
// forbidden, so the unconditional write is the only correct form here.
// REGION:      ldr x[[C]], [sp
// REGION:      msr {{DIT|S3_3_C4_C2_5}}, x[[C]]
// REGION-NEXT: ret

typedef unsigned long u64;
extern u64 sink_a(u64);
extern u64 sink_b(u64);

// The assembler prints PSTATE.DIT numerically unless the subtarget has
// FeatureDIT: `#26` is the pstatefield for the immediate form, `S3_3_C4_C2_5`
// the system register for the register form. Accept either spelling so the test
// does not depend on the feature set.

// TODAY-LABEL: _two_calls:
// Entry enable, a re-assert after EACH call, and an unconditional clear at the
// exit. The clear is why the re-asserts have to exist: a callee that cleared on
// its own exit would otherwise leave this frame unprotected.
// TODAY:      msr {{#26|DIT}}, #1
// TODAY:      bl {{_?}}sink_a
// TODAY-NEXT: msr {{#26|DIT}}, #1
// TODAY:      bl {{_?}}sink_b
// TODAY-NEXT: msr {{#26|DIT}}, #1
// TODAY:      msr {{#26|DIT}}, #0

// ABI-LABEL: _two_calls:
// Read the incoming value BEFORE enabling. An earlier draft computed the
// insertion point twice; the second call stopped at the freshly inserted `mrs`
// (not a FrameSetup instruction) and put the enable first, so the function saved
// the 1 it had just written and could never restore anything but 1.
// ABI:      mrs x{{[0-9]+}}, {{DIT|S3_3_C4_C2_5}}
// ABI-NEXT: str x{{[0-9]+}}, [sp
// ABI-NEXT: msr {{#26|DIT}}, #1
//
// Neither call site emits anything: the callee restores, so the caller has
// nothing to repair. This deletion is 94.7% of the switches on a full-LTO
// Bitcoin Core build and worth -16.89% on CoinSelection.
// ABI:      bl {{_?}}sink_a
// ABI-NOT:  msr
// ABI:      bl {{_?}}sink_b
// ABI-NOT:  msr {{#26|DIT}}, #1
//
// The exit is a GUARDED clear, and the two halves sit in DIFFERENT places. The
// reload must happen while the frame is still up; the mode switch must happen
// AFTER the epilogue, because the epilogue reloads callee-saved registers that
// may still hold secrets and those reloads are Needs. Placing the switch before
// the epilogue instead is caught by the final-MIR verifier, which is how this
// was found.
// The reload's exact position among the epilogue's own reloads is the
// scheduler's business; all that matters is that it precedes the SP adjustment,
// and the guarded switch follows it.
// ABI:      ldr x{{[0-9]+}}, [sp
// (a block-label comment for the clear block sits between the branch and the
// write, so this is CHECK, not CHECK-NEXT)
// TBNZ**W**, not TBNZX: the X form hard-codes b5=1, so `TBNZX ..., 24` tests bit
// 32+24 = 56, which is always zero in an MRS DIT result. The branch would never
// be taken, the clear would always run, and a function entered with DIT ON would
// return with it OFF - stripping its caller. The asm printer shows the raw
// operand either way, so pinning the register WIDTH here is what catches it.
// ABI:      tbnz w{{[0-9]+}}, #24, [[CONT:[.A-Za-z0-9_]+]]
// ABI:      msr {{#26|DIT}}, #0
// ABI-NEXT: [[CONT]]:
// ABI-NEXT: ret
//
// Guarding a CLEAR is safe under speculation and is the sequence Apple ships in
// _timingsafe_restore_if_supported. Guarding an ENABLE would be a leak and is
// never done. The unconditional `msr DIT, Xt` alternative would also be
// invisible to the final-MIR verifier, which only recognises the immediate form.
// ABI-NOT:  msr {{DIT|S3_3_C4_C2_5}}, x
u64 two_calls(u64 secret) {
  u64 a = sink_a(secret);
  u64 b = sink_b(secret);
  return a ^ b;
}

u64 one_call(u64 secret) { return sink_a(secret); }
