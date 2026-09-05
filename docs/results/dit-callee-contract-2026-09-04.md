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
restores every protected op at the same wasted coverage. The two indirect
sites it lists (the Poly1305 implementation table) resolve to functions the
shipped file already seeds. Six DIT writes under inherit against 44 under
the filled contract is the whole difference between one enable in a
forwarder that covers everything below it and each primitive covering
itself.

**The loop to convergence, with ownership.** Each round's report, split by
`utils/taint_obligations.py` against the 912 functions the library defines
(`utils/taint_owned_symbols.sh`), feeds the next round's seed file; the pass
runs with `-taint-owned-symbols` so the per-TU summary already separates
owned obligations from external calls.

| round | seeds | `msr DIT` sites | owned lines proposed | notes |
|---|---|---|---|---|
| 1 (shipped) | 65 | 107 | 21 | the signing entry `crypto_sign_ed25519` and the AEAD/pwhash entries |
| 2 | 86 | 153 | 10 | SHA-512, the curve's `ge25519_p3_tobytes` / `sc25519_muladd`, `randombytes_buf` |
| 3 | 96 | 191 | 6 | `sodium_bin2base64` |
| 4 | 102 | 197 | 0 from callee records | oracle: 96 uncovered in `crypto_hash_sha512_final`, reached only by frame address, so never a callee record; the tool now harvests the frame-address (`UNSOUND memory`) records |
| 5 to 8 | 120, 136, 151, 159 | 351, 402, 437, 452 | 38, 47, 46, then a stall | two report defects: a seed index for a register that maps to no parameter (a by-value struct spans two registers; fatal to the next build), and frame-address records re-proposed for already-seeded arguments |
| 8, widened | 159 | 452 | 17 | the frame-address record now fires for calls that also pass a register secret (`ge25519_p3_tobytes(sig, &R)`), which is what had left `fe25519_invert` uncovered at 42,312 ops |
| 9, 10 | 176, 186 | 447, 489 | 10, 2 | the field arithmetic |
| **11** | **188** | **489** | **0** | **converged; signing oracle 294,164 protected, 0 uncovered, 51,134 wasted (inherit 53,996), 10,400 DIT writes per two signatures (inherit 6)** |

What remains at the fixpoint: 8 indirect sites (the Poly1305, ChaCha20 and
`randombytes` implementation tables, seeded by name) and 35 calls into 10
libc functions (`memset`, `memcpy`, `memmove`, `__memcpy_chk`,
`__explicit_bzero_chk`, `strlen`, ...), filed as `external-call` and never
proposed.

Two things this settles. The contract's fixpoint is reachable by the loop
alone, with no knowledge of the library, and it protects exactly what
inherit protected on this workload at fewer wasted ops. And it costs
switches: 10,400 DIT writes per two signatures where inherit paid 6, because
every primitive down to the field multiply toggles for itself. That is the
number the serialising model prices; under the renamed model the NOP arms
say the sites' placement dominates. Neither is measured here; experiment
9's timing rig with the round-11 seed file is the measurement.

### 4.1 Cloning: the 10,400 become 41 (`docs/design/dit-cloning.md`)

`-mllvm -taint-dit-clone-seeded` gives every seeded function, and every
function it reaches by direct call in its TU, a `<name>.dit` twin that is
entered DIT-on by construction and emits no switch of its own; a call made
from DIT-on code is redirected to the twin, in the TU and across TUs (a
seeded declaration in the owned list is assumed to have one; the linker
resolves it). Same round-11 seeds, same driver, same oracle protocol:

| arm | sites | twins | `.text` | protected | uncovered | wasted | DIT writes |
|---|---|---|---|---|---|---|---|
| round 11, no twins (the row above) | 489 | 0 | 316,119 | 294,164 | 0 | 51,134 | 10,400 |
| seeded twins only | 382 | 68 | 360,219 | 294,164 | 0 | 53,972 | 4,784 |
| **seeded + reached twins** | **358** | **83** | **383,255** | **294,164** | **0** | **54,010** | **41** |
| inherit, for reference | 129 | 0 | 314,943 | 294,164 | 0 | 53,996 | 6 |

Protection does not move; the switches do. The intermediate row is why the
twin set is the call graph under the seeds and not the seeds alone:
`ge25519_cmov` is unseeded, instrumented by propagation, and called eight
times per table lookup from `ge25519_cmov8_base`, and with only seeded twins
that one callee was most of what was left. The 41 are the entries into the
library from unhardened code (the signing forwarder is Off; the function
below it enables once and calls nothing but twins). Price: +21% text, and
the twins' whole-function coverage is the +5.6% wasted. The no-flag build is
byte-identical to round 11 (whole-archive disassembly).

