// A file of external functions that never write PSTATE.DIT
// (-taint-dit-preserving-symbols). A call to one of them from DIT-on code
// needs no re-assert after it, since the callee returns the mode exactly as
// it found it. The file is trusted only for a callee this build does not
// define: a listed symbol the owned list names is ours, may clear at its own
// exit, and keeps its re-assert.
//
// REQUIRES: aarch64-registered-target
// RUN: echo "work,0,pointee" > %t.seed
// RUN: printf '# libc leaves\nmemcpy  # the mover\nours_ext\n' > %t.pres
// RUN: printf 'work\nours_ext\n' > %t.owned
//
// Control, no file: a re-assert follows every external call in DIT-on code.
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -O2 -S -o - \
// RUN:     -ftaint-harden=%t.seed -mllvm -taint-dit-placement=function %s \
// RUN:   | FileCheck --check-prefix=NOFILE %s
//
// With the file: memcpy's re-assert is gone (the call lowers an llvm.memcpy
// intrinsic, so it is a bare external symbol with no Function in the module:
// the lookup is by name); other_ext, unlisted, keeps its own; ours_ext is
// listed and, with no owned list, external as far as this TU knows, so it is
// trusted too.
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -O2 -S -o - \
// RUN:     -ftaint-harden=%t.seed -mllvm -taint-dit-placement=function \
// RUN:     -mllvm -taint-dit-preserving-symbols=%t.pres %s \
// RUN:   | FileCheck --check-prefix=PRES %s
//
// With the file AND an owned list naming ours_ext: the list wins, ours_ext
// keeps its re-assert; memcpy is still trusted.
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -O2 -S -o - \
// RUN:     -ftaint-harden=%t.seed -mllvm -taint-dit-placement=function \
// RUN:     -mllvm -taint-dit-preserving-symbols=%t.pres \
// RUN:     -mllvm -taint-owned-symbols=%t.owned %s \
// RUN:   | FileCheck --check-prefix=OWN %s

void *memcpy(void *, const void *, unsigned long);
extern void other_ext(unsigned long *);
extern void ours_ext(unsigned long *);
unsigned long sink[4];

void work(const unsigned long *key, unsigned long n) {
  unsigned long buf[8];
  memcpy(buf, key, n);        /* listed mover, handed the secret */
  sink[0] = buf[0] * buf[1];  /* secret op: DIT must be on here */
  other_ext(buf);             /* unlisted external */
  sink[1] = buf[2] * buf[3];
  ours_ext(buf);              /* listed, but ours under the owned list */
  sink[2] = buf[4] * buf[5];
}

// NOFILE-LABEL: work:
// NOFILE:       msr {{#26|DIT}}, #1
// NOFILE:       bl memcpy
// NOFILE-NEXT:  msr {{#26|DIT}}, #1
// NOFILE:       bl other_ext
// NOFILE-NEXT:  msr {{#26|DIT}}, #1
// NOFILE:       bl ours_ext
// NOFILE-NEXT:  msr {{#26|DIT}}, #1
// NOFILE:       msr {{#26|DIT}}, #0
// NOFILE:       ret

// PRES-LABEL: work:
// PRES:       msr {{#26|DIT}}, #1
// PRES:       bl memcpy
// PRES-NOT:   msr {{#26|DIT}}
// PRES:       bl other_ext
// PRES-NEXT:  msr {{#26|DIT}}, #1
// PRES:       bl ours_ext
// PRES-NOT:   msr {{#26|DIT}}, #1
// PRES:       msr {{#26|DIT}}, #0
// PRES:       ret

// OWN-LABEL: work:
// OWN:       msr {{#26|DIT}}, #1
// OWN:       bl memcpy
// OWN-NOT:   msr {{#26|DIT}}
// OWN:       bl other_ext
// OWN-NEXT:  msr {{#26|DIT}}, #1
// OWN:       bl ours_ext
// OWN-NEXT:  msr {{#26|DIT}}, #1
// OWN:       msr {{#26|DIT}}, #0
// OWN:       ret
