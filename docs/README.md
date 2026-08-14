# FastDIT documentation

Interprocedural taint analysis + PSTATE.DIT hardening for AArch64, built as an LLVM
fork (branch `dit-tainter`). Secret data entry points are declared in a taint-source
file, taint is propagated through registers/stack/global memory at the MIR level
across all functions of a TU, and PSTATE.DIT mode switches are inserted so
secret-dependent code runs with data-operand timing side channels suppressed.

**Threat model: data-operand instruction timing (DIT), NOT speculation.** Speculation
defense is out of scope. Operating instructions (build, flags, gotchas) live in
`CLAUDE.md` at the repo root; this folder is the reference material behind them.

## Read in this order

| # | Doc | Why |
|---|---|---|
| 1 | [overview.md](overview.md) | The map. What it is, how to run it, how the analysis works, limitations, current state. |
| 2 | [reference/dit-spec.md](reference/dit-spec.md) | What the hardware actually guarantees. Everything else assumes this. |
| 3 | [results/dit-cost-model.md](results/dit-cost-model.md) | What DIT costs. Read before any placement work. |
| 4 | [results/quickjs.md](results/quickjs.md) | The first positive result, and the method rule that makes it trustworthy. |
| 5 | [results/sqlcipher.md](results/sqlcipher.md) | The definitive negative, and the gem5 study that bounds the whole thesis at ~1.4%. |
| 6 | [design/dit-placement.md](design/dit-placement.md) | Where switches go, which gaps remain, and the optimal-placement design. |

## Overview

- **[overview.md](overview.md)** - the single consolidated entry-point doc: what this
  is, the threat model, how to run it, how the taint analysis works, why the 3-phase
  serialize/reparse design is load-bearing, and how cross-function memory is handled.

## Reference

Stable facts that other docs and the code depend on.

- **[reference/dit-spec.md](reference/dit-spec.md)** - what PSTATE.DIT actually
  guarantees: the covered instruction set, the exclusions that matter (divide and
  sqrt), and the fact that address-dependent timing is not covered. The
  `isDITProtected` membership hook in `AArch64InstrInfo.cpp` is transcribed from this
  doc, so **keep the two in sync**.
- **[reference/firefox-integration.md](reference/firefox-integration.md)** - how to
  put the pass into a real Firefox build: the one-flag model, the toolchain
  requirement, taint-source file format, the two correctness constraints, and the
  recommended compiler-wrapper integration.

## Design and internals

How the analysis is built, which bugs were found in it, and what is still open.

- **[design/dit-placement.md](design/dit-placement.md)** - the central design doc for
  where DIT switches go: what exists today, the placement constraints the spec
  imposes, the remaining security and performance gaps, and the proposed spec-aware
  optimal placement with its evaluation plan.
- **[design/dit-callee-ownership.md](design/dit-callee-ownership.md)** - the
  ownership rule adopted 2026-08-08: *only the frame that turned DIT on may turn it
  off*. An instrumented callee used to clear DIT on exit, so callers re-asserted after
  every call and the switch count scaled with the CALL count. Shipped:
  `AlwaysEnteredWithDIT` fixes resolvable in-TU calls, and
  `-taint-dit-reassert-report=<file>` audits the rest. **Deferred (out of scope, for
  advisor discussion): the runtime `MRS` mode**, the only thing that fixes indirect
  *and* cross-TU calls. Measured: `MRS DIT` = 1.00 cyc vs `MSR DIT` = 30.34, so per
  call 90.67 -> 2.01.
- **[design/dit-tailcall-gap.md](design/dit-tailcall-gap.md)** - the tail-call gap
  fixed 2026-08-05. Whole-function placement cleared DIT *before* a tail call, so the
  callee receiving the secret ran unprotected (found on libsodium `crypto_sign`). Also
  records the permanent residual: after a tail call DIT may stay set indefinitely, so
  **an instrumented function does not restore DIT on every exit path**.
- **[design/dit-precision.md](design/dit-precision.md)** - the DIT precision metric
  (`-taint-dit-precision-report`): per function, how many instructions MUST run with
  DIT versus how many DO. `precision = need/underdit` is the number placement should
  maximize, but only against a switch budget, and **always read the loop-weighted
  variant** - unweighted, convolve's region placement looks 13 points better while
  being 7.16x slower.
- **[design/context-insensitivity.md](design/context-insensitivity.md)** -
  context-insensitive mod-sets are **the dominant false-positive source** (measured on
  libsodium: 169 of 199 FPs). Also records that P1b is a far smaller lever than
  assumed: only 17 of 583 secret-writing call sites resolve provenance to an argument.
- **[design/spill-soundness-bugs.md](design/spill-soundness-bugs.md)** - two spill
  soundness bugs fixed 2026-07-27 (`implicit-def` counted as a use; narrowed reload of
  a spilled secret), plus what spilling *does* handle correctly.
