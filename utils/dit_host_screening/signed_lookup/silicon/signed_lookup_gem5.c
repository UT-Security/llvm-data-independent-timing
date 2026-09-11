/* Signed-lookup: a secret-fraction crossover on libsodium.
 *
 * One request = a PUBLIC lane that gathers records from a lookup table, then a
 * SECRET lane that seals the gathered record. The knob is lookups-per-record,
 * which moves only the ratio between the two lanes.
 *
 *   PUBLIC   lookup_lane()      - L value-dependent loads into a 512 KB table,
 *                                 chained so each address depends on the last.
 *                                 Never touches the key.
 *   SECRET   crypto_aead_chacha20poly1305_ietf_encrypt() of a 100-byte record
 *                                 carrying the digest: the same op and message
 *                                 size as experiment 09's CIO driver. ~2.2k
 *                                 cycles and 49 committed switches per op
 *                                 under the pass. Seeds are the CIO-parity set
 *                                 verbatim,
 *                                 benchmarks/crypto/libsodium_secret.txt,
 *                                 which needs the chacha20_ref.c rename patch
 *                                 that build_native_sodium.sh applies.
 *
 *   The secret op used to be an ed25519 signature (crypto_sign). Retired
 *   2026-09-03: at ~74k cycles and 52 switches per op its toggle rate is two
 *   orders of magnitude too low for the switch IMPLEMENTATION (serialising vs
 *   renamed MSR DIT) to register. Measured under gem5, serialising cost +2 pp
 *   at best and vanished into layout noise below f=70%. The AEAD op puts the
 *   same flow in experiment 09's regime. On the lane described below (gem5,
 *   IPC overhead, median of 5 stack offsets): serialising placement +41% at
 *   f=97%, renamed placement within 1% of unhardened everywhere, blanket
 *   climbing from +7% to +31% as the public lane grows, the two solid curves
 *   crossing near f=50%.
 *
 *   The pass and nop arms are built with -fno-optimize-sibling-calls
 *   (build_native_sodium.sh). A tail call has no epilogue, so DIT enabled
 *   before one is never cleared and selective placement silently degenerates
 *   to blanket; the signing driver measured exactly that (pub_dit=1.000)
 *   through crypto_sign's two-instruction forwarder. The AEAD entry point
 *   returns normally, but the disable stays, and the pub_dit gate below still
 *   catches a library built without it.
 *
 * WHY THE INDEX IS DERIVED FROM THE REQUEST ID, not a fixed chain. A repeating
 * pointer chase (i = next[i] over a fixed permutation) is memorised by VTAGE
 * after one pass, which is exactly why lvp_chase reports a 4.0x prize against
 * 1-2% on real code. Every request walks a different path, so the value
 * predictor has to work for its wins the way it does on real data structures.
 *
 * WHY THE CHAIN IS HASHED (fixed 2026-09-03). The first version filled the
 * table with a linear function of the index and mixed with acc*5+1. On the low
 * bits that form the index that composed map has an EVEN multiplier (406 mod
 * 512) and contracts to a fixed point within 9 steps for every request id:
 * entry 446 at tblbits=9. After that every lookup loaded the same address and
 * value, the stride predictor predicted it at 99.95%, and blanket's whole
 * public-lane cost (+127% gem5, +32% M5) was one constant load losing its
 * predictor - the value predictor's best case, not a pointer chase. Now the
 * table holds splitmix64 hashes, the index comes from the HIGH bits of the
 * 64-bit state, and the loaded value is folded back in by xor-multiply-shift,
 * so no low-bit subsystem exists to contract: over 20,000 steps the chain
 * visits all 512 entries, never repeats an index more than twice running, has
 * no period inside a request, and the loaded-value stride never repeats. The
 * chain is still serial (each address needs the previous load) and still
 * L1-resident. Numbers taken before this fix are not comparable to numbers
 * taken after it.
 *
 * THREE ITERATIONS IN FOUR CARRY AN LVP-PREDICTABLE LOAD. The M4/M5 have a
 * load value predictor because real code is full of loads that return the same
 * value at the same PC (FLOP: loop bounds, base pointers, headers), and DIT
 * switches it off. So in three iterations of four the record's HEADER is read
 * first - data-dependent address, constant value, the way every record's
 * type/length field is the same - and the data address depends on it. With the
 * LVP the header's value is predicted and the data load issues at once; under
 * DIT the header is one more serial L1 hit per iteration.
 *
 * Why three in four: blanket's public-lane cost is LINEAR in that fraction
 * (gem5, 5 stack offsets, 2026-09-03: at L=20000, +0.0 / +11.2 / +25.1 / +31.4
 * / +40.5% for q = 0, .25, .5, .75, 1; at L=200, +2.1 / +8.6 / +15.0 / +20.2 /
 * +27.1%), renamed placement is free at every q, and serialising placement's
 * cost does not depend on q at all. The lane is fixed at q=0.75, and
 * --predictable q4 (q = q4/4, default 3) stays only as the override that
 * reproduces that sensitivity sweep. q=0 is a pure hashed chase where blanket
 * is free; q=1 is the regime the constant-chain bug had put the whole lane in.
 * Where real applications sit on this axis is what FLOP's counts of
 * constant-trained loads and experiment 01's real public lane say.
 *
 * f_secret is MEASURED, not assumed: --nosecret runs the public lane alone,
 * and f = (full - public_only) / full. Same method as paper_experiments/01.
 *
 * ROI is the request loop only, so warmup and table fill are excluded.
 */
