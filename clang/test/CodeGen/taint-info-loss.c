// The pass must SAY when it loses the secret's trail, not silently widen.
//
// REQUIRES: aarch64-registered-target
//
// Over-approximating is safe for the secret but not free: on libsodium,
// crypto_sign is a two-instruction forwarder that enables DIT and tail-calls
// crypto_sign_ed25519 in another TU. DIT is never cleared, so every instruction
// afterwards runs protected and selective placement silently becomes blanket -
// measured at 100% of the public lane. Nothing in the output said so, and the
// repair was one seed line.
//
// So a severe loss WARNS ON STDERR BY DEFAULT, with no flag:
// RUN: %clang_cc1 -triple aarch64-apple-macosx -O2 -S \
// RUN:     -ftaint-harden=%S/Inputs/dit-infoloss-secret.txt %s -o /dev/null 2>&1 \
// RUN:   | FileCheck %s --check-prefix=WARN
// WARN: taint: fwd: DIT stays SET past this call
//
// ...and the report says what was lost, what it cost, and what to annotate:
// The report APPENDS (a build is many clang invocations), so clear it first or
// a re-run of this test would check a file with two builds' records in it.
// RUN: rm -f %t.loss
// RUN: %clang_cc1 -triple aarch64-apple-macosx -O2 -S \
// RUN:     -ftaint-harden=%S/Inputs/dit-infoloss-secret.txt \
// RUN:     -mllvm -taint-info-loss-report=%t.loss %s -o /dev/null 2>&1
// RUN: FileCheck %s --check-prefix=LOSS --input-file=%t.loss
//
// The cross-TU record carries a pasteable seed line, and it must name EVERY
// argument that carried taint - suggesting only the lowest index points the user
// at an output buffer and omits the key.
// LOSS: taint-stop cross-tu  in=fwd src=taint-info-loss.c callee=sink_extern
// LOSS:   severity  moderate
// LOSS:   repair    seed the TU that defines it:
// LOSS:               sink_extern,1,pointee
//
// LOSS: taint-stop leak-tailcall  in=fwd src=taint-info-loss.c callee=sink_extern
// LOSS:   severity  SEVERE
// LOSS:   repair    rebuild this TU with -ftaint-dit-abi

// Declaration only: another TU, so the analysis cannot follow the secret in.
unsigned long sink_extern(unsigned long a, const unsigned char *secret);

// A thin forwarder, exactly libsodium's shape. -O2 makes the call a tail call,
// which is what turns a recoverable imprecision into an unbounded one.
unsigned long fwd(unsigned long a, const unsigned char *secret) {
  return sink_extern(a, secret);
}
