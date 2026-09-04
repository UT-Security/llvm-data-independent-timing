# 10 - mbedTLS session ticket: the secret leaves the primitive

**Status: gates G0 (static) and G1 (gem5 oracle) RUN and PASSED, 2026-09-03. The
headline result is measured: annotating the entire crypto API surface still
leaves the constant-time PSK binder compare running with `PSTATE.DIT` clear on
every resumption, and the pass covers it. See sections 12, 13 and 14.** Written 2026-09-03 from three research memos
(`../../docs/research/decrypt-then-parse-*.md`) and the mbedTLS 3.6 source.
Nothing below is measured; every number is either a citation or an estimate
that the gates in section 8 exist to replace.

**Published artifact (design page):**
https://claude.ai/code/artifact/d1ac0e15-a836-41b6-8b13-0d7c434e457b
Source: `figures/leaves-primitive.html`. When data lands, republish **that URL**
(`Artifact` with `url=...`); publishing the file without it creates a second
artifact instead of updating this one.

---

## 1. The claim

> Experiments 09 and 02 both put the secret inside one library call. 09 is the
> f -> 100% endpoint, where blanket wins; 02 is a flow with a public lane, where
> the pass wins - but a human could have annotated that call in one line, and
> the pass has shown nothing a human could not.
>
> This experiment moves the secret OUT of the primitive. A session ticket is
> decrypted by AES-GCM and then **parsed by ordinary C**: a memcpy of the
> resumption secret into a struct, a copy into the handshake, an HKDF, a
> constant-time compare of the binder. None of that is a crypto primitive, none
> of it is annotated in any shipping library, and all of it computes on the key.
>
> The claim is two-sided. **Coverage:** annotating the primitive - what a
> developer writes first, and what CryptoMPK's policy does by design - leaves
> every glue operation running with `PSTATE.DIT` clear, and the gem5 shadow-taint
> oracle counts exactly how many. The pass covers them, because it follows the
> secret out of the primitive's output buffer. **Cost:** it does so at the cost
> of the glue alone, not at the cost of bracketing the whole resumption path
> (what a careful human does, and what Apple's scope guards and AWS-LC's
> caller-level hoisting do), and not at blanket's.

The experiment exists to measure the gap between those three placements on a
flow where the secret's lifetime is longer than the primitive's.

## 2. Why this shape, and why it has backing

**The shape.** "Decrypt-then-parse": key-class material comes out of a
primitive and is handled by non-crypto code before it enters the next primitive
or dies. Three research memos (literature, libraries, deployments) established:

