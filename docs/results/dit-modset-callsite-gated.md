# Gating the mod-set on the call site: 3.7x fewer switches, no coverage lost

**Implemented and measured 2026-08-19.** New flag
`-mllvm -taint-modset-callsite-gated`, default **off**. It is the "cheaper
stopgap worth measuring first" from `docs/design/context-insensitivity.md`.

---

## Bottom line

**This changes the verdict on Bitcoin Core.** Over 30 paired reps the pass goes
from losing to blanket DIT on 5 of 9 benchmarks - by **+28% to +51%** - to
**5 wins, 2 ties and 2 losses, neither loss over 3 points**.

`-taint-modset-gate-strict` narrows the unsoundness and is on by default with the
gate; it is byte-identical in output on every library measured (§6b).

**Do not pair it with `-taint-frame-addr-args`.** The two are antagonistic, not
complementary: the fallback's whole-frame taint makes the gate's predicate true
almost everywhere and the false positives come straight back (+45.32% on
`ConnectBlockAllEcdsa` against the gate's +0.66%). §6 retracts the opposite
recommendation an earlier revision of this document made from static counts.

On Bitcoin Core's vendored libsecp256k1 the flag removes **73% of the pass's
mode switches** and **99.99% of the DIT it was applying to code with no secret
in it**, while dynamic coverage on the signing path stays at **103% of the hand
oracle**. Under gem5 it closes almost the entire placement gap to the oracle:
**+6.80% -> +0.13%** on signing, serializing switches. On silicon, signature
verification goes from **+51.20% to +0.67%**.

| | base (`region`+hoist) | **gated** | oracle |
|---|---|---|---|
| `MSR DIT` in the secp256k1 TU | 660 | **178** | - |
| `MSR DIT` in `bench_bitcoin` | 541 | **178** | - |
| functions carrying >=1 switch | 73 | **18** | - |
| toggles executed per signature | 386 | **112** | 3 |
| toggles executed per verification | 506 | **2** | 0 |
| signing coverage (`ditSuppressed`, % of oracle) | 104.4% | **103.1%** | 100% |
| verification suppression (all of it waste) | 6,062,413 | **80** | 0 |

**It is not sound in general** - see §6 - which is why it ships off by default.

---

## 1. What the flag changes

A `FunctionMemEffects` mod-set is computed per **function**. Once any caller
passes a secret into a shared helper, that helper's mod-set goes TOP, and the
pass replays that TOP at **every** call site of it, poisoning callers that passed
nothing secret. The flag keeps the summary context-insensitive but makes its
*application* context-sensitive: the clobber fires only where the call site
actually passes a secret, matching how the external/indirect path already gates
on `HasTaintedArg`.

Two of the three mod-set applications are gated - `WritesSecretToUnknown` (TOP)
and `WritesSecretThroughArgPointee`, the two that set `ExternalMemClobbered` and
flood every subsequent load. **`WritesSecretToGlobal` is deliberately left
ungated**: it is already per-global rather than a flood, and it is exactly the
case ("the callee got the secret from a global, not from this caller") that the
gate is otherwise unsound for.

The suppression reaches the transitive re-export in `computeFunctionMemEffects`
too - a caller that absorbs no clobber re-exports none - which is what stops the
cascade rather than hiding its last hop. So the flag changes **summaries**, not
only codegen.

Diff: one `cl::opt` plus two guarded blocks in `TaintAnalysis.cpp`. Lit test
`llvm/test/CodeGen/AArch64/taint-analysis-modset-callsite-gated.mir` builds the
two-caller shared-helper shape and checks both directions: the public caller goes
clean, the secret caller is untouched.

---

## 2. Static effect on Bitcoin Core's libsecp256k1

Same TU, same nine seeds (`btc/seed9.txt`), same flags as the `build-hoist` that
measured +51% on `ConnectBlockAllEcdsa`.

**Every seeded entry point keeps its switches:**

| function | base | gated |
|---|---|---|
| `secp256k1_ecdsa_sign` | 3 | 3 |
| `secp256k1_ecdsa_sign_recoverable` | 3 | 3 |
| `secp256k1_schnorrsig_sign32` | 1 | 1 |
| `secp256k1_ec_pubkey_create` | 6 | 5 |
| `secp256k1_keypair_create` | 6 | 5 |
| `secp256k1_ec_seckey_tweak_add` | 6 | 5 |
| `secp256k1_ellswift_create` | 17 | 17 |
| `secp256k1_ellswift_xdh` | 8 | 8 |
| `secp256k1_ec_seckey_verify` | 4 | 4 |

**and so does the signing machinery underneath them:**
`ecdsa_sign_inner` 13->13, `schnorrsig_sign_internal` 21->21,
`nonce_function_bip340_impl` 20->20, `schnorrsig_challenge` 18->18,
`sha256_finalize` 15->15, `nonce_function_rfc6979_impl` 11->9,
`scalar_set_b32` 2->2.

**The false positives go to zero:**

| function | base | gated | what it is |
|---|---|---|---|
| `secp256k1_ecdsa_verify` | 17 | **0** | signature verification, public |
| `secp256k1_ecdsa_recover` | 14 | **0** | public |
| `secp256k1_musig_pubkey_agg` | 14 | **0** | public |
| `secp256k1_keypair_xonly_tweak_add` | 13 | **0** | public |
| `secp256k1_ec_pubkey_serialize` | 12 | **0** | public |
| `secp256k1_ecmult` | 20 | **0** | the *variable*-time multiply - verify's |
| `secp256k1_schnorrsig_verify` | 9 | **0** | public |
| `secp256k1_musig_*`, `silentpayments_*` | 158 | **0** | public |

55 of the 73 instrumented functions stop being instrumented at all.

---

## 3. Static soundness check

`-taint-uncovered-report` on both builds: base emits 644 entries, gated 46 - and
**the gated set is a strict subset**. `comm -13` gives **zero** entries present
in gated but not base, i.e. the flag introduces no new self-reported gap.

That is necessary, not sufficient: the uncovered report measures gaps *relative
to the taint the analysis computed*, so it shrinks when taint shrinks. Dynamic
coverage is what settles it.

---

## 4. gem5: dynamic coverage and cost

`compSimplifier.ditSuppressed` counts operations DIT actually blocked, so it *is*
the coverage. Neoverse-V2 config, `--eves --dmp --comp-simp`, 40 iterations,
driver `utils/dit_host_screening/modset/g5/mod_driver.c`. Two workloads: signing
(secret present) and verification (no secret anywhere - the fixture is built
before `m5_reset_stats`).

Gates: `simInsts` accounted for in every arm; checksums identical across all
arms of each workload; `dit_after=0` everywhere (no DIT escape); the `off` arm
reports exactly 0 suppressions; `off` and `always` are **bit-identical between
the two machine configs**, as they must be since neither executes a switch
inside the ROI.

### 4a. Signing - the coverage gate

| arm | simInsts | cycles (ser) | `ditSuppressed` (ser) | % of oracle |
|---|---|---|---|---|
| `off` | 17,851,690 | - | 0 | - |
| `oracle` (2 switches/sig) | 17,851,810 | 6,611,048 | 4,467,738 | 100% |
| `base` | 17,867,130 | 7,060,516 | 4,665,976 | 104.4% |
| **`gated`** | 17,856,170 | **6,619,781** | **4,608,037** | **103.1%** |

**No coverage loss.** Gated keeps 98.8% of base's suppression and still exceeds
the hand oracle by 3.1%, while cutting toggles per signature from **386 to 112**
and landing **+0.13%** from the oracle on cycles, against base's **+6.80%**.

Why coverage survives a 71% cut in switches: the switches that disappeared were
inside functions the caller **already holds DIT across**. `secp256k1_ecmult_gen_gej`
(8->0) and `ecmult_gen_blind` (8->0) run under `ecdsa_sign_inner`'s region, which
keeps all 13 of its switches. Their own switches were redundant re-assertions,
not protection - and `ditSuppressed` falling only 1.2% while they vanished is the
measurement that proves it rather than assuming it.

### 4b. Verification - the false-positive cost

No secret exists anywhere in this workload, so **every switch and every
suppression here is pure waste**.

| arm | cycles, speculative | vs `off` | cycles, serializing | vs `off` | `ditSuppressed` |
|---|---|---|---|---|---|
| `off` | 4,193,326 | - | 4,193,326 | - | 0 |
| `always` | 4,220,932 | +0.66% | 4,220,932 | +0.66% | 7,085,999 |
| `base` | 4,413,078 | **+5.24%** | 4,674,650 | **+11.48%** | 6,062,413 |
| **`gated`** | 4,246,930 | **+1.28%** | 4,252,916 | **+1.42%** | **80** |

Blanket DIT costs verification **+0.66%** - that code has almost no DIT
sensitivity, which is why the pass's cost here was never protection, only
toggling. The gate takes the serializing penalty from **+11.48% to +1.42%**, an
**8.1x** reduction, and leaves gated within 0.76 points of blanket DIT on code
that needs none.

The two machine configs agree on the mechanism: base executes 506 toggles per
verification, and the serializing penalty over speculative works out to ~13
cycles per switch on verification and ~20-26 on signing, consistent across arms.

### 4c. One modeled effect I cannot yet explain

On the **signing** workload in the speculative config the DIT-on arms are
*faster* than `off` (`always` −1.03%, `oracle` −0.75%). The plausible mechanism
is that DIT disables load-value prediction, and on this workload LVP's
misprediction recovery costs more than its hits save - but that is an
**inference, not a measurement**. It is also why §4a leans on `ditSuppressed`
rather than cycles for the coverage claim, and why cross-binary cycle deltas
under ~1% here should not be read as real.

---

## 5. Native silicon: Bitcoin Core

**Apple M5, two independent 15-rep runs, machine exclusive, arm order rotated
per rep.** Run 1 (`btc_gated.csv`) has 6 arms; run 2 (`btc_fa.csv`, §6) repeats
all of them and adds the two frame-address arms. Numbers below are run 1 unless
marked pooled.
`bench_bitcoin`, same nine seeds. `baseline` is `build-nodit`
(`-ftaint-harden=<empty>`, 0 switches) so the MIR round-trip is controlled for,
not stock -O2. `pass_hoist` = `build-hoist` (541 switches), `pass_gated` =
`build-gated` (178). Rig: `utils/dit_host_screening/btc/run_btc_gated.py`.

Gates: in-band `lvp_chase` control **3.87x**; harness (`null`) arm within
±1.41% on every benchmark; noise floor (`baseline` vs `baseline2`) median
−0.16%, worst 1.95%.

### Median % vs baseline

| benchmark | `always` | `pass_hoist` | **`pass_gated`** | baseline CoV |
|---|---|---|---|---|
| ConnectBlockAllEcdsa | −0.14% | +51.20% | **+0.67%** | 0.56% |
| SignTransactionECDSA | +7.30% | +44.58% | **+8.32%** | 4.71% |
| SignTransactionSchnorr | +1.84% | +46.80% | **+4.69%** | 0.39% |
| WalletAvailableCoins | +1.43% | +33.80% | **−0.07%** | 1.88% |
| WalletCreateTxUseOnlyPresetInputs | +1.58% | +33.47% | **+4.91%** | 6.07% |
| CoinSelection | +13.43% | −0.94% | **+0.31%** | 1.16% |
| TxGraphTrim | +8.38% | +0.81% | **−0.00%** | 2.08% |
| ComplexMemPool | +3.05% | +0.40% | **−0.33%** | 2.10% |
| WalletCreateTxUsePresetInputsAndCoinSelection | +3.12% | +1.46% | **+0.05%** | 4.14% |

### The comparison that decides it: vs blanket always-on DIT

**Pooled across both runs - 30 paired reps.** The second run (§5b) repeated
`baseline`/`null`/`always`/`pass_hoist`/`pass_gated`/`baseline2` unchanged, so
their per-rep ratios pool legitimately. Negative means the pass beats blanket DIT.

| benchmark | median | IQR | reps slower | |
|---|---|---|---|---|
| CoinSelection | **−12.38%** | −12.58 .. −11.13 | 0/30 | **win** |
| TxGraphTrim | **−6.55%** | −8.21 .. −5.75 | 0/30 | **win** |
| ComplexMemPool | **−3.02%** | −3.74 .. −2.08 | 1/30 | **win** |
| WalletCreateTxUsePresetInputsAndCoinSelection | **−2.87%** | −3.77 .. −1.84 | 0/30 | **win** |
| WalletAvailableCoins | **−1.80%** | −3.22 .. −0.28 | 7/30 | **win** |
| SignTransactionECDSA | +3.12% | −1.53 .. +7.79 | 20/30 | tie |
| WalletCreateTxUseOnlyPresetInputs | +3.49% | −1.37 .. +9.14 | 21/30 | tie |
| ConnectBlockAllEcdsa | +0.58% | +0.43 .. +0.86 | 27/30 | loss |
| SignTransactionSchnorr | +2.69% | +2.54 .. +2.81 | 30/30 | loss |

**5 wins, 2 ties, 2 losses - and both losses are under 3 points.** Compare
`pass_hoist` on the same runs: 4 wins and 5 losses of +28% to +51%.

The two ties are the two noisiest benchmarks in the set (baseline CoV 4.2-4.8%),
and their run-to-run spread is what makes them ties rather than results:
`SignTransactionECDSA` read +1.29% then +5.63%, `WalletCreateTxUseOnlyPresetInputs`
+6.81% then +2.01%. Neither should be quoted as a number.

### Reproducibility

`pass_gated` vs baseline, the two independent runs, median % per benchmark:

| benchmark | run 1 | run 2 | delta |
|---|---|---|---|
| ConnectBlockAllEcdsa | +0.67% | +0.66% | −0.01 |
| TxGraphTrim | −0.00% | +0.02% | +0.03 |
| SignTransactionSchnorr | +4.69% | +4.60% | −0.10 |
| SignTransactionECDSA | +8.32% | +8.45% | +0.13 |
| WalletAvailableCoins | −0.07% | +0.11% | +0.18 |
| WalletCreateTxUsePresetInputsAndCoinSelection | +0.05% | +0.52% | +0.47 |
| CoinSelection | +0.31% | −0.20% | −0.51 |
| ComplexMemPool | −0.33% | +0.58% | +0.91 |
| WalletCreateTxUseOnlyPresetInputs | +4.91% | +3.94% | −0.97 |

Worst disagreement 0.97 points, on the noisiest benchmark. Both runs: control
3.87x, harness within ±1.78%, noise floor median −0.13%.

### gem5 predicted the silicon residual

On verification, gem5-serializing put `gated` at +1.42% and `always` at +0.66%
over `off` - a predicted gap of **+0.76 points**. Silicon measured
**+0.61 points** (14/15 reps). Two independent instruments agreeing to 0.15
points on a sub-1% residual is the strongest cross-validation this project has
produced, and it is the residual §6 attributes to `scalar_set_b32`.

---

### 5c. The alignment control: the +51% is switches, not code layout

**The standard objection to this whole result**, and it has published support:
Marinaro et al. (AsiaCCS 2024) found on ARM that removing provably-unnecessary
hardening gave no improvement or ~10% *regressions*, and traced it to **code
alignment** - substituting NOPs for the removed instructions recovered the
performance, so the hardening had never been the cost.

`-mllvm -taint-dit-nop-switches` (hidden, default off) is the control. It emits
`HINT #0` in place of every inserted `MSR DIT`. `build-nopctl` vs `build-hoist`:
same instruction count (1,790,954), same binary size, and **every instruction at
the same address** - verified by diffing the full address column of both
disassemblies. Same dynamic instruction count, same layout, no mode switching.

> **!! Those three properties do not by themselves prove the control works, and
> checking only them is how an inert one hides.** Same instruction count, same
> size and same addresses are all satisfied by a NOP arm that is
> BYTE-IDENTICAL to its twin, which is exactly what happens when the flag never
> takes effect. **The check has two halves and both are needed:** the NOP arm
> must contain **zero `msr DIT`**, and it must still be the **same size** as its
> twin. Either half alone can be satisfied by a broken build.
>
> This is not hypothetical. The crossover rig had precisely that bug:
> `-taint-dit-nop-switches` is consumed at EMISSION
> (`AArch64AsmPrinter::emitInstruction`), and `build_arm` passed it only to the
> analysis stage of its two-stage llc pipeline, so every NOP arm came out
> byte-identical and the control was inert while looking like it passed. Fixed
> by `xover: fix the NOP arms, which were never NOPed`, which added the
> two-part gate.
>
> **The result below is unaffected**, and was re-verified rather than assumed.
> Bitcoin Core builds through clang's `-ftaint-harden`, not that script, and the
> flag reaches emission there: rebuilt on `dit-tainter` at `d04695a`, the
> default arm carries 19 `msr DIT` and the NOP arm carries 0 at the same binary
> size, with identical address columns and `d503415f msr DIT, #0x1` replaced in
> place by `d503201f nop`. The measured 51-point separation between the arms is
> independent evidence, since byte-identical binaries cannot differ by 51
> points.

| benchmark | `nop_ctl` vs base | `pass_hoist` vs base | `hoist` vs `nop_ctl` |
|---|---|---|---|
| ConnectBlockAllEcdsa | **−0.09%** (5/15) | +51.37% | **+51.56%** (15/15) |
| SignTransactionSchnorr | −0.12% (4/15) | +46.95% | +47.24% (15/15) |
| WalletAvailableCoins | +0.22% (8/15) | +35.66% | +34.79% (15/15) |
| WalletCreateTxUseOnlyPresetInputs | −0.25% (7/15) | +31.45% | +28.48% (15/15) |

**Attribution on `ConnectBlockAllEcdsa`: layout −0.2%, switches 100.2%.** Every
other affected benchmark falls between −0.8% and +0.6% layout. The alignment
effect does not apply here, and removing the switches recovers baseline *exactly*
on an otherwise byte-identical binary.

**Substitute at emission, not at insertion.** The first attempt replaced the
switch in `insertTimingModeSwitch` and drifted 400 instructions: the pass's
whole-function fallback finds switches by opcode, so with NOPs it cannot erase
them and adds more on top. Doing it in `AArch64AsmPrinter::emitInstruction`
leaves the MI stream untouched, so placement, fallback and the coverage verifier
all make exactly the decisions of the arm being controlled for; `MSR` and
`HINT #0` are both 4 bytes, so nothing downstream moves. **Adopt this control for
any future "we removed hardening and got faster" claim.**

---

## 6. Where it is unsound, and what it does not fix

**Unsound case.** A callee can write a secret it obtained from a **global** or
from a **previous call** rather than from this caller's arguments. Such a write
no longer reaches a caller that passes nothing secret. Leaving
`WritesSecretToGlobal` ungated covers the common shape of this (a secret parked
in a global), but not a secret reaching the callee through a global *pointer* it
dereferences, nor one carried in callee-owned state across calls. This is a
strictly larger hole than the shipped default has, and it is why the flag is off.

**What it does not reach: the argument half of context-insensitivity.** Gated
still executes exactly **2 switches per verification**, and they are all in
`secp256k1_scalar_set_b32` - which `secp256k1_ecdsa_verify` calls on the *public*
message hash (`secp256k1.c:485`) and which signing calls on the *secret* nonce.
Its summary carries `PointeeTaintedArgIndices` from the signing call sites and
replays it at the verifying ones. That is the same context-insensitivity in the
**register/argument** summary rather than the mod-set, and this flag does not
touch it. It is now the largest remaining false-positive source, and P1b or real
context-sensitivity is what reaches it.

---

### 6b. Taint origin, and `-taint-modset-gate-strict`

The gate is unsound when the callee's secret arrived from somewhere other than
this caller's arguments. `-taint-modset-gate-strict` narrows it: suppress a
callee's clobber only when that callee has **at least one tainted argument** -
i.e. when a caller's arguments can speak for its secret at all. **On by default
whenever the gate is on**; `=0` restores the permissive rule for A/B.

It is a proxy, not a proof: a callee could take a secret argument *and* read a
secret global. The sound rule is an origin bit computed in the fixed point ("all
of this function's taint originates from its own arguments"). This flag exists to
price that direction before building it.

**It is free.** On Bitcoin Core's libsecp256k1 and on coincurve's, strict and
permissive produce **byte-identical objects** (178 and 39 switches respectively),
and the linked `bench_bitcoin` binaries disassemble identically. Every timing
number in §4 and §5 therefore applies to the strict rule verbatim - measuring it
separately would be timing the same binary twice.

**It is not vacuous.** `taint-analysis-modset-gate-strict.mir` builds the shape
where it matters and shows all three behaviours: a callee that receives its secret
from a shared helper's `ReturnsTainted` (applied unconditionally by design) has a
secret-writing mod-set with an *empty* tainted-argument set; the permissive gate
drops that clobber and loses a real secret, strict keeps it.

**Correcting an intermediate claim.** I first reported that 9 of the 26 suppressed
mod-set clobbers (35%) were this unsound shape. That number was measuring the
wrong thing: it came from differencing the clobber reports of the two builds,
which mixes predicate suppressions with *second-order* disappearances - callees
whose mod-sets go empty once the flood feeding them is removed. All nine were the
second kind. In the gated build `ecmult_gen_blind`, `der_parse_integer`,
`context_preallocated_create`, `musig_nonce_gen_internal` and
`musig_pubkey_tweak_add_internal` report **zero** clobber sites, not suppressed
ones: their "secret" was itself a product of the flood. On these libraries the
permissive gate never fires on an argument-less callee, which is why strict costs
nothing.

**What is still open.** Strict narrows the hole; it does not close it. A callee
with both a secret argument and a secret from elsewhere is still gated. Closing
that needs the origin bit, and that is what would let the gate itself default on.

---

### Interaction with `-taint-frame-addr-args`: they are antagonistic

**Measured 2026-08-19, and it retracts an earlier claim in this document.** An
earlier revision recommended `+frame-addr +gate` as the shipping configuration on
the strength of static switch counts (404 vs the fallback's 975). **That was
wrong.** Static counts fall; the dynamic false positives do not.

gem5, verification workload, where no secret exists and every suppression is
waste:

| arm | `ditSuppressed` | toggles/verify | cycles vs `off` (ser) | (spec) |
|---|---|---|---|---|
| `always` | 7,085,999 | 0 | +0.66% | +0.66% |
| `base` | 6,062,413 | 506 | +11.48% | +5.24% |
| **`gated`** | **80** | **2** | **+1.42%** | **+1.28%** |
| `fa` | 7,046,230 | 676 | +15.70% | +4.27% |
| **`fagated`** | **6,042,126** | **616** | **+14.33%** | +4.48% |

Native M5, same run as §5, `ConnectBlockAllEcdsa`: `hoist` +51.14%, **`gated`
+0.66%**, `fa` +48.28%, **`fagated` +45.32%**. The gate recovers **33.35%** on
its own and **1.92%** with the fallback on. Turning the fallback on costs
**+44.43%** (15/15 reps) relative to the gate alone.

**Why: the fallback breaks the gate's own predicate.** The gate asks whether this
call site passes a secret in an argument *register*. The fallback taints frame
addresses on a **whole-frame** approximation, so `secp256k1_ecdsa_verify` passing
`&m` to `scalar_set_b32` (`secp256k1.c:485`) now looks like a secret-passing call
site; the clobber goes through, the flood taints more of the frame, and the next
call site looks secret too. The fallback makes `HasTaintedArg` true nearly
everywhere, which is exactly the condition under which the gate does nothing.

The direction of the earlier reasoning was right - the fallback *does* stop the
gate from firing at genuine `&secret_local` sites, and `+frame-addr +gate` does
restore `rfc6979_hmac_sha256_initialize` (0 -> 53 switches), `ecmult_gen_gej`,
`ecmult_const` and `scalar_split_lambda`. The error was checking only what it
restores and never what else it lets through.

`fagated` is also the **only** configuration whose signing coverage lands below
the hand oracle - 4,461,707 vs 4,467,738, or 99.86%. Small, but it is on the
wrong side of the line and it is alone there, while executing **500 toggles per
signature** against `base`'s 386 and `gated`'s 112.

**Conclusion: ship the gate alone**, and note that on a workload whose false
positives are never executed it is cloning, not the gate, that pays
(`dit-coincurve-timing.md` §6) - the two attack different terms and do not stack.

The `&secret_local` under-taint the fallback
exists to close is real, but the fallback costs more than the flood it replaces
and disables the precision fix. Closing that gap properly is **P1b** - apply
`WritesSecretThroughArgPointee{i}` to the object the caller passed for argument
*i*, which gives per-object precision instead of whole-frame taint and would not
trip the gate's predicate.

Two functions dropped relative to today's default do take secret keys -
`ec_seckey_tweak_mul` and `ec_seckey_negate` - but neither is in the nine-seed
set, so today's build covers them only by flood. That is an
**annotation-completeness** question, not a regression, and worth deciding
deliberately rather than inheriting by accident.

---

## 7. Status and what to do next

Default **off**, and the honest reason it stays off is §6's unsoundness, not the
performance - on performance it is now the best placement the pass has produced.

Verified: full `CodeGen/AArch64` + `CodeGen/MIR` lit suites pass (4,293 tests);
the pass's own soundness verifier (fatal gate) passed on every build; the new lit
test covers both directions; two independent native runs agree to within 0.97
points on every benchmark.

**Next, in order of value:**

1. **P1b** - apply `WritesSecretThroughArgPointee{i}` to the object the caller
   passed for argument *i*. It is the sound version of this stopgap, and it is
   also the only way to close the `&secret_local` under-taint without the
   frame-address fallback's whole-frame blunt taint, which §6 shows costs more
   than it buys.
2. **The argument-summary residue** - `scalar_set_b32` replaying signing's
   `PointeeTaintedArgIndices` at verification's call sites. 2 switches per
   verification, and now the largest false-positive source left.
3. ~~Re-run the coincurve workload with the gate.~~ **Done 2026-08-19
   (`dit-coincurve-timing.md` §6): the verdict there is UNCHANGED.** The gate cuts
   that build 575 → 39 switches - a bigger static cut than Bitcoin Core's - and
   moves it only from +8.40% to +6.04% against always-on, because that workload
   signs and never verifies, so it never *executed* the false positives. Cloning
   still wins there (+2.77%), the two do not stack (+2.75% combined), and the
   oracle's entire prize is 0.83%. **The gate pays where a real application runs
   the code the pass over-instruments, which is what separates a node from a
   signing library.**

Raw data: `utils/dit_host_screening/modset/` (rig, reports, `g5/gem5_stats.csv`)
and `utils/dit_host_screening/btc/{btc_gated,btc_fa}.csv` with
`run_btc_{gated,fa}.py` / `analyze_{gated,fa}.py`.
