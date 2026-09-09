# 15 - Security evaluation: one microbenchmark per gated optimization

**Status: complete on gem5.** Measured 2026-09-08, Neoverse-V2 FDP, on compiler
`cbec54a0866f` and gem5-DIT `25675e4aff03` **plus the uncommitted change this
experiment needs** (see Reproducing). 23 cells, 12 seconds, deterministic per binary path.

**Re-measured 2026-09-08 on gem5-DIT `25675e4aff03`**, which lands
`dit-apple-msr-not-ordered` (PR #110): `--apple`'s `msr DIT` no longer carries
`IsNonSpeculative`, so younger instructions are not held behind it. That is the
arm this experiment compares against, and it changes what `--apple` does, so
every number below is from after it.

**Provenance, because it nearly went wrong.** The first pass of these numbers
came off a `build/` whose clang reported a commit that is **not an ancestor of
HEAD**, with 260 commits between the two and several of them touching
`TaintAnalysis.cpp` and `TaintSourceAnnotator.cpp`. Those numbers are discarded;
every number here is from a compiler rebuilt at `cbec54a0866f`. Checking
`build/bin/clang --version` against `git rev-parse HEAD` costs one command and
is worth making a habit. That tree was also
configured `LLVM_ENABLE_ASSERTIONS=OFF`, which silently made 13 of the 58 taint
tests `Unsupported` rather than passing, so a second tree (`build-asserts/`) was
built to the recipe in CLAUDE.md: **57 pass, 1 expected XFAIL, 0 failures**, no
assertion fires in the taint pass on any of these probes, and the probe binaries
it produces are **instruction-for-instruction identical** to the recorded ones
(0 differing lines of 95,065; the file-level difference is debug info). Re-running
the whole matrix against them reproduces the leak column exactly and passes all
36 gates.

**This is the first experiment here in which the compiler and the simulator are
both under test.** Experiment 04 measures whether the pass sets `PSTATE.DIT`
over the secret; experiment 12 measures whether four ways of implementing the
switch actually suppress an optimization. Neither asks the composed question,
which is the one that ships: **the compiler chose where the region goes - does
the optimization still act on the secret?** A hole in the placement reads here
exactly like a hole in the gate, which is the point.

**No published artifact.** The paper figure is `figures/leaks.png`.

---

## The claim

> We construct one microbenchmark per gated optimization - value prediction,
> computation simplification, and the data memory-dependent prefetcher - plus
> one for the speculation rule all three depend on. Each runs on the gem5
> baseline and under ExpeDITe, with the secret entering through a seeded
> argument and the taint pass choosing the region. **The baseline leaks in
> every case and ExpeDITe leaks in none.**
>
> The baseline is not a strawman: it is the same binary with the mode switches
> omitted, so its column is the optimization running unobstructed. Its readings
> are large - 1,023 predicted secret loads, 32,768 multiplies given
> operand-dependent latency, 64,516 prefetches of lines addressed by a secret
> pointer - and ExpeDITe's are exactly zero on all three.
>
> The fourth microbenchmark is the one that could have gone either way, and its
> zero is worth the most. A `msr dit, #0` the compiler placed on a
> secret-dependent early exit **executes on a mispredicted path**, and the
> instructions behind it are architecturally still inside the DIT sequence.
> Under ExpeDITe's deferred clear nothing behind it is ever predicted: **0 of
> 475** predictions issued in a clear's shadow were in-sequence. Under the
> early-publish strawman, on the identical binary, **2,129 of 38,804** were.
>
> The third arm is Apple's design, and it is not a strawman either. Its DIT
> write is unordered, so younger instructions execute against the old mode
> while it is in flight. **That window leaks in all three gated
> optimizations**: 482 value predictions inside the region, all 32,768
> secret-operand multiplies given operand-dependent latency, and 8 prefetches of
> secret-addressed lines.
> ExpeDITe leaks on none of them, and on the probe that toggles often it also
> costs **72% more cycles** than ExpeDITe for the flush it raises per switch.
>
> One of those leaks has a named cause rather than only a measurement:
> **CompSimp is the only consumer of the DIT gate that skips the readiness
> test**, so it acts on a stale mode instead of failing safe. That is BUG-2 in
> the design doc, demonstrated.

## Attack assumptions, stated plainly

Every microbenchmark here observes microarchitectural state **directly in the
simulator** - the predictor's own counters, CompSimp's own counters, the DMP's
scan and issue counters - rather than recovering it through a timing channel.
Three simplifications follow, and each is worth naming because each makes the
test easier to pass in one direction and harder in the other.

**We read the mechanism, not the timing.** A real attacker sees the consequence
of a prediction - a cache line, a latency - not the prediction. So our leak
counts are strictly larger than what an attacker recovers: some predictions
leave no observable trace. That direction is the safe one. It means
**ExpeDITe's zeros are stronger than a timing measurement would be**, because
we are counting events an attacker would first have to detect; and it means our
baseline columns are upper bounds on the attack rather than attack rates, which
we do not claim they are.

**We do not model the attacker's own execution.** No contending process, no
cross-thread eviction, no PMU; this is single-threaded SE mode. Removing that
simplification means running the published attacks, and each of the three
optimizations has one. **GoFetch** recovers keys through the M-series DMP by
exactly the pointer-shaped-secret activation mb3 plants. **FLOP** recovers data
through the M3/M4 load value predictor, and its authors recommend precisely
this project's mitigation. **THOR** turns operand-dependent `TMUL` latency into
a non-speculative channel of the shape mb2 measures. What those attacks
establish is that the mechanisms are exploitable on silicon; what these
microbenchmarks establish is that under ExpeDITe the mechanisms do not fire.
The join is an argument, not a measurement, and we say so.

**The secret is declared, not discovered.** Each victim is seeded by hand, one
line each. That is deliberate - these test the gate, not the analysis - and it
is experiments 04 and 08 that measure whether the analysis finds the secret in
real code. A microbenchmark that both declared and found its own secret would
be measuring two things and separating neither.

One simplification we did **not** take: the regions are not hand-placed. Every
`msr DIT` in every hardened binary was emitted by the taint pass from a seed
line, which is why mb4's branch is a secret-dependent `tbnz` arising from
ordinary C rather than a gadget shaped to produce one.

## The four microbenchmarks

Sources in `gem5-DIT/benchmarks/expedite_security/`, one seed file each. The
victim is `victim()` in all four, seeded `victim,0,pointee` - the pointer is
public, the memory behind it is not.

### mb1 - value prediction

A rolled loop over a small secret buffer with add/sub/eor/orr/and work hanging
off each loaded word. After unrolling, each PC lands on one residue of the index
mask and therefore on one fixed secret word: the stride-0 case E-Stride reaches
confidence on fastest. If the predictor is allowed to act it will, and
`valuePredictor.predictions` reads out whether it was allowed. There is
deliberately no multiply - that would also be a CompSimp candidate and would
blur mb1's readout into mb2's.

This answers the training question too, which is the sharper one. On the
hardened arm the victim only ever executes inside the region, so a prediction in
the ROI would prove the predictor tables were trained on in-region values. There
are none.

### mb2 - computation simplification

Two secret words multiplied, half of them zero. CompSimp shortcuts a multiply by
0 or 1, retiring it early, so the multiply's **latency becomes a function of its
operand's value** - the channel THOR attacks. The baseline's `multByZero` is the
zero fraction showing through directly, 3,552 of 4,096 candidates. Under
ExpeDITe the gate aborts before the candidate test, so `candidates` falls to 0
and `ditSuppressed` reads **4,096** - every multiply the baseline saw. That
equality is what distinguishes "the gate saw every one" from "the workload
stopped producing them".

### mb3 - the DMP

Augury's activation pattern: a 65,536-entry array of pointers walked in order
and **never dereferenced by the program**. The DMP dereferences them anyway - it
scans every line it fills for pointer-shaped words and prefetches what they
point at - so the set of lines resident after the walk is a function of the
secret pointer values. Targets are shuffled across a 1 MB arena so a prefetch of
a pointed-to line cannot be confused with next-line prefetching, and the array
is sized past the L2 so its lines are genuinely filled: the DMP only scans on a
fill, so an array that fits in cache measures nothing.

**mb3's gate is not mb1's and mb2's.** Those key on the `DitCC` operand because
they must match the Arm instruction list. E1.2.5's prefetch clause is written
over the DIT *sequence*, so this gate keys on region membership and is stamped
on the request in the LSQ. That makes mb3 the only one of the four that would
still fail if the pass covered every secret *instruction* but put the region
boundary in the wrong place.

### mb4 - the speculation rule

Arm ARM E1.2.5 counts instructions that only ever execute speculatively as part
of the DIT sequence. So a `msr dit, #0` that runs on a wrong path must not
un-gate what is behind it.

The victim has a secret-dependent early exit - `if (v & 1) { sink ^= acc;
return; }`. `sink` is volatile, and that is what keeps the branch a branch: a
volatile store cannot be hoisted or speculated, so the two exits cannot be
if-converted into a `csel`. What the compiler emits:

```asm
        msr   DIT, #0x1                 ; region opens
loop:   ldr   x10, [x0, w10, uxtw #3]   ; the secret
        eor   x11, x10, x9
        add   x8, x11, x8
        tbnz  w10, #0x0, exit           ; SECRET-DEPENDENT, mispredicts
        ...                             ; in-region work
        b.ne  loop
exit:   msr   DIT, #0x1                 ; re-assert at the mixed join
        ...                             ; sink ^= acc
        msr   DIT, #0x0                 ; <-- reached on the wrong path
        ret
```

Both exits share one block, so there is one clear rather than two. When the
predictor takes `tbnz` while it is architecturally not taken, the core
speculatively runs that block: the clear executes, the `ret` returns on the
return-address stack, and everything the caller does after the call becomes the
clear's shadow while the region is architecturally still open. The shadow is a
stride-0 read of a **public** table - E1.2.5's requirement is about the
sequence, not the data, so what is behind a squashed clear must stay gated
whether or not it touches a secret, and keeping the shadow public means any
prediction counted here is the rule being broken rather than the placement
missing something.

## The arms

Three, plus one control that only mb4 needs.

| arm | binary | switch design | what it isolates |
|---|---|---|---|
| `base` | empty seed | none: no DIT anywhere | **the positive control** for mb1-mb3 |
| `apple` | seeded | the write is renamed and **unordered**; the architectural mode moves at the commit-time flush | a real design, and the one with a window |
| **`expedite`** | seeded | renamed, with the clear deferred until it cannot be squashed | ours |
| `writeback` | seeded | the clear published when it **executes** | **the positive control** for mb4 only |

`base` and the two hardened arms run the identical hardened binary except for
`base`, so anything that differs between `apple` and `expedite` is the switch and
not where the compiler put the region.

**`--apple`'s window, and why it is asymmetric.** Since PR #110 the `msr DIT`
carries no `IsNonSpeculative`, so younger instructions are not held behind it,
while `dit::archDitSet` still reads the architectural mode, which moves only at
commit. Between a DIT write's rename and the flush it raises when it commits,
younger instructions therefore execute against the **old** mode. Which way that
cuts depends on the direction of the write:

- at a **set**, the old mode is 0, so in-region instructions run **unprotected**
  - an entry-side leak;
- at a **clear**, the old mode is 1, so they stay gated - the exit side is safe
  by construction.

That asymmetry decides which probe can see it, and it is why mb4 needs a
different control: mb4 attacks the exit boundary, where `--apple` is safe for a
reason rather than by luck. Its control is the early-publish strawman, which has
the same compiler-placed clear and publishes it when it executes. That flag had
been removed from the gem5 config as unreachable and is restored by this
experiment, restricted to `--expedite`; `DeferredDestPublish`'s own docstring
says the value exists for exactly this.

The `cs` row is a sixth row and **not a fifth microbenchmark**: it is
`benchmarks/dit_clear_shadow` run under the same three switch designs. Its DIT
is inline asm, so it says nothing about the taint pass; it is here because it is
the gadget shaped to catch `--apple`'s entry window, and an experiment that
reported `--apple`'s zeros without it would be claiming more than it measured.

`mb4nt` is mb4 rebuilt at `-taint-dit-clone-seeded=0`. It is a control on the
DIT twins rather than a fifth microbenchmark - see "What the twins did" below.

## Result

One microbenchmark per gated optimization, three arms each. `data/results.csv`.

| microbenchmark | gated optimization | readout | `base` | `apple` | **`expedite`** |
|---|---|---|---:|---:|---:|
| value prediction | EVES load value prediction | predictions of the payload's loads | 1,023 | **482** | **0** |
| computation simplification | trivial-operand multiply shortcut | multiplies run with operand-dependent latency | 32,768 | **32,768** | **0** |
| DMP | pointer scan on an L2 fill | prefetches of secret-addressed lines | 64,516 | **8** | **0** |

**Every gated optimization leaks on the baseline, leaks under `--apple`, and
reads zero under ExpeDITe.**

The baseline is the same binary with the mode switches omitted, so its column is
the optimization running unobstructed. Note it cannot be obtained by leaving the
switch flag off: with no flag the simulator selects the serialising drain, and
the region is still live.

### The multiply row is capped, and why it has to be

`CompSimplifier::trySimplify` runs once per **issue** (`inst_queue.cc:936`), so
its counter counts issue attempts, not instructions. `--apple` squashes the
payload every time its mode switch commits, and the replay factor is large and
shape-dependent: measured **5.0** simplifications per architectural multiply at
one multiply per region entry and **6.0** at two. The raw reading on the shipped
gadget is 63,512 over 32,768 architectural multiplies.

Quoted raw that exceeds the unprotected baseline, which is nonsense as a
security reading - it says protection made the leak worse when what actually
happened is that the same multiply leaked and was replayed. So the row reports
the quantity that means something: **how many multiplies ran with
operand-dependent latency**, which cannot exceed the number of multiplies that
exist. The payload is homogeneous - same operands, same PCs, every iteration -
so a gate that let 63,512 issues through is not systematically stopping any
particular one: all 32,768 leaked. ExpeDITe stops every issue it sees, 61,446 of
them, and leaks none.

The supporting counts, for anyone who wants them:

| arm | multiply issues the gate saw | let through | suppressed |
|---|---:|---:|---:|
| `base` | 32,768 | 32,768 | 0 |
| `apple` | 127,124 | 63,512 | 63,612 |
| **`expedite`** | 61,446 | **0** | **61,446** |

One number in that table is worth knowing about for its own sake: ExpeDITe's
61,446 issues for 32,768 multiplies, on a design that never flushes. When
`trySimplify` returns false the instruction falls through to normal FU
allocation, and if the multiplier is busy it stays in `readyInsts` and is
retried - so `ditSuppressed` inflates under FU contention too. Neither side of
this counter counts instructions.

### All of it is the speculation path

Every `--apple` leak above happens **while the mode switch is still in flight**.
The work executes against the stale mode and is flushed when the switch commits.
Architecturally nothing leaked: each multiply's committed execution runs with
DIT correctly set and is suppressed, and `committedInstType::IntMult` is exactly
32,768 with no leaked survivors. The leak is entirely microarchitectural.

That is what Arm ARM E1.2.5 forbids - it counts instructions that only ever
execute speculatively as part of the DIT sequence, so a design that establishes
the mode at commit has already leaked by the time it fixes anything. The
consequence differs by mechanism:

- **value prediction and computation simplification**: the leak is the timing
  and power of work that never committed. The squash undoes the result, not the
  execution, and the prediction was already issued and already consumed by
  dependents.
- **the DMP**: the leak is durable. The prefetch is issued and the line fetched
  into the L2 (`l2caches.prefetcher.pfIssued`); squashing the access that
  triggered it does not evict it. The residue outlives the speculation, which is
  the GoFetch shape.

### The speculation rule: a clear on the wrong path

A fourth microbenchmark, and the only one where `--apple` legitimately does not
leak.

| microbenchmark | readout | `base` | `apple` | **`expedite`** | early-publish |
|---|---|---:|---:|---:|---:|
| speculation rule | `predictionsInDitSequence` | n/a | 0 | **0** | **2,129** |

A different speculation question from the three above: not "does work inside the
region run unprotected" but "does a clear that never architecturally executes
un-gate what follows it". A `msr dit, #0` on a secret-dependent early exit runs
on a mispredicted path; everything behind it is architecturally still inside the
DIT sequence.

`--apple` cannot fail this one, and not by luck: it reads the architectural
mode, which at a **clear** is still 1, so the shadow stays gated. Its window
leaks at region *entry*, which is what the three rows above measure. So neither
the baseline nor `--apple` can falsify ExpeDITe's zero here, and the row carries
a fourth arm - the early-publish strawman, the same clear published when it
executes. ExpeDITe's deferral is exactly what this tests: **0 of the 475**
predictions issued in a clear's shadow, against the strawman's **2,129 of
38,804**.

### The one derived number

The value-prediction row is the only one needing a derivation.
`valuePredictor.loadPredictions` is a whole-core counter, and that probe's
training calls predict loads of their own outside any region, so all three arms
carry the same floor. Measured on the ExpeDITe arm, where the payload
contributes nothing by construction, it is **1,023**; raw readings are 2,046 /
1,505 / 1,023.

That derivation is corroborated by a counter that needs no floor at all.
`predictionsInDitSequence` is region-scoped and reads **485** on `--apple`
against **0** on ExpeDITe, against the derived 482. Two independent instruments
agreeing to within three events is what makes the row trustworthy; neither is
quoted alone.

Its magnitude is also layout-sensitive: over five `argv[0]` offsets `--apple`
reads 470, 485, 488, 492 and 496 while `--expedite` reads exactly 0 at every
one. Quote the range, not the cell. The zero does not move.

### Where each leak comes from

**Value prediction.** 16 stride-0 loads of one hot line behind the switch. The
switch is unordered under `--apple`, so loads dispatched alongside it can issue
before it executes and read the stale mode.

**Computation simplification, and this one has a named cause.** 63,512
multiplies simplified inside a live DIT region against 32,768 with no DIT at
all. It leaks *more* than the unobstructed baseline because `--apple` flushes at
commit and the payload executes more than once; the counter is simplification
events, not distinct multiplies. The cause is not the window alone:
**CompSimp is the only caller of `dit::classify` that passes
`check_ready=false`** (`comp_simplifier.cc:71`). Every other caller takes the
default, gets `Why::NotReady` on an operand that is not yet readable, and fails
safe by treating it as in-region. CompSimp reads the DitCC regardless and acts
on a stale value. That is BUG-2 in
`docs/dit/design/dit-data-independent-timing.md`, and this row demonstrates it.

**The DMP.** 8 pointer prefetches, one fill scanned: a request stamped while the
architectural mode was still 0, whose fill arrived after the region had opened.
Small, and reported rather than rounded away - it is the mechanism GoFetch turns
into key recovery.

### What `--apple`'s window is, and what it is not

It is open while the switch is **in flight**, and the reachable part is
**rename to execute**, not execute to commit. Three things that sound like they
would widen it and do not, each measured:

- **Nops between the switch and the payload close it**, by pushing the payload
  past the switch's execute. A gap sweep from 0 to 192 reads zero throughout.
- **A cache-missing load older than the switch** delays its commit, not its
  execute. `ditTaggedNotReady` read 0 and `ditTaggedSet` read 163,606: the
  payload's DitCC was ready and already 1.
- **Feeding the register form `msr DIT, Xt` from a DRAM miss** does widen
  rename-to-execute, and ExpeDITe answers it exactly as designed
  (`ditTaggedNotReady` 32,696, the gate held conservatively) - but in that shape
  the predictions that appeared were the delay load's own, not the payload's,
  and vanished once the delay load was made unpredictable.

What reaches it is many independent instructions dispatched alongside the
switch. That is a property of the gadget, and it is why the value-prediction and
computation-simplification probes write their `msr dit` in inline asm while the
DMP probe does not need to.

### What `--apple` costs

| probe | `base` | `apple` | **`expedite`** | apple / expedite |
|---|---:|---:|---:|---:|
| mb1 | 48,245 | 48,390 | 48,254 | 1.00x |
| mb2 | 20,528 | 20,560 | 20,530 | 1.00x |
| mb3 | 65,663 | 49,249 | 49,174 | 1.00x |
| mb4 | 204,079 | 344,449 | **199,712** | **1.72x** |

Cycles in the ROI. The three probes that enter their region once pay nothing for
the flush; mb4, which enters 2,048 times and pays a flush per DIT write, costs
**72% more** under `--apple` than under ExpeDITe. The cost of that design is per
switch, so it tracks region entries exactly as its leak does.

### mb4 in full

| arm | predictions behind an in-flight clear | retired | **in-sequence** | behind a squashed set | cycles |
|---|---:|---:|---:|---:|---:|
| `apple` | 42,574 | 0 | **0** | 2,086 | 344,449 |
| **`expedite`** | 475 | 245 | **0** | 0 | **199,712** |
| `writeback` | 38,804 | 32,924 | **2,129** | 0 | 200,293 |

ExpeDITe's zero is 0 of 475, not the absence of a shadow: 245 retired, 190 were
squashed by something younger, 40 were on the same path, and the accounting
closes exactly. `writeback` has a shadow of the same order and leaks from it,
which is what makes that zero a measurement.

`--apple` has by far the largest shadow, 42,574, and leaks none of it, because at
a clear the old mode it reads is 1 and everything behind it stays gated. Its
2,086 predictions behind a set sat behind sets that were themselves squashed on a
wrong path, so no region ever opened and the oracle counts them as benign.

### What the twins did, and why mb4 is built twice

Under the shipped defaults a caller that is itself DIT-on calls `victim.dit`, a
twin entered DIT-on by construction that **emits no clear at all** - so where a
twin is called there is no wrong-path clear anywhere in the binary and mb4's
question has nothing to bite on. We hit that: an earlier revision of these
probes left `main` holding a secret, `main` went DIT-on, it called the twin, and
mb4's ExpeDITe *and* strawman columns both read zero. That is a real and
welcome property of the twins design, but it is not an answer, so mb4 is also
built at `-taint-dit-clone-seeded=0` (`mb4nt`). The two agree: 0 against 2,129
and 0 against 2,271. The rule holds either way, and it is tested where a clear
exists.

## The five quantities

Like 04, 06 and 12 this experiment has **no public lane and no secret lane**, so
`f_secret`, `C_public` and `C_secret` do not apply; the cycle column above
stands in for cost and is per switch design rather than per lane. The two that
do apply:

- **Work per region:** one victim call - 4,096 loop iterations for mb1 and mb2,
  a 65,536-entry pointer walk for mb3, up to 64 iterations for mb4.
- **Toggles per unit work:** 2 or 3 committed DIT writes per ROI for mb1-mb3
  (one region, entered once); **6,144 for mb4**, three per call over 2,048
  calls, which is what puts a clear in flight often enough for the rule to be
  tested at all.

## Validity gates

All 46 pass, printed by the rig on every run. Each exists because a zero in a
leak column is worthless without it, and four of them caught a wrong reading
during construction.

| gate | why | result |
|---|---|---|
| checksum identical across all five arms | a design that changed the architectural result would invalidate the comparison | 1 distinct value per microbenchmark |
| the positive control leaks | otherwise a zero is indistinguishable from a mechanism that was idle | 4,029 / 3,552 / 64,516 / 2,129 / 2,271 |
| `--apple` is safe at the exit boundary | it reads the architectural mode, still 1 at a clear, so this is a property to pin rather than a result to hope for | exit 0, entry 0 on both mb4 rows |
| the ExpeDITe arm does not | the claim | 0 on all five |
| the control arm is present at all | mb4's control is not in the default arm set, so the rig adds it rather than letting the zero stand unfalsified | added automatically |
| ROI work identical once DIT writes are counted | a flat tolerance fails here: mb4's 6,144 switches are 2.5% of its ROI. The gate is that the difference **equals** the committed DIT writes, within the marker's own slop | residual 0 on all five |
| the hardened arm executes inside a region | per mechanism: `ditTaggedSet` (mb1, mb4), `ditSuppressed` (mb2), `notifyDropDit` (mb3) | 98,168 / 4,096 / 8,191 / 351,293 |
| the base arm sets no DIT | the control must actually be uncontrolled | 0 on all five |
| mb4's branch actually mispredicts | no misprediction, no wrong-path clear, no test | 2,183 and 2,228 |
| mb4 issues predictions in a clear's shadow at all | otherwise `in_sequence = 0` is the absence of a shadow, which is `drain`'s answer, not ExpeDITe's | 475 and 798 |
| mb4's clear-shadow oracle accounting closes | `behindClear` must equal retired + in-sequence + squashed-younger + same-path, or predictions are being dropped | exact on both |
| `iew.earlyPublishClaimViolations` = 0 on every sound arm | the frontier's own safety claim, a panic on sound builds | 0 |

## What went wrong on the way, since the fixes are the interesting part

Four constructions read as clean zeros while measuring nothing. Each is now a
gate; they are recorded because the next person to build one of these will hit
them.

1. **A loop-invariant secret is not a workload.** mb1 first loaded eight fixed
   offsets. LICM hoisted every one out of the loop, the dependent work went with
   them, the body reduced to adding a constant, and *both* columns read zero.
2. **A secret-dependent branch is not automatically unpredictable.** mb4 first
   entered the victim at index 0 every call, so the early exit fell on the same
   iteration every time and TAGE learned it exactly: 14 mispredicts in 2,048
   calls and no wrong-path clear worth counting.
3. **A secret stored through an argument pointer blankets the caller.** mb4
   originally wrote its accumulator into a scratch buffer passed as a parameter.
   A secret stored through an argument pointer is an unknown store from the
   callee's view, so its memory summary truncates to TOP (the deliberate P0
   bluntness) and at the call site every subsequent load in the caller is
   poisoned. `main`'s **public** shadow loop came back tainted, `main` went
   DIT-on from its first instruction, the shadow was gated, and no prediction
   was ever issued behind a clear. This is correct conservative behaviour by the
   pass, and it is worth knowing how far it reaches. (The scratch buffer is gone
   entirely now: it was never read, so it was dead code, and the volatile store
   to `sink` is what actually keeps the branch.)
