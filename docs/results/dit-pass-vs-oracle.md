# The pass reaches the oracle: CPython and SQLite

> ⚠️ **READ `dit-coincurve-timing.md` BEFORE CITING THIS (2026-08-17).** On a
> real signing workload, where the secret is **9.1%** of runtime instead of the
> 0.02%/0.06% here, the pass is **+12.62%** against always-on's **+2.74%** —
> i.e. **4.6x worse than blanket protection**. This document is not wrong, but it
> is **insensitive**: a workload whose secret is 0.02% of runtime cannot
> distinguish good placement from bad, because the secret region is too small to
> hold any cost. **Do not cite it as evidence that the pass places well.**

**Measured 2026-08-14**, Apple M5, `dit-tainter` branch. Rig:
`utils/dit_host_screening/pass/`.

Closes the gap left open by `dit-oracle-composites.md`, which showed only that a
**hand-placed** oracle beats always-on. This measures what `-ftaint-harden`
actually emits.

---

## Bottom line

**With `-taint-dit-loop-hoist=1` the pass is indistinguishable from the hand
oracle on both hosts, and both recover essentially the entire always-on cost.**
This is the first time the *pass itself* — not an oracle, not a projection —
beats always-on DIT on a real host workload.

The shipped default (`region`, no hoist) recovers ~90%. Compare
`docs/results/sqlcipher.md`, where region placement cost **+56.97%** against an
always-on cost of +8.89%. The reversal is structural, and the granularity model
predicts it: see §4.

---

## 1. Setup

**Only `secp256k1.c` is instrumented; both hosts are stock.** The secret key
reaches the program through exactly one place, so the taint source is one line:

```
secp256k1_ecdsa_sign,3,pointee
```

secp256k1's amalgamated build is a single TU, so interprocedural taint reaches
the whole signing path without the per-library re-declaration that SQLCipher and
mbedTLS needed.

Every arm is compared to `nodit` — the **round-trip control**, the same pipeline
with an empty seed file, which emits **0** `MSR DIT`. Never to a stock `-O2`
build (`dit-measurement-traps` trap 7).

| arm | build | runtime DIT mode | switches in TU |
|---|---|---|---|
| `baseline` | `-ftaint-harden=<empty>` | off | 0 |
| `always` | `-ftaint-harden=<empty>` | set at process start | 1 |
| `oracle` | `-ftaint-harden=<empty>` | set/cleared per signature | 2 / signature |
| `pass_region` | `-ftaint-harden=seed` | off | **148** |
| `pass_hoist` | `+ -taint-dit-loop-hoist=1` | off | **136** |
| `pass_function` | `+ -taint-dit-placement=function` | off | **122** |
| `baseline2` | second run of `baseline` | off | 0 — the noise floor |

16 reps, 2 burn-in discarded, **arm order rotated every rep**. Checksums
identical across every arm on both hosts.

---

## 2. Results

### CPython — baseline 3.823 s, CoV 0.54%, noise floor +0.11%

| arm | median | vs baseline | reps slower | recovers |
|---|---|---|---|---|
| `always` | 4.199 s | **+9.99%** | 16/16 | — |
| `oracle` | 3.822 s | +0.01% | 8/16 | 99.9% |
| `pass_region` | 3.857 s | +0.91% | 13/16 | 90.9% |
| **`pass_hoist`** | **3.818 s** | **−0.08%** | 7/16 | **100.8%** |
| `pass_function` | 3.854 s | +1.18% | 14/16 | 88.2% |

### SQLite — baseline 2.927 s, CoV 1.16%, noise floor −0.16%

| arm | median | vs baseline | reps slower | recovers |
|---|---|---|---|---|
| `always` | 3.034 s | **+3.47%** | 16/16 | — |
| `oracle` | 2.928 s | +0.09% | 8/16 | 97.5% |
| `pass_region` | 2.944 s | +0.35% | 11/16 | 89.8% |
| **`pass_hoist`** | **2.931 s** | **−0.02%** | 8/16 | 100.5% |
| `pass_function` | 2.932 s | +0.03% | 9/16 | 99.3% |

Recovery figures above 100% are not real; every one of `oracle`, `pass_hoist`
and (on SQLite) `pass_function` sits at or inside the rig's own noise floor. The
defensible statement is **"indistinguishable from the oracle, and from zero"**.

---

## 3. Loop-hoist is again the make-or-break flag

`-taint-dit-loop-hoist=1` is the difference between recovering ~90% and
recovering all of it, on both hosts. That matches QuickJS, where without hoisting
there was **no win at all**. Two independent host families now say the same
thing: **the shipped default of `0` is the wrong default.**

`function` placement behaves differently on the two hosts (88.2% vs 99.3%),
which is what you would expect from a granularity knob interacting with how much
of each host's secret path sits inside one function.

---

## 4. Why this succeeds where SQLCipher failed

Both use the same pass and a similar-sized TU. The difference is **switches per
unit of secret work**, exactly as `dit-granularity-crossover` predicts:

| | SQLCipher | here |
|---|---|---|
| secret share of run | large (crypto-dominated ROI) | **0.02% (CPython) / 0.06% (SQLite)** |
| region size | 300-500 cyc (per 16-byte block) | ~13-16 us (per signature) |
| switch sites reached at runtime | ~256 per 4 KB page | ~45 per signature |
| region placement cost | **+56.97%** | **+0.91% / +0.35%** |

