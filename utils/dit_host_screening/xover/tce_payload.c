/* tce_payload.c - the SECRET lane: per-column AEAD on the write path.
 *
 * WHY ENCRYPT-ON-WRITE. The CIO-parity seed declares the PLAINTEXT and its
 * LENGTH secret, not just the key. On a decrypt-on-read workload that is fatal
 * without a declassification mechanism: the plaintext leaves the library and
 * everything downstream that touches it becomes secret-dependent, which is the
 * instruction-level interleaving regime where no placement exists. On the write
 * path the plaintext ENTERS at a named boundary and the AEAD output is
 * declassified by cryptographic semantics, exactly as a signature is - so the
 * index maintenance, page splits and journalling that follow are genuinely
 * public work. The declassification boundary is the protocol's, not ours.
 */

#include "tce_payload.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "sodium.h"

static int    g_mode = DIT_OFF;
static size_t g_flen = 128;

static unsigned char  g_key[32];        /* THE SECRET - the column key */
static unsigned char  g_nonce[12];
static unsigned char *g_plain;          /* plaintext - secret under CIO's model */
static unsigned long  g_toggles, g_ops;
static double         g_last_op_us;

/* FEAT_DIT is ARMv8.4. Building with -DXOVER_NO_DIT compiles the mode switches
 * out so the harness itself can be validated on a pre-8.4 host (beckham is a
 * Neoverse-N1, ARMv8.2, where `msr DIT` is UNDEFINED and traps). Measured
 * binaries are never built this way - they run under gem5, which models DIT. */
#ifdef XOVER_NO_DIT
static inline void dit_on(void)  { }
static inline void dit_off(void) { }
static inline unsigned long dit_read(void) { return 0UL; }
#else
static inline void dit_on(void)  { __asm__ volatile("msr DIT, #1" ::: "memory"); }
static inline void dit_off(void) { __asm__ volatile("msr DIT, #0" ::: "memory"); }

static inline unsigned long dit_read(void) {
    unsigned long d;
    __asm__ volatile("mrs %0, DIT" : "=r"(d));
    return (d >> 24) & 1UL;
}
#endif

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

int tce_init(int mode, size_t field_bytes, int timing) {
    g_mode = mode;
    g_flen = field_bytes ? field_bytes : 128;

    if (sodium_init() < 0) return -1;

    for (size_t i = 0; i < sizeof g_key;   i++) g_key[i]   = (unsigned char)(0x40 + i * 7);
    for (size_t i = 0; i < sizeof g_nonce; i++) g_nonce[i] = (unsigned char)(i * 13 + 1);

    g_plain = (unsigned char *)malloc(g_flen + 64);
    if (!g_plain) return -1;
    for (size_t i = 0; i < g_flen; i++) g_plain[i] = (unsigned char)(i * 31 + 7);

    /* Measure R once, DIT off, outside the ROI, so the region size is reported
     * rather than assumed. Skipped entirely when timing is off - see the header. */
    g_last_op_us = 0.0;
    if (!timing) {
        g_ops = 0; g_toggles = 0;
        if (g_mode == DIT_ALWAYS) { dit_on(); g_toggles++; }
        return 0;
    }
    unsigned char *scratch = (unsigned char *)malloc(g_flen + 64);
    if (!scratch) return -1;
    int saved = g_mode; g_mode = DIT_OFF;
    for (int i = 0; i < 256; i++) tce_encrypt_field(scratch, (unsigned long)i, 0);  /* warm */
    g_last_op_us = 1e30;
    for (int rep = 0; rep < 8; rep++) {          /* best-of-8: noise is one-directional */
        double t0 = now_s();
        for (int i = 0; i < 256; i++) tce_encrypt_field(scratch, (unsigned long)i, 0);
        double us = (now_s() - t0) * 1e6 / 256.0;
        if (us < g_last_op_us) g_last_op_us = us;
    }
    free(scratch);
    g_mode = saved; g_ops = 0; g_toggles = 0;

    if (g_mode == DIT_ALWAYS) { dit_on(); g_toggles++; }
    return 0;
}

size_t tce_encrypt_field(unsigned char *out, unsigned long row, int col) {
    unsigned long long clen = 0;

    /* Vary the nonce per (row, column): replaying one input would manufacture
     * its own predictability. */
    g_nonce[0] = (unsigned char)row;
    g_nonce[1] = (unsigned char)(row >> 8);
    g_nonce[2] = (unsigned char)col;

    if (g_mode == DIT_FIELD) { dit_on(); g_toggles++; }
    crypto_aead_chacha20poly1305_ietf_encrypt(
        out, &clen, g_plain, g_flen, NULL, 0, NULL, g_nonce, g_key);
    if (g_mode == DIT_FIELD) { dit_off(); g_toggles++; }

    g_ops++;
    return (size_t)clen;
}

void tce_plain_field(unsigned char *out, unsigned long row, int col) {
    /* Same byte count as a ciphertext (plaintext + 16-byte tag) so the stored
     * row is identical in size whatever enc_cols is. */
    memset(out, (int)((row + (unsigned long)col) & 0xff), g_flen + 16);
}

void tce_row_begin(void) { if (g_mode == DIT_ROW) { dit_on();  g_toggles++; } }
void tce_row_end(void)   { if (g_mode == DIT_ROW) { dit_off(); g_toggles++; } }

double        tce_last_op_us(void)  { return g_last_op_us; }
unsigned long tce_toggles(void)     { return g_toggles; }
unsigned long tce_count(void)       { return g_ops; }
unsigned long tce_dit_now(void)     { return dit_read(); }
size_t        tce_field_bytes(void) { return g_flen; }
