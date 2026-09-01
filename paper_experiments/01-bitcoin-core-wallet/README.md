# 01 - Bitcoin Core wallet

**Status: complete, both instruments.** Measured 2026-08-31 on Apple M5
(Mac17,2) and gem5 Neoverse-V2 FDP.

**Published artifact:** https://claude.ai/code/artifact/692f3b7d-18fe-4707-ab8a-3d5b84478c12
Source: `figures/crossover.html`. To update the page, republish **that URL**
(`Artifact` with `url=...`); publishing the file without the URL creates a
second artifact instead of updating this one.

---

## The claim

> Whether selective `PSTATE.DIT` placement pays is decided by the fraction of
> dynamic computation that is secret - and that fraction is a measurable
> property of a workload, before any compiler work is done.

Demonstrated inside **one unmodified call** into shipped Bitcoin Core wallet
code (`CreateTransaction`), containing a real public lane and a real secret lane
in the order the wallet runs them, with one integer varying the ratio.

## Headline results

| quantity | value |
|---|---|
| pass beats blanket, low f | **-2.89%** at f = 5.6% (1/10 reps slower, p < 0.05) |
| verdict flips | **f ~ 45%** (indistinguishable), blanket wins outright by f = 61% |
| blanket DIT costs, silicon | **-3.7% to -4.0% of IPC**, flat across f = 3.5%..75.2% |
| blanket DIT costs, gem5 | IPC **1.9559 -> 1.7737**, cycles **+10.27%** on the coin-selection kernel |
| the same at 4x the work | IPC **1.9492 -> 1.7578**, cycles **+10.89%** - the prize is not a one-shot artifact |

**The mechanism**: blanket's cost is flat in f because it protects everything
either way; what grows is the pass's toggle bill (+0.20% -> +6.08% switch cost).
The crossover is a fixed prize meeting a linearly growing cost.

## What is public and what is secret

| lane | code | why |
|---|---|---|
| **public** | `AvailableCoins`, the four coin-selection solvers, `CalculateMaximumSignedTxSize` (dummy signer), serialization | never reads the private key; amounts and UTXOs are on-chain data |
| **secret** | `CKey::Sign` -> `secp256k1_ecdsa_sign`, once per input | operates on the private key |

Taint seeds are the **pointee of the private-key pointer argument** at nine
entry points (`../../utils/dit_host_screening/btc/seed9.txt`), not whole
functions.

The public lane's prize is attributed by gem5 feature isolation entirely to
**EVES**, the value predictor: coin selection is a serial value-dependent
dependence chain, and DIT switches the predictor off.

## Contents

| path | what |
|---|---|
| `table1.md` | Table 1 - the crossover, with column definitions and caveats |
| `ipc.md` | what blanket DIT costs in IPC, silicon and gem5, and the method |
| `data/wallet_m5_ipc.csv` | 320 rows = 4 knob points x 8 arms x 10 reps (M5) |
| `data/wallet_sweep_20rep.csv` | 1280 rows = 8 knob points x 8 arms x 20 reps (M5) |
| `figures/crossover.html` | source of the published artifact above |

## Reproducing

Silicon. **Requires an exclusive machine** - nothing else may run during a
native timing measurement.

```sh
BTC_SWEEP_INPUTS=1,4,100,400 BTC_OUT=wallet_m5_ipc.csv \
  python3 utils/dit_host_screening/btc/run_wallet_sweep.py 10 1

python3 utils/dit_host_screening/btc/table_wallet_sweep.py \
  paper_experiments/01-bitcoin-core-wallet/data/wallet_m5_ipc.csv
python3 utils/dit_host_screening/btc/ipc_wallet_sweep.py \
  paper_experiments/01-bitcoin-core-wallet/data/wallet_m5_ipc.csv
```

gem5, for absolute IPC. **Pass the arguments explicitly** - the driver's
defaults (`--iter 50 --warmup 2 --targets 10`) are ~500x the work and do not
reproduce these numbers:

```sh
python3 utils/dit_host_screening/btc/btc_gem5.py --bench coinsel \
  --configs spec,serdit --iter 1 --warmup 1 --targets 1 --tag coinsel_repro
```

## Validity gates, all passed

in-band `lvp_chase` **3.99x** · noise floor -0.00%..+0.30% · harness null
-0.04%..+0.34% · NOP arm carries zero `msr DIT` · arithmetic closes (implied
C_secret +4.52%, spread 0.60 pp vs +3.39% measured) · gem5 `simInsts` identical
across switch models, `ditSuppressed = 0` in base · equal-length `argv[0]`.

## Known limits

1. **Region count and secret fraction are confounded** - each signature adds
   both. Separating them needs the `BTC_BENCH_CHAIN` axis; not yet run.
2. **The region boundary is inside libsecp256k1**, per `secp256k1_ecdsa_sign`
   call, not at the C++ `SignTransaction` phase boundary the design doc
   describes - this build instruments C only. Measurement valid, prose wrong.
3. **Closure is acceptable, not tight** (+4.52% vs +3.39%).
4. **gem5 cannot confirm magnitude here** - it reads ~zero on ECDSA signing.
5. **Older `coinsel` / `coinsel4` gem5 stats predate the equal-length-path
   gate.** Both have been re-taken (`coinsel_repro`, `coinsel4_repro`); quote
   the gated numbers in `ipc.md`, not the originals in `gem5-btc/coinsel*/`.
