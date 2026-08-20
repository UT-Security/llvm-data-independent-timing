/* dit_inv_bench.c - is constant-time safegcd modular inversion DIT-sensitive?
 *
 * Tests win-condition (b) from dit-finegrain-win-condition: is the secret
 * region itself cheap to protect? Prediction: yes, ~0%, because safegcd's
 * divstep loop is register-resident. No loads on the critical path means the
 * load value predictor has nothing to predict and the DMP has nothing to
 * chase, so DIT should have nothing to suppress.
 *
 * DESIGN, per dit-measurement-traps:
 *  - trap 7b: ONE binary, DIT toggled at runtime. No codegen lottery at all,
 *    which is the only measurement immune at ~1% effect sizes.
 *  - trap 5:  lvp_chase --mode const runs IN-BAND as a positive control. It
 *    must read ~4x on an M5 P-core or the rig is not measuring DIT and a null
 *    result is meaningless. lvp_chase --mode perm is the negative control.
 *  - trap 3:  paired round-robin interleaving, burn-in discarded.
 *  - trap 3:  any DIT-on ratio below 1.00x is an artifact, not a speedup.
 *
 * Subject: secp256k1_scalar_inverse, the constant-time safegcd inversion mod
 * the group order - i.e. exactly the operation whose variable-time BEEA
 * predecessor leaked ECDSA nonces in CVE-2016-7056.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "secp256k1.c"
#include "../include/secp256k1.h"
#include "assumptions.h"
#include "util.h"
#include "field_impl.h"
#include "group_impl.h"
#include "scalar_impl.h"

/* ---- DIT control ------------------------------------------------------- */

static inline void dit_set(int on) {
    if (on) __asm__ volatile("msr DIT, #1" ::: "memory");
    else    __asm__ volatile("msr DIT, #0" ::: "memory");
}

/* PSTATE.DIT is reported in bit 24 of the DIT system register. */
static inline uint64_t dit_read(void) {
    uint64_t v;
    __asm__ volatile("mrs %0, DIT" : "=r"(v));
    return (v >> 24) & 1;
}

static inline uint64_t now_ns(void) {
    return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
}

/* ---- subject: constant-time safegcd inversion -------------------------- */

/* Serial chain: each inversion consumes the previous one's result, so the
 * dependency structure matches ECDSA's k -> k^-1 rather than a throughput
 * loop the core could overlap. The +1 keeps values varying; without it
 * inv(inv(x)) == x would make the operand stream trivially predictable and
 * hand the value predictor a win that real code never gets. */
static uint64_t __attribute__((noinline))
bench_inv(secp256k1_scalar *state, size_t n) {
    secp256k1_scalar x = *state, out, one;
    secp256k1_scalar_set_int(&one, 1);
    for (size_t i = 0; i < n; i++) {
        secp256k1_scalar_inverse(&out, &x);
        secp256k1_scalar_add(&x, &out, &one);
    }
    *state = x;
    return x.d[0] ^ x.d[1] ^ x.d[2] ^ x.d[3];
}

/* Discriminator: same 256-bit limb arithmetic, no divstep loop. If this is
 * also DIT-sensitive the effect is generic bignum work; if it is flat the
 * effect is specific to safegcd's divstep structure. */
static uint64_t __attribute__((noinline))
bench_mul(secp256k1_scalar *state, size_t n) {
    secp256k1_scalar x = *state, out, one;
    secp256k1_scalar_set_int(&one, 1);
    for (size_t i = 0; i < n; i++) {
        secp256k1_scalar_mul(&out, &x, &x);
        secp256k1_scalar_add(&x, &out, &one);
    }
    *state = x;
    return x.d[0] ^ x.d[1] ^ x.d[2] ^ x.d[3];
}

/* The variable-time safegcd sibling: same algorithm, data-dependent exit.
 * This is the shape BEEA has and the shape CVE-2016-7056 exploited. */
