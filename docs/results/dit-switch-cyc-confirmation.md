# The switch-cyc default, confirmed - and what it actually prices

**Measured 2026-08-24**, native Apple M5, machine exclusive, 20 paired reps per
point, both public lanes of the libsodium composite. Pre-flight passed both
gates (DIT off const/perm = 0.2575, DIT on = 0.9984, so the rig demonstrably
gates the predictor). Checksums identical across all nine arms at every point.
Data `~/Documents/dit-crossover/out/native/confirm_{lua,sqlite}_deploy.jsonl`,
rig `utils/dit_host_screening/xover/` on `dit-tainter` (`5ad90a087fd5`).

> ⚠️ **RESULT 2 IS RETRACTED (2026-08-30). The NOP control was inert.**
>
> `-taint-dit-nop-switches` is consumed by `AArch64AsmPrinter::emitInstruction`,
> i.e. at EMISSION. `build_sodium_arms.sh` is two-stage and passed the flag only
> to the first `llc`, the one that runs the taint pass and writes MIR; the second,
> which emits the object, never received it. The flag was silently ignored and
> **every NOP arm was byte-identical to its non-NOP twin** - `def30.o` and
> `nop30.o` share a sha256, and `nop30.o` contains zero substituted instructions.
> That was true in every revision of the script, including `5ad90a087fd5`, the one
> this run used.
>
> So the def-minus-NOP term below is two timings of *the same binary*. It measured
> machine noise. Every conclusion drawn from it is unsupported - not disproven,
> unsupported. **Results 1, 3 and section 4 do not use the NOP arms and stand
> unaffected.**
>
> Fixed in `xover: fix the NOP arms, which were never NOPed`, which also adds a
> gate that fails the build unless each NOP arm carries zero `msr DIT` *and*
> matches its twin's object size. Re-running Result 2 needs only rebuilt arms.
>
> **Re-run under gem5 in section 8 (2026-08-31): both of Result 2's conclusions
> are contradicted on the merits, not just unsupported.**

## Why this run existed

Every previous `swcyc30` measurement was taken with the call-site mod-set gate
OFF, and every gated measurement was taken at `switch-cyc=0`. The shipped
default (`47d34937f5df`) is both at once, so **it had never been built as a
single arm** - it was an inference. The only knob varied here is
`-taint-dit-switch-cyc`, at 0 and 30, on top of the shipped defaults.

Arms: `off`/`always`/`oracle`/`batch` are runtime modes of one `nodit` binary
(argv-selected, so no codegen differs between them); `def30` is the shipped
default (397 switches, 81 instrumented functions); `def0` is the same build with
`-taint-dit-switch-cyc=0` (492 switches); `nop30`/`nop0` are their alignment
controls with identical switch counts; `off2` is `off` re-run last, as a drift
check.

## Result 1: the default is confirmed, and the 512 B verdict flips

Paired against blanket DIT (median, and how many of 20 reps were slower):

| msg | R (us) | lua, f≈12-25% | sqlite, f≈2-5% |
|---|---|---|---|
| 200 B | 0.41 | **+15.65% (20/20)** | -5.00% (0/20) |
| 330 B | 0.56 | **+5.01% (20/20)** | -7.13% (0/20) |
| **512 B** | 0.72 | **-1.44% (7/20)** | -8.55% (0/20) |
| 1400 B | 1.78 | -5.02% (0/20) | -9.29% (0/20) |
| 4 KiB | 4.90 | -7.11% (3/20) | -9.44% (0/20) |
| 64 KiB | 78.5 | -10.33% (0/20) | -10.20% (0/20) |

At 512 B `def0` is **+2.33%, slower in 20 of 20 reps** - an unambiguous loss -
and `def30` is **-1.44%, slower in 7 of 20** - a win. The knob converts a
unanimous loss into a win at the most decision-relevant point.

`def30` vs `def0` directly: **-8.96 / -6.08 / -4.01 / -1.99 points at 200 B
through 1400 B, slower in 0 of 20 reps at every one**; inert at 4 KiB (6/20) and
64 KiB (7/20), where regions are large enough that the admission test has little
left to merge. The 27-31% band from the earlier gate-off measurement reproduces.

