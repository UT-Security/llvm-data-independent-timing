/* The THIRD gap, and the one that actually stops libhydrogen.
 *
 * Identical to gapB_interior.c except that `produce` fills the buffer with
 * memcpy instead of a loop. libhydrogen's hydro_hash_final does exactly this:
 *
 *     memcpy(out + i * gimli_RATE, buf, gimli_RATE);
 *
 * memcpy is an EXTERNAL declaration, so the blunt-TOP rule fires: an external
 * callee receiving a secret sets WritesSecretToUnknown, and the summary comes
 * out `UNKNOWN(TOP)` instead of naming arg 0. There is then no precise
 * arg-pointee fact for B2 to consume, and the chain breaks one step before it.
 *
 * This is NOT the variable-offset case - the loop version in gapB_interior.c
 * resolves fine. The fix is the deferred libc model table
 * (docs/research/memory-summaries.md, P1), not more pointer provenance. */
#include <stdint.h>
#include <string.h>
/* n is runtime-variable so the memcpy cannot be expanded inline - a constant
 * size is lowered to plain stores and resolves fine, which is why the first
 * version of this repro did NOT reproduce the failure. */
__attribute__((noinline)) void produce(uint64_t *out, const uint64_t *key,
                                       unsigned long n) {
    uint64_t tmp[4];
    for (int i = 0; i < 4; i++) tmp[i] = key[i] * 3;
    memcpy(out, tmp, n);                   /* real external call */
}
__attribute__((noinline)) uint64_t consume(const uint64_t *p) {
    uint64_t a = 1;
    for (int i = 0; i < 4; i++) a = a * p[i] + 3;
    return a;
}
uint64_t via_memcpy(uint64_t *csig, const uint64_t *key) {
    uint64_t *slot = &csig[4];
    produce(slot, key, 32);
    return consume(slot);
}
