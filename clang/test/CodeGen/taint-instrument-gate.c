// A function is instrumented if it EXECUTES a tainted instruction, not if the
// join of its block-exit states happens to still hold one.
// docs/design/taint-domain.md S5.
//
// `leak` loads the secret into x0 and hands it to `consume`, which returns a
// public value: the call defines x0, the analysis clears the result register,
// and nothing else in `leak` is secret. Its merged exit state was therefore
// empty, and the instrumentation loop in runTaintInterproc skipped it on
// `TR.Merged.empty()` - even though the load and the secret-passing call are
// both Needs, and `functionHasTaintedRuns` says so. The Scenario-B check did
// not fire because it asks that second question, not the first. So the secret
// crossed the call with PSTATE.DIT clear and `leak` carried no switch.
//
// REQUIRES: aarch64-registered-target
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -O2 -S \
// RUN:     -ftaint-harden=%S/Inputs/taint-global-secret-seed.txt %s -o - \
// RUN:   | FileCheck %s

static long secret_global;
void set_secret(long s) { secret_global = s; }
void abort(void);

// Takes the secret, returns a constant: the caller's x0 is public afterwards.
__attribute__((noinline)) static long consume(long v) {
  if (v == 42)
    abort();
  return 7;
}

// CHECK-LABEL: leak:
// CHECK:       msr{{[[:space:]]+}}#26, #1
// CHECK:       bl {{.*}}consume
// CHECK:       ret
long leak(void) { return consume(secret_global); }

// The same shape from a DIRECTLY SEEDED argument (`f,0` in the seed file): the
// secret arrives in x0, the call consumes it and redefines x0 with a public
// value, the merged exit state is bottom, and the entry point of the whole
// hardening was emitted without a single switch.
// CHECK-LABEL: f:
// CHECK:       msr{{[[:space:]]+}}#26, #1
// CHECK:       bl {{.*}}consume
// CHECK:       ret
long f(long s) { return consume(s); }
