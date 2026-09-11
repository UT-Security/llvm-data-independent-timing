/* What one `msr DIT` costs on this machine, measured rather than assumed.
 *
 * The pass's whole bill on Apple silicon is mode writes: `MSR DIT` is
 * SERIALISING here, which is the arm gem5 models as --no-speculative-dit and
 * the reason experiment 02 reports two pass curves on the simulator and one on
 * hardware. The crossover runner turns (pass - nop) cycles per request into an
 * executed switch count by dividing by this number, so it has to come from this
 * machine and not from a table.
 *
 * Method: a long loop of back-to-back writes against an identical loop with the
 * writes removed, differenced. Both loops are the same shape, so the loop
 * overhead cancels and what is left is the write. The counter is PMC0 read bare
 * (see pmc_ipc.h for why bare); CNTVCT gives the implied clock so a run that
 * migrated or ran on an E-core is visible.
 *
 * Reports the ENABLE/DISABLE pair and each write separately, plus the
 * same-value write (`msr DIT,#1` when DIT is already 1), which is what a
 * redundant re-assert after a call costs and is cheaper but not free.
 *
 *   clang -O2 dit_switch_cost.c -o dit_switch_cost && ./dit_switch_cost
 */
#include <pthread/qos.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define PMC48(x) ((x) & ((1ULL << 48) - 1))
static inline uint64_t pmc0(void) {
    uint64_t v; __asm__ volatile("mrs %0, S3_2_c15_c0_0" : "=r"(v) :: "memory");
    return PMC48(v);
}
static inline uint64_t cntvct(void) {
    uint64_t v; __asm__ volatile("mrs %0, cntvct_el0" : "=r"(v)); return v;
}
static inline uint64_t cntfrq(void) {
    uint64_t v; __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v)); return v;
}

#define LOOP(name, body)                                                      \
    __attribute__((noinline)) static uint64_t name(uint64_t n) {              \
        uint64_t c0, c1;                                                      \
        c0 = pmc0();                                                          \
        for (uint64_t i = 0; i < n; i++) { body }                             \
        c1 = pmc0();                                                          \
        return c1 - c0;                                                       \
    }
LOOP(loop_empty,  __asm__ volatile("" ::: "memory");)
LOOP(loop_pair,   __asm__ volatile("msr DIT, #1" ::: "memory");
                  __asm__ volatile("msr DIT, #0" ::: "memory");)
LOOP(loop_same,   __asm__ volatile("msr DIT, #1" ::: "memory");
                  __asm__ volatile("msr DIT, #1" ::: "memory");)

int main(int argc, char **argv) {
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    uint64_t n = argc > 1 ? strtoull(argv[1], NULL, 0) : 2000000;
    /* the counters must move, or this host lacks the PMCR0_USEREN_EN patch */
    if (pmc0() == 0) { fprintf(stderr, "PMCs read zero: no EL0 access\n"); return 2; }
    double be = 1e30, bp = 1e30, bs = 1e30, ghz = 0;
    for (int r = 0; r < 9; r++) {
        uint64_t t0 = cntvct();
        double e = (double) loop_empty(n);
        double p = (double) loop_pair(n);
        __asm__ volatile("msr DIT, #1" ::: "memory");
        double s = (double) loop_same(n);
        __asm__ volatile("msr DIT, #0" ::: "memory");
        uint64_t t1 = cntvct();
        double g = (e + p + s) / ((double)(t1 - t0) / (double) cntfrq()) / 1e9;
        if (g < 3.4 || g > 5.0) continue;          /* migrated, or an E-core */
        if (e < be) be = e;
        if (p < bp) bp = p;
        if (s < bs) bs = s;
        ghz = g;
    }
    if (bp > 1e29) { fprintf(stderr, "every rep failed the clock gate\n"); return 3; }
    printf("cyc_per_pair=%.2f cyc_per_write=%.2f cyc_per_same_value_write=%.2f "
           "loop_overhead_cyc=%.2f n=%llu ghz=%.3f\n",
           (bp - be) / n, (bp - be) / n / 2.0, (bs - be) / n / 2.0,
           be / n, (unsigned long long) n, ghz);
    return 0;
}
