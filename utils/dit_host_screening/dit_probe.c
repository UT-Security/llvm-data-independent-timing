/* dit_probe.c - in-band positive/negative control for the DIT screening sweep.
 *
 * CRITICAL: this program never touches PSTATE.DIT itself. Its timing depends
 * entirely on whether the injected dylib set DIT, so running it under
 * dit_off.dylib vs dit_on.dylib validates the ENTIRE injection path end to end,
 * exactly the way every host in the sweep is measured.
 *
 * const  - value-predictable chase. LVP collapses it to ~0.22 ns/hop; with DIT
 *          set the LVP is off and it falls back onto the perm line (~0.87).
 *          Must read ~4x between arms or the sweep is measuring nothing
 *          (dit-measurement-traps trap 5).
 * perm   - random permutation cycle, same footprint, nothing to predict. Must
 *          stay flat between arms. This is the negative control.
 *
 * The robust gate (trap 6) is const-under-DIT / perm-under-DIT, which must land
 * in 0.997-1.003: it is a ratio of two SLOW measurements, so noise cannot fake
 * a pass.
 */

#include <mach/mach_time.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ENTRIES 4096u     /* 16 KB, comfortably L1-resident */

static uint64_t rs = 0x9E3779B97F4A7C15ull;
static uint64_t rng(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return rs; }

static double now_ns(void) {
    static mach_timebase_info_data_t tb;
    if (tb.denom == 0) mach_timebase_info(&tb);
    return (double)mach_absolute_time() * tb.numer / tb.denom;
}

/* Single static load, load-to-ADDRESS dependent, in asm so nothing can
 * restructure the chain. */
static uint32_t __attribute__((noinline))
chase(const uint32_t *arr, uint32_t x, uint64_t n) {
    __asm__ volatile(
        "1:\n"
        "ldr %w[x], [%[a], %w[x], uxtw #2]\n"
        "subs %[n], %[n], #1\n"
        "b.ne 1b\n"
        : [x] "+r"(x), [n] "+r"(n)
        : [a] "r"(arr)
        : "cc", "memory");
    return x;
}

/* Read PSTATE.DIT so the probe reports what it actually ran under, rather than
 * leaving us to infer it from timing. */
static unsigned long dit_get(void) {
    unsigned long d;
    __asm__ volatile("mrs %0, DIT" : "=r"(d));
    return (d >> 24) & 1UL;
}

int main(int argc, char **argv) {
    uint64_t hops = 20000000;
    if (argc > 1) hops = (uint64_t)atoll(argv[1]);

    uint32_t *carr = malloc(ENTRIES * sizeof(uint32_t));
    uint32_t *parr = malloc(ENTRIES * sizeof(uint32_t));
    uint32_t c = ENTRIES / 2;
    for (unsigned i = 0; i < ENTRIES; i++) carr[i] = c;
    {
        uint32_t *perm = malloc(ENTRIES * sizeof(uint32_t));
        for (unsigned i = 0; i < ENTRIES; i++) perm[i] = i;
        for (unsigned i = ENTRIES - 1; i > 0; i--) {
            unsigned j = (unsigned)(rng() % (i + 1));
            uint32_t t = perm[i]; perm[i] = perm[j]; perm[j] = t;
        }
        for (unsigned i = 0; i < ENTRIES; i++) parr[perm[i]] = perm[(i + 1) % ENTRIES];
        free(perm);
    }

    uint32_t w = chase(carr, 0, ENTRIES * 4);
    w ^= chase(parr, 0, ENTRIES * 4);

    double t0 = now_ns(); uint32_t x1 = chase(carr, c, hops); double t1 = now_ns();
    double t2 = now_ns(); uint32_t x2 = chase(parr, 0, hops); double t3 = now_ns();

    printf("PROBE dit=%lu const_ns_per_hop=%.4f perm_ns_per_hop=%.4f const_over_perm=%.4f ck=%u\n",
           dit_get(), (t1 - t0) / hops, (t3 - t2) / hops,
           ((t1 - t0) / hops) / ((t3 - t2) / hops), x1 ^ x2 ^ w);
    free(carr); free(parr);
    return 0;
}
