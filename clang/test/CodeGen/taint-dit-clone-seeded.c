// Cross-TU DIT cloning (-taint-dit-clone-seeded). Every seeded function gets a
// `<name>.dit` twin that is entered with PSTATE.DIT already set and emits no
// switch of its own. A call made from DIT-on code is redirected to the twin -
// in this TU, and across TUs when the callee is a seeded declaration the
// -taint-owned-symbols list says this build defines, in which case the twin is
// named on the strength of the flag alone and the linker resolves it. The
// caller then needs no re-assert after the call, the callee no enable/clear:
// three switches per dynamic call, gone. Under the callee contract, where
// every seeded callee otherwise toggles for itself.
//
// REQUIRES: aarch64-registered-target
// RUN: echo "outer,0,pointee" >  %t.seed
// RUN: echo "inner,0,pointee" >> %t.seed
// RUN: echo "fwd,0,pointee"   >> %t.seed
// RUN: echo "leaf,0,pointee"  >> %t.seed
// RUN: printf 'outer\ninner\nfwd\nleaf\n' > %t.owned
//
// TU A, the caller: it only DECLARES inner.
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -O2 -S -o %t.a.s -DTU_A \
// RUN:     -ftaint-harden=%t.seed -mllvm -taint-dit-contract=callee \
// RUN:     -mllvm -taint-owned-symbols=%t.owned -mllvm -taint-dit-clone-seeded %s
// RUN: FileCheck --check-prefix=A --input-file=%t.a.s %s
//
// TU B, the callee: defines inner and leaf, and fwd, which holds no secret of
// its own.
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -O2 -S -o %t.b.s -DTU_B \
// RUN:     -ftaint-harden=%t.seed -mllvm -taint-dit-contract=callee \
// RUN:     -mllvm -taint-owned-symbols=%t.owned -mllvm -taint-dit-clone-seeded %s
// RUN: FileCheck --check-prefix=B --input-file=%t.b.s %s
//
// The owned list does not name inner: this build may not define it, so no twin
// can be assumed and the call stays as it was, with its re-assert. (outer,
// defined here, still gets its twin: cloning a definition needs no list.) The
// same holds with no list at all: a cross-TU twin is only ever named on the
// list's word.
// RUN: printf 'outer\n' > %t.owned1
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -O2 -S -o %t.a1.s -DTU_A \
// RUN:     -ftaint-harden=%t.seed -mllvm -taint-dit-contract=callee \
// RUN:     -mllvm -taint-owned-symbols=%t.owned1 -mllvm -taint-dit-clone-seeded %s
// RUN: FileCheck --check-prefix=A-NOOWN --input-file=%t.a1.s %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -O2 -S -o %t.a2.s -DTU_A \
// RUN:     -ftaint-harden=%t.seed -mllvm -taint-dit-contract=callee %s
// RUN: FileCheck --check-prefix=A-NOOWN --input-file=%t.a2.s %s
//
// The defaults (callee contract, twins on) are exactly the explicit flags above:
// the same TU with no contract or clone flag must come out identical.
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -O2 -S -o %t.a.def.s -DTU_A \
// RUN:     -ftaint-harden=%t.seed -mllvm -taint-owned-symbols=%t.owned %s
// RUN: diff %t.a.s %t.a.def.s
//
// Twins off (-taint-dit-clone-seeded=0): no twin anywhere, nothing redirected.
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -O2 -S -o %t.a0.s -DTU_A \
// RUN:     -ftaint-harden=%t.seed -mllvm -taint-dit-contract=callee \
// RUN:     -mllvm -taint-dit-clone-seeded=0 -mllvm -taint-owned-symbols=%t.owned %s
// RUN: FileCheck --check-prefix=OFF --input-file=%t.a0.s %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -O2 -S -o %t.b0.s -DTU_B \
// RUN:     -ftaint-harden=%t.seed -mllvm -taint-dit-contract=callee \
// RUN:     -mllvm -taint-dit-clone-seeded=0 -mllvm -taint-owned-symbols=%t.owned %s
// RUN: FileCheck --check-prefix=OFF --input-file=%t.b0.s %s

#ifdef TU_A
void inner(const unsigned long *key);
unsigned long sink;
void outer(const unsigned long *key) {
  unsigned long k = *key;
  sink = k * 3;
  inner(key);          /* between two Needs: made from DIT-on code */
  sink += k * 5;
}
// The call goes to the twin and DIT is not re-asserted after it: one enable,
// one clear, in the whole function.
// A-LABEL: outer:
// A:           msr {{#26|DIT}}, #1
// A-NOT:       msr {{#26|DIT}}
// A:           bl inner.dit
// A-NOT:       msr {{#26|DIT}}
// A:           msr {{#26|DIT}}, #0
// A-NOT:       msr {{#26|DIT}}
// A:           ret
//
// A-NOOWN-LABEL: outer:
// A-NOOWN:       bl inner{{$}}
// A-NOOWN:       msr {{#26|DIT}}, #1
// A-NOOWN-NOT:   inner.dit
#endif

#ifdef TU_B
unsigned long sinkb;
void ext_unknown(void);
/* Not seeded: it receives the secret from inner by propagation and protects
   itself; it is reached from a seed, so it gets a twin too. */
__attribute__((noinline)) static void helper(unsigned long k) { sinkb ^= k * 13; }
void leaf(const unsigned long *key) { sinkb = *key * 7; }
void inner(const unsigned long *key) { sinkb = *key * 11; helper(*key); }
void fwd(const unsigned long *key) { ext_unknown(); leaf(key); }
// Output order: the originals, then the twins. The original protects itself.
// Its call to the propagated-taint helper is made from DIT-on code, so even
// the original sends it to the helper's twin and needs no re-assert after it;
// the helper's original keeps its own toggles for callers that are not DIT-on.
// B-LABEL: inner:
// B:           msr {{#26|DIT}}, #1
// B-NOT:       msr {{#26|DIT}}
// B:           bl helper.dit
// B-NOT:       msr {{#26|DIT}}
// B:           msr {{#26|DIT}}, #0
// B:           ret
// B-LABEL: helper:
// B:           msr {{#26|DIT}}, #1
// B:           msr {{#26|DIT}}, #0
// B:           ret
//
// fwd holds no secret and so gets no switch at all.
// B-LABEL: fwd:
// B-NOT:       msr {{#26|DIT}}
// B:           bl ext_unknown
// B-NOT:       msr {{#26|DIT}}
// B:           bl .Lleaf$local
// B-NOT:       msr {{#26|DIT}}
// B:           ret
//
// B-LABEL: inner.dit:
// B-NOT:       msr {{#26|DIT}}
// B:           bl helper.dit
// B-NOT:       msr {{#26|DIT}}
// B:           ret
// B-LABEL: helper.dit:
// B-NOT:       msr {{#26|DIT}}
// B:           ret
//
// fwd's twin owes its caller DIT-set-on-return: it re-asserts after the call
// that may have cleared it, sends its own call to leaf's twin, and never
// clears.
// B-LABEL: fwd.dit:
// B-NOT:       msr {{#26|DIT}}
// B:           bl ext_unknown
// B-NOT:       bl
// B:           msr {{#26|DIT}}, #1
// B-NOT:       msr {{#26|DIT}}
// B:           bl .Lleaf.dit$local
// B-NOT:       msr {{#26|DIT}}
// B:           ret
#endif

// OFF-NOT: .dit
