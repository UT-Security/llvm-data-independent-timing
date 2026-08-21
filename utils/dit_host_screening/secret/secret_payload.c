/* secret_payload.c - the SECRET half of each composite host benchmark.
 *
 * Real ECDSA signing over libsecp256k1. Chosen over a synthetic secret because:
 *  - one taint seed: secp256k1_ecdsa_sign(ctx, sig, msg32, seckey, ...), arg 3
 *  - self-contained C, direct calls, no vtable/function-pointer boundary
 *  - ~1.1-1.4 us per region, 3-4x above the dit-granularity-crossover threshold
 *  - the signature is PUBLISHED by definition of the protocol, so the
 *    declassification boundary is real cryptographic semantics rather than the
 *    write-to-sink harness trick the QuickJS result had to use
 *
 * THREE DIT MODES, selected at RUNTIME in one binary. No codegen differs
 * between arms, so dit-measurement-traps trap 7b cannot apply at all.
 *
 *   DIT_OFF     never set
 *   DIT_ALWAYS  set once at init, never cleared  (the always-on reference)
 *   DIT_ORACLE  set on entry to each sign, cleared on exit (perfect placement)
 *
 * COVERAGE AUDIT (trap 8 - an under-protecting oracle looks exactly like a
 * win). The secret key lives in `g_seckey` and is touched ONLY inside
 * secret_sign_n between the DIT enable and disable. Nothing else in any host
 * reads it. The message hash is public, the signature is public. So the oracle
 * covers 100% of secret-touching work by construction, not by inspection.
 */

#include "secret_payload.h"

#include <stdint.h>
#include <string.h>

#include "secp256k1.h"

static secp256k1_context *g_ctx;
static unsigned char g_seckey[32];   /* THE SECRET */
static int g_mode = DIT_OFF;
static unsigned long g_toggles;
static unsigned long g_signs;

static inline void dit_on(void) { __asm__ volatile("msr DIT, #1" ::: "memory"); }
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
    if (g_mode == DIT_ALWAYS) {
        dit_on();
        g_toggles++;
    }
}

/* n signatures. In DIT_ORACLE the region is exactly this loop body's crypto,
 * entered and left once per signature. */
unsigned long secret_sign_n(int n) {
    unsigned long acc = 0;
    unsigned char msg[32];
    secp256k1_ecdsa_signature sig;
    unsigned char out[72];
    size_t outlen;

    for (int i = 0; i < n; i++) {
        /* Public input: the message hash. Varying so the signature is not a
         * cached constant. */
        for (int j = 0; j < 32; j++)
            msg[j] = (unsigned char)(i * 31 + j * 17 + (int)g_signs);

        if (g_mode == DIT_ORACLE) { dit_on(); g_toggles++; }

        secp256k1_ecdsa_sign(g_ctx, &sig, msg, g_seckey, NULL, NULL);

        if (g_mode == DIT_ORACLE) { dit_off(); g_toggles++; }

        /* Declassified: the signature is published. Serializing it is public
         * work and is deliberately OUTSIDE the protected region. */
        outlen = sizeof out;
        secp256k1_ecdsa_signature_serialize_der(g_ctx, out, &outlen, &sig);
        for (size_t j = 0; j < outlen; j++)
            acc = acc * 131 + out[j];
        g_signs++;
    }
    return acc;
}

unsigned long secret_toggles(void) { return g_toggles; }
unsigned long secret_count(void) { return g_signs; }
unsigned long secret_dit_now(void) { return dit_read(); }
