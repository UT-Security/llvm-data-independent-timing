#include <stdint.h>
/* The callee writes a secret THROUGH ITS POINTER ARGUMENT. Its mod-set should
   record "writes secret through arg 0", and the caller should then know the
   buffer became secret. */
__attribute__((noinline)) void produce(uint64_t *out, const uint64_t *key) {
    for (int i = 0; i < 4; i++) out[i] = key[i] * 3;
}
__attribute__((noinline)) uint64_t consume(const uint64_t *p) {
    uint64_t a = 1;
    for (int i = 0; i < 4; i++) a = a * p[i] + 3;   /* secret multiply */
    return a;
}

/* WORKS: `buf` is a FRAME OBJECT, so P1b can name the object produce wrote. */
uint64_t via_local(const uint64_t *key) {
    uint64_t buf[4];
    produce(buf, key);
    return consume(buf);
}

/* FAILS: `buf` is this function's OWN INCOMING ARGUMENT. Same callee, same
   mod-set, but there is no frame object to name, so P1b falls back to a blunt
   clobber - which poisons LOADS in this function, and `buf` is never loaded
   here, it is passed on. */
uint64_t via_argptr(uint64_t *buf, const uint64_t *key) {
    produce(buf, key);
    return consume(buf);
}
