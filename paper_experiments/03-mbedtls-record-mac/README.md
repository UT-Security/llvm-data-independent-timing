# 03 - mbedTLS record MAC

**Status: complete on silicon.** Measured 2026-09-01 on Apple M5 (Mac17,2),
mbedTLS 3.6.2, kperf fixed counters.

**Published artifact:** https://claude.ai/code/artifact/a7949ca8-dba2-48c8-b583-9fdad41d8f8f
Source: `figures/grain.html`. To update the page, republish **that URL**
(`Artifact` with `url=...`); publishing the file without it creates a second
artifact instead of updating this one.

---

## The claim

> Experiments 01 and 02 both ask **pass vs blanket** along the secret-fraction
> axis. This one asks **fine vs coarse** along the **region-size** axis:
>
> Selective placement's advantage over whole-function placement is worth up to
> **13.84%**, it is **entirely an IPC effect** on an identical instruction
> stream, and it **reverses** below about **2.5 us of work per secret region** -
> where both placements are far worse than blanket.

## Why it is a different experiment

**No earlier experiment could measure fine grain.** In 01 and 02 the public and
secret work live in *different functions*, so whole-function placement would have
performed identically to region placement. Neither has a
`-taint-dit-placement=function` arm; there was nothing for one to do.

Here both kinds of work sit in **one seeded function**, `process_record`, which
is the only structure where the two policies can diverge. Verified structurally
rather than assumed - each arm reads `PSTATE.DIT` inside that function before the
public work:

| arm | public work covered |
|---|---|
| region | **0.000** |
| function | **1.000** |

And the knob is region size, not secret fraction: total bytes fixed at 8 MB,
`f_secret` held at 54-74%, chunk swept 64 B -> 16 KB so the region moves 256x and
the region *count* moves the other way.

## Re-measured 2026-09-03 after the phi fix

The phi fix (`docs/design/frame-address-gap.md`) is unflagged and default-on, and
it took this library from **41 to 49** `msr DIT` - concentrated in exactly the
path this experiment measures (`mbedtls_md_finish` 6 -> 8,
`mbedtls_md_hmac_update` 1 -> 3). Three runs, machine idle, `data/remeasure_2026-09-03.csv`.

**The headline is unchanged.** Region still beats function at every point,
growing with region size, and the numbers land on the recorded ones within
run-to-run spread:

| chunk | region vs function, now | recorded |
|---|---|---|
| 64 B | -0.36% | -1.17% |
| 256 B | -3.03% | -2.64% |
| 1,024 B | -6.81% | -7.56% |
| 4,096 B | -12.54% | -12.70% |
| 16,384 B | -13.56% | -13.84% |

**What moved is the crossover against blanket.** Region's absolute cost rose where
toggling dominates, which is what +19.5% more switches should do:

| chunk | region vs blanket, now | recorded |
|---|---|---|
| 64 B | **+47.9%** | +33.2% |
| 256 B | **+24.1%** | +16.9% |
| 1,024 B | **+3.1%** | **-0.6%** <- was the crossover |
| 4,096 B | -10.1% | -10.8% |
| 16,384 B | -12.9% | -13.4% |

**The crossover has moved from 1,024 B to somewhere between 1,024 and 4,096 B.**
The large-chunk end is unchanged; only the small-region regime got worse, which is
the regime where switch count is the cost.

**A note on variance.** Run-to-run spread at 16,384 B is -9.8% to -13.7% on the
same binaries - single runs do not resolve that end of the sweep, and the table
above reports medians of three. The recorded figures sit inside that spread.

## Headline results

| chunk | us per secret region | **region vs function** | region vs blanket |
|---|---|---|---|
| 64 B | 0.66 | -1.17% | **+33.2%** |
| 256 B | 1.06 | -2.64% | +16.9% |
| 1,024 B | 2.56 | -7.56% | **-0.6%** <- crossover |
| 4,096 B | 9.69 | -12.70% | -10.8% |
| 16,384 B | 37.28 | **-13.84%** | -13.4% |

**Fine grain's value is 100% IPC.** Instruction counts between the two placements
are identical to **+/-0.00%** at every point - both execute exactly three mode
writes per record; only the *first* one moves, from after the public loop to
function entry. So the whole delta is that the public work keeps its value
prediction. The identity confirms it: with equal instructions, time must be
1/IPC, and that predicts the measured delta to **within 0.1 pp everywhere**.

| chunk | IPC region | IPC function | IPC gain | time implied | time measured |
|---|---|---|---|---|---|
| 64 B | 2.250 | 2.224 | +1.17% | -1.16% | -1.17% |
| 1,024 B | 2.454 | 2.269 | +8.15% | -7.54% | -7.56% |
| 16,384 B | 2.643 | 2.278 | +16.02% | -13.81% | -13.84% |

