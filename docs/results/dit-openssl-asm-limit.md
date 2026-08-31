# The pass cannot reach OpenSSL's crypto on aarch64

**Measured 2026-08-31.** A scope limit, not a bug, and it belongs next to the LTO
and prebuilt-library limits rather than being discovered by a reader.

## The finding

OpenSSL implements its crypto hot paths on aarch64 in **hand-written assembly**,
generated from perlasm. The taint analysis runs on MIR lowered from C, so it
cannot see them, cannot taint through them, and cannot place a switch inside them.

19 perlasm generators in `crypto/*/asm/*armv8*.pl` and `*-armx.pl` (OpenSSL
3.5.4), covering exactly the primitives a TLS server spends its time in:

| primitive | generator | what it is |
|---|---|---|
| AES | `aesv8-armx.pl`, `vpaes-armv8.pl`, `bsaes-armv8.pl` | the bulk record cipher |
| AES-GCM | `aes-gcm-armv8_64.pl`, `ghashv8-armx.pl` | the default TLS 1.3 AEAD |
| ChaCha20-Poly1305 | `chacha-armv8.pl`, `poly1305-armv8.pl` | the other TLS 1.3 AEAD |
| P-256 | `ecp_nistz256-armv8.pl` | ECDHE and ECDSA |
| bignum | `armv8-mont.pl` | RSA, DH, the Montgomery ladder |
| SHA-1/256/512 | `sha*-armv8.pl` | the handshake transcript and HKDF |

Confirmed in the shipped library: `nm` on Homebrew's `libcrypto.dylib` exports
`_aes_v8_encrypt`, `_aes_v8_ctr32_encrypt_blocks`, `_bn_mul_mont`,
`_ChaCha20_ctr32`, `_ecp_nistz256_mul_mont` and 35 more such symbols.

## Why this is worse than the SQLCipher case

`sqlcipher.md` records the pass emitting 25 `MSR DIT` sites on an OpenSSL build
with **zero on any cipher instruction**, costing +2.27% for no protection. That was
attributed to the AES living in a prebuilt `libcrypto.dylib`, which building from
source would fix.

**Building from source does not fix it.** The AES is assembly in the source tree.
The same objection applies to any hardening pass that works on compiler IR, on
this architecture, against this library.

## The escape hatch, and why it is a strawman

`./Configure no-asm` forces the C implementations, which the pass can instrument.
Two reasons not to report numbers from it:

- Nobody deploys `no-asm` OpenSSL. It is several times slower.
- The C AES is the T-table implementation, whose real leak is **cache timing from
  data-dependent table indices** - which DIT does not cover. The project already
  concluded that "AES is a bad motivating workload" for precisely this reason.

## What IS reachable

`ssl/` contains no assembly at all. The TLS state machine and, in particular, the
TLS 1.3 key schedule in `ssl/tls13_enc.c` are C, handle raw key material, and run
once per handshake:

```
tls13_hkdf_expand / tls13_hkdf_expand_ex     HKDF-Expand-Label
tls13_derive_key / tls13_derive_iv           traffic key and IV
tls13_derive_finishedkey                     Finished MAC key
tls13_generate_secret                        the secret ladder
```

So a claim of the form *"the ABI reduces DIT overhead on TLS handshake key
handling"* is testable. A claim of the form *"we harden Nginx's TLS"* is not, on
this architecture.

## How to state it

The pass has three known reach limits, and they compound:

1. **Cross-TU** - taint is module-scoped, so a secret entering another TU needs
   its own seed line.
2. **Prebuilt libraries** - a dependency shipped as a binary cannot be
   instrumented (SQLCipher/OpenSSL, 25 switches protecting nothing).
3. **Hand-written assembly** - and on aarch64 this is where every serious crypto
   library puts its hot loops.

Limit 3 is the one with no workaround inside the compiler. It bounds which
libraries this approach can protect at all, independently of how good the
placement is: **libsodium works because its primitives are C.**
