# The callee contract, measured

**Date:** 2026-09-04. **Compiler:** branch `dit-callee-contract`, one build;
`-taint-dit-contract=inherit` (the default) vs `=callee`, nothing else
differs. **Oracle:** gem5-DIT with the store-rule fix, `--eves --dmp
--comp-simp`. **Host:** beckham. Design: `docs/design/dit-callee-contract.md`.

## 0. The default is byte-identical

Stripped objects, default contract vs the measured post state of PR #27:
flowprobe identical, libsecp256k1 identical, libsodium 129 of 129 identical,
mbedTLS (727 seeds) 108 of 108 identical. Every change below is behind the
flag. 53 of 53 taint lit tests pass, including the new
`clang/test/CodeGen/taint-dit-contract.c`, which runs both contracts on the
same five shapes.

## 1. Two things the measurement found about the measurement

**glibc's `memcpy` hides secret flows from the oracle.** Its multi-register
NEON loads are the register-tuple form the shadow taint cannot follow (the
same mechanism as flowprobe C4/C7), so a key copied by libc's `memcpy` reads as
public downstream. Linking the scalar hardened movers (`dit_movers.c`, a
byte-loop `memcpy`/`memmove`/`memset` built with the pass) is therefore also an
oracle fix. On libsecp256k1 signing it changes the **inherit** baseline from 40
uncovered ops to **614,260**: the HMAC-SHA256 nonce derivation over the
private key has been running unprotected in every configuration measured to
date, and the oracle could not see it. Every earlier oracle figure taken with
libc's `memcpy` on a path that copies a secret is an undercount; the Tier-2
"zero on libsecp256k1" claims are among them.

**The cause is the caller-to-callee frame-address gap, not the contract.**
`nonce_function_rfc6979_impl` stages the key in `keydata[112]` on its frame
and passes `&keydata` to the HMAC. The analysis does not treat a frame address
as passing a secret (`-taint-frame-addr-args`, default off, docs/design/
p1b-frame-provenance.md), so the HMAC and SHA-256 code is never instrumented,
and under inherit the caller's region does not reach it either. The info-loss
report has said so all along, as an `UNSOUND memory` record naming
`secp256k1_rfc6979_hmac_sha256_initialize` at line 522; the record now also
names the argument index, because the first repair pasted from it went on the
wrong argument.

## 2. flowprobe

| | inherit | callee |
|---|---|---|
| `msr DIT` sites | 53 | 50 |
| under-taint ops (oracle) | 256 | 256 |
| protected ops | 426 | 426 |
| wasted ops | 106 | 124 |

Unchanged coverage; the four open channels (asm, NEON tuple, ×2 storage) as
before. Obligations: none (every callee is in the TU).

## 3. libsecp256k1 (2 seeds, the ECDSA sign entry points; 20 signatures)

| arm | sites | uncovered | of which | DIT writes |
|---|---|---|---|---|
| inherit, libc memcpy (the old baseline) | 29 | 40 | driver seed site | 480 |
| inherit + hardened movers | 29 | **614,260** | `sha256_transform` 609,820, HMAC init 1,080, finalize 3,200 | 2,098 |
| callee + movers | 32 | 614,279 | same + 19 | 2,118 |
| callee + movers + the report's 4 seed lines (`rfc6979_hmac_sha256_initialize,2,pointee`, `rfc6979_hmac_sha256_generate,1,pointee`, `hmac_sha256_initialize,2,pointee`, `sha256_write,1,pointee`) | 68 | **3,240** | `sha256_finalize` 3,200 (one more frame hop), driver 40 | 7,992 |
| callee + movers + `-taint-frame-addr-args` | 157 | **40** | the driver's seed site only | 8,367 |

The contract's own obligation record on this library is one line: the nonce
function is reached through a pointer. Filling it is not what closed the gap;
the `UNSOUND memory` records were, one per frame-address hop, and the
frame-address flag closes all of them at once for 4x the DIT writes of the
old baseline (8,345 vs 2,098 per 20 signatures, small in absolute terms).

**The callee-saved restore class.** Before the restore rule the contract
left one `ldp x22, x21` per signature uncovered, 19 of 4.6M ops: the
epilogue reloads the caller's callee-saved registers, which hold a secret the
analysis has no way to know about. Under inherit the blunt TOP poisoning of
every reload after an external call kept the exit clear below these restores
by accident; the spill-slot exemption removed the accident. On mbedTLS the
same class was 224k ops per run in `mbedtls_mpi_sub_abs` alone (section 5).
With a callee-saved restore of a data register a placement Need, the exit
clear sits below the restores and the count is the driver's 40.

## 4. libsodium (shipped 65 seeds; signing, 2 rounds)

| arm | sites | protected | uncovered | wasted | DIT writes |
|---|---|---|---|---|---|
| inherit | 129 | 294,164 | 0 | 53,996 | 6 |
| callee | 107 | **0** | **294,164** | 0 | 0 |
| callee + the report's 21 seed lines | 153 | 294,164 | **0** | 53,988 | 44 |

The shipped seed file protects the signing path entirely by inheritance: its
seeds sit on forwarders (`crypto_sign` tail-calls `crypto_sign_ed25519` in
another TU), so under the contract nothing is covered. The obligation report
for that build has 20 records; pasting its 21 seed lines and rebuilding
restores every protected op at the same wasted coverage. One round. The two
indirect sites it lists (the Poly1305 implementation table) resolve to
functions the shipped file already seeds. Six DIT writes under inherit
against 44 under the filled contract is the whole difference between one
enable in a forwarder that covers everything below it and each primitive
covering itself.

