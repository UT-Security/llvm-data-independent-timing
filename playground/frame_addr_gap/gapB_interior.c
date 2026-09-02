/* Gap B as it actually appears in libhydrogen: the pointer handed to the callee
 * is an INTERIOR pointer into the caller's own argument, not the argument
 * itself.
 *
 *     uint8_t *sig    = &csig[hydro_sign_NONCEBYTES];   <-- arg base + constant
 *     uint8_t *eph_sk = sig;
 *     hydro_hash_final(&st, eph_sk, 32);                <-- fills it
 *     hydro_x25519_scalarmult_base_uniform(eph_pk, eph_sk);
 *
 * Following a COPY is not enough. This is GCC ipa-modref's `parm_offset` case:
 * a parameter base plus a compile-time-constant displacement keeps `parm_index`.
 * At whole-object granularity the offset itself does not even need storing - an
 * interior pointer into argument k's object is still argument k's object. */
#include <stdint.h>
__attribute__((noinline)) void produce(uint64_t *out, const uint64_t *key) {
    for (int i = 0; i < 4; i++) out[i] = key[i] * 3;
}
__attribute__((noinline)) uint64_t consume(const uint64_t *p) {
    uint64_t a = 1;
    for (int i = 0; i < 4; i++) a = a * p[i] + 3;   /* secret multiply */
    return a;
}
uint64_t via_interior(uint64_t *csig, const uint64_t *key) {
    uint64_t *slot = &csig[4];        /* interior of OUR OWN argument */
    produce(slot, key);
    return consume(slot);
}