4. **The declassification point decides where the region ends up.** A `printf`
   of a secret-derived checksum inside `main` is a Need; region placement
   coarsens outward from it and `main` ends up DIT-on with the ROI markers
   *inside* the region. Zero DIT writes then commit in the ROI, the whole
   measurement window runs gated, and the placement boundary the microbenchmark
   exists to exercise is never crossed while the counters are running. All four
   probes isolate the print in a `report()` function, which is what experiment
   04 does and for the same reason.

Two counters also mislead if read carelessly. `ditTaggedSet` lives on the value
predictor and reads zero on mb2 and mb3, which run without `--eves`; using it as
the region gate failed two microbenchmarks that were working perfectly. And the
count of DIT writes committed *in the ROI* is not evidence of coverage - it is
zero exactly when the region encloses the ROI, which is a protected run, not an
unprotected one.

## Limits

- **Four mechanisms, not a coverage proof.** These are the three optimizations
  this fork gates plus the rule they rest on. A fourth optimization, or an
  instruction class the ISA never tagged, would not show up here - that is what
  `util/dit/check_tag_set.py` and the gate probes are for.
- **The leak counts are not attack rates.** We read the mechanism directly, so a
  baseline column counts events an attacker would still have to detect through a
  timing channel. The zeros are the strong half; the magnitudes are upper
  bounds.
