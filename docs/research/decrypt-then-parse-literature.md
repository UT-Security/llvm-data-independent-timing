# Decrypt-then-parse research memo: Attack literature: where the leak sat in the glue after the primitive, and what vendors shipped as the fix

**Written 2026-09-03** as input to `paper_experiments/10-mbedtls-session-ticket/README.md`.
Sources were fetched and quoted directly; every quantitative claim carries a URL, and
claims marked "unverified" were not confirmed against a primary source and must not
be quoted without one. The original memo follows unedited.

---

# Research A - Does "decrypt-then-parse" glue have real-world backing in the attack literature?

Scope: post-primitive glue (padding removal, MAC copy, unpadding, base64/DER/bignum decoding, packet/ticket/session parsing, version checks) that handles secret material that came OUT of a crypto primitive. For each item: secret, exact glue, whether the leak was in the glue (not the primitive), the fix (and whether the fix is straight-line constant-time (CT) C, i.e. the code a DIT region would cover), size of the glue relative to the primitive, primary sources. "Unverified" marks claims I could not confirm from a primary source in this session. Sources were fetched live (papers via pdftotext, library code at current master unless a branch is named).

Method note: ~40 primary sources were fetched (papers, advisories, ChangeLogs, and the current source of OpenSSL, BoringSSL, NSS, GnuTLS, mbedTLS 3.6 and development, TF-PSA-Crypto, wolfSSL, libsodium, OpenSSH, Go, GnuPG, MIT krb5, Apache httpd). File and function names below were grepped from those files, not recalled from memory.

---

## 0. One-paragraph verdict

The shape is real and repeatedly attacked. The two strongest, best-documented instances are (1) RSA PKCS#1 v1.5 / OAEP unpadding plus the TLS premaster version check after RSA decryption (Bleichenbacher 1998 -> ROBOT 2018 -> Marvin 2023 -> Kerberos PKINIT 2024 -> Authlib JWE 2026), and (2) TLS CBC padding removal + MAC extraction + MAC-over-secret-length after AES-CBC decryption (Lucky 13 2013 and its five follow-ups through 2018). In both, every major C library today ships the glue as straight-line masked C (OpenSSL, BoringSSL, NSS, mbedTLS, wolfSSL; Go likewise), i.e. exactly the code that is "classically constant-time" and that value predictors would still leak from. A third instance, private-key file parsing (base64 -> DER -> bignum), has a published single-trace key-recovery attack (Util::Lookup, CCS 2021) and four vendor CT rewrites, and is the only one where the secret is a long-term key; OpenSSL, NSS and Go still ship table-based base64 decoders. The IETF itself states the limit of primitive-only hardening in RFC 8446 Appendix E.3: "even a constant-time padding removal function will likely feed the content into data-dependent functions".

---

## 1. Lucky Thirteen and its descendants (TLS CBC: padding check, MAC copy, MAC over secret length)

Paper: AlFardan and Paterson, "Lucky Thirteen: Breaking the TLS and DTLS Record Protocols", IEEE S&P 2013. https://www.isg.rhul.ac.uk/tls/TLStiming.pdf

