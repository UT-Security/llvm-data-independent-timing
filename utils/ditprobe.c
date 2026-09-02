/*
 * ditprobe - the validity-gate instrument for the DIT evaluation.
 *
 * RECONSTRUCTED 2026-09-02. The original driver lived only in an untracked home
 * directory on the M5 machine and was lost; what survived is its output
 * (paper_experiments/09-libsodium-cio-parity/data/ditprobe_gates.csv). This file
 * is rebuilt from that output's four metric names and from the arithmetic they
 * imply.
 *
 * THIS COPY IS CANONICAL. The rigs build drivers from $BENCH_DIR/<name>/<name>.c,
 * so utils/taint_libsodium_sudo_run.sh and utils/taint_libsodium_bench.sh install
 * this file to $BENCH_DIR/ditprobe/ditprobe.c on every run, overwriting what is
 * there. Edit it HERE, never in the benchmark checkout -- crypto-dit-benchmarks is
 * not part of this repo, and an instrument that exists only inside it is one
 * `rm -rf` from being lost a second time. That is not hypothetical: it is exactly
 * how the first copy went, along with the arm-N recipe (see
 * utils/taint_libsodium_narrow.sh) and the eval_util.h port (see
 * utils/taint_cio_eval_setup.sh).
 *
 * WHY IT EXISTS. Experiment 09's headline is a NULL result: blanket PSTATE.DIT
 * costs between -0.60% and +1.95% on CIO's six benchmarks. A null result from an
 * instrument that cannot see the thing it is looking for is worthless. These
 * four metrics are what make the null mean something, and the run script
 * interleaves this driver with every benchmark, in every arm, in the same
 * rotation, so the gates describe the same machine state the numbers came from.
 *
 *   Const    value-predictable pointer chase, ps/hop.  GATE 1: must SLOW DOWN
 *            sharply when DIT is on. This is the positive control - proof the
 *            mode is reaching the core and changing execution.
 *   Perm     random-permutation chase over the SAME buffer, ps/hop. GATE 2: must
 *            stay flat. Nothing about it is predictable, so DIT has nothing to
 *            take away; if this moves, the "slowdown" in Const is drift or
 *            thermal, not DIT.
 *   CoreMHz  dependent-ADD chain, MHz. GATE 3: P-cluster residency. QoS is a
 *            bias on Apple silicon, not a binding, so it is checked per rep
 *            rather than assumed.
 *   DitBit   PSTATE.DIT read INSIDE the measured region. GATE 4: must be 1 in
 *            the blanket arm and 0 in every selectively placed arm.
 *
 * HOW GATE 1 WORKS. Both chases are one dependent load chain, and both are
 * L1-resident, so prefetching cannot help either of them: the only way to beat
 * L1 load-to-use latency is to predict the loaded value before the load
 * returns. Const is a SELF-REFERENCING node - `*p == p`, so the load's value is
 * constant - and that is the one shape the core's load-value predictor learns.
 * Perm is a random 512-node cycle whose value is different every hop.
 *
 * The predictor is a CONSTANT-value predictor, not a stride predictor, and this
 * was measured rather than assumed. Rebuilding this file on M4 (2026-09-02) all
 * seven shapes were timed with the mode off and on:
 *
 *   self-loop, constant value      231 -> 682 ps/hop   2.952x
 *   2 / 4 / 8-node cycle           681 -> 681 ps/hop   1.000x
 *   64 / 512-node cycle (stride)   681 -> 681 ps/hop   1.000x
 *   512-node random permutation    681 -> 680 ps/hop   0.999x
 *
 * Only the constant is predicted. A strided sequence of loaded values is NOT,
 * which is worth recording because "value-predictable" invites the stride
 * reading and the stride reading measures nothing - it silently returns a 1.00x
 * gate that looks like a hardware finding and is really a broken instrument.
 *
 * The two arms of the pair have different footprints (one line vs 512) and that
 * is fine, because both sit in L1: with prediction off they both settle at L1
 * latency, which is why Const-on lands on top of Perm on both machines. What
 * gate 2 is for is showing that nothing ELSE moved - drift, thermals, cluster
 * migration - while gate 1 moved.
 *
 * ABSOLUTE ps/hop IS NOT PORTABLE; cycles/hop is. Const off is 1.02 cycles/hop
 * on both machines - 222 ps at 4597 MHz on M5, 231 ps at 4415 MHz on M4 - but
 * the ratio differs, 3.932x vs 2.952x, because L1 load-to-use is 4 cycles on M5
 * and 3 on M4. Compare the gate across hosts as a ratio and as cycles/hop, and
 * never by the raw picoseconds.
 *
 * The ARM is selected by ENABLE_DIT, read by dit_enable()/dit_disable() in
 * perf.c. Those two are deliberately instruction-count-symmetric - the arm that
 * does not set the mode issues a NOP - so the arms execute the same work.
 *
 * OUTPUT FORMAT is fixed by the run script's awk, which reads the metric name
 * from field 2 and the VALUE from the second-to-last field:
 *     === <Name> <integer value> <maxrss KB>
 * Keep it. Renaming a metric silently drops its gate from the results.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/resource.h>

#include "../utils.h"
#include "../perf.c"

/* 512 nodes x 64 B = 32 KB, one node per cache line: the Perm chain is a
   dependent walk that stays entirely in L1, so its ps/hop reads back L1
   load-to-use latency and nothing else. */
