# 12 - DIT clear shadow

**Status: complete on gem5.** Measured 2026-09-05, Neoverse-V2 FDP, EVES value
prediction, L1D prefetchers off. **No compiler involvement:** the workload is a
hand-written microbenchmark whose `msr dit` instructions are inline asm, so
nothing here depends on the taint pass and no compiler change can move these
numbers. The simulator does: gem5-DIT `81784f2a29` on branch
`dit-clear-shadow-eval` (PR #108, unmerged at the time of writing), which is
also the commit that added the counters this experiment reads.

**No published artifact.** The paper figures are `figures/leaks.png` (the
security result), `figures/gap-sweep.png` (when each design un-gates) and
`figures/cycles.png` (what each design costs).

**Companion to experiment 06.** 06 asks whether this folder's *cost* results
transfer to a core whose `MSR DIT` is expensive. 12 asks whether the *protection*
does: four ways to implement the switch, and whether each one actually stops a
value-dependent optimization inside a DIT region.

---

## The claim

> `PSTATE.DIT` has to gate value prediction even for instructions that only ever
> execute speculatively, because Arm ARM E1.2.5 counts those as part of the DIT
> sequence. A `msr dit` that drains the pipeline does this by construction.
> Three cheaper implementations do not obviously do it at all. Which of them
> are sound?
>
> **Two of the four are, and the failures are on opposite sides of the region.**
> A renamed switch that publishes a speculative clear when it executes leaks the
> whole shadow at region **exit**: 13.0 of 16 loads per attack round behind a
> mispredicted branch, 16.0 of 16 behind a mispredicted value. A switch that
> lets the front end run and flushes at commit is safe at exit and leaks at
> region **entry**: 15.9 of the 16 in-region loads per round are predicted
> before the set retires.
>
> **The renamed switch that defers the clear leaks nothing and still predicts.**
> Zero in-sequence predictions on all three scenarios, while issuing 17.6
> predictions per round behind clears that retired, against the draining
> switch's 17.8 after a drain that costs 2.6x the cycles.

## The gadget

One branch, architecturally taken over a `msr dit, #0` and the loads behind it,
mispredicted not-taken so the clear and the loads run on the wrong path:

```asm
        ldr   x0, [x2]          ; guard; a cold miss on the attack round
        cbnz  x0, skip          ; taken (x0 != 0), predicted not-taken
        msr   dit, #0           ; exits the region, on the wrong path only
        ldr   x1, [x28]         ; x16, loads of a constant the predictor knows
skip:
```

Every shadow is 16 loads of one hot line from 16 distinct PCs, the stride-0 case
E-Stride learns fastest, so once trained the predictor takes all 16 the moment
its gate opens and `valuePredictor.predictions` is a direct readout of the gate.
Three scenarios put the shadow behind a clear that is architectural (`arch`,
which also exercises region entry), behind one a mispredicted branch made
speculative (`branch`), and behind one a mispredicted **value** made speculative
(`value`). 64 attack rounds, 64 to 127 training calls per round drawn at random,
40 warm-up rounds, 24 nops between the clear and the first load unless stated
(the modeled core fetches 6 per cycle).

## The four designs

| # | design | `MSR DIT` mechanism | flag / knob |
|---|---|---|---|
| 1 | **drain** | rename stalls behind it until the ROB is empty; it executes at the head | `IsSerializeAfter`, `--no-speculative-dit` |
| 2 | **flush after** | front end runs on, younger instructions execute under the old mode, all of them flushed and refetched when the MSR commits | `IsSquashAfter`, `--dit-flush-after` |
| 3 | **renamed, naive** | DIT is a renamed register and the clear writes its 0 at writeback like any ALU op | `--dit-clear-publish writeback` |
| 4 | **renamed, deferred clear** | the clear's register holds a conservative 1 from rename until the clear provably cannot be squashed: at retirement, or at the branch frontier | `--dit-clear-publish commit`, and the default |

Designs 3 and 4-at-writeback are the same thing; 3 is kept as a named strawman
because it is the design a renamed switch arrives at if the clear is not
special-cased. A fifth column, a frontier that waits only on branches and not on
unvalidated value predictions, is the second strawman.

## Result

Value predictions issued inside the DIT sequence, per round, of 16 loads in
every shadow. `data/designs_gap24.csv`, gap 24.

| design | `arch` (set and clear both architectural) | `branch` | `value` |
|---|---|---|---|
| 1. drain | 0 | 0 | 0 |
| 2. flush after | **15.91, all at entry** | 0 | 0 |
| 3. renamed, naive | 0 | **13.00** | **16.00** |
| 4. renamed, clear at retirement | 0 | 0 | 0 |
| **4. renamed, clear at the frontier** | **0** | **0** | **0** |
| 4, register-form clear | 0 | 0 | 0 |
| strawman: branch-only frontier | 0 | 0 | **15.25** |

Across the whole experiment, 192 runs, the four sound configurations (drain,
frontier, register form, retirement) ran 96 times and issued **227,132
predictions behind an in-flight DIT write, of which 0 were inside the
sequence**: 227,129 retired behind a write that retired, 3 were squashed by a
mispredict younger than their clear, and the accounting closes exactly on every
run. Flush-after leaked 6,254 over its 24 runs, every one at entry. The two
strawmen leaked 13,221 and 8,173, every one at exit.

**Design 1 makes no prediction because there is nothing to predict.** The loads
behind a draining clear are never renamed until it retires, and on the wrong
path it never does. The zero is the absence of a shadow.

**Design 2 is safe at exit for the right reason and leaks at entry for the same
one.** A speculative clear's shadow reads *committed* DIT, still 1, so it is
gated, and the branch squashes the clear before it ever commits. Apply the same
rule to a set and its shadow runs under DIT 0 until the set reaches the head of
the ROB. The flush undoes the loads' execution, not the prediction that was
already issued and already consumed by dependents, which is what requirement 4
of E1.2.5 is about.

**Design 4 matches design 1 on protection while keeping the shadow.** 17.6
predictions per round behind clears that retired on `arch`, against design 1's
17.8, and it is the branch-only strawman that shows which half of the frontier
earns its keep: gating `branch` exactly like the full frontier, leaking 15.25 per
round on `value`, because a branch that executed on a predicted operand looks
resolved and is not.

## When each design un-gates

`data/gap_sweep.csv` varies the nops between the MSR and the first load, 6 per
front-end cycle. For the renamed designs the reading is how many of the 16
post-clear loads were predicted, which is the un-gating latency. For flush-after
it is how many of the 16 **in-region** loads leaked, which is the length of the
set's window.

| nops after the MSR | 0 | 6 | 12 | 24 | 48 | 96 | 192 |
|---|---:|---:|---:|---:|---:|---:|---:|
| no DIT, both shadows predicted | 35.1 | 36.0 | 35.4 | 33.4 | 34.4 | 33.4 | 35.9 |
| 1. drain, post-clear predicted | 17.3 | 15.9 | 18.3 | 17.8 | 18.5 | 17.3 | 15.9 |
| **2. flush after, in-region leaked** | **14.7** | **15.0** | **15.9** | **15.9** | **15.0** | **5.3** | **0.0** |
| 3. naive, post-clear predicted | 5.3 | 9.9 | 18.3 | 17.7 | 19.2 | 17.3 | 15.9 |
| 4. frontier, post-clear predicted | 0.9 | 0.7 | 2.1 | 17.7 | 19.2 | 17.3 | 15.9 |
| 4. retirement, post-clear predicted | 0.0 | 0.0 | 0.0 | 0.0 | 1.2 | 17.3 | 15.9 |
| 4. register form, post-clear predicted | 1.0 | 2.0 | 1.7 | 2.0 | 0.8 | 17.5 | 19.6 |

Counts above 16 include the frame-restore loads after the shadow. The frontier
holds for about two front-end cycles and has fully un-gated by four; it waits on
the previous call's return and publishes a cycle after that resolves. Retirement
needs about sixteen cycles, and the register-form clear, which has no decode-time
value, tracks it. Flush-after's entry window is the mirror image of that same
sixteen cycles: the set reaches the head at about the same time, and every
in-region load that dispatched first was predicted.

## What each design costs

Cycles per round. `arch` is one set and one clear per round; `branch` and `value`
are 65 to 128 gadget calls per round, each with its own pair.

| design | `arch` | `branch` | `value` |
|---|---:|---:|---:|
| no DIT | 32 | 3,438 | 3,440 |
| 1. drain | 87 | 8,302 | 10,475 |
| 2. flush after | **108** | **9,883** | **13,779** |
| 4. frontier | **34** | **4,027** | **4,091** |
| 4. retirement | 33 | 4,113 | 4,174 |
| 4. register form | 36 | 4,117 | 4,174 |

Flush-after is the most expensive of the four, not the cheapest, because
everything behind every MSR is fetched twice. That is worth stating plainly: the
design that leaks at entry does not buy anything for it on this workload.

## Which of these is real

This folder's position, measured on the M5 and recorded in
`docs/reference/harden-runbook.md` and `docs/overview.md`, is that **Apple's
`MSR DIT` serialises**, at ~30 cycles per executed write, and that gem5's
renamed model is a counterfactual with no silicon counterpart. So of the four
designs here, **design 1 is the one that ships**, design 4 is the design this
project argues for, and design 2 is a third reading of a non-renamed switch
with no measured counterpart either. Nothing below claims otherwise, and in
particular flush-after is **not** a model of Apple's switch.

That makes the drain's cost the one number here that can be checked against
silicon, and it checks out. `arch` commits exactly 2 DIT writes per round, so
the drain's 87 cycles against the frontier's 34 is **about 27 cycles per
executed write** (26.5 against the frontier, 27.5 against no DIT at all). The
M5 measures ~30 for a write that changes the bit, and experiment 06 measures
34.3 in this same simulator at a high toggle rate. Three independent
estimates inside a 27 to 34 band is the strongest calibration this experiment
has, and it is worth more than its own absolute cycle counts.

The protection results do not depend on that calibration. They are properties
of when each design makes the switch's value visible, which is a design
question, and the two that leak do so by 13 to 16 of 16 loads, not by a margin
that a modeling error could invent.

## The five quantities

This experiment has **no public lane and no secret lane**, like 04 and 06, so
three of the five do not apply. `f_secret` is undefined, and `C_public` and
`C_secret` are replaced by the cost table above, which is per design rather than
per lane. The two that do apply:

- **Work per region:** 16 loads, one cache line, constant value.
- **Toggles per unit work:** 2 committed DIT writes per round on `arch`; 130 to
  256 per round on `branch` and `value`, one pair per gadget call. That rate is
  what makes flush-after's double fetch and design 1's drain visible at all, and
  it is the same predictor experiment 06 arrived at.

## Validity gates

Every one of these passes on all 192 runs, and each exists because a zero in the
leak column is worthless without it.

| gate | why | result |
|---|---|---|
| checksum identical across all 8 configurations of each binary | a design that changed the architectural result would invalidate the comparison | 21 distinct triples for 21 scenario-gap pairs, so all 8 agree everywhere |
| checksum equals `(rounds + warmup) x shadows x 7` | catches a shadow that is loading something other than its own line, which is how a silently broken probe was found | 1,456 on `arch` at every gap |
| the no-DIT control predicts the shadow | otherwise a zero in a gated column cannot be told from a mechanism that was idle | 33.4 per round on `arch`, 1,658 and 1,744 on the attack scenarios |
| the strawmen fire | otherwise the counter cannot be shown to detect what it claims to | 13.00 and 16.00 per round, and 15.25 |
| oracle accounting closes | `behind_clear` must equal retired + in-sequence + younger + same-path + behind-squashed-set, or predictions are being dropped | exact on all 192 runs |
| `iew.earlyPublishClaimViolations` = 0 | the frontier's own safety claim, enforced as a panic on sound builds | 0 on all sound configurations; 497 on the branch-only strawman, which is the guard working |
| gem5 DIT regression suite | the simulator changes must not move anything else | 15 of 17 bit-identical; the 2 `leak/*` rows were already stale at HEAD and are identical on a parent-commit build |

## Limits

- **One optimization.** The oracle covers value prediction. Computation
  simplification reads the same gate and would take the same bookkeeping;
  the DMP gates on the request rather than the instruction and its evidence is
  elsewhere (gem5-DIT design doc section 9.10).
- **The leaking counts are floors, not magnitudes.** They are bounded by whether
  the branch predictor took the bait and by how many loads dispatched inside the
  window, not by the size of the hole. The sound configurations' zeros are exact.
- **A microbenchmark, deliberately.** The shadow is the most predictable load
  sequence that exists, which is what makes the gate readable; it says nothing
  about how often real code puts predictable loads in a clear's shadow.
- **Only design 1 has a silicon counterpart.** See "Which of these is real"
  below. A core that realises the mode change at the back end may also predict
  it at decode, which would close flush-after's entry window; this measures
  what is at stake if it does not.
- **Single-threaded SE mode.** An executed load stays squashable by a snoop
  until it commits on a multicore, which is a root of speculation these runs
  never see.

## Reproducing

The rig lives with the harness, in the gem5 fork:

```sh
# gem5-DIT, branch dit-clear-shadow-eval
benchmarks/dit_clear_shadow/build_dit_clear_shadow.sh
benchmarks/dit_clear_shadow/run_clear_shadow.py -o out/clear_shadow -j 24 \
    --gaps 24 --rounds 64 --train 64 --warmup 40
benchmarks/dit_clear_shadow/run_clear_shadow.py -o out/clear_shadow_sweep -j 24 \
    --scenarios arch,branch,value --gaps 0,6,12,24,48,96,192 \
    --rounds 64 --train 64 --warmup 40
~/.venvs/dit-plots/bin/python benchmarks/dit_clear_shadow/plot_clear_shadow.py \
    --matrix out/clear_shadow/results.csv --sweep out/clear_shadow_sweep/results.csv \
    --rounds 64 --gap 24 --out out/figs
```

192 runs, a few minutes on 24 cores, deterministic. `out/clear_shadow/results.csv`
and `out/clear_shadow_sweep/results.csv` are `data/designs_gap24.csv` and
`data/gap_sweep.csv` verbatim. The probe's own documentation is
`docs/dit/benchmarks/dit-clear-shadow.md` and the mechanism is design doc
sections 9.14 and 9.15.

## Contents

| path | what |
|---|---|
| `data/designs_gap24.csv` | 8 configurations x 3 scenarios at gap 24, all oracle counters, mispredict counts, cycles, checksums |
| `data/gap_sweep.csv` | the same 8 x 3 across 7 clear-to-load distances, 168 runs |
| `figures/leaks.png`, `.pdf` | the security result, in-sequence predictions per round by scenario and design |
| `figures/gap-sweep.png`, `.pdf` | when each design un-gates, and flush-after's entry window closing |
| `figures/cycles.png`, `.pdf` | cycles per round by design on `arch` |
