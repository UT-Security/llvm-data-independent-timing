#ifndef SECRET_PAYLOAD_H
#define SECRET_PAYLOAD_H

#define DIT_OFF 0
#define DIT_ALWAYS 1
#define DIT_ORACLE 2

void secret_init(int mode);
unsigned long secret_sign_n(int n);
unsigned long secret_toggles(void);
unsigned long secret_count(void);
unsigned long secret_dit_now(void);

#endif
