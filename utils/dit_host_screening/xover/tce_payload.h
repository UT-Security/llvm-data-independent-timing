#ifndef XOVER_TCE_PAYLOAD_H
#define XOVER_TCE_PAYLOAD_H

#include <stddef.h>

/* DIT modes, chosen at RUNTIME from argv so one binary serves every arm and the
 * per-binary codegen lottery cannot affect the comparison that matters most. */
#define DIT_OFF          0
#define DIT_ALWAYS       1
#define DIT_FIELD        2   /* toggle per encrypted field -> R = one field   */
#define DIT_ROW          3   /* toggle per row             -> R = enc_cols    */

/* Transparent Column Encryption, the pgsodium shape.
 *
 * pgsodium encrypts with crypto_aead_det_xchacha20poly1305; this uses
 * crypto_aead_chacha20poly1305_ietf, which the CIO-parity seed already covers,
 * so the pass can protect it without widening the annotation set. The two
 * differ by one hchacha20 call and a nonce, which is immaterial to the shape
 * being measured (whole-value AEAD, once per column, on the write path).
 *
 * COVERAGE AUDIT (trap 8 - an under-protecting oracle looks exactly like a
 * win). The key is filled once in tce_init, before any timing starts. Inside
 * the measured region the only thing that touches key or plaintext is the
 * libsodium call itself, between the oracle's enable and disable, so the
 * oracle covers 100% of secret-touching work in the region by construction.
 */

/* timing=0 disables every clock_gettime call AND the R calibration. Required
 * under gem5: SE mode returns SIMULATED time, so a timed value formatted into
 * the output differs between machine configurations and makes simInsts differ -
 * which is exactly the self-perturbing-driver failure the gate exists to catch.
 * It also removes the calibration's own 2048 AEAD calls, which would otherwise
 * be counted in ditSuppressed at every point including enc_cols=0. */
int tce_init(int mode, size_t field_bytes, int timing);

/* Encrypt one field into `out` (must hold field_bytes + 16). Returns the
 * ciphertext length. Called once per encrypted column, on the write path. */
size_t tce_encrypt_field(unsigned char *out, unsigned long row, int col);

/* Same-sized non-secret fill, so a row is byte-identical in SIZE whatever
 * enc_cols is set to: the B-tree does the same work at every point on the
 * knob, and only the amount of crypto moves. */
void   tce_plain_field(unsigned char *out, unsigned long row, int col);

void   tce_row_begin(void);   /* DIT_ROW toggles here */
void   tce_row_end(void);

double        tce_last_op_us(void);
unsigned long tce_toggles(void);
unsigned long tce_count(void);
unsigned long tce_dit_now(void);
size_t        tce_field_bytes(void);

#endif
