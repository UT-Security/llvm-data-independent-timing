# QuickJS: fine-grained DIT beats always-on, but by 0.41 points end-to-end

> **CORRECTION 2026-08-10, same day.** This doc first reported a **-6.28%** win.
> That used the **Octane score**, which measures only each benchmark's timed
> `run` section - excluding setup, teardown, GC, *and* the secret work itself
> (`NotifyResult` fires after measurement). On **end-to-end wall time**, which
> hides nothing, always-on costs **+1.05%** and fine-grained recovers **0.41
> points**. The direction survives and is consistent (0/8 reps slower), but the
> magnitude was inflated ~15x by the metric. Both tables are kept below.

**Date:** 2026-08-10. First positive result for the performance claim. Prior to
this, `fastdit-thesis-status` recorded "the performance claim has no supporting
workload" - true on libsodium, SQLCipher, and the SVG filters.

## The numbers - END-TO-END WALL TIME (the metric to quote)

| arm | median | vs base |
|---|---|---|
| `base` | 13.315 s | - |
| `null` | 13.333 s | +0.29% +/- 0.32 |
| **`always`** | 13.462 s | **+1.05% +/- 0.34** (8/8 slower) |
| `fine` | 13.452 s | +0.95% +/- 0.32 |
| **`fine_hoist`** | 13.406 s | **+0.64% +/- 0.24** |

**`always` -> `fine_hoist` = -0.41% +/- 0.22, 0/8 reps slower.** Consistent, CI
excludes zero, but small.

**`fine` (no loop-hoist) -> -0.10% +/- 0.14, 3/8** = indistinguishable from
always-on. The in-loop toggle tax (43 ms) eats the entire gain, so
`-taint-dit-loop-hoist=1` is not a tuning nicety here - without it there is no win
at all.

## The numbers - OCTANE SCORE (benchmark-internal, do NOT quote as end-to-end)

Why it differs by 7x: the Octane score covers only the timed `run` sections,
which are ~14% of process wall time. DIT costs 7.4% *there* (pointer-chasing,
LVP-sensitive) but 1.05% overall, because the untimed allocation/GC-heavy
majority is not LVP-sensitive.

QuickJS 2025-04-26 (`qjs`) running Octane (Richards, DeltaBlue, Splay,
NavierStokes, Crypto) with interleaved secret work. Apple M5. 8 reps x 5
interleaved arms, paired by rep. Octane score is higher-is-better.

| arm | score | secret_ms | vs base |
|---|---|---|---|
| `base` stock | 3597.5 | 5.0 | - |
| `null` stock + no-op dylib | 3571.0 | 5.0 | +1.00% +/- 1.41 (6/8) |
| **`always`** stock + DIT dylib | 3342.0 | 4.5 | **+7.41% +/- 1.03 (8/8)** |
| **`fine`** taint-placed | 3559.0 | 48.0 | **+0.80% +/- 1.64 (5/8)** |
| **`fine_hoist`** taint-placed, `-taint-dit-loop-hoist=1` | 3579.0 | 5.0 | **+0.66% +/- 1.90 (4/8)** |

(Score-based comparison, superseded by wall time above:
`always` -> `fine_hoist` = -6.28% +/- 1.80.)

## Why it works here - the four conditions, all satisfied

Per `dit-finegrain-win-condition`:

- **(a) secret fraction tiny.** 6 `MSR DIT` in the whole binary, all inside
  `secret_mix` and `js_secret_consume`. **Zero in `JS_CallInternal`.**
- **(b) secret code DIT-insensitive.** 5 ms of 13.4 s.
- **(c) few toggles.** 6 static switches; with `loop-hoist=1` they sit outside
  the FNV loop.
- **(d) public code DIT-sensitive.** The interpreter is nearly all serial
  dispatch and pointer chasing: always-on costs it 7.41%.

## The two things that made or broke it

