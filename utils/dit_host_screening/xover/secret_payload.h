#ifndef XOVER_SECRET_PAYLOAD_H
#define XOVER_SECRET_PAYLOAD_H

/* DIT modes, selected at RUNTIME from argv so one binary serves every arm and
 * dit-measurement-traps trap 7b (the per-binary codegen lottery) cannot apply. */
#define DIT_OFF          0
#define DIT_ALWAYS       1
#define DIT_ORACLE       2   /* toggle per signature   -> R = 1 signature  */
#define DIT_ORACLE_BATCH 3   /* toggle per call        -> R = n signatures */

void          secret_init(int mode);

/* SECRET lane: n ECDSA signatures over the static secret key. */
unsigned long secret_sign_n(int n);

/* PUBLIC lane: n ECDSA verifications. Touches no secret. Shares helper code
 * with the signing path, which is what makes taint false positives land here. */
unsigned long public_verify_n(int n);

unsigned long secret_toggles(void);
unsigned long secret_count(void);
unsigned long verify_count(void);
unsigned long secret_dit_now(void);

#endif
