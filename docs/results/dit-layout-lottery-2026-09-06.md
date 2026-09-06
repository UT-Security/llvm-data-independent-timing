# The layout term is a relink lottery, and three quarters of it is not the switches

**Measured 2026-09-06, gem5 NeoverseV2 FDP (`gem5-DIT` master `ce05d32089`),
libsodium 1.0.21 `--disable-asm`, CIO's own drivers, compiler `00f86ba06b5e`
at the 2026-09-05 defaults.** 280 new gem5 cells (60 decomposition, 220
relink), every gate passed. Data
under `docs/results/data/layout-*2026-09-06*`; the full table dump is
`data/layout-lottery-2026-09-06-analysis.txt`.

Experiment 09's renamed column says the pass costs nothing once you subtract
its own NOP twin, and that the twin itself is worth -6.33 to +6.10 points
depending on the row (`paper_experiments/09-libsodium-cio-parity/rerun-2026-09-06.md`).
This asks what that twin term *is*, and whether the compiler has any handle on
it.

## 1. Short version

- **The published alignment claim reproduces exactly.** `reproduce.sh`'s
  header says block alignment collapses the layout spread from 11.46 to 2.93
  points. Recomputed from the committed CSVs: **11.46 -> 7.93 -> 2.93** at no
  / 16 B / 64 B, worst |layout| 6.94 -> 5.70 -> 1.98, mean 1.97 -> 1.84 ->
  0.75. Every figure in `utils/dit_host_screening/cioparity/RESULTS.md`'s
  alignment table is right to the decimal.
- **But the mechanism recorded there is wrong.** RESULTS.md reads 16 B doing
  little and 64 B working as confirming "I-cache line placement". Across
  **350 gem5 cells** covering every arm and every relink offset in this study,
  the worst instruction-cache miss rate seen anywhere is **1 per 13,831
  accesses**, and L1D and L2 are the same. libsodium's hot code fits; nothing
  here is a cache effect. The extra cycles are cycles in which the fetch stage
  delivered no instruction while every fetch hit.
- **The term the rig calls "layout" is three different things**, and the
  switches are the smallest. Splitting it with two new arms (`rt`, `notwinnop`):
  the TU-wide **tail-call disable** contributes a mean 1.78 points, the
  **switch slots themselves** 1.41, and the **DIT twins** 3.53. Only the middle
  one is what the phrase suggests.
- **A pure relink reproduces the whole thing.** Link the *unhardened* library
  4 to 256 bytes further along in `.text` - unreachable padding, identical
  instruction stream, identical committed instruction count - and the same
  five benchmarks move over a **7.04-point** range. On three of five rows that
  null spread is larger than the layout term the rig attributes to the pass.
  **The layout term is one draw from a lottery the pass does not participate
  in.**
- **Read as ensembles rather than as one A/B, four of the five rows' clouds
  overlap.** aes-gcm encrypt's headline -6.33% becomes -0.15% median to
  median, with the pairwise range spanning -6.79 to +0.66.
- **No, the cost model cannot price it,** and the reason is not effort: the
  perturbation that produces it is invisible to the pass by construction. The
  admission test already prices instruction insertion through
  `-taint-dit-switch-cyc`, and that knob is a three-step staircase that
  saturates at 30.
- **There is a rig fix and it is worth taking:** experiment 09's `base` arm is
  plain `-O2` and is *not* codegen-matched to the hardened arms, because
  `-taint-no-tail-calls` rides on `-ftaint-harden`. Rebasing the pass column
  on `rt` moves it by -0.10 to -4.65 points. CLAUDE.md's claim that
  `-ftaint-harden=<empty>` is byte-identical to `-O2` has been false since
  2026-09-01.
- **64 B alignment should stay opt-in, and for a narrower reason than
  recorded.** It does not shrink the size of a layout draw; it *quantises*
  code motion to whole cache lines, so a sub-64 B code-size change is absorbed
  entirely. Distinct outcomes over 12 relink offsets go 12 -> 4. But the step
  between two adjacent 64 B buckets is still 7.67 points on ed25519, and the
  flag costs +3.07% library text and up to +3.08% runtime.