- Secret: TLS record plaintext (the paper targets HTTP session cookies). Application data, not a key; the attacker recovers it byte by byte.
- Glue that leaked: after CBC decryption, (a) the padding-length byte is read and the padding checked, (b) the MAC is located at a secret offset, (c) HMAC is computed over a secret-length message. HMAC cost is quantised in 64-byte compression blocks: "an extra compression function evaluation is needed for each additional 64 bytes of message data ... A single compression function evaluation takes typically around 500 to 1000 hardware cycles" (paper, Sec. 3). The distinguishing signal is one compression call (the paper's OpenSSL measurements are in the hundreds of cycles; "187 hardware cycles on our targeted server operating at a speed of 1.87 GHz translate to an absolute timing of 100 ns", Sec. 5).
- Leak location: entirely post-decryption. The block cipher was never the issue; the RFC 5246 6.2.3.2 advice ("This leaves a small timing channel ... not believed to be large enough to be exploitable") was the bug.
- Affected: OpenSSL (fixed 1.0.1d/1.0.0k/0.9.8y, 5 Feb 2013), NSS 3.14.3, PolarSSL 1.2.5, CyaSSL 2.5.0, GnuTLS (version not captured from the PDF; unverified), Java, Bouncy Castle (paper Sec. 1 and 6).
- Fix = straight-line CT glue, yes. OpenSSL commit 2acc020b770920657a169bf6be4ff12b254255e6 "Make CBC decoding constant time." (Adam Langley, 2013-01-28; found via GitHub commit search). Langley's write-up (https://www.imperialviolet.org/2013/02/04/luckythirteen.html): "always make memory accesses as if the padding was the maximum length", "read every location where the MAC might be found and copy to a MAC-sized buffer", "mac_size^2 operations to rotate it in constant-time (since the amount of rotation is also secret)", "we generate the contents of each of the final hash blocks in constant time and hash each of them", and a residual leak from "a DIV instruction timing variance of 16 cycles" that had to be removed. Albrecht and Paterson later quantified it: "OpenSSL prevents the Lucky 13 attack in 500 lines of code which achieves fully constant time/memory access" (Lucky Microseconds, Sec. 1.2).
- Current code (OpenSSL master):
  - ssl/record/methods/tls_pad.c: tls1_cbc_remove_padding_and_mac ("We can't check just |padding_length+1| bytes because that leaks decrypted information ... to_check = 256"), ssl3_cbc_copy_mac (rotation; "independent of the concrete value of the record length |reclen|, which may vary within a 256-byte window").
  - ssl/record/methods/ssl3_cbc.c: ssl3_cbc_digest_record ("data_size: the secret, reported length of the data once the MAC and padding has been removed"; processes max_mac_bytes/num_blocks/variance blocks with constant_time_eq_8_s masks).
  - providers/implementations/ciphers/cipher_aes_cbc_hmac_sha1_hw.c: the AES-NI stitched path ("code containing lucky-13 fix", constant_time_ge(maxpad, pad), "If pad is invalid then we will fail the above test but we must continue anyway because we are in constant time code").
  - Size: padding check 256 bytes; MAC copy scan of up to 256+mac bytes plus mac_size^2 rotation; ssl3_cbc_digest_record hashes the whole record (up to 2^14 bytes + 13-byte header). Relative to the primitive: with AES-NI the CT HMAC glue is the majority of decryption cycles for large records (HMAC-SHA at ~500-1000 cycles per 64 B vs. AES-NI CBC decrypt at roughly 1 cycle/byte or less; the AES figure is my estimate, the SHA figure is the paper's).
- Other libraries, current code:
  - BoringSSL crypto/cipher/tls_cbc.cc: EVP_tls_cbc_remove_padding, EVP_tls_cbc_copy_mac, EVP_tls_cbc_digest_record (uses value_barrier_w to stop the compiler from turning masks back into branches).
  - NSS lib/ssl/ssl3con.c: ssl_RemoveTLSCBCPadding, ssl3_CBCExtractMAC (rotateOffset masking), ssl3_ComputeRecordMACConstantTime; lib/freebl/hmacct.c MAC() ("varianceBlocks = isSSLv3 ? 2 : 6").
  - mbedTLS library/ssl_msg.c (3.6 and development): padding loop with mbedtls_ct_uint_ge/mbedtls_ct_uint_eq ("The padding check involves a series of up to 256 ... always perform exactly" the same accesses), mbedtls_ct_hmac (ssl_msg.c line 66/187 in 3.6; line 58 in development), mbedtls_ct_memcpy_offset (library/constant_time.c in 3.6; utilities/constant_time.c in TF-PSA-Crypto for 4.x). ChangeLog: 1.3.10 (2015-02) "Padding checks in cipher layer are now constant-time"; 1.3.x "Add countermeasure against 'Lucky 13 strikes back'"; 2.12.0 (2018-07-25) CVE-2018-0497/0498; 2.23.0 (2020-07) countermeasure ineffective with MBEDTLS_SHAxxx_ALT; 2.24.0 (2020-09-01) "The new countermeasure defends against local attackers, even if they have access to fine-grained measurements" (reported by Yavuz, Fowze, Bai, Hernandez, Butler); 3.1.0 (2021-12) introduces constant_time.c.
  - wolfSSL src/internal.c: MaskPadding ("Constant time implementation - does maximum pad size possible", ctMaskLTE loop to TLS_MAX_PAD_SZ), MaskMac (scanStart = sz-1-TLS_MAX_PAD_SZ-macSz), TimingPadVerify ("Clamp it in constant time"); src/tls.c Hmac_UpdateFinal_CT ("Update-Final need to be constant time"). The older dummy-compression version (PadCheck/CompressRounds/GetRounds) is still in the file under a build option. Ronen et al.: wolfSSL "switched to the full constant time solution in release 3.15.3 (released 20th June 2018)".
  - GnuTLS lib/cipher-cbc.c: dummy_wait ("force additional hash compression function evaluations to prevent timing attacks"), "we access all 256 bytes of ciphertext for padding check". This is pseudo-CT (branches remain); Red Hat assigned CVE-2018-10844/10845/10846 and GnuTLS chose to promote Encrypt-then-MAC (RFC 7366) rather than a full CT rewrite (Ronen et al., Sec. 1.3).
  - Go crypto/tls/conn.go extractPadding ("returns, in constant time, the length of the padding ... toCheck := 256") and a "roughly constant time" MAC.
- Follow-ups (all in the glue, none in AES):
  - Lucky Microseconds (Albrecht, Paterson; eprint 2015/1129): s2n_verify_cbc "counted bytes submitted to HMAC instead of compression function calls"; fix counted compression calls and switched usleep -> nanosleep.
  - Lucky 13 Strikes Back (Irazoqui et al., AsiaCCS 2015): Flush+Reload under memory deduplication detects the dummy calls; PolarSSL 1.3.6, GnuTLS, CyaSSL broken; "the Lucky 13 patches in OpenSSL, Mozilla NSS and MatrixSSL are immune".
  - CVE-2016-2107 (OpenSSL secadv 2016-05-03): the CT padding check "failed to verify sufficient data existed for both MAC and padding bytes"; found by Somorovsky with TLS-Attacker; fixed 1.0.2h/1.0.1t. A bug inside the CT glue itself.
  - Stacco (Xiao et al., CCS 2017, arXiv 1707.03473): page/cacheline/branch-level control-flow inference in SGX on the same glue; "almost all libraries, except for OpenSSL, are vulnerable to all levels"; 48,388 (GnuTLS) and 25,717 (mbedTLS-SGX) queries per AES block; Bleichenbacher on OpenSSL RSA-4096 premaster in 57,286 queries.
  - Pseudo Constant Time Implementations of TLS Are Only Pseudo Secure (Ronen, Paterson, Shamir; eprint 2018/747): SHA-384 constants hard-coded as "64"/"8" instead of "128"/"16" in mbedTLS ssl_decrypt_buf, GnuTLS dummy_wait ("9" vs "17"), wolfSSL GetRounds ("64"/"55" vs "128"/"111"), s2n; cache attacks on padding-dependent memory access; conclusion: "nothing short of the full 'belt and braces' approach adopted in OpenSSL is sufficient".

Assessment for the case study: strongest attack lineage; largest straight-line CT glue in shipping code; but the secret is application plaintext and CBC suites are legacy (absent from TLS 1.3).

---

## 2. SSH CBC plaintext recovery (decrypt the length field, then parse)

Paper: Albrecht, Paterson, Watson, "Plaintext Recovery Attacks Against SSH", IEEE S&P 2009. Mirror: https://www.cs.umd.edu/~jkatz/security/downloads/PlaintextRecoverySSH.pdf (isg.rhul.ac.uk copy is 404).

- Secret: 32 bits of an arbitrary plaintext block (with probability 2^-18; 14 bits with 2^-14 against OpenSSH 4.7 in CBC mode).
- Glue: the 4-byte packet length is in the first decrypted block and "must be computed before the MAC can be validated"; OpenSSH "first checking that the packet length field is at most 2^18" and a block multiple, then waits for that many bytes before the MAC check. The oracle is how many bytes must be fed before the MAC error appears, plus the length sanity checks.
- Leak location: post-decryption parse, by design of the Binary Packet Protocol.
- Fix: not straight-line CT code. OpenSSH advisory (https://www.openssh.com/txt/cbc.adv) recommended CTR/arcfour; OpenSSH 5.2 made CTR preferred and added the discard mitigation still present in packet.c: ssh_packet_start_discard/ssh_packet_stop_discard ("Record number of bytes over which the mac has already been computed in order to minimize timing attacks"; on a bad length it discards PACKET_MAX_SIZE bytes and runs mac_compute over them before failing). This equalises protocol behaviour; the parse itself remains ordinary C.
- Size: 4 secret bytes and two comparisons; negligible relative to the cipher.
- Assessment: excellent historical example of the shape, weak as a DIT case (no vendor CT glue, secret is plaintext, CBC in SSH is disabled by default today).

---

## 3. RSA unpadding after RSA decryption: Bleichenbacher, Manger, ROBOT, Marvin; the TLS version check; Raccoon

ROBOT (Boeck, Somorovsky, Young; USENIX Security 2018; eprint 2017/1189; https://robotattack.org):
- Secret: TLS premaster secret (and via the private-key oracle, signatures).
- Glue: PKCS#1 v1.5 unpadding, the RFC 5246 7.4.7.1 version check and the error handling around them. Oracles were behavioural (different alerts, "connection timeouts, TCP resets, duplicate alert responses"). Vendors/CVEs: F5 CVE-2017-6168, Citrix CVE-2017-17382, Radware CVE-2017-17427, Cisco ACE CVE-2017-17428, Erlang CVE-2017-1000385, Bouncy Castle CVE-2017-13098, wolfSSL CVE-2017-13099 ("a timeout for a correctly formatted message and errors for all messages that had any flaw in their structure ... fixed in Git, 3.12.2 still vulnerable"), plus Palo Alto, IBM GSKit, Cisco ASA.
- The paper's Sec. 8.2 already points at the next glue leak: "In OpenSSL the result of the RSA decryption is handled with the internal BN (bignum) functions. If the decrypted value has one or several leading zeros the operation will be slightly faster" (attributed to Adam Langley).

Marvin (Kario, "Everlasting ROBOT: the Marvin Attack", eprint 2023/1442; https://people.redhat.com/~hkario/marvin/):
- Explicit primitive/glue decomposition (Sec. 4): "1. Modular exponentiation ... 2. Padding checks and secret extraction (PKCS#1 v1.5 or OAEP) 3. Secret value use and error handling".
- Where the residual leaks were: not in the padding code but in the boundary between the primitive and the bytes: variable-width bignum "clamping/normalization" after unblinding, and the conversion "into a byte string (which is necessary to test padding ... or to feed it into a KDF)". Site text: "Both OpenSSL and NSS bugs were in the numerical library, not in the padding or error reporting code." Magnitudes: OpenSSL "just under 30ns", NSS "about 60ns" on an i9-12900KS; GnuTLS after fix "+-2ns (so about 10.5 CPU cycles)"; libgcrypt ~200 ns (gcrypt-devel thread, the leak there being an s-expression allocation sized exactly to the message on success and none on failure).
- CVEs: OpenSSL CVE-2022-4304 (secadv 2023-02-07; "affects all RSA padding modes: PKCS#1 v1.5, RSA-OEAP and RSASVE"; fixed 3.0.8/1.1.1t), NSS CVE-2023-4421, GnuTLS CVE-2023-0361 (3.8.0) and CVE-2023-5981/2024-0553, Go CVE-2023-45287, python-cryptography CVE-2023-50782, M2Crypto CVE-2023-50781, mbedTLS CVE-2024-23170, Java, BouncyCastle. mbedTLS 3.6.7 (2026-07-07) still fixing this class: "error handling in the library would reveal through timing the difference between success, invalid padding, or output too large for buffer".
- Fix approach: implicit rejection (synthetic random message derived from key+ciphertext on failure) plus CT last-multiplication and fixed-width bn2bin. Kario: "We were able to implement both the arbitrary precision multiplication and Montgomery reduction algorithms for 64 bit CPUs in just 200 lines of portable C code."

Current straight-line CT unpadding glue (all verified in source):
- OpenSSL crypto/rsa/rsa_pk1.c: ossl_rsa_padding_check_PKCS1_type_2 (scans all num bytes with constant_time_*; masked logarithmic shift copy "in a way that does not reveal the size of the data being copied via a timing side channel"; implicit rejection: "will return a deterministically generated random message"), ossl_rsa_padding_check_PKCS1_type_2_TLS ("Klima-Pokorny-Rosa extension of Bleichenbacher's attack exploits the version number check ... version checks are done in constant time and are treated like any other decryption error"; random premaster on failure). crypto/rsa/rsa_oaep.c RSA_padding_check_PKCS1_OAEP_mgf1 (Manger reference; masked copy). ssl/statem/statem_srvr.c tls_process_cke_rsa. Size: one masked pass over num bytes (256 for RSA-2048) plus a log(num)-step masked shift; a few thousand cycles versus ~10^6 cycles for the CRT exponentiation, i.e. well under 1% (my estimate).
- mbedTLS library/rsa.c (3.6; TF-PSA-Crypto drivers/builtin/src/rsa.c in 4.x): mbedtls_ct_rsaes_pkcs1_v15_unpadding. Its comment is the clearest statement of the threat model in any library: "Potential side channels include overall timing, memory access patterns (especially visible to an adversary who has access to a shared memory cache), and branches (especially visible to an adversary who has access to a shared code cache or to a shared branch predictor)."
- wolfSSL wolfcrypt/src/rsa.c: RsaUnPad ("Decrypted with private key - unpad must be constant time", ctMask16Eq scan over all bytes), wc_RsaUnPad_ex, RsaUnPad_OAEP (ConstantCompare + ctMaskSelWord32); wolfcrypt/src/misc.c ctMask*.
- NSS lib/freebl/rsapkcs.c: RSA_DecryptBlock ("we always have to generate a full moduluslen error string. Otherwise we create a timing dependency on errorLength"; PORT_CT_* masks; deterministic error output), OAEP decode with constantTimeEQ8/constantTimeCompare.
- BoringSSL ssl/handshake_server.cc: CONSTTIME_SECRET(decrypt_buf.data(), decrypt_len) then "Prepare a random premaster, to be used on invalid padding. See RFC 5246". (Its RSA_padding_check_PKCS1_type_2 lives in a file I did not fetch; unverified path.)
- Go crypto/rsa/pkcs1v15.go: DecryptPKCS1v15SessionKey (subtle.ConstantTimeEq/ConstantTimeCopy; "to allow constant time padding removal").
- GnuTLS lib/nettle/pk.c: _rsa_sec_decrypt (Nettle rsa_sec_decrypt) with the comment that failure must avoid "unallocation (which creates a side channel)".

Raccoon (Merget, Brinkmann, Aviram, Somorovsky et al., 2020; https://raccoon-attack.com):
- Secret: DH premaster (g^ab mod p). Glue: TLS <= 1.2 "all leading zero bytes in the premaster secret are stripped before used in further computations", so the PRF hashes fewer blocks. Leak is in the strip-then-hash step between the DH primitive and the KDF. OpenSSL CVE-2020-1968 (secadv 2020-09-09, rated Low, static DH only; mitigation was SSL_OP_SINGLE_DH_USE by default since 1.0.2f, not a CT rewrite), F5 CVE-2020-5929. Today OpenSSL ssl/s3_lib.c ssl_derive sets EVP_PKEY_CTX_set_dh_pad(pctx, 1) (fixed-width output), and TLS 1.3 keeps leading zeros. Glue size: one BN-to-bytes conversion of 256 bytes.

Assessment: the strongest candidate. Attack every few years for 25 years; every library rewrote the glue as straight-line masked C; the secret (premaster, session key, CEK) stays secret through the glue and is consumed by the next primitive (PRF/KDF/AEAD). Also see Sec. 9: Kerberos PKINIT and JWE RSA1_5 are the same glue in non-TLS settings.

---

## 4. "Certified Side Channels" (Garcia, ul Hassan, Tuveri, Gridin, Cabrera Aldaya, Brumley; USENIX Security 2020; arXiv 1909.01785)

- Thesis: "the format in which private keys are persisted impacts Side Channel Analysis (SCA) security"; "key parsing in general ... shows potential as a lucrative SCA attack vector".
- What leaked (long-term private keys, via timing, EM, cache):
  1. OpenSSL EC keys with explicit curve parameters, or with cofactor omitted/zero: parsing yields an EC_GROUP that bypasses EC_GFp_nistz256_method and the Montgomery-ladder path and falls into the SCA-insecure wNAF code (CVE-2019-1547). Found in the wild (GOST engine keys, RFC 4357).
  2. OpenSSL DSA keys in PVK/MSBLOB formats (crypto/pem/pvkfmt.c): the parser recomputes the public key with BN_mod_exp without BN_FLG_CONSTTIME, leaking "more than half of the exponent bits" via Prime+Probe.
  3. OpenSSL RSA_check_key_ex (crypto/rsa/rsa_chk.c), reachable from `openssl rsa -check` and `pkey -check`: BN_mod_inverse and gcd leak p and q.
  4. mbedTLS v2.18.1 mbedtls_pk_parse_keyfile -> mbedtls_rsa_deduce_crt / mbedtls_rsa_deduce_private_exponent -> mbedtls_mpi_inv_mod and mbedtls_mpi_gcd (binary extended Euclid), "this code path in mbedTLS executes every time this library loads a private key".
- Fixes: mostly steering, not CT rewrites of the parser: OpenSSL computes the cofactor from the Hasse bound, sets BN_FLG_CONSTTIME on the parse-time computations, matches explicit parameters to named curves, and replaced the variable-time GCD with a CT Bernstein-Yang implementation. mbedTLS 2.21.0 (2020-02-20): "To avoid a side channel vulnerability when parsing an RSA private key, read all the CRT parameters from the DER structure rather than reconstructing them" (Jack Lloyd; ARMmbed/mbed-crypto#352). mbedTLS advisory 2020-07 (2.23.0): mbedtls_pk_parse_key/mbedtls_ecp_check_pub_priv did an unrandomised scalar multiplication on import (Cabrera Aldaya, Brumley).
- Base64/DER themselves are NOT what leaked in this paper (that is Util::Lookup, Sec. 6). The leak is the arithmetic the parser triggers on the freshly decoded secret.
- Related: "Deja Vu: Side-Channel Analysis of Mozilla's NSS" (ul Hassan et al., CCS 2020; arXiv 2008.06004): CVE-2020-12399 DSA ("This nonce unpadding opens the door to a timing attack"), CVE-2020-6829/12400 ECDSA wNAF, CVE-2020-12402 RSA keygen GCD (EM); mostly primitive-level, but the DSA one is a representation/glue leak of the same kind as item 5.

Assessment: strong on "secret stays secret" (long-term key) and on published attacks; weaker on "vendor rewrote the glue as CT" (they mostly avoided the computation). Combined with item 6 it makes the best long-term-key case study.

---

## 5. "Big Numbers - Big Troubles" (Weiser, Schrammel, Bodden, Gruss; USENIX Security 2020; https://www.usenix.org/system/files/sec20summer_weiser_prepub_0.pdf)

- Secret: (EC)DSA nonce k (leaks a few bits per signature; lattice recovers the key). Stays secret through the glue.
- Glue leaks (Table 2), in OpenSSL/LibreSSL/BoringSSL:
  - V2 "k-padding resize": lazy bn_wexpand when adding q to the nonce leaks the topmost zero bits (CVE-2018-0734, CVE-2018-0735); V3 consttime-swap; V4 a downgrade introduced while fixing V2; V5 k-padding (top).
  - V6 "Buffer conversion" (OpenSSL EC, nistp 64-bit code): "the nonce is converted from a Bignumber to a byte array with BN_bn2bin", leaking "Topmost 0-bytes of k"; BN_bin2bn "introduces a tiny side-channel leakage" by removing leading zeros.
  - V8/V9 Euclid BN_div and conditional negation in inversion; V10 small k^-1 (LibreSSL removes blinding too early).
- Fixes: OpenSSL commits 99540ec, 8b44198b, 805315d3, PR #9511; OpenSSL switched to Fermat inversion; "the OpenSSL team decided to rework Bignumber arithmetic, similar to BoringSSL" (fixed-width limbs). BoringSSL crypto/fipsmodule/bn/bytes.cc.inc BN_bin2bn / BN_bn2bin_padded are fixed-width. mbedTLS 2.7.0 (2018-02-03): "Make mbedtls_mpi_read_binary() constant-time with respect to the input data. Previously, trailing zero bytes were detected and omitted ... potentially leading to slight timing differences" (Macchetti, Kudelski). Util::Lookup's Microwalk table independently rates OpenSSL BN_bin2bn at ~4.0 bits of leakage and ASN1_get_object at ~2.9 bits.
- Taxonomy: the paper does not name a glue category; it structures leaks by nonce lifecycle step (generation, exponentiation/scalar multiplication, inversion, multiplication) plus "Nonce representation". The recurring root cause is minimal-width bignum representation, i.e. secret-dependent length at the bytes<->bignum boundary.
- Size: tens to hundreds of bytes per conversion; negligible versus the scalar multiplication.

---

## 6. Base64 decoding of secrets

Paper: Sieck, Berndt, Wichelmann, Eisenbarth, "Util::Lookup: Exploiting key decoding in cryptographic libraries", CCS 2021, arXiv 2108.04600.
- Secret: RSA private key from a PEM file (PKCS#8/PKCS#1). Stays secret.
- Glue: lookup-table (LUT) base64 decoding in PEM loading. Table 1: Botan 2.17.0, GNU Nettle 3.6, mbedTLS 2.24.0, OpenSSL 1.1.1h, wolfSSL 4.5.0 (80-byte LUT), RustSGX, Microsoft CNG; expected leakage 0.81-0.97 bit per character. In OpenSSL "Functions prefixed with EVP_DecodeUpdate ... EVP_DecodeBlock ... performs a LUT-based" decode; Microwalk (Table 5) rates EVP_DecodeUpdate/EVP_DecodeBlock at the 12-bit ceiling, BN_bin2bn ~4.0 bits, ASN1_get_object ~2.9 bits, PEM_read_bio ~1 bit.
- Attack: single-stepped Prime+Probe on SGX enclaves; "LVI ... mitigations ease the exploitability"; full RSA key recovery with an extend-and-prune algorithm from one trace.
- Disclosure (paper Sec. 1.2): Botan CVE-2021-24115 (fixed 2.17.3); mbedTLS CVE-2021-24119 (fixed 2.26.0); wolfSSL CVE-2021-24116 (fixed 4.6.0); RustSGX CVE-2021-24117; "GNU Nettle: No response, not yet fixed"; "NSS: Not yet fixed"; "OpenSSL: No response, not yet fixed". OSV text for CVE-2021-24116/24119: "a side-channel vulnerability in base64 PEM file decoding allows system-level (administrator) attackers to obtain information about secret RSA keys via a controlled-channel and side-channel attack on software running in isolated environments that can be single stepped, especially Intel SGX."
- Fixes are straight-line CT C (case-decision, no tables):
  - BoringSSL crypto/base64/base64.cc (pre-dates the paper): "Since PEM is sometimes used to carry private keys, we decode base64 data itself in constant-time." (and the same sentence for encoding).
  - libsodium src/libsodium/sodium/codecs.c: b64_char_to_byte/b64_byte_to_char built from EQ/GT/GE/LT arithmetic masks; docs: "unlike base64_decode(), sodium_base642bin() is constant-time (a property that is important for any code that touches cryptographic inputs, such as plaintexts or keys)". No commit rationale beyond the documentation was found (unverified why/when).
  - mbedTLS library/base64.c: mbedtls_ct_base64_dec_value / mbedtls_ct_base64_enc_char; ChangeLog 2.26.0 (2021-03-08): "Guard against strong local side channel attack against base64 tables by making access to them use constant flow code"; 3.1.0: "Improve the performance of base64 constant-flow code. The result is still slower than the original non-constant-flow implementation".
  - wolfSSL wolfcrypt/src/coding.c: Base64_Char2Val_CT is the default in Base64_Decode; the table version was demoted to Base64_Decode_nonCT.
  - Nimbus-JOSE-JWT (Java) also uses case-decision decoding (paper Sec. 6).
- Still table-based today (verified in source): OpenSSL crypto/evp/encode.c (static const unsigned char data_ascii2bin[128]; conv_ascii2bin returns table[a]); Go encoding/base64 (decodeMap [256]uint8). I found no evidence of a Go "constant-time base64" change; treat that premise as unverified/likely incorrect. Go's crypto/x509 and encoding/pem use the table decoder.
- Size: an RSA-2048 PKCS#8 PEM is ~1.7 KB of text -> ~1.2 KB DER; CT decoding costs a few dozen ALU ops per character (tens of thousands of cycles); no paper measured cycles. There is no "next primitive": the secret flows base64 -> DER (ASN1_get_object) -> BN_bin2bn -> key struct, i.e. three utility layers in a row, which is a good compiler demonstration.
- HTTP Basic auth / JWT secrets: no paper or advisory found treating base64 of passwords as a side channel; unverified/thin.

---

## 7. TLS 1.3 record padding removal (scan backwards over zeros for the content type)

- RFC 8446 Sec. 5.4: "the receiving implementation scans the field from the end toward the beginning until it finds a non-zero octet. This non-zero octet is the content type". Appendix E.3: padding hides length "but may be able to measure it indirectly by the use of timing channels exposed during record processing ... even a constant-time padding removal function will likely feed the content into data-dependent functions. At minimum, a fully constant-time server or client would require close cooperation with the application-layer protocol implementation".
- Mavrogiannopoulos (Red Hat), IETF TLS list, 11 Aug 2017, "draft-ietf-tls-tls13-21: TLS 1.3 record padding removal leaks padding size" (https://mailarchive.ietf.org/arch/msg/tls/otmUa4vDrXNIJAGb3m2P7ZfeqF0/): the byte-by-byte scan lets an adversary "distinguish between a 1-byte payload and 1024-byte payload".
- Secret: only the padding/content length (traffic analysis), not key material. No published attack paper found.
- Implementations (verified at master): OpenSSL ssl/record/methods/tls_common.c tls13_common_post_process_record, BoringSSL ssl/tls_record.cc (do { type = out->back(); ... } while (type == 0)), NSS lib/ssl/tls13con.c tls13_UnprotectRecord (while (plaintext->len > 0 && !(plaintext->buf[plaintext->len - 1]))), mbedTLS library/ssl_msg.c ssl_parse_inner_plaintext (do/while), wolfSSL src/internal.c (for loop with break), Go crypto/tls/conn.go (loop with break) - all variable-time by design. Only GnuTLS lib/cipher.c offers "we intentionally iterate through all data, to avoid leaking the padding length due to timing differences in processing" when the application sets GNUTLS_SAFE_PADDING_CHECK (manual: "under TLS1.3 the padding removal time depends on the padding data for an efficient implementation"), and even that loop keeps a data-dependent branch.
- Assessment: recognised but unfixed; weak secret; useful only as a secondary illustration.

---

## 8. Session tickets: decrypt, then parse the session (including the master/resumption secret)

- OpenSSL ssl/statem/extensions_srvr.c tls_decrypt_ticket: HMAC checked with CRYPTO_memcmp, EVP_Decrypt*, then d2i_SSL_SESSION over the plaintext; ssl/ssl_asn1.c has master_key as an ASN1_OCTET_STRING in SSL_SESSION_ASN1. The same file's PSK path says of itself: "None of this code is constant time anyway." (comment near the binder check).
- mbedTLS library/ssl_ticket.c mbedtls_ssl_ticket_parse: AEAD decrypt (psa_aead_decrypt or mbedtls_cipher_auth_decrypt_ext) then mbedtls_ssl_session_load (library/ssl_tls.c); key selection by name is a plain loop; no CT considerations.
- Literature: none. Searches for timing/side-channel analysis of ticket parsing (d2i_SSL_SESSION, mbedtls_ssl_ticket_parse) returned nothing relevant; the only related work is STEK-compromise analysis (Hebrok et al., USENIX Security 2023) which is about key management, not parsing. Because tickets are AEAD/Encrypt-then-MAC there is no padding oracle; the parse is length-driven copying of a secret whose length is public.
- Assessment: the shape is exactly the one wanted (secret out of AEAD, then ASN.1/serialisation parse, then into the PRF/HKDF), the secret genuinely stays secret, but there is zero attack literature. Say so if used.

---

## 9. Kerberos, GnuPG/libgcrypt PKESK, JWE

Kerberos:
- Shagam and Ronen, "Windows into the Past: Exploiting Legacy Crypto in Modern OS's Kerberos Implementation", USENIX Security 2024 (abstract from usenix.org): "smartcard-based authentication uses RSA encryption with the notorious PKCS #1 v1.5 padding scheme. Although the RSA decryption is done securely inside the smartcard, a non-constant time unpadding code runs on the client's CPU. This makes both Windows's and several Linux distributions' implementations vulnerable to the Bleichenbacher attack that can recover cryptographic session tokens ... we demonstrate microarchitectural side channel-based end-to-end attacks on the Windows Kerberos implementation". Microsoft CVE-2024-29995 (CWE-208 observable timing discrepancy; the NVD page could not be fetched, CWE from the search result, unverified). This is the purest instance of "primitive secure, glue leaks" found in this survey.
- Symmetric path (MIT krb5 src/lib/crypto/krb/: decrypt.c, enc_dk_hmac.c, verify_checksum.c; then ASN.1 decode of EncASRepPart in lib/krb5/krb via krb5_kdc_rep_decrypt_proc): decrypt, HMAC-over-plaintext compare, ASN.1 decode of the session key. No published timing analysis found; thin.

GnuPG / libgcrypt:
- g10/pubkey-enc.c get_it(): after pk decrypt the "DEK frame" "0 2 RND(n bytes) 0 A DEK(k bytes) CSUM(2 bytes)" is parsed with ordinary branches (checks on frame[frameidx], algorithm byte, 16-bit checksum). Not CT.
- Format oracles: Maury, Reinhard, Levillain, Gilbert, "Format Oracles on OpenPGP", CT-RSA 2015 (HAL hal-01154822): padding-oracle principle "generalized to exploit any property of decrypted ciphertexts, either stemming from the encryption scheme, or the application data format"; oracles in GnuPG and others; "2 to 2^8 oracle requests per plaintext byte". Mister and Zuccherato 2005 (eprint 2005/033): the post-decryption "quick check" of the repeated prefix bytes in OpenPGP CFB is an oracle (~2 queries per 16-byte block; CVE-2005-0366; GnuPG disabled the quick check). Ising et al., "multipart/oracle: Tapping into Format Oracles in Email End-to-End Encryption", USENIX Security 2023 (title only verified).
- Marvin in libgcrypt (gcrypt-devel thread): s-expression allocation sized to the message on success versus none on failure gives "a very clear signal" (~200 ns); the maintainers' response was to document that remote timing is in scope rather than to ship a CT rewrite (per the thread summary; unverified beyond that).
- Assessment: strong literature that the parse after decryption is an oracle, but the fixes were behavioural (disable checks, MDC/AEAD), not CT glue.

JWE (RSA1_5 key unwrap, then CEK length/format checks):
- Detering, Somorovsky, Mainka, Mladenov, Schwenk, "On The (In-)Security Of JavaScript Object Signing And Encryption", ROOTS 2017: "severe vulnerabilities in six popular JOSE libraries ... up to highly complex cryptographic Bleichenbacher attacks breaking the confidentiality of encrypted JSON messages" (abstract via search; the PDF host refused connections, so the per-library list is unverified).
- Authlib CVE-2026-28490 (GHSA-7432-952r-cw78): the underlying `cryptography` library returns random bytes on bad PKCS#1 v1.5 padding (CT implicit rejection), but Authlib "raises ValueError('Invalid "cek" length') immediately after decryption, before reaching AES-GCM tag validation", recreating the oracle. PHP JWE GHSA-5739-39v2-5754 (Marvin-style). Textbook: correct CT primitive, leaky glue one call up.

---

## 10. Cookie/session decryption then deserialisation in C web servers

- Apache httpd modules/session/mod_session_crypto.c decrypt_string: apr_base64_decode -> SipHash MAC verify (ap_siphash24_auth) -> apr_crypto_block_decrypt -> mod_session.c session_identity_decode (key=value string parse, expiry check). Shape present; no side-channel literature at all (searches return only the module documentation). Thin, as expected.

---

## Q1. Is there a recognised name/taxonomy for "the leak is in the glue after the primitive"?

There is no single agreed term, but several overlapping named categories, each with a primary source:
1. "Padding oracle" (Vaudenay, EUROCRYPT 2002) and its generalisation "format oracle" - Maury et al., CT-RSA 2015: "generalized to exploit any property of decrypted ciphertexts, either stemming from the encryption scheme, or the application data format"; reused by Ising et al., USENIX Security 2023. This names the oracle, and the oracle sources explicitly include "timing leaks, memory caching strategies, and other side-channels" (CT-RSA 2015 paper, per the fetched summary).
2. "Pseudo constant time" (Ronen, Paterson, Shamir, 2018) for glue that equalises operation counts but not memory access; the contrasting "fully constant time/constant memory access" (Albrecht and Paterson 2015, of OpenSSL's Lucky 13 glue).
3. "Certified side channels" / "key formats as an SCA attack vector" / "key parsing" (Garcia et al., USENIX Security 2020).
4. "Utility functions" as the overlooked class (Sieck et al., CCS 2021: "a common oversight in these libraries is the existence of utility functions, which handle and thus possibly leak confidential information").
5. "Control-flow inference attacks" on "error handling and reporting" code (Stacco, CCS 2017).
6. Marvin's three-step decomposition of RSA decryption (modexp / padding check and secret extraction / secret use and error handling), which is the closest thing to a primitive-vs-glue taxonomy in the literature.
7. "Bit lengths can be secrets, too" (Brumley and Tuveri 2011, as summarised in Deja Vu) for the leading-zero/length family (Raccoon, BN_bin2bn, mpi_read_binary, nonce unpadding).
8. Standards-level recognition: RFC 8446 E.3 ("even a constant-time padding removal function will likely feed the content into data-dependent functions").

## Q2. Which glue regions are straight-line CT C in shipping libraries today (the code DIT would protect)?

Verified in current source (paths at master unless noted):
- TLS CBC padding/MAC glue: OpenSSL ssl/record/methods/tls_pad.c (tls1_cbc_remove_padding_and_mac, ssl3_cbc_copy_mac), ssl/record/methods/ssl3_cbc.c (ssl3_cbc_digest_record), providers/implementations/ciphers/cipher_aes_cbc_hmac_sha1_hw.c (and the sha256 twin); BoringSSL crypto/cipher/tls_cbc.cc (EVP_tls_cbc_remove_padding, EVP_tls_cbc_copy_mac, EVP_tls_cbc_digest_record); NSS lib/ssl/ssl3con.c (ssl_RemoveTLSCBCPadding, ssl3_CBCExtractMAC, ssl3_ComputeRecordMACConstantTime) and lib/freebl/hmacct.c (MAC); mbedTLS library/ssl_msg.c (padding loop, mbedtls_ct_hmac, mbedtls_ct_memcpy_offset in library/constant_time.c for 3.6 and utilities/constant_time.c in TF-PSA-Crypto for 4.x); wolfSSL src/internal.c (MaskPadding, MaskMac, TimingPadVerify) and src/tls.c (Hmac_UpdateFinal_CT); Go crypto/tls/conn.go (extractPadding). GnuTLS lib/cipher-cbc.c is pseudo-CT, not straight-line.
- RSA unpadding and TLS premaster check: OpenSSL crypto/rsa/rsa_pk1.c (ossl_rsa_padding_check_PKCS1_type_2, ossl_rsa_padding_check_PKCS1_type_2_TLS), crypto/rsa/rsa_oaep.c (RSA_padding_check_PKCS1_OAEP_mgf1), ssl/statem/statem_srvr.c (tls_process_cke_rsa); mbedTLS library/rsa.c / TF-PSA-Crypto drivers/builtin/src/rsa.c (mbedtls_ct_rsaes_pkcs1_v15_unpadding); wolfSSL wolfcrypt/src/rsa.c (RsaUnPad, RsaUnPad_OAEP, wc_RsaUnPad_ex); NSS lib/freebl/rsapkcs.c (RSA_DecryptBlock, OAEP decode); BoringSSL ssl/handshake_server.cc (premaster path; padding check file unverified); Go crypto/rsa/pkcs1v15.go (DecryptPKCS1v15SessionKey); GnuTLS lib/nettle/pk.c via Nettle rsa_sec_decrypt.
- Base64 of keys: BoringSSL crypto/base64/base64.cc; libsodium src/libsodium/sodium/codecs.c (sodium_base642bin, sodium_hex2bin); mbedTLS library/base64.c (mbedtls_ct_base64_dec_value/enc_char; TF-PSA-Crypto utilities/base64.c in 4.x); wolfSSL wolfcrypt/src/coding.c (Base64_Char2Val_CT). NOT CT: OpenSSL crypto/evp/encode.c, NSS, Nettle, Go encoding/base64.
- Byte<->bignum conversions with fixed width: BoringSSL crypto/fipsmodule/bn/bytes.cc.inc (BN_bin2bn, BN_bn2bin_padded); OpenSSL BN_bn2binpad (crypto/bn/bn_lib.c) as used in crypto/rsa/rsa_ossl.c; mbedTLS mbedtls_mpi_read_binary (CT since 2.7.0); OpenSSL ssl/s3_lib.c ssl_derive with EVP_PKEY_CTX_set_dh_pad(pctx, 1).
- CT comparisons/copies used by all of the above: OpenSSL include/internal/constant_time.h, mbedTLS constant_time.c (mbedtls_ct_memcmp, mbedtls_ct_memcpy_if, mbedtls_ct_memcpy_offset, mbedtls_ct_zeroize_if), wolfSSL misc.c ctMask*, NSS PORT_CT_*.
- NOT straight-line CT anywhere (i.e. the compiler would be placing DIT where no human has): TLS 1.3 inner-plaintext scan (all libraries except opt-in GnuTLS), session ticket parsing (OpenSSL d2i_SSL_SESSION, mbedTLS mbedtls_ssl_session_load), GnuPG PKESK frame parse, Kerberos EncASRepPart decode, SSH length parse, mod_session_crypto.

## Q3. Ranking for a paper case study

Strength = (published attack on the glue) + (vendor fix as CT glue) + (secret stays secret through the glue).

1. RSA PKCS#1 v1.5/OAEP unpadding + TLS premaster version check (and the same glue in Kerberos PKINIT clients and JWE RSA1_5 unwrap). Attacks: Bleichenbacher 1998, Klima-Pokorny-Rosa 2003, Manger 2001, ROBOT 2018 (7+ vendors, CVEs), Stacco 2017 (SGX), Marvin 2023 (10+ CVEs), Windows Kerberos 2024 (CVE-2024-29995), Authlib 2026. Fix: straight-line masked C in every C library and Go. Secret: premaster/session key/CEK, stays secret and is consumed by the next primitive (PRF/KDF/AEAD). Best fit for "decrypt-then-parse"; note Marvin shows the leak can sit at the bignum-to-bytes boundary, so the taint region should start at the modexp output, not at the padding check.
2. TLS CBC padding/MAC glue (Lucky 13 family). Richest lineage (six papers/CVEs 2013-2018), largest CT glue (OpenSSL ~500 LOC; the glue is most of the decryption cost). Weaker on secrecy: the secret is application plaintext; CBC suites are legacy.
3. Private-key file parsing: base64 (Util::Lookup, four CVEs, five CT rewrites) + DER + bignum conversion (Certified Side Channels CVE-2019-1547, mbedTLS 2.21.0; Big Numbers V6; mbedTLS 2.7.0). Secret: long-term private key (strongest secrecy); three utility layers in sequence; OpenSSL/NSS/Go still leak here, so a compiler-placed DIT region would exceed what those vendors ship. Weaker on "consumed by a primitive": the key is stored, not immediately used.
4. Leading-zero stripping / minimal-width bignum conversions (Raccoon, BN_bin2bn/bn2bin, NSS nonce unpadding, Marvin unblinding). Attacks and fixed-width fixes exist; glue is tiny and often now inside the primitive's API.
5. SSH length-field parse (published attack; protocol-level mitigation, no CT glue; plaintext secret).
6. GnuPG/OpenPGP post-decryption parsing (format-oracle literature 2005-2023; fixes are behavioural; PKESK parse still non-CT; session key stays secret).
7. TLS 1.3 padding removal (IETF-acknowledged timing leak, GnuTLS opt-in CT, no attack paper, weak secret).
8. Session tickets (OpenSSL/mbedTLS): ideal shape and strong secret, zero literature.
9. Kerberos symmetric enc-part decode, JWE beyond RSA1_5, Apache mod_session_crypto: thin; say so.

Recommendation: build the third case study on candidate 1 (an application flow that does RSA-decrypt -> unpadding/version check -> premaster into the PRF, or the equivalent key-unwrap -> CEK check -> AEAD), and use candidate 3 (PEM -> base64 -> DER -> BN_bin2bn) as the "no human annotated this" contrast, since OpenSSL/NSS/Go ship it without any CT hardening. Candidate 2 is the right one for the "glue costs more than the primitive" cost argument.

---

## Primary sources consulted (URLs)

- Lucky 13 paper: https://www.isg.rhul.ac.uk/tls/TLStiming.pdf
- Langley, Lucky Thirteen fix write-up: https://www.imperialviolet.org/2013/02/04/luckythirteen.html
- OpenSSL commit "Make CBC decoding constant time." 2acc020b770920657a169bf6be4ff12b254255e6 (GitHub commit search)
- Lucky Microseconds: https://eprint.iacr.org/2015/1129.pdf
- Lucky 13 Strikes Back: https://users.wpi.edu/~teisenbarth/pdf/Lucky13%20AsiaCCS2015.pdf
- Pseudo Constant Time: https://eprint.iacr.org/2018/747.pdf
- mbedTLS advisory 2018-02: https://mbed-tls.readthedocs.io/en/latest/security-advisories/mbedtls-security-advisory-2018-02/
- OpenSSL secadv 2016-05-03 (CVE-2016-2107): https://openssl-library.org/news/secadv/20160503.txt
- Stacco: https://arxiv.org/abs/1707.03473
- SSH paper: https://www.cs.umd.edu/~jkatz/security/downloads/PlaintextRecoverySSH.pdf ; OpenSSH advisory: https://www.openssh.com/txt/cbc.adv ; packet.c: https://raw.githubusercontent.com/openssh/openssh-portable/master/packet.c
- ROBOT: https://robotattack.org/ ; https://eprint.iacr.org/2017/1189.pdf
- Marvin: https://people.redhat.com/~hkario/marvin/ ; https://eprint.iacr.org/2023/1442.pdf ; OpenSSL secadv 2023-02-07: https://openssl-library.org/news/secadv/20230207.txt ; libgcrypt thread: https://www.mail-archive.com/gcrypt-devel@gnupg.org/msg00035.html
- Raccoon: https://raccoon-attack.com/ ; OpenSSL secadv 2020-09-09: https://openssl-library.org/news/secadv/20200909.txt
- Certified Side Channels: https://arxiv.org/abs/1909.01785 ; mbedTLS advisory 2020-07: https://mbed-tls.readthedocs.io/en/latest/security-advisories/mbedtls-security-advisory-2020-07/ ; 2020-04: https://mbed-tls.readthedocs.io/en/latest/security-advisories/mbedtls-security-advisory-2020-04/
- Big Numbers - Big Troubles: https://www.usenix.org/system/files/sec20summer_weiser_prepub_0.pdf
- Deja Vu (NSS): https://arxiv.org/abs/2008.06004
- Util::Lookup: https://arxiv.org/abs/2108.04600 ; OSV CVE-2021-24116 and CVE-2021-24119: https://api.osv.dev/v1/vulns/CVE-2021-24116 , https://api.osv.dev/v1/vulns/CVE-2021-24119
- RFC 8446: https://www.rfc-editor.org/rfc/rfc8446.txt ; IETF TLS thread: https://mailarchive.ietf.org/arch/msg/tls/otmUa4vDrXNIJAGb3m2P7ZfeqF0/ ; GnuTLS manual: https://www.gnutls.org/manual/html_node/On-Record-Padding.html
- Format Oracles on OpenPGP (abstract via HAL): https://hal.science/hal-01154822v1 ; Mister-Zuccherato: https://eprint.iacr.org/2005/033.pdf
- Windows into the Past (Kerberos): https://www.usenix.org/conference/usenixsecurity24/presentation/shagam
- JOSE: Detering et al. ROOTS 2017 (https://dl.acm.org/doi/10.1145/3150376.3150379, abstract only); Authlib GHSA-7432-952r-cw78: https://github.com/authlib/authlib/security/advisories/GHSA-7432-952r-cw78
- Library sources fetched at master (or the branch named): OpenSSL (ssl/record/methods/tls_pad.c, ssl/record/methods/ssl3_cbc.c, ssl/record/methods/tls_common.c, ssl/statem/extensions_srvr.c, ssl/statem/statem_srvr.c, ssl/ssl_asn1.c, ssl/s3_lib.c, crypto/rsa/rsa_pk1.c, crypto/rsa/rsa_oaep.c, crypto/rsa/rsa_ossl.c, crypto/bn/bn_lib.c, crypto/evp/encode.c, providers/implementations/ciphers/cipher_aes_cbc_hmac_sha1_hw.c); BoringSSL (crypto/cipher/tls_cbc.cc, ssl/tls_record.cc, ssl/handshake_server.cc, crypto/base64/base64.cc, crypto/fipsmodule/bn/bytes.cc.inc, crypto/fipsmodule/rsa/rsa_impl.cc.inc); NSS (lib/ssl/ssl3con.c, lib/ssl/tls13con.c, lib/freebl/rsapkcs.c, lib/freebl/hmacct.c); GnuTLS (lib/cipher.c, lib/cipher-cbc.c, lib/nettle/pk.c); mbedTLS mbedtls-3.6 (library/constant_time.c, library/ssl_msg.c, library/rsa.c, library/base64.c, library/ssl_ticket.c, library/ssl_tls.c, ChangeLog) and development (library/ssl_msg.c), TF-PSA-Crypto tree; wolfSSL (src/internal.c, src/tls.c, wolfcrypt/src/rsa.c, wolfcrypt/src/coding.c, wolfcrypt/src/misc.c); libsodium (src/libsodium/sodium/codecs.c); Go (crypto/rsa/pkcs1v15.go, crypto/tls/conn.go, encoding/base64/base64.go); GnuPG (g10/pubkey-enc.c); MIT krb5 (lib/krb5/krb/get_in_tkt.c, decode_kdc.c, lib/crypto/krb listing); Apache httpd (modules/session/mod_session_crypto.c, mod_session.c).
