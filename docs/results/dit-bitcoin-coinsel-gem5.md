# gem5 reproduces Bitcoin CoinSelection's always-on DIT cost: +11.1% against silicon's +13.0%

**Measured 2026-08-26.** Silicon: Apple M5, exclusive machine, 15 paired reps,
arm order rotated, baseline CoV 1.17%. gem5: `gem5-DIT` at `66e6c23690`,
Neoverse V2 FDP config, deterministic, four selections per ROI.
Rigs: `utils/dit_host_screening/btc/run_sign_ecdsa.py` (with
`BTC_BENCH=CoinSelection`), `utils/dit_host_screening/btc/btc_gem5.py --bench coinsel`.

---

## Bottom line

**gem5 sees the prize silicon sees.** Always-on DIT on Bitcoin Core's coin
selection costs **+13.01% on silicon (15/15 reps)** and **+11.06% in gem5** --
85% of it, on a different microarchitecture.

This matters beyond the number, because it settles what gem5 can be used for.
`docs/paper/evaluation-framework.md` §5 says "gem5 understated always-on 4.6x on
the one workload measured both ways" and concludes silicon answers *how much*
while gem5 answers only *which placement is better*. There are now two workloads
measured both ways, and on this one gem5 understates by **1.23x**, not 4.6x.

The rule that actually holds is narrower and more useful:

> gem5 reproduces always-on DIT's cost on code whose DIT sensitivity is real --
> serial, branchy, value-predictable work. It reports ~zero on workloads where
> silicon also struggles to resolve a cost. It is not uniformly pessimistic; the
> 4.6x figure is a property of the workload it was measured on, not of gem5.

---

## 1. The measurement

| | silicon (Apple M5) | gem5 (Neoverse V2) | gem5 / silicon |
|---|---|---|---|
| **CoinSelection**, blanket DIT | **+13.01%** (15/15) | **+11.06%** | **0.85** |
| SignTransactionECDSA, blanket DIT | +3.39% (26/40, marginal) | -1.11% | - |

gem5, four selections per ROI, both switch models:

| arm | switches | spec | serdit |
|---|---|---|---|
| base | 0 | 112,257,299 cyc | 112,257,299 cyc (+0.000%) |
| blanket | 1 (constructor, pre-ROI) | **+11.06%** | **+11.07%** |

Stable in the ROI length: one selection reads +10.60%, four read +11.06%.

The two switch models agreeing to 0.01pp is the expected result and a useful
internal check -- blanket executes its single `msr DIT` in a constructor before
the ROI, so `--no-speculative-dit` has nothing to rewrite inside the measured
region.

Silicon, same day, same compiler, six arms:

| arm | median vs baseline | reps slower |
|---|---|---|
| null (harness) | -0.03% | 7/15 |
| **always** (blanket DIT) | **+13.01%** | **15/15** |
| pass (96 switches) | +0.16% | 9/15 = noise |
| passnop (same placement, NOPs) | -0.40% | 4/15 = noise |
| baseline2 (noise floor) | +0.06% | 8/15 |

**pass vs always: -11.82%, 0/15.** The pass beats blanket DIT in every rep.
It places nothing on this benchmark's executed path -- coin selection is C++
compiled by the system compiler, and the pass instruments only the C
libsecp256k1 that coin selection never calls -- so it pays none of blanket's
cost and keeps the whole prize.

In-band `lvp_chase` 3.98x. Baseline CoV 1.17%, versus 22.3% on
SignTransactionECDSA: this is by far the cleaner of the two benchmarks and its
medians can be quoted without hedging.

---

## 1b. The prize is one mechanism: EVES

DIT can only cost what the mechanisms it disables are worth, so run
blanket-vs-base with one mechanism enabled at a time (gem5, one selection):

| mechanism | base cycles | blanket | DIT cost |
|---|---|---|---|
| none | 33,742,398 | 33,729,874 | -0.04% |
| **EVES** (value predictor) | **30,464,081** | 33,668,894 | **+10.52%** |
| DMP | 33,738,536 | 33,738,736 | +0.00% |
| comp-simp | 33,737,241 | 33,743,484 | +0.02% |
| SIP | 33,742,398 | 33,729,874 | -0.04% |

**All of CoinSelection's DIT cost is the value predictor.** EVES makes coin
selection 10.8% faster (33.74M -> 30.46M cycles) and DIT hands back exactly that;
DMP, computational simplification and SIP contribute nothing. The `none` row is
the control -- with nothing for DIT to suppress it reads -0.04%, so the other
rows are the mechanism and not layout.

Why this workload and not the crypto: `SelectCoinsBnB` derives both its next
address (`utxo_pool[curr_selection.back()]`) and its next branch
(`curr_amount + lookahead[...] < selection_target`) from values just loaded. That
serial value-dependent chain is what a value predictor exists for.