## 2. Splitting the term

Two arms are new (`build_arms.sh`, `run_cio_gem5.py`):

| arm | what it is | switches | twins | tail calls |
|---|---|---|---|---|
| `base` | plain `-O2` | 0 | no | **on** |
| `rt` | `-ftaint-harden=<empty seed file>` | 0 | no | off |
| `notwinnop` | shipped flags, `-taint-dit-clone-seeded=0`, `-taint-dit-nop-switches` | 0 (HINT #0) | no | off |
| `notwin` | shipped flags, `-taint-dit-clone-seeded=0` | real | no | off |
| `taintnop` | shipped flags, `-taint-dit-nop-switches` | 0 (HINT #0) | yes | off |
| `taint` | shipped defaults | real | yes | off |

`rt` was already defined in `build_arms.sh` and simply never run; `notwin` /
`notwinnop` are added here. Cycles per operation, `spec` (renamed switch):

| bench | base | rt | notwinnop | notwin | taintnop | taint |
|---|---|---|---|---|---|---|
| ed25519 | 78,789.9 | 78,072.4 | 78,137.7 | 78,169.0 | 76,946.0 | 77,193.4 |
| chacha-enc | 2,185.9 | 2,243.8 | 2,229.9 | 2,216.3 | 2,289.0 | 2,269.9 |
| chacha-dec | 2,289.8 | 2,369.6 | 2,387.4 | 2,405.8 | 2,429.5 | 2,442.6 |
| aes-enc | 1,217.0 | 1,218.3 | 1,212.0 | 1,211.0 | 1,140.0 | 1,142.0 |
| aes-dec | 1,076.7 | 1,095.6 | 1,150.0 | 1,180.6 | 1,088.7 | 1,177.0 |

All 30 cells shared with `data/gem5_apple_bracket_2026-09-06.csv` reproduce
**identical to the decimal** in a different work directory. As percentages of
`base`:

| bench | A tail-call | B switch slots | C twins | A+B+C = "layout" | D renamed sw | E serialising sw |
|---|---|---|---|---|---|---|
| ed25519 | -0.91 | +0.08 | -1.51 | **-2.34** | +0.31 | +0.85 |
| chacha-enc | +2.65 | -0.64 | +2.70 | **+4.72** | -0.87 | +34.59 |
| chacha-dec | +3.49 | +0.78 | +1.84 | **+6.10** | +0.57 | +31.93 |
| aes-enc | +0.11 | -0.52 | -5.92 | **-6.33** | +0.16 | +9.73 |
| aes-dec | +1.76 | +5.05 | -5.69 | **+1.11** | +8.20 | +23.25 |
| **spread** | 4.40 | 5.69 | 8.62 | **12.43** | 9.07 | |
| **mean \|term\|** | 1.78 | 1.41 | 3.53 | **4.12** | 2.03 | |

A = `rt - base`, B = `notwinnop - rt`, C = `taintnop - notwinnop`.

Three things fall out.

**The switch slots are the smallest of the three.** Inserting the `msr DIT`
instructions and moving everything after them is worth a mean 1.41 points and
never more than 5.05. The term the rig reports is dominated by the other two.

**Term C is not purely address motion.** The twins remove executed switch
slots as well as moving code: on ed25519 `notwinnop` commits 175,272
instructions per signature against `taintnop`'s 171,813, because the untwinned
compiler executes **3,463** `msr DIT` per signature where the twins execute 2.
On the AES rows the instruction counts differ by 10 and 13 while the cycles
differ by 72 and 61, so there it *is* address motion.

**The twins are not on trial.** They are the reason the serialising column is
survivable at all: untwinned, ed25519 costs **+94.66%** serialising against
`taint`'s -1.50%, chacha +65/67% against +38/39%, aes-dec +56.92% against
+24.36%. A 3.53-point mean layout term is a cheap price for that.

## 3. The null control

`utils/dit_host_screening/cioparity/relink_null.sh` links the **unhardened**
library K bytes further along in `.text`. The padding is unreachable `nop`s in
their own section ahead of everything else. Not one instruction of the program
changes, not one instruction of the padding executes, and `run_cio_gem5.py`'s
own gate confirms the committed instruction count is identical for every K.
Every difference between two of these binaries is address placement.

Percent against K = 0, `spec`:

| bench | K=4 | K=8 | K=12 | K=16 | K=24 | K=32 | K=48 | K=64 | K=96 | K=128 | K=256 | spread |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| ed25519 | +2.76 | +0.83 | -1.46 | +1.29 | +1.59 | -0.60 | +1.20 | +0.21 | -0.13 | +3.94 | +3.45 | **5.40** |
| chacha-enc | +0.51 | +2.43 | +3.30 | +1.13 | +1.70 | +0.06 | +2.29 | -1.23 | -0.20 | -0.44 | +1.80 | **4.53** |
| chacha-dec | -0.85 | +1.75 | +0.58 | +0.13 | +6.18 | +2.92 | +3.27 | +0.17 | +0.57 | +1.36 | +0.40 | **7.04** |
| aes-enc | +0.32 | +0.18 | +0.16 | +0.12 | +0.35 | +0.25 | +0.50 | +0.01 | +0.16 | +0.00 | +0.02 | **0.50** |
| aes-dec | +0.38 | +0.34 | +1.48 | +0.47 | +0.76 | +1.21 | +1.06 | +0.02 | +1.82 | +0.37 | -0.12 | **1.94** |

**Six unreachable NOPs move chacha20-poly1305 decrypt by +6.18%**, which is
more than the entire layout term the rig books against the pass on that row
(+6.10%). On ed25519, chacha-dec and aes-dec the null spread exceeds the
layout term outright.

Eight further offsets, all whole multiples of the 64 B fetch line, from 512 B
to 64 KB - enough to remap the entire binary through every L1I set - stay
inside -2.06 to +1.32. So the sensitivity is not monotone in displacement and
does not grow with it; it is a property of the particular addresses drawn.

### Read as ensembles

The rig publishes the K = 0 pair. Running the same 12 offsets on the
**hardened** `taintnop` library as well gives two clouds instead of two points:

| bench | base min/med/max | taintnop min/med/max | K=0 term | median term | clouds overlap |
|---|---|---|---|---|---|
| ed25519 | 77,641 / 79,590 / 81,893 | 75,661 / 76,645 / 77,213 | -2.34% | **-3.70%** | no |
| chacha-enc | 2,159 / 2,204 / 2,258 | 2,212 / 2,254 / 2,319 | +4.72% | **+2.28%** | YES |
| chacha-dec | 2,270 / 2,303 / 2,431 | 2,316 / 2,375 / 2,451 | +6.10% | **+3.14%** | YES |
| aes-enc | 1,217 / 1,219 / 1,223 | 1,140 / 1,217 / 1,225 | -6.33% | **-0.15%** | YES |
| aes-dec | 1,075 / 1,081 / 1,096 | 1,086 / 1,089 / 1,099 | +1.11% | **+0.71%** | YES |

Pairwise, one arm's 12 draws against the other's, the aes-enc term ranges
-6.79 to +0.66 and the chacha-dec term -4.76 to +7.97. **The published -6.33%
on aes-gcm encrypt is the extreme of its own distribution**: the `base`
library happens to be insensitive there (its own spread is 0.50 points) while
the hardened one is not (7.46 points), and K = 0 draws the hardened library's
best offset.

ed25519 is the one row where the clouds separate, and they separate the way
the term already said: the hardened binary is genuinely faster, by 3.70% on
medians rather than the 2.34% one draw reported.

## 4. What the mechanism is, and is not

**It is not any cache.** Across all 350 cells - the six-arm decomposition, the
three relink sweeps, and the committed 2026-09-06 arms - the worst cell
anywhere reads 1.000 L1I miss, 0.150 L1D miss and 0.220 L2 miss *per
operation*, against 13,831 to 16,183 L1I accesses and ~53,000 L1D accesses per
ed25519 signature. The instruction working set fits in the 64 KB 8-way L1I and
never leaves it. RESULTS.md's inference that "16 B does little while 64 B works
confirms the mechanism is I-cache line placement" does not survive the
counters: 64 B works for a different reason (§5).

**It is not the DIT mode.** Every arm and every offset in §3 executes zero
`msr DIT`, `commit.ditCycles` is zero in all of them, and the runner gates that
their cycle counts are identical under both switch models.

**It is not the value predictor, the branch predictor, or the L1D
prefetcher,** each of which is PC-indexed and therefore a plausible suspect.
On the largest clean pair available - the 64 B-aligned ed25519 build at K = 0
against K = 4, a +7.67% step with an identical instruction stream - the value
predictor makes 310 *more correct* predictions in the slower binary (3,747 ->
4,057), every committed return is predicted correctly in both
(`ras.correct == ras.used`), and the L1D prefetcher's `pfUseful` is **0** in
both, so it contributes nothing to either.

**What the extra cycles are** is front-end delivery. Within each benchmark,
across the 20 relink offsets, cycles track `fetch.nisnDist::0` - cycles in
which the fetch stage delivered no instruction - at r = +0.75 to +0.95 with a
slope near 1.0 on the three benchmarks with real spread, while every fetch
hits in L1I. On the +7.67% aligned pair the accounting is almost exact:
+5,994 cycles, +5,980 cycles of `fetch.status::running`, +5,976 of
`fetch.ftNumber::0`, +5,945 of `decode.status::Blocked`.

**What the counters cannot do is separate cause from consequence.** Fetch
delivering nothing while decode is blocked is equally the signature of a
starved front end and of a back-end stall propagating upstream, and in a
1,200-cycle region at IPC ~1.2 with the issue queue full half the time, both
descriptions fit. No structure singles itself out. That is the honest
attribution: the layout term is where the instruction stream lands relative to
fetch and issue boundaries, it is not mediated by any cache or predictor whose
counter we can read, and it is a per-binary property rather than a per-policy
one.

## 5. What 64 B alignment actually does

Re-running the null control on a library built with
`-align-all-nofallthru-blocks=6`:

| bench | unaligned K=0 | aligned K=0 | alignment costs | unaligned spread | aligned spread | distinct outcomes |
|---|---|---|---|---|---|---|
| ed25519 | 78,790 | 78,113 | -0.86% | 5.48% | **7.67%** | 12 -> 4 |
| chacha-enc | 2,186 | 2,253 | +3.08% | 4.59% | 2.59% | 12 -> 4 |
| chacha-dec | 2,290 | 2,334 | +1.93% | 7.10% | 1.13% | 12 -> 4 |
| aes-enc | 1,217 | 1,208 | -0.74% | 0.50% | 0.07% | 11 -> 2 |
| aes-dec | 1,077 | 1,083 | +0.59% | 1.94% | 0.25% | 12 -> 4 |

The mechanism is visible in the symbol table, not in the timings: under 64 B
alignment, pads of 4, 8, 24 and 64 bytes all place `crypto_hash_sha512` at the
*same* address, because the padding is absorbed by the alignment slack. The
flag does not make the machine insensitive to where code sits. It **quantises
code motion to whole cache lines**, so any code-size change smaller than the
slack moves nothing at all - which is exactly the change inserting a handful of
`msr DIT` makes.

That is a real benefit for a measurement rig and it is why the published
spread collapses. It is not a benefit for a shipped binary, and it is not free:
+3.07% library text, and up to +3.08% runtime on three of five rows. And it
does not bound a draw - ed25519 still steps **7.67 points** between two
adjacent 64 B buckets, worse than its unaligned spread.

**Recommendation unchanged in substance, corrected in rationale: 64 B
alignment stays an evaluation flag, off by default.** RESULTS.md's follow-on
suggestion - a targeted version aligning only hot loop heads - inherits the
same limitation and would still not bound a draw.

## 6. The rig defect: `base` is not codegen-matched

`base` is plain `-O2`. Every `taint*` arm carries `-ftaint-harden`, and since
2026-09-01 the TU-wide tail-call disable rides on that flag's presence. So the
arms differ in codegen before a single switch is placed:

| variant | `libsodium.a` | vs base | tail-call `b` | `bl` | `ret` | static `msr DIT` |
|---|---|---|---|---|---|---|
| base | 690,598 | - | 279 | 5,444 | 1,626 | 0 |
| rt | 696,734 | +0.89% | 235 | 5,488 | 1,693 | 0 |
| notwin | 697,838 | +1.05% | 235 | 5,488 | 1,694 | 303 |
| taint | 796,500 | +15.34% | 235 | 5,715 | 1,772 | 167 |

(counts from the linked `eval_ed25519` binaries; the remaining 235 are in statically
linked libc, which the flag never reached.)

**CLAUDE.md's "a round-trip control (`-ftaint-harden=<empty seed file>`, zero
switches) is now byte-identical to a plain `-O2` build" is stale.** It was
verified 2026-08-31; the tail-call disable moved onto `-ftaint-harden` on
2026-09-01 and it has been false since. `rt` is +6,136 bytes of library text.

It is a *real* cost - a hardened build genuinely pays it, and it is a soundness
requirement, not a lottery - so it belongs in the pass column of a
DIT-vs-no-DIT comparison and not in a "layout" bucket. But it must not be
inside the term a NOP twin is supposed to cancel. Rebasing experiment 09's pass
column on `rt`:

| bench | taint / base | taint / rt | moves | serialising / base | serialising / rt | moves |
|---|---|---|---|---|---|---|
| ed25519 | -2.03% | -1.13% | +0.90 | -1.50% | -0.59% | +0.91 |
| chacha-enc | +3.84% | +1.16% | -2.68 | +39.31% | +35.71% | -3.59 |
| chacha-dec | +6.67% | +3.08% | -3.59 | +38.03% | +33.39% | -4.65 |
| aes-enc | -6.16% | -6.26% | -0.10 | +3.40% | +3.29% | -0.11 |
| aes-dec | +9.32% | +7.43% | -1.89 | +24.36% | +22.22% | -2.15 |

### Why the tail-call disable is not free, in either direction

`base` vs `rt` on ed25519 differ only by 44 tail calls becoming `bl` + `ret`.
Committed control flow barely moves - `IsCall` 1,183 -> 1,194, and every
committed return is RAS-correct in both - but the *speculative* work collapses:
`commit.commitSquashedInsts` **20,951 -> 8,389** (-60%), `ras.squashes` 2,654
-> 1,967, `branchPred.squashes_0::total` 2,964 -> 2,226, for -0.91% cycles.
libsodium's ed25519 tail calls cost speculation, and hardening removes them.
On the chacha rows the same change costs +2.65% and +3.49%. It is signed, like
everything else in this document.

## 7. Can the cost model price it?

No, and the reason is structural rather than a question of effort.

The perturbation in §3 is invisible to the pass by construction: the MIR is
identical, the block sizes are identical, the loop structure is identical, the
frequencies are identical. A pass cannot price an input it does not have. Final
addresses are set after it runs, by `block-placement`, `machine-outliner`,
`branch-relaxation` and then the linker, and - as §3 shows - by whatever else
happens to be linked ahead of the library.

The admission test also already prices the thing it *can* see. The comment on
`-taint-dit-switch-cyc` in `TaintAnalysis.cpp` says so directly: what 30 buys
is "fewer instructions inserted into hot loops", not the mode write, which is
unresolvable against the NOP control. And the knob is a three-step staircase -
30 and 100 produce byte-identical objects on libsodium, and every value from
300 to 100,000 produces one other object. Everything the model can merge is
merged at the shipped default. There is no headroom for a layout term to buy.

The three terms of §2 that a compiler *does* control already have knobs, and
each has been measured and settled elsewhere: `-taint-dit-clone-seeded`
(the twins are worth 60 points of serialising cost, `docs/design/dit-cloning.md`),
`-taint-dit-twin-narrow` (measured, does not pay,
`docs/results/dit-twin-narrowing-2026-09-05.md`), `-taint-no-tail-calls`
(a soundness requirement, `docs/design/dit-tailcall-gap.md`).

## 8. What should change

1. **Run `rt` in experiment 09 and quote the pass column against it.** The arm
   already existed; it costs five extra cells per sweep. `base` stays as the
   unhardened reference for the DIT-vs-no-DIT question, but `taint - rt` is
   the number that is about DIT.
2. **Fix CLAUDE.md's byte-identity claim** for `-ftaint-harden=<empty>`; it
   has been wrong since 2026-09-01.
3. **Quote a resolution limit with the layout term, and make it per-benchmark.**
   RESULTS.md's "about 10 points, not sub-point" is the right instinct; §3
   gives the measured value per row (0.50 to 7.04 points on this library) and
   `relink_null.sh` recomputes it for any workload in a dozen cells.
4. **Correct the recorded mechanism** in RESULTS.md: not I-cache line
   placement, and 64 B alignment works by quantising code motion, not by
   fixing cache placement.
5. **Nothing in the compiler.** No default should change on the strength of a
   term that a six-NOP relink reproduces.

## 9. Reproduce

```
# the decomposition (60 cells, ~10 min at 30-way)
CIO=~/Documents/cio-2026-09-04 LLVM=<build> G5=<gem5-DIT> \
WORK=<work> LIB_VARIANTS="rt notwin notwinnop" ARMS="rt notwin notwinnop" \
  utils/dit_host_screening/cioparity/build_arms.sh lib link
G5=<gem5-DIT> WORK=<work> utils/dit_host_screening/cioparity/run_cio_gem5.py \
  --arms base,rt,notwin,notwinnop,taint,taintnop --configs spec,serdit \
  --jobs 30 --out <work>/out

# the null control (60 cells)
LLVM=<build> G5=<gem5-DIT> WORK=<work> OUT=<pads> \
  utils/dit_host_screening/cioparity/relink_null.sh
G5=<gem5-DIT> WORK=<pads> utils/dit_host_screening/cioparity/run_cio_gem5.py \
  --arms pad0,pad4,pad8,pad12,pad16,pad24,pad32,pad48,pad64,pad96,pad128,pad256 \
  --configs spec --jobs 24 --out <pads>/out

# the same on the hardened library, and at 64 B alignment
LIB=<work>/taintnop OUT=<padtaint> ... relink_null.sh
ALIGN=6 LIB=<align-built library> OUT=<padalign> ... relink_null.sh
```

`relink_null.sh` assembles each pad to a fixed object name so the binaries are
byte-reproducible; its `.text` is byte-identical to the binaries measured here.

## 10. Threats

- **One library, one core model.** libsodium's hot code fits in L1I, which is
  what rules the caches out *here*. A workload with a real instruction working
  set could have a layout term that is an I-cache effect, and the counters
  would say so - `relink_null.sh` plus the miss columns is the test.
- **`HINT #0` is not a neutral filler.** CLAUDE.md prices it at ~0.25% faster
  than a real op at the same address, so terms B and C slightly understate the
  cost of an inserted instruction. It is a quarter of a point against swings of
  five to seven, and it cannot change any conclusion here.
- **The ensembles are 12 draws, not a distribution.** Medians over 12 relink
  offsets are a better estimator than one draw, not a confidence interval.
  Nothing in §3 should be read as a p-value.
- **The ROIs are small.** aes-gcm is ~1,200 cycles per operation. The per-ROI
  deltas are exactly constant across all 50 measured operations (1,212 against
  1,140 on every one of them), so these are steady-state differences and not
  boundary transients, but a longer kernel would dilute anything that is a
  fixed per-call cost.
