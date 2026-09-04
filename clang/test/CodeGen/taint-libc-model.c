// REQUIRES: asserts
// -debug-only= exists only in an assertions build; without this line the test
// fails outright on a Release build instead of being marked unsupported.
// REQUIRES: aarch64-registered-target
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -O2 -S -o /dev/null \
// RUN:     -ftaint-harden=%S/Inputs/taint-libc-model-seed.txt \
// RUN:     -mllvm -taint-frame-addr-args -mllvm -taint-arg-provenance \
// RUN:     -mllvm -taint-arg-pointee-args \
// RUN:     -mllvm -taint-libc-model -mllvm -debug-only=taint-analysis %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=ON
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -O2 -S -o /dev/null \
// RUN:     -ftaint-harden=%S/Inputs/taint-libc-model-seed.txt \
// RUN:     -mllvm -taint-frame-addr-args -mllvm -taint-arg-provenance \
// RUN:     -mllvm -taint-arg-pointee-args \
// RUN:     -mllvm -debug-only=taint-analysis %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=OFF

// A callee that fills a caller-visible buffer with memcpy. memcpy is an external
// declaration, so the blunt-TOP rule loses the argument number: the summary says
// "wrote a secret somewhere" instead of "wrote a secret through arg 0". That is
// safe for a caller that LOADS the buffer - TOP poisons loads - and useless for
// one that PASSES IT ON, because nothing marks the pointer.
//
// -taint-frame-addr-args is required in BOTH arms and is not what is under test:
// the memcpy SOURCE here is `&tmp`, a frame object, so without gap A the call
// does not even look like it receives a secret and there is no effect to model.
// The three gaps sit on one chain.
//
// The length must be runtime-variable. A constant-size memcpy is expanded to
// plain stores, which resolve without any model, so a constant here would make
// the test pass for the wrong reason.

// Declared rather than #included: -cc1 with a Linux triple has no sysroot, and
// the declaration is what the model matches on anyway.
void *memcpy(void *, const void *, unsigned long);

__attribute__((noinline)) void produce(unsigned long *out,
                                       const unsigned long *key,
                                       unsigned long n) {
  unsigned long tmp[4];
  for (int i = 0; i < 4; i++)
    tmp[i] = key[i] * 3;
  memcpy(out, tmp, n);
}

unsigned long via(unsigned long *buf, const unsigned long *key) {
  produce(buf, key, 32);
  return buf[0];
}

// With the model, the effect keeps its argument number and stays nameable one
// level up.
// ON: mem-effects[produce]: arg0

// Without it, the same call collapses to TOP.
// OFF: mem-effects[produce]: UNKNOWN(TOP)

// Pin the decision itself, so a regression that keeps the summary but stops
// resolving the destination still fails.
// ON: libc move memcpy: our own arg 0's pointee becomes secret