#define NODES   512u
#define STRIDE  64u
#define HOPS    (1u << 24)      /* ~16.7M hops: milliseconds of work, so the
                                   41.67 ns timebase quantisation is noise */
#define ADDS    64u             /* dependent adds per unrolled iteration */
#define ADD_IT  (1u << 18)

static void *g_sink;            /* keeps the chase from being optimised away */
static unsigned char *g_buf;

static inline uint64_t now_ns(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }

static void **node(unsigned i) { return (void **)(g_buf + (size_t)i * STRIDE); }

/* Node 0 points at itself, so every hop loads the identical value and the
   load-value predictor breaks the dependent chain. This is the positive
   control; see the measured table in the header for why it has to be a constant
   and not a stride. */
static void build_const(void) { *node(0) = node(0); }

/* A single random Hamiltonian cycle over the same nodes: same lines, same chain
   length, nothing to predict. Fixed seed so every arm and every rep walks the
   identical order - a per-run permutation would add variance between arms. */
static void build_perm(void) {
    unsigned *ord = malloc(NODES * sizeof *ord);
    for (unsigned i = 0; i < NODES; i++) ord[i] = i;
    srand(172812u);                       /* CIO's EVAL_UTIL_H_SEED, for luck */
    for (unsigned i = NODES - 1; i > 0; i--) {   /* Fisher-Yates on ord[1..] so
                                                    node 0 stays the entry */
        unsigned j = 1u + (unsigned)(rand() % (int)i);
        unsigned t = ord[i]; ord[i] = ord[j]; ord[j] = t;
    }
    for (unsigned i = 0; i < NODES; i++) *node(ord[i]) = node(ord[(i + 1) % NODES]);
    free(ord);
}

static void chase(uint64_t hops) {
    void **p = node(0);
    while (hops--) p = (void **)*p;
    g_sink = p;
}

/* ps/hop for whichever chain is currently built. Warm up first: the buffer has
   to be in L1 and the predictor has to have seen the pattern, or the first
   million hops measure the cold path instead of the steady state. */
static unsigned probe_chase_ps(void) {
    chase(HOPS / 8);
    dit_enable();
    uint64_t t0 = now_ns();
    chase(HOPS);
    uint64_t t1 = now_ns();
    dit_disable();
    return (unsigned)((double)(t1 - t0) * 1000.0 / (double)HOPS + 0.5);
}

/* A dependent add chain runs at exactly 1 per cycle, so elapsed time over a
   known chain length reads back the core clock. The loop's own cmp/branch is
   off the critical path on an out-of-order core, which is why the chain is
   unrolled 64 deep. */
#define ADD8(x) __asm__ volatile("add %0, %0, #1" : "+r"(x)); \
                __asm__ volatile("add %0, %0, #1" : "+r"(x)); \
                __asm__ volatile("add %0, %0, #1" : "+r"(x)); \
                __asm__ volatile("add %0, %0, #1" : "+r"(x)); \
                __asm__ volatile("add %0, %0, #1" : "+r"(x)); \
                __asm__ volatile("add %0, %0, #1" : "+r"(x)); \
                __asm__ volatile("add %0, %0, #1" : "+r"(x)); \
                __asm__ volatile("add %0, %0, #1" : "+r"(x));
static unsigned probe_core_mhz(void) {
    uint64_t x = 0;
    for (unsigned i = 0; i < ADD_IT / 8; i++) { ADD8(x) }      /* warmup */
    dit_enable();
    uint64_t t0 = now_ns();
    for (unsigned i = 0; i < ADD_IT; i++) { ADD8(x) ADD8(x) ADD8(x) ADD8(x)
                                            ADD8(x) ADD8(x) ADD8(x) ADD8(x) }
    uint64_t t1 = now_ns();
    dit_disable();
    g_sink = (void *)(uintptr_t)x;
    double cycles = (double)ADD_IT * (double)ADDS;
    return (unsigned)(cycles / (double)(t1 - t0) * 1000.0 + 0.5);
}

static unsigned probe_dit_bit(void) {
    unsigned long d;
    dit_enable();
    __asm__ volatile("mrs %0, DIT" : "=r"(d));
    dit_disable();
    return (unsigned)((d >> 24) & 1UL);
}

int main(void) {
    if (posix_memalign((void **)&g_buf, 16384, (size_t)NODES * STRIDE) != 0) {
        fprintf(stderr, "ditprobe: allocation failed\n");
        return 1;
    }
    memset(g_buf, 0, (size_t)NODES * STRIDE);

    if (perf_init("ditprobe") == 0) perf_start();

    build_const();
    unsigned c_ps = probe_chase_ps();
    build_perm();
    unsigned p_ps = probe_chase_ps();
    unsigned mhz  = probe_core_mhz();
    unsigned bit  = probe_dit_bit();

    perf_stop();
    perf_cleanup();

    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    unsigned mem = (unsigned)(ru.ru_maxrss / 1024);

    printf("=== Const %u %u\n",   c_ps, mem);
    printf("=== Perm %u %u\n",    p_ps, mem);
    printf("=== CoreMHz %u %u\n", mhz,  mem);
    printf("=== DitBit %u %u\n",  bit,  mem);

    free(g_buf);
    return 0;
}
