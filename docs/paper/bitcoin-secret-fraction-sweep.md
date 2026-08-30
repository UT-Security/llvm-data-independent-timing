# Benchmark design: the secret-fraction sweep inside one Bitcoin Core wallet call

**Status: the design, with the knob measured working.** Written 2026-08-26.
The sweep itself has not been run under the full rig; §7 says what that requires.

This is benchmark 4 of `evaluation-framework.md` §6, made into a curve. It is the
exemplar the framework asks for -- a real public lane, a real secret lane, and a
knob that moves only the ratio -- with one property the SQLCipher exemplar does
not have: **both lanes are inside a single unmodified call to shipped wallet
code, in the order the wallet runs them.**

---

## 1. What coin selection is doing

`SelectCoinsBnB` (`src/wallet/coinselection.cpp`) is a depth-first
branch-and-bound over UTXOs sorted descending, carrying an index stack
`curr_selection` and a suffix-sum array `lookahead`:

```cpp
OutputGroup& utxo = utxo_pool[next_utxo];           // address from search state
curr_amount += utxo.GetSelectionAmount();           // loaded value -> accumulator
if (curr_amount + lookahead[curr_selection.back()] < selection_target)
    should_cut = true;                              // branch on loaded values
```

Both the next address and the next branch derive from values just loaded. It is
a serial, value-dependent dependence chain -- the shape a value predictor exists
for, and the reason this benchmark is DIT-sensitive where crypto is not.
`ChooseSelectionResult` runs four such solvers per output type: BnB, CoinGrinder,
KnapsackSolver, SelectCoinsSRD.

### The prize is one mechanism, and we know which

gem5 feature isolation on the coin-selection kernel, blanket DIT vs base
(`docs/results/dit-bitcoin-coinsel-gem5.md`):

| mechanism | base cycles | blanket | DIT cost |
|---|---|---|---|
| none | 33,742,398 | 33,729,874 | -0.04% |
| **EVES** (value predictor) | **30,464,081** | 33,668,894 | **+10.52%** |
| DMP | 33,738,536 | 33,738,736 | +0.00% |
| comp-simp | 33,737,241 | 33,743,484 | +0.02% |
| SIP | 33,742,398 | 33,729,874 | -0.04% |

**All of it is EVES.** Value prediction makes coin selection 10.8% faster
(33.74M -> 30.46M cycles); DIT disables it and hands back exactly that. Nothing
else contributes. The `none` row is the control: with no mechanism for DIT to
suppress it reads -0.04%, so what the other rows measure is the mechanism and
not layout.

This is the strongest form of the framework's condition (d) -- the prize is not
just present, it is attributed to a single named mechanism.

**Design consequence.** The public lane's prize lives *inside* a serial
dependence chain that a predictor is breaking. An interleaved secret region must
sit outside that chain. Wrapping DIT around a signing call is harmless; toggling
inside the BnB loop would not merely cost switches, it would destroy the chain
the prize is made of, and the experiment would be measuring its own
perturbation.

---

## 2. The flow already exists

`CreateTransactionInternal` (`src/wallet/spend.cpp`) runs, in this order:

```
AvailableCoins()                   enumerate the wallet's UTXOs        PUBLIC
SelectCoins() -> four solvers      the +13% lives here                 PUBLIC
CalculateMaximumSignedTxSize()     DUMMY signatures, no key            PUBLIC
wallet.SignTransaction(txNew)      CKey::Sign per input                SECRET
```

Selection completes entirely before any real key is touched, and the size
estimate deliberately uses `DummySignatureCreator`. The secret work is therefore
**one contiguous phase at the end**, entered through the nine `src/key.cpp`
entry points the pass already seeds.

Nothing here is constructed. The only quantity we vary is the ratio.

---

## 3. The knobs

Three environment variables in `src/bench/wallet_create_tx.cpp`. All default to
the benchmark's shipped values, so an unset environment reproduces the numbers
this benchmark has always reported.

| variable | default | moves |
|---|---|---|
| `BTC_BENCH_INPUTS` | 4 | inputs spent, hence signatures -> **secret work** |
| `BTC_BENCH_CHAIN` | 5000 | blocks, hence UTXO pool size -> **public search** |
| `BTC_BENCH_SIGN` | 1 | 0 skips the real signatures -> **isolates the secret lane** |

`num_of_internal_inputs` was already a parameter of the benchmark, pinned at 4;
`BTC_BENCH_INPUTS` unpins it. `BTC_BENCH_SIGN` forwards `CreateTransaction`'s own
`sign` parameter, the path `fundrawtransaction` and PSBT creation use -- so
`sign=0` is a shipped code path, not a benchmark-only branch.

Having two knobs on opposite lanes is worth more than one: moving f_secret from
the secret side (`INPUTS`) and from the public side (`CHAIN`) should trace the
same curve, and disagreement is a defect the single-knob design cannot detect.

---

## 4. f_secret is measured, not modelled

This is the part that makes the design defensible, and it exists because the
obvious version of it failed.

**The trap.** `BTC_BENCH_INPUTS` looks like a clean secret-fraction knob: more
inputs, more signatures. It is not obviously one, because each added input also
carries public work -- a dummy signature for size estimation, script solving,
serialization. A knob that moves both lanes is exactly what
`evaluation-framework.md` §2.3 rejects (it is why SQLCipher's database size was
rejected as a knob: it also changes B-tree depth).