- **[design/frame-addr-fallback.md](design/frame-addr-fallback.md)** - the
  `-taint-frame-addr-args` prototype (default OFF): the `f(&local_secret)` under-taint
  it closes, and its measured cost.
- **[design/scalability.md](design/scalability.md)** - the compile-time wall, found
  and fixed 2026-08-10. `-ftaint-harden` could not compile `quickjs.c` (54k lines, 940
  functions); now **733 s = 10.7x** baseline with 29/29 tests passing. Overhead is a
  flat 1.3-1.5x up to ~100 functions. Cause was `JS_CallInternal`: 1890 blocks, a
  209-predecessor computed-goto hub. **The decisive fix was skipping
  `propagateTaintMBB` when `IN` is unchanged** (the transfer is a pure function of IN,
  so it cannot change OUT); `join` early-out plus reserve helped; the RPO worklist did
  *not* (the CFG is irreducible, one giant SCC, so there is no useful topological
  order). Still open: **taint spread** - one narrow taint source produced 13,222
  `MSR DIT`, 618 of them inside `JS_CallInternal`.

## Results

Measured numbers. These are the claims the project stands on, with their caveats.

- **[results/dit-cost-model.md](results/dit-cost-model.md)** - **the cost model, and
  read it before any placement work.** Toggle is ~30 cyc serializing (measured, M4);
  the read is free (`MRS DIT` = 1 cyc, M5); dwell runs up to ~15% on sensitive SPEC
  2026 benchmarks. Both terms matter and they pull opposite ways, and that tension
  *is* the placement problem. Also carries the **2026-08-03 libsodium negative**:
  blanket DIT is free (1.00-1.02x) while taint-driven placement costs +46%..+94% at
  the shipped defaults, roughly half that when tuned for serializing switches. Do NOT
  conclude "DIT is free" from the ~0 microkernels; that blind spot is documented in
  the doc's History section. Benchmarks in `playground/dit_bench/`.
- **[results/quickjs.md](results/quickjs.md)** - **the first positive result for the
  performance claim (2026-08-10/11).** QuickJS + Octane on M5, end-to-end wall time:
  always-on ~+1.0%, fine-grained's true DIT cost +0.06% +/-0.21, so fine-grained
  recovers ~1.07%, essentially the whole always-on cost.
  - **MUST-READ METHOD RULE: baseline fine-grained against a round-trip control
    (`-ftaint-harden=<empty file>`), never the stock -O2 build.** The 3-phase MIR
    round-trip alone costs +0.58% +/-0.24 with zero `MSR DIT` emitted. That artifact
    is ~10x the real DIT cost and, charged to DIT, produced two retracted claims.
  - A **-6.28% figure was first reported and is WRONG** as an end-to-end number: it
    came from the Octane score, which covers only the timed `run` sections (~14% of
    wall) and excludes setup, GC, *and* the secret work.
  - Make-or-break factors: taint containment (an arbitrary taint source gave 13,222
    switches, 618 in the interpreter, so **declassification decides generalisation**)
    and **`-taint-dit-loop-hoist=1`** (without it there is no win at all: 3/8, in-loop
    toggles eat everything). Caveats: constructed workload, secret fraction 0.04%, n=8.
  - **Granularity crossover, measured 2026-08-11** (dwell held constant, only region
    count varies): fine-grained wins at 8k-128k DIT regions (-0.49%/-0.35%), **loses
    above ~550k regions**, and is 10x worse than always-on at 32.8M regions (+9.78%).
    Crossover is ~40k regions/sec = **~1 us of work per region**. **Two axes: secret
    fraction sets the size of the prize (~1% here), granularity decides whether you
    can collect it.**

