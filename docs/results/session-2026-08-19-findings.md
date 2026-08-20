# Session findings, 2026-08-15 → 2026-08-19

Consolidated record. Each section links the detailed doc. Measurements are on
Apple M5 unless marked gem5.

---

## 1. The headline: secret fraction decides everything

**Same pass, same crypto library, opposite verdicts — the denominator is the
variable.**

| workload | secret fraction | pass vs always-on |
|---|---|---|
| coincurve / eth-account (silicon, 40 reps) | **19.5%** | **+2.74% WORSE** |
| gem5 SQLite+ECDSA composite | **2.23%** | **−2.66% BETTER** |
| Bitcoin Core `CoinSelection` (pure public) | **0%** | **−11.07% BETTER**, 0/15 slower |

At high secret fraction most of always-on's cost is unavoidable (it is DIT over
genuinely secret work) and the pass's toggle overhead swamps the remainder. At
low fraction the public work dominates and the pass recovers most of it.

**This relationship, measured on both sides, is a stronger contribution than
"fine-grained beats always-on."** Docs: `dit-secret-fraction-decides` (memory),
`dit-gem5-composite.md`, `dit-coincurve-timing.md`.

---

## 2. Host screening — where the prize is (`dit-host-screening.md`)

Same binary, DIT injected at load, 12 reps, controls in-band.

| host | workload | always-on DIT |
|---|---|---|
| Lua 5.4.7 | binary-trees | **+14.52%** |
| CPython 3.16 | pyperformance ×4 | **+7.00%** |
| SQLite | speedtest1 | **+6.09%** |
| git 2.53 | rev-list, 580k commits | +2.48% |
| QuickJS | Octane subset | +1.08% |

**Calibration: QuickJS read +1.08% against the project's own documented +1.05%**
on an independent harness — the reason to believe the other four rows.

The old "+4.4% unencrypted SQLite" note was real and understated: **+6.09%**.

### Mechanism (`dit-host-screening.md` §4)
Lua micro, 200M iterations: serial FP accumulator **+32.39%** vs the same work
with four independent accumulators **+20.06%**; adding a float divide changes
nothing (+33.93% / +19.53%). **The serial chain is worth 12–14 points, the
divide zero — but parallel guest code still pays +20%**, which is the
interpreter's own dispatch loop, a chain the guest cannot break. Condition (d)
is therefore a property of the HOST, not the guest workload.

I predicted spectralnorm would be ~0% and it was the highest (+26%); I had
mislabelled a loop-carried FP accumulator as parallel.

---

## 3. Bitcoin Core (`dit-bitcoin-core-screen.md`)

Selected by candidate survey as the one application clearing every structural
criterion. **`assembly ... OFF`** verified at configure time, so the whole crypto
path is portable C reachable by the pass. Nine entry points, all in
`src/key.cpp`, all direct calls, indices verified 0-based against the headers.

Microbenchmark screen, 15 reps, control 3.87x:

| benchmark | class | always-on | pass | pass vs always |
|---|---|---|---|---|
| CoinSelection | public | +14.45% | +0.16% | **−11.07%** (0/15) |
| TxGraphTrim | public | +6.27% | −0.97% | **−8.12%** (0/15) |
| ComplexMemPool | public | +2.41% | +0.70% | −2.74% |
| **ConnectBlockAllEcdsa** | verify | **−0.02%** | **+50.98%** | **+51.08%** |
| SignTransactionECDSA | secret | +0.10% | +39.36% | +37.02% |
| SignTransactionSchnorr | secret | +1.93% | +43.49% | +40.18% |

**The pass wins decisively where taint is correct and loses catastrophically
where taint is wrong.** `ConnectBlockAllEcdsa` is signature *verification* —
public data only — where blanket DIT costs −0.02% and the pass costs +51%,
because `secp256k1_ecdsa_verify` carries **17 switches** it should not have.

**`-reindex-chainstate` over real mainnet (200k blocks, 7.3M txs) is
INCONCLUSIVE, not null**: CoV 6.7–7.3%, and the null arm reads +3.12%, so the rig
cannot resolve a few-percent effect on an I/O-bound workload. Fix by moving the
datadir to a RAM disk.