**1. Taint containment is everything.** An arbitrary taint source
(`js_string_repeat,1`, a builtin returning a JSValue) produced **13,222**
`MSR DIT` including **618 inside `JS_CallInternal`** - taint flows back through
the return value into the interpreter and spreads through generic value
handling. The working configuration returns `JS_UNDEFINED` and consumes the
digest into a sink, so nothing secret re-enters the interpreter: **6** switches.
Same program, same pass, 2200x difference in instrumentation. Compile time
tracked it too: 733 s vs 125 s.

**Implication: the win depends on the secret not returning to the caller.** A
realistic password *verify* returns a secret-derived boolean, and with no
declassification mechanism that taint flows straight back into the interpreter.
Declassification is therefore not a nicety - it is what decides whether this
result generalises.

**2. Loop placement.** At the shipped default (`-taint-dit-loop-hoist=0`,
block-minimal) the switches land *inside* the FNV loop: secret work went 5 ms ->
48 ms, a 10x per-iteration toggle tax (~36 us/call vs ~8k cycles of actual work).
`-taint-dit-loop-hoist=1` restores it to 5 ms. On serializing-switch hardware the
default is the wrong choice, exactly as `docs/design/dit-placement.md` warns.

## Honest limits

- **The workload is constructed, not found in the wild.** The secret entry point
  was added to `quickjs.c` for this experiment. It demonstrates that the win
  condition is achievable, not that it occurs naturally.
- **The secret fraction is very small** (5 ms / 13.4 s = 0.04%). This is close to
  "protect almost nothing, pay almost nothing". The interesting result is the
  *curve*: win vs secret fraction, and where it crosses zero. Not yet measured.
- **The metric matters more than anything else here.** A benchmark-internal
  score reported a 15x larger win than end-to-end wall time. Any claim must say
  which metric it is using.
- **n=8**, CIs +/-1-2% on score, +/-0.2-0.3% on wall. The always-on effect (+7.41%) sits far outside that so the
  headline is safe, but `fine` vs `base` is not resolvable at this n.
- **The `null` arm is not perfectly clean** here (+1.00%, 6/8), unlike on Firefox.
  It is within noise but means `fine`'s small positive offset may be injection
  overhead rather than DIT.

## Reproduce

```
cd ~/Documents/quickjs-2025-04-26        # hook added to quickjs.c, .orig kept
/tmp/qjs_bench.sh                        # builds hoist variant + 5-arm sweep
```
Taint source `qjs_secret.txt` = `secret_mix,0,pointee`. Compile is ~125 s
(the pass needed three scalability fixes first - `docs/design/scalability.md`).

---

## Granularity sweep: where fine-grained stops paying (2026-08-11)

The secret-*fraction* sweep was abandoned as near-tautological: both arms run DIT
over the secret work identically, so that term cancels and
`win ~= DIT overhead on the PUBLIC work`, which is a constant. Varying the
fraction only re-expresses that constant over a larger denominator.

**Granularity is the axis that decides anything.** Total secret work held fixed
(8000 calls x 64 KiB = 500 MB hashed at every point); only chunk size varies, so
the number of DIT regions moves 4096x while dwell is constant by construction.
`secret_mix_chunk` writes to a sink instead of returning, which keeps the chunk
loop public - a returned value would taint the caller and collapse everything
into one region. Containment: **4 `MSR DIT`, all in `secret_mix_chunk`**.

| chunk | DIT regions | always vs base | fine vs base | win (always -> fine) |
|---|---|---|---|---|
| 65536 | 8,000 | +0.97% +/-0.39 | +0.48% +/-0.35 | **-0.49% +/-0.22** |
| 4096 | 128,000 | +0.99% +/-0.32 | +0.63% +/-0.17 | **-0.35% +/-0.23** |
| 512 | 1,024,000 | +1.05% +/-0.22 | +1.20% +/-0.12 | **+0.14% +/-0.13** |
| 64 | 8,192,000 | +1.11% +/-0.26 | +4.24% +/-0.31 | **+3.09% +/-0.15** |
| 16 | 32,768,000 | +0.90% +/-0.37 | +10.77% +/-0.41 | **+9.78% +/-0.21** |

