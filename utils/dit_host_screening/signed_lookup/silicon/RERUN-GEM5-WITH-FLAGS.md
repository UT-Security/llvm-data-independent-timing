# The gem5 column of experiment 02 has to be rerun with `--apple` / `--expedite`

**Why:** the committed gem5 arms (`data/gem5_arms.csv`, run 2026-09-06) predate
those flags. They were run on the old two-model surface —

```
MODELS = {"renamed": [], "serialising": ["--no-speculative-dit"]}
BASEFLAGS = ["--eves", "--dmp", "--comp-simp"]
```

— so the "serialising" column is `--no-speculative-dit`, not `--apple`, and
there is no `--expedite` column at all. Experiment 14's runner
(`utils/dit_host_screening/awslc/run_awslc_gem5.py`) already uses both:

```python
CONFIGS = {"apple":    ["--apple",    "--eves", "--dmp", "--comp-simp"],
           "expedite": ["--expedite", "--eves", "--dmp", "--comp-simp"]}
```

and its provenance names what they are: `--apple` flushes after the switch
(squash at commit), `--expedite` is the renamed switch with a deferred clear and
the `isb` fused at rename.

**Where the flags live.** NOT in this repo's `gem5-DIT` submodule and not on any
of its 33 refs — `git ls-remote origin` has no `dit-flag-restructure` and
`455bc87c56a4` is "not our ref". Experiment 14's gem5 provenance points at a
separate clone on the simulator host:

```
gem5   /home/rgangar/Documents/gem5-DIT-flags
       branch dit-flag-restructure @ 455bc87c56a427bb81ab8c1fc4d105a01f73d0f8
host   beckham, 160 cores, Linux aarch64
```

So the rerun has to happen there. The exp02 driver binaries are static aarch64
ELF for the simulator; nothing about this runs on the Mac.

## The change

One table in `gem5-DIT/benchmarks/signed_lookup/run_gem5.py`:

```python
-MODELS = {"renamed": [], "serialising": ["--no-speculative-dit"]}
+MODELS = {"apple": ["--apple"], "expedite": ["--expedite"]}
 BASEFLAGS = ["--eves", "--dmp", "--comp-simp"]
```

and the `ARMS` list below it, which spells the model name per arm ("renamed" /
"serialising" -> "apple" / "expedite"). Then:

```sh
# on beckham
G5=~/Documents/gem5-DIT-flags \
LLVM_BUILD=<the taint build> \
WORK=~/Documents/signed_lookup-gem5-apple \
  paper_experiments/02-libsodium-signed-lookup/reproduce.sh build sweep derive
```

Two consumers read the `switch` column and need the same rename:
`utils/dit_host_screening/signed_lookup/fig_exp02.py` (lines 71-72) and
`fig_exp02_silicon.py` (lines 137-138, 193-195). `derive_exp02.py` reads the
literal directory name `..._renamed` at line 90 for the value-predictor table.

## What `--apple` will and will not fix

It will price the two `msr DIT` writes under the model the flag names. It will
**not** close the gap to the M4, for two reasons that are worth knowing before
the rerun:

1. **gem5 has no FEAT_SB.** Nothing in the checked-out tree decodes `0xd50330ff`
   (`sb`); the barrier group ends at `Isb64` and falls through to `Unknown64`.
   So `api_bracket.c` substitutes `isb sy`, and exp02's api arm took that default
   (`-DAPI_CHACHA`, no `-DAPI_BARRIER_*`). Experiment 14's runner says the same
   thing outright: "gem5 has no FEAT_SB and `isb sy` in its place was only ever a
   stand-in". On the M4 the real `sb` is **most of the bracket's cost** — dropping
   it takes the bracket from ~480 cycles to ~300 (`bracketnobar`). Whether
   `dit-flag-restructure` adds FEAT_SB is the first thing to check; if it does
   not, the gem5 bracket column still understates by the barrier.
2. **The name is contested by the project's own numbers.** `--apple` selects
   flush-after, which `paper_experiments/12-dit-clear-shadow/README.md`
   calibrates as design 2 and says explicitly is **not** a model of Apple's
   switch — it calls design 1 (drain) "the one that ships" and measures
   flush-after as the *more* expensive of the two (108 cycles per round against
   87). Design 1 also matches silicon on the unit price: ~27 cycles per executed
   write in gem5, ~30 on the M5, 34.3 in experiment 06, and **33.5 measured on
   this M4** by `dit_switch_cost.c`. So if the goal is "what an Apple part does",
   drain is the better-calibrated model and `--apple` is a misnomer; if the goal
   is "what the flag named `--apple` does", it is flush-after. Worth settling
   before a paper column rests on it.

## What the gap actually is, from the committed data

`api - apinop`, i.e. the bracket against its own instruction-matched twin, in
cycles per request. Both instruments, same driver, same library, 2 committed DIT
writes per request in the gem5 arm.

| L | gem5 request | gem5 bracket | | M4 request | M4 bracket | |
|---|---|---|---|---|---|---|
| 10 | 2,512 | **50** | +2.0% | 1,131 | **320** | +28.3% |
| 50 | 3,195 | **31** | +1.0% | 1,573 | **504** | +32.0% |
| 200 | 5,772 | **58** | +1.0% | 3,637 | **480** | +13.2% |
| 1,000 | 19,556 | **-9** | -0.1% | 14,482 | **501** | +3.5% |
| 5,000 | 88,161 | **-42** | -0.1% | 68,783 | **456** | +0.7% |
| 20,000 | 345,462 | **-625** | -0.2% | 272,602 | **582** | +0.2% |

**gem5 prices the whole sequence at 31-58 cycles and the M4 at 456-582** — an
order of magnitude — and past L=1,000 the gem5 figure goes NEGATIVE, i.e. the
bracket costs less than the model's own layout term and vanishes into it. That is
the "no perf loss": not a finding about brackets, a floor on what that model can
resolve. The M4 number is 456-582 at every length, flat, and measured against
the same twin.
