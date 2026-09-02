#include <stdint.h>
/* The callee does a secret-dependent multiply. It must run with DIT on. */
__attribute__((noinline)) uint64_t consume(const uint64_t *p) {
    uint64_t a = 1;
    for (int i = 0; i < 4; i++) a = a * p[i] + 3;
    return a;
}
/* seed: entry,0,pointee   (memory behind `key` is secret) */
uint64_t entry(const uint64_t *key) {
    uint64_t local[4];
    for (int i = 0; i < 4; i++) local[i] = key[i] ^ 0x55;  /* taints local's cells */
    return consume(local);                                  /* passes &local */
}