## Result 2: the parameter does not price what its name says - RETRACTED

> **Retracted 2026-08-30, see the notice at the top.** Kept in full rather than
> deleted: the reasoning is sound *given* a working control, and the shape of the
> error is worth preserving. An inert control does not fail loudly - it produces
> exactly the reading a working one produces when layout is the whole story,
> because the two binaries are the same binary.


**The NOP control tracks the real build everywhere.** With
`-taint-dit-nop-switches` every inserted `MSR DIT` is emitted as `HINT #0` at an
identical address, so the instruction stream is unchanged and **no DIT executes
at all**. Across 24 paired measurements the def-minus-NOP term runs **-0.33 to
+0.97 points**, straddling zero, against machine drift (`off2` - `off`) reaching
1.15. It is frequently NEGATIVE - the real build beating its own NOP twin -
which is the known ~0.25% `HINT #0` penalty showing through, and which means
this term **understates** true DIT cost rather than flattering it.

The NOP arms also reproduce the switch-cyc win almost exactly:

| msg | `def0`->`def30` | `nop0`->`nop30` | layout share |
|---|---|---|---|
| 200 B | -12.73 | -12.68 | 99.6% |
| 512 B | -4.65 | -4.65 | 100% |
| 1400 B | -2.36 | -1.98 | 84% |

~~**So what `switch-cyc=30` buys is fewer INSTRUCTIONS INSERTED INTO HOT LOOPS,
not cheaper mode switching**, and PSTATE.DIT's own execution cost is unresolvable
at every region size measured - bounded around ±0.5 points while the pass itself
costs up to +28%.~~

**UNSUPPORTED.** The table above compares a binary pair against itself, so the
99.6% / 100% / 84% "layout share" is 100% by construction and the deviations are
run-to-run drift. With a WORKING NOP control the term is large and cleanly
resolvable, at least on a different instrument: on the SQLite TCE composite under
gem5, `nop30` sits within ±0.9% of the baseline at all twelve points while `def30`
climbs monotonically to +23.4% under a serializing switch - layout costs nothing
and the switches cost everything, the opposite reading. That is gem5 and a
different workload, so it does not refute the M5 measurement; it shows the term is
resolvable once the control works. See `docs/design/dit-tailcall-gap.md` §7.

This corroborates, on a different library and a different instrument, the
SQLCipher finding that most of what looks like switch cost is codegen
(`CLAUDE.md`, 2026-08-24): with all 121 HMAC/SHA switches NOPed, that build still
cost the majority of its total under a renamed switch.

### The trap this creates - also unsupported

> Both claims below rest on the retracted def-minus-NOP term. The *advice* may
> still be right, but this run is not evidence for it.


**Do not lower `switch-cyc` for hardware with a renamed, non-serializing `MSR
DIT`.** Renaming makes the mode switch cheaper; the mode switch is not the term
being paid. The 22.6 cyc serializing / 9.7 cyc renamed figures are real gem5
measurements of the switch alone and they do not govern this default.

It also weakens a forward-looking claim we have made elsewhere: "renaming the
switch rescues placements that over-toggle" was established by varying the switch
model under gem5. On this workload the DIT-specific term is ~0.3-0.5 points
against a 28-point total, so renamed-switch silicon would recover the 0.3, not
the 28. Report that limit rather than dropping it.

## Result 3: the denominator, inside one sweep

At **the same message size (200 B)**, `def30` LOSES to blanket by 15.65% on the
lua lane (f = 16.7%) and WINS by 5.00% on the sqlite lane (f = 3.2%). Same
binary, same region size; only the secret's share of runtime differs. The
secret-fraction relationship has been measured on both sides before across
separate experiments - this is it reproducing as a controlled contrast within a
single run.

## What this does NOT establish

- **Only two lanes of one library.** The attribution (layout, not DIT) is
  measured on libsodium at f = 2-25%. Bitcoin Core, where the gate is worth 50
  points and the toggle count is thin, is not covered here.
- ~~The DIT-only term is an **upper bound**, not a measurement: it never clears
  the drift floor cleanly, and the NOP baseline biases it downward.~~ There was no
  DIT-only term: the NOP baseline was the same binary. See the notice at the top.
