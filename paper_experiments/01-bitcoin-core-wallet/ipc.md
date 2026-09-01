# What blanket DIT costs in IPC: Apple M5 and gem5

**Measured 2026-08-31.** Silicon: `data/wallet_m5_ipc.csv`
(320 rows = 4 knob points x 8 arms x 10 reps). gem5: `~/Documents/dit-browser-bench/
gem5-btc/coinsel_repro/`. Regenerate with `ipc_wallet_sweep.py` and
`btc_gem5.py --bench coinsel --iter 1 --warmup 1 --targets 1`.

---

## 1. Absolute IPC IS measurable on this machine - corrected 2026-09-01

This section previously said absolute silicon IPC was unavailable and that every
absolute IPC figure in the project came from gem5. **That was wrong**, and
experiment 02 measures absolute IPC on the M5 directly.

What is genuinely unavailable is real: there is no `perf`, this box has Command
Line Tools so no `xctrace`, and `/usr/lib/libkperf.dylib` **does not exist**.
That last fact is what the original claim rested on. But the PRIVATE FRAMEWORK
loads and exposes the `kpc_*` API:

    /System/Library/PrivateFrameworks/kperf.framework/kperf

`kpc_get_counter_count(KPC_CLASS_FIXED_MASK)` reports **2 fixed counters**, which
on Apple silicon are core cycles and instructions retired - exactly what IPC
needs, with no event encodings to guess. Arming requires root:
`kpc_force_all_ctrs_set(1)` returns -1 otherwise.

Working implementation:
`gem5-DIT/benchmarks/signed_lookup/kperf_ipc.h`. It degrades to `ipc=na` when
unarmed rather than silently falling back to timing, and the runner treats that
as fatal. Measured IPC across that experiment's arms spans 0.50 to 3.88, which is
the plausibility check on the fixed-counter ordering.

**None of the numbers below change.** They are IPC *changes* derived by the
identity in §2, which needs no counter and is exact under the stated assumption.
What changes is that the assumption can now be checked directly rather than
bounded - see the end of §2 - and that a future re-take of this experiment could
report absolute IPC instead of only the ratio.

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

**That assumption is now independently confirmed, not just bounded.** Experiment
02 ran the same workload under wall-clock and under kperf cycle counters and
compared the two deltas directly:

| point | wall-clock delta | kperf cycle delta |
|---|---|---|
| public lane, L = 20,000 | +11.92% | **+11.92%** |
| full flow, L = 20,000 | +9.15% | **+8.84%** |

Agreement to 0.3 points, i.e. inside the drift bound above. Time really is
standing in for cycles on this machine, so the identity holds and every IPC
figure in this file derived through it is sound.

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

### Work-scaling control: the same measurement at four selections

`coinsel4` is the identical benchmark with `--targets 4` - four coin selections
per run instead of one, same 400-coin pool, 3.68x the cycles. It answers whether
the prize is an artifact of measuring a single one-shot selection with its
warmup and cold caches folded in. It is not:

| run | selections | base IPC | blanket IPC | IPC change | cycles |
|---|---|---|---|---|---|
| `coinsel` | 1 | 1.955873 | 1.773742 | -9.31% | +10.27% |
| `coinsel4` | 4 | 1.949240 | 1.757778 | **-9.82%** | **+10.89%** |

The effect holds at 4x the work and grows slightly, consistent with the prize
living in the selection loop: more iterations means a larger share of runtime
inside the DIT-sensitive dependence chain and proportionally less fixed setup
diluting it. Both rows are gate-compliant, so they are comparable to each other.

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
+10.60%.**

`coinsel4` was produced the same way and has now also been re-taken
(`coinsel4_repro`, 2026-08-31):

| quantity | old (unequal paths) | new (equal paths) | shift |
|---|---|---|---|
| base cycles | 112,257,299 | 112,404,358 | +0.131% |
| blanket cycles (spec) | 124,671,686 | 124,647,767 | -0.019% |
| blanket vs base | +11.06% | **+10.89%** | -0.17 pp |
| IPC change | -9.97% | **-9.82%** | -0.15 pp |

Same signature - the base arm absorbs the shift, blanket barely moves - but a
**different magnitude** (-0.17 pp here against -0.33 pp on `coinsel`). That is
the point: `argv[0]` alignment has no consistent size or sign, it depends on the
specific path length, so an affected run must be re-measured and cannot be
corrected by applying another run's offset.

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
