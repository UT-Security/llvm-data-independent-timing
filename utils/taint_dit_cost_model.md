# The DIT cost model — measured

**Measured:** 2026-07-14, **Apple M4** (`hw.optional.arm.FEAT_DIT = 1`), macOS,
`cc -O2`, P-core (QoS `USER_INTERACTIVE`), best-of-N.
**Benchmarks:** `playground/dit_bench/` (`sh playground/dit_bench/run.sh`).
Frequency calibrated per run against a dependent `add` chain (3.94 GHz observed).

This file addresses the question every DIT placement decision was blocked on
(`taint_dit_placement.md` §4 P2, handoff next-action #1): **what does DIT cost?**
It answers the *toggle* half. It does **not** answer the *dwell* half — see the
warning immediately below.

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
Value Predictor (FLOP, USENIX Sec'25; see `taint_value_timing_leaks_research.md`),
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
   instead of guessed. `taint_dit_placement.md` §5's lazy-code-motion design
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
   `ESCAPE` call-site report, and `taint_dit_placement.md` §3 G2 — a tainted
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

- **Where does the SPEC 2026 15% come from?** Which benchmarks, which core, and
  which code patterns. This is the number that drives the whole dwell term, and
  reducing it to a set of *patterns* is what would let the region-admission test
  above be computed statically. (Explicit project goal: find real-world workloads
  where DIT is needed *and* coarse-grained placement hurts. Deferred, not dropped.)
- Full SPEC 2026 methodology (hardware, config, per-benchmark deltas) is not yet
  recorded here — it lives with the project owner. Capture it here when available.
