# Relaxed callee ownership: halves the toggle cost, still short of always-on

> **STATUS 2026-08-24: THE FLAG IS GONE.** `-taint-dit-relaxed-ownership` was deleted.
> Its precondition is a local-linkage, address-not-taken callee, which a shared library
> structurally cannot satisfy, and the libsodium composite measured it at ~0 (1117 -> 1098
> static switches, runtime inside noise). Corridor merging under
> `-taint-dit-switch-cyc` reaches the same re-assert sites and works on external linkage
> too. This document is kept as the record of what callee ownership was worth.

**Implemented and measured 2026-08-17.** New flag
`-mllvm -taint-dit-relaxed-ownership` (default **off**). Measured on the
`eth-account` / coincurve signing workload, 50 paired reps, arm order rotated.

---

## Bottom line

**The fix works and is verified sound, but it is not sufficient.** It removes
every `clears-on-exit` re-assert, halves the pass's overhead, and does not reduce
coverage by one instruction — yet the pass is still **+3.04% slower than blanket
always-on DIT**, where the hand oracle is **1.98% faster**.

| | before | after | oracle |
|---|---|---|---|
| switches in coincurve `.so` | 575 | **289** | 4 |
| re-assert sites | 48 | **19** | 0 |
| raw signing overhead | +44.07% | **+22.30%** | +6.63% |
| whole workload vs baseline | +10.81% | **+5.65%** | +0.55% |
| **vs always-on** | +8.24% worse | **+3.04% worse** | **1.98% faster** |

Roughly half the gap closed. The remaining half is a different problem — see §4.

---

## 1. The diagnosis

`-taint-dit-reassert-report` on libsecp256k1 with both ECDSA entry points seeded:

| reason | sites |
|---|---|
| **`clears-on-exit`** | **32** |
| `propagates-unresolvable` | 11 |
| `indirect` | 4 |

The 32 are shared static helpers. Broken down by callee,
`secp256k1_scalar_set_b32` alone appears under **10 different callers** — four
inside `secp256k1_ecdsa_sign`, the rest in `ecdsa_verify`,
`ec_seckey_tweak_add/mul`, `ec_pubkey_tweak_add/mul`, `ecmult_gen_blind`,
`nonce_function_rfc6979` and `ecdsa_signature_parse_compact`.

That mixture is exactly what the shipped `AlwaysEnteredWithDIT` rule cannot
qualify: it requires **every** in-TU call site to pass a secret, and a helper
shared between signing and verification never will. So each such call costs
**three** switches — callee sets, callee clears, caller re-asserts — where the
oracle needs none.

---

## 2. The change

> A **local-linkage, address-not-taken, instrumented** callee does not clear
> `PSTATE.DIT` before returning, so its callers skip the after-call re-assert.

This is the "function cloning" alternative from
`docs/design/dit-callee-ownership.md` §5 reduced to its cheapest form: rather
than emitting a `foo.dit` clone, it simply extends the existing `OwnsDIT`
machinery, which already knows how to emit "entry enable, no exit clear". The
diff is one predicate plus the two `OwnsDIT` decision sites; such functions route
to whole-function coverage exactly as `AlwaysEnteredWithDIT` ones already do.

**The entry ENABLE is still emitted.** Eliding it is the fail-dangerous direction
the design doc rules out, so the floor is **one** switch per call, not zero.

**Local linkage is what bounds the blast radius.** An externally-visible function
keeps its exit clear, so DIT cannot escape the hardened library — at worst it
stays on to the boundary of the outermost external function, which clears it.
This is why `secp256k1_ecdsa_sign` (external) still owns and clears DIT while
every static helper beneath it stops toggling.

**Failure direction is fail-safe.** If the analysis is wrong, DIT is left *on* —
a dwell cost — never off over a secret.

---

## 3. Verification

| gate | result |
|---|---|
| compiler soundness verifier (fatal on an uncovered Need) | **passed** |
| gem5 `compSimplifier.ditSuppressed` vs oracle | **101.2%** (hoist 101.3%) — no under-protection |
| DIT leak check (`mrs DIT` after the ROI) | **`dit_after=0`** — no escape |
| checksums, all arms and all builds | **identical** |
| in-band `lvp_chase` control | **3.97x** |
| noise floor (`baseline` vs `baseline2`) | −0.11%, 20/50 |

**One unexplained observation, recorded rather than smoothed over:**
`valuePredictor.ditTaggedSet` fell 17% (26.9M → 22.3M) while
`compSimplifier.ditSuppressed` — the real suppression count — held flat. The
likely cause is that `ditTaggedSet` is counted at both dispatch *and* commit
(`lvp.hh` says so), so 286 fewer serializing switches means fewer pipeline
flushes and less re-dispatch double-counting. **That is an inference, not a
measurement.** The protection guarantee rests on `ditSuppressed` and the
compiler's verifier, both of which are clean.

---

## 4. Why it is still not enough

Toggle arithmetic from the raw-signing column: 351.9 ms vs 287.7 ms baseline is
64.2 ms over 25,000 signatures = 2.57 us/sig. Subtracting the DIT dwell
(6.63% of 287.7 ms = 0.76 us/sig) leaves **~1.8 us/sig of switching ≈ 28 toggles
per signature**, down from ~75 but still 14x the oracle's 2.

The survivors are:

1. **The entry enables** — one per call to every instrumented helper, kept
   deliberately as the fail-safe direction. Removing them needs to know the
   caller's state, which is either **cloning** (specialise the callee per call
   context) or **Mode 2** (runtime `mrs DIT` read). Both are the deferred designs.
2. **The 19 remaining re-asserts** — 11 `propagates-unresolvable`, 8 `indirect`.
   Only Mode 2 reaches these; cloning cannot pick a clone at an indirect site.

**Correction to the design doc.** §5 argues cloning is "strictly weaker" and
worth "roughly two thirds of the win", reasoning from SQLCipher, whose hot path
is an indirect dispatch through `cipher_descriptor[]`. **That conclusion does not
transfer to libsecp256k1**, whose hot path is entirely direct calls — here
cloning would reach the entry enables too and land near the oracle, while this
cheaper relaxation cannot. The right fix is workload-shape-dependent, and the
doc's SQLCipher-based ranking should not be applied to direct-call libraries.

---

## 5. Status

The flag is **default off**. It is a strict improvement on this workload with no
measured coverage loss, but it does not make the pass beat always-on, so turning
it on by default should wait until the entry-enable term is also addressed —
otherwise the shipped default changes codegen for a result that is still a loss
against blanket protection.

Raw data: `utils/dit_host_screening/coincurve/signbench.csv`;
pre-change run kept as `signbench_prerelax.csv`.
