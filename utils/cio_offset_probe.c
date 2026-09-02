/*
 * cio_offset_probe - measure the instrument's own cost, in its own units.
 *
 * WHY. CIO's drivers time one crypto call as
 *     start = START_CYCLE_TIMER; op(); end = STOP_CYCLE_TIMER;
 * and under root those macros are kpc_get_thread_counters(), a call into the
 * kperf driver rather than a register read. The counter is sampled part-way
 * through each call, so the tail of the first call and the head of the second
 * land BETWEEN the two timestamps and are counted as if they were crypto. Every
 * sample therefore carries a fixed additive offset.
 *
 * That offset cancels in arm-vs-arm DIFFERENCES (extra cycles/op, cycles per
 * switch) but NOT in the ratios, because it sits in the denominator. On the fast
 * primitives it is most of the denominator: aes256-gcm encrypt's true cost is a
 * few hundred cycles against a measured baseline of ~3,700. So the percentage
 * columns of paper_experiments/09 are deflated by (true+offset)/true, and this
 * program is how you find out by how much.
 *
 * WHAT IT MEASURES. Region cost against a payload of KNOWN cycle cost: a chain
 * of dependent ADDs, which retire one per cycle (the CoreMHz gate in ditprobe.c
 * is the same trick and reads the core clock correctly, which is what licenses
 * the assumption). Sweeping the payload gives a straight line:
 *
 *     measured = slope * payload + intercept
 *
 * The INTERCEPT is the offset. The SLOPE is the check that makes the intercept
 * mean anything, and what it must equal depends on which cycle source the shim
 * got: 1.0 under kperf, which counts cycles, and 1/GHz under the CNTVCT_EL0
 * fallback, which counts TIME (1 ns per tick on M4, ~41.67 ns on the 24 MHz
 * hosts). Either way the fit must be straight; a slope that misses its expected
 * value says the instrument perturbs the payload rather than merely adding to
 * it, and then no single offset exists to subtract.
 *
 * WHAT THE SLOPE DOES NOT PROVE. The payload is a register-only ADD chain: it
 * touches no memory, so a slope of 1.0 shows the syscall does not perturb
 * REGISTER work. It says nothing about cache, TLB or branch-predictor state,
 * and a real crypto call depends on all three. If the kperf syscall evicts
 * lines the crypto then has to refill, the crypto is genuinely slower inside
 * the instrumented region than outside, and that cost is NOT part of the
 * intercept. So (measured - intercept) is an UPPER bound on the uninstrumented
 * cost, and a direct CNTVCT_EL0 measurement of the same call is a lower one.
 * Quote the pair, not a point estimate. Extending the sweep with a
 * cache-resident payload would close this gap and has not been done.
 *
 * It deliberately goes through eval_util.h's macros rather than calling the shim
 * directly, so it measures the instrument the rig actually used, not a
 * reimplementation of it.
 *
 * BUILD (same flags as taint_libsodium_sudo_run.sh part 2):
 *   clang -fomit-frame-pointer -O2 -std=c18 -DCIO_SHIM_KPERF \
 *         -I<crypto-dit-benchmarks> -include utils/cio_arm_shim.h \
 *         -I<cio-eval> -o cio_offset_probe utils/cio_offset_probe.c -lm
 * RUN     sudo -E ./cio_offset_probe        (without root it reports the
 *                                            CNTVCT_EL0 path instead)
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include "eval_util.h"

#define ITERS   1000            /* CIO's iteration count */
#define WARMUP  25              /* CIO's warmup */

static uint64_t g_sink;

/* 8 dependent adds; the loop's own cmp/branch is off the critical path. */
#define ADD8(x) __asm__ volatile("add %0, %0, #1" : "+r"(x)); \
                __asm__ volatile("add %0, %0, #1" : "+r"(x)); \
                __asm__ volatile("add %0, %0, #1" : "+r"(x)); \
                __asm__ volatile("add %0, %0, #1" : "+r"(x)); \
                __asm__ volatile("add %0, %0, #1" : "+r"(x)); \
                __asm__ volatile("add %0, %0, #1" : "+r"(x)); \
                __asm__ volatile("add %0, %0, #1" : "+r"(x)); \
                __asm__ volatile("add %0, %0, #1" : "+r"(x));

static inline void payload(unsigned cycles) {
    uint64_t x = g_sink;
    for (unsigned i = 0; i < cycles / 8; i++) { ADD8(x) }
    g_sink = x;
}

/* Core clock, same dependent-ADD technique as ditprobe's CoreMHz gate. Called
   next to every sweep point because QoS is a BIAS on Apple silicon, not a
   binding, and a pass that drifted to the E-cluster measures a different
   syscall cost in cycles -- the offset is a cycle count, so which core it was
   measured on changes the answer. There is no way to pin, so the only honest
   option is to measure and discard.

   A LOW READING IS NOT NECESSARILY AN E-CORE. Measured on M4 2026-09-02, the
   first two passes after an idle period ramped 1798 -> 3090 and 3143 -> 4105
   MHz across the sweep: that is a P-core coming up under DVFS, not cluster
   migration. Both look identical to this probe, so it reports "low clock" and
   discards, without claiming which. The offset turned out barely sensitive to
   either -- every pass, ramping or not, landed in 3229-3306 cycles -- but the
   discard stays, because that insensitivity is a result and not an assumption
   to build in. */