---

## 4. Serializing vs renamed `MSR DIT` — both negatives are toggle-bound

The fork models `MSR DIT` two ways. Same binary, two machine configs (the
trap-7b-immune measurement).

| case | serializing | renamed | reduction |
|---|---|---|---|
| over-toggling placement (`relaxed` arm) | +3.09% | +0.24% | **13x** |
| false positives in `ecdsa_verify` (no secret at all) | +6.07% | +1.60% | **3.8x** |
| toggle-thin placement (composite, 128 switches) | +0.57% | +0.47% | none |

**Rule: renaming the switch rescues placements that over-toggle; it does not
improve one that is already toggle-thin.**

I initially asserted the precision cost would NOT be rescued. That was wrong —
always-on on that code costs −0.02%, proving dwell was zero and toggles were the
only remaining term. Do not extrapolate magnitudes between gem5 (Neoverse-like)
and M5.

**Upstream gem5 cannot model this at all** (`dit-upstream-gem5.md`): `msr DIT,
#1` is an unknown instruction and panics; `compSimplifier`/`EVESValuePredictor`
exist only in the fork. Upstream reproduces the fork's non-DIT baseline
*exactly* (simInsts 13,268,748 both), so the fork's modelling is additive.

---

## 5. Compiler work

**Relaxed callee ownership** (`dit-relaxed-ownership.md`, flag
`-taint-dit-relaxed-ownership`, default off). Local-linkage instrumented callees
no longer clear DIT, so callers skip the re-assert. Re-assert sites **48 → 19**;
switches 575 → 289; pass overhead halved (+10.81% → +5.63%). Coverage verified
unchanged (gem5 `ditSuppressed` 101.2% of oracle) and no DIT leak
(`dit_after=0`). Still loses to always-on, so not enabled by default.

**Function cloning** (branch `dit-clone`). IR-level `CloneFunction` makes
`foo.dit` copies that emit NO switches — the one place eliding the entry enable
is safe, because nothing else can name the symbol. MIR pass redirects DIT-on
call sites. Re-assert sites **48 → 6**; signing region **+45.18% → +9.82%**
against the oracle's +6.50%.

**Upstream LLVM bug found and fixed.** `-ftaint-harden` failed on every `-g`
build: `MIRPrinter::printStackObjectDbgInfo` concatenates multiple
`VariableDbgInfo` entries for one frame index with no separator
(`debug-info-variable: '!1335!1338'`), which its own parser rejects. Triggered by
StackColoring merging slots with disjoint lifetimes. **This is upstream LLVM, not
the fork** — reproduced under plain `llc -run-pass=none`, 15-line C repro. Fixed
with comma-separated lists in printer and parser (120 insertions, 4 files, new
lit test, no YAML schema change). Verified: failing command compiles, 148 `msr
DIT` with `-g` == 148 without, dwarfdump clean, taint tests 28/28, CodeGen +
DebugInfo 4363 pass / 0 fail. Fixed clang at `~/Documents/llvm-project/build-gfix`.

---

## 6. The constant-time / DIT interaction (`dit_inv_bench_README.md`)

libsecp256k1's **constant-time** safegcd inversion costs **+23.3%** under DIT;
its **variable-time** sibling costs **+0.24%**. Cause proven causally, not
inferred: `modinv64_impl.h:176` declares `volatile uint64_t c1, c2` — the
standard idiom for stopping the compiler re-introducing branches — which forces
a store+reload every iteration, ~1180 per inversion, on the serial chain, from a
fixed stack slot, returning one of two repeating values. The LVP was covering it;
DIT switches the LVP off.

Replacing it with `__asm__ volatile("" : "+r"(c))` makes the code **22% faster
outright AND DIT-neutral**, zero memory ops in the loop, upstream tests pass at
64 iterations with `-DVERIFY`.