- **Only two lanes of one library**, as above. The staircase in §4 is a property
  of libsodium's corridor-length distribution, not a universal fact.

## 4. Calibration: 30 is the saturation point, not a guess

**Run 2026-08-24**, same rig and discipline, lua lane, 20 paired reps.
Data `~/Documents/dit-crossover/out/native/calib_lua_deploy.jsonl`.

The worry this sweep was meant to settle: measured switch cost is 9.7-22.6 cyc
against a dwell of 0.0039 cyc per suppressed op, so the physical ratio is orders
of magnitude above the 30:1 that `switch-cyc=30 / dwell-per-instr=1` encodes -
and since the win turned out to be instruction count rather than switch cost, the
right value looked like it might be far higher.

**It is not, and the compiler settles it without timing.** `-taint-dit-switch-cyc`
is a THREE-STEP STAIRCASE on this library:

| switch-cyc | 0 | 30 | 100 | 300 | 1000 | 3000 | 10000 | 100000 |
|---|---|---|---|---|---|---|---|---|
| switches | 492 | 397 | 397 | 395 | 395 | 395 | 395 | 395 |
| object | A | **B** | **B** | C | C | C | C | C |

`def30.o` and `def100.o` are **byte-identical** (sha256
`dc49cfa7e8cb...`), and every value from 300 through **100,000** produces one
identical object (`9ed7cb4a4427...`). At a ratio of 100,000:1 the compiler emits
the same code as at 300:1: everything the admission test can merge is already
merged at 30.

Timing the only pair that differs (2 switches out of 397) is a null result:

| msg | `def300` vs `def30` | `def30` vs `def0` |
|---|---|---|
| 200 B | +0.40% (12/20) | **-8.41% (0/20)** |
| 330 B | +0.36% (11/20) | **-5.52% (0/20)** |
| 512 B | +0.49% (12/20) | **-3.85% (0/20)** |
| 1400 B | +0.66% (14/20) | -1.82% (1/20) |
| 4 KiB | +0.32% (12/20) | -1.45% (3/20) |
| 64 KiB | -0.31% (7/20) | -0.39% (9/20) |

Pooled across all six points, `def300` vs `def30` is **+0.32%, slower in 68 of 120
reps** - 57%, about 1.5 sigma from chance, so NOT significant. Five of six points
carry a positive sign, which hints 300 may be marginally worse (it merges two more
corridors, buying dwell for almost no switch saving), but that is not a claim this
data supports. **Keep 30.** The `def30`-vs-`def0` column reproduces the previous
run (-8.96/-6.08/-4.01/-1.99) to within 0.6 points on fresh builds in a fresh
session, which is the better use of this table.

