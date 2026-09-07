# Where the developer's bracket is bad: candidate search, 2026-09-06

Two research passes (Linux kernel and systems software; browsers and applications)
looking for a REAL workload with an existing benchmark or load generator where the
developer's bracket (`msr DIT,#1; sb` at the entry of a crypto function, `msr DIT,#0`
at its exit, Apple's recipe) is bad and fine-grained placement is needed. **[verified]**
means read from the cited source that day; **[unverified]** means from memory or
inference and must be checked before it goes in the paper.

## The condition, stated once

Under the pass's taint from KEYS, decrypted plaintext is key-derived and therefore
secret. Every decrypt-then-process path (inflate after AES in zip, frame parsing
after QUIC decryption, XML after a KeePass unlock) is covered by the pass too, so on
those paths the pass equals a bracket around the whole handler. The pass has no
declassification point (nothing in `TaintAnalysis.cpp` declassifies a store), and
adopting one is a threat-model decision the paper would have to state.

So the bracket is bad only when BOTH hold:

1. **The escaped material is genuinely key material** (a session key parsed out of
   a decrypted ticket, a wrapped key unwrapped and read, a decrypted length that
   drives buffer sizing), so a bracket on the primitive is unsound and the sound
   bracket must enclose the handler; and
2. **the handler also does public work that does not touch the secret** and is
   expensive under DIT on Apple silicon (pointer chasing, interpreter dispatch,
   Huffman decoding; NOT crypto arithmetic, memcpy, or LZMA/zstd).

Shape-1-only candidates (public work under an API a developer would not bracket,
e.g. a QUIC send path whose AEAD is a leaf at the end) are not bracket-bad: the
developer brackets the leaf and ties the pass. Every workload measured so far
(libsodium, mbedTLS AES in libarchive, PHP's builtins, experiments 01/02's bracket
arms) had crypto factored into leaves, and the bracket tied the pass on all of them.

## The kernel is already blanket, with no cost number