Contrast the whole-workload figures in `dit-cost-model.md` (EVES +1.44% on
SQLCipher, DMP +1.89% on SPEC intspeed, SIP +0.78% on gapbs): coin selection is
not a 1-2% workload, and it is not a mixture -- it is 10.5 points of one
mechanism.

Note `--sip` reproduces `none` exactly here, so SIP does nothing for this code.

---

## 2. What the gem5 workload is

`benchmarks/bitcoin/btc_coinsel_gem5.cpp` in `gem5-DIT`, built by
`build_coinsel_arms.sh`. `bench_bitcoin` itself cannot run in gem5 SE mode --
boost, libevent, sqlite, a thread pool and a filesystem -- but what CoinSelection
*measures* is `AttemptSelection`, and that reduces to four solvers taking plain
`vector<OutputGroup>`, needing no wallet and no chain.

Real, unmodified, from `src/wallet/coinselection.cpp`: `SelectCoinsBnB`,
`CoinGrinder`, `KnapsackSolver`, `SelectCoinsSRD`. The UTXO pool is the
benchmark's own: 400 coins, same biased amount distribution, same four output
types in the same proportions, same fee rule, from Bitcoin's `FastRandomContext`
in deterministic mode. Link closure is 16 Bitcoin sources plus a stub TU.

Fidelity: the driver runs at **~2.8x** the benchmark's cycles per selection
(30.5M vs ~11.0M). Same algorithms, same pool, somewhat more work. Quote the
ratio, not the absolute cycles.

### Two ways to get this wrong, both hit here

**`AttemptSelection` selects per output type.** It runs `ChooseSelectionResult`
once per type and returns the best; it falls back to the mixed all-coins pool
*only if every type fails to fund the target*. With 400 coins and these targets
no type ever fails, so the benchmark never runs the all-400 case. A first version
of this driver ran only that case and did **20.8x** the benchmark's work per
selection. The driver now mirrors the per-type structure and carries a
`mixed_fallbacks` counter that must read 0; it does.

**nanobench reports `ns/selection`, not `ns/batch`.** The benchmark declares
`.batch(NUM_TARGETS)`, and nanobench divides by the batch. CoinSelection's
3,134,612 is the cost of **one** selection, not ten. Reading it as a batch makes
the silicon reference 10x too small and every fidelity comparison meaningless.
The table header says which; read it.

---

## 3. The control that failed, and the gate it produced

The gem5 rig now enforces: **the switch model must be a no-op on any arm with no
executed DIT switch.** `base` is cycle-identical between `spec` and `serdit`
(delta +0.0000%).

That gate exists because it failed. An earlier driver selected blanket DIT at
runtime from a `DIT_MODE` env var, so that base and blanket could be one binary
-- attractive, because it removes the codegen difference between them. But it put
an `msr DIT` inside `main()`, a few blocks from the ROI loop, and then:

- `nodit` moved **0.549%** between switch models while executing no switch at all
- the DMP reported **19** fills dropped as "issued in a DIT region" in a run with
  DIT off

A control arm that responds to the knob under test bounds nothing. The blanket
switch now lives in a constructor in its own translation unit, so `base`,
`nodit` and `taint` contain no `msr DIT` beyond what the pass placed.

Gates, all enforced in `btc_gem5.py` rather than documented: `simInsts` identical
across switch models; `ditSuppressed` = 0 in unprotected arms; switch model a
no-op on arms with no switch; checksums identical across every run.

---

## 4. Reading it with the signing result

The two Bitcoin benchmarks bracket the framework's decision rule inside one
application, and both instruments agree on which side each falls:

| | secret fraction | blanket DIT | the pass | verdict |
|---|---|---|---|---|
| CoinSelection | 0% | +13.01% (15/15) | +0.16% (noise) | **pass wins by 11.82%, 0/15** |
| SignTransactionECDSA | ~100% | +3.39% (26/40) | +7.71% (34/40) | **pass loses by 4.12%** |

Where public DIT-sensitive work dominates, blanket pays +13% and the pass keeps
nearly all of it. Where the workload is entirely secret, blanket is close to
free, there is no prize to recover, and the pass can only pay for the switches it
inserts -- which the NOP control prices at +6.56% of a +7.71% total.

See `dit-bitcoin-sign-two-instruments.md` for the signing half.

---

## Reproducing

```
# gem5 arms (in gem5-DIT)
benchmarks/bitcoin/build_coinsel_arms.sh

# gem5
python3 utils/dit_host_screening/btc/btc_gem5.py --bench coinsel \
        --iter 1 --warmup 1 --targets 4

# silicon
BTC_BENCH=CoinSelection BTC_OUT=coinsel_native.csv \
  python3 utils/dit_host_screening/btc/run_sign_ecdsa.py 15 1
python3 utils/dit_host_screening/btc/analyze_sign_ecdsa.py \
        utils/dit_host_screening/btc/coinsel_native.csv
```
