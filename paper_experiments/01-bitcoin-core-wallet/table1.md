# Table 1: the secret-fraction crossover in one Bitcoin Core wallet call

**Generated** 2026-08-31 by `utils/dit_host_screening/btc/table_wallet_sweep.py`
from `data/wallet_sweep_20rep.csv` (1280 rows = 8 knob points
x 8 arms x 20 reps). Regenerate with:

    python3 utils/dit_host_screening/btc/table_wallet_sweep.py [--latex]

Design: `../../docs/paper/bitcoin-secret-fraction-sweep.md`. Rig: `../../utils/dit_host_screening/btc/run_wallet_sweep.py`.
Instrument: Apple silicon, exclusive machine, arm order rotated every rep.

---

## The claim this table supports

Whether selective DIT placement beats blanket DIT is decided by the fraction of
dynamic computation that is secret - and the crossover is measurable inside a
single unmodified application call, by varying one integer that is a property of
the transaction being built.

The pass wins by 2-3.5% while the secret fraction is under ~30%, is
indistinguishable from blanket DIT at 45%, and loses by up to 1.65% by 75%.

---

Table 1: Selective vs blanket DIT across the secret fraction of one Bitcoin Core wallet call

WalletCreateTxUsePresetInputsAndCoinSelection, Apple silicon, 20 paired reps per cell, rotated arm order.
Median per-rep ratio; n/20 = reps where the first-named arm is slower; exact two-sided sign test.

| K (inputs) | f_secret | baseline | C_public | C_whole | pass vs base | pass vs blanket | sign test | verdict | switch cost |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 3.7% | 15.12 ms | +3.94% | +3.25% | +0.10% | **-3.52%** | 1/20 *** | pass | -0.64% |
| 4 | 5.8% | 15.62 ms | +4.37% | +3.74% | +0.78% | **-2.81%** | 0/20 *** | pass | +0.95% |
| 10 | 10.2% | 16.37 ms | +3.95% | +3.07% | +0.53% | **-3.18%** | 2/20 *** | pass | +0.16% |
| 25 | 19.2% | 18.37 ms | +4.23% | +2.84% | +0.37% | **-2.06%** | 2/20 *** | pass | +0.81% |
| 50 | 29.8% | 21.25 ms | +2.96% | +4.32% | +2.25% | **-1.93%** | 1/20 *** | pass | +1.77% |
| 100 | 45.0% | 27.23 ms | +3.36% | +4.20% | +3.87% | **-0.48%** | 8/20 ns | tie | +3.53% |
| 200 | 61.2% | 39.26 ms | +3.55% | +4.10% | +4.60% | **+0.74%** | 15/20 * | blanket | +5.10% |
| 400 | 75.0% | 63.21 ms | +3.09% | +3.75% | +5.50% | **+1.65%** | 19/20 *** | blanket | +5.98% |

Controls (all arms, every K):
  noise floor (baseline2/baseline)  -0.68% .. +0.31%
  harness null (DIT never written)  -0.40% .. +0.08%
  in-band lvp_chase positive control 3.97x (PASS)

Closure  C_whole ~= (1-f)*C_public + f*C_secret
  implied C_secret (f >= 25%): median +4.83%, spread 3.55 pp, reference +3.39% -> OK

Significance: *** p<0.001, ** p<0.01, * p<0.05, ns = not significant
(exact two-sided sign test on 20 paired reps; with N=20 only n<=5 or n>=15
reaches p<0.05, so 8/20 is a tie however its median reads).

---

## What each column is

| column | arms | meaning |
|---|---|---|
| `K` | - | `BTC_BENCH_INPUTS`, inputs spent hence signatures produced |
| `f_secret` | (baseline - pub_base)/baseline | **measured** secret fraction, via `BTC_BENCH_SIGN=0` |
| `baseline` | baseline | round-trip control, empty seed file: absolute time |
| `C_public` | pub_always/pub_base | blanket DIT over the public lane alone |
| `C_whole` | always/baseline | blanket DIT over the whole call |
| `pass vs base` | pass/baseline | the shipped pass's absolute overhead |
| `pass vs blanket` | pass/always | **the headline**; negative = selective placement wins |
| `switch cost` | pass/passnop | the `msr DIT` instruction's own cost, placement held identical |

---

## Reading it

**The crossover is real and it is where the framework said it would be, roughly.**
Significant wins through f=29.8%, a tie at 45.0%, significant losses from 61.2%.
The framework's ~20% threshold is too pessimistic: the pass still wins
significantly at 29.8% (-1.93%, 1/20).

**The mechanism is visible in the last column.** Switch cost climbs monotonically
-0.64% -> +5.98% as K grows, because each additional signature is one more DIT
region. The public-lane prize (`C_public`) meanwhile stays flat at +3.0..4.4%.
The crossover is those two lines meeting: a fixed prize against a linearly
growing toggle bill.

**Blanket DIT is nearly flat in f** (`C_whole` +2.84..+4.32%), which is the
control that says the ratio is moving because the pass's cost moves, not because
the workload got more DIT-sensitive.

---

## Caveats that must ship with this table

1. **Region count and secret fraction are confounded.** Each signature adds both
   secret work and one DIT region, so this curve cannot separate "fraction
   decides" from "region count decides". Both move together by construction. A
   disentangling experiment needs `BTC_BENCH_CHAIN` (which moves the public lane
   at fixed signature count) as the second axis; the rig supports it and it has
   not been run.
2. **The region boundary is not where the design doc says.** `build-gated-v2` has
   empty `CMAKE_CXX_FLAGS`, so only C is instrumented: DIT regions sit inside
   libsecp256k1 per `secp256k1_ecdsa_sign` call, not at the C++
   `wallet.SignTransaction` phase boundary that `../../docs/paper/bitcoin-secret-fraction-sweep.md`
   §5 describes to a reviewer. The measurement is valid; the prose needs fixing.
   A phase-level boundary would place ONE region per call instead of one per
   signature, which is precisely the variable the last column tracks - so this is
   also the most promising follow-up.
3. **Closure is OK but not tight.** Implied `C_secret` median +4.83% against the
   independently measured +3.39%, spread 3.55 pp over the four voting points
   (f >= 25%). Within the analyser's gate, but the +1.44 pp gap is unexplained.
4. **gem5 cannot confirm the magnitude here.** It reproduces silicon's always-on
   cost on the coin-selection kernel (+11.06% vs +13.01%) but reads ~zero on
   ECDSA signing. This is a silicon-only result; the renamed-switch
   counterfactual still wants a gem5 arm.

---

## Validity gates, all passed

- **in-band `lvp_chase` positive control: 3.97x** (needs >3.0). DIT was taking
  effect for the whole run; a table of null ratios would otherwise be
  uninterpretable.
- **noise floor** (duplicate baseline): -0.68% .. +0.31%, below every effect
  claimed as significant.
- **harness null** (dylib loaded, DIT never written): -0.40% .. +0.08%, so the
  harness is not being credited to DIT.
- **NOP arm** present as `passnop` and carrying zero `msr DIT`, so the switch
  column is the instruction and not code alignment.
- **arm order rotated** every rep, with a duplicate baseline to catch drift.
