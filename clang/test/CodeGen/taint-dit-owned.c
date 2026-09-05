// Ownership in the callee contract's obligation report. With
// -taint-owned-symbols naming the functions this build defines, an unseen
// callee in the list is an obligation with a seed line; one outside it is
// external code the developer does not own, filed as `external-call` (out of
// scope, no repair) and counted separately on stderr. memcpy is external here
// too, with its class named. Without the file every named callee is an
// obligation (the control run).
//
// REQUIRES: aarch64-registered-target
// RUN: echo "leak_owned,0,pointee" >  %t.seed
// RUN: echo "leak_libc,0,pointee"  >> %t.seed
// RUN: echo "mover,0,pointee"      >> %t.seed
// RUN: printf 'ours_ext\nleak_owned\nleak_libc\nmover\n' > %t.owned
//
// RUN: rm -f %t.loss
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -O2 -S -o /dev/null \
// RUN:     -ftaint-harden=%t.seed -mllvm -taint-dit-contract=callee \
// RUN:     -mllvm -taint-owned-symbols=%t.owned \
// RUN:     -mllvm -taint-info-loss-report=%t.loss %s 2>%t.err
// RUN: FileCheck --check-prefix=OWN --input-file=%t.loss %s
// RUN: FileCheck --check-prefix=OWN-ABSENT --input-file=%t.loss %s
// RUN: FileCheck --check-prefix=OWN-ERR --input-file=%t.err %s
//
// RUN: rm -f %t.ctl.loss
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -O2 -S -o /dev/null \
// RUN:     -ftaint-harden=%t.seed -mllvm -taint-dit-contract=callee \
// RUN:     -mllvm -taint-info-loss-report=%t.ctl.loss %s 2>/dev/null
// RUN: FileCheck --check-prefix=CTL --input-file=%t.ctl.loss %s

void *memcpy(void *, const void *, unsigned long);
extern void ours_ext(const unsigned long *);   /* defined by another TU of ours */
extern void libc_ext(const unsigned long *);   /* not defined by this build */
unsigned char dst[64];

void leak_owned(const unsigned long *key) { ours_ext(key); }
void leak_libc(const unsigned long *key) { libc_ext(key); }
void mover(const unsigned long *key, unsigned long n) { memcpy(dst, key, n); }

// OWN-DAG: taint-stop uncovered-callee  in=leak_owned {{.*}}callee=ours_ext
// OWN-DAG: ours_ext,0,pointee
// OWN-DAG: taint-stop external-call  in=leak_libc {{.*}}callee=libc_ext
// OWN-DAG: severity  info
// OWN-DAG: out of scope for the seed loop
// OWN-DAG: taint-stop external-call  in=mover {{.*}}callee=memcpy
// OWN-DAG: a hardened mover linked ahead of libc would cover them
// OWN-ABSENT-NOT: libc_ext,0,pointee
// OWN-ABSENT-NOT: memcpy,1,pointee
//
// OWN-ERR: taint: taint-dit-owned.c: 1 secret-passing call site(s) reach 1 callee(s) this build does not cover (callee contract)
// OWN-ERR: taint: taint-dit-owned.c: 2 secret-passing call site(s) reach 2 external callee(s) this build does not define (out of scope
//
// The control, no ownership file: all three are obligations.
// CTL-DAG: taint-stop uncovered-callee  in=leak_owned {{.*}}callee=ours_ext
// CTL-DAG: taint-stop uncovered-callee  in=leak_libc {{.*}}callee=libc_ext
// CTL-DAG: taint-stop uncovered-callee  in=mover {{.*}}callee=memcpy
