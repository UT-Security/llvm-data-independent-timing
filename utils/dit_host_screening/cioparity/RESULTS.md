# Serialised vs renamed `MSR DIT`, on experiment 09's benchmarks

**Status: complete, gem5. All gates pass. Bit-reproducible across machines and work dirs.** Measured 2026-09-02 on a Neoverse-N1
host (aarch64 Linux, 160 cores, **no FEAT_DIT** - which is why gem5 is the only
instrument available here). libsodium 1.0.21 `--disable-asm`, CIO's own drivers
byte-for-byte, CIO's own seeds. Simulator: gem5-DIT `2acf7637bc` plus two
patches made for this study (PMULL64, `ditCycles`). `data/provenance.txt`.

**All six of CIO's benchmarks now run**, including the two AES-GCM rows
experiment 09 had to disclaim and the argon2id row where silicon could only
report noise.

## The question

Experiment 09 concludes that the pass's entire cost on libsodium is **switch
serialisation with no dwell term**: blanket `PSTATE.DIT` is free (-0.60% to
+1.95%), the pass costs +12% to +124%, and cycles-per-switch comes out
consistent at 41.2 / 40.3 / 44.8 across three independent benchmarks.

That last column is measured cycles divided by measured switches. Its own README
says so: *"multiplying it back out reproduces the measurement by construction and
proves nothing."* An Apple M5 has exactly one `MSR DIT` implementation and it
serialises, so on silicon the mechanism cannot be switched off and the
decomposition stays an inference.

gem5 can switch it off. `--no-speculative-dit` selects the serialising path;
without it the write is a renamed CC-register write. Same binary, same input,
one mechanism changed.

## Headline

**A renamed switch costs approximately nothing; a serialising one costs ~20-37
cycles.** Same hardened binary, same input, differing only in how the core
implements the mode write:

| benchmark | renamed | serialising |
|---|---|---|
| chacha20-poly1305 encrypt | **-0.28%** | **+81.85%** |
| chacha20-poly1305 decrypt | **+2.57%** | **+89.06%** |
| aes256-gcm encrypt | **-0.76%** | **+29.01%** |
| aes256-gcm decrypt | **+8.23%** | **+51.06%** |
| ed25519 sign | **-1.39%** | **+1.70%** |
| argon2id | **+2.04%** | **+1.97%** |

Experiment 09's inference is confirmed causally rather than by a
self-reproducing ratio. **This comparison is immune to the layout confound
described below**: it is the same binary under two machine configurations, so
code layout cancels identically. It is the only comparison in this study with
that property, which is a good reason it carries the result.

## Cycles per serialising switch

A difference between two binaries that differ **only** in whether the switch
executes - identical placement, identical instruction count, identical
addresses.

| benchmark | renamed | serialising |
|---|---|---|
| chacha20-poly1305 decrypt | +0.1 | **20.3** |
| chacha20-poly1305 encrypt | -0.3 | **19.0** |
| aes256-gcm encrypt | +0.3 | **24.5** |
| aes256-gcm decrypt | +6.0 | **37.1** |
| ed25519 sign | +10.7 (+/-9) | 39.0 (+/-9) |

ed25519's per-switch figures carry **+/-9 cycles and should not be quoted**: 85
switches against a 78,000-cycle region means 1% of layout jitter is 780 cycles,
9 per switch. Pinning `__FILE__` alone (see Reproducibility) moved its renamed
figure from -0.2 to +10.7 while chacha and aes-encrypt moved by 0.1 or less.
The claim rests on the rows that can resolve it. aes-gcm decrypt's renamed +6.0
is its dwell term showing through (blanket alone is +8.41% there).

### Against silicon, in the quantity that survives an instrument change

**Compare cycles per switch, not percentages.** A per-switch figure is a
DIFFERENCE divided by a count, so a fixed per-region instrument cost cancels
exactly; a percentage puts that cost in the denominator and compresses the
ratio. This is not a hypothetical distinction: experiment 09's own M5 numbers
were understated **4x on chacha and 13-15x on AES** by 2,521-3,234 cycles of
`kpc_get_thread_counters()` sitting inside the timed region, and the per-switch
column was the part unaffected.