static double core_ghz(void) {
    uint64_t x = g_sink;
    const unsigned N = 1u << 16;
    for (unsigned i = 0; i < N / 8; i++) { ADD8(x) }
    uint64_t a = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    for (unsigned i = 0; i < N; i++) { ADD8(x) ADD8(x) ADD8(x) ADD8(x)
                                       ADD8(x) ADD8(x) ADD8(x) ADD8(x) }
    uint64_t b = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    g_sink = x;
    return (double)N * 64.0 / (double)(b - a);
}

static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

int main(void) {
    static const unsigned sweep[] = { 0, 128, 256, 512, 1024, 2048, 4096 };
    const unsigned NS = sizeof sweep / sizeof *sweep;
    const unsigned PASSES = 5;
    const double P_CORE_GHZ = 4.0;    /* M4 P-core ~4.4, E-core ~2.7 */
    uint64_t *t = malloc(ITERS * sizeof *t);

    int kperf = 0;
#ifdef CIO_SHIM_KPERF
    kperf = cio_shim_kperf_ok;
#endif
    const char *unit = kperf ? "cycles" : "CNTVCT ticks";
    printf("cycle source: %s\n\n", kperf ? "kperf" : "cntvct_el0");
    printf("%6s %9s %12s %12s %10s %8s\n",
           "pass", "payload", "median", "med-payload", "MHz", "cluster");

    double icepts[64]; unsigned nic = 0;
    for (unsigned pass = 0; pass < PASSES; pass++) {
        double px[16], py[16], mhz_min = 1e9, mhz_max = 0;
        unsigned ok = 1;
        for (unsigned sI = 0; sI < NS; sI++) {
            volatile uint64_t start = 0, end = 0;
            for (unsigned i = 0; i < ITERS + WARMUP; i++) {
                start = START_CYCLE_TIMER;
                payload(sweep[sI]);
                end = STOP_CYCLE_TIMER;
                if (i >= WARMUP) t[i - WARMUP] = end - start;
            }
            qsort(t, ITERS, sizeof *t, cmp_u64);
            double med = (double)t[ITERS / 2];
            double g = core_ghz();
            if (g < mhz_min) mhz_min = g;
            if (g > mhz_max) mhz_max = g;
            if (g < P_CORE_GHZ) ok = 0;
            px[sI] = sweep[sI]; py[sI] = med;
            printf("%6u %9u %12.0f %12.0f %10.0f %8s\n", pass + 1, sweep[sI], med,
                   med - (kperf ? sweep[sI] : 0), g * 1000.0,
                   g < P_CORE_GHZ ? "low !" : "P");
        }
        double mx = 0, my = 0;
        for (unsigned i = 0; i < NS; i++) { mx += px[i]; my += py[i]; }
        mx /= NS; my /= NS;
        double num = 0, den = 0;
        for (unsigned i = 0; i < NS; i++) {
            num += (px[i]-mx)*(py[i]-my); den += (px[i]-mx)*(px[i]-mx); }
        double slope = num/den, ic = my - slope*mx;
        double expect = kperf ? 1.0 : 1.0 / ((mhz_min+mhz_max)/2.0);
        int additive = slope > expect*0.9 && slope < expect*1.1;
        printf("  -> pass %u: slope %.4f (exp %.4f) intercept %.0f %s  [%s%s]\n\n",
               pass + 1, slope, expect, ic, unit,
               ok ? "full clock" : "LOW CLOCK, DISCARDED",
               additive ? "" : ", NOT ADDITIVE - DISCARDED");
        if (ok && additive) icepts[nic++] = ic;
    }

    printf("================================================================\n");
    if (nic == 0) {
        printf("NO VALID PASSES. Every sweep either drifted to the E-cluster or\n"
               "failed the additivity check. The first passes after an idle\n"
               "period normally fail while the P-core ramps; re-run and let it\n"
               "warm up. Do not use any intercept printed above.\n"
               "use any intercept printed above.\n");
        free(t); return 1;
    }
    for (unsigned i = 0; i < nic; i++)
        for (unsigned j = i + 1; j < nic; j++)
            if (icepts[j] < icepts[i]) { double z = icepts[i]; icepts[i] = icepts[j]; icepts[j] = z; }
    printf("valid full-clock passes: %u of %u\n", nic, PASSES);
    printf("INSTRUMENTATION OFFSET  median %.0f %s   (range %.0f - %.0f)\n",
           icepts[nic/2], unit, icepts[0], icepts[nic-1]);
    if (!kperf)
        printf("\n  This is the cheap register-read path, NOT the rig's kperf offset.\n"
               "  Re-run under sudo -E.\n");
    free(t);
    return 0;
}
