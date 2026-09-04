// A pointer's provenance must be INTERSECTED at every join, including a join
// against a predecessor that carries no taint. docs/design/taint-domain.md S5.
//
// `p` is `&a` on one path and `&b` on the other, and neither path holds any
// secret yet, so both predecessor states are lattice bottom. TaintState::join
// used to return early on a bottom argument BEFORE intersecting provenance, so
// `p` reached the call still named as pointing at whichever local the first
// predecessor chose. `get_secret` writes its secret through `p`; the precise
// arg-pointee application then tainted that one local, and a read of the other
// one - `b`, when the first predecessor said `&a` - came back public. The
// multiply on it ran with PSTATE.DIT clear, and `caller` carried no switch at
// all. With the intersection always performed, `p`'s base is unknown at the
// call, the write degrades to the blunt clobber, and the reload is secret.
//
// REQUIRES: aarch64-registered-target
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -O2 -S \
// RUN:     -ftaint-harden=%S/Inputs/taint-global-secret-seed.txt %s -o - \
// RUN:   | FileCheck %s

static long secret_global;
void set_secret(long s) { secret_global = s; }

// Reads the secret from the global, so its taint is not argument-sourced and
// its mod-set is applied at every call site regardless of what is passed.
__attribute__((noinline)) void get_secret(long *out) { *out = secret_global; }

// Opaque side effects keep the two arms as real blocks with a real join, so the
// address of the chosen local is materialised in each arm and copied at the
// join rather than selected.
void side1(void);
void side2(void);

// CHECK-LABEL: caller:
// CHECK:       bl {{.*}}get_secret
// CHECK:       msr{{[[:space:]]+}}#26, #1
// CHECK:       ret
long caller(int c) {
  long a = 0, b = 0;
  long *p;
  if (c) {
    p = &a;
    side1();
  } else {
    p = &b;
    side2();
  }
  get_secret(p);
  return b * 3;
}
