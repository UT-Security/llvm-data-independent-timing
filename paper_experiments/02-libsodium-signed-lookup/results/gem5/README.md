# Experiment 02, raw gem5 results

The runner's own output, before `derive_exp02.py` turns it into `../../data/`.
`data/` is the derived, provenance-headed import that the tables and figures are
computed from; this directory is what the rig actually wrote, one row per run,
every arm and every stack offset included.

**Run of 2026-09-06.** 1,100 gem5 runs, 0 failed, on an aarch64 Linux host
(beckham). Simulator: this repo's `gem5-DIT` submodule at the pinned commit
`ce05d32089`, built as `gem5.fast`. Compiler: this repo's own `build/`, at
`c733b86567bb`. Config `configs/example/arm/fdp_neoverse_v2_binary.py`,
`--eves --dmp --comp-simp`, Neoverse-V2 FDP.

## What is here

| file | what it is |
|---|---|
| `results_off5.json` | the canonical lane (q4=3), one entry per run: L, arm, switch model, offset, cycles, insts, dit_writes. **This is the raw measurement.** |
| `results_off5_q0.json` | the same for the pure hashed chase (q4=0) |
| `results_off5_q2.json` | the same for q=0.5 (q4=2) |
| `results_off5_q0-1-2-3-4.json` | the predictability sensitivity sweep, L=200 and 20,000 |
| `gem5_arms_off5*.csv` | the runner's per-cell aggregation: median cycles over the 5 offsets, IPC, `vs_base_pct`, `dit_writes`, `spread_pct` |
| `gem5_value_predictor_off5*.csv` | value-predictor counters per cell |
| `arm_switch_counts.txt` | static `msr DIT` sites per arm binary and per archive, and the `.dit` twin count |
| `seeds_used.txt` | the taint seed file this build used, copied verbatim |
| `owned.txt` | the owned-symbols list generated from the base objects, which is what lets a DIT-on caller name a twin in another TU |
| `infoloss.txt` | `-taint-info-loss-report` for the pass build: the obligations the analysis could not close |
| `prec.txt` | `-taint-dit-precision-report`: need / underdit / collateral / switches per function |
| `build_arms_tail.log` | the tail of the arm build (the capture was tail-limited; the per-symbol switch dump above it was not kept) |

## What is NOT here, and where it is

The full per-run gem5 output - `stats.txt`, `config.ini`, `config.json`,
`run.log` for each of the 1,100 runs - is **1.7 GB** and is not committed. It
lives in the run directory this sweep used:

    ~/Documents/signed_lookup-gem5-20260906/runs/<tag>/

`<tag>` is `L<L>[_q<q>]_<arm>_<model>[_nosign][_o<offset>]`, the same key the
runner builds. Everything the experiment claims is derived from the `cycles`,
`insts` and `dit_writes` fields captured in the JSON here; the rest of each
`stats.txt` is gem5's full counter dump.

## Regenerating

    WORK=<fresh dir> ./reproduce.sh          # build, sweep, derive, figures

`reproduce.sh` defaults `LLVM_BUILD` and `G5` to this repo's own build and
submodule, and does **not** resume by default: `run_gem5.py --resume` skips on
`stats.txt` existing, with no binary hash and no compiler recorded, so resuming
into a `WORK` filled by another compiler silently mixes arms. Pass `RESUME=1`
only when you know the dir was filled by the same build.

## The Apple bracket arms

`api` and `apinop` are measured and kept here, but they are not part of this
experiment's story - the question is what ExpeDITe costs against unhardened and
against blanket. The bracket is experiment 09's comparison. Its numbers on this
workload, for the record: +0.6 to +2.8% IPC for the bracket, +0.2 to +0.8% for
its instruction-matched NOP twin, so most of what it costs here is the inserted
instructions rather than the mode.
