# The DIT cost model — measured

**Measured:** 2026-07-14, **Apple M4** (`hw.optional.arm.FEAT_DIT = 1`), macOS,
`cc -O2`, P-core (QoS `USER_INTERACTIVE`), best-of-N.
**Benchmarks:** `playground/dit_bench/` (`sh playground/dit_bench/run.sh`).
Frequency calibrated per run against a dependent `add` chain (3.94 GHz observed).

This file answers the question every DIT placement decision was blocked on
(`taint_dit_placement.md` §4 P2, handoff next-action #1): **what does DIT cost?**

## The two numbers

| Term | Measured on M4 |
|---|---|
| **Toggle** — `MSR DIT` that *changes* the bit | **~30 cycles**, fully serializing |
| **Dwell** — running code with `PSTATE.DIT = 1` | **~0** (≤1%, at noise) |

### Toggle cost, in context (cycles, 32 instructions per loop iteration)

| Instruction | Cycles each |
|---|---|
| `add` (dependent, reference) | 1.00 |
| **`msr DIT` (alternating 1/0)** | **30.25** |
| `msr DIT, #1` when already 1 (no change) | 12.00 |
| `isb` | 34.01 |
| `dsb ish` / `dsb sy` | 18.00 |
| `dsb sy` + `isb` (what `-taint-insert-isb` emits) | 29.51 |
| `bl` + `ret` (pair, for scale) | 2.03 |

A toggle is a **pipeline flush**: interleaving independent ALU work with the
toggles does not hide any of the cost (32 toggles + 64 independent adds = 1224
cyc vs 32 cyc for the adds alone → ~37 cyc/toggle, i.e. no overlap). A
**region costs ~60 cycles** (enter + exit) — roughly **30× a call/ret**.

Note the same-value write still costs 12 cycles. It is cheaper than a real
toggle but **not free**, so redundant re-asserts (the post-call `MSR DIT, #1`)
are not free either.

### Dwell cost: zero, on everything tried

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

Whole-program DIT is **not a slowdown at all** on this workload (the 3% is
noise/layout, not a speedup one should claim).

## What this means for the project

**The premise that motivated secret-awareness does not hold on M4.** The
handoff states: *"DIT is NOT free… while it is ON the core loses hardware
optimizations. So DIT-everywhere is expensive… the project's entire value is
enabling DIT only around instructions that operate on secrets."* On M4 the
dwell term is **zero**, so DIT-everywhere is **not** expensive, and the "avoid
paying for DIT on public code" argument has no measured cost to avoid.

The objective function collapses accordingly:

```
cost = toggles × 30 cyc  +  instrs_in_DIT × ~0
     = toggles × 30 cyc
```

Consequences, in order of how much they change the plan:

1. **Fine-grained region placement is a pessimization, not an optimization.**
   Narrowing a region can only *add* toggles; each one costs ~30 cycles while
   the dwell it saves costs nothing. Handoff next-action #3 ("replace function
   granularity with cost-model-driven region placement") is now
   **contraindicated on this hardware** — the cost model, once measured, argues
   the other way. This retroactively **validates** `taint_dit_placement.md` §4
   P2 ("treat dwell as near-free and toggle count as the objective") and §5's
   lazy-code-motion design, whose objective is minimizing *executed toggles*.
2. **The optimum is to coarsen, i.e. hoist toggles up and out.** Minimizing
   toggles means: set DIT once at the outermost tainted point, let callees
   inherit it (PSTATE.DIT survives calls; AAPCS64 has no callee-saved rule for
   it), and never toggle in a leaf. Today's function granularity is *not* that
   optimum — a tainted leaf called in a hot loop pays ~60 cyc/activation, 30× a
   call/ret. The `PreservesDIT` summary bit is the seed of the fix; §5.3 of the
   placement doc is the design.
   The degenerate optimum — `MSR DIT, #1` once per thread entry and never again
   — is exactly what the arm64 kernel and BoringSSL do by hand, and on M4 the
   measurements cannot distinguish it from anything cleverer.
3. **What still justifies the taint analysis** (none of it is the perf argument):
   - **Portability of the claim.** This is *one* FEAT_DIT implementation. Arm
     does not architecturally promise a zero-cost DIT, and Neoverse V1/N2 and
     Graviton3+ are unmeasured. A core with real dwell cost restores the
     original argument verbatim. **Re-run `run.sh` on Graviton3 before
     generalizing anything here.**
   - **Toggle placement still needs taint** to know which functions need a
     toggle at all — and toggles are the entire measured cost.
   - **Correctness/audit**, which is where the analysis is uniquely load-bearing:
     the `ESCAPE` call-site report, and `taint_dit_placement.md` §3 G2 — a
     tainted `SDIV`/`UDIV` is **not** covered by DIT, so DIT-everywhere is
     *silent false assurance* while the taint analysis can point at the
     uncovered instruction. "Which instructions touch secrets" is a question
     DIT-everywhere cannot answer and this pass can.

## Caveats — read before generalizing

- **M4 only, and absence of evidence is not evidence of absence.** No kernel was
  found where dwell costs anything; that is not proof none exists. The
  DMP-sensitive kernel in particular did **not** cleanly isolate the data
  memory-dependent prefetcher (GoFetch mechanism, which DIT is documented to
  disable on M3+): at 3.27 ns/op into a 256 MB arena the loads are already
  running with heavy memory-level parallelism from out-of-order execution, so
  any DMP contribution is masked. A sharper DMP test could still find a real
  dwell cost for pointer-heavy secret code. Treat "dwell = 0" as *measured on
  these kernels*, not as an architectural guarantee.
- DIT constrains a **specified instruction list**. Instructions outside it
  (divides, FP/denormal paths) have no DIT guarantee, so a zero dwell cost is
  partly just "the M4 had little data-dependent timing to suppress here."
- All numbers are single-threaded, P-core, one microarchitecture, `-O2`.
