#include <stdint.h>
__attribute__((noinline)) void produce(uint64_t *out, const uint64_t *key) {
    for (int i = 0; i < 4; i++) out[i] = key[i] * 3;   /* secret through arg0 */
}
__attribute__((noinline)) uint64_t consume(const uint64_t *p) {
    uint64_t a = 1;
    for (int i = 0; i < 4; i++) a = a * p[i] + 3;
    return a;
}

/* SHAPE 1: the caller LOADS the buffer itself. */
uint64_t inline_consume(uint64_t *buf, const uint64_t *key) {
    produce(buf, key);
    uint64_t a = 1;
    for (int i = 0; i < 4; i++) a = a * buf[i] + 3;    /* secret multiply, here */
    return a;
}

/* SHAPE 2: the caller PASSES THE POINTER ON. */
uint64_t pass_on(uint64_t *buf, const uint64_t *key) {
    produce(buf, key);
    return consume(buf);
}
