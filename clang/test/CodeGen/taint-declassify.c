// REQUIRES: asserts
// -debug-only= exists only in an assertions build; without this line the test
// fails outright on a Release build instead of being marked unsupported.
// REQUIRES: aarch64-registered-target
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -O2 -S -o /dev/null \
// RUN:     -ftaint-harden=%S/Inputs/taint-declassify-seed.txt \
// RUN:     -mllvm -debug-only=taint-interproc %s 2>&1 \
// RUN:   | FileCheck %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -O2 -S -o /dev/null \
// RUN:     -ftaint-harden=%S/Inputs/taint-declassify-nosink.txt \
// RUN:     -mllvm -debug-only=taint-interproc %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=NOSINK

// Declassification, CryptoMPK's `sinktaint`. A parameter tagged `declassify`
// drops whatever taint reaches it: the caller may pass a secret-derived value,
// and the callee asserts it is public by then. Unsound by construction - that
// is what declassification IS - so what matters is that it cannot over-reach.
//
// The tag is on the PARAMETER, not the object. Object granularity is what makes
// CryptoMPK's version unsound, and libhydrogen shows why in one buffer:
// hydro_sign_prehash keeps the ephemeral SECRET at &csig[32] while csig is the
// public signature buffer, so declassifying that OBJECT would strip protection
// from the secret it temporarily holds. A parameter tag stops taint at one call
// boundary and says nothing about the storage.

__attribute__((noinline)) unsigned long publish(const unsigned long *p) {
  unsigned long a = 1;
  for (int i = 0; i < 4; i++)
    a = a * p[i] + 3;
  return a;
}

unsigned long entry(const unsigned long *key) {
  return publish(key);
}

// Seeded `entry,0,pointee` and `publish,0,declassify`: taint reaches the call
// and stops at the parameter.
// CHECK: callee publish: arg 0 is DECLASSIFIED, taint stops here

// Without the tag the same call propagates, so the test pins the tag's effect
// rather than an accident of this shape.
// NOSINK-NOT: is DECLASSIFIED
// NOSINK: callee publish: arg 0 now pointee-tainted
