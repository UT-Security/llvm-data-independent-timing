// Narrowing twins (-taint-dit-twin-narrow). A `.dit` twin is entered with DIT
// set and must return it set; by default it stays DIT-on end to end. With the
// knob it places regions like an original with the entry and exit inverted:
// a clear at entry over a public preamble the admission test admits, the
// enable at the first secret block, and an enable again before any return it
// would otherwise reach DIT-off. With the twin switch cost at 0 every corridor
// stays Off (the maximal narrowing arm); at the default cost a short preamble
// is merged and the twin looks as before.
//
// REQUIRES: aarch64-registered-target
// RUN: echo "work,0,pointee" > %t.seed
// RUN: echo "caller,0,pointee" >> %t.seed
// RUN: printf 'work\ncaller\n' > %t.owned
//
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -O2 -S -o %t.max.s \
// RUN:     -ftaint-harden=%t.seed -mllvm -taint-owned-symbols=%t.owned \
// RUN:     -mllvm -taint-dit-twin-narrow -mllvm -taint-dit-twin-switch-cyc=0 %s
// RUN: FileCheck --check-prefix=MAX --input-file=%t.max.s %s
//
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -O2 -S -o %t.def.s \
// RUN:     -ftaint-harden=%t.seed -mllvm -taint-owned-symbols=%t.owned %s
// RUN: FileCheck --check-prefix=DEF --input-file=%t.def.s %s

unsigned long pub[256];
unsigned long sink;
/* A long public preamble (a loop over public data, no secret touched), then
   the secret work. */
__attribute__((noinline)) void work(const unsigned long *key, unsigned long n) {
  unsigned long acc = 0;
  for (unsigned long i = 0; i < n; i++)
    acc += pub[i & 255] * 3 + i;
  sink = acc ^ (*key * 7);
}
void caller(const unsigned long *key) {
  unsigned long k = *key;
  sink = k * 3;
  work(key, 1000);      /* made from DIT-on code: goes to the twin */
  sink += k * 5;
}

// Maximal narrowing: the twin clears at entry, the public loop runs DIT-off,
// the enable comes at the secret store, and nothing clears before the return.
// MAX-LABEL: work.dit:
// MAX:           msr {{#26|DIT}}, #0
// MAX-NOT:       msr {{#26|DIT}}
// MAX:           msr {{#26|DIT}}, #1
// MAX-NOT:       msr {{#26|DIT}}, #0
// MAX:           ret
//
// Default: the twin has no switch of its own.
// DEF-LABEL: work.dit:
// DEF-NOT:       msr {{#26|DIT}}
// DEF:           ret
