/* cov_driver.c - does the taint pass protect as much as the hand oracle?
 *
 * The silicon result (docs/results/dit-pass-vs-oracle.md) showed the pass
 * matching the oracle on wall time. That is only trustworthy if the pass
 * protects AT LEAST as much secret work as the oracle does - a placement that
 * is faster because it covers less looks exactly like a win
 * (dit-measurement-traps trap 8, the retracted +8.15%).
 *
 * gem5 can settle it directly: compSimplifier.ditSuppressed counts dynamic
 * operations that DIT actually blocked, and lvp.ditTaggedSet counts DitCC
 * instructions observed with DIT set. Same driver, same input, four placements
 * - the counts ARE the coverage.
 *
 *   off     no DIT anywhere                -> expect ~0
 *   always  DIT set for the whole ROI      -> the ceiling
 *   oracle  DIT around each sign call      -> hand placement
 *   (pass)  no manual DIT; the -ftaint-harden switches do it
 *
 * `oracle` and `pass` are the comparison that matters:
 *   pass >= oracle  -> the pass protects at least as much; wall-time win real
 *   pass <  oracle  -> under-protection, and the win is an artifact
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gem5/m5ops.h>

#include "secp256k1.h"

#define MODE_OFF    0
#define MODE_ALWAYS 1
#define MODE_ORACLE 2
#define MODE_PASS   3   /* manual DIT never touched; pass switches do the work */

static inline void dit_on(void)  { __asm__ volatile("msr DIT, #1" ::: "memory"); }
static inline void dit_off(void) { __asm__ volatile("msr DIT, #0" ::: "memory"); }

static secp256k1_context *ctx;
static unsigned char seckey[32];

/* Kept in its own noinline function so the ROI boundary is unambiguous and the
 * oracle's region is exactly "the signing call", matching host_*.c. */
static unsigned long __attribute__((noinline))
sign_n(int n, int mode)
{
    unsigned long acc = 0;
    unsigned char msg[32];
    secp256k1_ecdsa_signature sig;
    unsigned char out[72];
    size_t outlen;

    for (int i = 0; i < n; i++) {
        for (int j = 0; j < 32; j++)
            msg[j] = (unsigned char)(i * 31 + j * 17);

        if (mode == MODE_ORACLE) dit_on();
        secp256k1_ecdsa_sign(ctx, &sig, msg, seckey, NULL, NULL);
        if (mode == MODE_ORACLE) dit_off();

        outlen = sizeof out;
        secp256k1_ecdsa_signature_serialize_der(ctx, out, &outlen, &sig);
        for (size_t j = 0; j < outlen; j++)
            acc = acc * 131 + out[j];
    }
    return acc;
}

int main(int argc, char **argv)
{
    int mode = argc > 1 ? atoi(argv[1]) : MODE_OFF;
    int n    = argc > 2 ? atoi(argv[2]) : 100;

    ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
    for (int i = 0; i < 32; i++)
        seckey[i] = (unsigned char)(0x40 + i * 7);
    seckey[31] |= 1;

    /* warm: touch the precomputed tables and the context before the ROI so the
     * measured region is signing, not first-touch page faults */
    unsigned long warm = sign_n(2, MODE_OFF);

    if (mode == MODE_ALWAYS) dit_on();
    m5_reset_stats(0, 0);
    unsigned long acc = sign_n(n, mode);
    m5_dump_reset_stats(0, 0);
    if (mode == MODE_ALWAYS) dit_off();

    printf("cov mode=%d n=%d checksum=%lu warm=%lu\n",
           mode, n, acc % 100000000UL, warm % 1000UL);
    return 0;
}
