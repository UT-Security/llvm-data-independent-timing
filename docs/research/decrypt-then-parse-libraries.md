# Decrypt-then-parse research memo: Library survey: where decrypt-then-parse glue sits in 13 C/C++ projects, with function-pointer hazards and harness feasibility

**Written 2026-09-03** as input to `paper_experiments/10-mbedtls-session-ticket/README.md`.
Sources were fetched and quoted directly; every quantitative claim carries a URL, and
claims marked "unverified" were not confirmed against a primary source and must not
be quoted without one. The original memo follows unedited.

---

# Research B: decrypt-then-parse glue in C/C++ crypto libraries

Date: 2026-09-02. Sources were read from raw files (mbedTLS: full shallow clone of
branch `mbedtls-3.6`, head `d6664ceac9a126267154729dfbbb9ce4a6d8cfa9`, "Merge pull
request #10920", 2026-09-02; other libraries: branch heads `openssl-3.5`, BoringSSL
`main`, wolfSSL `master`, libsodium `master`, openssh-portable `master`, Tor `main`,
krb5 `master`, httpd `trunk`, Linux `master`, OpenVPN `master`, strongSwan `master`,
GnuPG `master`, all as of 2026-09-02). Line numbers below are from those heads.
Cycle figures are estimates from instruction counts, not measurements, unless stated.

Conventions used in the verdict column:
- CT = the glue is straight-line constant-time C by construction (no secret branch,
  no secret index); "branchy" = it branches on decrypted bytes.
- direct = reachable from the primitive's caller through direct calls only; "fp" =
  a function-pointer hop sits between primitive and glue (blocks interprocedural
  placement).
- region = estimated work per event in the secret region (the part that would be
  inside DIT switches).

## 0. Ranked verdict

1. mbedTLS TLS 1.2 RSA key-exchange premaster path (best semantic fit, CT, ~2-3 us
   glue, one function-pointer hazard that a second seed bridges).
