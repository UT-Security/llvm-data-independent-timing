# The `ReturnsPointeeTainted` summary bit, and the seeded-callee return gate

**Date:** 2026-09-04. **Compiler:** `dit-tainter` at `f138dbae16c0` (pre) vs
this change (post), same build dir, same flags; every pre/post pair below
differs in nothing else. **Oracle:** gem5-DIT with the store-rule fix
(`gem5-DIT` #93). **Host:** beckham (aarch64, no FEAT_DIT; gem5 only).

## What was fixed

Phase 2 (`phase2-unknown-tainted-2026-09-04.md` §"U5 is the wrong lever")
showed that flowprobe C1 needs a summary bit, not a flag: a callee handed a
secret buffer that returns `buf + k` returns no secret VALUE, so
`ReturnsTainted` is false, and every load the caller makes through the result
read clean. This change adds that bit and the rules it needs to be reachable:

1. **`FunctionTaintSummary::ReturnsPointeeTainted`** - x0 is pointee-tainted at
   some return. Computed by the same replay walk as `ReturnsTainted`, applied at
   in-TU call sites with the same applicability rule, and at external call
   sites that received a secret ("unknown means tainted": the result may be a
   pointer to it - `memcpy` returns `dst`).
2. **The address of a module-secret global is a pointer to secret memory.**
   Materialising `&g` (ADRP/ADD with a GlobalAddress operand) for a global in
   `ModuleSecretGlobals`, or whole-object tainted by a callee, yields a
   pointee-tainted register. Without this a function that fills a global and
   returns its address (C1's exact shape) returns a public pointer.
3. **A secret stored through a pointer makes that pointer pointee-tainted.**
   The address registers of a secret store (every physical register use that is
   not a stored value, per `getNumStoredValueRegs`; SP and a reserved FP
   excluded) become pointee-tainted. Without this a function that mallocs,
   fills the block and returns it (C5) returns a public pointer.
4. **Globals carry pointer-ness** (taint-domain.md §5 item 1, lifted). A global
   store deposits the whole `TaintVal`, and a global that holds a pointer to
   secret memory - by a pointee-tainted store, or by a secret stored through a
   pointer loaded from it - is recorded in `TaintState::PointeeGlobals`,
   exported as `FunctionMemEffects::WritesPointeeToGlobal`, and folded
   module-wide (`TaintSummaryInfo::ModulePointeeGlobals`) like the secret-global
   set, so a sibling that reloads the pointer sees it.
5. **A seeded callee's return applies at every call site.** This is the one
   that closed C5, and it is a gate defect that predates the pointee bit: the
   return-applicability rule was `NonArgSourced || HasTaintedArg`, i.e. "apply
   the callee's return only where WE passed a secret". A seed is the user's
   statement that the parameter is secret at the callee, at every call, whatever
   the caller knows; flowprobe's `produce_all` has no parameters, materialises
   the key itself (the oracle taints it with a runtime marker), calls the seeded
   `c5_produce` and parks the returned pointer in a global. From `produce_all`'s
   view it passed nothing secret, so the return - value or pointee - was
   dropped, with the summary bit set and correct. `hasSeededParam(Callee)` now
   also satisfies the rule; `CalleeTaintIsOurs` stays false there, so the
   absorbed secret is recorded as non-argument-sourced, which it is.

The U5 flag (`-taint-call-result-pointee`) is removed: the bit subsumes it.

## flowprobe (the probe that motivated it)

Under-taint ops per consumer, gem5 oracle, one run each. `post2` is the bit
plus rules 1-4 without the gate change, kept to show where each channel closed.

| function | pre | bit + rules 1-4 | + seeded-return gate |
|---|---|---|---|
| `c1_consume` (returned pointer, global) | 63 | **0** | 0 |
| `c5_consume` (returned pointer, heap, via a global) | 63 | 63 | **0** |
| `c3_consume` / `c6_consume` (inline asm) | 63 / 63 | 63 / 63 | 63 / 63 |
| `c4_consume` / `c7_consume` (NEON tuple) | 63 / 63 | 63 / 63 | 63 / 63 |
| total | 389 | 325 | **256** |

C1 closed on the address-of-secret-global rule alone (`run_c1` passes `c1_buf`
directly; the return is not even on its path). C5 needed rule 3 to make
`c5_produce` return a pointee-tainted pointer AND the gate change to let the
unseeded `produce_all` accept it. The 256 that remain are exactly the four asm
and NEON channels, 63 each, plus one stray op in each of their `run_*`
wrappers - both mechanisms unchanged by this work and still open.

`clang/test/CodeGen/taint-returns-pointee.c` pins all five rules on synthetic
callers (argument-derived pointer, filled global, filled heap block, global
pointer variable, and the C5 shape with an unseeded caller for both the pointee
and the value return). All 52 taint lit tests pass.

## Cost on the three libraries

Static, pre vs post, one compiler build each, objects compared with debug info
and `.comment` stripped.

| library | seeds | objects differing | `msr DIT` sites | note |
|---|---|---|---|---|
| libsodium 1.0.21 | shipped 65 | **0 of 129** | 129 -> 129 | byte-identical |
| libsecp256k1 | 2 (the ECDSA sign entry points) | 1 of 1 | 20 -> 29 | see below |
| mbedTLS 3.6.2 | 727 (`seed_pass_r5.txt`) | 17 of 108 | 2,865 -> 3,318 (+15.8%) | see below |

**libsecp256k1.** The nine new sites are `secp256k1_scalar_mul` (3) and
`secp256k1_scalar_mul_512` (2), newly instrumented, and four post-call
re-asserts in `secp256k1_ecdsa_sign_inner` around its calls to them. Rule 3
is what reaches them: the secret scalar lives on the caller's frame, its
address is held in a general register, a secret is stored through it, and the
register - now pointee-tainted - is passed on (the trace shows even
`secp256k1_scalar_set_b32`'s `int *overflow` argument arriving pointee-tainted).
The oracle says the multiply was **already covered**, by the caller holding DIT
across the call: protected 4,643,378 and under-taint 40 (all in the driver's
seed site) are identical pre and post over 20 signatures. So on this library
the change is the non-monotone seed pattern (`seed-loop-not-monotone`): a callee
that inherited coverage now toggles for itself, +12 executed DIT writes per
signature (480 -> 720 over 20), -1.08% cycles on the renamed model, i.e. noise.

**mbedTLS.** 75 functions gain Needs, none lose, 48 are newly instrumented.
The largest are the record layer and the self-tests, both public-argument
callers of seeded routines whose return the gate used to drop:

| function | need pre -> post | switch sites |
|---|---|---|
| `mbedtls_ssl_read_record` | 0 -> 417 | 0 -> 19 |
| `mbedtls_ssl_flight_transmit` | 0 -> 331 | 0 -> 9 |
| `mbedtls_x509_dn_gets` | 0 -> 222 | 0 -> 13 |
| `mbedtls_aes_self_test` | 0 -> 188 | 0 -> 49 |
| `mbedtls_ssl_write_record` | 0 -> 171 | 0 -> 6 |
| `mbedtls_ssl_fetch_input` | 0 -> 110 | 0 -> 12 |
| `mbedtls_gcm_self_test` | 0 -> 20 | 0 -> 52 |

The record layer is the interesting one: `mbedtls_ssl_decrypt_buf` is seeded
on the transform, and its error code depends on the MAC comparison, so its
return IS secret-dependent; before this change a caller that passed only the
public `ssl` context had that return read clean, and every branch on it in
`mbedtls_ssl_read_record` ran with DIT off. That is the padding-oracle shape,
and it is what the gate change is for. The self-tests are the price: they call
seeded primitives with public test vectors, and by the seed's own declaration
those calls are secret-passing.

### The resumption workload, dynamically

Protocol of experiment 10 (`timing_c5.sh`, `read_timing_c5.py`,
`read_oracle_c5.py` in gem5-DIT `benchmarks/tls_resume`): `--resumptions 5
--kex dhe`, five argv[0] path lengths, medians; the harness is linked
identically against each arm's library; the oracle pair is `--resumptions 0`
and `2`, per-resumption figures are their half-difference.

| arm | renamed | serialising | DIT writes per run |
|---|---|---|---|
| round-trip control (0 switches) | 0 | 0 | 0 |
| blanket (DIT process-wide) | -1.40% | -1.24% | 0 |
| r5 seeds, pre | +2.90% | +252.91% | 12,109,416 |
| r5 seeds, post | +3.50% | +252.62% | 12,110,336 |
| **post over pre** | **+0.58%** (spread 0.27%) | **-0.08%** (spread 0.01%) | +920 |

The +15.8% static sites execute **920 more switches per run**, 184 per
resumption, against 12.1M: the self-tests and most of the new sites never run.
So the serialising model, which prices switches, sees nothing, and the renamed
model's +0.58% (twice its spread) is dwell - the record layer now runs under
DIT, and wasted coverage grows by 4,296 ops per resumption (+0.03%).

What it buys, per resumption, fixed oracle:

| | pre | post |
|---|---|---|
| uncovered | 8,610 | **8,222** (-4.5%) |
| protected | 12,058,868 | 12,059,256 |
| wasted | 15,952,932 | 15,957,228 |
| coverage | 99.929% | 99.932% |
| functions with any under-taint (n2 run) | 120 | **108** |

The twelve that closed are the record layer and the DRBG's AES, every one to
zero: `srv_recv` 154, `mbedtls_ssl_read_record` 110, `mbedtls_aes_crypt_ecb`
108, `mbedtls_ssl_prepare_handshake_record` 108, `mbedtls_ssl_fetch_input`
102, `mbedtls_ssl_write_record` 82, `mbedtls_ssl_handle_message_type` 60,
`mbedtls_x509_get_name` 24, `mbedtls_x509_parse_subject_alt_name` 20,
`mbedtls_ssl_decrypt_buf` 11, `mbedtls_ssl_encrypt_buf` 10,
`ssl_parse_record_header` 6. What remains is the allocator and `memcpy` floor
(`__memcpy_generic` 2,209 and `free` 712 per n2 run, both down slightly) - the
address channel DIT does not cover, unchanged in kind.

**Verdict:** the seeded-return gate buys the record layer's secret-dependent
error handling for +0.58% on renamed hardware and nothing on serialising; the
executed-switch count, which sets the price on this workload
(`u4-and-full-seeding-cost`), is unchanged to within 0.01%.

## What this does not do

- The mod-set gate is untouched. A seeded callee's memory clobber still applies
  only where the caller passes a secret (or the callee is non-argument-sourced).
  The same declaration argument applies to it, but the clobber is all of memory
  where the return is one register, and widening it is the U4 decision
  (`phase2-unknown-tainted-2026-09-04.md`), taken the other way at +51%.
- Transitivity through a caller that DID pass a secret: `CalleeTaintIsOurs`
  still treats a seeded callee's return as argument-sourced when the caller
  passed something tainted, so a wrapper `K(x) { return seeded(x); }` called
  with public data still returns clean. Closing it means a seeded callee never
  counts as "ours", which routes through the single `NonArgSourced` bit and so
  un-gates the wrapper's mod-set too. Not measured; noted as a follow-up.
- C3/C4/C6/C7 (inline asm, NEON tuples) are as open as before.
