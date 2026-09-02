# Serialised vs renamed `MSR DIT`, on experiment 09's benchmarks

**Status: complete, gem5. All gates pass.** Measured 2026-09-02 on a Neoverse-N1
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
| chacha20-poly1305 encrypt | **-0.90%** | **+80.72%** |
| chacha20-poly1305 decrypt | **+3.66%** | **+91.07%** |
| aes256-gcm encrypt | **-0.75%** | **+28.94%** |
| aes256-gcm decrypt | **+8.46%** | **+51.38%** |
| ed25519 sign | **-1.59%** | **+1.33%** |
| argon2id | **+1.35%** | **+1.42%** |

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
| chacha20-poly1305 decrypt | +0.2 | **20.4** |
| chacha20-poly1305 encrypt | -0.3 | **19.0** |
| ed25519 sign | -0.2 | **26.7** |
| aes256-gcm encrypt | +0.3 | **24.4** |
| aes256-gcm decrypt | +5.8 | **36.9** |

Independent reference points, none from this run: 34.3 and 76.3 cyc/write
(experiment 06, gem5, mbedTLS at two toggle rates), ~41 cyc/switch (experiment
09, Apple M5 **silicon**, this library), ~24 cyc/switch (experiment 02, M5).
Ours sits inside that 19-76 spread. Ordering and proportionality transfer;
magnitudes are workload-dependent.

## Toggle rate is the explanatory variable, over a 30,000x span

Experiment 06 established that the penalty tracks committed toggle rate almost
linearly, from +0.66 points at 86 writes/Mcycle to +15.80 at 4,601. This study
extends that axis at **both** ends:

| benchmark | writes / Mcycle (baseline) | serialising penalty |
|---|---|---|
| chacha20-poly1305 decrypt | 43,176 | +87.42 |
| chacha20-poly1305 encrypt | 42,217 | +81.62 |
| aes256-gcm decrypt | 13,787 | +42.92 |
| ed25519 sign | 1,089 | +2.92 |
| **argon2id** | **1.3** | **+0.07** |

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

Measured spread of that effect alone: **-6.82% to +7.89%, a 14.7-point range**,
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
16B, 64B), whose layout terms differ by up to 14.7 points, the layout-free
ranking is **identical on 4 of 5 benchmarks**:

| benchmark | taint | taintfn | fine | winner (all 3 alignments) |
|---|---|---|---|---|
| ed25519 | +2.91 / +2.78 / +1.44 | +3.10 / +2.98 / +2.63 | +5.78 / +7.38 / +5.57 | taint |
| chacha20 enc | +80.34 / +81.04 / +78.02 | +86.63 / +85.81 / +82.72 | +102.54 / +104.19 / +98.39 | taint |
| chacha20 dec | +88.13 / +81.11 / +77.56 | +89.12 / +85.01 / +81.88 | +106.50 / +103.27 / +98.53 | taint |
| aes-gcm dec | +50.86 / +50.53 / +50.75 | +55.04 / +54.57 / +55.02 | +54.52 / +52.76 / +53.51 | taint |
| aes-gcm enc | +30.00 / +29.90 / +30.71 | +30.72 / +31.25 / **+24.47** | +48.22 / +49.42 / +48.58 | taint, taint, **taintfn** |

**Without the twins, the aes-gcm decrypt cell gave the opposite answer**: raw
totals read `fine` +47.70% against `taint` +51.38%, because `fine` happened to
draw a -6.82% layout term. Layout-free, `fine`'s DIT cost is +54.52% against
`taint`'s +50.86%. `fine` executes MORE switches (18 vs 15) at the SAME dwell
(94% vs 96%) - it is strictly worse placement - and still measured 6.7% faster.

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
| layout spread (max-min) | 14.71 pts | 9.45 | **2.87** |
| worst \|layout\| | 7.89% | 5.86% | **2.24%** |
| mean \|layout\| | 2.05% | 1.66% | **0.74%** |
| **code size** | - | +0.79% | **+4.16%** |
| **runtime, base arm** | - | - | **-0.05% to +6.99%** |

That 16B does little while 64B works confirms the mechanism is **I-cache line
placement**, not instruction-bundle alignment: 16B does not control which cache
line a block starts in, so it merely reshuffles the lottery (it moved the worst
outlier from aes-dec/`fine` to aes-enc/`taintfn`).

**Recommendation: 64B alignment ON as an evaluation flag, OFF as a shipping
default.** It buys measurement stability by making every build up to 7% slower
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

argon2id is the exception that proves the rule: at 1.3 writes/Mcycle every
policy is within 3 points of every other, because there is nothing to rank.

## Dwell, measured directly

`commit.ditCycles` (added for this study) counts cycles with the mode set:

| benchmark | taint | taintfn | fine | blanket |
|---|---|---|---|---|
| ed25519 | 99% | 99% | 99% | 100% |
| chacha20 enc/dec | 90% | 90% | 87-88% | 100% |
| aes-gcm enc/dec | 96% | 95% | 91-94% | 100% |

**Every policy is close to blanket on these workloads.** Region placement
narrows very little here, which is consistent with experiment 09's `f_secret ~
100%` framing - and it is why `fine`'s lower static collateral (2,466 vs 3,084)
buys no dynamic advantage. On aes-gcm decrypt `taint` and `fine` dwell within
0.7 points of each other, which is what falsified the "narrower regions dwell
less" explanation.

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
8. **Tail-call audit**: no function carrying `msr DIT` may tail-call out. 159
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

- **argon2id is one operation per cell** (326M cycles), and `commit.ditCycles`
  was not available in the simulator used for that sweep, so its dwell column is
  absent.
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
