/* sodium_payload.c - the SECRET lane, built on libsodium.
 *
 * WHY LIBSODIUM, GIVEN IT IS THE PROJECT'S CLEAREST NEGATIVE. The 2026-08-03
 * result (+46% ed25519 .. +94% AEAD) measured the primitives ALONE - f = 1, no
 * public code at all. With no public code the prize is zero by construction and
 * the pass can only lose; that is the "bulk crypto as the whole workload"
 * anti-pattern, not a property of libsodium. What libsodium actually offers is
 * the opposite: its primitives are DIT-INSENSITIVE (c_S ~ 0.1% measured across
 * 13 of them), which is condition (b) satisfied - protecting the secret is
 * nearly free, so nearly all of blanket's cost is recoverable. The failure was
 * never dwell, it was toggle count.
 *
 * AND IT SPANS THE GRANULARITY AXIS WITH REAL CODE. Poly1305 at ~0.1 us through
 * Argon2id at ~100 ms is six orders of magnitude of work-per-region, every point
 * of it a primitive deployed software actually calls - and for the AEAD, R is
 * simply the message size, which is a deployment parameter rather than a knob.
 *
 * ANNOTATION. The seed is CIO parity (counter-optimization/cio's published
 * libsodium config, pointee-typed). Note that it declares the PLAINTEXT and its
 * LENGTH secret, not just the key - a much wider source set than SQLCipher's
 * key-only annotation. That is deliberate and it is also a free experiment: the
 * annotation set is itself a dial on tau, which is exactly the ablation Serberus
 * (S&P'24) ran when widening its taint sources tripled its overhead.
 *
 * COVERAGE AUDIT (trap 8 - an under-protecting oracle looks exactly like a win).
 * The key and the plaintext are filled ONCE in secret_init, before any timing
 * starts. Inside the measured region the only thing that touches them is the
 * libsodium call itself, between the oracle's enable and disable. So the oracle
 * covers 100% of secret-touching work in the region by construction.
 */

#include "sodium_payload.h"

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "sodium.h"

static int    g_mode = DIT_OFF;
static int    g_prim = PRIM_AEAD;
static size_t g_msglen;
static unsigned long g_ops;
static size_t g_mem;

static unsigned char  g_key[64];        /* THE SECRET */
static unsigned char  g_sk[64];         /* THE SECRET (Ed25519) */
static unsigned char  g_nonce[24];
static unsigned char  g_salt[16];
static unsigned char *g_msg;            /* plaintext - secret under CIO's model */
static unsigned char *g_out;
static unsigned char  g_mac[16];
static unsigned long  g_toggles, g_ops_done;
static double         g_last_op_us;

static inline void dit_on(void)  { __asm__ volatile("msr DIT, #1" ::: "memory"); }
static inline void dit_off(void) { __asm__ volatile("msr DIT, #0" ::: "memory"); }

static inline unsigned long dit_read(void) {
    unsigned long d;
    __asm__ volatile("mrs %0, DIT" : "=r"(d));
    return (d >> 24) & 1UL;
}

#ifdef GEM5_NO_SELF_TIMING
/* gem5 SE returns SIMULATED time from clock_gettime, so self-timing makes the
 * run depend on its own cycle count and it stops being a deterministic replay:
 * simInsts then differs between machine configs for an identical binary, because
 * a timing-derived value printed with %%.3f/%%.4f emits different digits and
 * therefore different work. Measured residual before this guard: 1.4e-6 relative
 * (dit-gem5-composite.md sec 3 is the same defect on the secp composite).
 *
 * With the guard on, f and R come from DIFFERENCING against an nper=0 run of the
 * same binary, which is exact because gem5 is deterministic:
 *     f = (cyc - cyc_nocrypto) / cyc
 *     R = (cyc - cyc_nocrypto) / ops / freq
 * so nothing is lost -- the in-run timer was only ever a cross-check. */
static double now_s(void) { return 0.0; }
#else
static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
#endif