#include <sodium.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TBL_MAX_BITS 16
/* record values: hashed, unpredictable */
static uint64_t tbl[1u << TBL_MAX_BITS];
/* record headers: all the same value, LVP-predictable */
static uint64_t hdr[1u << TBL_MAX_BITS];
#define HDR_CONST 0x2545F4914F6CDD1DULL
/* --predictable: the header is read in (q4/4) of iterations; default 3/4 */
static uint32_t pred_q4 = 3;
static uint32_t tbl_mask = (1u << 9) - 1u;   /* set from --tblbits */
static volatile uint64_t sink;
/* requests entering the PUBLIC lane with DIT set */
static uint64_t pub_dit_on;

/* Dual-target. Under gem5 the ROI is delimited by m5 pseudo-ops; natively it
 * is timed with a monotonic clock. Same source, same loop, so the native
 * screen and the simulator measure the same region.
 *
 * m5op encodings are 0xff<op>0110: RESET_STATS 0x40, DUMP_RESET_STATS 0x42
 * (include/gem5/asm/generic/m5ops.h). */
#ifdef GEM5_BUILD
static inline void roi_begin(void) {
    register long a0 __asm__("x0") = 0; register long a1 __asm__("x1") = 0;
    __asm__ volatile(".inst 0xff400110" :: "r"(a0), "r"(a1) : "memory");
}
static inline void roi_end(void) {
    register long a0 __asm__("x0") = 0; register long a1 __asm__("x1") = 0;
    __asm__ volatile(".inst 0xff420110" :: "r"(a0), "r"(a1) : "memory");
}
static double roi_ns(void) { return 0.0; }
/* Shadow-taint oracle (experiment 10's frontier applied to this flow). Inert
 * unless -DTAINT_ORACLE: the normal gem5 arms are byte-identical without it.
 * RESERVED1 0x55 seeds, RESERVED2 0x56 reports. */
#ifdef TAINT_ORACLE
static inline void m5_taint_seed(const void *p, unsigned long n) {
    register unsigned long a0 __asm__("x0") = (unsigned long) p;
    register unsigned long a1 __asm__("x1") = n;
    __asm__ volatile(".inst 0xff550110" :: "r"(a0), "r"(a1) : "memory");
}
static inline void m5_taint_report(void) {
    __asm__ volatile(".inst 0xff560110" ::: "memory", "x0");
}
#else
static inline void m5_taint_seed(const void *p, unsigned long n)
{
    (void)p;
    (void)n;
}
static inline void m5_taint_report(void) {}
#endif
#else
static inline void m5_taint_seed(const void *p, unsigned long n)
{
    (void)p;
    (void)n;
}
static inline void m5_taint_report(void) {}
#include <time.h>

#include "kperf_ipc.h"