| instrument | cycles per serialising switch |
|---|---|
| Apple M5, this library (corrected) | **43.6** |
| Apple M4, this library (corrected) | **33.4** |
| **gem5 Neoverse-V2, this run** | **19.0 - 37.1** |
| experiment 06, gem5, mbedTLS at two toggle rates | 34.3 and 76.3 |
| experiment 02, Apple M5 | ~24 |

**This corrects a claim in an earlier version of this file.** It read "our
19-37 sits inside a 19-76 spread" against a stale ~41 for M5. With both hosts
re-measured, gem5's serialising switch is cheaper than *both* pieces of silicon,
which strengthens rather than weakens the point below: gem5's serialising model
is the **optimistic** end, not the conservative one experiment 06 records it as.

The pass-arm percentages are shown for completeness and should not be compared
across instruments without the per-switch column beside them:

| benchmark | M5 | M4 | gem5 | M5 base | gem5 base |
|---|---|---|---|---|---|
| chacha20-poly1305 encrypt | 4.83x | 3.43x | **1.82x** | 1,119 | 2,212 |
| chacha20-poly1305 decrypt | 4.90x | 3.62x | **1.89x** | 1,180 | 2,291 |
| aes256-gcm encrypt | 5.27x | 3.34x | **1.29x** | 252 | 1,219 |
| aes256-gcm decrypt | 4.14x | 2.95x | **1.51x** | 345 | 1,090 |
| ed25519 sign | 1.13x | 1.09x | **1.02x** | 36,690 | 78,459 |
| argon2id | 1.000x | 0.996x | **1.014x** | 197.9M | 326.6M |

gem5 reads far lower, and the gap decomposes exactly: its baseline is ~2x
slower (which dilutes any ratio) and its switch is ~2.3x cheaper. 2 x 2.3 = 4.6,
against the 4.7x observed on chacha encrypt. Ordering and proportionality
transfer; absolute percentages do not.

## Toggle rate is the explanatory variable, over a 30,000x span

Experiment 06 established that the penalty tracks committed toggle rate almost
linearly, from +0.66 points at 86 writes/Mcycle to +15.80 at 4,601. This study
extends that axis at **both** ends:

| benchmark | writes / Mcycle (baseline) | serialising penalty |
|---|---|---|
| chacha20-poly1305 decrypt | 42,728 | +86.48 |
| chacha20-poly1305 encrypt | 42,494 | +82.13 |
| aes256-gcm decrypt | 13,758 | +42.83 |
| ed25519 sign | 1,093 | +3.09 |
| **argon2id** | **1.35** | **-0.07** |

argon2id is the null endpoint and the cleanest confirmation: 438 executed
switches amortised over 326 million cycles cost nothing measurable. It also
**resolves a number silicon could not**: experiment 09's
`results_summary.csv` records `pass_switches_per_op = -197187` for this row -
noise divided by noise, which its README honestly renders as "~0 (noise)" and
"n/a". gem5 counts 438 exactly.

## THE LAYOUT CONFOUND, and what is and is not fixed

This section supersedes an earlier version of this file that reported "finer
placement wins on aes256-gcm decrypt" and called it the "coarser always wins"
result inverting. **That was wrong**, and so were two successive mechanisms
offered for it (the DIT-gated prefetcher, then the value predictor staying live
over public code).

### The confound

Inserting a `msr DIT` does two independent things: the switch **executes**
(serialisation, dwell - DIT's real cost), and the code **moves** (4.00 bytes per
switch, measured exactly, in every policy). After 524 switches that is 2,176
bytes = 34 cache lines of displacement for everything downstream. Different
cache-line placement, different BTB indices, different fetch-group boundaries -
none of it anything to do with DIT. You would get the same from inserting 524 of
any 4-byte instruction.

Measured spread of that effect alone: **-6.94% to +4.52%, an 11.5-point range**,
against policy differences of 1-8 points. It is large enough to invert a
ranking, and it did.

### What IS fixed: the measurement, via per-policy layout twins

Each placement policy now ships a `<policy>nop` arm emitted from **its own**
`.mir` with `-taint-dit-nop-switches` on the object stage: identical placement,
identical instruction count, **byte-identical addresses**, `HINT #0` where each
`msr DIT` was. `(policy - policyNOP)` therefore cancels layout arithmetically -
no estimation, no cost.

