#ifndef XOVER_SODIUM_PAYLOAD_H
#define XOVER_SODIUM_PAYLOAD_H

#include <stddef.h>

/* DIT modes, chosen at RUNTIME from argv so one binary serves every arm and the
 * per-binary codegen lottery cannot affect the comparison. */
#define DIT_OFF          0
#define DIT_ALWAYS       1
#define DIT_ORACLE       2   /* toggle per crypto call  -> R = one operation  */
#define DIT_ORACLE_BATCH 3   /* toggle per batch        -> R = n operations   */

/* Primitives. Every one of these is covered by the CIO-parity seed, so the pass
 * can actually protect it; the unseeded parts of libsodium are not used.
 *
 * They span six orders of magnitude of work per operation, which is what makes
 * libsodium a better vehicle for the granularity axis than a single signature
 * scheme: R is a real deployment parameter here, not a knob we invented.
 *
 *   AUTH    Poly1305 one-time MAC                       ~0.1 us
 *   AEAD    ChaCha20-Poly1305 IETF, size-parameterised  ~0.3 us .. ~300 us
 *   SIGN    Ed25519                                     ~50 us
 *   PWHASH  Argon2id                                    ~10 ms .. ~100 ms
 *   GCM     AES-256-GCM, for contrast only              (hardware AES is already
 *           constant-time, so this is the primitive DIT should NOT help)
 */
#define PRIM_AUTH   0
#define PRIM_AEAD   1
#define PRIM_SIGN   2
#define PRIM_PWHASH 3
#define PRIM_GCM    4

/* msgsize applies to AEAD/GCM/AUTH; ops/mem apply to PWHASH. */
int           secret_init(int mode, int prim, size_t msgsize,
                          unsigned long ops, size_t mem);
unsigned long secret_work_n(int n);
const char   *secret_prim_name(void);
double        secret_last_op_us(void);   /* measured R, one operation */

unsigned long secret_toggles(void);
unsigned long secret_count(void);
unsigned long secret_dit_now(void);

#endif