## 5. mbedTLS 3.6.2, TLS 1.3 resumption (`--resumptions 5 --kex dhe`)

### Static

| | r5 inherit | r5 callee | r4 inherit | r4 callee |
|---|---|---|---|---|
| `msr DIT` sites | 3,222 | 3,302 | 3,168 | 3,252 |
| Needs | 21,131 | 18,073 | 20,221 | 17,217 |
| weighted Needs | 119,096 | 111,133 | 118,078 | 110,169 |
| instructions under DIT | 41,253 | 40,254 | 40,010 | 39,075 |
| objects differing from inherit | | 40 of 108 | | 39 of 108 |
| functions with a Need | 469 | 460 | 458 | 449 |

Fewer Needs (calls are not Needs), 2.5% more sites (callees toggle for
themselves, and the epilogue restores of data registers are covered), 2.4%
fewer instructions under DIT. No function gains a Need under the contract.

### The obligation list, r5 seeds

424 records: 389 named callees, 35 indirect sites, 80 distinct seed lines.

| class | records | repair |
|---|---|---|
| libc movers (`memcpy` 118, `memset` 53, `memmove` 4) | 175 | link `dit_movers.o` (the `r5clm` arm) |
| allocators (`calloc` 44, `free` 39) | 83 | none: sizes and pointers derived from secrets, the leading-zero limb trim |
| `mbedtls_platform_zeroize` | 26 | one seed line |
| other cross-TU (`asn1_get_tag` 16, `mpi_sub_int` 8, `mpi_grow` 4, ...) | ~93 | seed lines, the next round |
| `printf` family | 12 | over-approximation from the record layer |
| indirect (`cipher.c` 14, `ssl_msg.c` 15, ...) | 35 | seed the dispatch targets |

### Dynamic

Protocol of experiment 10 (`--resumptions 5 --kex dhe`, five argv[0] path
lengths, medians; the n0/n2 oracle pair per arm). Two states of the contract
are reported: before and after the callee-saved restore rule, because the
difference between them is the price of that class on its own.

| arm | renamed | serialising | DIT writes / run | coverage / res |
|---|---|---|---|---|
| round-trip control | 0 | 0 | 0 | |
| blanket | -1.40% | -1.24% | 0 | |
| r5 seeds, inherit (shipped) | +3.50% | +252.62% | 12,110,336 | 99.932% (8,222 uncovered) |
| r5 seeds, callee, restores uncovered | +4.57% | +240.71% | 11,318,148 | 96.789% (387,438) |
| **r5 seeds, callee (final)** | **+6.17%** | **+251.53%** | 11,916,847 | 99.877% (14,803) |
| r5 seeds, callee + naive hardened movers | +10.09% | +272.57% | 13,116,695 | 99.891% (13,295) |

Spreads are 0.00-0.27% across the five path lengths on every arm.

**Timing.** The contract executes 1.6% fewer DIT writes than inherit and the
serialising model, which prices writes, agrees to the sign: -0.31%. The
renamed model charges **+2.59%** over inherit for it, at slightly FEWER
covered instructions (the oracle's protected plus wasted per resumption:
27.94M against 28.02M). That is neither switches nor dwell; the counters here
do not explain it, and it is the first question for Phase C. The naive
movers add +3.69% renamed and +5.99% serialising on top, which is the byte
loop, not DIT (1.2M more writes per run from their own enable and clear).

**Coverage.** Uncovered ops per resumption go from 8,222 to 14,803. The
residual classifier splits them:

| class | inherit | callee | callee + movers |
|---|---|---|---|
| value arithmetic in crypto (the real DIT class) | 3,209 | 3,216 | 3,216 |
| loads/stores/moves inside crypto functions (limb traffic) | 1,562 | 7,196 | 7,196 |
| pointer/memory traffic (libc, allocator, `memcpy`) | 2,730 | 3,476 | ~2,100 |
| other | 721 | 915 | ~800 |

The value-arithmetic class is identical under both contracts (3,072 of it in
`mbedtls_mpi_mul_mpi` either way, a pre-existing residual). What the contract
loses is limb traffic in three routines, `mbedtls_mpi_add_mod` 3,234,
`ecp_double_add_mxz` 2,050 and `mbedtls_mpi_mul_mpi` 551, that inherit
covered from their callers' regions and that the contract expects them to
cover themselves; they are in-TU, seeded, and instrumented, so this is a
placement question inside those functions (Phase C), not an obligation. The
`memcpy` share (1,376) is the mover obligation and the movers close it.

The restore rule on its own is worth 372,635 uncovered ops per resumption
(387,438 before it, 14,803 after), the whole of the gap between 96.79% and
99.88%.

## 6. Verdict

- **The contract holds and is measurable.** Every dependency between
  functions is explicit; seeding is monotone (libsodium, section 4); the
  obligation list is the seed loop's input, and the oracle confirms each
  round. The price on the TLS workload is +2.59% on the renamed model and
  nothing on the serialising one, for 0.055 points of coverage that the
  classifier locates in three bignum routines' own placement.
- **Two findings outrank the contract itself.** The oracle has been blind
  through glibc's `memcpy`, and the libsecp256k1 nonce derivation was never
  protected under any contract; and callee-saved restores of a caller's
  secrets were only ever covered by accident. Both are now visible, one is
  fixed by a placement rule, the other by the existing frame-address flag
  once the movers let the oracle see it.
- **Do not flip the default yet.** The +2.59% is unexplained, the limb-traffic
  loss belongs to Phase C's placement work, and the naive movers are not a
  shippable `memcpy`. The flag, the obligation report, and the movers are
  the tools; the seed sets for libsodium and libsecp256k1 in the rig are the
  first two obligation lists closed.
