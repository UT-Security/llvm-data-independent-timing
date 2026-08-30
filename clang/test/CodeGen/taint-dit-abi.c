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
// The exit restores the entry value instead of clearing. `msr DIT, Xt` writes
// back bit 24, and at this point DIT is 1, so the write is a no-op or a clear
// and never an enable - the speculation hazard of
// dit-unconditional-design.md 3.1 cannot arise for it by construction.
// ABI:      ldr x{{[0-9]+}}, [sp
// ABI:      msr {{DIT|S3_3_C4_C2_5}}, x{{[0-9]+}}
u64 two_calls(u64 secret) {
  u64 a = sink_a(secret);
  u64 b = sink_b(secret);
  return a ^ b;
}

u64 one_call(u64 secret) { return sink_a(secret); }