**Always-on is flat at ~1.0% across all five points.** It never toggles, so
chunking cannot affect it - which makes it an internal control confirming that
every change in the fine arm is toggle cost.

**Crossover: between 128k and 1.02M DIT regions**, interpolating ~550k over a
13.8 s run = **~40k regions/sec**, i.e. **~1 us of work per region**. Below that,
toggle overhead exceeds everything fine-grained placement can save. At the
extreme (32.8M regions) fine-grained is **10x worse than always-on**.

Measured toggle cost: **62-74 ns per region** = ~250-300 cyc for the 4 `MSR DIT`
in that function, so ~60-75 cyc per switch. Same order as the 30 cyc in
`docs/results/dit-cost-model.md`, measured end-to-end in a real program.

### The two-axis summary

- **Secret fraction sets the size of the prize.** Here the prize is the
  always-on cost of the public code: ~1.0%. Nothing about placement can exceed it.
- **Granularity decides whether you can collect it.** Above ~1 us of work per
  region you collect roughly half; below it you pay multiples of the prize.

### Actionable for the pass

The admission test (`-taint-dit-switch-cyc`) currently defaults to **0 = finest**,
which asserts toggles are free. These data say a region must hold **~3000+ cycles
of work** before it is worth creating. That is far coarser than the shipped
default and is measured end-to-end, not modelled.


---

## CORRECTION 2026-08-11: the round-trip control changes everything above

Every `fine`-arm number above was measured against the **stock -O2 binary**. That
is the wrong baseline. The `-ftaint-harden` pipeline (lower to MIR -> serialise to
MIR text -> reparse -> re-lower) changes codegen *by itself*, with an empty taint
file and zero `MSR DIT` emitted.

Measured with a dedicated control binary (`-ftaint-harden=<empty file>`),
chunk=65536, n=6 paired:

| comparison | effect |
|---|---|
| `base -> rt` - round-trip codegen only, **zero DIT** | **+0.58% +/- 0.24** |
| `rt -> fine` - DIT dwell + toggles, **isolated** | **+0.06% +/- 0.21** |
| `base -> fine` - the two mixed (what was reported above) | +0.63% +/- 0.14 |

**Almost the entire "fine-grained cost" was the pipeline, not DIT.** At coarse
granularity the round-trip artifact is ~10x the real DIT cost.

What the artifact is: **not** code bloat. `JS_CallInternal` has *fewer*
instructions after the round-trip (9,584 vs 9,633), with the difference spread
thinly across a dozen opcodes and identical branch density. It looks like block
layout / post-RA scheduling landing differently, which matters disproportionately
in a 38 KB function with a 209-target computed-goto dispatch loop. **No specific
pass has been identified** - do not claim one without evidence.

### Corrected granularity results (0.58% pipeline offset removed)

| DIT regions | always | fine (DIT only) | true win |
|---|---|---|---|
| 8,000 | +0.97% | -0.10% | **fine better by 1.07%** |
| 128,000 | +0.99% | +0.05% | **0.94%** |
| 1,024,000 | +1.05% | +0.62% | **0.43%** |
| 8,192,000 | +1.11% | +3.66% | -2.55% (fine worse) |
| 32,768,000 | +0.90% | +10.19% | -9.29% |

- **The win is LARGER than first reported**: ~1.07% at coarse granularity, i.e.
  fine-grained recovers essentially the *whole* always-on cost, not half of it.
- **The crossover moves later**: ~1.4M regions, i.e. **~0.34 us (~1300 cyc) of
  secret work per region**, not ~1 us.

### Also retracted: "the secret code is 30x more DIT-sensitive"

Measured directly - same binary, same data, only `PSTATE.DIT` flipped - the FNV
loop costs **0.0%** under DIT (114.0 ms both ways, reproduced). The secret code
gains nothing from the optimizations DIT disables. The apparent sensitivity was
the round-trip artifact being misattributed.

### Rule going forward

**Baseline every fine-grained measurement against a round-trip control built with
an empty taint source, never against the stock build.** Otherwise the tool's own
codegen variance is charged to DIT, and here it exceeded the signal by ~10x.
