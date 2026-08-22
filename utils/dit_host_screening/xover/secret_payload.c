/* secret_payload.c - the SECRET lane and the VERIFY lane of the crossover
 * composite.
 *
 * Extends utils/dit_host_screening/secret/secret_payload.c with the two things
 * the crossover sweep needs:
 *
 *   1. DIT_ORACLE_BATCH - toggles once around a whole batch instead of once per
 *      signature. With `sigs` this gives independent control of the work per DIT
 *      region (R) while the secret fraction (f) is held fixed by `period`.
 *
 *   2. public_verify_n() - real secp256k1_ecdsa_verify over PUBLIC data. It
 *      holds no secret, so every DIT switch the pass puts in it is a false
 *      positive, and the verify rate becomes a dial on phi. This is the shape
 *      CT-Wasm sec 6.4.1 reports (verification poisoned by helpers shared with
 *      signing, fixed by hand-copying at +85% code size) and the shape that cost
 *      Bitcoin Core's ConnectBlockAllEcdsa +51%.
 *
 * COVERAGE AUDIT (trap 8 - an under-protecting oracle looks exactly like a win).
 * The secret key g_seckey is read ONLY inside secret_sign_n, between the enable
 * and the disable, and once inside secret_init to derive the public key before
 * the ROI opens. Nothing else in the composite reads it. So the oracle covers
 * 100% of secret-touching work inside the ROI by construction, not by
 * inspection.
 */

#include "secret_payload.h"

#include <stdint.h>
#include <string.h>

#include "secp256k1.h"

/* Distinct pre-signed messages for the verify lane. Cycling through several
 * avoids dit-measurement-traps trap 4, where a probe replaying one input
 * manufactures its own value-predictability. */
#define VERIFY_POOL 64

static secp256k1_context *g_ctx;
static unsigned char      g_seckey[32];        /* THE SECRET */
static secp256k1_pubkey   g_pubkey;            /* public */
static secp256k1_ecdsa_signature g_sigs[VERIFY_POOL];  /* public */
static unsigned char      g_msgs[VERIFY_POOL][32];     /* public */
static int                g_mode = DIT_OFF;
static unsigned long      g_toggles;
static unsigned long      g_signs;
static unsigned long      g_verifies;
static unsigned long      g_vidx;

static inline void dit_on(void)  { __asm__ volatile("msr DIT, #1" ::: "memory"); }
static inline void dit_off(void) { __asm__ volatile("msr DIT, #0" ::: "memory"); }

static inline unsigned long dit_read(void) {
    unsigned long d;
    __asm__ volatile("mrs %0, DIT" : "=r"(d));
    return (d >> 24) & 1UL;
}

void secret_init(int mode) {
    g_mode = mode;
    g_ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);

    for (int i = 0; i < 32; i++)
        g_seckey[i] = (unsigned char)(0x40 + i * 7);
    g_seckey[31] |= 1;

    /* Derive the public key and a pool of signatures ONCE, before the ROI.
     * This is genuine secret work, which is why it is deliberately outside the
     * measured region - see dit-real-app-coincurve-timing, where per-signature
     * key derivation inside the workload was half the secret work and an oracle
     * that missed it read as a win. */
    secp256k1_ec_pubkey_create(g_ctx, &g_pubkey, g_seckey);
    for (int i = 0; i < VERIFY_POOL; i++) {
        for (int j = 0; j < 32; j++)
            g_msgs[i][j] = (unsigned char)(i * 131 + j * 17 + 3);
        secp256k1_ecdsa_sign(g_ctx, &g_sigs[i], g_msgs[i], g_seckey, NULL, NULL);
    }

    if (g_mode == DIT_ALWAYS) {
        dit_on();
        g_toggles++;
    }
}

unsigned long secret_sign_n(int n) {
    unsigned long acc = 0;
    unsigned char msg[32];
    secp256k1_ecdsa_signature sig;
    unsigned char out[72];
    size_t outlen;

    /* Batch oracle: one region for the whole call. R scales with n. */
    if (g_mode == DIT_ORACLE_BATCH) { dit_on(); g_toggles++; }

    for (int i = 0; i < n; i++) {
        /* Public input: the message hash, varying so the signature is not a
         * cached constant. */
        for (int j = 0; j < 32; j++)
            msg[j] = (unsigned char)(i * 31 + j * 17 + (int)g_signs);

        /* Per-signature oracle: R is one signature regardless of n. */
        if (g_mode == DIT_ORACLE) { dit_on(); g_toggles++; }

        secp256k1_ecdsa_sign(g_ctx, &sig, msg, g_seckey, NULL, NULL);

        if (g_mode == DIT_ORACLE) { dit_off(); g_toggles++; }

        /* Declassified: the signature is published by definition of the
         * protocol. Serializing it is public work, deliberately outside the
         * protected region. */
        outlen = sizeof out;
        secp256k1_ecdsa_signature_serialize_der(g_ctx, out, &outlen, &sig);
        for (size_t j = 0; j < outlen; j++)
            acc = acc * 131 + out[j];
        g_signs++;
    }

    if (g_mode == DIT_ORACLE_BATCH) { dit_off(); g_toggles++; }
    return acc;
}

unsigned long public_verify_n(int n) {
    unsigned long acc = 0;

    /* No DIT toggling here in ANY mode. Under DIT_ALWAYS the bit is already set
     * process-wide; under the oracle modes this code is public and correctly
     * runs with DIT off. Any msr DIT that executes here in a pass-built binary
     * is, by construction, a false positive. */
    for (int i = 0; i < n; i++) {
        unsigned k = (unsigned)(g_vidx++ % VERIFY_POOL);
        acc += (unsigned long)secp256k1_ecdsa_verify(g_ctx, &g_sigs[k],
                                                     g_msgs[k], &g_pubkey);
        g_verifies++;
    }
    return acc;
}

unsigned long secret_toggles(void) { return g_toggles; }
unsigned long secret_count(void)   { return g_signs; }
unsigned long verify_count(void)   { return g_verifies; }
unsigned long secret_dit_now(void) { return dit_read(); }