**What it hands to the next piece of work.** The 395 survivors are structurally
out of reach of corridor merging - no finite switch cost touches them - so they
are not interior off-corridors between two on-regions. They are entry enables,
exit clears, and post-call re-asserts. Further switch reduction needs the
callee-ownership mechanism (cloning, or Mode 2's runtime `mrs DIT`), not a bigger
constant. Given §2 showed the cost is instructions in hot loops, and these are
what remain, that is now the highest-value target.

---

## 8. Result 2 re-run under gem5 (2026-08-31): both claims contradicted

**120 points**, gem5 NeoverseV2, both libsodium composite lanes, five message
sizes, both switch models, all gates passing. Rig
`utils/dit_host_screening/xover/run_sodium_gem5.py` +
`scripts/build_sodium_hosts_gem5.sh`.

WHAT THIS IS AND IS NOT. Same WORKLOAD as Result 2 -- both lanes of the libsodium
composite -- on a different INSTRUMENT, with arms that are actually NOPed. It
cannot restore or replace Result 2: silicon is for magnitude, gem5 for ordering,
and gem5 charges ~21 cyc of rename stall per serializing switch by model. The
M5 re-run still needs doing; the arms exist and are gated for it.

### 8.1 The def-minus-NOP term does not straddle zero

Against the `nodit` baseline (empty seed, same pipeline):

| lane | model | 200 B | 512 B | 1400 B | 4 KiB | 64 KiB |
|---|---|---|---|---|---|---|
| lua | serializing | **+9.86p** | **+8.14p** | **+5.19p** | +1.28p | +0.42p |
| lua | renamed | +3.30p | +3.07p | +1.98p | +0.84p | -0.06p |
| sqlite | serializing | +2.66p | +1.64p | +1.87p | +1.29p | +0.89p |
| sqlite | renamed | +1.00p | +0.38p | +0.23p | +0.32p | +0.01p |

Positive at 19 of 20 points, up to **+9.86**, against Result 2's "-0.33 to +0.97,
straddling zero". And it SCALES WITH THE SWITCH MODEL -- serializing is 2-3x
renamed at the small sizes. The two builds are identical apart from how `MSR DIT`
executes, so a term that tracks the model is the mode switches being paid for.

`nop30` sits within +-1.01% of baseline at all 20 points and is NEGATIVE at every
Lua point. Layout is not merely cheap on these workloads; it is at noise.

### 8.2 The layout share is ~0%, not ~100%

Result 2's own table form, on the Lua lane where `def0->def30` is large enough for
the ratio to mean anything:

| msg | `def0->def30` | `nop0->nop30` | layout share | Result 2 reported |
|---|---|---|---|---|
| 200 B | **-4.30p** | -0.06p | **1%** | 99.6% |
| 512 B | **-2.74p** | +0.13p | **-5%** | 100% |
| 1400 B | **-1.96p** | +0.15p | **-8%** | 84% |

So what `switch-cyc=30` buys is cheaper mode switching, not fewer instructions in
hot loops. The conclusion in Result 2 is inverted.

**Do not read the same ratio off the sqlite lane or the two largest Lua sizes.**
There `def0->def30` is +-0.5p or less, and dividing by it produces figures from
-355% to 4667%. Noise over noise. The three rows above are the only ones with a
denominator worth dividing by, which is also why the single-lane presentation is
the honest one.

### 8.3 What the trap section got backwards

Section "The trap this creates" argued that renaming the switch would recover
"the 0.3, not the 28", because the mode switch was not the term being paid. On the
Lua lane the switch model does not shave tenths off the verdict -- it REVERSES it:

| lane | model | 200 B | 512 B | 1400 B | 4 KiB | 64 KiB |
|---|---|---|---|---|---|---|
| lua | serializing | **+5.89%** | **+4.23%** | **+2.39%** | -2.16% | -3.34% |
| lua | renamed | -0.72% | -0.83% | -0.92% | -3.25% | -3.57% |
| sqlite | serializing | -0.59% | -0.68% | -0.28% | -1.30% | -1.83% |
| sqlite | renamed | -1.54% | -1.79% | -1.47% | -1.86% | -1.75% |

(`def30` vs `always`; negative = selective beats blanket.)

At Lua 200 B, serializing loses by 5.89 points and renamed wins by 0.72 -- same
binaries, same input, only the switch model differs. **On that lane at small
message sizes the renamed switch is what makes selective placement viable at
all.** SQLite wins in all ten cells, so this is a property of the lane, not of the
pass.

This also reproduces RESULT 1's structure, which was never retracted: selective
beats blanket where f is small and loses where f is large.

### 8.4 Caveats, and one that cuts the other way

- **gem5, not silicon.** Shows the term is resolvable and which way it points, not
  the M5 magnitude.
- **Arms built by the pre-driver script.** These were built through the two-stage
  `llc` path, which is correct for reproducing Result 2 as originally measured but
  is no longer how the pass ships (`xover: build the libsodium arms through the
  clang driver`, same day).
- **The Lua lane sits at f = 32-67%**, above the >20% band where
  `paper/evaluation-framework.md` says the prize collapses; the original ran at
  f = 2-25%. So this is not a like-for-like reproduction of the original's
  operating point. But the collapse it predicts is exactly what the serializing
  rows show, so the caveat is about comparability, not about the mechanism.
- **The sqlite lane's `def0->def30` was too small to test 8.2**, which is a sizing
  choice of this run, not a finding.

### 8.5 Net

Both of Result 2's claims are contradicted: the DIT-specific term is large and
model-dependent rather than unresolvable, and the layout share is ~0% rather than
~100%. The retraction stands on the inert control alone; this says the conclusion
was also wrong on the merits, on a different instrument. Results 1, 3 and section
4 remain unaffected throughout.
