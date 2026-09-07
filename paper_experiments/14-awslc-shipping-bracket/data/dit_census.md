# Which `bssl speed` rows enter AWS-LC's DIT bracket

Census build (`ditcount`: the shipped bracket with a counter in `armv8_set_dit`), every row of the suite at 20 ms, one thread. 632 rows in 304 families; **381 rows in 213 families enter the bracket at least once per timed loop, 251 rows in 91 families never do**. Entries per call is ditEntries / numCalls; a range means it changes with the input size. `bracketed_filters.txt` (145 filters) selects exactly the entering rows.

## Families that enter the bracket

| family | rows | bracket entries per call | sizes |
|---|---|---|---|
| CMAC-AES-128-CBC | 5 | 2 to 1,025 | 16, 256, 1350, 8192, 16384 |
| CMAC-AES-256-CBC | 5 | 2 to 1,025 | 16, 256, 1350, 8192, 16384 |
| TrustToken-Exp1-Batch10 begin_issuance | 1 | 70 | - |
| TrustToken-Exp2VOPRF-Batch10 begin_issuance | 1 | 70 | - |
| TrustToken-Exp2PMB-Batch10 begin_issuance | 1 | 70 | - |
| TrustToken-Exp1-Batch10 issue | 1 | 51 | - |
| TrustToken-Exp2PMB-Batch10 issue | 1 | 51 | - |
| EVP ECDH P-224 | 1 | 35 | - |
| EVP ECDH P-256 | 1 | 35 | - |
| EVP ECDH P-384 | 1 | 35 | - |
| EVP ECDH P-521 | 1 | 35 | - |
| EVP ECDH secp256k1 | 1 | 35 | - |
| TrustToken-Exp1-Batch10 finish_issuance | 1 | 30 | - |
| TrustToken-Exp2PMB-Batch10 finish_issuance | 1 | 30 | - |
| EVP ECDH X25519 | 1 | 25 | - |
| TrustToken-Exp1-Batch1 issue | 1 | 24 | - |
| TrustToken-Exp2PMB-Batch1 issue | 1 | 24 | - |
| TrustToken-Exp1-Batch1 generate_key | 1 | 18 | - |
| TrustToken-Exp1-Batch10 generate_key | 1 | 18 | - |
| TrustToken-Exp2PMB-Batch1 generate_key | 1 | 18 | - |
| TrustToken-Exp2PMB-Batch10 generate_key | 1 | 18 | - |
| TrustToken-Exp2VOPRF-Batch10 issue | 1 | 15 | - |
| TrustToken-Exp2VOPRF-Batch10 finish_issuance | 1 | 10 | - |
| FFDH 2048 | 1 | 8 | - |
| FFDH 4096 | 1 | 8 | - |
| RSA 2048 verify (fresh key) | 1 | 7 | - |
| RSA 3072 verify (fresh key) | 1 | 7 | - |
| RSA 4096 verify (fresh key) | 1 | 7 | - |
| RSA 8192 verify (fresh key) | 1 | 7 | - |
| AEAD-DES-EDE3-CBC-SHA1 seal | 5 | 6 to 7 | 16, 256, 1350, 8192, 16384 |
| AEAD-AES-128-CBC-SHA1 seal | 5 | 6 to 7 | 16, 256, 1350, 8192, 16384 |
| AEAD-AES-256-CBC-SHA1 seal | 5 | 6 to 7 | 16, 256, 1350, 8192, 16384 |
| TrustToken-Exp1-Batch1 begin_issuance | 1 | 7 | - |
| TrustToken-Exp2VOfPRF-Batch1 begin_issuance | 1 | 7 | - |
| TrustToken-Exp2PMB-Batch1 begin_issuance | 1 | 7 | - |
| ECDH P-224 | 1 | 6 | - |
| ECDH P-256 | 1 | 6 | - |
| ECDH P-384 | 1 | 6 | - |
| ECDH P-521 | 1 | 6 | - |
| ECDH secp256k1 | 1 | 6 | - |
| Generate P-224 with EVP_PKEY_keygen | 1 | 6 | - |
| Generate P-256 with EVP_PKEY_keygen | 1 | 6 | - |
| Generate P-384 with EVP_PKEY_keygen | 1 | 6 | - |
| Generate P-521 with EVP_PKEY_keygen | 1 | 6 | - |
| Generate secp256k1 with EVP_PKEY_keygen | 1 | 6 | - |
| MLDSA44 keygen | 1 | 6 | - |
| MLDSA65 keygen | 1 | 6 | - |
| MLDSA87 keygen | 1 | 6 | - |
| TrustToken-Exp2VOfPRF-Batch1 issue | 1 | 6 | - |
| EVP-AES-128-CBC decrypt | 5 | 5 | 16, 256, 1350, 8192, 16384 |
| EVP-AES-192-CBC decrypt | 5 | 5 | 16, 256, 1350, 8192, 16384 |
| EVP-AES-256-CBC decrypt | 5 | 5 | 16, 256, 1350, 8192, 16384 |
| ML-KEM-512 keygen | 1 | 5 | - |
| ML-KEM-768 keygen | 1 | 5 | - |
| ML-KEM-1024 keygen | 1 | 5 | - |
| AEAD-AES-128-CBC-SHA1 open | 5 | 5 | 16, 256, 1350, 8192, 16384 |
| AEAD-AES-256-CBC-SHA1 open | 5 | 5 | 16, 256, 1350, 8192, 16384 |
| Generate P-224 with EC_KEY_generate_key_fips | 1 | 5 | - |
| Generate P-256 with EC_KEY_generate_key_fips | 1 | 5 | - |
| Generate P-384 with EC_KEY_generate_key_fips | 1 | 5 | - |
| Generate P-521 with EC_KEY_generate_key_fips | 1 | 5 | - |
| Generate secp256k1 with EC_KEY_generate_key_fips | 1 | 5 | - |
| RSA 2048 signing | 1 | 4.24 | - |
| RSA 3072 signing | 1 | 4.19 | - |
| RSA 4096 signing | 1 | 4.11 | - |
| EVP-AES-128-GCM encrypt | 5 | 4 | 16, 256, 1350, 8192, 16384 |
| EVP-AES-128-GCM decrypt | 5 | 4 | 16, 256, 1350, 8192, 16384 |
| EVP-AES-192-GCM encrypt | 5 | 4 | 16, 256, 1350, 8192, 16384 |
| EVP-AES-192-GCM decrypt | 5 | 4 | 16, 256, 1350, 8192, 16384 |
| EVP-AES-256-GCM encrypt | 5 | 4 | 16, 256, 1350, 8192, 16384 |
| EVP-AES-256-GCM decrypt | 5 | 4 | 16, 256, 1350, 8192, 16384 |
| EVP-AES-128-CTR decrypt | 5 | 4 | 16, 256, 1350, 8192, 16384 |
| EVP-AES-192-CTR decrypt | 5 | 4 | 16, 256, 1350, 8192, 16384 |
| EVP-AES-256-CTR decrypt | 5 | 4 | 16, 256, 1350, 8192, 16384 |
| EVP-RC4 decrypt | 5 | 4 | 16, 256, 1350, 8192, 16384 |
| ECDSA P-224 signing | 1 | 4 | - |
| ECDSA P-256 signing | 1 | 4 | - |
| ECDSA P-384 signing | 1 | 4 | - |
| ECDSA P-521 signing | 1 | 4 | - |
| ECDSA secp256k1 signing | 1 | 4 | - |
| Generate P-224 with EC_KEY_generate_key | 1 | 4 | - |
| Generate P-256 with EC_KEY_generate_key | 1 | 4 | - |
| Generate P-384 with EC_KEY_generate_key | 1 | 4 | - |
| Generate P-521 with EC_KEY_generate_key | 1 | 4 | - |
| Generate secp256k1 with EC_KEY_generate_key | 1 | 4 | - |
| EVP-ChaCha20-Poly1305 encrypt | 5 | 4 | 16, 256, 1350, 8192, 16384 |
| EVP-ChaCha20-Poly1305 decrypt | 5 | 4 | 16, 256, 1350, 8192, 16384 |
| RSA 2048 verify (same key) | 1 | 4 | - |
| RSA 2048 private key parse | 1 | 4 | - |
| RSA 3072 verify (same key) | 1 | 4 | - |
| RSA 3072 private key parse | 1 | 4 | - |
| RSA 4096 verify (same key) | 1 | 4 | - |
| RSA 4096 private key parse | 1 | 4 | - |
| RSA 8192 signing | 1 | 4 | - |
| RSA 8192 verify (same key) | 1 | 4 | - |
| RSA 8192 private key parse | 1 | 4 | - |
| ML-KEM-512 encaps | 1 | 4 | - |
| ML-KEM-768 encaps | 1 | 4 | - |
| ML-KEM-1024 encaps | 1 | 4 | - |
| MLDSA44 signing | 1 | 4 | - |
| MLDSA65 signing | 1 | 4 | - |
| MLDSA87 signing | 1 | 4 | - |
| Ed25519 key generation | 1 | 4 | - |
| TrustToken-Exp2VOfPRF-Batch1 generate_key | 1 | 4 | - |
| TrustToken-Exp2VOPRF-Batch10 generate_key | 1 | 4 | - |
| EVP-AES-128-CTR encrypt | 5 | 3 | 16, 256, 1350, 8192, 16384 |
| EVP-AES-192-CTR encrypt | 5 | 3 | 16, 256, 1350, 8192, 16384 |
| EVP-AES-256-CTR encrypt | 5 | 3 | 16, 256, 1350, 8192, 16384 |
| EVP-AES-128-CBC encrypt | 5 | 3 | 16, 256, 1350, 8192, 16384 |
| EVP-AES-192-CBC encrypt | 5 | 3 | 16, 256, 1350, 8192, 16384 |
| EVP-AES-256-CBC encrypt | 5 | 3 | 16, 256, 1350, 8192, 16384 |
| AES-256-XTS encrypt init | 1 | 3 | - |
| AES-256-XTS decrypt init | 1 | 3 | - |
| AES-256-XTS decrypt | 5 | 3 | 16, 256, 1350, 8192, 16384 |
| EVP-RC4 encrypt | 5 | 3 | 16, 256, 1350, 8192, 16384 |
| CMAC-AES-128-CBC init | 1 | 3 | - |
| CMAC-AES-256-CBC init | 1 | 3 | - |
| RNG | 5 | 3 | 16, 256, 1350, 8192, 16384 |
| EC POINT P-224 mul public | 1 | 3 | - |
| EC POINT P-256 mul public | 1 | 3 | - |
| EC POINT P-384 mul public | 1 | 3 | - |
| EC POINT P-521 mul public | 1 | 3 | - |
| EC POINT secp256k1 mul public | 1 | 3 | - |
| SPAKE2 over Ed25519 | 1 | 3 | - |
| HRSS generate | 1 | 3 | - |
| HRSS encap | 1 | 3 | - |
| TrustToken-Exp1-Batch1 finish_issuance | 1 | 3 | - |
| TrustToken-Exp2PMB-Batch1 finish_issuance | 1 | 3 | - |
| AES-256-XTS encrypt | 5 | 2 | 16, 256, 1350, 8192, 16384 |
| EC POINT P-224 mul | 1 | 2 | - |
| EC POINT P-224 mul base | 1 | 2 | - |
| EC POINT P-256 mul | 1 | 2 | - |
| EC POINT P-256 mul base | 1 | 2 | - |
| EC POINT P-384 mul | 1 | 2 | - |
| EC POINT P-384 mul base | 1 | 2 | - |
| EC POINT P-521 mul | 1 | 2 | - |
| EC POINT P-521 mul base | 1 | 2 | - |
| EC POINT secp256k1 mul | 1 | 2 | - |
| EC POINT secp256k1 mul base | 1 | 2 | - |
| AEAD-AES-128-GCM open | 5 | 2 | 16, 256, 1350, 8192, 16384 |
| AEAD-AES-256-GCM open | 5 | 2 | 16, 256, 1350, 8192, 16384 |
| AEAD-DES-EDE3-CBC-SHA1 seal init | 1 | 2 | - |
| AEAD-AES-128-CBC-SHA1 seal init | 1 | 2 | - |
| AEAD-AES-256-CBC-SHA1 seal init | 1 | 2 | - |
| AEAD-AES-128-CBC-SHA1 open init | 1 | 2 | - |
| AEAD-AES-256-CBC-SHA1 open init | 1 | 2 | - |
| AEAD-AES-128-GCM-SIV open | 5 | 2 | 16, 256, 1350, 8192, 16384 |
| AEAD-AES-256-GCM-SIV open | 5 | 2 | 16, 256, 1350, 8192, 16384 |
| ECDH X25519 | 1 | 2 | - |
| AES-128 encrypt setup | 1 | 1 | - |
| AES-128 encrypt | 1 | 1 | - |
| AES-128 decrypt setup | 1 | 1 | - |
| AES-128 decrypt | 1 | 1 | - |
| AES-192 encrypt setup | 1 | 1 | - |
| AES-192 encrypt | 1 | 1 | - |
| AES-192 decrypt setup | 1 | 1 | - |
| AES-192 decrypt | 1 | 1 | - |
| AES-256 encrypt setup | 1 | 1 | - |
| AES-256 encrypt | 1 | 1 | - |
| AES-256 decrypt setup | 1 | 1 | - |
| AES-256 decrypt | 1 | 1 | - |
| EVP-AES-128-GCM encrypt init | 1 | 1 | - |
| EVP-AES-128-GCM decrypt init | 1 | 1 | - |
| EVP-AES-192-GCM encrypt init | 1 | 1 | - |
| EVP-AES-192-GCM decrypt init | 1 | 1 | - |
| EVP-AES-256-GCM encrypt init | 1 | 1 | - |
| EVP-AES-256-GCM decrypt init | 1 | 1 | - |
| EVP-AES-128-CTR encrypt init | 1 | 1 | - |
| EVP-AES-128-CTR decrypt init | 1 | 1 | - |
| EVP-AES-192-CTR encrypt init | 1 | 1 | - |
| EVP-AES-192-CTR decrypt init | 1 | 1 | - |
| EVP-AES-256-CTR encrypt init | 1 | 1 | - |
| EVP-AES-256-CTR decrypt init | 1 | 1 | - |
| EVP-AES-128-CBC encrypt init | 1 | 1 | - |
| EVP-AES-128-CBC decrypt init | 1 | 1 | - |
| EVP-AES-192-CBC encrypt init | 1 | 1 | - |
| EVP-AES-192-CBC decrypt init | 1 | 1 | - |
| EVP-AES-256-CBC encrypt init | 1 | 1 | - |
| EVP-AES-256-CBC decrypt init | 1 | 1 | - |
| EVP-RC4 encrypt init | 1 | 1 | - |
| EVP-RC4 decrypt init | 1 | 1 | - |
| EVP-ChaCha20-Poly1305 encrypt init | 1 | 1 | - |
| EVP-ChaCha20-Poly1305 decrypt init | 1 | 1 | - |
| ML-KEM-512 decaps | 1 | 1 | - |
| ML-KEM-768 decaps | 1 | 1 | - |
| ML-KEM-1024 decaps | 1 | 1 | - |
| MLDSA44 verify | 1 | 1 | - |
| MLDSA65 verify | 1 | 1 | - |
| MLDSA87 verify | 1 | 1 | - |
| AEAD-AES-128-GCM seal init | 1 | 1 | - |
| AEAD-AES-128-GCM seal | 5 | 1 | 16, 256, 1350, 8192, 16384 |
| AEAD-AES-128-GCM open init | 1 | 1 | - |
| AEAD-AES-256-GCM seal init | 1 | 1 | - |
| AEAD-AES-256-GCM seal | 5 | 1 | 16, 256, 1350, 8192, 16384 |
| AEAD-AES-256-GCM open init | 1 | 1 | - |
| AEAD-ChaCha20-Poly1305 seal init | 1 | 1 | - |
| AEAD-ChaCha20-Poly1305 seal | 5 | 1 | 16, 256, 1350, 8192, 16384 |
| AEAD-AES-128-GCM-SIV seal init | 1 | 1 | - |
| AEAD-AES-128-GCM-SIV seal | 5 | 1 | 16, 256, 1350, 8192, 16384 |
| AEAD-AES-256-GCM-SIV seal init | 1 | 1 | - |
| AEAD-AES-256-GCM-SIV seal | 5 | 1 | 16, 256, 1350, 8192, 16384 |
| AEAD-AES-128-GCM-SIV open init | 1 | 1 | - |
| AEAD-AES-256-GCM-SIV open init | 1 | 1 | - |
| AEAD-AES-128-CCM-Bluetooth seal init | 1 | 1 | - |
| AEAD-AES-128-CCM-Bluetooth seal | 5 | 1 | 16, 256, 1350, 8192, 16384 |
| Ed25519 signing | 1 | 1 | - |
| Curve25519 base-point multiplication | 1 | 1 | - |
| Curve25519 arbitrary point multiplication | 1 | 1 | - |
| TrustToken-Exp2VOfPRF-Batch1 finish_issuance | 1 | 1 | - |
| TrustToken-Exp2VOfPRF-Batch1 redeem | 1 | 1 | - |
| TrustToken-Exp2VOPRF-Batch10 redeem | 1 | 1 | - |
| Ed25519 PKCS#8 v1 decode | 1 | 1 | - |
| Ed25519 PKCS#8 v2 decode | 1 | 1 | - |

