// A callee that returns a POINTER TO SECRET MEMORY, three ways. Before the
// ReturnsPointeeTainted summary bit (2026-09-04) the return carried no pointee
// fact, so the caller's load through it read clean and the arithmetic after it
// ran with DIT off (flowprobe C1/C5, 63 under-taint ops each). Each caller's
// precision line pins the load and the multiply as Needs.
//
// REQUIRES: aarch64-registered-target
// RUN: echo "via_arg,0,pointee"    >  %t.seed
// RUN: echo "via_global,0,pointee" >> %t.seed
// RUN: echo "via_heap,0,pointee"   >> %t.seed
// RUN: echo "via_gptr,0,pointee"   >> %t.seed
// RUN: echo "seeded_produce,0,pointee" >> %t.seed
// RUN: echo "seeded_bit,0,pointee" >> %t.seed
// RUN: rm -f %t.prec
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -O2 -S -o /dev/null \
// RUN:     -ftaint-harden=%t.seed -mllvm -taint-dit-precision-report=%t.prec %s
// RUN: FileCheck --input-file=%t.prec %s

void *malloc(unsigned long);

/* 1. the returned pointer is derived from a pointee-tainted argument */
__attribute__((noinline))
static unsigned long *into(const unsigned long *k) { return (unsigned long *)k + 3; }
__attribute__((noinline))
unsigned long via_arg(const unsigned long *key) {
    unsigned long *q = into(key);
    return q[0] * 11;
}

/* 2. the callee fills a global and returns its address (rule: the address of
 *    a module-secret global is pointee-tainted) */
static unsigned long g_buf[4];
__attribute__((noinline))
static unsigned long *fill_global(const unsigned long *k) {
    for (int i = 0; i < 4; i++) g_buf[i] = k[i];
    return g_buf;
}
__attribute__((noinline))
unsigned long via_global(const unsigned long *key) {
    unsigned long *q = fill_global(key);
    return q[1] * 13;
}

/* 3. the callee mallocs, fills the block and returns it (rule: a secret
 *    stored through a pointer makes that pointer pointee-tainted) */
__attribute__((noinline))
static unsigned long *fill_heap(const unsigned long *k) {
    unsigned long *p = malloc(32);
    for (int i = 0; i < 4; i++) p[i] = k[i];
    return p;
}
__attribute__((noinline))
unsigned long via_heap(const unsigned long *key) {
    unsigned long *q = fill_heap(key);
    return q[2] * 17;
}

/* 4. the callee stores the secret through a pointer it loaded from a GLOBAL
 *    pointer variable and returns that global (rule: the global now holds a
 *    pointer to secret memory, module-wide; the reload of it is pointee-tainted).
 *    This is flowprobe C5's exact shape. */
static unsigned long g_store[4];
static unsigned long *g_ptr = g_store;
__attribute__((noinline))
static unsigned long *fill_gptr(const unsigned long *k) {
    for (int i = 0; i < 4; i++) g_ptr[i] = k[i];
    return g_ptr;
}
__attribute__((noinline))
unsigned long via_gptr(const unsigned long *key) {
    unsigned long *q = fill_gptr(key);
    return q[3] * 19;
}

/* 5. flowprobe C5 exactly: the producer is SEEDED but its caller is not - the
 *    caller builds the key itself (under the oracle a runtime marker taints it)
 *    and parks the returned pointer in a global for a later consumer. The return
 *    gate "apply the callee's return only where WE passed a secret" dropped this
 *    with the summary bit set and correct: the seed makes the parameter secret
 *    at every call site, whatever the caller knows. */
__attribute__((noinline))
unsigned long *seeded_produce(const unsigned long *k) {
    unsigned long *p = malloc(32);
    for (int i = 0; i < 4; i++) p[i] = k[i];
    return p;
}
static unsigned long *g_parked;
__attribute__((noinline))
void unseeded_park(void) {
    unsigned long sk[4] = {1, 2, 3, 4};
    g_parked = seeded_produce(sk);
}
__attribute__((noinline))
unsigned long via_unseeded(void) { return g_parked[1] * 23; }

/* 6. the same gate, value return: a seeded callee returns a secret VALUE to an
 *    unseeded caller, which stores it in a global; the consumer reads it. */
__attribute__((noinline))
unsigned long seeded_bit(const unsigned long *k) { return k[0] & 1; }
static unsigned long g_bit;
__attribute__((noinline))
void unseeded_keep(void) {
    unsigned long sk[4] = {5, 6, 7, 8};
    g_bit = seeded_bit(sk);
}
__attribute__((noinline))
unsigned long via_unseeded_value(void) { return g_bit * 29; }

// Each caller: the call (secret-passing), the load through the result, and
// the multiply. need=1 (the call alone) is the pre-fix reading, measured on
// this compiler before the change. via_heap reads 4: fill_heap's mod-set is
// TOP (it stores through a malloc'd pointer), which under the shipped contract
// poisons the caller's own link-register reload too; under
// -taint-dit-contract=callee spill slots are exempt and it reads 3.
// CHECK-DAG: {{^}}via_arg need=3 {{.*}}
// CHECK-DAG: {{^}}via_global need=3 {{.*}}
// CHECK-DAG: {{^}}via_heap need=4 {{.*}}
// CHECK-DAG: {{^}}via_gptr need={{[3-9]}} {{.*}}
// The consumers load the parked result back and multiply (need>=2; the value
// case also pins the store of the secret in unseeded_keep). Parking the
// POINTER in unseeded_park is a store of a public address and correctly has no
// Need. Pre-fix none of the three functions has a Need at all.
// CHECK-DAG: {{^}}via_unseeded need={{[2-9]}} {{.*}}
// CHECK-DAG: {{^}}unseeded_keep need={{[1-9]}} {{.*}}
// CHECK-DAG: {{^}}via_unseeded_value need={{[1-9]}} {{.*}}