This is exact, and the evidence is that it survives regimes where raw layout
differs wildly. Re-running the whole sweep at three alignment settings (none,
16B, 64B), whose layout terms differ by up to 11.5 points, the layout-free
ranking is **identical on every row whose gap exceeds the resolution limit**:

| benchmark | taint | taintfn | fine | winner (all 3 alignments) |
|---|---|---|---|---|
| ed25519 | +4.26 | +2.50 | +4.42 | taintfn, by 1.8 - inside the resolution limit |
| chacha20 enc | +80.85 | +86.29 | +102.58 | taint |
| chacha20 dec | +86.90 | +87.72 | +108.94 | taint |
| aes-gcm dec | +51.09 | +54.66 | +54.33 | taint |
| aes-gcm enc | +30.08 | +30.79 | +48.28 | taint |

Canonical (bit-reproducible) sweep. Re-run at 16B and 64B block alignment
through the same fixed rig, the ranking is identical on the three rows whose
gaps exceed the resolution limit (both chacha20 rows and aes-gcm decrypt:
`taint` at every setting) and flips between `taint` and `taintfn` on the two
rows whose gap is 1-2 points (ed25519, aes-gcm encrypt) - which is what a
~10-point resolution limit predicts.

**Without the twins, the aes-gcm decrypt cell gave the opposite answer**: raw
totals read `fine` +47.39% against `taint` +51.06%, because `fine` happened to
draw a -6.94% layout term. Layout-free, `fine`'s DIT cost is +54.33% against
`taint`'s +51.09%. `fine` executes MORE switches (18 vs 15) at the SAME dwell
(94% vs 96%) - it is strictly worse placement - and still measured 6.7% faster.

### The same confound was found independently, on silicon

While this study was running, experiment 09 added **arm Z** on both Apple hosts:
the same idea, a NOP-switch layout control. It reads within +/-0.7% of 1.0 on
five rows and **1.0769x on M5 / 1.0870x on M4 for aes256-gcm encrypt** - the same
benchmark where the layout term is largest here. Three machines, two instruments,
same effect on the same row, found in parallel and independently.

That matters more than either result alone. A 7-8% layout term on silicon is not
a simulator artifact and not a gem5 modelling choice; it is what instrumenting a
252-tick region costs through code movement, and an A-vs-P comparison books it as
DIT cost. It is the strongest available argument that per-policy layout twins
belong in the rig permanently rather than as a one-off control.

**RESOLUTION LIMIT: about 10 points, not sub-point.** The aes-gcm encrypt row
swings 6 points across alignment settings and flips its winner, because the twin
cancels layout between a policy and its twin but the percentage denominator
(`base`) still moves. Differences of 20-50 points (`fine` vs `taint` everywhere)
are resolvable; differences of 1 point (taint 30.00 vs taintfn 30.72) are not,
and were never resolvable in this rig.

### What is NOT fixed: the real cost in a shipped binary

A hardened binary genuinely is slower partly because its code moved, and users
pay that. It is **not** fixable by better placement, and the pass should not try:
the cost is a function of final addresses, which depend on `block-placement`,
`machine-outliner`, `branch-relaxation` and then the linker - none of which the
placement pass can see. A layout term in the merge cost model would be a number
the pass cannot compute, fitted to one binary.

Cache-line alignment normalises it but does not remove it, and charges for the
privilege:

| metric | no alignment | 16B | **64B** |
|---|---|---|---|
| layout spread (max-min) | 11.46 pts | 7.93 | **2.93** |
| worst \|layout\| | 6.94% | 5.70% | **1.98%** |
| mean \|layout\| | 1.97% | 1.84% | **0.75%** |
| **code size** | - | +0.79% | **+4.16%** |
| **runtime, base arm** | - | - | **+0.30% to +4.83%** |

That 16B does little while 64B works confirms the mechanism is **I-cache line
placement**, not instruction-bundle alignment: 16B does not control which cache
line a block starts in, so it merely reshuffles the lottery (the worst
cell only moves from one benchmark to another).