## Families that never enter the bracket

These rows run the same instructions in every arm of the experiment; any difference measured on them is noise or a layout effect.

| family | rows |
|---|---|
| MD4 | 5 |
| MD5 | 5 |
| SHA-1 | 5 |
| SHA-224 | 5 |
| SHA-256 | 5 |
| SHA-384 | 5 |
| SHA-512 | 5 |
| SHA3-224 | 5 |
| SHA3-256 | 5 |
| SHA3-384 | 5 |
| SHA3-512 | 5 |
| KECCAK-256 | 5 |
| SHAKE-128 | 5 |
| SHAKE-256 | 5 |
| SHAKE256-x4 (Absorb) | 5 |
| SHAKE256-x4 (Squeeze) | 5 |
| RIPEMD-160 | 5 |
| MD5-SHA-1 | 5 |
| HMAC-MD5 init | 1 |
| HMAC-MD5 | 5 |
| HMAC-SHA1 init | 1 |
| HMAC-SHA1 | 5 |
| HMAC-SHA256 init | 1 |
| HMAC-SHA256 | 5 |
| HMAC-SHA384 init | 1 |
| HMAC-SHA384 | 5 |
| HMAC-SHA512 init | 1 |
| HMAC-SHA512 | 5 |
| HMAC-SHA3-224 init | 1 |
| HMAC-SHA3-224 | 5 |
| HMAC-SHA3-256 init | 1 |
| HMAC-SHA3-256 | 5 |
| HMAC-SHA3-384 init | 1 |
| HMAC-SHA3-384 | 5 |
| HMAC-SHA3-512 init | 1 |
| HMAC-SHA3-512 | 5 |
| HMAC-MD5-OneShot | 5 |
| HMAC-SHA1-OneShot | 5 |
| HMAC-SHA256-OneShot | 5 |
| HMAC-SHA384-OneShot | 5 |
| HMAC-SHA512-OneShot | 5 |
| HMAC-SHA3-224-OneShot | 5 |
| HMAC-SHA3-256-OneShot | 5 |
| HMAC-SHA3-384-OneShot | 5 |
| HMAC-SHA3-512-OneShot | 5 |
| ECDSA P-224 verify | 1 |
| ECDSA P-256 verify | 1 |
| ECDSA P-384 verify | 1 |
| ECDSA P-521 verify | 1 |
| ECDSA secp256k1 verify | 1 |
| EC POINT P-224 dbl | 1 |
| EC POINT P-224 add | 1 |
| EC POINT P-256 dbl | 1 |
| EC POINT P-256 add | 1 |
| EC POINT P-384 dbl | 1 |
| EC POINT P-384 add | 1 |
| EC POINT P-521 dbl | 1 |
| EC POINT P-521 add | 1 |
| EC POINT secp256k1 dbl | 1 |
| EC POINT secp256k1 add | 1 |
| scrypt (N = 1024, r = 8, p = 16) | 1 |
| scrypt (N = 16384, r = 8, p = 1) | 1 |
| RSA 2048 key-gen | 1 |
| RSA 3072 key-gen | 1 |
| RSA 4096 key-gen | 1 |
| Ed25519 verify | 1 |
| RSA FIPS 2048 key-gen | 1 |
| RSA FIPS 3072 key-gen | 1 |
| RSA FIPS 4096 key-gen | 1 |
| HRSS decap | 1 |
| BLAKE2b-256 | 5 |
| CRYPTO_refcount_inc 1000 iterations with 1 threads | 1 |
| CRYPTO_sysrand | 5 |
| hash-to-curve P256_XMD:SHA-256_SSWU_RO_ | 1 |
| hash-to-curve P384_XMD:SHA-384_SSWU_RO_ | 1 |
| hash-to-scalar P384_XMD:SHA-512 | 1 |
| TrustToken-Exp1-Batch1 begin_redemption | 1 |
| TrustToken-Exp1-Batch1 redeem | 1 |
| TrustToken-Exp1-Batch10 begin_redemption | 1 |
| TrustToken-Exp1-Batch10 redeem | 1 |
| TrustToken-Exp2VOfPRF-Batch1 begin_redemption | 1 |
| TrustToken-Exp2VOPRF-Batch10 begin_redemption | 1 |
| TrustToken-Exp2PMB-Batch1 begin_redemption | 1 |
| TrustToken-Exp2PMB-Batch1 redeem | 1 |
| TrustToken-Exp2PMB-Batch10 begin_redemption | 1 |
| TrustToken-Exp2PMB-Batch10 redeem | 1 |
| Ed25519 PKCS#8 v1 encode | 1 |
| Ed25519 PKCS#8 v2 encode | 1 |
| base64 decode | 1 |
| SipHash-2-4 | 5 |
| Jitter | 5 |

