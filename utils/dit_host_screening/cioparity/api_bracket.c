/*
 * api_bracket.c - the "hand placement at the public API" arm.
 *
 * What a careful library author does by hand, and what Apple's corecrypto
 * scope guards and AWS-LC's caller-level hoisting do: PSTATE.DIT is set once
 * at the entry of each public crypto function and cleared once at its exit.
 * Nothing inside is touched. It is the arm between blanket (the mode set for
 * the whole process) and the pass (the mode placed inside the library by the
 * analysis): exactly two mode writes per API call, no analysis, no knowledge
 * of where inside the call the secret is.
 *
 * Mechanism: the UNHARDENED library, and the linker's --wrap on the public
 * entry points CIO's drivers call, so __wrap_f brackets __real_f. The
 * driver, the library and the layout are those of the `base` arm; only the
 * wrappers are added. Guarded per benchmark so a binary wraps only what it
 * links (an unguarded __real_crypto_pwhash would pull argon2 into the ed25519
 * binary).
 *
 * build_arms.sh links this with the matching -DAPI_* and -Wl,--wrap=... set.
 */
#include <stddef.h>

#define DIT_ON()  __asm__ volatile("msr DIT, #1" ::: "memory")
#define DIT_OFF() __asm__ volatile("msr DIT, #0" ::: "memory")

#define BRACKET(rettype, name, params, args)                       \
    rettype __real_##name params;                                  \
    rettype __wrap_##name params {                                 \
        rettype r_;                                                \
        DIT_ON();                                                  \
        r_ = __real_##name args;                                   \
        DIT_OFF();                                                 \
        return r_;                                                 \
    }
#define BRACKET_VOID(name, params, args)                           \
    void __real_##name params;                                     \
    void __wrap_##name params {                                    \
        DIT_ON();                                                  \
        __real_##name args;                                        \
        DIT_OFF();                                                 \
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

#ifdef API_PWHASH
BRACKET(int, crypto_pwhash,
        (uc *const out, ull outlen, const char *const passwd, ull passwdlen,
         const uc *const salt, ull opslimit, size_t memlimit, int alg),
        (out, outlen, passwd, passwdlen, salt, opslimit, memlimit, alg))
#endif
