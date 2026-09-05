// Both contracts are pinned and the twins are off: the switch counts below are
// per contract, not per twin. (Since 2026-09-05 callee and the twins are the
// defaults.)
// The callee contract (-taint-dit-contract=callee): every function protects its
// own secrets, a call is never a Need for its arguments, and a secret reaching
// a callee this build cannot see is an OBLIGATION in the info-loss report - not
// something the caller covers by holding DIT across the call. The shipped
// `inherit` contract is run first as the control, so each check below is a
// difference between the two, not a property either would show alone.
//
// REQUIRES: aarch64-registered-target
// RUN: echo "wrap,0,pointee"     >  %t.seed
// RUN: echo "leak,0,pointee"     >> %t.seed
// RUN: echo "dispatch,1,pointee" >> %t.seed
// RUN: echo "mover,0,pointee"    >> %t.seed
// RUN: echo "compare,0,pointee"  >> %t.seed
//
// RUN: rm -f %t.inh.prec %t.inh.loss
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -O2 -S -o /dev/null \
// RUN:     -ftaint-harden=%t.seed -mllvm -taint-dit-contract=inherit \
// RUN:     -mllvm -taint-dit-clone-seeded=0 -mllvm -taint-dit-precision-report=%t.inh.prec \
// RUN:     -mllvm -taint-info-loss-report=%t.inh.loss %s 2>/dev/null
// RUN: FileCheck --check-prefix=INH --input-file=%t.inh.prec %s
// RUN: FileCheck --check-prefix=INH-LOSS --input-file=%t.inh.loss %s
//
// RUN: rm -f %t.cal.prec %t.cal.loss
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -O2 -S -o /dev/null \
// RUN:     -ftaint-harden=%t.seed -mllvm -taint-dit-contract=callee \
// RUN:     -mllvm -taint-dit-clone-seeded=0 -mllvm -taint-dit-precision-report=%t.cal.prec \
// RUN:     -mllvm -taint-info-loss-report=%t.cal.loss %s 2>%t.cal.err
// RUN: FileCheck --check-prefix=CAL --input-file=%t.cal.prec %s
// RUN: FileCheck --check-prefix=CAL-ABSENT --input-file=%t.cal.prec %s
// RUN: FileCheck --check-prefix=CAL-LOSS --input-file=%t.cal.loss %s
// RUN: FileCheck --check-prefix=CAL-LOSS-ABSENT --input-file=%t.cal.loss %s
// RUN: FileCheck --check-prefix=CAL-ERR --input-file=%t.cal.err %s

void *memcpy(void *, const void *, unsigned long);
int memcmp(const void *, const void *, unsigned long);
extern void ext_consume(const unsigned long *);
unsigned long sink;
unsigned char dst[64];

/* 1. An in-TU callee. The caller's only secret-carrying instruction is the
 *    call itself. Inherit: the call is a Need, `wrap` is instrumented. Callee
 *    contract: `wrap` has no Need and no switches; `helper` covers itself under
 *    both. */
__attribute__((noinline))
static void helper(const unsigned long *k) { sink = k[0] * 3; }
void wrap(const unsigned long *key) { helper(key); }

/* 2. An external callee. Inherit: `leak` is instrumented so `ext_consume`
 *    inherits DIT, and the report says so at moderate severity. Callee contract:
 *    `leak` has no switches and the report carries an UNCOVERED obligation with
 *    the seed line. */
void leak(const unsigned long *key) { ext_consume(key); }

/* 3. An indirect callee: the ecp_mod_p256 shape. The target cannot be named, so
 *    the obligation says to seed every target. */
void dispatch(void (*fp)(const unsigned long *), const unsigned long *key) {
    fp(key);
}

/* 4. A libc mover with a run-time length (so it stays a libcall rather than
 *    being expanded inline). Its loads and stores of the secret bytes are the
 *    data-value channel DIT covers, so it IS an obligation, with its own
 *    repair: link a hardened mover ahead of libc. */
void mover(const unsigned long *key, unsigned long n) { memcpy(dst, key, n); }

/* 5. memcmp: an obligation too, and the record's seed line is the repair. */
int compare(const unsigned long *key, unsigned long n) {
    return memcmp(dst, key, n);
}

// The control: every secret-passing call is a Need, so each caller is
// instrumented, and the unseen callees are moderate "inherits protection"
// records.
// INH-DAG: {{^}}wrap need={{[1-9]}} {{.*}}
// INH-DAG: {{^}}leak need={{[1-9]}} {{.*}}
// INH-DAG: {{^}}dispatch need={{[1-9]}} {{.*}}
// INH-DAG: {{^}}mover need={{[1-9]}} {{.*}}
// INH-DAG: {{^}}helper need={{[1-9]}} {{.*}}
// INH-LOSS: taint-stop cross-tu  in=leak {{.*}}callee=ext_consume
// INH-LOSS-NEXT: severity  moderate
//
// The contract: the callee covers itself, the callers carry nothing. `compare`
// still gets a line, at need=0, because its return hands back memcmp's
// tainted result; the others have no tainted instruction at all.
// CAL-DAG: {{^}}helper need={{[1-9]}} {{.*}}
// CAL-DAG: {{^}}compare need=0 {{.*}}
// CAL-ABSENT-NOT: {{^}}wrap need=
// CAL-ABSENT-NOT: {{^}}leak need=
// CAL-ABSENT-NOT: {{^}}dispatch need=
// CAL-ABSENT-NOT: {{^}}mover need=
//
// The obligations, with the pasteable seed line for the named callee.
// CAL-LOSS-DAG: taint-stop uncovered-callee  in=leak {{.*}}callee=ext_consume
// CAL-LOSS-DAG: severity  UNCOVERED
// CAL-LOSS-DAG: ext_consume,0,pointee
// CAL-LOSS-DAG: taint-stop uncovered-indirect  in=dispatch {{.*}}callee=<indirect>
// CAL-LOSS-DAG: seed every function this pointer can reach
// CAL-LOSS-DAG: taint-stop uncovered-callee  in=compare {{.*}}callee=memcmp
// CAL-LOSS-DAG: taint-stop uncovered-callee  in=mover {{.*}}callee=memcpy
// CAL-LOSS-DAG: link a hardened mover ahead of libc
// CAL-LOSS-ABSENT-NOT: in=wrap
//
// Once per TU on stderr: four sites (leak, dispatch, mover, compare), three
// named callees (ext_consume, memcpy, memcmp), one indirect target.
// CAL-ERR: taint: taint-dit-contract.c: 4 secret-passing call site(s) reach 3 callee(s) and 1 indirect target(s) this build does not cover (callee contract)
