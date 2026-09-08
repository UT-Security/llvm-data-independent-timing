# gem5 with `--apple`, `--expedite` and FEAT_SB — the bracket is free in the model

**Run 2026-09-08 on the M4 itself.** gem5-DIT `b76c101ad4` (origin/master, PR #109
`dit-flag-restructure`), which adds `--apple`, `--expedite` and **FEAT_SB**, so
the bracket runs Apple's real `sb` instead of a substitute. `gem5.fast` built
here; the workload cross-compiled to static aarch64-linux ELF with
`util/cross/taint-cross-cc`, so nothing needed an aarch64 host. 270 simulations
(240 arms × models × 6 L × 5 offsets, plus 30 `--nosecret` for f_secret), ~9 min.

**Data:** `data/gem5_apple_arms.csv`. **Figure:** the left panel of
`figures/crossover-gem5-vs-m4.{png,pdf}`.

## The three lines

IPC overhead vs unhardened, median of 5 stack offsets, f_secret gem5's own:

| f_secret | L | blanket | bracket `--apple` | bracket `--expedite` |
|---|---|---|---|---|
| 96.5% | 10 | +1.94% | -4.03% | -6.39% |
| 80.4% | 50 | +10.38% | -2.85% | -2.76% |
| 44.7% | 200 | +18.24% | -1.67% | -1.70% |
| 13.1% | 1,000 | +27.68% | -0.47% | -0.71% |
| 3.4% | 5,000 | +30.00% | -0.30% | -0.14% |
| 1.1% | 20,000 | +30.75% | -0.12% | +0.04% |

**Blanket reproduces the committed sweep** (+1.94 → +30.75 here against
+2.25 → +30.95 in `gem5_arms.csv`, a different gem5 build on a different host),
which is the check that says this rig is the same rig.

**The bracket is cheaper than unhardened at every point, under both switch
designs**, so there is no crossover to mark on this panel — the M4's is at
f\* = 52%. Two mode writes and an `sb` per 2,481-to-345,942-cycle request is
below what the model resolves.

## The switch design does not separate them, and that is the result

`--apple` and `--expedite` differ by at most 2.4 points (at f=96.5%) and by
under 0.3 anywhere else. That is not a failure to measure: **a two-write bracket
is too sparse for the switch design to matter.** The design is what separates
*dense* placement — the pass executes ~40 writes per request on this flow, where
the committed CSV puts serialising at +25.3% against renamed's -0.1% at the same
f. The bracket's whole point is that it does not care.

## What `api - apinop` says, and why it is not on the figure

Against its instruction-matched twin the bracket reads **negative** at 10 of 12
cells (-41 to -117 cycles/request, `--apple`; -74 to -117, `--expedite`), with
two positive outliers at L=5,000. `HINT #0` is not a free issue slot in this
model, so the twin is slower than the arm it controls for and the difference
measures the nops. Adding FEAT_SB did not change that: `sb` is `Sb64`,
`IsSerializeAfter`, a rename-stall drain, and after a switch that already
serialised it finds the pipeline empty. So `api - apinop` remains unusable as
the bracket's cost in gem5 — as it was with `isb sy`, and as the previous rerun
(`../gem5-barrier-rerun/`) found with `dsb nsh; isb sy`.

**The silicon number is the one that carries.** On the M4 the same bracket costs
455-510 cycles per call, measured against a twin at the same instruction count,
and 140-170 of that is the `sb` — which is why the M4 panel has a crossover and
this one does not.

## Reproducing

```sh
# gem5-DIT at origin/master, then:
scons build/ARM/gem5.fast -j9
# the arms, cross-compiled (see run_apple_expedite.py's header for the four builds)
python3 run_apple_expedite.py     # 240 sims, ~8 min at 6 workers
python3 measure_f_secret.py       # 30 more, gem5's own --nosecret
```

Run dirs are 270 × ~1 MB of `stats.txt` and are not committed; `sweep.log` is
what the runner printed.