**Timed** (gem5, both switch models, against instruction-matched NOP
baselines; the full tables are in `docs/design/dit-cloning.md` §5.1):

| workload | model | no twins | twins | blanket |
|---|---|---|---|---|
| ed25519, 50 x 1 KiB | serialising | +76.22% | **+2.20%** | +1.77% |
| ed25519 | renamed | -2.49% | +1.42% | +1.77% |
| AEAD, 200 x 1400 B | serialising | +8.19% | +6.09% | +0.80% |
| AEAD | renamed | +0.68% | +0.64% | +0.80% |

Serialising: the twins are the result (3,530 -> 16 switches per signature).
Renamed: the switches were never the cost; the no-twin arm beats its own
NOP baseline because a renamed `MSR DIT` is cheaper than the NOP standing
in for it, and the twins cost what blanket costs, dwell. AEAD keeps 38 of 58
switches per call behind the Poly1305/ChaCha20 implementation tables, since
an indirect call is never redirected.

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
| r5 seeds, inherit, every switch a NOP | +4.84% | +4.81% | 0 | |
| r5 seeds, callee, every switch a NOP | +7.14% | +7.11% | 0 | |
| **r5 seeds, callee + twins (the default since 2026-09-05)** | **+11.30%** | **+40.27%** | 1,037,782 | 99.955% (5,484) |
| r5 seeds, callee + twins, every switch a NOP | +12.59% | +12.50% | 0 | |

The twins rows were added 2026-09-05 (`docs/design/dit-cloning.md` §5.2,
experiment 10 §16): 91% fewer executed switches, the best coverage of any
arm, and on the renamed model +5.1 points over the twin-less contract that
the NOP control attributes entirely to instruction fetch on the duplicated
code.

Spreads are 0.00-0.27% across the five path lengths on every arm.

**Timing, and the NOP control that explains it.** The contract executes
1.6% fewer DIT writes than inherit and the serialising model, which prices
writes, agrees to the sign: -0.31%. The renamed model charges **+2.59%** over
inherit for it, at slightly FEWER covered instructions (the oracle's
protected plus wasted per resumption: 27.94M against 28.02M), so neither
switches nor dwell. The NOP arms (`-taint-dit-nop-switches`: every switch
site carries a `HINT #0` instead, identical code otherwise, 3,222 and 3,302
of them) say what it is:

| | inherit | callee |
|---|---|---|
| NOP arm vs control, renamed | +4.84% | +7.14% |
| NOP arm vs control, serialising | +4.81% | +7.11% |
| real arm minus NOP arm, renamed | **-1.35 points** | **-0.97 points** |
| real arm minus NOP arm, serialising | +247.80 points | +244.41 points |

On the renamed model the placement's instructions cost MORE than the mode
they switch: the whole +3.50% of the shipped arm and the whole +6.17% of the
contract are the inserted instructions and the layout they impose, and
executing DIT in their place recovers about a point, the same direction and
size as blanket's -1.40%. The +2.59% between the contracts is +2.30 points
of NOP-arm difference: 80 more sites, and more of them inside the hot bignum
callees' own entries and exits. The NOP arm is the same to 0.03% under both
switch models, as it must be. On the serialising model the mode is the
cost, +244 to +248 points, and the placement's own share is the same 5-7%.
The known caveat applies: a `HINT #0` measured ~0.25% slower than a real
filler op on two cache points (CLAUDE.md), so the placement share is
overstated and DIT's understated by about that much. This settles, for this
workload on the clang path, the "contested" note in CLAUDE.md: the layout
term is not zero, it is the whole renamed-model cost. The naive movers add
+3.69% renamed and +5.99% serialising on top, which is the byte loop, not
DIT (1.2M more writes per run from their own enable and clear).

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
- **Phase C item 1 is done and it is the whole switch cost.** Twins
  (section 4.1) take libsodium's contract from 10,400 executed DIT writes to
  41 against inherit's 6, at identical coverage and +21% text.
- **Flipped 2026-09-05, with the twins** (section 4.1 and
  `docs/design/dit-cloning.md` §5.1: the twins remove the switch cost that
  was the reason not to). The bullet below is the verdict as it stood the
  day before, kept as written.
- **Do not flip the default yet.** The +2.59% is placement, not DIT (the NOP
  arms), which makes it Phase C's to reduce along with the limb-traffic loss;
  and the naive movers are not a shippable `memcpy`. The flag, the obligation report, and the movers are
  the tools; the seed sets for libsodium and libsecp256k1 in the rig are the
  first two obligation lists closed.
