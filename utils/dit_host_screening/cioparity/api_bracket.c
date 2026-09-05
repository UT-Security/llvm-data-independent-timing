/*
 * api_bracket.c - the "hand placement at the public API" arm.
 *
 * What a careful library author does by hand: PSTATE.DIT is set once at the
 * entry of each public crypto function and restored once at its exit. Nothing
 * inside is touched. It is the arm between blanket (the mode set for the whole
 * process) and the pass (the mode placed inside the library by the analysis).
 *
 * The prologue and epilogue are Apple's, from "Writing ARM64 code for Apple
 * platforms" (Enable DIT for constant-time cryptographic operations), which
 * is also what timingsafe_enable_if_supported() / _restore_if_supported() do:
 *
 *   was = (mrs DIT >> 24) & 1        read the previous state (the token)
 *   msr DIT, #1                      turn it on
 *   sb                               "to ensure that subsequent instruction
 *                                     timing reflects the updated DIT state,
 *                                     add a speculation barrier"; on a device
 *                                     without FEAT_SB Apple uses dsb nsh; isb sy
 *   ... the operation ...
 *   if (!was) msr DIT, #0            restore, never clear a caller's DIT
 *
 * gem5 does not implement SB, so the barrier here is `isb sy` by default
 * (API_BARRIER_ISB); API_BARRIER_DSBISB selects Apple's no-SB fallback pair and
 * API_BARRIER_NONE drops it. API_NO_MRS drops the token read and clears
 * unconditionally (the pre-2026-09-05 form of this arm), which bounds a gem5
 * artifact: `mrs DIT` decodes to Mrs64, IsSerializeBefore, a pipeline drain,
 * where the M5 reads it in 1 cycle (docs/results/dit-cost-model.md).
 *
 * Mechanism: the UNHARDENED library, and the linker's --wrap on the public
 * entry points CIO's drivers call, so __wrap_f brackets __real_f. The driver,
 * the library and the layout are those of the `base` arm; only the wrappers
 * are added. Guarded per benchmark so a binary wraps only what it links.
 *
 * build_arms.sh links this with the matching -DAPI_* and -Wl,--wrap=... set.
 */
#include <stddef.h>

#if defined(API_BARRIER_SB)
   /* Apple's actual instruction, for silicon with FEAT_SB (every M-series);
    * the raw encoding so no -march or target attribute is needed */
#  define DIT_BARRIER() __asm__ volatile(".inst 0xd50330ff" ::: "memory")
#elif defined(API_BARRIER_DSBISB)
#  define DIT_BARRIER() __asm__ volatile("dsb nsh\n\tisb sy" ::: "memory")
#elif defined(API_BARRIER_NONE)
#  define DIT_BARRIER() ((void)0)
#elif defined(API_BARRIER_NOP)
   /* the rig's layout control for the barrier: HINT #0 at the isb's address */
#  define DIT_BARRIER() __asm__ volatile("hint #0" ::: "memory")
#else /* API_BARRIER_ISB, the default: isb in place of sb, which gem5 lacks */
#  define DIT_BARRIER() __asm__ volatile("isb sy" ::: "memory")
#endif

#if defined(API_NOP)
   /* The bracket's instruction-matched layout control: every instruction of
    * the full sequence kept, none of them touching DIT. mrs -> mov (one
    * instruction, a register write), msr -> hint #0, the barrier -> hint #0,
    * the conditional clear -> the same tbnz over a hint #0. */
#  define DIT_ENTER(was) do { __asm__ volatile("mov %0, #0" : "=r"(was)); __asm__ volatile("hint #0" ::: "memory"); __asm__ volatile("hint #0" ::: "memory"); } while (0)
#  define DIT_LEAVE(was) do { if (!(was)) __asm__ volatile("hint #0" ::: "memory"); } while (0)
#elif defined(API_NO_MRS)
#  define DIT_ENTER(was) do { (was) = 0; __asm__ volatile("msr DIT, #1" ::: "memory"); DIT_BARRIER(); } while (0)
#  define DIT_LEAVE(was) do { (void)(was); __asm__ volatile("msr DIT, #0" ::: "memory"); } while (0)
#else
static inline unsigned long dit_was_on(void) {
    unsigned long v;
    __asm__ volatile("mrs %0, DIT" : "=r"(v));
    return (v >> 24) & 1;
}
#  define DIT_ENTER(was) do { (was) = dit_was_on(); __asm__ volatile("msr DIT, #1" ::: "memory"); DIT_BARRIER(); } while (0)
#  define DIT_LEAVE(was) do { if (!(was)) __asm__ volatile("msr DIT, #0" ::: "memory"); } while (0)
#endif