static uint64_t __attribute__((noinline))
bench_inv_var(secp256k1_scalar *state, size_t n) {
    secp256k1_scalar x = *state, out, one;
    secp256k1_scalar_set_int(&one, 1);
    for (size_t i = 0; i < n; i++) {
        secp256k1_scalar_inverse_var(&out, &x);
        secp256k1_scalar_add(&x, &out, &one);
    }
    *state = x;
    return x.d[0] ^ x.d[1] ^ x.d[2] ^ x.d[3];
}

/* MECHANISM TEST. Hypothesis: the DIT cost comes from safegcd's constant-time
 * PADDING. Once f,g converge the trailing divsteps operate on limbs that are
 * 0 or -1 every iteration, so the loads feeding the serial chain return the
 * same values over and over - the value predictor's ideal input. DIT switches
 * that off and the padding has to be paid at full latency.
 *
 * If true, a SMALL input (converges early, long degenerate tail) must show a
 * LARGER DIT ratio than a full-width input (converges late, short tail).
 * Inputs vary every iteration in both arms, so neither is a repetition probe
 * of the kind that manufactured the fake +9.5% in trap 4. */
static uint64_t __attribute__((noinline))
bench_inv_width(secp256k1_scalar *state, size_t n, int wide) {
    secp256k1_scalar x, out; uint64_t acc = 0;
    uint64_t s = 0x243F6A8885A308D3ull;
    for (size_t i = 0; i < n; i++) {
        s ^= s << 13; s ^= s >> 7; s ^= s << 17;
        memset(&x, 0, sizeof x);
        if (wide) { x.d[0]=s; x.d[1]=s*0x9E3779B9ull; x.d[2]=s^0x5DEECE66Dull; x.d[3]=(s>>3)|1; }
        else      { x.d[0] = (s & 0xFFFF) | 1; }      /* 16-bit, still varying */
        secp256k1_scalar_inverse(&out, &x);
        acc ^= out.d[0] ^ out.d[3];
    }
    (void)state;
    return acc;
}

/* ---- positive/negative control: lvp_chase ------------------------------ */

/* Single static load, load-to-ADDRESS dependent, kept in asm so nothing can
 * restructure the chain. Verbatim shape from gem5-DIT/benchmarks/lvp_chase. */
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

static uint64_t rs = 0x9E3779B97F4A7C15ull;
static uint64_t rng(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return rs; }