**The resolution.** Run every point twice, with `BTC_BENCH_SIGN=1` and `=0`.
Then

    f_secret(K) = ( t[sign=1] - t[sign=0] ) / t[sign=1]

is a *measurement* of the secret lane at that knob setting, taken with the
public lane held byte-identical. No modelling, no assumed per-signature cost.
This is the same discipline as the framework's "measure the protected REGION and
the WHOLE PROGRAM in the same run and check the arithmetic closes".

**Measured, M5, 3-rep medians** (a demonstration that the knob functions, *not*
a measurement -- no arm rotation, no round-trip control, no in-band probe):

| `BTC_BENCH_INPUTS` | sign=1 | sign=0 | **f_secret** | blanket DIT |
|---|---|---|---|---|
| 4 (shipped) | 14.81 ms | 14.21 ms | **4.00%** | +6.22% |
| 25 | 17.82 ms | 14.39 ms | **19.30%** | +4.24% |
| 100 | 27.33 ms | 14.63 ms | **46.47%** | +2.75% |
| 400 | 62.54 ms | 15.53 ms | **75.17%** | +4.53% |

Two things to read off it.

**The public lane is nearly flat**: 14.21 -> 15.53 ms while the secret lane grows
14x. So the added per-input work is overwhelmingly the real signature after all,
and `INPUTS` is a far cleaner knob than the trap above feared. But that is
something the `sign=0` arm *established*; without it the +2.33%/+6.17%/+4.72%/
+4.48% blanket-DIT series looked non-monotonic and inexplicable.

**One integer spans 4% to 75%**, crossing both thresholds the framework cares
about -- the ~3% below which nearly all of always-on is recoverable, and the ~20%
above which the prize collapses. Transactions with 100-400 inputs are ordinary;
consolidation sweeps routinely have more.

Expected shape, for the sweep to confirm or refute:

    C_blanket(f) ~= (1 - f)*C_public + f*C_secret

with C_public ~ +6% (this call's selection-dominated end) and C_secret ~ +3%
(`SignTransactionECDSA`, itself only marginally resolvable at 26/40). The three-
rep series is consistent with that and far too noisy to claim it.

---

## 5. Why the flow maps

The claim to a reviewer is not "we built a synthetic mix". It is:

> One unmodified call into a shipped wallet, with one integer varied that is a
> property of the transaction being built. The DIT region boundary falls on
> `SignTransaction` -- a function boundary in shipped code, not one we
> introduced.

That boundary is what makes the region coarse enough to be worth protecting:
signing is tens of microseconds of work per call, four orders of magnitude above
the ~1300-cycle floor from the granularity sweep, and it sits at a direct call
that loop hoisting can reach.

It also means the benchmark tests the *placement policy* and not the pass's
ability to find an obscure boundary. If selective DIT cannot win here, the
failure is the idea, not the implementation.

---

## 6. What this benchmark can and cannot settle

**Can**: whether the crossover predicted by f_secret happens where the framework
says it does, inside one application, with the pass unchanged.

**Cannot**: the magnitude question on gem5 alone. gem5 reproduces silicon's
always-on cost on the coin-selection kernel (+11.06% vs +13.01%) but reads ~zero
on ECDSA signing, where silicon is itself marginal. Run the sweep on silicon for
magnitude and on gem5 for the renamed-switch counterfactual, per
`evaluation-framework.md` §5 as revised.

---

## 7. Running it

Not yet run under the full rig. A real sweep needs the six-arm treatment already
built for the other two Bitcoin benchmarks -- round-trip baseline, null arm,
rotated arm order, duplicate baseline, in-band `lvp_chase` -- at every knob
setting, and both `sign` values at every point:

```
for K in 4 25 100 400; do
  for S in 1 0; do
    BTC_BENCH_INPUTS=$K BTC_BENCH_SIGN=$S \
    BTC_BENCH=WalletCreateTxUsePresetInputsAndCoinSelection \
    BTC_OUT=wallet_K${K}_S${S}.csv \
      python3 utils/dit_host_screening/btc/run_sign_ecdsa.py 25 1
  done
done
```

Budget, measured rather than feared: one invocation at `-min-time=2000` costs
**2.7 s** at K=4 and 2.7 s at K=400, of which the 5000-block chain regeneration
is a **0.7 s** floor. So setup does not dominate. Eight knob settings x eight
arms x 2.7 s is ~176 s per rep, and 20 reps plus a burn-in is about **an hour**
on an exclusive machine.

Do not lower `BTC_BENCH_CHAIN` to save time *within* a curve -- it moves the
public lane, which is the other axis.

Note also that `BTC_BENCH_INPUTS` is bounded by the wallet's coins of the chosen
output type (9800 at the default chain size); the benchmark aborts with a message
rather than silently spending fewer, because a sweep that under-spent would
report the wrong secret fraction.

---

## Sources

`docs/results/dit-bitcoin-coinsel-gem5.md` (the EVES isolation, gem5 vs silicon),
`docs/results/dit-bitcoin-sign-two-instruments.md` (the secret-lane endpoint and
the NOP control), `docs/results/dit-bitcoin-core-screen.md` (the original screen),
`evaluation-framework.md` §2.3 (knob criteria) and §5 (which instrument answers
which question).
