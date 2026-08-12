# The DIT cost model — measured

**Measured:** 2026-07-14, **Apple M4** (`hw.optional.arm.FEAT_DIT = 1`), macOS,
`cc -O2`, P-core (QoS `USER_INTERACTIVE`), best-of-N.
**Benchmarks:** `playground/dit_bench/` (`sh playground/dit_bench/run.sh`).
Frequency calibrated per run against a dependent `add` chain (3.94 GHz observed).

This file addresses the question every DIT placement decision was blocked on
(`docs/design/dit-placement.md` §4 P2, handoff next-action #1): **what does DIT cost?**
It answers the *toggle* half. It does **not** answer the *dwell* half — see the
warning immediately below.

## The READ is free - `MRS DIT` is 1 cycle (measured 2026-08-08, M5)

`MSR DIT` (write) costs ~30 cycles. **`MRS DIT` (read) costs 1.00 cycle**, and
still 1.00 with a data dependency forced on the result, so it is not merely being
hidden by out-of-order execution. The write is **30x** the read.

That asymmetry is what makes the DIT *ownership rule* pay. A callee can read its
entry state, and skip clearing when it was entered with DIT already set, instead
of the caller blindly re-asserting afterwards:

| per call | cycles |
|---|---|
| today: callee sets, callee clears, caller re-asserts (3x `MSR`) | **90.67** |
| ownership, entry state kept in a frame slot (`mrs`/`str`/`ldr`/`tbnz`) | **2.01** |
| ownership, entry state kept in a register (`mrs`/`tbnz`) | **1.03** |

**45x cheaper per call, and it works through an indirect call** - the caller never
has to know who it called, which is the case `PreservesDIT` provably cannot reach
(libtomcrypt dispatches AES through a table `register_cipher()` writes at run
time). Benchmark: `playground/dit_bench/dit_own_bench.c`.

⚠️ **Measurement trap.** The guarded `msr DIT, #0` only disappears when DIT is
genuinely ON, so the loop must run with DIT set. A first attempt measured the
sequence with DIT off, the `tbnz` fell through, the 30-cycle write executed, and
the ownership path read as **52 cycles** - i.e. barely better than today, the
opposite conclusion. The benchmark now asserts DIT=1 during those runs and prints
it. Gate any future variant on that line.

## The two numbers

| Term | Status |
|---|---|
| **Toggle** — `MSR DIT` that *changes* the bit | **~30 cycles**, fully serializing — solid, see below |
| **Dwell** — running code with `PSTATE.DIT = 1` | **NONZERO and workload-dependent.** Up to **~15% on some SPEC 2026 benchmarks** with DIT fully on (measured separately by the project owner). The microkernels below show ~0 — **they are not representative; see §"Why the microkernels show zero".** |

> ⚠️ **Do not cite the ~0 dwell numbers in this file as "DIT is free."** They were
> briefly written up that way on 2026-07-14 and that conclusion was **wrong**. The
> ground truth is the SPEC 2026 result: real workloads lose real performance when
> DIT is left on. Both terms of the objective function are alive:
>
> ```
> cost = toggles × ~30 cyc  +  dwell(workload) × time_in_DIT
> ```
>
> Minimizing *only* toggles (i.e. coarsening to whole-function or whole-program
> DIT) is what costs 15% on the benchmarks that are sensitive. **This is precisely
> the cost the taint analysis exists to avoid, and it validates the project's
> premise rather than undermining it.**

### Toggle cost, in context (cycles, 32 instructions per loop iteration)

| Instruction | Cycles each |
|---|---|
| `add` (dependent, reference) | 1.00 |
| **`msr DIT` (alternating 1/0)** | **30.25** |
| `msr DIT, #1` when already 1 (no change) | 12.00 |
| `isb` | 34.01 |
| `dsb ish` / `dsb sy` | 18.00 |
| `dsb sy` + `isb` (what the removed ISB/DSB mode emitted, for reference) | 29.51 |
| `bl` + `ret` (pair, for scale) | 2.03 |

A toggle is a **pipeline flush**: interleaving independent ALU work with the
toggles does not hide any of the cost (32 toggles + 64 independent adds = 1224
cyc vs 32 cyc for the adds alone → ~37 cyc/toggle, i.e. no overlap). A
**region costs ~60 cycles** (enter + exit) — roughly **30× a call/ret**.

Note the same-value write still costs 12 cycles. It is cheaper than a real
toggle but **not free**, so redundant re-asserts (the post-call `MSR DIT, #1`)
are not free either.

### Dwell cost: the DIT-sensitive workload, FOUND — the Apple LVP (up to 4×)

**The cleanest DIT-sensitive workload is LVP-predictable pointer chasing** (measured
2026-07-15 on M4; `playground/dit_bench/lvp_dit.c`, reproduced). The M4 has a Load
Value Predictor (FLOP, USENIX Sec'25; see `docs/research/value-timing-leaks.md`),
and **DIT disables it**. On a self-dependent load chase over a constant-valued
L1-resident array, DIT-on vs DIT-off is **0.999 → 3.999 cyc/hop = 4.00× dwell
cost** — identical code/data/cache, so unambiguously the LVP. This is the largest
per-region dwell cost measured, and it is exactly the code fine-grained placement
must keep OUT of DIT: the LVP accelerates *public* predictable-load code.

Whole-program it dilutes: FLOP measured **4.5% on Speedometer 3.0** for
process-wide DIT in Safari (0.6% on BYTE). So the dwell term ranges from ~0 (plain
ALU, below) through ~4.5–15% (whole real workloads) to **4× on LVP-critical
regions** — the spread across code type *is* why placement granularity matters.

**Also, historically:** with DIT fully on, **some SPEC 2026 benchmarks lose ~15%**
(measured by the project owner; hardware/benchmark breakdown TBD — see "Open" at
the end). Dwell is real, it is workload-dependent, and it is exactly the cost this
project exists to avoid paying on public code.

The kernels below therefore measure **what my microbenchmarks failed to contain**,
not what DIT costs. They are kept because a *reproducible negative* is useful: it
bounds where the 15% is *not* coming from (plain integer ALU, multiplies, and
these load/store patterns on M4), which narrows the hunt for where it *is*.

Identical code, `PSTATE.DIT` set before the timed loop and verified still set
after it (the harness `MRS`-checks this and aborts otherwise — an early version
of this measurement was wrong until that guard was added).

| Kernel | DIT off | DIT on | Ratio |
|---|---|---|---|
| dependent `add` chain | 1.000 cyc/op | 1.000 | **1.000x** |
| dependent `mul` chain | 3.000 | 3.000 | **1.000x** |
| dependent `umulh` chain | 3.000 | 3.000 | **1.000x** |
| independent `mul` (throughput) | 0.333 | 0.333 | **1.000x** |
| deref pointer array (DMP-sensitive) | 3.268 ns/op | 3.296 | 1.009x |
| pointer chase (latency-bound) | 8.057 ns/op | 8.052 | 0.999x |
| streaming loads | 0.285 ns/op | 0.283 | 0.992x |
| silent stores (store value already present) | 0.254 ns/op | 0.254 | 1.000x |
| dirty stores | 0.254 ns/op | 0.254 | 1.000x |

Operand-value sensitivity (`mul`/`umulh` with zero vs random operands) is
**1.000x in all four combinations even with DIT off** — the M4's integer
multiplier shows no data-dependent timing to suppress in the first place.

**End-to-end, the project's own reference workload:** `firefox_convolve_int.c`,
byte-identical code, `PSTATE.DIT` set for the *entire program* via a constructor:

```
DIT off : min 624.9 ms   DIT on : min 604.7 ms   →  0.968x
```

i.e. `firefox_convolve_int` is **DIT-insensitive** on M4. Combined with the SPEC
2026 result, the useful reading is not "DIT is free" but: **DIT sensitivity varies
enormously by workload.** `firefox_convolve_int` is therefore a *bad* benchmark for
evaluating placement quality — it cannot show a win, because it has nothing to
lose. Placement work needs a DIT-sensitive workload to be evaluated against
(finding those is an explicit project goal; see "Open").

### Why the microkernels show zero

Hypotheses for the gap between these kernels (~0) and SPEC 2026 (~15%), i.e. where
to look for the sensitive patterns — **untested, listed for the next session**:

- **SIMD/NEON and FP.** Everything here is scalar integer. Data-dependent
  optimizations in the vector and floating-point paths are a prime suspect and are
  entirely unmeasured.
- **The data memory-dependent prefetcher (DMP).** DIT disables it on M3+. My
  DMP-sensitive kernel did *not* isolate it: at 3.27 ns/op into a 256 MB arena the
  loads already run with heavy memory-level parallelism from out-of-order
  execution, masking any DMP contribution. A GoFetch-style test would be sharper.
  Pointer-heavy workloads are where this should bite.
- **Microarchitectural breadth.** Zero-latency move elimination, store-to-load
  forwarding, branch/predictor interactions — none probed.
- **Scale.** SPEC benchmarks have large working sets and complex control flow; a
  32-instruction loop with a hot L1 working set exercises almost none of the
  machinery DIT constrains.

## ★ END-TO-END RUNTIME, MEASURED (2026-08-03) — libsodium, and it is a NEGATIVE

**This is the "measure runtime" item that was next-action #1 for the whole project.**
Everything before this section was either a microbenchmark or a static count. Rig and
repro: `utils/taint_libsodium_eval.sh` + `utils/taint_libsodium_bench.sh`.

Five configurations, identical benchmark source, libsodium 1.0.21 (`--disable-asm`,
whole-library bitcode). **Re-measured 2026-08-05 with the corrected harness**
(interleaved round-robin, median of 10, `cntvct_el0` ns/op):

| benchmark | metric | A base | B default | D tuned | E function | C whole-DIT | B/A | D/A | E/A | **C/A** |
|---|---|---|---|---|---|---|---|---|---|---|
| ed25519 | Sign | 9808 | 14263 | 11222 | 11005 | 9842 | 1.454x | 1.144x | 1.122x | **1.003x** |
| ed25519 | Verify | 20492 | 22224 | 20862 | 20880 | 20548 | 1.085x | 1.018x | 1.019x | **1.003x** |
| aead_chacha20poly1305 | Encrypt | 1280 | 2483 | 1942 | 2028 | 1279 | 1.941x | 1.518x | 1.585x | **1.000x** |
| aead_chacha20poly1305 | Decrypt | 1318 | 2528 | 1988 | 2091 | 1316 | 1.918x | 1.507x | 1.586x | **0.998x** |

Noise floors from the same run (within-config spread vs between-config range):
AEAD encrypt **2.2% vs 94.1%** and decrypt **2.3% vs 92.2%** — solidly resolvable;
ed25519 Sign **2.5% vs 45.4%** — solid; ed25519 **Verify 2.8% vs 8.5% — marginal**, and
in a noisier back-to-back run it tripped the `NOT RESOLVABLE` check (5.9% vs 8.7%).
**Treat Verify's ~1.08x as indicative only**; Sign and AEAD are the trustworthy figures.
The earlier min-of-5 numbers (1.464/1.098/1.937/1.917) agree to within ~1%, confirming
the harness bugs that wrecked argon2id did not distort these — each config here takes
seconds, so there is no room for thermal drift.

- **A** unhardened, DIT never set. **B** taint-hardened at the *shipped defaults*
  (`region`, `switch-cyc=0`, `loop-hoist=0`). **D** taint-hardened tuned for
  serializing switches (`switch-cyc=30`, `loop-hoist=1`). **E** `placement=function`.
  **C** unhardened + DIT set across the whole measured region (the coarse mitigation
  FLOP/Safari ship).

**The four findings, in order of importance:**

1. **Blanket DIT is FREE on libsodium (1.000–1.020x).** These primitives are
   DIT-insensitive on M4. That is now three independent workloads agreeing —
   `firefox_convolve_int` (0.968x), the int8 MAC gate (below), and libsodium.
2. **Every taint-driven policy costs MORE than blanket DIT.** At the shipped defaults:
   **+46%** on ed25519 sign, **+94%** on AEAD encrypt. On this workload the coarse
   mitigation dominates ours on *both* axes — it is faster *and* covers strictly more
   code.
3. **The toggle-cost diagnosis is confirmed, and the shipped defaults are mistuned for
   real hardware.** `switch-cyc=0` asserts toggles are free; they are ~30 cyc and
   serializing here. Retuning (D) recovers about half the loss — 1.46→1.16 and
   1.94→1.51. This is the first measurement backing the placement doc's claim that
   `loop-hoist=1` is the right choice for serializing-switch hardware.
4. **Function granularity ≈ tuned region placement** (E ≈ D). Exactly what the model
   predicts when dwell ≈ 0: narrowing coverage buys nothing, so you only pay toggles.

**What this does and does not mean.** It does **not** refute the approach — it is what
`cost = toggles×30cyc + dwell×time` predicts when `dwell ≈ 0`, and the value
proposition was always conditional on dwell being real. What it establishes is that
**libsodium-on-M4 cannot justify fine-grained placement**, and it puts a number behind
`docs/research/ct-call-handling.md` §5.2's warning: *"it is worth approximately nothing on DIT
unless DIT-everywhere is measurably expensive."* On DIT-insensitive workloads the
analysis's value is the **audit** output (ESCAPE / UNCOVERED / CLOBBER), not speed.

**Caveats.** No `sudo` ⇒ no kperf cycle counters; these are `cntvct_el0` deltas over
1000 iterations, so ratios are solid and absolute *cycles* are not (the counter measures
time, not cycles). **`cntfrq_el0` on this M4 is 1 GHz, so 1 tick = 1 ns** — an earlier
version of this file said 24 MHz, which was wrong. Verified against a direct
`clock_gettime` measurement of one argon2id hash: 271,631,726 ticks vs 0.271 s.

### argon2id — the long-operation case, and it is FREE (2026-08-05)

argon2id is CIO's headline worst case (**27.84x**). It could not be run at the harness's
default 1000 iterations: its loops are nested, giving `66 x num_runs` hashes at
`OPSLIMIT/MEMLIMIT_MODERATE` (0.271 s each, measured) ≈ **5.5 h per configuration, ~27 h
for the matrix**. Re-run at `ITERS=5 WARMUP=1` (~9 min) via the new override in
`taint_libsodium_bench.sh`:

**Result: NO MEASURABLE OVERHEAD.** Corrected run (interleaved, `REPS=5`, medians):

| benchmark | metric | A base | B default | D tuned | E function | C whole-DIT |
|---|---|---|---|---|---|---|
| argon2id | KDF median (ms) | 280.5 | 281.0 | 280.4 | 281.0 | 279.9 |
| | vs base | — | **1.002x** | **1.000x** | **1.002x** | **0.998x** |

Every configuration is within **±0.2%** of baseline — including blanket DIT. Between-config
spread of the medians is **0.4%** against a within-config spread of 4.1%, so the harness
correctly reports these as *not resolvable*: the true differences are smaller than what
this setup can measure. The honest claim is **"argon2id overhead is unmeasurable, well
under 1%"**, not any specific ratio.

> ### ⚠️ A measurement bug, and the lesson (2026-08-05)
>
> The first `REPS=1` run reported `1.010/1.025/1.028/1.031x` and the `REPS=5` re-run
> reported `1.036/1.040/1.041/1.042x`. **Both were artifacts.** `taint_libsodium_bench.sh`
> originally looped *variant outer, reps inner*, which confounds configuration with
> wall-clock time. On argon2id each configuration takes ~9 min, so across a 45-min run
> the machine warms up and whichever config ran first wins:
>
> ```
> A baseline  271.7  275.1  279.7  280.3  281.3   <- monotonic drift, 3.5%
> B default   281.6  281.5  282.2  282.7  282.6
> D tuned     282.6  283.1  284.7  282.9  283.2
> E function  282.8  283.4  283.3  283.1  283.2
> C whole-DIT 283.1  283.2  283.2  283.2  283.8
> ```
>
> Baseline's own spread (**3.5%**) is as large as the entire between-config spread
> (**4.2%**) — when within-group variance matches between-group variance there is no
> signal. `min-of-N` made it worse by selecting baseline's coldest run. Comparing like
> with like — baseline's *warm* sample (281.3) against the others' minima (281.5–283.1)
> — every configuration is within **~0.6%**.
>
> **Two fixes, both in `taint_libsodium_bench.sh`:**
> 1. **Interleave.** Build all variants first, then run round-robin (rep outer, variant
>    inner) so drift hits every config equally. **Any benchmark whose per-configuration
>    runtime is long enough for the machine to drift needs this** — ed25519/AEAD escaped
>    it only because each config there takes seconds.
> 2. **Report the MEDIAN, not min-of-N.** `min` assumes noise only ever makes a run
>    slower, so the fastest sample is cleanest. That is false here: argon2id's very first
>    process launch came in at **273.0 ms** against a 280–284 ms steady state, so `min`
>    latched onto a cold-start outlier and *still* reported a spurious **1.026x** even
>    after interleaving. Median ignores it and gives ±0.2%.
>
> A within-config vs between-config spread line now prints automatically and flags
> `NOT RESOLVABLE` when the two are comparable.
>
> **Residual, not fixed:** each config still holds a fixed *position within* a rep (A
> always first), so rep 1's cold slot always lands on baseline. Diluted across 5 reps and
> harmless at this effect size, but rotating the order per rep would be needed to resolve
> sub-1% differences.

**The finding that matters: instrumentation overhead is amortized by operation length.**

| operation | duration | default-placement overhead | absolute delta |
|---|---|---|---|
| AEAD chacha20poly1305 encrypt | **1.28 µs** | **+94%** | 1.2 µs |
| ed25519 sign | **9.81 µs** | **+45%** | 4.5 µs |
| argon2id KDF | **280.5 ms** | **~0%** | 0.5 ms (within noise) |

The cost tracks *executed toggles relative to runtime*, not a fixed per-call charge (the
absolute deltas are 1.2 µs and 4.5 µs for AEAD and ed25519, not equal). argon2id spends
its time memory-bound over a 256 MiB working set, so its toggles vanish into the noise.
**Against CIO's own 27.84x on this exact primitive, ours is unmeasurable (≤1%)** — the
project's strongest head-to-head number, worth stating whenever the libsodium negative
above is cited.

### int8 quantized MAC — the DIT-sensitivity gate, run at last (2026-08-03)

`playground/dit_bench/int8_mac_dit.c` was committed but never run and its results were
never recorded. Run now: **flat 1.000x in all eight cells** (4 activation patterns ×
{PAR, DEP loop shapes}), DIT on vs off. More important than the ratio: the *activation
data itself* does not change timing even with DIT **off** — ZERO/CONST/SPARSE/RAND are
indistinguishable (PAR 1225.2–1226.1 µs; DEP 239.0–239.1 ms). There is no
value-dependent timing here for DIT to suppress. Reproducible to within 0.1% across
three runs; the calibration line varies (2.7–4.6 GHz) because it runs before the core
boosts, and is not the measurement.

The machine is not at fault: `lvp_dit.c` in the same session shows the LVP alive and
DIT killing it at exactly **4.00x**.

**Honest limitation.** The `DEP` shape was designed to be the sensitive case and showed
nothing, so the benchmark never demonstrated it *can* detect the LVP. That leaves "int8
MAC is not LVP-exploitable" and "this DEP construction fails to engage the LVP"
unseparated. One concrete suspect: `a[idx]` is a sign-extending **byte** load, where
`lvp_dit.c`'s working probe uses 4-byte loads. Fix the probe before citing this as a
general result about int8 MAC.

## What this means for the project

**The project's premise stands.** The handoff's rationale — *"DIT is NOT free…
while it is ON the core loses hardware optimizations. So DIT-everywhere is
expensive… the project's entire value is enabling DIT only around instructions
that operate on secrets"* — is **supported** by the SPEC 2026 result (~15% on
sensitive benchmarks with DIT fully on). Both terms are live:

```
cost = toggles × ~30 cyc  +  dwell(workload) × time_in_DIT
       \__ favours coarse __/   \__ favours fine-grained __/
```

The two terms **pull in opposite directions**, which is what makes placement a
real optimization problem rather than a "just coarsen it" problem:

1. **Fine-grained region placement remains the goal** (handoff next-action #3).
   The dwell term is what it buys, and on a DIT-sensitive workload that is worth
   up to ~15%. What the toggle measurement adds is a **hard floor on how fine it
   is worth going**: a region costs ~60 cycles to enter and leave, so a region is
   only worth creating if it removes more than ~60 cycles' worth of dwell from
   the covered code. That is the concrete admission test the current hand-tuned
   `-taint-region-merge-gap` knob is a proxy for — and now it can be derived
   instead of guessed. `docs/design/dit-placement.md` §5's lazy-code-motion design
   still applies; its objective just gains the dwell term rather than minimizing
   toggles alone.
2. **Coarsening is still right *within* the call graph, where it is free.**
   Hoisting a toggle out of a hot leaf (set DIT at the outermost tainted point,
   let callees inherit it — PSTATE.DIT survives calls and AAPCS64 has no
   callee-saved rule for it) removes toggles **without extending dwell over any
   additional secret-free code**, so it is a pure win independent of the dwell
   number. Today's function granularity pays ~60 cyc per activation of a tainted
   leaf called in a hot loop. `PreservesDIT` + §5.3 of the placement doc is the
   fix. This is the part of the earlier (wrong) analysis that survives.
3. **Forward-looking, the dwell term only grows.** This project targets a 5+ year
   horizon. Future cores add *more* data-dependent optimizations for DIT to
   suppress, not fewer, so the cost of DIT-everywhere trends **up** and the value
   of secret-aware placement trends up with it. No measurement on today's silicon
   — favourable or not — should be read as a statement about that trajectory.
4. **Beyond performance, the taint analysis is load-bearing for correctness:** the
   `ESCAPE` call-site report, and `docs/design/dit-placement.md` §3 G2 — a tainted
   `SDIV`/`UDIV` is **not** covered by DIT, so DIT-everywhere is *silent false
   assurance*, while the analysis can point at the uncovered instruction.

## History — a wrong conclusion, recorded so it is not re-derived

On 2026-07-14 this file initially concluded from the microkernels that "dwell ≈ 0,
therefore DIT-everywhere is nearly free and fine-grained placement is a
pessimization." **That was wrong**, and it was wrong in an instructive way: the
kernels were scalar-integer, small-working-set loops that happen to contain none
of the patterns DIT penalizes, so they measured the benchmark's blind spots rather
than DIT's cost. The SPEC 2026 data (~15%) is the ground truth. If a future
measurement again shows ~0 on some workload, the correct inference is *"this
workload is DIT-insensitive"*, **not** *"DIT is free."*

## Caveats — read before generalizing

- **The ~30 cyc toggle number is M4, single-threaded, P-core, `-O2`.** It should be
  re-measured per target core; it is the one number here that is solid, but it is
  solid *for this microarchitecture*.
- **The ~0 dwell kernels prove nothing about DIT** beyond "these specific patterns
  are insensitive on M4." Do not generalize from them (see History).
- DIT constrains a **specified instruction list**; instructions outside it (divides,
  FP/denormal paths) get no DIT guarantee at all — relevant to §3 G2, not to cost.

## Open

- **A DIT-sensitive REAL workload is now the single blocking gap.** Three workloads
  have come back insensitive (firefox_convolve 0.968x, int8 MAC 1.000x, libsodium
  1.00–1.02x). Until one is found, fine-grained placement cannot be shown to win on
  anything, and the honest position is that coarse/whole-process DIT is the better
  engineering choice on every workload measured so far. The LVP pointer-chase (4.00x)
  is the one confirmed sensitive pattern — a *real* application built around that
  access pattern is what to hunt for next.
- **Retune the shipped defaults.** `-taint-dit-switch-cyc=0` encodes "toggles are
  free", which is false on M4 by ~30 cyc each, and measurably costs ~2x the overhead
  of the tuned setting. Either change the defaults to the serializing-hardware values
  or make the cost model derive them from a target hint.
- **Where does the SPEC 2026 15% come from?** Which benchmarks, which core, and
  which code patterns. This is the number that drives the whole dwell term, and
  reducing it to a set of *patterns* is what would let the region-admission test
  above be computed statically. (Explicit project goal: find real-world workloads
  where DIT is needed *and* coarse-grained placement hurts. Deferred, not dropped.)
- Full SPEC 2026 methodology (hardware, config, per-benchmark deltas) is not yet
  recorded here — it lives with the project owner. Capture it here when available.