**Recommendation: 64B alignment ON as an evaluation flag, OFF as a shipping
default.** It buys measurement stability by making every build up to 5% slower
and 4% bigger, and it does not reduce the layout cost DIT actually incurs - it
makes it uniform, which benefits the experimenter, not the binary. The blunt
`-align-all-nofallthru-blocks` was used here; a targeted version aligning only
hot loop heads would likely get most of the benefit for a fraction of the size
cost, and is now worth building since the mechanism is confirmed.

## Placement ranking, layout-free

With per-policy twins, at every alignment setting: **`taint` - the shipped
region default - wins on 4 of 5, and is tied within noise on the fifth.**
Experiment 09's "coarser beats finer" holds on gem5 too. `fine`
(`switch-cyc=0`, no loop hoist) is 20-50 points worse everywhere.

argon2id is the exception that proves the rule: at 1.35 writes/Mcycle every
policy is within 3 points of every other, because there is nothing to rank.

## Dwell, measured directly

`commit.ditCycles` (added for this study) counts cycles with the mode set:

| benchmark | taint | taintfn | fine | blanket |
|---|---|---|---|---|
| ed25519 | 99% | 99% | 99% | 100% |
| chacha20 enc/dec | 90% | 90% | 87-88% | 100% |
| aes-gcm enc/dec | 96% | 95% | 91-94% | 100% |
| argon2id | **100%** | **100%** | **100%** | 100% |

