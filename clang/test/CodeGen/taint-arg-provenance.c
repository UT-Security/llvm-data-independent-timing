// REQUIRES: aarch64-registered-target
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -O2 -S -o /dev/null \
// RUN:     -ftaint-harden=%S/Inputs/taint-arg-provenance-seed.txt \
// RUN:     -mllvm -taint-arg-provenance -mllvm -debug-only=taint-analysis %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=ON
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -O2 -S -o /dev/null \
// RUN:     -ftaint-harden=%S/Inputs/taint-arg-provenance-seed.txt \
// RUN:     -mllvm -debug-only=taint-analysis %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=OFF

// B1: name the object an incoming pointer argument points at, so a callee's
// arg-pointee mod-set applies to that object instead of clobbering all of the
// caller's memory. See docs/design/frame-address-gap.md.
//
// `produce` writes a secret through its out-parameter; its own summary is
// precise either way. What this pins is the CALLER: `via_argptr` passed a
// pointer that is its OWN argument, not a frame object, so before B1 there was
// no object to name and the effect collapsed to a whole-caller clobber that
// re-exported as TOP.

__attribute__((noinline)) void produce(unsigned long *out,
                                       const unsigned long *key) {
  for (int i = 0; i < 4; i++)
    out[i] = key[i] * 3;
}

__attribute__((noinline)) unsigned long consume(const unsigned long *p) {
  unsigned long a = 1;
  for (int i = 0; i < 4; i++)
    a = a * p[i] + 3;
  return a;
}

unsigned long via_argptr(unsigned long *buf, const unsigned long *key) {
  produce(buf, key);
  return consume(buf);
}

// The callee's own summary is precise in both arms - it is not what B1 changes.
// ON: mem-effects[produce]: arg0
// OFF: mem-effects[produce]: arg0

// The caller is. With B1 the effect lands on the argument it was actually
// passed and stays nameable one level up; without it the caller's whole memory
// is clobbered and the summary degrades to TOP.
// ON: mem-effects[via_argptr]: arg0
// OFF: mem-effects[via_argptr]: UNKNOWN(TOP)

// Pin the call-site decision too, so a regression that keeps the summary but
// loses the naming still fails.
// ON: writes secret through a pointer arg (P1b/B1: named
// OFF: writes secret through a pointer arg (P1a: blunt clobber, provenance unknown)