- **Microbenchmarks, deliberately.** Each gadget is the most favourable input
  its mechanism has - stride-0 values, a 50% zero fraction, a shuffled pointer
  array past the L2. That is what makes the gate readable, and it says nothing
  about how often real code presents these shapes. What real code presents is
  experiments 04, 08 and 09.
- **The secret is declared.** These test the gate given a correct seed. Whether
  the seed is correct is experiment 08's question; what the seed loop costs is
  experiment 07's.
- **Single-threaded SE mode.** An executed load stays squashable by a snoop
  until it commits on a multicore, a root of speculation these runs never see.
- **mb1's `drain` residual is 3 predictions and is not explained.** It is three
  events out of a baseline of 4,029, on an arm that is not the shipped design,
  and we have not instrumented which three. It is reported rather than rounded
  away.
- **mb3's residual attribution is read off the arm structure**, which is strong
  - `apple` leaks at entry and `writeback` at exit, exactly as experiment 12
  predicts for those two designs - but it is not a per-request trace.

## Reproducing

The rig lives with the harness, in the gem5 fork:

```sh
# gem5-DIT
scons -C util/m5 build/arm64/out/libm5.a
scons build/ARM/gem5.fast -j <n>
benchmarks/expedite_security/build_expedite_security.sh
CXX=g++ GAPS=24 benchmarks/dit_clear_shadow/build_dit_clear_shadow.sh
benchmarks/expedite_security/run_expedite_security.py -o out/expedite_sec -j 25
~/.venvs/dit-plots/bin/python \
    benchmarks/expedite_security/plot_expedite_security.py \
    --results out/expedite_sec/results.csv --out out/figs
```