- **It is the shape of a 25-year attack lineage.** Bleichenbacher 1998 -> ROBOT
  2018 (seven vendors, CVEs) -> Marvin 2023 (ten-plus CVEs, including OpenSSL
  CVE-2022-4304 and mbedTLS CVE-2024-23170) -> Kerberos PKINIT 2024
  (CVE-2024-29995: "RSA decryption is done securely inside the smartcard, a
  non-constant time unpadding code runs on the client's CPU"). Marvin's own
  decomposition is the taxonomy: modexp / padding check and secret extraction /
  secret use and error handling. The leak keeps moving one call up, out of the
  primitive and into the glue.
- **Every C library now ships that glue as straight-line constant-time C** -
  OpenSSL `rsa_pk1.c` and `tls_pad.c`, mbedTLS `constant_time.c`, wolfSSL
  `RsaUnPad` and `TimingPadVerify`, NSS `RSA_DecryptBlock`. Straight-line
  constant-time code is precisely what a value predictor still leaks from
  (FLOP, USENIX Sec'25) and what `PSTATE.DIT` exists to protect. So the
  industry has already located the glue for us; it just cannot protect it
  against this channel.
- **The IETF concedes the boundary.** RFC 8446 Appendix E.3: "even a
  constant-time padding removal function will likely feed the content into
  data-dependent functions." That sentence is the reason a taint-tracking
  compiler is the right tool and a hand annotation is not.
- **Session tickets are the deployed, frequent, modern instance.** About 40% of
  HTTPS connections at Cloudflare are resumptions (2017); 9-12% of TLS 1.3
  connections carried a PSK in 2019 passive data; 77.6% of top-1M sites issue
  tickets; OpenSSL issues two per handshake by default. The decrypted ticket
  holds the resumption secret, and no library parses it in constant time - s2n
  comes closest, comparing the cipher-suite bytes it read out of the decrypted
  ticket with `s2n_constant_time_equals`. **There is no published attack on
  ticket parsing.** That is stated, not hidden: the literature lives on the RSA
  path, which this rig also runs (section 4, flow B).
- **Prior systems did not measure it.** ct-verif and Binsec/Rel verified one
  glue function (MEE-CBC) without a cost; CryptoMPK's "crypto-aware" taint
  declassifies every cipher output by policy, which erases this flow entirely;
  ConTExT, CIO and DECLASSIFLOW never touch protocol glue.

Details, function names and URLs: `docs/research/decrypt-then-parse-literature.md`
(attacks and fixes), `-libraries.md` (where the glue sits in 13 libraries, with
function-pointer hazards), `-applications.md` (deployment rates, RFC-defined
declassification points, prior art).

## 3. Where it sits among the case studies

| # | secret's lifetime | human's best annotation | who wins |
|---|---|---|---|
| 09 | the whole program | nothing to annotate | blanket, by 12-124% |
| 02 | one library call inside a flow | one line at the call | the pass, by 21% |
| **10** | **out of the primitive and into glue** | **annotate the primitive (unsound) or bracket the phase (blanket in a region)** | **to be measured: the pass at the glue's cost with the bracket's coverage** |

03 already put public and secret work in one function, but sequentially at call
granularity; its region boundary is still a call boundary. Here the secret is
carried through stores and loads across four functions in two translation
units, which is the structure the taint analysis was built for.

## 4. The workload: mbedTLS 3.6, two flows in one rig

mbedTLS because the tree already builds and instruments it (03, 05's mbedTLS
handshake rig in gem5-DIT), it is pure C with no hand-written assembly on these
paths, and both flows below run in one process as two contexts over memory
buffers - the `benchmarks/tls_handshake` pattern, which works in gem5 SE mode.

### Flow A (primary): TLS 1.3 resumption via session tickets

The server side of a resumed handshake. Function names from mbedTLS 3.6 head
`d6664cea` (`library/`); line numbers in 3.6.2 will differ slightly.

```
ssl_tls13_server.c  ssl_tls13_offered_psks_check_identity_match_ticket
  -> conf->f_ticket_parse(...)                  FUNCTION POINTER, installed by the app
     ssl_ticket.c    mbedtls_ssl_ticket_parse      key select by public name, then
       -> psa_aead_decrypt(...)                 THE PRIMITIVE: AES-GCM, in place
       -> mbedtls_ssl_session_load(session, ticket, clear_len)
          ssl_tls.c  ssl_session_load             header memcmp, version, endpoint, ciphersuite
            -> ssl_tls13_session_load             ticket_age_add, flags, resumption_key_len,
                                                  memcpy(session->resumption_key, p, len)   <- THE SECRET
                                                  then early-data size, creation time, ALPN
  -> version / lifetime / age checks             public metadata (declassified, section 6)
  -> mbedtls_ssl_set_hs_psk(ssl, session->resumption_key, len)     copy into the handshake
  -> mbedtls_ssl_tls13_create_psk_binder(...)   HKDF over the PSK (crypto, direct via PSA)
  -> mbedtls_ct_memcmp(server_computed_binder, binder, hash_len)   glue on a secret-derived value
  -> key schedule: early secret from the PSK    crypto
```

What a developer annotates first: the ticket key (the input to
`psa_aead_decrypt`). What that misses: everything after the decrypt returns,
which is four functions in three translation units. Region size per resumption:
session load about 200-400 cycles, PSA import and HKDF binder about 2-4 us,
compare and key schedule after that. The glue is small against the crypto
around it; the point is that it exists and is unannotated, not that it is
expensive.

### Flow B (secondary, the literature anchor): TLS 1.2 RSA key exchange

```
ssl_tls12_server.c  ssl_parse_client_key_exchange -> ssl_parse_encrypted_pms
  -> ssl_decrypt_encrypted_pms -> mbedtls_pk_decrypt        FUNCTION POINTER (pk_info->decrypt_func)
     rsa.c   mbedtls_rsa_rsaes_pkcs1_v15_decrypt
       -> mbedtls_rsa_private                               THE PRIMITIVE: CRT modexp
       -> mbedtls_ct_rsaes_pkcs1_v15_unpadding             CT scan of ~254 bytes, ct_zeroize_if,
                                                            ct_memmove_left (O(n^2), ~10k instr)
  back in ssl_parse_encrypted_pms:                          CT version check (ct_uint_ne x4),
                                                            f_rng(fake_pms) always, ct_memcpy_if(diff, ...)
  -> mbedtls_ssl_derive_keys -> ssl_compute_master -> handshake->tls_prf(premaster, 48, ...)   FP, then direct HMAC
```

This is the Bleichenbacher / ROBOT / Marvin glue, verbatim, in the library the
attack chain named most recently. Region: unpadding about 1.5-2 us, premaster
glue 0.5-1 us, PRF 2-3 us. Annotating `mbedtls_rsa_private` misses the
unpadding; annotating `mbedtls_pk_decrypt` misses the version check and the
fake-premaster select. RSA key exchange is deprecated (99.3% of top-1M sites
avoid it), which is why it is the secondary flow and not the headline.

### Considered and not chosen

- **TLS 1.2 CBC mac-then-encrypt records (Lucky 13 glue).** The largest
  constant-time glue in any library (50-100 us per record, all direct calls),
  but its secret is application plaintext that goes to the application, the
  suite is legacy, encrypt-then-MAC is on by default, and the HMAC overlaps 03's
  secret lane. Right for a "glue costs more than the primitive" cost argument
  if one is ever needed; wrong for this claim.
- **Encrypted private-key loading (PKCS#8 PBES2).** Strongest attack literature
  (Certified Side Channels, Util::Lookup) and the cleanest boundary, but it runs
  once per process and the tainted code is bignum arithmetic, so it reads as 09
  again.
- **TLS 1.3 inner-plaintext padding strip.** A 1-16 iteration loop on secret
  bytes whose output byte drives the record dispatcher; too small, and its
  output is application data.
- **Kerberos, cookies, Tor cells, WireGuard, IKEv2, OpenSSH, HTTP Basic.**
  Either the decrypted material is not a key, or the parse is behind
  function-pointer crypto backends the pass cannot bridge, or (cookies) the
  plaintext floods the application - the 13,222-switch regime.

## 5. What is public and what is secret

| lane | code | why |
|---|---|---|
| **secret, primitive** | `psa_aead_decrypt` of the ticket (A); `mbedtls_rsa_private` (B) | operates on the ticket key / the private key |
| **secret, glue** | `ssl_tls13_session_load`, `mbedtls_ssl_set_hs_psk`, the binder `mbedtls_ct_memcmp`, the PSK inputs to the key schedule (A); `mbedtls_ct_rsaes_pkcs1_v15_unpadding`, the version check and `mbedtls_ct_memcpy_if` in `ssl_parse_encrypted_pms`, the premaster into `tls_prf` (B) | computes on the secret after it left the primitive |
| **secret, downstream crypto** | HKDF / PRF, traffic-key derivation, record AEAD | key-derived; covered by every arm that covers the glue |
| **public** | ClientHello and extension parsing, ticket key selection by name, transcript hashing of public messages, ticket age arithmetic, record framing, and the application's handling of decrypted request bytes (declassified at the record boundary, section 6) | never reads the key |

The public lane of a TLS server is not known to be DIT-sensitive. Section 8
gate G3 measures it before any crossover is attempted, and section 9 says what
happens if it is not.

## 6. Declassification: what the protocol makes public, and where

Taint is transitive. Without declassification the decrypted ticket taints the
whole server, since every traffic key descends from the PSK. The pass now has
parameter-scoped declassification (`function,argno,declassify`, 2026-09-02) and
the oracle has `m5_taint_declassify`; both are used at protocol-defined points
only, and the count of assertions ships with the result. Per the tip commit's
wording, the resulting zero is **sound modulo these assertions**, and is
labelled so.

| point | what becomes public | authority |
|---|---|---|
| after `ssl_tls13_session_load` | every session field except `resumption_key` (ticket_age_add, flags, lifetime, creation time, ALPN, ciphersuite) | RFC 5077 section 4: only `master_secret` is key material; RFC 8446 section 4.6.1 |
| binder check | the one-bit result of `mbedtls_ct_memcmp` | RFC 8446 section 4.2.11.2 |
| ticket age / lifetime check | the one-bit accept result | RFC 8446 section 4.2.10 |
| `mbedtls_ssl_read` return | the application plaintext of a record | RFC 8446 Appendix E.3: the record layer's constant time ends at the application boundary |
| transport send | ciphertext on the wire | public by definition |
| (B) after key parse | the RSA public modulus and exponent | public key |

Each row is one `m5_taint_declassify` in the oracle harness and, where the
static pass needs it, one seed line. The design deliberately declassifies
per FIELD, not per object: 08 showed that object-granularity declassification
strips protection from a secret that shares a buffer with a public value
(libhydrogen's `csig`), and the ticket plaintext is exactly such a buffer.

## 7. Arms, seeds, and what the pass can and cannot see

### The three human placements and the compiler's

| arm | what it is | coverage expected | cost expected |
|---|---|---|---|
| `nodit` | round-trip baseline, `-ftaint-harden=<empty>` | none | 1.00 |
| `blanket` | DIT set at startup, same binary as `nodit` | everything | all public work under DIT |
| `bracket` | DIT set around the server handshake step that consumes the ticket (A) or the ClientKeyExchange (B), cleared after: what a careful human writes with a scope guard | everything on the path, plus the public handshake work inside the bracket | the bracketed public work under DIT |
| `prim` | the pass, seeded at the primitive only (ticket key / private key) | the primitive and whatever its own translation unit reaches; **the glue after the decrypt returns is expected uncovered** | lowest, and unsound |
| `pass` | the pass, seeds grown to oracle parity by the 07 loop, per-TU build | oracle parity | the glue plus its switches |
| ~~`pass-wl`~~ | `pass` seeds on whole-library bitcode. **Dead at G0** (section 12): with ANY seed set, 83% of the library's instructions run under DIT. It is blanket with 8,800 switches, not an arm. |  |  |
| `func` | `pass` with `-taint-dit-placement=function` | oracle parity | whole functions |
| `nop` | `pass` with every switch as `HINT #0` | none | layout control |

`off`, `blanket` and `bracket` share one binary and take the mode from argv, so
no codegen difference can contaminate the comparison that matters most.

### What the analysis can see, stated before it is measured

Two mechanics of the pass decide the seed sets, and they are the reason this
experiment is informative rather than a foregone conclusion:

1. **Input flows are reported, output flows are not.** When a tainted argument
   reaches a callee in another translation unit, the pass keeps DIT on across
   the call ("the callee inherits protection") and `-taint-info-loss-report`
   names the seed that would narrow it. When a callee in another TU takes
   PUBLIC inputs and writes a SECRET through an out-pointer - which is what
   `f_ticket_parse` does for its caller in `ssl_tls13_server.c` - nothing
   tainted was passed, so nothing is reported and the caller's later loads of
   `session->resumption_key` are public to the analysis. The consumer-side glue
   (`set_hs_psk`, the binder, the compare) is invisible to `prim` **and** to the
   report. Only the oracle finds it. This is the 08 finding ("our own
   information-loss report says nothing about the gap") on a protocol flow,
   and the experiment measures how many secret operations sit on such flows.
2. **In-TU output flows are handled bluntly.** Within one TU (or the whole
   library, in `pass-wl`) a callee that writes a secret through a pointer
   argument makes the caller treat ALL memory as secret afterwards
   (`modset-argptr`, "applied bluntly, whole memory"). Sound, and the origin of
   over-protection: after `mbedtls_ssl_ticket_parse` returns, the remainder of
   its caller may run entirely protected. `-taint-clobber-report` pinpoints
   these sites. The over-protection count (section 8) is where `pass-wl` may
   lose to `pass`, and that is a precision result worth having either way.

Seeds, first round, in the pass's `function,argindex,pointee` form:

```
# A, prim: the developer's first annotation - the ticket key at the primitive
mbedtls_ssl_ticket_parse,2,pointee        # buf: decrypted in place; the whole ticket
# A, pass: what the oracle will demand (expected; the 07 loop decides)
mbedtls_ssl_session_load,1,pointee
mbedtls_ssl_set_hs_psk,1,pointee
mbedtls_ssl_tls13_create_psk_binder,2,pointee
mbedtls_ct_memcmp,0,pointee
ssl_tls13_session_copy_ticket,1,pointee   # found by READING the consumer, not by any report (G0)
# B, prim
mbedtls_rsa_rsaes_pkcs1_v15_decrypt,0,pointee
# B, pass
ssl_decrypt_encrypted_pms,3,pointee        # bridges pk_info->decrypt_func
ssl_compute_master,0,pointee               # bridges handshake->tls_prf
```

The seed-line count needed to reach parity, per-TU versus whole-library, is
itself reported: it is the developer cost 07 measured, on a flow where the
report cannot help with half of it.

### Oracle seeding

`m5_taint_seed` on the ticket key bytes before `mbedtls_ssl_ticket_rotate`
installs them (A), and on the PEM key bytes before `mbedtls_pk_parse_key` (B,
then declassify N and E). Seeding the key rather than the plaintext is what
makes "the secret leaves the primitive" a measured fact: the plaintext is
tainted only because the oracle propagated it through the decrypt.
`m5_taint_report` is taken after the resumed handshake completes and before the
first application record, as 04 does before the signature is consumed.

## 8. What is measured, and the gates in the order that fails fastest

Every gem5 number is exact and per event; every silicon number ships with the
controls in `evaluation-framework.md` section 5.

| quantity | instrument | per arm |
|---|---|---|
| secret operations executed with DIT clear, by function | gem5 oracle | the coverage axis; `prim` is expected non-zero, the rest zero modulo section 6 |
| instructions committed with DIT set that carry no secret operand | gem5 oracle | the over-protection axis; where `bracket` and `blanket` pay, and where `pass-wl`'s blunt mod-set shows |
| `MSR DIT` executed per resumption (committed, `commit.ditWrites`) | gem5 | toggles per unit work |
| cycles per resumed handshake, serialising and renamed switch models | gem5 | cost per event |
| the five quantities: `f_secret`, `C_public`, `C_secret`, work per region, toggles per unit work | both | as every experiment |
| time per connection at resumption rate r and records per connection K | Apple M5, kperf | the crossover, if G3 passes |

**G0 - static, minutes.** Build both flows' arms. `prim` must show zero
switches in `ssl_tls13_session_load` and `ssl_parse_encrypted_pms` (the human's
gap is real); `pass` must show switches there. Read the info-loss and clobber
reports. Whole-library compile must finish; `quickjs.c` took 733 s for 940
functions after the 2026-08-10 fix, and mbedTLS's library is several times
that. If `pass-wl` does not build in an hour, it is dropped and the per-TU seed
count is the result.

**G1 - oracle, gem5, hours.** `prim` under-taint in the glue must be non-zero.
If it is zero the glue holds no secret operations and the experiment is
vacuous: stop and say so. `pass`, `bracket`, `blanket` must agree at the
declassification residue, and that residue must be attributable line by line
to section 6.

**G2 - over-protection, gem5.** Report, do not gate: public instructions under
DIT per resumption for each arm. This is where the compiler's precision is
judged against the bracket.

**G3 - headroom, silicon.** Blanket DIT on the r = 0 connection stream (full
handshakes plus K records, no ticket work). If under ~1%, the TLS server's own
public lane has no prize; the crossover is not run on it (section 9). This is
the framework's Q1 and it kills more candidates than anything else.

**G4 - the standing controls.** Identical checksums across arms (fixed-seed
DRBG, as the handshake rig already does); `simInsts` identical across switch
models; zero `ditSuppressed` in `nodit`; the NOP arm carries zero `msr DIT`
and matches its twin's size; equal-length `argv[0]` paths averaged over eight
lengths; rotated arm order and a duplicate baseline on silicon; an exclusive
machine; `MBEDTLS_HAVE_TIME` off in the gem5 build so no clock enters the
instruction stream (ticket lifetime checks compile out; they are public work).

## 9. If the public lane has no headroom

Then blanket is nearly free on the TLS server's own code, no placement can
beat free on time, and the honest result is the coverage-and-cost-per-event
table from gem5: `prim` is cheapest and unsound; `pass` reaches the oracle at
X cycles per resumption; `bracket` reaches it at Y with Z public instructions
swept in. That is still the claim in section 1, minus a crossover figure.

The crossover then moves to a composite: the same rig with the SQLite host from
`utils/dit_host_screening/xover/` as the application behind the server
(measured +3.48% always-on headroom on M5), resumptions per N queries as the
knob. It is a synthetic pairing in the same sense 02 and 03 are, and would be
labelled as such.

## 10. Known weaknesses, stated now

- **The glue is small.** Hundreds of cycles of parsing next to microseconds of
  AEAD and HKDF. The cost result will be bounded, not dramatic; the experiment
  is about where protection lands, not how much it costs.
- **No attack on ticket parsing exists.** Flow B carries the literature; flow
  A carries the deployment. The paper must not imply otherwise.
- **The whole ticket plaintext is tainted by the seed**, public fields
  included, and the public fields must be declassified per section 6. Each
  declassification is an assertion the oracle cannot check.
- **Function pointers.** `f_ticket_parse`, `pk_info->decrypt_func`, `f_rng`,
  `tls_prf`. The pass cannot hoist across them; seeds bridge them. In the
  default 3.6 configuration the cipher layer is also function-pointer
  dispatched; build with `MBEDTLS_USE_PSA_CRYPTO` so the ticket AEAD and all of
  TLS 1.3 dispatch through PSA's switch tables (direct calls). `mbedtls_md_*`
  is direct already.
- **The blunt mod-set** may make `pass-wl` over-protect the rest of the
  handshake after the parse. If so, that is the result, and it names the next
  compiler change (field-sensitive mod-sets).
- **Assembly footnote.** `MBEDTLS_HAVE_ASM` is on by default and gives bignum
  an inline-asm multiply-accumulate on aarch64, inside `mbedtls_rsa_private`
  (flow B's primitive, not its glue). Undefine it for the instrumented build so
  the pass sees the primitive too, and say so.
- **mbedTLS source is not on the Linux host.** It is at
  `~/Documents/mbedtls-3.6.2` on the Mac; the gem5 cross-build reads
  `MBEDTLS_SRC`.

## 11. Work plan

1. **Rig**, in gem5-DIT under `benchmarks/tls_resume/`: `tls_resume_gem5.c` from
   `tls_handshake_gem5.c`, adding `mbedtls_ssl_ticket_setup` and
   `mbedtls_ssl_conf_session_tickets_cb` on the server, ticket capture with
   `mbedtls_ssl_get_session` and `mbedtls_ssl_set_session` on the client, and
   flags `--flow ticket|rsa`, `--resume-every k`, `--records K`, `--mode
   off|blanket|bracket`, plus the `m5_taint_seed` / `m5_taint_declassify` /
   `m5_taint_report` calls of section 7. Flow B forces TLS 1.2 and
   `TLS-RSA-WITH-AES-128-GCM-SHA256`. One ROI per run: the resumed handshakes.
2. **Build script** from `tls_handshake/build.sh`: the eight arms of section 7,
   `MBEDTLS_USE_PSA_CRYPTO` on, `MBEDTLS_HAVE_TIME` and `MBEDTLS_HAVE_ASM` off,
   the stale-object exclusion 03 found, and a whole-library bitcode variant.
3. **G0**, then the 07 annotation loop on both flows until the oracle is at the
   declassification residue; record seed counts per round.
4. **gem5 sweep**: eight arms x two flows x two switch models x eight `argv[0]`
   lengths, 100 resumptions per run. Each resumed handshake is a few million
   instructions, so this is hours, not days.
5. **Silicon**: G3 first; the crossover only if it passes, else section 9.
6. Data to `data/`, figure source to `figures/`, this file rewritten from
   "design" to "result", the row in `../README.md` updated.

## 12. Gate G0, run 2026-09-03: the static probe

`utils/dit_host_screening/decrypt_parse/probe_static.sh`, on this tree's clang
(built at `c9b8dd0c`, 40 commits behind the tip; none of the missing commits
change what is measured here except that `-taint-frame-addr-args` is absent),
mbedTLS 3.6.2 configured as section 10 says, aarch64 Linux host. Each variant
of the whole library builds in **7 s**. Raw numbers: `data/static_probe.csv`
(per function, from `-taint-dit-precision-report`: `need` = instructions the
analysis says must run under DIT, `underdit` = instructions that do,
`collateral` = the difference) and `data/whole_library_probe.csv`.

**The human's gap is real, and larger than section 4 guessed.** With the
primitive-only seeds (the AEAD's key inside `gcm.c`/`aes.c`, the RSA private
operation's context), `libmbedtls.a` carries **zero** `MSR DIT` and every glue
function on both paths has `need = 0`. That includes
`mbedtls_ct_rsaes_pkcs1_v15_unpadding`, in the same file as the primitive: the
constant-time Bleichenbacher glue that the industry wrote for exactly this
boundary runs with DIT clear when the primitive alone is annotated.

**The pass reaches the glue once it is told where the secret leaves.** Static
`need` instructions under the glue seeds, per-TU build:

| function | need | under DIT | collateral | switches |
|---|---|---|---|---|
| `mbedtls_ssl_ticket_parse` | 23 | 67 | 44 | 6 |
| `ssl_session_load` (loader inlined) | 134 | 269 | 135 | 14 |
| `mbedtls_ssl_session_load` | 13 | 30 | 17 | 8 |
| `mbedtls_ssl_set_hs_psk` | 13 | 29 | 16 | 5 |
| `setup_psa_key_derivation` | 26 | 67 | 41 | 10 |
| `mbedtls_ssl_tls13_create_psk_binder` | 12 | 32 | 20 | 9 |
| `mbedtls_ssl_tls13_evolve_secret` (reached in-TU) | 15 | 45 | 30 | 10 |
| `mbedtls_ct_memcmp` | 5 | 10 | 5 | 3 |
| **flow A total** | **241** | **549** | **308** | **65** |
| `mbedtls_rsa_rsaes_pkcs1_v15_decrypt` (the unpadding) | 136 | 175 | 39 | 7 |
| `ssl_decrypt_encrypted_pms` | 6 | 15 | 9 | 3 |
| `ssl_parse_encrypted_pms` (version check, fake PMS) | 0 | 0 | 0 | 0 |
| `ssl_compute_master` | 56 | 180 | 124 | 18 |
| **flow B total** | **198** | **370** | **172** | **28** |

Static counts; the unpadding's O(n^2) `mbedtls_ct_memmove_left` alone is
~10,000 dynamic instructions per decrypt. Precision inside the glue functions
is **30-78%**: the pass sweeps in the public metadata parsing next to the
secret copy. That is the over-protection G2 will price against the bracket.

**Two caller-side blind spots, both confirmed, both closable, neither by the
report.**

1. **The mod-set gate.** `ssl_parse_encrypted_pms` holds the premaster in a
   local buffer that an in-TU callee fills through an external call. The
   clobber report lists the callee's mod-set as TOP at that call, but the
   pass applies it only where a secret is passed IN, and here none is. With
   `-mllvm -taint-no-modset-gate` the version check gets `need = 83`, 6
   switches, and 7 more switches spread into `mbedtls_ssl_handshake_server_step`.
   The gate is a precision feature (it took secp256k1 verify from +51% to
   +0.67%); on this flow it hides the Marvin glue.
2. **The function pointer.** `f_ticket_parse` is indirect, so
   `ssl_tls13_server.c` sees nothing return from it, gate or no gate. Its
   `ssl_tls13_session_copy_ticket` copies the resumption key into the
   negotiation state with its own memcpy: an uncovered secret copy that no
   report names, found by reading the file. One seed line covers it
   (`need = 8`, 5 switches).

So the seed set that reaches every named glue site is **9 lines**, and the
07 annotation loop finds **7** of them: the info-loss report names the
cross-TU input flows (`mbedtls_ssl_session_load`, `psa_import_key`,
`mbedtls_pk_decrypt`, ...) and is silent on both output flows. That is the
08 finding, reproduced on a protocol.

**Whole-library mode is dead.** Linking all 108 translation units into one
module and running the pass takes **17 s** (1,618 functions), so compile time
was never the risk. Precision is: **8,835 switches in 1,067 functions and
83.3% of all 117,964 library instructions under DIT** - with the full seed
set, with the primitive-only seeds, and with just the five flow-A glue seeds
(`data/whole_library_probe.csv`). The spread does not depend on what is
seeded, so it is structural: once a caller's memory is TOP, every argument it
passes is secret, every callee's mod-set fires, and TOP is absorbing across
the call graph. `mbedtls_ssl_handshake_server_step` alone carries 306
switches. The `pass-wl` arm is removed; the per-TU build with hand seeds is
the pass. Whether a field-sensitive or gated whole-program mode can be built
is a compiler question this experiment now motivates rather than answers.

**Verdict.** The probe passed its own kill criteria: the glue is not a few
dozen memcpy instructions, the pass reaches it, and the human's annotation
misses all of it. What it did not pass is the hope that the pass finds the
glue on its own: two of nine seeds need a human or the oracle. The next step
is unchanged from section 11: add ticket resumption to the gem5 handshake rig
and run one oracle pass of `prim` for the dynamic count.

## 13. Gate G1, first attempt: the default key exchange buries the signal

Rig: `gem5-DIT/benchmarks/tls_resume/`, a client and server in one process over
memory pipes (the `tls_handshake` pattern), one full TLS 1.3 handshake to obtain
a ticket, then N resumed handshakes as the ROI. The oracle seed is the server's
ticket key; declassification is applied per section 6. gem5 master tip
(NeoverseV2 FDP, `--eves --dmp --comp-simp`). Data: `data/oracle_dhe.csv`,
`data/oracle_raw/`.

Build config matters and two settings were found the hard way:
- **`MBEDTLS_DEBUG_C` off.** With it on, the library formats secret-derived
  values into debug strings even with no callback installed:
  `mbedtls_debug_print_msg` alone was 893 under-taint ops per full handshake.
  Production ships without it; so does the rig.
- **`MBEDTLS_PSA_CRYPTO_EXTERNAL_RNG` on,** so PSA's RNG is the harness's
  fixed-seed DRBG and every arm does byte-identical work (the checksum gate).

**The baselines are clean and behave.** One full handshake, no resumption,
debug off:

| arm | secret ops with DIT clear |
|---|---|
| `null` (unhardened control) | 6,812 |
| `blanket` | 0 |
| `prim` (primitive seeds) | 714 |
| `pass` / `api` | 343 |

The control fires (thousands of secret ops run clear when nothing is hardened),
blanket covers all of them, and the seeded arms sit between. The oracle works.

**The full resumed handshake does not isolate the glue.** Four resumptions,
`psk_dhe_ke` (the deployed default, forward-secret):

| arm | under-taint (DIT clear) | wasted (DIT set, no secret) |
|---|---|---|
| `blanket` | **14** | 71,114,506 |
| `bracket` (human scope guard) | 35,497,519 | 16,703,814 |
| `pass` (API + 6 glue seeds) | 70,285,439 | **1,813,270** |
| `api` (API surface only) | 70,290,717 | 1,799,028 |
| `func` (pass, function placement) | 70,262,692 | 1,886,144 |

The under-taint column is useless here and the reason is mechanical, not a bug
in the seeds. Symbolized, **97% of those 70 million ops are `mbedtls_mpi_*`
bignum arithmetic** (`data/oracle_raw/pass_n4_by_function.txt`): the X25519
ECDHE that a `psk_dhe_ke` resumption performs every time. A single full
handshake shows 6,812 tainted ops; four resumptions show 70 million. That is
**superlinear accumulation**, and a tainted-store trace
(`benchmarks/tls_resume/trace_analyze.py`) shows why: the ticket key's taint
reaches the AES-GCM decrypt, then the PSA **DRBG state**, and from there the
ephemeral scalars the ECDHE consumes. Whether that spread is a real
over-approximation the pass would have to protect, or an oracle artifact (taint
is not cleared when a buffer is freed and reused, and the RNG-boundary
declassification the design did not specify), is not settled here. Either way the
ticket-parse glue - hundreds of ops - is buried under tens of millions of bignum.

Isolating it with pure-PSK resumption (`psk_ke`, no ECDHE) was attempted and the
handshake fails a parameter check (`-0x6600`) that was not debugged; that is the
first task on resuming, alongside declassifying the RNG boundary or teaching the
oracle to clear taint on free.

**One dynamic result is clean and it is the cost half of the claim.** The wasted
-coverage column - instructions run under DIT that carry no secret - is not
affected by the bignum spread, because it counts over-protection, not
under-protection. Per resumed handshake:

| placement | wasted ops / resumption | vs the pass |
|---|---|---|
| `blanket` | ~17,800,000 | 39x |
| `bracket` | ~4,180,000 | 9x |
| `pass` | ~453,000 | 1x |

**The compiler is about 9x more precise than a careful human's scope guard and
about 39x more precise than blanket**, measured, on the deployed key-exchange
mode. That is the "at the glue's cost, not the bracket's" half of section 1,
and it holds independently of the coverage measurement being blocked.

**Verdict on this attempt.** The precision column is real and the static coverage
result stands, but the dynamic *coverage* number is not readable here. Section 14
isolates it.

## 14. Gate G1, corrected: pure-PSK resumption, and the number the experiment exists for

> **SCOPE WARNING, added 2026-09-03 after section 15 was measured.** Every
> coverage figure in this section describes `psk_ke`, pure-PSK resumption with
> **no ECDHE**. That is not what deployments run. Under the default
> `psk_dhe_ke` each resumption performs a fresh X25519 exchange whose ephemeral
> private scalar is a *second* secret, unrelated to the ticket key, and the
> seed sets used below name no key-agreement, ECDH or bignum entry point at
> all. Measured under the deployed configuration, the `pass` arm below protects
> **3.42% of secret operations per resumption, not 98%** (section 15). The
> binder-compare result (547 -> 0) is unaffected, because that path is identical
> in both configurations. Nothing else in this section should be quoted without
> the `psk_ke` qualifier.

The ECDHE is not the subject. It is a fresh key exchange whose scalar comes from
the RNG, not from the ticket, and removing it isolates the flow this experiment
is about. **The isolation is a client-side, resumption-only restriction:** the
initial full handshake has no PSK yet and must use the certificate/ephemeral
exchange to establish and issue a ticket, so restricting it too makes it fail
with nothing to offer (that was the `-0x6600` "ClientHello misses mandatory
extensions" seen on the first try - the server had no PSK *and* no key share to
work with). The server keeps all modes; only the client, and only for the
resumed handshakes, is pinned to `psk_ke`. Run with `--kex psk`; the structural
check is that the server sends 370 bytes instead of 410, the key share gone.

Data: `data/oracle_psk.csv`, breakdowns in `data/oracle_raw/psk_*`.

### The headline: what a human annotation cannot reach

`mbedtls_ct_memcmp` is the constant-time comparison that decides whether a
resumption is accepted. It computes on the secret-derived PSK binder. Operations
of it executing with `PSTATE.DIT` **clear**, per resumption:

| annotation | binder-compare ops running unprotected |
|---|---|
| none (unhardened control) | **871** |
| the primitives (AES, GCM, SHA cores) | **547** |
| **the entire crypto API surface** (23 PSA and builtin entry points) | **547** |
| the human's scope guard around the server handshake | 355 |
| **the pass** (API + 6 glue seeds) | **0** |
| function placement | **0** |
| blanket | 0 |

That **547 is exact at N=1, N=2 and N=4** - a per-resumption invariant, not an
average, and therefore not an artifact of the run length (see the limits below).

**Annotating every entry point of the crypto library does not protect the
comparison that gates the handshake.** It is not a crypto function, it takes no
key as an argument, and no amount of naming the library's API reaches it. The
taint pass reaches it because it follows the secret out of the AEAD's output
buffer, through the session loader and the PSK copy, into the compare. That is
"the secret leaves the primitive", measured, on a deployed protocol flow.

The human's scope guard is not zero either (355), and the reason is instructive:
it wraps the server's handshake step, and the client computes its own binder
outside that scope. A guard protects exactly what it brackets.

### The full coverage and cost table

Per resumption, `psk_ke`, from `(N=1 run - N=0 run)` - the least contaminated
run length, see the limits below:

| arm | secret ops with DIT clear | non-secret ops under DIT | binder compare |
|---|---|---|---|
| `null` (unhardened) | 625,973 | 0 | 871 |
| `prim` | 43,436 | 107,653 | 547 |
| `api` | 12,254 | 250,207 | 547 |
| `pass` | 11,126 | 254,520 | **0** |
| `func` | 7,923 | 268,745 | 0 |
| `nogate` | 7,251 | 292,752 | 0 |
| `bracket` | 306,578 | 172,634 | 355 |
| `blanket` | **0** | 340,959 | 0 |

### Read the first column before the last: the pass is NOT sound here

**`blanket` leaves zero secret operations unprotected and `pass` leaves
11,126.** That is the first thing this table says and it must not be buried
under the binder-compare result. Selective placement on this workload is a
*reduction* in exposure, from 625,973 to 11,126 per resumption, not an
elimination. Anyone who needs soundness on this flow should set the bit
process-wide; blanket also costs only 1.34x the pass's over-protection here
(next bullet), so on this workload there is little reason not to.

Decomposing what `pass` leaves, per resumption (`data/oracle_raw/psk_pass_residual_split.txt`):

| where the residual is | ops | share | can annotation reach it? |
|---|---|---|---|
| **outside instrumented code**: `free` 2,915, `memcpy` 2,387, `calloc` 2,306, `memset` 120, harness pipe stubs 360 | **8,218** | **74%** | **Partly.** Not by instrumenting libc, but a protected caller leaves DIT set across the call and the callee inherits it - seed round 2 below cut this to 3,731 without touching libc, at the price of dwell |
| **inside instrumented mbedTLS**: transcript hash (`psa_hash_finish` 266, `mbedtls_sha256_clone` 252), record decrypt (`mbedtls_ssl_decrypt_buf` 200), key derivation (`psa_key_derivation_input_bytes` 136), and a long tail | **2,908** | **26%** | **Yes.** The pass compiled these and still left them clear: the secret reaches them through PSA operation structs and record contexts that the round-1 seed set never names. Seed round 2 took this to 1,906 |

### Why it cannot simply be made sound: the residual is a dial, not a wall

The obvious follow-up is "then add more seeds until it is sound". I did one
round of exactly that - naming the operation *contexts*
(`psa_hash_operation_t`, `psa_key_derivation_operation_t`, the record
transform) that round 1 had named only the *inputs* of - and measured both axes
(`data/seed_rounds.csv`, seeds in
`../../utils/dit_host_screening/decrypt_parse/seed_pass_round2.txt`):

| config | uncovered / res | wasted / res | coverage | switches |
|---|---|---|---|---|
| unhardened | 625,973 | 0 | 0% | 3 |
| `prim` (19 seeds) | 43,436 | 107,653 | 93.06% | 95 |
| `api` (42 seeds) | 12,254 | 250,207 | 98.04% | 401 |
| `pass` round 1 (48 seeds) | 11,126 | 254,520 | 98.22% | 459 |
| `pass` round 2 (69 seeds) | 5,637 | 296,930 | 99.10% | 557 |
| round 2 + `-taint-no-modset-gate` | **2,495** | 317,524 | **99.60%** | 1,162 |
| `blanket` | **0** | 340,959 | 100% | 3 |

**No configuration of the pass reaches zero, and the closest one costs 93% of
blanket's over-protection.** What survives at maximum conservatism is allocator
internals reached from callers the analysis never considered tainted (`free`
530, `calloc` 454, `memcpy` 355), the harness's transport stubs (163, a
single-process artifact), the client side (134, same), `ctr_drbg_update_internal`
(93) and small server tails.

**So on this workload there is no setting that is both sound and meaningfully
cheaper than blanket** - which is the framework's Q1 answering "use blanket
here", not a failure of the pass. The frontier is only worth walking on flows
where blanket is unaffordable, and this one is not: it is two thirds crypto.

**The same measurement on a flow that DOES have a public lane inverts the
verdict**, and it was run to check exactly this (experiment 02,
`../02-libsodium-signed-lookup/data/oracle_frontier.csv`). There the pass's
over-protection is *constant* at 1,083 ops per request while blanket's grows
with the public lane, reaching **208.8x** at `f_secret` = 1.56%; and the pass
covers 3,585 of 3,589 secret ops, the four survivors being the harness reading
published ciphertext. So the two experiments bracket the decision rule: at 65%
secret use blanket, at 1.6% secret the pass is effectively sound and two orders
of magnitude cheaper in coverage waste.
Three consequences:

0. **The comparison that matters is not pass-versus-blanket.** Where blanket is
   affordable it is strictly better: sound, and here only 1.34x the pass's
   over-protection. Selective placement earns its place only where blanket is
   *not* affordable - +32.64% on experiment 02's flow, +33.2% on experiment 03's
   small records - because a deployment that refuses to pay 33% does not run
   blanket, it runs nothing. On those workloads the pass competes with **zero
   protection**, not with full protection. This experiment is on the wrong side
   of that line and says so.
1. **Uninstrumented code is not a hard wall.** The libc share of the residual
   fell from 8,218 to 3,731 *without instrumenting libc*, because when the
   caller is protected the pass leaves DIT set across the call and the callee
   inherits it - which is precisely what `-taint-info-loss-report` says it does
   at every cross-TU stop. So `memcpy` on secret bytes is covered exactly when
   its caller is. Covering it costs dwell; it is not unreachable. The earlier
   "74% unreachable" reading of the round-1 split was too pessimistic and is
   corrected here.
2. **Soundness and cheapness are the same dial.** Blanket is trivially sound.
   The only way to cost less is to leave something unprotected, so any
   selective placement is unsound unless it can prove what it skips holds no
   secret - and the mechanism that buys coverage (keeping DIT on across calls
   it cannot see, widening regions) is the mechanism that costs dwell. The
   limit of the process is blanket. The whole-library arm in section 12 is the
   same finding from the other end: let taint flow conservatively through the
   entire call graph and 83% of all instructions end up under DIT.

Three limits are *not* on that dial and no amount of seeding removes them:

- **Seeds are a human specification.** The analysis is sound relative to a seed
  set it cannot derive. Nothing told it that a `psa_hash_operation_t` carries
  secret state; that was found by reading the oracle's residual, not from any
  report. This is experiment 08's finding (97.61% of secret ops unprotected at
  the seed a developer writes first) on a protocol flow.
- **Cross-TU output flows are invisible** (section 12): a callee taking public
  inputs and writing a secret through an out-pointer reports nothing, so the
  caller's later loads of that buffer look public.
- **Declassification is deliberately unsound**, and `PSTATE.DIT` does not cover
  secret-dependent addresses, divides or branches at all - a threat-model
  boundary, not an analysis one (`-taint-uncovered-report` enumerates these).

So the claim this experiment supports is narrow and should be stated that way:

> Among **selective** placements - the only category that can cost less than
> blanket - the pass reaches glue that no annotation of the crypto library's API
> reaches. It does not achieve blanket's coverage, and roughly a quarter of what
> it misses is addressable with more seeds rather than being a fundamental limit.

- **In bulk, the coverage gap between `api` and `pass` is small** (12,254 vs
  11,126) precisely because 74% of both residuals is the same uninstrumentable
  libc. The security-relevant difference is not the bulk count, it is *which*
  operations: the binder compare goes from 547 to zero.
- **A pure-PSK resumption is almost entirely crypto**, so there is little public
  work for selective placement to save: blanket wastes 340,959 non-secret ops
  per resumption against the pass's 254,520, a factor of **1.34** (the same
  ratio at N=4, so it is a property of the workload, not the run length). This is
  experiment 09's lesson recurring - when the measured region is nearly all
  secret, blanket is close to free and no placement can win on cost. The
  39x precision advantage measured in section 13 belongs to the flow *with*
  the ECDHE, and that measurement is contaminated.

**So this experiment's contribution is on the soundness axis, next to 04 and
08, not the cost axis.** It answers "what does the compiler protect that a
careful engineer does not", with a named function and a measured count.

### Known limits of this measurement

**The largest limit is the key exchange, and it has its own section.** See
section 15: these figures are `psk_ke` only.

- **Stale taint on free, and what it does and does not touch.** The oracle never
  clears taint when a buffer is freed and reused, so the *bulk* residual of a
  hardened arm grows with run length:

  | per resumption | N=1 | N=2 | N=4 |
  |---|---|---|---|
  | `null` (the secret-op universe) | 625,973 | 645,684 | 670,137 |
  | `pass` residual | 11,126 | 15,092 | 22,741 |
  | `prim` residual | 43,436 | 54,182 | 73,352 |

  The universe is stable (1.07x across a 4x longer run), so the workload is
  consistent; the growth is confined to the hardened arms' residuals and is the
  artifact. **Quote the N=1 bulk figures, not N=4.**

  The headline is unaffected, because it counts one function rather than the
  whole residual. The binder compare reads **exactly 547 ops per resumption
  under `prim` at both N=2 and N=4, and exactly 0 under `pass` at both.** A
  count that is constant across run lengths is not an accumulation artifact.
- **The client is in the same process.** In deployment it is another machine.
  It inflates the `bracket` residual (its 89% hash bucket is largely the
  client's transcript hashing) and adds client-side glue tails to every arm.
- **`psk_ke` is not the deployed default.** `psk_dhe_ke` is, and the section 13
  numbers are the honest statement of what happens there. `psk_ke` is the
  isolation that makes the glue readable, and is labelled as such.

## Contents (planned)

| path | what |
|---|---|
| `data/oracle_coverage.csv` | secret ops with DIT clear, by function and arm, both flows |
| `data/over_protection.csv` | public instructions under DIT per resumption, by arm |
| `data/gem5_cost.csv` | cycles and committed `MSR DIT` per resumption, both switch models |
| `data/seed_rounds.csv` | the annotation loop: seeds per round, per-TU and whole-library |
| `data/static_probe.csv` | **G0, measured.** per-function need / under-DIT / collateral / switches, four seed sets |
| `data/whole_library_probe.csv` | **G0, measured.** the whole-library spread under five seed sets |
| `data/oracle_dhe.csv` | **G1 first attempt, measured.** under-taint and wasted coverage per arm, psk_dhe_ke |
| `data/oracle_psk.csv` | **G1 corrected, measured.** the same under psk_ke, plus the binder-compare column |
| `data/seed_rounds.csv` | **measured.** the coverage/cost curve: two seed rounds against `prim`, `api` and `blanket` |
| `data/oracle_raw/` | **G1, measured.** raw oracle reports and the pass per-function under-taint breakdown |
| `data/silicon_headroom.csv` | G3, and the crossover if run |
| `figures/leaves-primitive.html` | the published page |


## 15. The deployed key exchange, and a seeding gap that invalidated section 14's aggregates

Measured 2026-09-03, after noticing that section 14's isolation (`psk_ke`)
removes the elliptic-curve exchange every real resumption performs.

**The gap.** Under `psk_dhe_ke`, mbedTLS generates an ephemeral X25519 private
scalar per handshake via `psa_generate_key` (`ssl_tls13_generic.c`) and agrees a
shared secret via `psa_raw_key_agreement` (`ssl_tls13_keys.c`). That scalar is a
second secret with no relationship to the ticket key. The seed sets of sections
12-14 name **no** key-agreement, ECDH, ECP or bignum entry point, so the entire
exchange ran with `PSTATE.DIT` clear.

**Measured, oracle seeded at the ticket key AND the PSA RNG** (the scalar lives
in a PSA key slot the harness cannot address, so the RNG is the only reachable
seed point; this over-approximates, since nonces come from the same source and
are declassified again at the transport):

| arm | one full handshake | per resumption |
|---|---|---|
| `null` (control) | 0% of 15,130,577 | 0% of 20,372,928 |
| **`pass2`** = section 14's seeds | **4.13%** | **3.42%** |
| **`pass3`** = + 13 ECDHE seed lines | **93.96%** | (pending) |
| `blanket` | 100% | 100% |

**Thirteen seed lines take coverage from 4% to 94%.** They add 361 switches
across 30 newly covered functions, all of them the curve arithmetic:
`ecp_double_jac`, `ecp_add_mixed`, `ecp_select_comb`, `ecp_randomize_jac`,
`mbedtls_ecdh_compute_shared`, `mbedtls_ecp_gen_privkey`. Seeds in
`../../utils/dit_host_screening/decrypt_parse/` and
`gem5-DIT/benchmarks/tls_resume/seed_pass3.txt`.

**And soundness costs what blanket costs.** Going 4% -> 94% took
over-protection from 391,007 to 19,006,221 ops per handshake, against blanket's
20,662,665. The soundly-seeded pass covers 92% of what blanket covers to protect
94% of what blanket protects. On this workload there is no exploitable gap
between them, which is section 14's frontier conclusion confirmed under the
deployed configuration instead of inferred from an isolated one.

**The lesson for the method, not just this experiment.** The isolation that made
the ticket glue readable also removed the dominant secret. A seed set validated
against one configuration says nothing about another, and only the dynamic
oracle caught it - the static reports named no missing ECDH function, because
nothing had ever told the analysis the scalar existed.
