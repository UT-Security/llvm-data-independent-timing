# What blanket DIT costs in IPC: Apple M5 and gem5

**Measured 2026-08-31.** Silicon: `data/wallet_m5_ipc.csv`
(320 rows = 4 knob points x 8 arms x 10 reps). gem5: `~/Documents/dit-browser-bench/
gem5-btc/coinsel_repro/`. Regenerate with `ipc_wallet_sweep.py` and
`btc_gem5.py --bench coinsel --iter 1 --warmup 1 --targets 1`.

---

## 1. Absolute IPC is not measurable on this machine

macOS exposes no usable PMU: no `perf`; `kpc`/`kperf` are private and root-only;
this box has Command Line Tools, so no `xctrace`. Retired-instruction and cycle
counts cannot be read on the M5, so **absolute silicon IPC is unavailable**. Any
absolute IPC figure in this project comes from gem5.

## 2. The IPC *change* is exact anyway, with no counter

The `baseline` and `always` arms are **the same binary** (`build-nodit-v2`),
differing only by `DYLD_INSERT_LIBRARIES=dit_on.dylib`, which sets `PSTATE.DIT`
process-wide. Blanket DIT changes no instruction. So with `I` fixed:

    IPC_blanket / IPC_base = C_base / C_blanket = t_base / t_blanket
                           = 1 / (1 + C_whole)

gem5 confirms the premise to the instruction, on three separate runs of this
code: `numInsts = 59,697,945` and `simInsts = 59,678,264`, **identical** in base
and blanket, at both switch models.

The one assumption is equal clock between arms. The duplicate-baseline arm
bounds it: two instruction-identical arms read -0.00%..+0.30% across this run,
which is the uncertainty on each figure below.

## 3. Silicon: blanket DIT costs 3.7-4.0% of IPC, flat in secret fraction

`WalletCreateTxUsePresetInputsAndCoinSelection`, M5 (Mac17,2), 10 paired reps,
rotated arm order.

| K | f_secret | no DIT | blanket | time cost | **IPC change** | +/- drift | n/N |
|---|---|---|---|---|---|---|---|
| 1 | 3.5% | 15.39 ms | 16.00 ms | +4.09% | **-3.93%** | 0.30% | 10/10 |
| 4 | 5.6% | 15.78 ms | 16.39 ms | +4.08% | **-3.92%** | 0.00% | 9/10 |
| 100 | 44.6% | 27.76 ms | 28.84 ms | +3.88% | **-3.73%** | 0.16% | 10/10 |
| 400 | 75.2% | 64.23 ms | 66.90 ms | +4.19% | **-4.02%** | 0.21% | 10/10 |

**The flatness is the result.** Blanket DIT's cost does not depend on how much of
the work is secret, because it protects everything either way. That is the fixed
denominator selective placement must beat, and it is why the crossover is driven
by the pass's toggle bill rather than by blanket getting cheaper or dearer.

Gates: noise floor -0.00%..+0.30%, harness null -0.04%..+0.34%, in-band
`lvp_chase` **3.99x**, closure OK (implied `C_secret` +4.52%, spread 0.60 pp).

## 4. gem5: absolute IPC, and the number that should be quoted

Coin-selection kernel, Neoverse-V2 FDP, `--iter 1 --warmup 1 --targets 1`,
**equal-length argv[0] via the `armlink` slots** (gate 3 of `btc_gem5.py`):

| arm | switch model | IPC | cycles | vs base |
|---|---|---|---|---|
| base | either | **1.955873** | 30,522,406 | - |
| blanket | spec | **1.773742** | 33,656,493 | +10.27% |
| blanket | serdit | 1.773147 | 33,667,788 | +10.31% |

**IPC -9.31% (spec) / -9.34% (serdit); cycles +10.27%.**

Switch model is a no-op here (+0.034% between spec and serdit) because the
blanket arm takes its switch from a constructor before `main`, never inside the
measured region. That is by design, and gate 3 confirms it on the base arm at
+0.0000%.

The ECDSA-signing arms, for contrast, read IPC 2.7131 -> 2.7292, i.e. **no DIT
sensitivity at all** in gem5 - the secret lane has nothing for DIT to suppress.

### The previously committed coinsel numbers carry a path-length confound

`gem5-btc/coinsel/` was produced **before** `canon_path` existed, passing
`--binary .../btc_coinsel_base` and `.../btc_coinsel_blanket` directly. Those
paths differ in length, which is `dit-gem5-rig-traps` #5: argv[0] length shifts
stack alignment for the whole run, worth up to 0.84% on a byte-identical binary.

| quantity | old (unequal paths) | new (equal paths) | shift |
|---|---|---|---|
| base cycles | 30,430,974 | 30,522,406 | +0.30% |
| blanket cycles (spec) | 33,657,714 | 33,656,493 | -0.004% |
| blanket vs base | +10.60% | **+10.27%** | -0.33 pp |
| IPC change | -9.59% | **-9.31%** | -0.28 pp |

The base arm moved +0.30%; blanket was unchanged. The finding survives easily
(the effect is 30x the confound), but **quote -9.31% / +10.27%, not -9.59% /
+10.60%.** `coinsel4` (IPC 1.9518 -> 1.7572) was produced the same way and has
not been re-taken.

## 5. Two rig gaps found while doing this

1. **The driver's defaults do not reproduce the committed numbers.** `coinsel`
   was run at `--iter 1 --warmup 1 --targets 1`; the defaults are `50 / 2 / 10`,
   roughly 500x the work (4 jobs x 99.8% CPU for 31 min without finishing).
   `btc_gem5_coinsel.csv` records `wall_s` but not `iter/warmup/targets`, so the
   run's own output cannot say which configuration produced it - the argv had to
   be recovered from `config.ini`. **Add those three as CSV columns.**
2. **No IPC column.** The CSV carries `cycles` and `simInsts` but not IPC.
   `simInsts/cycles` gives 1.9552 where gem5's own `.ipc` gives 1.9559, because
   gem5 divides by committed `numInsts` (59,697,945), not `simInsts`
   (59,678,264). Emit `numInsts` and `ipc` to make the CSV self-sufficient.