**Every policy is close to blanket on these workloads.** Region placement
narrows very little here, which is consistent with experiment 09's `f_secret ~
100%` framing - and it is why `fine`'s lower static collateral (2,466 vs 3,084)
buys no dynamic advantage. On aes-gcm decrypt `taint` and `fine` dwell within
0.7 points of each other, which is what falsified the "narrower regions dwell
less" explanation.

**argon2id at 100% in every hardened arm is the dynamic confirmation of the
memory-leak finding.** The static analysis said taint never reaches the hashing
kernel and the mode stays set across it by way of an over-tainted variant flag;
`ditCycles` now shows the entire 325M-cycle operation running with the mode on,
under region, function and fine placement alike. Every hardened arm is blanket
here, measured rather than inferred.

## Reproducibility: bit-identical, and it had to be earned

`reproduce.sh` regenerates every `gem5_*` file in one command. Its first run
from a differently named work directory did NOT reproduce: 67 of 80 cells moved,
**0.18% median / 2.57% worst**, and one gate failed. The archives built by the
pass were byte-identical, so placement was never in question; two paths were
leaking into the *driver* binary:

- CIO's drivers use `assert()`, whose `__FILE__` was the absolute source path.
  An 8-character-longer work-dir name lengthened `.rodata` and moved every
  address after it. Drivers now compile from a bare relative name inside the
  staging dir, so `__FILE__` is `eval_ed25519.c` everywhere.
- The equal-width `argv[0]` trick fixed the file name but not the directory
  prefix (71 vs 63 characters). `argv[0]` now lives under `/tmp/cio_<hash>/`,
  the same length on every machine.

With both pinned: **45 of 45 driver binaries byte-identical** across two work
dirs, and **two full 80-cell sweeps from two work dirs identical in every cell
on every counter**. The gate failure was the leak, not the simulator: the same
binary is deterministic across repeated runs and inert under the switch model
once its paths are equal.

The canonical data is the fixed sweep. Against the pre-fix sweep it differs by
**0.06% median / 3.32% worst** - the pinned `__FILE__` is itself a layout
change - and no ranking or conclusion moved except ed25519's sub-resolution
policy order. This is the rig's own documented trap, a 0.84% shift from an
`argv[0]` length change, which turned out to have been only half-closed.

## Gates

gem5 is deterministic, so the 15-rep median the silicon run needs is replaced by
exact checks. All enforced in `run_cio_gem5.py`; **all passing** across 240 runs
(80 per alignment setting) plus 12 argon2id cells.

1. **`simInsts` identical across switch models** for one binary and input. A
   self-timing driver fails here - gem5 SE returns *simulated* time.
2. **Every inert arm (`base`, `<policy>nop`) bit-identical across switch
   models.** No DIT executes in them, so the model must not move them.
3. **`<policy>nop` vs `policy` instruction count within 0.5%** - what licenses
   reading the difference as a pure switch term. Measured identical to the digit.
4. **Inert arms commit zero DIT writes and zero `ditCycles`.**
5. **`blanket` dwells >95% of the region** - or it is not blanket.
6. **Pass arms toggle > 0** - or placement inserted nothing that executes.
7. **Mode clear at exit for every arm, set for `blanket`** - on separate
   `-DDIT_READBACK` binaries (see below).
8. **Instrument offset measured, not assumed: 6 cycles.** The M5 rig's timer cost
   2,521-3,234 cycles per region and sat in every denominator. Ours is
   `m5_reset_stats` / `m5_dump_reset_stats`, measured the same way with an EMPTY
   region: **6 cycles, 5 instructions** (`pmull_test/`-style probe, 30 regions,
   exact every time), against a 1,088-2,270 cycle baseline. Correcting it out of
   every denominator would move results by at most **0.28 points**
   (+80.72% -> +80.94%), so it is recorded rather than applied.
9. **Tail-call audit**: no function carrying `msr DIT` may tail-call out. 159
   tail calls in the plain `-O2` control, **exactly 6 survivors** in every
   hardened arm - the same six experiment 09's README lists - all in functions
   with no `msr DIT`. The `-O2` control is what proves the detector works.

### Gates that were themselves wrong

- **`ditSuppressed > 0` was a gate and should not have been.** It failed 16 of 24
  AES-GCM cells as false alarms. `ditSuppressed` counts DIT-gated *optimisations
  suppressed*, so it is evidence the mode is active only where such an
  optimisation was eligible; AES/PMULL offer comp-simp nothing, so it reads 0
  while `fine` commits 900 DIT writes and the mode is provably set. Sufficient,
  not necessary. Now an informational note; the sound witnesses are committed
  DIT writes and the mode readback.
- **The mode readback was itself a confound** (`data/readback_observer_effect.txt`).
  A `mrs DIT` anywhere in a timing binary decodes to `MrsDit64` under the
  speculative model and serialising `Mrs64` under `--no-speculative-dit`, which
  moved ed25519 by 0.35% from an exit-time destructor. Caught by gate 2. The
  readback now lives behind `-DDIT_READBACK` on separate `gate`-stage binaries;
  timings come from readback-free ones. `blanket` necessarily keeps one
  constructor `msr DIT`, which is inherent to that arm and is recorded.

## Simulator patches made for this study

Both on `~/Documents/gem5-DIT-pmull`, neither pushed.

- **PMULL64** (`b328f8e719`, branch `pmull64`). gem5 implemented PMULL only for
  `size == 0`; libsodium's GHASH uses `size == 3` (64x64 -> 128), which fell to
  `Unknown64` and panicked, so AES-GCM could not run at all. No new arithmetic
  was needed - `pmullCode` was already generic over `Element`/`BigElement` and
  `bigger_type_t<uint64_t>` was already `__uint128_t`. Validated against GF(2)[x]
  algebra rather than a reference implementation of itself, and **against this
  host's own hardware `pmull`** (Neoverse-N1): 15/15 native, 15/15 simulated,
  byte-identical, with the unpatched build panicking on the same binary.
  Ungated (no FEAT_PMULL check) and generic `SimdMultOp` latency - must be fixed
  before going near upstream. `data/pmull64_validation.txt`.
- **`ditCycles`** (`2ca3920ec1`, branch `ditcycles`). Cycles with the mode set,
  sampled at commit once per tick. The five existing counters price the mode
  SWITCH; nothing priced the mode being ON, and that inference had already
  produced two wrong mechanisms for one measurement. Only immediate forms move
  the tracked state; `msr DIT, Xt` leaves it unchanged rather than guessing, so
  do not read this on an `-ftaint-dit-abi` build without fixing that.

## Compiler changes made for this study (uncommitted)

Built and tested 2026-09-02 in a dedicated build for this worktree: 3273/3273
targets, zero errors, **42/42 taint regression tests pass** (including
`taint-analysis-dit-precision.mir`, which covers the report extended below).
Both changes are **report-only**: the object emitted from the same MIR is
byte-identical to the one the sweeps measured, 524 `msr DIT` either way.

- **`memory` information-loss category** with a new `TaintLossSeverity::Unsound`.
  The report had `cross-tu` and `indirect` records, both describing
  OVER-approximation ("the callee inherits DIT"). Taint lost through memory is
  the opposite direction - coverage may be ABSENT and nothing says so - so
  reusing `Moderate` would have misdescribed it. 93 records on libsodium,
  including the argon2id path where the password reaches `argon2_ctx` inside a
  stack-allocated `argon2_context` and the entire hashing kernel
  (`argon2_initialize`, `argon2_fill_memory_blocks`, `argon2_fill_segment_ref`)
  carries zero switches and appeared in **no** report. Report-only: verified
  byte-identical output.
- **`needuncovered` counter** in the DIT precision report. `need` and `underdit`
  were tallied independently, so `need <= underdit` could hold while the wrong
  instructions were covered - it was not a soundness number. `needuncovered` is:
  instructions that MUST run with DIT set and do not. **0 for every policy** on
  libsodium (`need = 4,172`), and the 113 residual UNCOVERED sites are
  byte-identical across policies. This is what rules out "the faster policy is
  faster because it skipped a secret."

## Limits

- **argon2id is one operation per cell** (325M cycles, 620.6M instructions). One
  is exact on a deterministic simulator; the re-run through the fixed rig
  reproduces 438 switches to the count and a serialising term of -0.07 points
  against the pre-fix +0.07, both zero within noise.
- **This is the whole-library path, not the shipped per-TU flag.** Hardening per
  TU with `clang -ftaint-harden` yields 134 static switches and **3 committed
  writes per signature**, because taint cannot cross a TU boundary so the mode is
  set once and inherited. At ~38 writes/Mcycle there is nothing to serialise and
  this experiment would return a null it could not distinguish from a real one.
  Experiment 09's *timing* arms used whole-library bitcode; this rig reproduces
  that (524/565/639 switches against the native 521/569/631, 108 functions and
  21/21 seeded entry points - exact placement parity on both published measures).
  Worth recording in its own right: **the shipped per-TU flag is far coarser than
  the evaluated configuration.**
- **gem5 magnitudes are inflated** ~3x against silicon throughout this project;
  ordering and proportionality transfer, absolute percentages do not.
- **"Serialising" is gem5's model, not a shipping core.** Note our serialising
  cost (19-37 cyc) is *below* the M5's ~41, so on this evidence gem5's model is
  the OPTIMISTIC end, not the conservative one as experiment 06 records.
- **One workload family, one gem5 configuration.** No region-size or
  secret-fraction sweep; those are experiments 03 and 01/02.

## Reproducing

```sh
cd utils/dit_host_screening/cioparity
CIO=<counter-optimization/cio checkout> ./build_arms_wl.sh          # 8 arms + audits
CIO=<...> ALIGN=6 WORK=~/Documents/libsodium-cioparity-a64 ./build_arms_wl.sh
G5=~/Documents/gem5-DIT-pmull WORK=~/Documents/libsodium-cioparity-a64 \
  python3 run_cio_gem5.py --jobs 40
python3 analyze.py ~/Documents/libsodium-cioparity-a64/out/results.jsonl
```

`build_arms.sh` builds the per-TU variant instead; it is what experiment 09's
gem5 *oracle* arms used (0/134/137 switches) and is kept for that.

**Traps this rig fell into, recorded because each one produced a plausible wrong
answer:**

1. `-taint-dit-nop-switches` is read by `AArch64AsmPrinter`, i.e. at EMISSION.
   Passed to the analysis stage it does nothing and the NOP arm comes out
   byte-identical to its twin - which is how Result 2 of
   `docs/results/dit-switch-cyc-confirmation.md` was retracted on 2026-08-30.
   Caught only because switch counts are printed per arm. Keep them printed.
2. A single `nop` arm controls for ONE policy's layout. Cross-policy comparison
   needs one twin per policy, or the codegen lottery inverts the ranking.
3. Any `mrs DIT` in a timing binary breaks the switch-model comparison.
4. `ditSuppressed` is not evidence the mode is on.
5. gem5 SE returns simulated time; a self-timing driver is not deterministic.
6. `argv[0]` LENGTH shifts stack alignment - 0.84% from a file name. Every arm
   runs from an equal-width path.
