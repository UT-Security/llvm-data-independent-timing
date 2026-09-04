// Phase 2, U1 (docs/design/taint-domain.md S5): a load whose memory object the
// analysis cannot resolve reads CLEAN unless a pointee-tainted base, an
// AA-connected unknown store or a TOP bit reaches it. -taint-unknown-load-tainted
// flips that to CIO's "unknown means tainted": Data-tainted whenever the state
// holds any memory-resident secret. Here the frame holds one (`local`), and the
// load through the unrelated public pointer `p` resolves to no known object.
//
// The emitted code does NOT distinguish the two: `u1` is one block with two
// Needs already (the secret store and the call passing &local), so block
// placement covers the load incidentally either way. What changes is what the
// analysis KNOWS, visible in the precision report: need=2 -> need=5 (the load,
// the multiply after it, and the return-value move become Needs). Measured
// 2026-09-04 as byte-identical on all 108 mbedTLS objects at full seeding, so
// this control is what proves the flag is live (docs/results/
// phase2-unknown-tainted-2026-09-04.md).
//
// REQUIRES: aarch64-registered-target
// RUN: echo "u1,0" > %t.seed
// RUN: rm -f %t.off.prec %t.on.prec
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -O2 -S -o /dev/null \
// RUN:     -ftaint-harden=%t.seed -mllvm -taint-dit-precision-report=%t.off.prec %s
// RUN: FileCheck --check-prefix=OFF --input-file=%t.off.prec %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -O2 -S -o /dev/null \
// RUN:     -ftaint-harden=%t.seed -mllvm -taint-unknown-load-tainted \
// RUN:     -mllvm -taint-dit-precision-report=%t.on.prec %s
// RUN: FileCheck --check-prefix=ON --input-file=%t.on.prec %s

void opaque(unsigned long *);

__attribute__((noinline))
unsigned long u1(unsigned long secret, unsigned long *p) {
    unsigned long local = secret * 3;      /* secret into a frame cell */
    opaque(&local);                        /* keep the store live */
    return p[0] * 7;                       /* unresolved load, then a MUL */
}

// OFF: {{^}}u1 need=2 {{.*}}
// ON:  {{^}}u1 need=5 {{.*}}
