#include <stdint.h>
__attribute__((noinline)) void produce(uint64_t *out, const uint64_t *key) {
    for (int i = 0; i < 4; i++) out[i] = key[i] * 3;
}
__attribute__((noinline)) uint64_t consume(const uint64_t *p) {
    uint64_t a = 1;
    for (int i = 0; i < 4; i++) a = a * p[i] + 3;
    return a;
}
/* the secret path the seed is meant to fix */
uint64_t pass_on(uint64_t *buf, const uint64_t *key) {
    produce(buf, key);
    return consume(buf);
}
/* a PUBLIC caller of the same helper - no secret anywhere near it */
uint64_t public_path(uint64_t *pub) {
    return consume(pub);
}