**The floor matters as much as the ceiling.** At 0.66 us regions, blanket costs
+10.75% while region costs **+47.47%** and function +49.21%. Below ~2.5 us,
selective placement of *any* granularity is the wrong choice. That puts a
measured number on the project's standing "fine grain stops paying below ~1us per
region" belief, which rested on one QuickJS data point, and places it somewhat
higher.

The `nop` control spans -1.67% to +0.07% throughout, so none of this is the
layout cost of inserting switches.

## The five quantities

| | value |
|---|---|
| `f_secret` | 53.6-73.9% (measured per point via `--nomac`), held roughly fixed by design |
| `C_public` | **is** the region-vs-function column: those arms differ only in whether the public lane is covered. 1.17% -> 13.84% of total runtime |
| `C_secret` | ~2.6% at large chunks (region arm, where only the secret lane is covered) |
| work per region | **0.66 to 37.28 us** - the axis |
| toggles per unit work | **exactly 3 mode writes per record**, constant in chunk, so toggles *per second* fall 256x across the sweep |

## The design gate that killed the first attempt

The original plan used a TLS proxy **demux** as the public lane - hash lookup plus
chain walk per record. It **failed the headroom gate**, and the reason generalises
(`data/demux_gate.csv`):

| configuration | penalty | sign test |
|---|---|---|
| conns=4096 chain=16, independent records | **-5.36%** | 0/9 |
| conns=4096 chain=16, **records chained** | **+38.01%** | 9/9 |

Same code, same table, same access pattern. The only change is whether record
r+1's work depends on record r's result. **Inter-record ILP is what hides the
prize**: a real demux is embarrassingly parallel across records, the
out-of-order engine overlaps them, and value prediction has little left to add.

Two consequences worth carrying:

- **The win condition needs a fourth clause.** The project knows headroom needs a
  serial dependence chain; this adds that the chain must span the *unit of work*.
  Per-unit chains get overlapped away. It is why coin selection works in 01 (a
  solver's search is serial across the whole call) and why filters never did.
- **Experiment 02's public lane sits at the favourable extreme.** It is one
  unbroken chain thousands of iterations long with zero ILP, which is not what
  request-processing code looks like. Its +30.6% is real but not typical.

This experiment therefore stopped hunting for headroom and measures the
granularity axis instead, reporting **both** public-lane modes (`ilp` realistic,
`dep` favourable) so the dependence matters explicitly.

## A build bug found on the way

`~/Documents/mbedtls-3.6.2` carries **107 stale `.o` files and three archives**,
and the existing `benchmarks/tls_handshake/build.sh` copied them with
`tar --exclude=.git`. `make` then judged every object up to date and **skipped
every compile**: the taint flag is passed and nothing is recompiled, producing a
"hardened" library with **zero switches and no error**. Fixed in both that script
and this experiment's. Anything built from that tree before 2026-09-01 should be
re-checked with `llvm-objdump -d ... | grep -ci 'msr.*DIT'`.

## Reproducing

Rigs live with the harness, in **gem5-DIT** under `benchmarks/record_mac/`.
Requires an exclusive machine, and root for kperf.

```sh
LLVM_BUILD=... benchmarks/record_mac/build_native_mbedtls.sh base taint taintfn nop
LLVM_BUILD=... benchmarks/record_mac/build_native.sh
sudo python3 benchmarks/record_mac/run_grain.py
```

`CHUNKS`, `MODES`, `REPS`, `TOTAL_KB` and `PUB_DIV` are env overrides for
re-running a single point. The runner **aborts** if any arm's `pub_dit` does not
match its policy.

## Known limits

- **Silicon only.** No gem5, so no feature isolation attributing the IPC gain to
  a specific structure. The mechanism is inferred from the identical instruction
  counts plus experiment 02's gem5 evidence that DIT gates the value predictor.
- **`f_secret` is not perfectly constant** (53.6-73.9%). Public work tracks the
  HMAC's cost rather than the chunk, which removed most of the drift but not all.
- **The public lane is still synthetic** in the same way 02's is, though the
  secret lane is a real library primitive and the interleaving is the realistic
  part.
- **One anomaly**: `dep` at chunk=1024 reads -1.79% where `ilp` reads -7.56%,
  against a smooth trend everywhere else. Not explained.

| path | what |
|---|---|
| `data/grain_sweep.csv` | 5 arms x 5 region sizes x 2 public-lane modes, with kperf IPC and instruction deltas |
| `data/region_size.csv` | the chunk knob converted to microseconds per secret region |
| `data/demux_gate.csv` | the headroom gate that killed the first design, and the ILP test that explained it |
| `data/placement_verification.csv` | structural proof the arms differ: `pub_dit` 0.000 vs 1.000 |
| `figures/grain.html` | source of the published artifact |