static struct timespec t0, t1;
static inline void roi_begin(void) {
    kperf_begin(); clock_gettime(CLOCK_MONOTONIC, &t0);
}
static inline void roi_end(void) {
    clock_gettime(CLOCK_MONOTONIC, &t1); kperf_end();
}
static double roi_ns(void) {
    return (t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec);
}
/* Read PSTATE.DIT so an arm can prove which mode it actually ran in. */
static inline unsigned long dit_now(void) {
    unsigned long d;
    __asm__ volatile("mrs %0, DIT" : "=r"(d));
    return (d >> 24) & 1UL;
}
#endif

/* splitmix64 finalizer: table contents and the per-request seed. */
static inline uint64_t mix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

/* PUBLIC. Serial, value-dependent: the next address cannot be formed until the
 * previous load returns. That is the shape EVES and the DMP exist for, and the
 * shape coin selection has in paper_experiments/01. See "WHY THE CHAIN IS
 * HASHED" above for why the index comes from the high bits. */
__attribute__((noinline)) static uint64_t lookup_lane(uint64_t reqid, int L) {
    uint64_t acc = mix64(reqid);
    for (int j = 0; j < L; j++) {
        /* value-dependent load address */
        uint32_t idx = (uint32_t)(acc >> 40) & tbl_mask;
        if ((uint32_t)(j & 3) < pred_q4) {
            /* constant value, address from the chain: LVP-predictable */
            uint64_t h = hdr[idx];
            /* the data address depends on the header value */
            idx = (uint32_t)((acc ^ h) >> 24) & tbl_mask;
        }
        /* hashed value: unpredictable; fold it into all 64 bits */
        acc = (acc ^ tbl[idx]) * 0x9E3779B97F4A7C15ULL;
        acc ^= acc >> 29;
    }
    return acc;
}

int main(int argc, char **argv) {
    int iter = 200, warmup = 50, L = 64, dosecret = 1, tblbits = 9;
    int blanket = 0;
    /* --chunks N: finer interleaving. Each request's L lookups and its 100-byte
     * AEAD are split into N pieces, alternating L/N lookups with one AEAD call
     * over a 100/N-byte slice (its own nonce), so the secret work is the same
     * bytes in N calls and the public work the same lookups in N runs. N=1 is
     * the request as it always was. Per-call placements (the Apple bracket,
     * the pass's per-entry switches) pay N times; blanket does not. */
    int chunks = 1;
    for (int i = 1; i < argc; i++) {
        int more = i + 1 < argc;
        if (!strcmp(argv[i], "--iter") && more)
            iter = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--warmup") && more)
            warmup = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--lookups") && more)
            L = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--tblbits") && more)
            tblbits = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--nosecret"))
            dosecret = 0;   /* public lane only */
        else if (!strcmp(argv[i], "--predictable") && more) {
            int q = atoi(argv[++i]);
            pred_q4 = q < 0 ? 0 : q > 4 ? 4 : (uint32_t)q;
        }
        /* In-process blanket DIT. Equivalent to the dit_on.dylib constructor
         * used by paper_experiments/01, but survives sudo, which sanitises
         * DYLD_* and would otherwise silently drop the blanket arm. The single
         * MSR executes before the ROI, so no instruction inside the measured
         * region differs between arms. */
        else if (!strcmp(argv[i], "--blanket")) blanket = 1;
        else if (!strcmp(argv[i], "--chunks") && more) {
            chunks = atoi(argv[++i]);
            if (chunks < 1) chunks = 1;
        }
    }
    if (sodium_init() < 0) return 1;
#ifndef GEM5_BUILD
    if (blanket) __asm__ volatile("msr DIT, #1" ::: "memory");
#endif
#ifndef GEM5_BUILD
    kperf_init();   /* silently degrades to ipc=na without root */
#endif

    if (tblbits < 4) tblbits = 4;
    if (tblbits > TBL_MAX_BITS) tblbits = TBL_MAX_BITS;
    tbl_mask = (1u << tblbits) - 1u;
    for (uint32_t i = 0; i <= tbl_mask; i++)
        { tbl[i] = mix64(i); hdr[i] = HDR_CONST; }

    /* SECRET lane: 100-byte message (experiment 09's size), the request's
     * digest in its first 8 bytes and the request id as the nonce. */
    enum { AEAD_MLEN = 100 };
    unsigned char ak[crypto_aead_chacha20poly1305_ietf_KEYBYTES];
    unsigned char amsg[AEAD_MLEN];
    unsigned char act[AEAD_MLEN + crypto_aead_chacha20poly1305_ietf_ABYTES];
    unsigned char npub[crypto_aead_chacha20poly1305_ietf_NPUBBYTES];
    unsigned long long actlen = 0;
    crypto_aead_chacha20poly1305_ietf_keygen(ak);
    /* THE SEED: the AEAD key. Everything the oracle later counts as secret is
     * derived from it, so the public lane's chain - which never touches ak -
     * stays public and the measurement is of the secret lane's reach. */
    m5_taint_seed(ak, sizeof ak);
    for (int i = 0; i < AEAD_MLEN; i++) amsg[i] = (unsigned char)i;
    memset(npub, 0, sizeof npub); memset(act, 0, sizeof act);
    /* One secret call over the slice [c*clen, (c+1)*clen) of the message, the
     * request's digest in its first bytes and (reqid, c) as the nonce. With
     * chunks == 1 this is the request as it always was: the whole message,
     * the digest in its first 8 bytes, reqid as the nonce. */
    const int clen = AEAD_MLEN / chunks;
#define SECRET_OP(d, reqid, c) do {                                     \
        uint64_t r_ = (reqid) * (uint64_t)chunks + (uint64_t)(c);       \
        unsigned char *m_ = amsg + (c) * clen;                          \
        memcpy(m_, &(d), clen < 8 ? clen : 8); memcpy(npub, &r_, 8);    \
        crypto_aead_chacha20poly1305_ietf_encrypt(act, &actlen, m_,     \
                                                  (unsigned long long)clen, \
                                                  NULL, 0, NULL, npub, ak); \
    } while (0)
    /* A request: L/chunks lookups, then one secret call, chunks times. */