/* Two ways to interpose. GNU/lld: the linker's --wrap, __wrap_f brackets
 * __real_f (gem5, Linux). Apple's ld64 has no --wrap, so on macOS the DRIVER
 * TU is compiled with -Dcrypto_sign=expedite_api_crypto_sign (and so on for
 * the entry points it calls; the drivers declare them with extern prototypes,
 * which the define renames consistently) and this file, compiled with
 * -DAPI_MACRO_RENAME, defines expedite_api_f bracketing the real f. Same
 * wrapper frame, same prologue and epilogue, either way. */
#ifdef API_MACRO_RENAME
#  define API_WRAP(name) expedite_api_##name
#  define API_REAL(name) name
#else
#  define API_WRAP(name) __wrap_##name
#  define API_REAL(name) __real_##name
#endif

#define BRACKET(rettype, name, params, args)                       \
    rettype API_REAL(name) params;                                 \
    rettype API_WRAP(name) params {                                \
        rettype r_; unsigned long was_;                            \
        DIT_ENTER(was_);                                           \
        r_ = API_REAL(name) args;                                  \
        DIT_LEAVE(was_);                                           \
        return r_;                                                 \
    }
#define BRACKET_VOID(name, params, args)                           \
    void API_REAL(name) params;                                    \
    void API_WRAP(name) params {                                   \
        unsigned long was_;                                        \
        DIT_ENTER(was_);                                           \
        API_REAL(name) args;                                       \
        DIT_LEAVE(was_);                                           \
    }

typedef unsigned char uc;
typedef unsigned long long ull;

#ifdef API_SIGN
BRACKET(int, crypto_sign_keypair, (uc *pk, uc *sk), (pk, sk))
BRACKET(int, crypto_sign,
        (uc *sm, ull *smlen_p, const uc *m, ull mlen, const uc *sk),
        (sm, smlen_p, m, mlen, sk))
BRACKET(int, crypto_sign_open,
        (uc *m, ull *mlen_p, const uc *sm, ull smlen, const uc *pk),
        (m, mlen_p, sm, smlen, pk))
#endif

#ifdef API_CHACHA
BRACKET_VOID(crypto_aead_chacha20poly1305_ietf_keygen, (uc *k), (k))
BRACKET(int, crypto_aead_chacha20poly1305_ietf_encrypt,
        (uc *c, ull *clen_p, const uc *m, ull mlen, const uc *ad, ull adlen,
         const uc *nsec, const uc *npub, const uc *k),
        (c, clen_p, m, mlen, ad, adlen, nsec, npub, k))
BRACKET(int, crypto_aead_chacha20poly1305_ietf_decrypt,
        (uc *m, ull *mlen_p, uc *nsec, const uc *c, ull clen, const uc *ad,
         ull adlen, const uc *npub, const uc *k),
        (m, mlen_p, nsec, c, clen, ad, adlen, npub, k))
#endif

#ifdef API_AES
BRACKET_VOID(crypto_aead_aes256gcm_keygen, (uc *k), (k))
BRACKET(int, crypto_aead_aes256gcm_encrypt,
        (uc *c, ull *clen_p, const uc *m, ull mlen, const uc *ad, ull adlen,
         const uc *nsec, const uc *npub, const uc *k),
        (c, clen_p, m, mlen, ad, adlen, nsec, npub, k))
BRACKET(int, crypto_aead_aes256gcm_decrypt,
        (uc *m, ull *mlen_p, uc *nsec, const uc *c, ull clen, const uc *ad,
         ull adlen, const uc *npub, const uc *k),
        (m, mlen_p, nsec, c, clen, ad, adlen, npub, k))
#endif

#ifdef API_SECP
/* libsecp256k1, the wallet flow's secret lane (CKey::Sign): the two entry
 * points handed the private key. Pointer-only signatures spelled with void
 * pointers, ABI-identical on AArch64, so this file needs no secp256k1.h. */
BRACKET(int, secp256k1_ecdsa_sign,
        (const void *ctx, void *sig, const void *msg32, const void *seckey,
         const void *noncefp, const void *ndata),
        (ctx, sig, msg32, seckey, noncefp, ndata))
BRACKET(int, secp256k1_ec_pubkey_create,
        (const void *ctx, void *pubkey, const void *seckey),
        (ctx, pubkey, seckey))
#endif

#ifdef API_PWHASH
BRACKET(int, crypto_pwhash,
        (uc *const out, ull outlen, const char *const passwd, ull passwdlen,
         const uc *const salt, ull opslimit, size_t memlimit, int alg),
        (out, outlen, passwd, passwdlen, salt, opslimit, memlimit, alg))
#endif
