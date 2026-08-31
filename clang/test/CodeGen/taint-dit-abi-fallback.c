// Region placement can fall back to whole-function granularity (irreducible
// need-loop, or its own soundness verifier rejecting the placement). The
// fallback erases every switch and re-runs whole-function placement, which under
// the callee-saved ABI emits the carrier save AND the exit restores itself.
//
// The region path used to then do it again, because emitDITCarrierSave reports
// success for a save that already exists. The second restore was placed in the
// block the first one had split off, so its reload landed AFTER `add sp` and read
// the CALLER's frame, and the unconditional form wrote bit 24 of that garbage
// straight into PSTATE.DIT. If it happened to be 0 and the caller was mid-region,
// the caller's secret work continued unprotected - and under this ABI no caller
// re-asserts to repair it.
//
// REQUIRES: aarch64-registered-target
// RUN: %clang_cc1 -triple aarch64-apple-macosx -O1 -S -ftaint-dit-abi \
// RUN:     -ftaint-harden=%S/Inputs/dit-abi-fallback-secret.txt \
// RUN:     -mllvm -taint-dit-switch-cyc=0 %s -o - 2>&1 \
// RUN:   | FileCheck %s

// The fallback must actually be taken, or this test proves nothing.
// CHECK: fell back to function granularity

// Exactly one carrier: one read, one reload, one restore. Two of any of them is
// the bug.
// CHECK-COUNT-1: mrs x{{[0-9]+}}, {{DIT|S3_3_C4_C2_5}}
// CHECK-NOT:     mrs x{{[0-9]+}}, {{DIT|S3_3_C4_C2_5}}

typedef unsigned long u64;
extern u64 sink(u64);
extern void pubwork(unsigned);
u64 irr(u64 secret, unsigned n, int flag) {
  u64 h = 0; unsigned i = 0;
  if (flag) goto mid;
  while (i < n) { h ^= sink(secret + i);
mid:              pubwork(i); i++; }
  return h ^ secret;
}
