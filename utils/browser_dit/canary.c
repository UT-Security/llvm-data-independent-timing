/* canary.c - a ~1 second diagnostic run before/after each browser measurement.
 *
 * Does double duty:
 *
 *  1. THERMAL DRIFT. The PERM chase is a serial, L1-resident pointer chase, so
 *     its ns/hop tracks core frequency almost directly. If perm_ns drifts across
 *     a two-hour sweep, the machine throttled and the A/B numbers around the
 *     drift are suspect. This is the check that `pmset -g therm` cannot do
 *     (it reports nothing on this machine).
 *
 *  2. POSITIVE CONTROL FOR DIT. The CONST chase is value-predictable, so the
 *     Load Value Predictor collapses it to ~1 cyc/hop; setting PSTATE.DIT
 *     disables the LVP and it falls back onto the PERM line. If const_dit_ns is
 *     not close to perm_ns, DIT is not actually taking effect and every browser
 *     number in the sweep is meaningless. See docs/research/value-timing-leaks.md.
 *
 * Prints one JSON object on stdout.
 */

#include <mach/mach_time.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define N 1024u          /* 4 KB, comfortably L1-resident */
#define HOPS 20000000u

static uint32_t arr[N];

static inline void dit_on(void) { __asm__ volatile("msr DIT, #1" ::: "memory"); }
static inline void dit_off(void) { __asm__ volatile("msr DIT, #0" ::: "memory"); }

static double now_ns(void) {
    static mach_timebase_info_data_t tb;
    if (tb.denom == 0)
        mach_timebase_info(&tb);
    return (double)mach_absolute_time() * tb.numer / tb.denom;
}

/* Serial: each load's address depends on the previous load's value. */
static double chase(unsigned hops) {
    uint32_t x = 0;
    double t0 = now_ns();
    for (unsigned i = 0; i < hops; i++)
        x = arr[x];
    double t1 = now_ns();
    __asm__ volatile("" ::"r"(x));
    return (t1 - t0) / hops;
}

static void fill_const(void) {
    for (unsigned i = 0; i < N; i++)
        arr[i] = 0;
}

static void fill_perm(void) {
    for (unsigned i = 0; i < N; i++)
        arr[i] = i;
    /* Fisher-Yates with a fixed seed: identical permutation every invocation,
     * so runs are comparable across the sweep. */
    unsigned s = 12345u;
    for (unsigned i = N - 1; i > 0; i--) {
        s = s * 1664525u + 1013904223u;
        unsigned j = s % (i + 1);
        uint32_t t = arr[i];
        arr[i] = arr[j];
        arr[j] = t;
    }
}

#define ROUNDS 5

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static double median(double *v, int n) {
    qsort(v, n, sizeof *v, cmp_double);
    return n & 1 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

int main(void) {
    double perm[ROUNDS], cst[ROUNDS], cst_dit[ROUNDS];

    /* The first measurement after process start reads high (cold caches, clock
     * ramp), and DIT has been observed once in ~8 runs to not gate the LVP at
     * all. Both are outliers, not signal, so take the median of several rounds
     * after discarding a warm-up rather than aborting a two-hour sweep on one
     * bad reading. */
    fill_perm();
    chase(HOPS / 2);

    for (int i = 0; i < ROUNDS; i++) {
        fill_perm();
        perm[i] = chase(HOPS);

        fill_const();
        chase(HOPS / 10);
        cst[i] = chase(HOPS);

        dit_on();
        cst_dit[i] = chase(HOPS);
        dit_off();
    }

    double p = median(perm, ROUNDS);
    double cd = median(cst_dit, ROUNDS);

    /* Use the MINIMUM for the LVP-on baseline, not the median.
     *
     * Noise on this machine only ever inflates a measurement (scheduling,
     * interrupts, migration), so the minimum is the cleanest estimate of the
     * true LVP-predicting time, while the median drags upward. Dividing by a
     * per-round const value produced a ratio that dipped below 2 whenever the
     * BASELINE hiccuped, and that was misread as "DIT intermittently fails to
     * gate the LVP". It does not: across 800 measured rounds the DIT-on time
     * never once fell below 0.868 ns/hop, while the DIT-off baseline was seen
     * as high as 0.40 against a 0.217 floor. Gate on absolute times, never on a
     * ratio with a noisy denominator. */
    double c = cst[0];
    for (int i = 1; i < ROUNDS; i++)
        if (cst[i] < c)
            c = cst[i];

    /* The real invariant: with DIT set, the const chase should land on the perm
     * line, because the LVP is off and both are then plain L1 load-to-use. Both
     * of those are the SLOW measurements, so noise inflates rather than deflates
     * them and this comparison is robust. 1.0 = perfect. */
    double lands_on_perm = cd / p;

    printf("{\"perm_ns\":%.4f,\"const_ns\":%.4f,\"const_dit_ns\":%.4f,"
           "\"lvp_ratio\":%.3f,\"dit_effect\":%.3f,\"dit_lands_on_perm\":%.3f,"
           "\"rounds\":%d}\n",
           p, c, cd, p / c, cd / c, lands_on_perm, ROUNDS);
    return 0;
}
