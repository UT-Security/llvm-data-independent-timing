/* CryptoMPK's libhydrogen_vector.patch inverts the SSE2 test, so on their
 * x86-64 host this selects gimli-core/portable.h -- which is the file their
 * shipped taint report covers. On aarch64 __SSE2__ is undefined, so the
 * unmodified patched form would pull in <emmintrin.h> and fail to compile.
 * Select portable.h directly: same code they measured, builds here. */
#include "gimli-core/portable.h"

static void
gimli_core_u8(uint8_t state_u8[gimli_BLOCKBYTES], uint8_t tag)
{
    state_u8[gimli_BLOCKBYTES - 1] ^= tag;
#ifndef NATIVE_LITTLE_ENDIAN
    uint32_t state_u32[12];
    int      i;

    for (i = 0; i < 12; i++) {
        state_u32[i] = LOAD32_LE(&state_u8[i * 4]);
    }
    gimli_core(state_u32);
    for (i = 0; i < 12; i++) {
        STORE32_LE(&state_u8[i * 4], state_u32[i]);
    }
#else
    gimli_core((uint32_t *) (void *) state_u8); /* state_u8 must be properly aligned */
#endif
}