- **[results/sqlcipher.md](results/sqlcipher.md)** - **the definitive negative, and
  the study that bounds the thesis.** With a correct oracle (all three provider entry
  points, `cipher` + `kdf` + **`hmac`**) the recoverable headroom on M5 is **+0.89%**
  on the deprecated libtomcrypt provider and **-0.08%, i.e. ZERO**, on the DEFAULT
  shipping OpenSSL/hardware-AES build. Almost all of always-on's +8.6% is DIT *on the
  crypto*, which any correct placement must also pay. A **"+8.15% first positive
  result" was reported and RETRACTED the same day** - the oracle had wrapped 2 of 3
  entry points, so it was protecting *less*, not costing less. **Audit a manual
  placement for coverage before believing its performance.** Two findings that stand:
  the pass **structurally cannot instrument prebuilt `libcrypto.dylib`** (25 sites,
  none on a cipher instruction), and software AES is DIT-expensive only because of
  **T-table data-dependent loads**, whose real leak - cache timing - DIT does not
  cover, so **AES is a bad motivating workload**.
  - **gem5 corroboration, 2026-08-13** (full study:
    `gem5-DIT/docs/dit/studies/sqlcipher-dit-placement-2026-08-13.md`). Running the
    identical binary under serializing vs renamed `MSR DIT` isolates **toggle cost
    with dwell held constant**, which silicon cannot do: **+0.08% / +12.8% / +19.1%**
    for **6 / 54 / 63** switch sites. It reproduces the M5 ordering and the
    region:hoist ratio (1.49x vs 1.52x) at about a third the magnitude, so the
    granularity result no longer rests on one machine.
  - **The prize is ~1.4%**, all of it value prediction; DMP, SIP and comp-simp are
    inert or net-negative on this ROI. **The shipped placement spends 19% to protect
    something worth 1.4%.** No placement policy can recover more than the gated
    optimizations are worth - and **microbenchmarks overstate that value ~200x**
    (`lvp_chase` 4.0x vs 1-2% on real code).
  - **The round-trip control is necessary but NOT sufficient.** Under gem5 the
    zero-`MSR DIT` `nodit` binary is the **slowest in the entire matrix** (+2.65% vs
    plain), exceeding the whole dwell effect. The artifact is a per-binary codegen
    lottery (+0.58% QuickJS, +0.06% native, +2.65% gem5), so at ~1% effect sizes only
    a **same-binary, two-configuration** comparison is trustworthy.

## Research

Literature reviews and motivation. These carry "verified" and "refuted" sections;
respect the refuted ones.

- **[research/value-timing-leaks.md](research/value-timing-leaks.md)** - **why this
  project exists.** Non-crypto value-dependent-timing leaks. The Apple M4 has a Load
  Value Predictor and DIT disables it (FLOP, USENIX Sec'25); DIT-for-secret-regions is
  that paper's own recommendation, and whole-process DIT costs 4.5% on Speedometer, so
  fine-grained taint-driven placement is the cheap version. Also covers the Firefox
  subnormal-FP pixel-stealing leak, its integer "fix", and how the LVP reopened it.
- **[research/browser-history-leaks.md](research/browser-history-leaks.md)** - what
  Narayan et al. (WOOT'18) exploited, the 2010 `:visited` mitigation and the
  data-operand-timing assumption it rests on, Chrome's 2025 partitioning fix (Firefox
  and WebKit have NOT shipped it), and which hardware reopenings DIT does *not* cover
  (DVFS, GPU compression, cache). Carries **the benchmark reframe**: fine-grained wins
  when the *public* code has headroom and the secret code has none, which is the
  browser's shape. **§6 holds the always-on browser DIT measurements** (2026-08-09/10):
  +2.61% (+/-0.51) Firefox and +1.80% (+/-0.16) Chromium on Speedometer 3.1 / M5,
  20/20 reps slower in both. `RENDERER_ONLY=1` matches FLOP's actual methodology and
  gives Firefox +2.45%, Chromium +2.12%, so confining DIT to the renderer does NOT
  reduce the cost. An apparent "`MSR DIT` intermittently fails to gate the LVP"
  finding was **retracted** (noise in the ratio's denominator; across 800 rounds in 40
  processes DIT never once failed).
- **[research/memory-summaries.md](research/memory-summaries.md)** - literature and
  design behind interprocedural taint through memory. The callee-to-caller gap was
  fixed as blunt-TOP P0 on 2026-07-15 via a `FunctionMemEffects` mod-set plus
  caller-side `ExternalMemClobbered`. Contains the P1 argument-provenance design of
  record (2026-07-20). Repro: `playground/callee_memory_gap.c`.
- **[research/ct-call-handling.md](research/ct-call-handling.md)** - what the Spectre
  and constant-time hardening tools do *at a call*, where this design sits relative to
  them, whether anyone else carries a memory-effects summary, and the hard question of
  whether secret-awareness is paying for itself.
- **[research/cio-and-ct-literature.md](research/cio-and-ct-literature.md)** - CIO and
  the broader constant-time / speculative-execution literature, with explicit
  verified, refuted, and open-question sections.

## Handoff

- **[handoff.md](handoff.md)** - session handoff notes: why the M4 matters, the
  corrected threat model, state of the work, and next actions in priority order.

## Where the non-doc material lives

| What | Where |
|---|---|
| Harden-a-C-file wrapper | `utils/taint_harden_c.sh` |
| libsodium build + benchmark rig | `utils/taint_libsodium_eval.sh`, `utils/taint_libsodium_bench.sh` |
| Browser always-on DIT rig | `utils/taint_browser_dit_bench.sh`, `utils/browser_dit/` |
| DIT precision / region-spacing analysis | `utils/taint_dit_precision.py`, `utils/taint_region_distance.py` |
| DIT cost microbenchmarks | `playground/dit_bench/` |
| Scratch experiments (not shipping code) | `playground/` |
| Tests | `llvm/test/CodeGen/AArch64/taint-analysis-*.mir`, `llvm/test/Transforms/TaintAnnotate/` |