Commit `01ab991fc0ee5019aecc4ca461311fc8aa75ece5`, "arm64: Enable data independent
timing (DIT) in the kernel", **Ard Biesheuvel**, authored 2022-11-07, committed by
Will Deacon, shipped in **v6.2** **[verified]** (https://github.com/torvalds/linux/commit/01ab991fc0ee;
LWN https://lwn.net/Articles/921511/ "Linux v6.2 will enable DIT on Arm, but only
in the kernel"). The rationale is precaution, quoted: "Currently, we have no idea
whether or not running privileged code with DIT disabled on a CPU that implements
support for it may result in a side channel that exposes privileged data to
unprivileged user space processes, so let's be cautious and just enable DIT while
running in the kernel if supported by all CPUs." **The message contains no
performance sentence**, and LWN's coverage carries none for Arm **[verified]**.
Mechanism: `cpu_enable_dit()` sets it at boot and `kernel_entry` in `entry.S`
asserts it on every exception entry from EL0 (`alternative_insn nop,
SET_PSTATE_DIT(1), ARM64_HAS_DIT`); user space keeps its own PSTATE.DIT via SPSR.
**No command-line override**: `arch/arm64/kernel/pi/idreg-override.c` has no `dit`
field **[verified]**; disabling is a two-line patch. A Linux guest on Apple silicon
reports `dit` in `/proc/cpuinfo` (Docker Desktop on M1 Max,
https://github.com/docker/for-mac/issues/6111 **[verified]**; backend of that era
**[unverified]**). `LLVM=1` kernel builds are documented and supported
(`Documentation/process/changes.rst`, clang >= 17.0.1 **[verified]**). The arm64
kernel's hot ChaCha20/Poly1305/AES are NEON assembly under `arch/arm64/crypto/` with
generic C fallbacks selectable by Kconfig **[unverified]**.

**Candidate experiment (eval:silicon): the shipped blanket tax.** Stock kernel vs the
two-line patch, in a VM on the M4 (UTM/QEMU-HVF or Virtualization.framework), on
syscall-, filesystem- and network-heavy loads (lmbench, will-it-scale, iperf3 and
redis-benchmark over loopback, a kernel build, fio on tmpfs). Whatever the number, it
is one nobody has: the largest blanket deployment in existence was enabled without
one. Verified kernel decrypt-then-parse paths, all already blanket: kTLS
`tls_sw_recvmsg -> tls_rx_one_record -> tls_decrypt_sw -> tls_decrypt_sg ->
tls_do_decryption`, then `tls_padding_length` reads the content type from plaintext
and `tls_set_sw_offload` installs KeyUpdate material; WireGuard
`wg_packet_decrypt_worker -> decrypt_packet` then `wg_packet_consume_data_done`
parses plaintext and `wg_allowedips_lookup_src` walks a trie on decrypted bytes;
IPsec `esp_input -> esp_input_done2` reads `padlen`/`nexthdr` from the decrypted
trailer **[all verified in source]**.

## Ranked candidates

| # | workload / load generator | crypto (C? force flag) | bracket a developer writes | independent public work under the sound bracket; DIT-sensitivity | crossings per unit | key escapes the primitive? | build | verdict |
|---|---|---|---|---|---|---|---|---|
| 1 | **MIT krb5 KDC, TGS-REQ path.** No shipped load tool; `kinit`/`kvno` loops or a libkrb5 driver; KDC single-threaded (`-w` workers) | `--with-crypto-impl=builtin`: `builtin/aes/{aescrypt.c,aeskey.c,aestab.c}`, `sha2/`, `hmac.c`, `kdf.c`, `pbkdf2.c`; only asm is x86 `iaesx64.s` **[verified]** | the libk5crypto API (`krb5_c_decrypt`, `krb5_c_encrypt`, `krb5_c_verify_checksum`, `krb5_c_make_checksum`, `krb5_c_prf`, ...) | `kdc_process_tgs_req -> kdc_rd_ap_req -> krb5_rd_req_decoded_anyflag` decrypts the ticket with the TGS key, ASN.1-parses the plaintext, the session key inside decrypts the authenticator, `krb5_c_verify_checksum` over the body, reply encrypted with it (`kdc_util.c` **[verified]**). Independent public work: request ASN.1 decode, `kdc_get_server_key`/`get_local_tgt` KDB lookups (DB2/LMDB B-tree), transited/policy checks, reply framing | ~6-10 primitive calls per TGS-REQ | **Yes, textbook**: the decrypted `EncTicketPart` is parsed and its `session` key is then used | autoconf, clang | **Best fit.** Kill risks: function-pointer dispatch (`krb5_keytypes[]`, enc providers) stops propagation at every layer, so each primitive needs a seed; heap keyblocks through many layers may TOP-poison the handler; the KDB may fit in cache and blanket may be ~1% |
| 2 | **strongSwan charon, IKEv2 responder.** Ships `load-tester` plugin (`--enable-load-tester`; `initiators`, `iterations`, `delay`, `proposal`, `fake_kernel`; `modpnull` DH group) https://docs.strongswan.org/docs/latest/plugins/load-tester.html **[verified]** | own C plugins `aes`, `sha2`, `hmac`, `gcm`, `chapoly`, `curve25519` **[names unverified]**; MODP via `gmp` (asm) -> use x25519 or `modpnull` | `aead->decrypt(...)` (vtable) | `encrypted_payload.c`: `decrypt -> decrypt_content -> aead->decrypt -> parse`: `padding.len = plain->ptr[plain->len - 1] + 1`, payload lengths read from plaintext **[verified]**. Independent: IKE_SA lookup by SPI (hashtable), payload object construction, task-manager state machine, linked lists everywhere | 3-5 per IKE message | **Yes**: plaintext padding and lengths drive the parser; derived keys in `keymat` **[unverified]** | autoconf | **Strong second**; same vtable and TOP-poisoning risks; `modpnull` may read as unrepresentative |
| 3 | **OpenSSH sshd `--without-openssl`** (scp/sftp throughput, connection loop) | all C: `chacha20-poly1305@openssh.com` via `chacha.c`/`poly1305.c`, `aes*-ctr` via rijndael, KEX `curve25519-sha256`, `sntrup761x25519-sha512`, `mlkem768x25519-sha256` (`cipher.c`, `cipher-chachapoly.c`, `kex-names.c` **[verified]**) | `cipher_crypt` / `chachapoly_crypt` | `ssh_packet_read_poll2`: `cipher_get_length` decrypts the 4-byte length, `sshbuf_reserve(... aadlen + need ...)`, `cipher_crypt`, `padlen = sshbuf_ptr(...)[4]`, `sshbuf_consume_end(..., padlen)` (`packet.c` **[verified]**). Independent work is event loop, channels, sshbuf: memcpy-class, DIT-free | 2 per packet | **Yes, crisply**: a key-derived length sizes the buffer | trivial | **Best illustration** of the unsound leaf bracket for the paper's prose; **no time win** expected |
| 4 | **Dropbear + libtomcrypt** (scp/sftp) | pure C **[libtomcrypt aarch64 asm absence unverified]** | `decrypt_packet` | `read_packet_init`: `plen = buf_getint(ses.readbuf) + 4` after decrypting the first block, `buf_resize`, `checkmac` with `constant_time_memcmp` (`packet.c` **[verified]**) | 1 per packet | **Yes** | trivial | same as OpenSSH: illustration, negligible prize |
| 5 | **OpenVPN + mbedTLS** (iperf3 through the tunnel) | mbedTLS C | `openvpn_decrypt` | `openvpn_decrypt_aead`: `packet_id_read`, `cipher_ctx_update`, `cipher_ctx_final_check_tag`, `crypto_check_replay` in one function (`crypto.c` **[verified]**); replay bitmap and buffer ops are tiny and DIT-insensitive | 1-2 per packet | weak (packet-id in the clear for AEAD) | easy | **No** |
| 6 | **rnp OpenPGP decrypt** (CLI on large messages) | C++ over Botan; aarch64 AES is C++ intrinsics (`aes_armv8`, disable via `--disable-modules=aes_armv8`) https://botan.randombit.net/handbook/hardware_acceleration.html **[verified]** | `process_pgp_source` / `init_packet_sequence` | `encrypted_src_read_cfb/aead -> compressed_src_read (inflate, BZ2_bzDecompress) -> literal_src_read` (`stream-parse.cpp` **[verified]**); zlib +3%, bzip2 +4-6% | 1 per message | **Yes**: PKESK decryption yields the session key, parsed by glue before the bulk decrypt **[function name unverified]** | cmake | real shape 2, but the decompression win **needs declassification** (bulk plaintext is tainted) |
| 7 | **libarchive bsdtar, WinZip-AES + deflate** (already host-screened 2026-09-05) | mbedTLS backend, already seeded | `zip_read_data_deflate` (**[verified]**) | per 256 KB: `archive_decrypto_aes_ctr_update` + `archive_hmac_sha1_update`, then `inflate()` on the decrypted bytes | ~1 per 256 KB | payload only | easy | **needs declassification**, and CTR is unauthenticated until the end, so "declassify after authentication" is not even satisfiable; today pass = bracket |
| 8 | **H2O/quicly QUIC send path** (`h2load --h3`) | picotls: `openssl` (asm), `minicrypto` (cifra + micro-ecc, pure C, slow) **[verified]**; `mbedtls` backend **[unverified]** | `ptls_aead_encrypt` (a leaf) | `quicly_send -> do_send`: stream scheduling over `quicly_linklist_t`, ACK building, loss detection over `quicly_sentmap_t`, varints, then `commit_send_packet` applies `ptls_aead_encrypt` + header protection (`quicly.c` **[verified]**); pointer chasing, independent of the key | 2 AEAD per ~1.2 KB packet | header-protection mask only (tiny) | moderate; AEAD backend is the blocker | **not bracket-bad**: the developer brackets the leaf. A shape-1 win only if the bracket is placed on `quicly_send`, which nobody would do |
| 9 | **Firefox / NSS** (Raptor tp6 page loads run real TLS through mitmproxy https://wiki.mozilla.org/TestEngineering/Performance/Raptor/Mitmproxy **[verified]**) | **NSS freebl has no hand-written aarch64 asm**: `aes-armv8.c`, `sha1-armv8.c`, `sha256-armv8.c` are C intrinsics in `armv8_c_lib`; only `.s/.S` are ppc64/sparc; HACL* vectorised files are x64-only (`freebl.gyp` **[verified]**) | the record-layer AEAD open **[names unverified]** | HTML/JS is outside the bracket | 1-2 per record | client: tickets opaque | full Firefox with the fork clang: very large | reach demonstration only; unlike Chromium's BoringSSL (all hot aarch64 primitives perlasm, `OPENSSL_NO_ASM` gives unshippable C **[verified]**) |
| 10 | Chromium QUIC (`QuicFramer::ProcessPacket -> ... -> RemoveHeaderProtection -> DecryptPayload -> ProcessIetfFrameData` **[verified]**), Web Push `GCMMessageCryptographer::Decrypt` (tiny shape 2, no benchmark **[verified]**), libolm `olm_group_decrypt` (**[verified]**, deprecated, no load generator), KeePassXC, GnuPG `--disable-asm`, PostgreSQL SCRAM (`src/common/sha2.c` fallback **[verified]**), Redis AUTH, wolfSSL (default build is C; `--enable-armasm` off by default **[verified]**), Tor/WireGuard-go/boringtun/age (asm or not C) | | | | | | | **Out**: asm, no benchmark, handshake-only, or wrong language |

## A shipped bracket: AWS-LC (added 2026-09-06)

AWS-LC ships the developer's bracket as a build option, `-DENABLE_DATA_INDEPENDENT_TIMING=ON`:
`SET_DIT_AUTO_RESET` (save/restore via a cleanup attribute, **no speculation barrier**) at the
entry of 23 FIPS-module files' secret-handling entry points, including the single-block AES
calls **[verified in source]**. The only cost statement is qualitative (`BUILDING.md`: "mostly due
to setting and resetting the DIT flag"; hoisting via `bssl speed -dit` gave "benchmarks that are
close to the release build" on an M2); PRs #1687 and #1783 carry no numbers **[verified]**. Not a
bracket-bad candidate (all crypto, asm hot loops the pass cannot reach) but the first shipped
bracket, measurable on the vendor's own benchmark with PMC counters: experiment 14
(`../14-awslc-shipping-bracket/`, complete 2026-09-06).

## Standard benchmarks and published numbers

- **No standard browser benchmark carries secrets as its measured work.** Speedometer,
  MotionMark, JetStream measure JS/DOM/graphics (JetStream's crypto items are JS
  ports run by the JIT). The one place real TLS runs inside a standard suite is page
  load replay: Firefox Raptor tp6 through mitmproxy **[verified]**, Chromium telemetry
  through Web Page Replay **[unverified]**. WebAuthn, autofill and E2EE web apps run
  crypto in WASM/JS or Rust natives.
- **No published measurement of DIT or Intel DOITM on a real application, and no work
  placing DIT selectively in real software.** Closest: "Let's DOIT" (TCHES 2025,
  https://tches.iacr.org/index.php/TCHES/article/view/12229) costs restricting
  Jasmin crypto to the DOIT instruction list, not the mode on applications
  **[abstract only]**. Go 1.24 shipped the bracket as an API,
  `crypto/subtle.WithDataIndependentTiming` on arm64, with `GODEBUG
  dataindependenttiming=1` for the whole program, off by default
  (https://go.dev/doc/go1.24 **[verified]**). GoFetch's page says DIT disables the DMP
  on M3 but not M1/M2 and gives no overhead number (https://gofetch.fail/
  **[verified]**; the PDF was not retrievable). Intel says DOITM is not intended to
  be always-on **[second-hand]**. Apple's "Enable DIT for constant-time cryptographic
  operations" section exists (anchor **[verified]**) but its text was not retrieved.

## Order of work

1. **Kerberos KDC host screen** (experiment 13): blanket and the crypto-API bracket
   first, the two arms a developer has today. Under 1% blanket kills it.
   **Done 2026-09-06, killed**: blanket -0.8% (TGS-REQ only, 10,000 requests) and +0.7%
   (AS+TGS 1:1), MAD 0.5-0.7%, on the KDC's CPU time; the bracket +0.5 / +0.1 points
   over its NOP twin. The KDC's public work (DB2 lookups, ASN.1 codec) is not
   DIT-sensitive on the M4. The shape is real (the session key is parsed out of the
   decrypted ticket), the prize is not. See `../13-kerberos-kdc/`.
2. **strongSwan** gets the same screen next.
3. **Kernel blanket tax** in a VM: independent of the pass, serves eval:silicon.
4. OpenSSH/Dropbear: prose illustration of the unsound leaf bracket, no measurement
   needed beyond confirming the length flow.
5. rnp / libarchive: only if the paper adopts a declassification point.