## Where the macro is in the source

`SET_DIT_AUTO_RESET` appears 181 times in 20 files (the bracket is one macro at the top of each public entry point).

| file | sites |
|---|---|
| `crypto/fipsmodule/rsa/rsa.c` | 46 |
| `crypto/fipsmodule/evp/evp.c` | 37 |
| `crypto/fipsmodule/evp/evp_ctx.c` | 23 |
| `crypto/fipsmodule/dh/dh.c` | 20 |
| `crypto/fipsmodule/evp/digestsign.c` | 10 |
| `crypto/fipsmodule/cipher/cipher.c` | 7 |
| `crypto/fipsmodule/cipher/aead.c` | 6 |
| `crypto/fipsmodule/curve25519/curve25519.c` | 5 |
| `crypto/fipsmodule/rand/ctrdrbg.c` | 5 |
| `crypto/evp_extra/p_dh_asn1.c` | 4 |
| `crypto/fipsmodule/aes/aes.c` | 4 |
| `crypto/fipsmodule/ec/ec.c` | 3 |
| `crypto/fipsmodule/hkdf/hkdf.c` | 2 |
| `crypto/fipsmodule/kdf/sskdf.c` | 2 |
| `crypto/fipsmodule/tls/kdf.c` | 2 |
| `crypto/evp_extra/evp_asn1.c` | 1 |
| `crypto/fipsmodule/evp/p_hkdf.c` | 1 |
| `crypto/fipsmodule/kdf/kbkdf.c` | 1 |
| `crypto/fipsmodule/rsa/rsa_impl.c` | 1 |
| `crypto/fipsmodule/sshkdf/sshkdf.c` | 1 |
