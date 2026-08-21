/* mod_driver.c - does -taint-modset-callsite-gated cost any real coverage?
 *
 * The static switch count fell 660 -> 178 in Bitcoin Core's libsecp256k1, and
 * every seeded entry point kept its switches. That is necessary but not
 * sufficient: a placement that is cheaper because it covers LESS looks exactly
 * like a precision win (dit-measurement-traps trap 8). Only dynamic coverage
 * settles it.
 *
 * compSimplifier.ditSuppressed counts operations DIT actually blocked. Same
 * driver, same input, two workloads:
 *
 *   WL_SIGN    secret present. gated must match base and the oracle.
 *              A DROP here is a coverage loss and kills the flag.
 *   WL_VERIFY  no secret anywhere. base carries 17 switches in ecdsa_verify
 *              that protect nothing; gated should carry 0.
 *
 * Modes mirror gem5cov/cov_driver.c so the numbers are comparable.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gem5/m5ops.h>

#include "secp256k1.h"

#define MODE_OFF    0
#define MODE_ALWAYS 1
#define MODE_ORACLE 2
#define MODE_PASS   3

#define WL_SIGN   0
#define WL_VERIFY 1

static inline void dit_on(void)  { __asm__ volatile("msr DIT, #1" ::: "memory"); }
static inline void dit_off(void) { __asm__ volatile("msr DIT, #0" ::: "memory"); }

static secp256k1_context *ctx;
static unsigned char seckey[32];
static secp256k1_pubkey pub;
static secp256k1_ecdsa_signature fixed_sig;
static unsigned char fixed_msg[32];

/* The oracle wraps BOTH entry points a signature touches: the signature itself
 * and the public-key derivation, which is secret work too (the trap that made
 * the coincurve oracle under-protect twice). */
static unsigned long __attribute__((noinline))
sign_n(int n, int mode)
{
    unsigned long acc = 0;
    unsigned char msg[32];
    secp256k1_ecdsa_signature sig;
    secp256k1_pubkey p;
    unsigned char out[72];
    size_t outlen;

    for (int i = 0; i < n; i++) {
        for (int j = 0; j < 32; j++)
            msg[j] = (unsigned char)(i * 31 + j * 17);

        if (mode == MODE_ORACLE) dit_on();
        secp256k1_ecdsa_sign(ctx, &sig, msg, seckey, NULL, NULL);
        secp256k1_ec_pubkey_create(ctx, &p, seckey);
        if (mode == MODE_ORACLE) dit_off();

        outlen = sizeof out;
        secp256k1_ecdsa_signature_serialize_der(ctx, out, &outlen, &sig);
        for (size_t j = 0; j < outlen; j++)
            acc = acc * 131 + out[j];
        acc = acc * 7 + p.data[0];
    }
    return acc;
}

/* No secret is in scope at all: a fixed signature over a fixed message under a
 * public key. Every switch executed here is pure waste. */
static unsigned long __attribute__((noinline))
verify_n(int n, int mode)
{
    unsigned long acc = 0;
    (void)mode;                 /* the oracle protects NOTHING here, by design */
    for (int i = 0; i < n; i++)
        acc += (unsigned long)secp256k1_ecdsa_verify(ctx, &fixed_sig,
                                                     fixed_msg, &pub);
    return acc;
}

int main(int argc, char **argv)
{
    int mode = argc > 1 ? atoi(argv[1]) : MODE_OFF;
    int n    = argc > 2 ? atoi(argv[2]) : 100;
    int wl   = argc > 3 ? atoi(argv[3]) : WL_SIGN;

    ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
    for (int i = 0; i < 32; i++)
        seckey[i] = (unsigned char)(0x40 + i * 7);
    seckey[31] |= 1;

    /* Build the verify fixture OUTSIDE the ROI. It uses the secret key, but it
     * runs before m5_reset_stats, so none of it lands in the verify counts. */
    secp256k1_ec_pubkey_create(ctx, &pub, seckey);
    for (int j = 0; j < 32; j++) fixed_msg[j] = (unsigned char)(j * 3 + 1);
    secp256k1_ecdsa_sign(ctx, &fixed_sig, fixed_msg, seckey, NULL, NULL);

    /* warm: touch the precomputed tables before the ROI */
    unsigned long warm = wl == WL_VERIFY ? verify_n(2, MODE_OFF)
                                         : sign_n(2, MODE_OFF);

    if (mode == MODE_ALWAYS) dit_on();
    m5_reset_stats(0, 0);
    unsigned long acc = wl == WL_VERIFY ? verify_n(n, mode) : sign_n(n, mode);
    m5_dump_reset_stats(0, 0);
    if (mode == MODE_ALWAYS) dit_off();

    unsigned long dit_after;
    __asm__ volatile("mrs %0, DIT" : "=r"(dit_after));
    printf("mod mode=%d n=%d wl=%d checksum=%lu warm=%lu dit_after=%lu\n",
           mode, n, wl, acc % 100000000UL, warm % 1000UL,
           (dit_after >> 24) & 1UL);
    return 0;
}
