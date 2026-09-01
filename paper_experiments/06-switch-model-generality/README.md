# 06 - switch-model generality

**Status: complete on gem5.** Measured 2026-09-01, Neoverse-V2 FDP, experiments
02 and 03's workloads under both `MSR DIT` implementations.

---

## The claim

> Every silicon number in this folder is Apple M5, whose `MSR DIT` is cheap. Do
> any of the results transfer to a core where it is not?
>
> **Only where the toggle rate is low.** The switch model's cost tracks the
> **committed** toggle rate almost linearly: at 4,601 writes per million cycles
> a serialising switch adds **+15.80 points**; at 86 writes per million cycles it
> adds **+0.66**, which is nothing.
>
> So experiment 02's crossover survives a serialising core. Experiment 03's
> small-region regime does not - it goes from +1.64% to **+17.44%**.

## Result

Experiment 03's workload - one seeded function holding public bookkeeping and an
mbedTLS HMAC - at the two extremes of its region-size sweep:

| chunk | model | base | region | function | committed DIT writes |
|---|---|---|---|---|---|
| 64 B | renamed | - | **+1.64%** | +18.93% | 19,065 |
| 64 B | **serialising** | - | **+17.44%** | **+35.04%** | 19,065 |
| 16 KB | renamed | - | -1.60% | +36.90% | 93 |
| 16 KB | **serialising** | - | -0.94% | +36.87% | 93 |

| chunk | writes / Mcycle | region penalty | function penalty | **cycles per write** |
|---|---|---|---|---|
| 64 B | 4,601 | **+15.80 pp** | +16.11 pp | **34.3** |
| 16 KB | 86 | +0.66 pp | -0.03 pp | 76.3 |

**205x more committed writes, 24x more switch-model penalty.** At the
low-toggle end the two models are indistinguishable - 0.03 to 0.66 points, inside
the noise. The per-write cost of ~34 cycles is in the same range as the 24.0
cycles measured directly on silicon in experiment 02, with gem5 inflating as it
does everywhere else.

## Why experiment 02 is insensitive and 03 is not

Experiment 02 commits **exactly 49 DIT writes per signature** at every knob point.
Its toggle rate is roughly 50x lower, and the switch model costs it only +0.38 to
+1.54 points (`data/signed_lookup_switch_model.csv`). Experiment 03 at chunk=64
commits 19,065 writes in 4.1M cycles - one per ~217 cycles.

That is the general rule, and it is the same predictor the callee-saved ABI work
arrived at from a different direction: **ask how often a mode write actually
executes per unit of work, not how many appear in the binary.**

## What transfers and what does not

| result | survives a serialising core? |
|---|---|
| 02's crossover at f* = 59% | **yes** - 49 writes/signature is far below the threshold |
| 03's fine-grain win at large regions | **yes** - 93 writes, models within 0.66 pp |
| 03's small-region floor | **no** - it gets much worse: region +1.64% -> +17.44% |
| 04's "coarse beats fine on libsecp256k1" | **yes, more so** - 20 static sites |

The practical reading: the **region-size threshold moves up** on a serialising
core. Fine grain was already the wrong choice below ~2.5 us per region on the M5;
on hardware where a mode write costs ~34 cycles it is wrong over a wider range
still.

## Limits

- **Two workloads, two extremes.** The middle of experiment 03's sweep was not
  run under both models.
- **32 KB per run, not the native 8 MB.** The hardened arms are pathologically
  slow to simulate at full size - one run exceeded 15 minutes where the base arm
  took seconds. Worth recording as a property of simulating high toggle rates,
  but it means these are small ROIs.
- **gem5 magnitudes are inflated** throughout, consistently ~3x against silicon
  elsewhere in this folder. The **ordering and the proportionality** are the
  transferable parts, not the absolute percentages.
- **"Serialising" is gem5's model, not a specific shipping core.** It is the
  conservative end; a real core may sit anywhere between.

## Contents

| path | what |
|---|---|
| `data/record_mac_switch_model.csv` | experiment 03's workload, both models, 3 arms x 2 region sizes |
| `data/sensitivity.csv` | the penalty as a function of committed toggle rate, and cycles per write |
| `data/signed_lookup_switch_model.csv` | experiment 02's workload for contrast - 50x lower toggle rate |
