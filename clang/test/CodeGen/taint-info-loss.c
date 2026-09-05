// The pass must SAY when it loses the secret's trail, not silently widen.
//
// REQUIRES: aarch64-registered-target
//
// Over-approximating is safe for the secret but not free: on libsodium,
// crypto_sign is a two-instruction forwarder that enables DIT and tail-calls
// crypto_sign_ed25519 in another TU. DIT is never cleared, so every instruction
// afterwards runs protected and selective placement silently becomes blanket -
// measured at 100% of the public lane. Nothing in the output said so, and the
// repair was one seed line - and, since 2026-09-01, is the shipped default
// (docs/design/dit-tailcall-gap.md).
//
// THE TAIL-CALL LEAK IS NO LONGER REACHABLE BY DEFAULT (2026-09-01). `-ftaint-harden`
// disables tail calls TU-wide, so `fwd` gets a real `bl` and an epilogue to clear DIT
// in, and there is nothing severe to report. That is the fix this report argued for;
// what the report has to keep proving is that it still FIRES when the leak is put back.
//
// Default: the cross-TU records, and no severe warning on stderr.
// RUN: rm -f %t.loss
// RUN: %clang_cc1 -triple aarch64-apple-macosx -O2 -S \
// RUN:     -ftaint-harden=%S/Inputs/dit-infoloss-secret.txt -mllvm -taint-dit-contract=inherit \
// RUN:     -mllvm -taint-info-loss-report=%t.loss %s -o /dev/null 2>&1 \
// RUN:   | FileCheck %s --check-prefix=NOWARN --allow-empty
// NOWARN-NOT: DIT stays SET past this call
//
// RUN: FileCheck %s --check-prefix=NOLEAK --input-file=%t.loss
// NOLEAK-NOT: leak-tailcall
// NOLEAK-NOT: SEVERE
//
// Put the tail call back and the severe loss returns, warning on stderr with no
// report flag needed - a silent degeneration to blanket coverage is not discoverable.
// RUN: %clang_cc1 -triple aarch64-apple-macosx -O2 -S \
// RUN:     -ftaint-harden=%S/Inputs/dit-infoloss-secret.txt -mllvm -taint-dit-contract=inherit \
// RUN:     -mllvm -taint-no-tail-calls=0 %s -o /dev/null 2>&1 \
// RUN:   | FileCheck %s --check-prefix=WARN
// WARN: taint: fwd: DIT stays SET past this call
//
// ...and the report says what was lost, what it cost, and what to annotate:
// The report APPENDS (a build is many clang invocations), so clear it first or
// a re-run of this test would check a file with two builds' records in it.
// RUN: rm -f %t.loss
// RUN: %clang_cc1 -triple aarch64-apple-macosx -O2 -S \
// RUN:     -ftaint-harden=%S/Inputs/dit-infoloss-secret.txt -mllvm -taint-dit-contract=inherit \
// RUN:     -mllvm -taint-no-tail-calls=0 \
// RUN:     -mllvm -taint-info-loss-report=%t.loss %s -o /dev/null 2>&1
// RUN: FileCheck %s --check-prefix=LOSS --input-file=%t.loss
//
// Records appear in file order: the cross-TU ones from the analysis pass, then
// the severe tail-call one from placement.
//
// The cross-TU record carries a pasteable seed line, and it must name EVERY
// argument that carried taint - suggesting only the lowest index points the user
// at an output buffer and omits the key.
// LOSS: taint-stop cross-tu  in=fwd src=taint-info-loss.c callee=sink_extern
// LOSS:   severity  moderate
// LOSS:   repair    seed the TU that defines it:
// LOSS-NEXT:               sink_extern,1,pointee
//
// A PARTIALLY seeded callee is still reported, listing only what is MISSING.
// sink_partial is seeded on argument 1; arguments 1 and 2 both carry taint here,
// so only argument 2 should be suggested.
// LOSS: taint-stop cross-tu  in=fwd3 src=taint-info-loss.c callee=sink_partial
// LOSS:   repair    seed the TU that defines it:
// LOSS-NEXT:               sink_partial,2,pointee
//
// The repair for the severe one is no longer an annotation at all - it is a build
// configuration to put back.
// LOSS: taint-stop leak-tailcall  in=fwd src=taint-info-loss.c callee=sink_extern
// LOSS:   severity  SEVERE
// LOSS:   repair    disable tail calls for this TU. Through clang that is already the default
//
// A callee the seed file ALREADY covers in full is suppressed outright. Telling
// the user to seed something they have seeded is what made the report look like
// it never converged - 9 of 41 records on libsodium were exactly that. Scanned
// over the whole file, hence its own prefix.
// RUN: FileCheck %s --check-prefix=NOSEED --input-file=%t.loss
// NOSEED-NOT: sink_seeded
// NOSEED-NOT: sink_partial,1

// Declaration only: another TU, so the analysis cannot follow the secret in.
unsigned long sink_extern(unsigned long a, const unsigned char *secret);

// A thin forwarder, exactly libsodium's shape. Plain -O2 makes the call a tail call,
// which is what turns a recoverable imprecision into an unbounded one; -ftaint-harden
// now suppresses that, so only the -taint-no-tail-calls=0 arm above sees it.
unsigned long fwd(unsigned long a, const unsigned char *secret) {
  return sink_extern(a, secret);
}

// Fully seeded callee. Not a tail call (the +1 keeps a real return), so this
// isolates the cross-TU suppression from the tail-call leak above.
unsigned long sink_seeded(unsigned long a, const unsigned char *secret);
unsigned long fwd2(unsigned long a, const unsigned char *secret) {
  return sink_seeded(a, secret) + 1;
}

// Seeded on argument 1 only, while arguments 1 AND 2 carry taint here.
unsigned long sink_partial(unsigned long a, const unsigned char *s1,
                           const unsigned char *s2);
unsigned long fwd3(unsigned long a, const unsigned char *s1,
                   const unsigned char *s2) {
  return sink_partial(a, s1, s2) + 1;
}
