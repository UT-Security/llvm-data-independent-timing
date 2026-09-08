# The gem5 column of experiment 02 has to be rerun: `--apple` / `--expedite`, and `dsb nsh; isb sy`

## Build the gem5 bracket with `-DAPI_BARRIER_DSBISB`

**This is the actionable fix and it needs no FEAT_SB work.** Apple's guide gives
two recipes: `sb` on a part that has FEAT_SB, and `dsb nsh; isb sy` on one that
does not. gem5 implements **both instructions of the fallback exactly** --
`Dsb64Local` with `IsSerializeAfter` and `Isb64` with `IsSquashAfter` -- and no
`sb` at all. So the gem5 bracket arm should be built with Apple's fallback, which
is a recipe Apple actually publishes:

```diff
-link api     base  "$API_BRACKET -DAPI_CHACHA -Wl,--wrap=..."
+link api     base  "$API_BRACKET -DAPI_CHACHA -DAPI_BARRIER_DSBISB -Wl,--wrap=..."
```

(and the same on `apinop`, whose `-DAPI_NOP` already NOPs whatever barrier is
selected). What it is on today -- no `-DAPI_BARRIER_*`, so `api_bracket.c`'s
`isb sy`-only default -- is **not an Apple recipe at all**. It was chosen because
gem5 lacks `sb`, and `isb` alone is half of the fallback. Switching to the full
pair costs nothing and makes the gem5 column a faithful model of something Apple
ships, rather than of a sequence nobody would write.

**Apple ships `sb`, verified from the binary.** `/usr/lib/system/libsystem_platform.dylib`
(macOS SDK 26.1) implements the API the guide points at:

```
timingsafe_enable_if_supported:        timingsafe_restore_if_supported:
    mrs  x8, DIT                           tbnz w0, #0x0, +8
    ubfx x0, x8, #24, #1   ; the token     msr  DIT, #0x0
    msr  DIT, #0x1                         ret
    sb
    ret
```

So on an M-series part the shipping barrier is `sb`, and `api_bracket.c`'s
`-DAPI_BARRIER_SB` arm reproduces Apple's sequence instruction for instruction.
gem5 can never run that arm; it can run the fallback.



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

1. **gem5 has no FEAT_SB — upstream either.** Checked against upstream
   `gem5/gem5` `stable` @ `f5c5a6e390f5` (2026-09-07): the A64 barrier decode
   group ends at `Isb64` and falls through to `Unknown64`
   (`src/arch/arm/isa/formats/aarch64.isa:269-273`), and nothing anywhere
   implements `sb` or FEAT_SB. So `api_bracket.c` substitutes `isb sy`, and
   exp02's api arm took that default (`-DAPI_CHACHA`, no `-DAPI_BARRIER_*`).
   Experiment 14's runner says the same thing outright: "gem5 has no FEAT_SB and
   `isb sy` in its place was only ever a stand-in".

   **And the `isb` model itself is not the problem — it is upstream and it is
   the stronger of gem5's two barrier models.** Upstream has
   `isbIop = ArmInstObjParams("isb", "Isb64", "IsbOp64", "", ['IsSquashAfter'])`
   (`src/arch/arm/isa/insts/misc64.isa:191`), byte-identical in the fork, and it
   deliberately differs from `dsb`, which gets `IsSerializeAfter`. The two are
   not interchangeable and `IsSquashAfter` is the more expensive:

   | flag | where | what happens |
   |---|---|---|
   | `IsSerializeAfter` (dsb) | rename (`o3/rename.cc:756`) | marks the next instruction serializeBefore, which "makes the instruction wait in rename until the ROB is empty". Drains; younger instructions stay fetched in the rename queue; **no squash, no refetch** |
   | `IsSquashAfter` (isb) | commit (`o3/commit.cc:1119`) | the ISB must reach the ROB head, so everything older drains; then `squashAfter()` sets `SquashAfterPending`, squashing everything younger and **restarting fetch** |

   So `isb` does stall the front end — by redirecting it at commit rather than
   blocking rename — and it drains *and* discards *and* refetches. Changing it to
   `IsSerializeAfter` would make it CHEAPER, not more expensive.

   The ~20 cycles experiment 09 measures is therefore the modelled pipeline
   depth, not a lax flag: `configs/common/cores/arm/neoverse_v2.py` has
   `commitToFetchDelay=1`, `fetchToDecodeDelay=3`, `decodeToRenameDelay=2`,
   `renameToIEWDelay=1`, so fetch-to-issue is ~7 cycles and a squash-plus-refill
   lands around 15-20. exp09 measured 18-27. Self-consistent.

   **Where the undercharge is, stated at the granularity the data supports.**
   The whole bracket, against its own NOP twin, is 31-58 cycles per request in
   gem5 and 460-580 on the M4 (table below). That 10x is solid on both
   instruments: it is large against each rig's layout band.

   Attributing it to a single instruction is not solid, and an earlier version
   of this note overstated it. Two measurements of the barrier alone:

   | | `sb` | `dsb nsh; isb sy` |
   |---|---|---|
   | isolated (`sbdrain.c`: controlled independent DRAM loads in flight, then the write, then the barrier) | 22 cyc with nothing in flight, rising to 58 with 32 loads in flight | ~60-69 cyc, flat in what is in flight |
   | in situ (`bracket - bracketnobar` / `bracketdsb - bracketnobar`, L=200/1000/5000) | 147 / 172 / 140 | 8 / 16 / 25 |

   The two disagree about which barrier is dearer, so **neither ordering should
   be quoted.** What both agree on is that a barrier costs tens of cycles in
   isolation, and that removing the barrier entirely still leaves ~290-400
   cycles of the M4 bracket unexplained -- which the tight-loop price of
   `mrs` + two `msr` (78 cycles all in) does not account for either. The
   defensible reading is that **the sequence costs 4-6x its isolated price
   because serialising in the middle of a real instruction stream is far dearer
   than serialising in a loop**, and no one instruction in it owns that.

   Experiment 14 reports the same barrier at **4 cycles** after a mode-changing
   write ("a barrier after a serialising one finds it empty"), on a 16-byte AES
   seal. `sbdrain.c` shows why that is not general: the barrier's cost GROWS
   with what is in flight (22 -> 58 cycles over 0 -> 32 independent loads), so a
   16-byte seal and a bracket sitting after a 200-lookup pointer chase are not
   the same measurement. The structural claim does not hold; the number is a
   property of the surrounding code.
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