int secret_init(int mode, int prim, size_t msgsize, unsigned long ops, size_t mem) {
    g_mode = mode; g_prim = prim; g_msglen = msgsize; g_ops = ops; g_mem = mem;

    if (sodium_init() < 0) return -1;

    for (size_t i = 0; i < sizeof g_key;  i++) g_key[i]  = (unsigned char)(0x40 + i * 7);
    for (size_t i = 0; i < sizeof g_nonce;i++) g_nonce[i]= (unsigned char)(i * 13 + 1);
    for (size_t i = 0; i < sizeof g_salt; i++) g_salt[i] = (unsigned char)(i * 29 + 5);

    size_t buf = msgsize ? msgsize : 64;
    g_msg = (unsigned char *)malloc(buf + 64);
    g_out = (unsigned char *)malloc(buf + 128);
    if (!g_msg || !g_out) return -1;
    for (size_t i = 0; i < buf; i++) g_msg[i] = (unsigned char)(i * 31 + 7);

    if (prim == PRIM_SIGN) {
        unsigned char pk[32];
        crypto_sign_seed_keypair(pk, g_sk, g_key);   /* derived once, outside the ROI */
    }

    /* Measure R once, so the region size is reported rather than assumed. */
    double t0 = now_s();
    int saved = g_mode; g_mode = DIT_OFF;
    secret_work_n(prim == PRIM_PWHASH ? 1 : 32);
    g_last_op_us = (now_s() - t0) * 1e6 / (prim == PRIM_PWHASH ? 1 : 32);
    g_mode = saved; g_ops_done = 0; g_toggles = 0;

    if (g_mode == DIT_ALWAYS) { dit_on(); g_toggles++; }
    return 0;
}

static unsigned long one_op(void) {
    unsigned long long clen = 0;
    switch (g_prim) {
    case PRIM_AUTH:
        crypto_onetimeauth(g_mac, g_msg, g_msglen, g_key);
        return g_mac[0];
    case PRIM_AEAD:
        crypto_aead_chacha20poly1305_ietf_encrypt(
            g_out, &clen, g_msg, g_msglen, NULL, 0, NULL, g_nonce, g_key);
        return (unsigned long)clen + g_out[0];
    case PRIM_GCM:
        crypto_aead_aes256gcm_encrypt(
            g_out, &clen, g_msg, g_msglen, NULL, 0, NULL, g_nonce, g_key);
        return (unsigned long)clen + g_out[0];
    case PRIM_SIGN: {
        unsigned long long smlen = 0;
        crypto_sign(g_out, &smlen, g_msg, g_msglen, g_sk);
        return (unsigned long)smlen + g_out[0];
    }
    case PRIM_PWHASH:
        crypto_pwhash_argon2id(g_out, 32, (const char *)g_msg, g_msglen,
                               g_salt, g_ops, g_mem,
                               crypto_pwhash_argon2id_ALG_ARGON2ID13);
        return g_out[0];
    }
    return 0;
}

unsigned long secret_work_n(int n) {
    unsigned long acc = 0;

    if (g_mode == DIT_ORACLE_BATCH) { dit_on(); g_toggles++; }

    for (int i = 0; i < n; i++) {
        /* Vary the nonce so successive operations are not identical work - a
         * probe that replays one input manufactures its own predictability. */
        g_nonce[0] = (unsigned char)(g_ops_done + i);

        if (g_mode == DIT_ORACLE) { dit_on(); g_toggles++; }
        acc += one_op();
        if (g_mode == DIT_ORACLE) { dit_off(); g_toggles++; }

        g_ops_done++;
    }

    if (g_mode == DIT_ORACLE_BATCH) { dit_off(); g_toggles++; }
    return acc;
}

const char *secret_prim_name(void) {
    switch (g_prim) {
    case PRIM_AUTH:   return "poly1305";
    case PRIM_AEAD:   return "chacha20poly1305";
    case PRIM_GCM:    return "aes256gcm";
    case PRIM_SIGN:   return "ed25519";
    case PRIM_PWHASH: return "argon2id";
    }
    return "?";
}

double        secret_last_op_us(void) { return g_last_op_us; }
unsigned long secret_toggles(void)    { return g_toggles; }
unsigned long secret_count(void)      { return g_ops_done; }
unsigned long secret_dit_now(void)    { return dit_read(); }