int main(int argc, char **argv) {
    size_t reps      = 30;
    size_t burnin    = 5;
    size_t n_inv     = 20000;      /* ~25 ms ROI, per trap 3 sizing */
    size_t hops      = 20000000;
    size_t entries   = 4096;       /* 16 KB, L1-resident */

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--reps")   && i+1 < argc) reps   = (size_t)atol(argv[++i]);
        else if (!strcmp(argv[i], "--burnin") && i+1 < argc) burnin = (size_t)atol(argv[++i]);
        else if (!strcmp(argv[i], "--inv")    && i+1 < argc) n_inv  = (size_t)atol(argv[++i]);
        else if (!strcmp(argv[i], "--hops")   && i+1 < argc) hops   = (size_t)atol(argv[++i]);
    }

    /* verify the DIT bit actually moves before measuring anything */
    dit_set(1);
    uint64_t d_on = dit_read();
    dit_set(0);
    uint64_t d_off = dit_read();
    if (d_on != 1 || d_off != 0) {
        fprintf(stderr, "FATAL: DIT bit did not toggle (on=%llu off=%llu)\n",
                (unsigned long long)d_on, (unsigned long long)d_off);
        return 1;
    }
    fprintf(stderr, "DIT toggle verified: on=%llu off=%llu\n",
            (unsigned long long)d_on, (unsigned long long)d_off);

    /* const array: arr[C] == C, one address one value, maximally predictable */
    uint32_t *carr = malloc(entries * sizeof(uint32_t));
    uint32_t c = (uint32_t)(entries / 2);
    for (size_t i = 0; i < entries; i++) carr[i] = c;

    /* perm array: random permutation cycle, same footprint, nothing to predict */
    uint32_t *parr = malloc(entries * sizeof(uint32_t));
    {
        uint32_t *perm = malloc(entries * sizeof(uint32_t));
        for (size_t i = 0; i < entries; i++) perm[i] = (uint32_t)i;
        for (size_t i = entries - 1; i > 0; i--) {
            size_t j = rng() % (i + 1);
            uint32_t t = perm[i]; perm[i] = perm[j]; perm[j] = t;
        }
        for (size_t i = 0; i < entries; i++) parr[perm[i]] = perm[(i + 1) % entries];
        free(perm);
    }

    secp256k1_scalar seed;
    secp256k1_scalar_set_int(&seed, 0);
    seed.d[0] = 0x123456789abcdefull; seed.d[1] = 0xfedcba9876543210ull;
    seed.d[2] = 0x0f1e2d3c4b5a6978ull; seed.d[3] = 0x00ff00ff00ff00ffull;

    /* warm caches, train predictors, spin the CPU up out of low-power state */
    uint32_t wx = chase(carr, 0, entries * 4);
    wx ^= chase(parr, 0, entries * 4);
    { secp256k1_scalar w = seed; bench_inv(&w, 200); }

    printf("rep,arm,dit,ns,checksum\n");

    for (size_t r = 0; r < reps + burnin; r++) {
        int keep = (r >= burnin);
        size_t rr = keep ? r - burnin : 0;
        uint64_t t0, t1;
        uint64_t ck;

        /* --- subject, paired adjacent so drift hits both arms equally --- */
        for (int on = 0; on <= 1; on++) {
            secp256k1_scalar st = seed;
            dit_set(on);
            t0 = now_ns();
            ck = bench_inv(&st, n_inv);
            t1 = now_ns();
            dit_set(0);
            if (keep) printf("%zu,inv,%d,%llu,%llu\n", rr, on,
                             (unsigned long long)(t1-t0), (unsigned long long)ck);
        }

        for (int on = 0; on <= 1; on++) {
            secp256k1_scalar st = seed;
            dit_set(on);
            t0 = now_ns();
            ck = bench_inv_var(&st, n_inv);
            t1 = now_ns();
            dit_set(0);
            if (keep) printf("%zu,inv_var,%d,%llu,%llu\n", rr, on,
                             (unsigned long long)(t1-t0), (unsigned long long)ck);
        }

        for (int on = 0; on <= 1; on++) {
            secp256k1_scalar st = seed;
            dit_set(on);
            t0 = now_ns();
            ck = bench_mul(&st, n_inv * 20);   /* mul is ~20x cheaper; match ROI */
            t1 = now_ns();
            dit_set(0);
            if (keep) printf("%zu,mul,%d,%llu,%llu\n", rr, on,
                             (unsigned long long)(t1-t0), (unsigned long long)ck);
        }

        for (int wide = 0; wide <= 1; wide++) {
            for (int on = 0; on <= 1; on++) {
                secp256k1_scalar st = seed;
                dit_set(on);
                t0 = now_ns();
                ck = bench_inv_width(&st, n_inv, wide);
                t1 = now_ns();
                dit_set(0);
                if (keep) printf("%zu,inv_%s,%d,%llu,%llu\n", rr, wide?"wide":"small", on,
                                 (unsigned long long)(t1-t0), (unsigned long long)ck);
            }
        }

        /* --- positive control: must show ~4x --- */
        for (int on = 0; on <= 1; on++) {
            dit_set(on);
            t0 = now_ns();
            uint32_t x = chase(carr, c, hops);
            t1 = now_ns();
            dit_set(0);
            if (keep) printf("%zu,chase_const,%d,%llu,%llu\n", rr, on,
                             (unsigned long long)(t1-t0), (unsigned long long)x);
        }

        /* --- negative control: must stay flat --- */
        for (int on = 0; on <= 1; on++) {
            dit_set(on);
            t0 = now_ns();
            uint32_t x = chase(parr, 0, hops);
            t1 = now_ns();
            dit_set(0);
            if (keep) printf("%zu,chase_perm,%d,%llu,%llu\n", rr, on,
                             (unsigned long long)(t1-t0), (unsigned long long)x);
        }
        fflush(stdout);
    }

    fprintf(stderr, "warm checksum %u\n", wx);
    free(carr); free(parr);
    return 0;
}