23 cells, about 12 seconds on 25 cores. Deterministic for a given binary path: the
guest stack moves with the length of `argv[0]`, which the `cs` row's magnitude is
sensitive to (see above). The build script needs the
taint clang and defaults to the parent repo's `build/`; it fails loudly if the
base arm carries a DIT write or the hardened arm carries none.
`out/expedite_sec/results.csv` is `data/results.csv` verbatim. The probes' own
documentation is `gem5-DIT/docs/dit/benchmarks/expedite-security.md`.

Arms default to `base,apple,expedite`; `--arms` also accepts `drain` (the
unconditional serialising write) and `writeback`. The rig adds each row's
control arm itself, so mb4 always gets `writeback` whatever `--arms` says.

**This experiment requires a gem5-DIT change that is not yet committed**: the
four probes and their rig under `benchmarks/expedite_security/`, their
documentation, and `--dit-clear-publish` restored to the FDP config. The
submodule pin must move with the results, in the same PR - onto
`25675e4aff03` or later, since `--apple`'s behaviour here depends on PR #110.

## Contents

| path | what |
|---|---|
| `data/results.csv` | 23 cells: 3 arms on every row, plus mb4's control on the two mb4 rows |
| `data/results.md` | the same as tables, with every validity gate |
| `figures/leaks.png`, `.pdf` | the leak column per microbenchmark per arm, symlog, zeros capped in their arm's colour |