2. mbedTLS TLS 1.2 CBC MAC-then-encrypt record path in `mbedtls_ssl_decrypt_buf`
   (largest CT region, ~50-100 us per >256-byte record, all direct; overlaps with
   experiment 03's secret lane, and needs Encrypt-then-MAC disabled).
3. mbedTLS TLS 1.3 session-ticket resumption chain (`mbedtls_ssl_ticket_parse` ->
   `ssl_tls13_session_load` -> `mbedtls_ssl_set_hs_psk` -> binder HKDF ->
   `mbedtls_ct_memcmp`): literally "session-state deserialization", ~3-5 us, but
   enters through the `f_ticket_parse` function pointer.

Backups outside mbedTLS, in order: BoringSSL (CT base64 + CT RSA unpad + C++ TLS
layer, asm switchable off with `OPENSSL_NO_ASM`), wolfSSL (CT `TimingPadVerify` /
`MaskMac` / `RsaUnPad`, asm auto-enabled on aarch64 hosts but `--disable-armasm`
works). Everything else fails at least one hard constraint (see table).

## 1. mbedTLS 3.6 (branch head d6664cea)

### 1.0 Build facts that matter for the pass

- `MBEDTLS_USE_PSA_CRYPTO` is OFF by default:
  `include/mbedtls/mbedtls_config.h:2230` is `//#define MBEDTLS_USE_PSA_CRYPTO`.
  https://github.com/Mbed-TLS/mbedtls/blob/d6664ceac9a126267154729dfbbb9ce4a6d8cfa9/include/mbedtls/mbedtls_config.h#L2230
  Consequence: TLS 1.2 record protection, ticket AEAD, PBES2/PKCS12 and RSA go
  through the legacy `mbedtls_cipher_*` / `mbedtls_md_*` / `mbedtls_pk_*` layers.
  TLS 1.3 always uses PSA regardless (docs/use-psa-crypto.md: "It also has no
  effect on most of the TLS 1.3 code, which always uses PSA crypto").
- Dispatch mechanisms (this decides "direct vs fp"):
  - `mbedtls_cipher_*`: FUNCTION POINTERS. `library/cipher.c:679,722,1107` call
    `mbedtls_cipher_get_base(ctx->cipher_info)->cbc_func(...)`, `:612` `->ecb_func`,
    `:771` `->ctr_func`; PKCS7 unpadding is `ctx->get_padding(...)` at `:1117`.
    AEAD entry `mbedtls_cipher_aead_decrypt` (`cipher.c:1511`) switches on mode and
    calls `mbedtls_gcm_auth_decrypt` directly, but GCM's block cipher then goes back
    through `mbedtls_cipher_update` (fp) because `MBEDTLS_BLOCK_CIPHER_C` is only
    auto-enabled when `MBEDTLS_CIPHER_C` is off (`gcm.c:105-109`).
  - `mbedtls_md_*`: DIRECT. `library/md.c:273,448,527` are `switch (md_info->type)`
    into `mbedtls_sha256_update` etc. So HMAC, `mbedtls_ct_hmac`, the TLS 1.2 PRF and
    PBKDF2 are direct-call chains.
  - `mbedtls_pk_decrypt`: FUNCTION POINTER. `library/pk.c:1486`
    `ctx->pk_info->decrypt_func(...)` -> `pk_wrap.c:362 rsa_decrypt_wrap` ->
    `mbedtls_rsa_pkcs1_decrypt` (direct from there on).
  - SSL layer pointers: `conf->f_ticket_parse` / `f_ticket_write`
    (`ssl_tls12_server.c:503`, `ssl_tls13_server.c:219`), `conf->f_rng`
    (`ssl_tls12_server.c:3452`), `handshake->tls_prf` / `calc_verify` /
    `calc_finished` (`ssl_tls.c:7037-7045`, used at `ssl_tls.c:7252` and `:8273`),
    BIO callbacks `f_send`/`f_recv`.
  - PSA driver wrappers (`psa_crypto_driver_wrappers.h`) are generated
    `switch (location)` code -> direct calls into `mbedtls_psa_aead_decrypt`
    (`psa_crypto_aead.c:229`, which switches on alg to `mbedtls_gcm_auth_decrypt`
    `:271`, `mbedtls_ccm_auth_decrypt` `:259`, `mbedtls_chachapoly_auth_decrypt`
    `:287`) and `psa_crypto_hash.c` (`switch (operation->alg)` `:24`). Direct.
- Assembly / intrinsics in the default config (the project's "no asm by default"
  claim needs two footnotes):
  - `MBEDTLS_HAVE_ASM` is ON (`mbedtls_config.h:52`). `library/bn_mul.h:241`
    `#if defined(__aarch64__) ...` provides inline-asm `MULADDC` for bignum, i.e.
    RSA private operations run inline asm. Undefining `MBEDTLS_HAVE_ASM` removes it
    (and makes `mbedtls_ct_compiler_opaque` fall back to a volatile read,
    `constant_time_impl.h:60-80`).
  - `MBEDTLS_AESCE_C` is ON (`:2385`): `library/aesce.c:165,195` use `vaeseq_u8`
    intrinsics under `pragma clang attribute push (target("aes"))` (`:87-94`).
    Compiler-generated, not hand-written, runtime-detected. SHA-2 intrinsics are
    OFF (`:3510,3519,3630` commented), so SHA-256 is plain C.
  https://github.com/Mbed-TLS/mbedtls/blob/d6664ceac9a126267154729dfbbb9ce4a6d8cfa9/library/bn_mul.h#L241

### 1a. `library/ssl_msg.c` `mbedtls_ssl_decrypt_buf` (line 1482)

https://github.com/Mbed-TLS/mbedtls/blob/d6664ceac9a126267154729dfbbb9ce4a6d8cfa9/library/ssl_msg.c#L1482

Called directly from `ssl_prepare_record_content` (`:4108`, call at `:4133`), which
is reached from `ssl_get_next_record` / `mbedtls_ssl_read_record` (`:4333`), all
direct. Paths, in code order:

1. STREAM (NULL cipher, `:1536`): length check only, then falls to the MAC block.
2. AEAD (`:1548-1660`): builds nonce and additional data (public), then
   `psa_aead_decrypt` (`:1608`, PSA build) or `mbedtls_cipher_auth_decrypt_ext`
   (`:1621`, default build; fp inside). `auth_done++`. After the MAC/padding block,
   for TLS 1.3 only: `ssl_parse_inner_plaintext(data, &rec->data_len, &rec->type)`
   (`:2098`), defined at `:597`:
   ```c
   do { if (remaining == 0) return -1; remaining--; } while (content[remaining] == 0);
   *content_size = remaining; *rec_type = content[remaining];
   ```
   This is the TLS 1.3 padding strip: a data-dependent loop and a secret-derived
   content type that then drives `mbedtls_ssl_handle_message_type` control flow.
   NOT constant-time, and tiny: mbedTLS pads to 16-byte granularity by default
   (`MBEDTLS_SSL_CID_TLS1_3_PADDING_GRANULARITY`), so 1-16 iterations, ~10-60
   cycles. Region far below the ~2.5 us crossover measured in experiment 03.
   Taint hazard: `rec->type` becomes secret and feeds a `switch`, so the pass will
   either stop at the branch or flood the record dispatcher.
3. CBC, Encrypt-then-MAC (`ssl_mode == MBEDTLS_SSL_MODE_CBC_ETM`, `:1704-1815`):
   HMAC over `add_data || ciphertext` via `psa_mac_verify_*` or
   `mbedtls_md_hmac_update/finish` (direct), compared with `mbedtls_ct_memcmp`
   (`:1798`). Then `mbedtls_cipher_crypt` (fp) or `psa_cipher_*`, then the CT
   padding check below with `auth_done == 1` (`:1916-1920`). Glue that touches
   plaintext: `padlen = data[rec->data_len - 1]` (`:1912`),
   `mbedtls_ct_uint_ge` / `mbedtls_ct_bool_and` / `mbedtls_ct_size_if_else_0`
   (`:1916-1920`), then the 256-read padding loop (`:1950-1972`). CT, small:
   ~256 x ~8 ops = ~2k instructions plus the HMAC over ciphertext (public input,
   secret key). Estimated glue ~1k-3k cycles.
4. CBC, MAC-then-encrypt (default TLS 1.2 CBC when either peer disables EtM; note
   `MBEDTLS_SSL_ENCRYPT_THEN_MAC` is ON by default, `mbedtls_config.h:1738`, so the
   harness must call `mbedtls_ssl_conf_encrypt_then_mac(conf, MBEDTLS_SSL_ETM_DISABLED)`
   on one side). After `mbedtls_cipher_crypt` (fp into `aes_crypt_cbc_wrap`):
   - `padlen = data[rec->data_len - 1]` (`:1912`); CT length check
     `mbedtls_ct_uint_ge(rec->data_len, transform->maclen + padlen + 1)` (`:1932`).
   - Padding scan (`:1950-1972`): "always perform exactly min(256, plaintext_len)
     reads", `volatile unsigned char *const check = data`, per byte
     `mbedtls_ct_uint_ge`, `mbedtls_ct_size_if_else_0`, `mbedtls_ct_uint_eq`,
     `pad_count += increment`; then `correct = mbedtls_ct_bool_and(mbedtls_ct_uint_eq(pad_count, padlen), correct)`,
     `padlen = mbedtls_ct_size_if_else_0(correct, padlen)`, `rec->data_len -= padlen`.
   - `mbedtls_ct_hmac(...)` (`:2044` PSA / `:2050` legacy) with
     `min_len = max(0, max_len - 256)`, `max_len = data_len + padlen`. Legacy body
     (`:187-315`): HMAC inner hash up to `min_len`, then
     `for (offset = min_len; offset <= max_len; offset++) { mbedtls_md_clone; mbedtls_md_finish; mbedtls_ct_memcpy_if(mbedtls_ct_uint_eq(offset, data_len_secret), ...); mbedtls_md_update(ctx, data+offset, 1) }`
     (`:246-255`), then outer hash. Up to 257 clone+finish pairs; each finish is
     1-2 SHA-256 compressions in C (~300-500 cycles each) plus a ~110-byte context
     copy. Estimate 130k-260k cycles for records >= 256 bytes.
   - `mbedtls_ct_memcpy_offset(mac_peer, data, rec->data_len, min_len, max_len, maclen)`
     (`:2060`; body `constant_time.c:214`): 257 x `mbedtls_ct_memcpy_if` of
     `maclen` bytes, ~30-40k cycles.
   - `mbedtls_ct_memcmp(mac_peer, mac_expect, maclen)` (`:2071`; body
     `constant_time.c:67`, volatile 4-byte XOR-accumulate).
   - Final `if (correct == MBEDTLS_CT_FALSE) return MBEDTLS_ERR_SSL_INVALID_MAC;`
     (`:2085`), the single public branch after all secret work.
   All CT and all direct from `mbedtls_ssl_decrypt_buf`; the underlying hash is
   direct via `md.c` switches. Secret region per record ~150-300k cycles
   (~50-100 us at 3 GHz), i.e. 20-40x above the crossover. Secret bytes: the
   decrypted record plaintext (padding bytes, MAC bytes) and the MAC key.

What a human annotating "the crypto call" misses here: everything after
`mbedtls_cipher_crypt` returns (padding scan, `ct_hmac`, `memcpy_offset`,
`memcmp`) lives in the same function but is not a primitive call.

Public lane: `ssl_parse_record_header` (`:3859`), sequence-number / epoch / CID
handling, `ssl_extract_add_data_from_record`, and the app-level loop over records
(the demux the project already tried). Knob: record size (16 B to 16 KB; the
`ct_hmac` loop saturates at 256, so region size grows with record only through the
base hash) and record count.

Harness: yes. Two contexts over memory buffers, TLS 1.2, ciphersuite
`TLS-RSA-WITH-AES-128-CBC-SHA256` (or ECDHE-...-CBC-SHA256), ETM disabled on one
side, then `mbedtls_ssl_write` / `mbedtls_ssl_read` in a loop. No sockets.

### 1b. `library/constant_time.c` / `constant_time_impl.h`: the `mbedtls_ct_*` family and callers

Definitions (out-of-line, `constant_time.c`): `mbedtls_ct_memcmp` (67),
`mbedtls_ct_memcmp_partial` (120), `mbedtls_ct_memmove_left` (150),
`mbedtls_ct_memcpy_if` (169), `mbedtls_ct_memcpy_offset` (214),
`mbedtls_ct_zeroize_if` (231). Inline (`constant_time_impl.h`): `mbedtls_ct_bool`,
`bool_and/or/not/ne`, `bool_if`, `bool_if_else_0`, `compiler_opaque`, `error_if`,
`error_if_else_0`, `if`, `mpi_uint_if`, `mpi_uint_if_else_0`, `size_if`,
`size_if_else_0`, `uchar_in_range_if`, `uint_eq/ne/lt/gt/ge/le`, `uint_if`,
`uint_if_else_0`. Also `mbedtls_ct_hmac` (ssl_msg.c 66/187),
`mbedtls_ct_rsaes_pkcs1_v15_unpadding` (rsa.c 420), `mbedtls_ct_base64_enc_char` /
`mbedtls_ct_base64_dec_value` (base64.c 26/41).

Callers (grep of `library/*.c`, excluding constant_time*):

| function | callers (file:line) |
|---|---|
| `mbedtls_ct_memcmp` | ccm.c:590, chachapoly.c:320, cipher.c:1298,1319, gcm.c:760, nist_kw.c:358,402, psa_crypto.c:2514,2584,3013, psa_crypto_mac.c:445, rsa.c:2049,2530,2774, ssl_cookie.c:354, ssl_msg.c:1798,2071, ssl_tls.c:8567 (Finished), ssl_tls12_client.c:627,629 (renego info), ssl_tls12_server.c:116,3518 (PSK identity), ssl_tls13_generic.c:1153 (Finished), ssl_tls13_server.c:383 (PSK identity),451 (PSK binder) |
| `mbedtls_ct_memcmp_partial` | nist_kw.c:422 |
| `mbedtls_ct_memmove_left` | rsa.c:519 |
| `mbedtls_ct_memcpy_if` | bignum_core.c:576, ssl_msg.c:147,250 (ct_hmac), ssl_tls12_server.c:3474 (fake PMS select) |
| `mbedtls_ct_memcpy_offset` | ssl_msg.c:2060 |
| `mbedtls_ct_zeroize_if` | rsa.c:503 |
| `mbedtls_ct_uchar_in_range_if` | base64.c:32-36,48-52 |
| `mbedtls_ct_error_if` / `_else_0` | nist_kw.c:416, psa_crypto_cipher.c:595,599, rsa.c:491,494,559,565, cipher.c:1141,1424 (PKCS7 unpad result) |
| `mbedtls_ct_size_if` / `_else_0` | bignum.c:452,90, ssl_msg.c:1920,1936,1966,1968,1978 |
| `mbedtls_ct_uint_if` / `_else_0` | bignum.c:51, rsa.c:477,509,461,2058, cmac.c:85 |
| `mbedtls_ct_bool` | bignum*.c (many), cipher.c:910,965, cmac.c:85, psa_crypto.c:4704,4858, rsa.c:448,2044,2049, ssl_tls12_server.c:3438 |
| `mbedtls_ct_uint_eq` | bignum_core.c:605, cipher.c:862,958 (PKCS7 unpad), rsa.c:459,556,562,2057, ssl_msg.c:147,250,1967,1971 |
| `mbedtls_ct_uint_ne` | cipher.c:869,916,997, rsa.c:453,2062, ssl_tls12_server.c:3439-3441 |
| `mbedtls_ct_uint_lt` | bignum_core.c:147,156, psa_crypto_cipher.c:598 |
| `mbedtls_ct_uint_gt` | cipher.c:861,957, nist_kw.c:416, rsa.c:468,483 |
| `mbedtls_ct_uint_ge` | bignum_core.c:120, cipher.c:868,963, ssl_msg.c:1916,1932,1965 |
| `mbedtls_ct_uint_le` | no callers |
| `mbedtls_ct_mpi_uint_if` / `_else_0` | bignum_core.c:184,199,200,434, bignum.c:135 |
| `mbedtls_ct_hmac` | ssl_msg.c:2044,2050 only |
| `mbedtls_ct_rsaes_pkcs1_v15_unpadding` | rsa.c:2126 only |

Notable non-record CT glue this surfaces: `cipher.c:848-878 get_pkcs_padding`
(PKCS7 unpad of the last block, CT, but reached via `ctx->get_padding` fp) used by
PBES2/PKCS12/PEM decryption, and `base64.c` CT decode used by PEM.

### 1c. RSA: `library/rsa.c` and the TLS 1.2 premaster path

https://github.com/Mbed-TLS/mbedtls/blob/d6664ceac9a126267154729dfbbb9ce4a6d8cfa9/library/rsa.c#L420
https://github.com/Mbed-TLS/mbedtls/blob/d6664ceac9a126267154729dfbbb9ce4a6d8cfa9/library/ssl_tls12_server.c#L3399

- `mbedtls_rsa_rsaes_pkcs1_v15_decrypt` (`rsa.c:2098`): `mbedtls_rsa_private`
  (`:1476`, bignum with blinding, inline asm under `MBEDTLS_HAVE_ASM`) into a stack
  `buf[MBEDTLS_MPI_MAX_SIZE]`, then `mbedtls_ct_rsaes_pkcs1_v15_unpadding(buf, ilen, output, output_max_len, olen)`
  (`:2126`). Direct.
- `mbedtls_ct_rsaes_pkcs1_v15_unpadding` (`:420-530`): fully CT by construction
  (comment at `:426-436` lists timing, memory access and branch side channels).
  Loop over all `ilen` bytes (`:458-462`), `mbedtls_ct_error_if` return code,
  `mbedtls_ct_zeroize_if(bad|too_large, input+11, ilen-11)` (`:503`),
  `mbedtls_ct_memmove_left(input + ilen - plaintext_max_size, plaintext_max_size, ...)`
  (`:519`; body `constant_time.c:150` is O(total^2): `total` outer x `total-1`
  inner `mbedtls_ct_uint_if`), then a fixed-size `memcpy` to `output`.
  In the TLS path `output_max_len = 48`, so `plaintext_max_size = 48` and memmove
  is 48 x 47 = 2256 inner steps (~10k instructions). Total unpadding for RSA-2048:
  ~254-byte scan (~2k instr) + zeroize (~1k) + memmove (~10k) = ~13k instructions,
  ~4-6k cycles, ~1.5-2 us.
- `mbedtls_rsa_rsaes_oaep_decrypt` (`:1971`): `mgf_mask` twice (direct MD),
  then CT checks `:2044-2062` (`mbedtls_ct_memcmp(lhash, p, hlen)`, CT pad_len
  loop over `ilen - 2*hlen - 2` bytes), but ends with a plain
  `if (bad != MBEDTLS_CT_FALSE) goto cleanup;` and a length-dependent `memcpy`
  (`:2076-2084`). "Constant-time up to the accept/reject branch", and the output
  copy length is secret-dependent. Weaker than v1.5.
- TLS 1.2 server (`ssl_tls12_server.c`): `ssl_parse_client_key_exchange` (`:3537`)
  -> `ssl_parse_encrypted_pms` (`:3399`) -> `ssl_decrypt_encrypted_pms` (`:3313`)
  -> `mbedtls_pk_decrypt(private_key, p, len, peer_pms, ...)` (`:3392`, FP into
  `rsa_decrypt_wrap` -> `mbedtls_rsa_pkcs1_decrypt` -> v1.5 decrypt above).
  Back in `ssl_parse_encrypted_pms`: comment "Avoid data-dependent branches while
  checking for invalid padding, to protect against timing-based Bleichenbacher-type
  attacks"; `diff = mbedtls_ct_bool(ret) | mbedtls_ct_uint_ne(peer_pmslen, 48) | mbedtls_ct_uint_ne(peer_pms[0], ver[0]) | ...`
  (`:3438-3441`), `ssl->conf->f_rng(..., fake_pms, 48)` always (`:3452`, FP into
  CTR-DRBG, ~1-2k cycles), `mbedtls_ct_memcpy_if(diff, pms, fake_pms, peer_pms, 48)`
  (`:3474`). Then `mbedtls_ssl_derive_keys` (`ssl_tls.c:7272`, direct) ->
  `ssl_compute_master` (`:7075`) -> `handshake->tls_prf(handshake->premaster, ...)`
  (`:7252`, FP) -> `tls_prf_generic` (`:6893`, direct `mbedtls_md_hmac_*`).
- Secret through the glue: the 48-byte premaster (or the fake one) and the raw
  RSA output; the PKCS#1 validity bit is exactly what must not leak.
- Region: unpadding ~1.5-2 us + pms glue ~0.5-1 us + PRF ~2-3 us (10-20 SHA-256
  compressions) if the pass can merge across the `tls_prf` pointer; ~2-3 us if not.
- Function-pointer hazards: `pk_info->decrypt_func` (between SSL glue and RSA),
  `conf->f_rng`, `handshake->tls_prf`. Bridging seeds:
  `mbedtls_rsa_rsaes_pkcs1_v15_decrypt,0,pointee` (private key) covers the RSA
  side; `ssl_decrypt_encrypted_pms,3,pointee` (`peer_pms`, an output buffer tainted
  from entry) covers the SSL side; `ssl_compute_master,0,pointee` covers the PRF.
- Public lane: transcript hashing of ClientHello/Certificate/ServerKeyExchange
  (`mbedtls_ssl_update_handshake_status` -> `mbedtls_md` over public bytes),
  optional client-certificate chain verification (`mbedtls_x509_crt_verify`, public,
  size knob = chain length), ClientHello extension parsing. Knob: RSA key size
  (region grows ~linearly in `ilen` for the scan; memmove fixed at 48), number of
  handshakes, client cert chain depth.
- Harness: yes. Force `MBEDTLS_SSL_VERSION_TLS1_2` max version and ciphersuite
  `TLS-RSA-WITH-AES-128-GCM-SHA256` (or CBC to combine with 1a). One process.
- Asm footnote: `mbedtls_rsa_private` uses `bn_mul.h` inline asm unless
  `MBEDTLS_HAVE_ASM` is undefined; the glue itself is pure C.

### 1d. Session tickets: `library/ssl_ticket.c` -> `mbedtls_ssl_session_load`

https://github.com/Mbed-TLS/mbedtls/blob/d6664ceac9a126267154729dfbbb9ce4a6d8cfa9/library/ssl_ticket.c#L418
https://github.com/Mbed-TLS/mbedtls/blob/d6664ceac9a126267154729dfbbb9ce4a6d8cfa9/library/ssl_tls.c#L3617
https://github.com/Mbed-TLS/mbedtls/blob/d6664ceac9a126267154729dfbbb9ce4a6d8cfa9/library/ssl_tls.c#L3978

- `mbedtls_ssl_ticket_parse(p_ticket, session, buf, len)` (`ssl_ticket.c:418`):
  layout `key_name[4] || iv[12] || enc_len[2] || ciphertext || tag[16]`;
  `ssl_ticket_update_keys` (public), `ssl_ticket_select_key` (public 4-byte name),
  then `psa_aead_decrypt(key->key, key->alg, iv, 12, key_name, 18, ticket, enc_len+16, ticket, enc_len, &clear_len)`
  (`:471`, PSA build) or `mbedtls_cipher_auth_decrypt_ext(&key->ctx, ...)` (`:479`,
  default build: `cipher.c:1511 mbedtls_cipher_aead_decrypt` switch -> direct
  `mbedtls_gcm_auth_decrypt`, whose AES-ECB goes through `mbedtls_cipher_update`
  fp). In-place decrypt. Then `mbedtls_ssl_session_load(session, ticket, clear_len)`
  (`:500`), then lifetime check (public).
- `mbedtls_ssl_session_load` (`ssl_tls.c:4539`) -> `ssl_session_load` (`:4477`):
  header `memcmp` with `ssl_serialized_session_header` (bytes are a public
  constant, but the comparison input is decrypted data), reads
  `tls_version`, `endpoint`, `ciphersuite`, then `switch (session->tls_version)`:
  - `ssl_tls12_session_load` (`:3617`): `start` u64, `id_len` u8 (bounds branch),
    `memcpy(session->id, p, 32)` (`:3659`), `memcpy(session->master, p, 48)`
    (`:3662`, THE secret), `verify_result` u32, then optional peer certificate
    (`mbedtls_x509_crt_parse_der`, `:3709`, public but large and branchy) or cert
    digest, client-side ticket copy, server-side `ticket_creation_time`, mfl,
    `encrypt_then_mac`. Branchy on lengths (format metadata), straight-line on
    the secret bytes themselves. ~200-500 cycles excluding any cert parse.
  - `ssl_tls13_session_load` (`:3978`): `ticket_age_add` u32, `ticket_flags` u8,
    `resumption_key_len` u8, `memcpy(session->resumption_key, p, resumption_key_len)`
    (`:4002`, THE secret, 32 or 48 bytes), then early-data size, record size limit,
    server: `ticket_creation_time`, ALPN string (`mbedtls_ssl_session_set_ticket_alpn`,
    calloc + strcpy); client: hostname, times, ticket blob. ~200-400 cycles.
- Consumers of the secret:
  - TLS 1.2: `ssl_parse_session_ticket_ext` (`ssl_tls12_server.c:470`) calls
    `ssl->conf->f_ticket_parse(...)` (`:503`, FP), then
    `memcpy(ssl->session_negotiate, &session, sizeof(mbedtls_ssl_session))` (`:527`,
    the whole struct including `master`), later `mbedtls_ssl_derive_keys` ->
    `ssl_tls12_populate_transform` -> `handshake->tls_prf(session->master, 48, ...)`
    (`ssl_tls.c:8273`, FP) key expansion.
  - TLS 1.3: `ssl_tls13_offered_psks_check_identity_match_ticket`
    (`ssl_tls13_server.c:~180-262`): `memcpy(ticket_buffer, identity, identity_len)`,
    `ssl->conf->f_ticket_parse(...)` (`:219`, FP), version/lifetime/age checks
    (public metadata), then in `ssl_tls13_offered_psks_check_identity_match`
    (`:330`) `mbedtls_ssl_set_hs_psk(ssl, session->resumption_key, session->resumption_key_len)`
    (`:350-353`), then binder: `mbedtls_ssl_tls13_create_psk_binder` (ssl_tls13_keys.c,
    PSA HKDF via `psa_key_derivation_*`, direct through driver-wrapper switches)
    and `mbedtls_ct_memcmp(server_computed_binder, binder, hash_len)`
    (`ssl_tls13_server.c:451`). This chain is decrypt -> deserialize -> copy -> KDF
    -> compare, all C. Region: session_load ~0.1 us + PSA key import/derive
    (~12-20 SHA-256 compressions plus PSA slot management) ~2-4 us. Borderline
    against the 2.5 us crossover; multiple offered PSK identities per ClientHello
    are each decrypted and parsed (loop in `ssl_tls13_parse_pre_shared_key_ext`),
    a natural region-count knob.
- Secret bytes: `master[48]` (TLS 1.2) / `resumption_key[32|48]` (TLS 1.3) plus,
  strictly, every decrypted byte (the pass will taint the whole plaintext buffer,
  including the public metadata; expect over-approximation into the length
  branches).
- Function-pointer hazards: `f_ticket_parse` (entry), `tls_prf` (TLS 1.2 exit),
  `mbedtls_cipher_*` inside the AEAD in the default build. Seeds:
  `mbedtls_ssl_ticket_parse,2,pointee` (buf) covers decrypt + parse;
  `mbedtls_ssl_set_hs_psk,1,pointee` covers the TLS 1.3 binder stage;
  `ssl_tls12_populate_transform` / `ssl_compute_master,0,pointee` covers 1.2.
- Public lane: ClientHello parsing, ticket key rotation, `ssl_ticket_select_key`,
  transcript hashing; knob: number of PSK identities offered (TLS 1.3), ticket
  payload size (ALPN/hostname/peer cert), resumption rate.
- Harness: yes. Server: `mbedtls_ssl_ticket_setup(&tctx, rng, p_rng, MBEDTLS_CIPHER_AES_256_GCM, lifetime)`
  + `mbedtls_ssl_conf_session_tickets_cb(conf, mbedtls_ssl_ticket_write, mbedtls_ssl_ticket_parse, &tctx)`;
  client: `mbedtls_ssl_conf_session_tickets(conf, MBEDTLS_SSL_SESSION_TICKETS_ENABLED)`,
  after handshake 1 `mbedtls_ssl_get_session`, then `mbedtls_ssl_set_session` on a
  fresh client context; handshake 2 exercises the parse (both 1.2 and 1.3;
  `MBEDTLS_SSL_SESSION_TICKETS` and `MBEDTLS_SSL_TICKET_C` are on by default,
  `mbedtls_config.h:2085,3693`). This is exactly what `programs/ssl/ssl_server2`
  does, minus sockets.

### 1e. Encrypted PKCS#8 keys: `library/pkparse.c`

https://github.com/Mbed-TLS/mbedtls/blob/d6664ceac9a126267154729dfbbb9ce4a6d8cfa9/library/pkparse.c#L884

- `mbedtls_pk_parse_key(pk, key, keylen, pwd, pwdlen, f_rng, p_rng)` (`:973`):
  PEM branches. For "ENCRYPTED PRIVATE KEY" PEM: `mbedtls_pem_read_buffer`
  (`pem.c`, `mbedtls_base64_decode` at `:412/426` on ciphertext, i.e. public), then
  `pk_parse_key_pkcs8_encrypted_der(pk, pem.buf, ...)`. For legacy
  "RSA PRIVATE KEY" with `DEK-Info`: `pem.c:442-454 pem_des3_decrypt / pem_des_decrypt / pem_aes_decrypt`
  (direct `mbedtls_aes_crypt_cbc`, key from `pem_pbkdf1` = MD5, direct) then
  `pem_check_pkcs_padding` (`:244`, plain branchy pad check) then the DER parse.
- `pk_parse_key_pkcs8_encrypted_der` (`:~884-968`): `mbedtls_asn1_get_tag` x2,
  `mbedtls_asn1_get_alg` (all on public ciphertext structure), then
  `mbedtls_pkcs12_pbe_ext(&pbe_params, MBEDTLS_PKCS12_PBE_DECRYPT, cipher_alg, md_alg, pwd, pwdlen, p, len, buf, len, &outlen)`
  (`:926`) or `mbedtls_pkcs5_pbes2_ext(&pbe_params, MBEDTLS_PKCS5_DECRYPT, pwd, pwdlen, p, len, buf, len, &outlen)`
  (`:941`), in-place, then `pk_parse_key_pkcs8_unencrypted_der(pk, buf, outlen, ...)`
  (`:968`).
- `pk_parse_key_pkcs8_unencrypted_der` (`:742`): `mbedtls_asn1_get_tag` (SEQUENCE),
  `mbedtls_asn1_get_int` (version), `pk_get_pk_alg` (OID), `mbedtls_asn1_get_tag`
  (OCTET STRING), `mbedtls_pk_setup`, then `mbedtls_rsa_parse_key(mbedtls_pk_rsa(*pk), p, len)`
  (`rsa.c:79`: `mbedtls_asn1_get_mpi` x8 -> `mbedtls_mpi_read_binary`, then
  `mbedtls_rsa_complete` `:723` which may run `mbedtls_rsa_deduce_*` bignum) or
  `pk_parse_key_sec1_der` (`:604`: `mbedtls_asn1_get_tag`, private scalar
  `mbedtls_pk_ecc_set_key(pk, d, d_len)`, optional public point
  `mbedtls_pk_ecc_set_pubkey`, else `mbedtls_pk_ecc_set_pubkey_from_prv` which is a
  scalar multiplication) or `pk_parse_key_rfc8410_der` (`:429`).
- The DER parsers branch on the decrypted bytes (tag/length checks in
  `mbedtls_asn1_get_tag`/`get_len`): NOT constant-time, and the secret bytes then
  feed bignum imports and, for RSA, `mbedtls_rsa_complete` (large, inline asm).
  Region dominated by bignum (~10^5-10^6 cycles), so the value-predictor angle is
  weak and the "already constant-time" premise is violated.
- Direct? `mbedtls_pk_parse_key` -> `pk_parse_key_pkcs8_encrypted_der` ->
  `mbedtls_pkcs5_pbes2_ext` -> `mbedtls_cipher_crypt` (FP) and
  `ctx->get_padding` (FP) -> back -> `pk_parse_key_pkcs8_unencrypted_der` (direct).
  Harness: trivial single call.
- Better mini-candidate in the same file: unencrypted PEM private keys. The base64
  decode (`base64.c:128 mbedtls_base64_decode`, digits via CT
  `mbedtls_ct_base64_dec_value` `:41`) processes the secret key material itself,
  straight-line except a whitespace/`=` scan whose branches depend on ASCII
  structure, not the 6-bit values. For a 2048-bit RSA PEM (~1.6 KB) that is
  ~1600 x ~30 ops, ~15-20k cycles, ~5 us, direct calls, seed
  `mbedtls_pk_parse_key,1,pointee`. It is decode-then-parse rather than
  decrypt-then-parse; the following DER parse is branchy as above.

### 1f. `library/pkcs12.c`, `library/pkcs5.c`

- `mbedtls_pkcs5_pbes2_ext` (`pkcs5.c:129`): parse PBES2 params (public),
  `mbedtls_pkcs5_pbkdf2_hmac_ext` (`:91` of the body; direct `mbedtls_md_hmac_*`,
  password-keyed, iteration count public), `mbedtls_cipher_setup/setkey`,
  `mbedtls_cipher_set_padding_mode(PKCS7)`, `mbedtls_cipher_crypt(&cipher_ctx, iv, ivlen, data, datalen, output, output_len)`
  (`:256`, FP `cbc_func`, then `mbedtls_cipher_finish_padded` -> FP `get_padding`
  -> CT `get_pkcs_padding` `cipher.c:848`; result folded with
  `mbedtls_ct_error_if_else_0` at `cipher.c:1141`). Output length is secret-derived
  (from the pad byte) but computed CT; it then drives the DER parser.
- `mbedtls_pkcs12_pbe_ext` (`pkcs12.c:150`): `pkcs12_pbe_derive_key_iv` (`:76`,
  `mbedtls_pkcs12_derivation` `:326`, direct MD), same cipher path,
  `mbedtls_cipher_crypt` at `:228`.
- Only caller of both in the library: `pkparse.c` (and `pkcs12.c` self-tests).
  Verdict as 1e.

### 1g. mbedTLS candidates compared

| candidate | secret glue | CT? | direct from primitive caller? | region / event | public lane + knob | harness |
|---|---|---|---|---|---|---|
| TLS 1.2 CBC MtE record (`mbedtls_ssl_decrypt_buf`) | pad scan, `ct_hmac`, `memcpy_offset`, `memcmp` | yes | yes (cipher decrypt itself behind `cbc_func` fp, but the glue is in the caller) | 150-300k cyc (~50-100 us) for >=256 B records | record header/demux; record size, count | yes, ETM off on one side |
| TLS 1.2 CBC EtM record | HMAC over ciphertext, `ct_memcmp`, CT pad scan | yes | yes | ~2-3k cyc (~1 us) | same | yes (default) |
| TLS 1.3 AEAD record | `ssl_parse_inner_plaintext` strip loop + content-type dispatch | no (loop on secret, switch on secret type) | yes | ~10-60 cyc | same | yes |
| TLS 1.2 RSA premaster | `ct_rsaes_pkcs1_v15_unpadding`, version check, fake-PMS select, PRF | yes | fp: `pk_info->decrypt_func`, `f_rng`, `tls_prf` | ~5-8k cyc (~2-3 us) glue, +PRF ~2-3 us | transcript hash, cert verify; key size, cert chain | yes, RSA suite, TLS 1.2 |
| TLS 1.2 ticket parse | `ssl_tls12_session_load`, struct copy, `tls_prf` | copies straight-line; length branches | fp: `f_ticket_parse` entry, `tls_prf` exit | ~0.3 us parse | ClientHello parsing; ticket size | yes |
| TLS 1.3 ticket -> binder | `ssl_tls13_session_load`, `set_hs_psk`, HKDF, `ct_memcmp` | copies straight-line; PSA KDF direct | fp: `f_ticket_parse` entry only | ~3-5 us | ClientHello parsing; PSK identities per hello | yes |
| encrypted PKCS#8 load | PKCS7 unpad (CT), DER parse (branchy), `mpi_read_binary`, `rsa_complete` | no | fp: `cipher_crypt`, `get_padding` | ~10^5-10^6 cyc, bignum-dominated | PEM scan, base64 of ciphertext; PBKDF2 iterations (password-keyed, not public) | trivial |
| unencrypted PEM key (bonus) | CT base64 decode then DER parse | decode yes, parse no | yes | ~5 us decode | PEM header scan | trivial |

## 2. OpenSSL 3.5 (branch `openssl-3.5`)

- `ssl/record/methods/tls_pad.c`: `ssl3_cbc_remove_padding_and_mac` (53),
  `tls1_cbc_remove_padding_and_mac` (98), `ssl3_cbc_copy_mac` (182). The TLS one:
  `good = constant_time_ge_s(*reclen, overhead + padding_length)`, 256-iteration
  `constant_time_ge_8_s` mask loop, `good = constant_time_eq_s(0xff, good & 0xff)`,
  `*reclen -= good & (padding_length + 1)`, then `ssl3_cbc_copy_mac` (rotating
  64-byte-aligned scan, `constant_time_select_8` with random bytes on failure).
  CT, plain C. Caller: `tls1_cipher` (`tls1_meth.c:173`, call at `:460`), which is
  installed in a method table and invoked as `rl->funcs->cipher(...)`
  (`tls_common.c:827`) -> FP. The cipher itself is `EVP_Cipher`/`EVP_CipherUpdate`
  (provider fp, aarch64 asm `aesv8-armx`). The MAC after it:
  `rl->funcs->mac` (fp) -> `tls1_mac` -> `ssl3_cbc_digest_record`
  (`ssl3_cbc.c:126`) which picks `md_transform` as a FUNCTION POINTER to
  `SHA256_Transform` etc. (`:170-212`; on aarch64 that is `sha256_block_data_order`
  asm), then `CRYPTO_memcmp` (`tls_common.c:805/897`). Verdict: glue is C and CT
  but sits between two fp hops and asm primitives.
  https://github.com/openssl/openssl/blob/openssl-3.5/ssl/record/methods/tls_pad.c
- TLS 1.3 strip: `ssl/record/methods/tls13_meth.c:293-298`
  `for (end = rec->length - 1; end > 0 && rec->data[end] == 0; end--) continue; rec->length = end; rec->type = rec->data[end];`
  Branchy, tiny, after `EVP_CipherFinal_ex` (fp).
- `crypto/rsa/rsa_pk1.c`: `ossl_rsa_padding_check_PKCS1_type_2` (387) implements
  implicit rejection: `ossl_rsa_prf` synthetic message (`:277`), candidate-length
  selection with `constant_time_select_int` (`:454-465`), CT scan (`:468-510`),
  `constant_time_select_8` copy. `ossl_rsa_padding_check_PKCS1_type_2_TLS` (546):
  CT version check `constant_time_eq(from[flen - 48], client_version >> 8)`
  (`:592-594`), `constant_time_select_8` copy (`:624`). Both CT C; reached via the
  provider RSA decrypt (fp) after `BN_mod_exp_mont_consttime` (armv8-mont asm).
  https://github.com/openssl/openssl/blob/openssl-3.5/crypto/rsa/rsa_pk1.c
- Tickets: `ssl/t1_lib.c tls_decrypt_ticket` (3031): keyname `memcmp` (`:3122`),
  `EVP_DecryptInit_ex(aes256cbc)` (`:3136`), HMAC via `ssl_hmac_update/final`
  (EVP_MAC fp), `CRYPTO_memcmp(tick_hmac, etick + eticklen, mlen)` (`:3177`),
  `EVP_DecryptUpdate/Final` (`:3186/3191`), `d2i_SSL_SESSION_ex` (`:3199`) ->
  `ssl/ssl_asn1.c:262` generic ASN.1 template decode -> `ssl_session_memcpy(ret->master_key, ...)`
  (`:318`). Parse is the generic `ASN1_item_d2i` machinery: branchy, fp-heavy.
- `crypto/evp/encode.c EVP_DecodeUpdate` (293): `conv_ascii2bin` is a table lookup
  `data_ascii2bin[128]` (`:69,102-115`) indexed by the input byte: NOT CT (secret
  index). `crypto/pem/pem_lib.c PEM_do_header` (443): `EVP_BytesToKey(MD5)`,
  `EVP_DecryptUpdate/Final_ex` (fp), then caller does `d2i_*`. `crypto/pkcs12/p12_decr.c
  PKCS12_pbe_crypt_ex` (19) `EVP_CipherUpdate/Final_ex` then
  `PKCS12_item_decrypt_d2i_ex` (139) `ASN1_item_d2i` (`:166`).
- Asm reach on aarch64 (`crypto/*/asm` listing, branch openssl-3.5): aes
  (`aesv8-armx.pl`, `bsaes-armv8.pl`, `vpaes-armv8.pl`), bn (`armv8-mont.pl`), chacha
  (`chacha-armv8.pl`, `-sve`), ec (`ecp_nistz256-armv8.pl`, `ecp_sm2p256-armv8.pl`),
  md5 (`md5-aarch64.pl`), modes (`aes-gcm-armv8_64.pl`, `-unroll8_64`, `ghashv8-armx.pl`),
  poly1305 (`poly1305-armv8.pl`), sha (`keccak1600-armv8.pl`, `sha1-armv8.pl`,
  `sha512-armv8.pl` which also emits sha256). `no-asm` removes them but every EVP
  hop stays a function pointer. Verdict: the GLUE is plain C (tls_pad.c, rsa_pk1.c,
  t1_lib.c) but unreachable by direct-call placement.

## 3. BoringSSL (branch `main`)

- `crypto/base64/base64.cc`: `conv_bin2ascii` (46) and `base64_ascii_to_bin` (220)
  carry the comment "Since PEM is sometimes used to carry private keys, we
  decode base64 data itself in constant-time." using `constant_time_in_range_8`,
  `constant_time_eq_8`, `constant_time_select_8`; `base64_decode_quad` (248),
  `EVP_DecodeUpdate` (297), `EVP_DecodeBase64` (362). Origin: commit
  536036abf46a13e52a43a92f6e44a87404e8755f "Implement base64 in constant-time."
  (David Benjamin, 2017-04-14); follow-up c49c9e7e61f5 "Optimize constant-time
  base64 implementation slightly." No data-indexed tables remain in the decoder.
  https://github.com/google/boringssl/blob/main/crypto/base64/base64.cc#L220
  https://github.com/google/boringssl/commit/536036abf46a13e52a43a92f6e44a87404e8755f
- `ssl/tls_record.cc tls_open_record`: `aead_read_ctx->Open(...)` (`:182`, C++
  method -> `EVP_AEAD_CTX_open`, fp `EVP_AEAD->open`), then TLS 1.3 strip
  `do { ... type = out->back(); *out = out->subspan(0, out->size() - 1); } while (type == 0);`
  (`:212-228`). Branchy, tiny.
- Tickets are in `ssl/extensions.cc`, not `ssl_session.cc`:
  `decrypt_ticket_with_cipher_ctx` (~4816): `HMAC_Update/Final`,
  `CRYPTO_memcmp(mac, ticket_mac.data(), mac_len)` (`:4839`),
  `EVP_DecryptUpdate_ex` / `EVP_DecryptFinal_ex2` (`:4862-4865`, fp);
  `ssl_decrypt_ticket_with_ticket_keys` (4905), `ssl_decrypt_ticket_with_method`
  (4944, `ticket_aead_method->open` fp), `ssl_process_ticket` (4966) ->
  `SSL_SESSION_parse` (`ssl/ssl_asn1.cc`, CBS-based hand-written DER parse,
  branchy). `ssl/ssl_session.cc` holds only the encrypt side (`:360-440`).
- RSA: `crypto/fipsmodule/rsa/padding.cc.inc` holds type_1 / PSS / MGF1 only.
  `rsa_padding_check_PKCS1_type_2` is `crypto/rsa/rsa_crypt.cc:246` (CT:
  `constant_time_eq_w`, `constant_time_select_w`, `constant_time_ge_w`);
  `RSA_padding_check_PKCS1_OAEP_mgf1` `:117` (CT scan, `CRYPTO_memcmp` of hash,
  `constant_time_declassify_w(bad)` then branch). Called from
  `rsa_default_decrypt` (`:429-467`) via `RSA_METHOD` (fp).
- Constraints: `ssl/` is C++ (fine for clang), `crypto/` is C with perlasm for
  aarch64 (`OPENSSL_NO_ASM` build flag switches it off), fp dispatch for every
  EVP/AEAD/RSA primitive. Harness: BIO pairs in one process are the standard
  `ssl_test.cc` pattern. Verdict: best non-mbedTLS backup, mainly for the CT
  base64 decode-then-parse of PEM private keys (`PEM_read_bio_PrivateKey` ->
  `EVP_DecodeUpdate` -> `d2i_AutoPrivateKey`).

## 4. wolfSSL (branch `master`)

- `src/internal.c`: `TimingPadVerify` (23532): `good = MaskPadding(input, pLen, macSz)`
  (static, 23435: 256-iteration `ctMaskLTE` loop), `padLen &= ctMaskIntGTE(...)`,
  `ssl->hmac(...)` (FP to `TLS_hmac`/`Hmac_UpdateFinal_CT`), `good |= MaskMac(...)`
  (static, 23463: rotating scan with `ctMaskGTE`/`ctMaskLT`, CT compare loop),
  mask-fold to a single `ret`. CT C. `PadCheck` (23319/23414) is the older
  XOR-accumulate helper. `ConstantCompare` lives in `wolfcrypt/src/misc.c:824`
  (`WC_MISC_STATIC WC_INLINE int ConstantCompare`), used in `internal.c:6400-6430`
  (Finished) and `wolfcrypt/src/rsa.c:1867` (OAEP).
- `wolfcrypt/src/rsa.c`: `RsaUnPad` (2039, PKCS#1 v1.5 with `ctMask16Eq`,
  `ctMaskLT`, `ctMaskNotEq`, `:2086-2104`), `RsaUnPad_OAEP` (1771,
  `ConstantCompare` + `ctMaskSelWord32`), `RsaUnPad_ex` dispatcher; called from
  `wc_RsaFunction`-based decrypt directly (no fp inside wolfCrypt).
- Tickets: `DoDecryptTicket` (`internal.c:42879`): `ExternalTicket` layout,
  `ssl->ctx->ticketEncCb(...)` (`:42928`, FP, default `DefTicketEncCb` `:44157`
  using `wc_AesGcmDecrypt`/ChaCha20-Poly1305 directly), `*it = (InternalTicket*)et->enc_ticket`;
  then `DoClientTicketCheck` (timestamp/suite checks, `XMEMCMP(suite, ...)`) and
  `DoClientTicketFinalize` (`:43151`): `XMEMCPY(ssl->arrays->masterSecret, it->msecret, SECRET_LEN)`
  (`:43199`), `ato32(it->timestamp, ...)`, suite bytes; TLS 1.3 `DoClientTicket`
  (`:43480`). Struct-cast parse: straight-line copies, a few public branches.
- Asm: `configure.ac:1618-1621` auto-sets `enable_armasm=yes` on `*aarch64*` hosts
  when not given; `--disable-armasm` (or `--disable-asm`) yields pure C.
  https://github.com/wolfSSL/wolfssl/blob/master/configure.ac
- Harness: custom I/O callbacks (`wolfSSL_SetIORecv/Send`) over memory buffers are
  standard; single process. Verdict: viable backup #2; hazards are `ssl->hmac`
  and `ticketEncCb` function pointers.

## 5. libsodium (branch `master`)

- `src/libsodium/sodium/codecs.c`: `sodium_hex2bin` (branch-free per-character
  arithmetic `c_num0`, `c_alpha0`, but `if ((c_num0 | c_alpha0) == 0U) break;` and
  buffer-size branches; loop trip count = input length), `sodium_base642bin` uses
  `b64_char_to_byte` built from the CT macros documented at `:105` ("Some macros
  for constant-time comparisons"). Direct calls, C, no asm. Regions are tiny
  (32-64 byte keys, ~100-300 cycles).
- Post-decrypt parsing: none. `crypto_aead_chacha20poly1305_decrypt_detached`
  (`aead_chacha20poly1305.c:195`) ends with `crypto_verify_16(computed_mac, mac)`
  (`:230`) inside the primitive; `crypto_secretbox_open_easy` likewise. The library
  returns raw plaintext and never parses it. Verdict: no decrypt-then-parse glue;
  only decode-then-use of keys, too small to be a region.

## 6. OpenSSH (openssh-portable `master`)

- `packet.c ssh_packet_read_poll2` (1622): non-ETM/non-AEAD path decrypts one
  block (`cipher_crypt(..., block_size, 0, 0)`), `state->packlen = PEEK_U32(...)`
  (`:1684`), range branch on the secret-derived length, computes `need`, decrypts
  the rest, `mac_check` (`mac.c`, `timingsafe_bcmp`), then `padlen` from the
  first plaintext byte and `sshbuf_consume`. ETM/AEAD path: `cipher_get_length`
  (ChaCha20 header key) gives the length before decryption. `cipher.c` uses
  libcrypto EVP (fp, asm) except built-in `chachapoly`/`aesctr`. Branchy on
  secret length; needs the whole `struct ssh` state; sockets in real use.
- `sshkey.c`: `sshkey_parse_private2` (3163) -> `private2_uudecode` (2941,
  base64 of ciphertext) -> `private2_decrypt` (3016): `bcrypt_pbkdf`,
  `cipher_init` + `cipher_crypt` (`:3115-3117`, AEAD or CTR), then
  `sshbuf_get_u32(check1)`, `sshbuf_get_u32(check2)`, `if (check1 != check2)`
  (`:3131-3135`, branch on decrypted bytes) -> `sshkey_private_deserialize`
  (2602: `sshbuf_get_cstring` type, `sshbuf_get_bignum2`/`get_string`, BN/EVP
  imports, branchy) -> `private2_check_padding` (1739: `if (pad != (++i & 0xff))`
  per byte). Not CT anywhere; libcrypto fp + asm. Harness: key parse is single
  process. Verdict: poor.

## 7. Tor (`main`)

- `src/core/or/relay.c circuit_receive_relay_cell` (236) ->
  `src/core/crypto/relay_crypto.c relay_decrypt_cell` (152) -> `switch (crypto->kind)`
  -> `relay_crypto_tor1.c tor1_crypt_relay_forward` (161):
  `tor1_crypt_one_payload` (AES-CTR via `crypto_cipher_crypt_inplace`, OpenSSL EVP
  fp + asm) then `relay_cell_is_recognized_v0` (`:91`, `get_uint16(payload+off) == 0`,
  branch on 2 decrypted bytes) then `tor1_relay_digest_matches_v0` (`:49`): SHA-1
  over the 509-byte payload with the 4 digest bytes zeroed, then
  `if (calculated_integrity != received_integrity)` (`:70`, plain compare, no
  `tor_memeq`) and digest checkpoint/restore. Then `relay_msg.c
  relay_msg_decode_cell_in_place` (254) -> `decode_v0_cell` (166): command,
  stream_id, length reads with bounds branches -> `handle_relay_msg`. CGO
  (`relay_crypto_cgo.c`) replaces this with polyval/AES tweakable encryption.
  Branchy glue, OpenSSL dependence, event-loop state. `test_relaycrypt.c` shows
  the crypto layer can run standalone. Verdict: poor.

## 8. MIT Kerberos (`master`)

- `lib/krb5/krb/decrypt_tk.c krb5_decrypt_tkt_part` (13): `krb5_c_decrypt`
  (`:31`) -> enctype table `krb5_keytypes[].decrypt` (FP) ->
  `lib/crypto/krb/enc_dk_hmac.c krb5int_dk_decrypt` (`krb5int_hmac`, then
  `k5_bcmp(cksum, trailer->data.data, hmacsize)` `:81`; `k5_bcmp` is the CT
  XOR-accumulate in `util/support/bcmp.c`) or `enc_etm.c krb5int_etm_decrypt`
  (210, `k5_bcmp` `:244`); hash/cipher providers are fp tables (builtin or
  OpenSSL). Then `decode_krb5_enc_tkt_part(&scratch, ...)` (`:39`) -> generic
  ASN.1 decoder `lib/krb5/asn.1/asn1_encode.c` (`k5_asn1_decode_int` 186,
  `k5_asn1_decode_bytestring` 223, type-driven `decode_atype`): branchy on every
  decrypted byte; the session key is an OCTET STRING inside.
- Client: `get_in_tkt.c decrypt_as_reply` (48) -> `kdc_rep_dc.c
  krb5_kdc_rep_decrypt_proc` (37): `krb5_c_decrypt` then
  `decode_krb5_enc_kdc_rep_part` (`:69`), session key copied out. Same shape.
- Single process feasible (library calls). Verdict: semantically ideal, but parse
  is a generic branchy DER decoder and crypto is fp-dispatched.

## 9. Apache httpd `mod_session_crypto` (`trunk`)

- `modules/session/mod_session_crypto.c decrypt_string` (344): `apr_base64_decode`
  (`:362`, table lookup), SipHash verification of salt+IV+ciphertext (`:386`),
  `apr_crypto_passphrase` (`:397`, PBKDF2 via APR driver fp),
  `apr_crypto_block_decrypt_init/decrypt/finish` (`:434-456`, driver fp ->
  OpenSSL/NSS/commoncrypto) -> `session_crypto_decode` -> `mod_session.c
  session_identity_decode` (~290-320): `apr_strtok`/`strchr`/`ap_unescape_urlencoded`
  string parsing, `apr_table_set`. Branchy, needs `request_rec`/pools. Other C
  servers checked by reasoning: nginx has no built-in encrypted-cookie decrypt.
  Verdict: poor.

## 10. Others

- WireGuard (Linux `drivers/net/wireguard/noise.c`):
  `wg_noise_handshake_consume_initiation`: `message_decrypt(s, src->encrypted_static, ...)`
  (`chacha20poly1305_decrypt`, kernel lib/crypto with arm64 asm) ->
  `wg_pubkey_hashtable_lookup(wg->peer_hashtable, s)` (SipHash + hash table
  indexed by the DECRYPTED static key, i.e. secret-indexed memory) ->
  `message_decrypt(t, src->encrypted_timestamp, ...)` ->
  `memcmp(t, handshake->latest_timestamp, 12)` (plain, `:50,62`) ->
  `memcpy` of remote ephemeral/hash/chaining key. `receive.c
  wg_packet_consume_data_done` (335): after `decrypt_packet` (242),
  `ip_hdr(skb)->version` switch and `tot_len` parse. Kernel-only, skb-bound: not
  embeddable in the gem5 SE harness. Verdict: out.
- strongSwan `src/libcharon/encoding/payloads/encrypted_payload.c`:
  `decrypt_content` (619): `aead->decrypt(aead, crypt, assoc, iv, NULL)` (plugin
  vtable fp) -> `padding.len = plain->ptr[plain->len - 1] + 1;` and
  `if (padding.len > plain->len)` (`:656-663`, branch on decrypted byte) ->
  `parse` (575): generic `parser->parse_payload` per IKEv2 payload with
  `untoh16` length checks (branchy). Daemon architecture, plugin fps. Verdict: out.
- OpenVPN `src/openvpn/crypto.c`: `openvpn_decrypt_v1` (616): HMAC verify with
  `memcmp_constant_time` (`:652`), `cipher_ctx_update/final` (backend wrapper,
  OpenSSL EVP fp or mbedTLS `mbedtls_cipher_*` fp), then `packet_id_read(&pin, &work, long_form)`
  (`packet_id.c`: 4-8 byte `buf_read` + `ntohl`, `:719-752`) and
  `crypto_check_replay` -> `packet_id_test` (sliding-window branches). AEAD path
  (435) reads the packet-id from the AAD before decryption (public). Glue is a
  few bytes; harness feasible via `openvpn_encrypt/decrypt` unit tests with the
  mbedTLS backend, but region ~50-100 cycles. Verdict: poor on size.
- GnuPG `g10/pubkey-enc.c get_it` (234): the RSA/ECDH decrypt happens in
  gpg-agent (`agent_pkdecrypt`, `:315`, IPC over Assuan socket), so the
  single-process constraint fails outright. The frame parse (`:355-430`: pad byte
  `frame[nframe-1]`, `for (frameidx++; frameidx < nframe && frame[frameidx]; frameidx++)`
  random-pad skip, `memcpy(dek->key, ...)`, 16-bit checksum loop and `csum != csum2`
  branch) is classic branchy PKESK glue. libgcrypt's own `gcry_pk_decrypt` returns
  an S-expression MPI, no session-key parse. Verdict: out.

## 11. Cross-cutting table

| library | glue function(s) | file | secret through glue | CT? | direct? | region | public lane + knob | harness (1 process, no asm) | verdict |
|---|---|---|---|---|---|---|---|---|---|
| mbedTLS | `mbedtls_ssl_decrypt_buf` CBC MtE: pad scan, `mbedtls_ct_hmac`, `mbedtls_ct_memcpy_offset`, `mbedtls_ct_memcmp` | library/ssl_msg.c 1482-2090 | record plaintext, pad, MAC | yes | yes | 150-300k cyc / record | record header parse; record size, count | yes (ETM off) | rank 2 |
| mbedTLS | `mbedtls_ct_rsaes_pkcs1_v15_unpadding`, `ssl_parse_encrypted_pms`, `ssl_compute_master` | library/rsa.c 420, ssl_tls12_server.c 3399, ssl_tls.c 7075 | RSA output, 48-byte PMS | yes | fp x3 (`decrypt_func`, `f_rng`, `tls_prf`), bridgeable by seeds | ~5-8k cyc glue, +PRF | transcript hash, cert verify; RSA size, chain depth | yes (TLS 1.2 RSA suite) | rank 1 |
| mbedTLS | `mbedtls_ssl_ticket_parse` -> `ssl_tls13_session_load` -> `mbedtls_ssl_set_hs_psk` -> binder -> `mbedtls_ct_memcmp` | ssl_ticket.c 418, ssl_tls.c 3978, ssl_tls13_server.c 219-451 | resumption_key (32/48 B), whole ticket plaintext | copies yes; length branches | fp at entry (`f_ticket_parse`) | ~3-5 us | ClientHello parse; PSK identities per hello | yes | rank 3 |
| mbedTLS | `ssl_tls12_session_load` -> `tls_prf` | ssl_tls.c 3617, 8273 | master[48] | copies yes | fp entry+exit | ~0.3 us parse | same | yes | small |
| mbedTLS | `ssl_parse_inner_plaintext` (TLS 1.3 strip) | ssl_msg.c 597 | content type, pad | no | yes | 10-60 cyc | record parse | yes | too small, taints dispatcher |
| mbedTLS | PKCS#8 encrypted key: `get_pkcs_padding`, DER parse, `mbedtls_rsa_parse_key`, `mbedtls_rsa_complete` | pkparse.c 884/742, cipher.c 848, rsa.c 79/723 | private key DER | unpad yes, parse no | fp (`cipher_crypt`, `get_padding`) | bignum-dominated | PEM scan | yes | semantic fit only |
| mbedTLS | PEM base64 CT decode of unencrypted key | base64.c 41/128, pem.c 412 | key DER | decode yes | yes | ~5 us | PEM scan | yes | bonus |
| OpenSSL 3.5 | `tls1_cbc_remove_padding_and_mac`, `ssl3_cbc_copy_mac`, `ssl3_cbc_digest_record` | ssl/record/methods/tls_pad.c, ssl3_cbc.c | record plaintext | yes | no (`rl->funcs->*`, EVP, `md_transform` fp) | ~100k cyc | record layer | no (asm on every primitive; `no-asm` possible) | glue is C but unreachable |
| OpenSSL 3.5 | `ossl_rsa_padding_check_PKCS1_type_2[_TLS]` | crypto/rsa/rsa_pk1.c 387/546 | RSA output, PMS | yes | no (provider fp) | ~5k cyc + PRF | handshake | same | as above |
| OpenSSL 3.5 | `tls_decrypt_ticket` -> `d2i_SSL_SESSION_ex` | ssl/t1_lib.c 3031, ssl_asn1.c 262 | master_key | no (ASN1 template decode) | no | ~2-5k cyc | ClientHello | same | poor |
| OpenSSL 3.5 | `EVP_DecodeUpdate` | crypto/evp/encode.c 293 | PEM key | no (table lookup) | n/a | - | - | - | not CT |
| BoringSSL | `base64_ascii_to_bin`/`EVP_DecodeUpdate` -> `d2i_AutoPrivateKey` | crypto/base64/base64.cc 220-300 | PEM key | decode yes | yes to decode; parse via `d2i` | ~5 us / 2048-bit key | PEM scan | yes with `OPENSSL_NO_ASM` (C++ ssl) | backup 1 |
| BoringSSL | `tls_open_record` strip; `decrypt_ticket_with_cipher_ctx` -> `SSL_SESSION_parse`; `rsa_padding_check_PKCS1_type_2` | ssl/tls_record.cc 212, ssl/extensions.cc 4816-5020, crypto/rsa/rsa_crypt.cc 246 | type byte; session; PMS | no; no; yes | fp (EVP_AEAD, EVP_CIPHER, RSA_METHOD) | tiny; ~2k; ~5k | record/handshake | as above | backup 1 |
| wolfSSL | `TimingPadVerify`, `MaskPadding`, `MaskMac`; `RsaUnPad`; `DoDecryptTicket` -> `DoClientTicketFinalize` | src/internal.c 23532/23435/23463/43151, wolfcrypt/src/rsa.c 2039 | record; PMS; masterSecret | yes; yes; copies | fp `ssl->hmac`, `ticketEncCb` | ~100k; ~5k; ~0.3 us | record/handshake | yes with `--disable-armasm` | backup 2 |
| libsodium | `sodium_hex2bin`, `sodium_base642bin` | src/libsodium/sodium/codecs.c | key bytes | mostly | yes | ~100-300 cyc | none | yes | no decrypt-then-parse exists |
| OpenSSH | `ssh_packet_read_poll2`; `private2_decrypt` -> `sshkey_private_deserialize` -> `private2_check_padding` | packet.c 1622; sshkey.c 3016/2602/1739 | packet length+payload; private key | no | libcrypto fp | - | - | key parse only | poor |
| Tor | `tor1_crypt_relay_forward` -> `relay_cell_is_recognized_v0` -> `tor1_relay_digest_matches_v0` -> `decode_v0_cell` | src/core/crypto/relay_crypto_tor1.c 161/91/49, relay_msg.c 166 | cell payload | no | OpenSSL EVP fp | ~2k cyc (SHA-1) | cell routing | partial | poor |
| krb5 | `krb5_decrypt_tkt_part` -> `krb5int_dk_decrypt` (`k5_bcmp`) -> `decode_krb5_enc_tkt_part`; `krb5_kdc_rep_decrypt_proc` | lib/krb5/krb/decrypt_tk.c, lib/crypto/krb/enc_dk_hmac.c 81, kdc_rep_dc.c | session key in DER | bcmp yes, DER no | enctype fp tables | DER-size | none | yes | semantic fit only |
| httpd | `decrypt_string` -> `session_identity_decode` | modules/session/mod_session_crypto.c 344, mod_session.c | cookie fields | no | APR driver fp | - | - | no (request_rec) | out |
| WireGuard | `wg_noise_handshake_consume_initiation`, `wg_packet_consume_data_done` | drivers/net/wireguard/noise.c, receive.c | static key, timestamp, IP hdr | no (`memcmp`, hashtable on decrypted key) | direct but kernel | - | - | no (kernel) | out |
| OpenVPN | `openvpn_decrypt_v1` -> `packet_id_read` -> `packet_id_test` | src/openvpn/crypto.c 616, packet_id.c | 4-8 byte packet id | no | backend fp | ~50-100 cyc | - | yes (unit-test style) | too small |
| strongSwan | `decrypt_content` -> `parse` | src/libcharon/encoding/payloads/encrypted_payload.c 619/575 | IKE payloads | no | aead vtable fp | - | - | no | out |
| GnuPG | `get_it` frame parse | g10/pubkey-enc.c 234-430 | session key | no | agent IPC | - | - | no (two processes) | out |

## 12. Function-pointer flags (placement blockers)

- mbedTLS default build: `mbedtls_cipher_*` (`cipher_info->base->cbc_func/ecb_func/ctr_func`,
  `ctx->get_padding`) under every CBC/GCM/PBES2 decrypt; `pk_info->decrypt_func`
  under `mbedtls_pk_decrypt`; `conf->f_ticket_parse`/`f_ticket_write`; `conf->f_rng`;
  `handshake->tls_prf`/`calc_verify`/`calc_finished`; BIO `f_send`/`f_recv`;
  `conf->f_psk`. NOT function pointers: `mbedtls_md_*` (switch), PSA driver
  wrappers (switch), `mbedtls_ct_*`, `mbedtls_ssl_session_load`, the whole record
  glue in `mbedtls_ssl_decrypt_buf`, `mbedtls_rsa_*` internals.
- OpenSSL: provider method tables for every primitive plus `rl->funcs->*` in the
  record layer plus `md_transform` in `ssl3_cbc_digest_record`.
- BoringSSL: `EVP_AEAD->open`, `EVP_CIPHER`, `EVP_MD`, `RSA_METHOD`,
  `ticket_aead_method`.
- wolfSSL: `ssl->hmac`, `ctx->ticketEncCb`, I/O callbacks; wolfCrypt internals direct.
- OpenSSH/Tor/httpd/strongSwan/OpenVPN/krb5: fp-dispatched crypto backends.

## 13. Recommendation detail for the case study

Use mbedTLS and build one harness that exercises candidates 1 and 2 in the same
connection: TLS 1.2, `TLS-RSA-WITH-AES-128-CBC-SHA256`, ETM disabled on the
client. Handshake = RSA premaster path (rank 1); data phase = CBC MtE records
(rank 2). Add a resumption handshake with tickets for rank 3 (switch max version
to TLS 1.3 for the ticket-to-binder chain). Seeds to try, in the project's
`function,argindex,pointee` form:

- `mbedtls_rsa_rsaes_pkcs1_v15_decrypt,0,pointee` (private key; taints the RSA
  output buffer and thus `mbedtls_ct_rsaes_pkcs1_v15_unpadding`),
  `ssl_decrypt_encrypted_pms,3,pointee` (bridges `decrypt_func`),
  `ssl_compute_master,0,pointee` (bridges `tls_prf`).
- `mbedtls_ssl_decrypt_buf,1,pointee` (transform: keys) or `,2,pointee` (rec) for
  the record path.
- `mbedtls_ssl_ticket_parse,2,pointee` and `mbedtls_ssl_set_hs_psk,1,pointee` for
  tickets.

Expected story: annotating only `mbedtls_rsa_private` / `mbedtls_cipher_crypt` /
`psa_aead_decrypt` leaves the CT unpadding, the Bleichenbacher fake-PMS select,
the Lucky-13 pad scan + `ct_hmac` + `memcpy_offset`, and the ticket deserialization
uncovered; the compiler places regions there because the taint flows from the
primitive's output buffer. Measure region sizes to confirm the ~2-3 us (RSA glue)
and ~50-100 us (CBC record) estimates before choosing the headline.