#define REQUEST(reqid) do {                                             \
        uint64_t d_ = 0;                                                \
        for (int c_ = 0; c_ < chunks; c_++) {                           \
            d_ ^= lookup_lane((reqid) * (uint64_t)chunks + (uint64_t)c_, \
                              L / chunks);                              \
            if (dosecret) SECRET_OP(d_, (reqid), c_);                   \
        }                                                               \
        sink += d_ + act[0];                                            \
    } while (0)
    for (int i = 0; i < warmup; i++)
        REQUEST((uint64_t)i);
    roi_begin();
    for (int i = 0; i < iter; i++) {
#ifndef GEM5_BUILD
        pub_dit_on += dit_now();      /* validity gate, see pub_dit= below */
#endif
        REQUEST((uint64_t)i + 1000000);
    }
    roi_end();
    m5_taint_report();

#ifdef GEM5_BUILD
    printf("signed_lookup iter=%d warmup=%d lookups=%d tblbits=%d pred=%u "
           "secret=%d chunks=%d sink=%llu\n",
           iter, warmup, L, tblbits, pred_q4, dosecret, chunks,
           (unsigned long long)sink);
#else
    if (kperf_ok)
        printf("signed_lookup iter=%d warmup=%d lookups=%d tblbits=%d "
               "pred=%u secret=%d dit=%lu ns=%.0f cycles=%llu insts=%llu "
               "ipc=%.4f pub_dit=%.3f sink=%llu\n",
               iter, warmup, L, tblbits, pred_q4, dosecret, dit_now(),
               roi_ns(), (unsigned long long)kperf_cycles(),
               (unsigned long long)kperf_insts(),
               (double)kperf_insts() / (double)kperf_cycles(),
               (double)pub_dit_on / (double)iter, (unsigned long long)sink);
    else
        printf("signed_lookup iter=%d warmup=%d lookups=%d tblbits=%d "
               "pred=%u secret=%d dit=%lu ns=%.0f ipc=na pub_dit=%.3f "
               "sink=%llu\n",
               iter, warmup, L, tblbits, pred_q4, dosecret, dit_now(),
               roi_ns(),
               (double)pub_dit_on / (double)iter, (unsigned long long)sink);
#endif
    return 0;
}