**Generalisable claim: constant-time software and DIT are not additive.** The
idiom that makes crypto constant-time manufactures exactly the value-predictable
loads DIT de-optimizes, so the hardened implementation is penalised ~100x more
than its unhardened sibling.

**BEEA itself is NOT a DIT workload** — its leak is iteration count and branch
direction (CVE-2016-7056), and ARM DIT's covered list contains only CFINV and NOP
under "Branches". Third false-assurance case after SDIV/FP and AES T-tables.

---

## 7. Literature position

- **Apple corecrypto is the reference implementation of selective DIT**: 508
  scope-guard sites across 269 files, and its re-entrancy comment is verbatim the
  callee-ownership rule. Apple is **actively removing nested toggles**. AWS-LC has
  181 sites.
- **Vendors say fine-grained is the intent and ship no tooling.** Intel: *"does
  not recommend enabling this mode globally… only for software specifically
  designed to benefit"*, and *"performance impact may be significantly higher on
  future processors."* Dave Hansen: *"DOITM itself is dead."*
- **OpenBSD forces DIT on for every process** claiming *"no measurable impact on
  performance"*, with no data — contradicted by every number here.
- **Closest prior art: "Let's DOIT" (TCHES 2025).** But it enforces the
  instruction SUBSET, not the mode bit; its +65% ChaCha20-ref is subset cost, not
  toggle cost. Do not conflate.
- **Selective-hardening analogues** (oo7 TSE'21 5.9% vs a **430%** global
  baseline; SpecFuzz USENIX'20 3% vs 22%, JSMN 5×/11×) argue exactly our thesis
  — **but at 100x the headroom.** Ours is 1–15%.
- **The project's `dit-cost-model.md` appears to be the only cycle-level
  measurement of DIT toggle cost in existence.** Everything public is qualitative.

---

## 8. Methodology — traps hit and gates adopted

**Trap 8 (under-protecting oracle) bit THREE times**, each caught by an
arithmetic inconsistency between region-level and whole-program numbers:
1. Seeded `ecdsa_sign` when eth-keys uses `sign_recoverable`.
2. Missed `ec_pubkey_create`/`keypair_create` — `eth_keys` rebuilds a
   `PrivateKey` per signature, so key derivation is ~half the secret work.
3. Built a gem5 composite against the oracle-patched secp256k1 tree, so the
   "baseline" ran oracle placement (847,278 suppressions in an arm where DIT is
   never set).

> **Detector, now standard: measure the protected REGION and the WHOLE PROGRAM in
> the same run and check the arithmetic closes.** Whole-program timing alone
> cannot distinguish "placed well" from "placed somewhere irrelevant."

**New trap: fixed arm order** penalises whichever arm runs last (~1.3%,
demonstrated with a zero-switch arm). Rotate, and keep a duplicate baseline arm.

**Two gem5 gates, both exact and free, both failed first time:**
1. `simInsts` must be IDENTICAL across machine configs. It was not — the driver
   called `clock_gettime`, and gem5 SE returns *simulated* time, feeding cycle
   counts back into control flow.
2. The unprotected arm must report ZERO `compSimplifier.ditSuppressed`.

**Native silicon runs must be exclusive.** gem5 is deterministic and safe to run
alongside anything; native is not. I violated this and had to discard a run.

**Insensitive ≠ null.** The CPython/SQLite "pass reaches the oracle" result
(+9.99% → −0.08%) is real but was measured at a **0.02% secret fraction**, where
placement quality cannot matter. Both that doc and its memory now carry warnings.

---

## 9. Open items

1. **Precision is now the dominant engineering problem**, not placement.
   `ecdsa_verify` (17 switches), `ecdsa_recover` (14), `musig_*`, `ec_pubkey_serialize`
   carry switches with no secret. Context-insensitive mod-sets.
2. Bitcoin Core reindex on a RAM disk to resolve the end-to-end number.
3. Sweep Bitcoin Core's secret-fraction knob (`-assumevalid`, wallet send rate)
   to draw the curve in one real application.
4. Upstream the MIRPrinter fix.
5. Propose the `volatile` → register-barrier fix to libsecp256k1.
