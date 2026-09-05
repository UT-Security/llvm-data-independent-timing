// -taint-dit-external-preserves: a callee this module does not define is
// assumed never to write PSTATE.DIT, so a call to it from DIT-on code needs
// no re-assert after it. The one exception is a symbol the owned list names:
// that is our own function in another TU, which clears at its own exit under
// the callee contract, so its re-assert stays.
//
// REQUIRES: aarch64-registered-target
// RUN: echo "work,0,pointee" > %t.seed
// RUN: printf 'work\nours_ext\n' > %t.owned
//
// Control, the assumption off (=0): a re-assert follows every external call
// in DIT-on code. This was the default until 2026-09-05.
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -O2 -S -o - \
// RUN:     -ftaint-harden=%t.seed -mllvm -taint-dit-placement=function \
// RUN:     -mllvm -taint-dit-external-preserves=0 %s \
// RUN:   | FileCheck --check-prefix=NOFLAG %s
//
// The default (no flag) and no owned list: every external call is trusted, including
// memcpy, whose `bl` lowers an llvm.memcpy intrinsic and has no Function in
// the module (the test is by symbol).
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -O2 -S -o - \
// RUN:     -ftaint-harden=%t.seed -mllvm -taint-dit-placement=function %s \
// RUN:   | FileCheck --check-prefix=EXT %s
//
// With the flag AND an owned list naming ours_ext: ours_ext is ours, its
// re-assert stays; memcpy and other_ext are trusted.
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -O2 -S -o - \
// RUN:     -ftaint-harden=%t.seed -mllvm -taint-dit-placement=function \
// RUN:     -mllvm -taint-dit-external-preserves \
// RUN:     -mllvm -taint-owned-symbols=%t.owned %s \
// RUN:   | FileCheck --check-prefix=OWN %s

void *memcpy(void *, const void *, unsigned long);
extern void other_ext(unsigned long *);
extern void ours_ext(unsigned long *);
unsigned long sink[4];

void work(const unsigned long *key, unsigned long n) {
  unsigned long buf[8];
  memcpy(buf, key, n);        /* libc mover, handed the secret */
  sink[0] = buf[0] * buf[1];  /* secret op: DIT must be on here */
  other_ext(buf);             /* some other external */
  sink[1] = buf[2] * buf[3];
  ours_ext(buf);              /* ours under the owned list */
  sink[2] = buf[4] * buf[5];
}

// NOFLAG-LABEL: work:
// NOFLAG:       msr {{#26|DIT}}, #1
// NOFLAG:       bl memcpy
// NOFLAG-NEXT:  msr {{#26|DIT}}, #1
// NOFLAG:       bl other_ext
// NOFLAG-NEXT:  msr {{#26|DIT}}, #1
// NOFLAG:       bl ours_ext
// NOFLAG-NEXT:  msr {{#26|DIT}}, #1
// NOFLAG:       msr {{#26|DIT}}, #0
// NOFLAG:       ret

// EXT-LABEL: work:
// EXT:       msr {{#26|DIT}}, #1
// EXT:       bl memcpy
// EXT-NOT:   msr {{#26|DIT}}
// EXT:       bl other_ext
// EXT-NOT:   msr {{#26|DIT}}
// EXT:       bl ours_ext
// EXT-NOT:   msr {{#26|DIT}}, #1
// EXT:       msr {{#26|DIT}}, #0
// EXT:       ret

// OWN-LABEL: work:
// OWN:       msr {{#26|DIT}}, #1
// OWN:       bl memcpy
// OWN-NOT:   msr {{#26|DIT}}
// OWN:       bl other_ext
// OWN-NOT:   msr {{#26|DIT}}
// OWN:       bl ours_ext
// OWN-NEXT:  msr {{#26|DIT}}, #1
// OWN:       msr {{#26|DIT}}, #0
// OWN:       ret