The secret being genuinely rare is condition (a) doing real work: when secret
code is 0.02% of the run, even sloppy placement inside it is cheap in absolute
terms. **Placement precision matters in proportion to the secret's share of the
dynamic instruction stream** — which is a cleaner statement of the design
constraint than "regions must be big".

---

## 5. Two things this does NOT establish

**Coverage — RESOLVED 2026-08-16, the pass does not under-protect.** The concern
was real: the oracle wraps the entire `secp256k1_ecdsa_sign` call blindly, the
pass protects only what taint reached, and a placement that is faster *because
it covers less* looks exactly like a win — the failure that produced and then
retracted SQLCipher's "+8.15%". Settled under gem5 by counting DIT-covered
dynamic work directly. See §7.

**The pass over-approximates, visibly.** The placement census:

| function | switches | touches the secret? |
|---|---|---|
| `secp256k1_ecdsa_verify` | **30** | **no** |
| `secp256k1_ecdsa_sign` | 17 | yes |
| `nonce_function_rfc6979_impl` | 11 | yes |
| `secp256k1_ec_pubkey_tweak_mul` | 9 | no |
| `secp256k1_ecmult_gen_blind` | 8 | context setup only |
| `secp256k1_der_parse_integer` | 7 | no |
| `secp256k1_ecdsa_signature_parse_compact` | 6 | no |

**Verification takes no secret and carries the largest single block of
switches.** It is free in these workloads only because neither host calls
verify. A Bitcoin node — which validates far more signatures than it produces —
would pay for all 30, and this would look very different. That is a precision
bug (context-insensitive mod-sets, `docs/design/context-insensitivity.md`), not
a placement bug, and it is now the highest-value precision target.

---

## 6. Status change

`docs/overview.md` still says "no measured workload yet justifies fine-grained
placement". **That is now superseded** on two hosts, with the pass rather than an
oracle, subject to §5's coverage verification.

---

## 7. gem5 coverage check — the pass protects at least as much as the oracle

**Measured 2026-08-16**, `gem5.opt` (`--eves --dmp --comp-simp`), driver
`utils/dit_host_screening/gem5cov/cov_driver.c`, 50 signatures in the ROI.

Wall time cannot distinguish "placed well" from "protected less". gem5 can, by
counting DIT-covered dynamic work directly:
`compSimplifier.ditSuppressed` (simplifications actually blocked by DIT) and
`valuePredictor.ditTaggedSet` (DitCC instructions seen with DIT set).

The driver is minimal by design — just the signing path — because the coverage
question is entirely about what taint reached inside `secp256k1_ecdsa_sign`. All
arms are the same source; only placement differs.

| arm | simInsts | `ditSuppressed` | `ditTaggedSet` |
|---|---|---|---|
| `off` | 13,268,748 | **0** | **0** |
| `always` | 13,268,748 | 3,005,423 | 26,745,727 |
| `oracle` | 13,268,898 | 3,033,037 | 26,707,027 |
| `pass_region` | 13,271,048 | 3,067,777 | 26,559,921 |
| `pass_hoist` | 13,270,948 | 3,073,180 | 26,888,969 |

**Coverage relative to the hand oracle:**

| arm | `ditSuppressed` | `ditTaggedSet` |
|---|---|---|
| `pass_region` | **101.1%** | 99.4% |
| `pass_hoist` | **101.3%** | 100.7% |
| `always` | 99.1% | 100.1% |

**Verdict: no under-protection.** Both pass placements cover the same secret work
as the hand oracle, to within ~1% — and that ~1% is the counters' own noise, not
a coverage gap (both are counted at dispatch *and* commit, so speculation moves
them; `lvp.hh` says so explicitly of `ditTaggedSet`). The §2 wall-time win is
therefore a real placement result, not an artifact of protecting less.

Gates:
- **`off` reads exactly 0 on both counters** — the counters genuinely track DIT,
  so a null result would have been distinguishable from a broken rig
  (`dit-measurement-traps` trap 5).
- **Instruction counts agree across arms to 0.02%**, so the counts are
  comparable (runbook gate 4).
- **First stats dump parsed, not the last** (trap 9): the ROI dump is 13.3M
  instructions, the teardown dump is 2,983.

**Simulator build note.** These arms were first run on `gem5.opt`, because
`gem5.fast` was stale (built 2026-08-07, predating `SIPPrefetcher`, so it could
not even import `fdp_neoverse_v2_binary.py`). `gem5.fast` was rebuilt
2026-08-16 and **reproduces all five arms bit-identically** — same `simInsts`,
same `ditSuppressed`, same `ditTaggedSet`, to the digit. The coverage conclusion
does not depend on which simulator binary is used, and the matrix runner
(`util/run_dit_matrix.sh`, which is sized around `gem5.fast`) is usable again.

One thing to read carefully: **`always` ≈ `oracle` here, which is expected and
is not a contradiction of §2.** This driver is ~99% signing by construction, so
there is almost no public work for blanket protection to additionally cover. In
the composite hosts the public work dominates, which is where always-on's +9.99%
/ +3.47% comes from.

---

Raw data: `utils/dit_host_screening/pass/pass_results.csv`,
`utils/dit_host_screening/gem5cov/`.
