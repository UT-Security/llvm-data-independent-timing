# DIT Barrier Placement: Current State, Gaps, and an Optimal-Placement Design

Status of the PSTATE.DIT backend (`-taint-insert-dit`), what the Arm spec lets us rely
on, where the current placement is unsound or wasteful, and a concrete design for
spec-aware optimal placement of `MSR DIT` mode switches.

---

## 1. What exists today

| Piece | Where |
|---|---|
| Master switch `-taint-insert-dit` (the `-taint-barrier-mode={isb,dit}` selector and the ISB/DSB mode were removed 2026-07-14; DIT is the only mechanism) | `llvm/lib/CodeGen/TaintAnalysis.cpp` (cl::opt), decl in `llvm/include/llvm/CodeGen/TaintAnalysis.h` |
| Generic hook `insertTimingModeSwitch(MBB, MI, DL, Enable)` | `llvm/include/llvm/CodeGen/TargetInstrInfo.h`, default in `TargetInstrInfo.cpp` |
| AArch64 impl: `MSR DIT, #1/#0` via `MSRpstateImm4` (pstatefield encoding 26) | `llvm/lib/Target/AArch64/AArch64InstrInfo.cpp` |
| Placement policy (function granularity) | `insertTaintDITSwitches` in `TaintAnalysis.cpp` |
| Lit test | `llvm/test/CodeGen/AArch64/taint-analysis-dit.mir` |

**Current policy:** if a function contains ≥ 1 tainted run (`collectTaintedRuns`),
insert `MSR DIT, #1` as the very first instruction of the entry block and
`MSR DIT, #0` immediately before every `isReturn()` instruction (covers
`RET_ReallyLR` and `TCRETURN*` tail calls). Additionally (P0 fix for G1),
`MSR DIT, #1` is re-asserted immediately after every non-tail call site, since a
callee may clear DIT on its own exit. Untainted functions are untouched.
The tainted-run computation (value taint, pointee-tainted loads, address-sensitive
memory accesses, merged across gaps of `-taint-region-merge-gap` clean
instructions) is unchanged by the removal of the ISB/DSB mode.

Placement is **purely syntactic** today: it keys off "function contains taint",
not off which instructions need protection, which paths reach them, or what the
call graph guarantees about the incoming DIT state.

---

## 2. What the spec gives us (placement constraints)

From the Arm ARM (DDI 0487), PSTATE.DIT / FEAT_DIT (Armv8.4; ID_AA64PFR0_EL1.DIT):

1. **Guarantee scope.** When PSTATE.DIT == 1, instructions in the DIT-listed set
   execute with timing (and asynchronous-exception response) **independent of
   their operand data values**. The listed set covers the bulk of A64 integer
   data-processing, most Advanced SIMD/NEON data-processing, and the crypto
   extensions (AES/SHA — constant-time crypto is the headline use case).
2. **Exclusions that matter to us.**
   - **`SDIV`/`UDIV` are not DIT-listed.** A divide on a secret operand stays
     data-dependent-timed with DIT=1.
   - FP `FDIV`/`FSQRT` are likewise not guaranteed.
   - **Loads/stores:** DIT guarantees independence from the *data values*
     transferred, **not** from the *address* — cache/TLB timing remains
     address-dependent. DIT is not a defense for secret-indexed accesses.
   - Branches: DIT does nothing for secret-dependent control flow (branch
     predictor/fetch timing).
3. **Toggle semantics.** `MSR DIT, #imm` is a direct PSTATE write:
   **self-synchronizing, no ISB required**; effects apply to subsequent
   instructions in program order. This makes toggles cheap and placement purely a
   dataflow problem (no barrier-ordering interactions).
4. **State propagation.** PSTATE.DIT is per-thread state: it survives calls,
   returns, and (via SPSR save/restore) exceptions and signal delivery. Nothing
   restores it around a call — **whatever the callee leaves behind is what the
   caller continues with**. There is no callee-saved convention for DIT in
   AAPCS64.
5. **Runtime requirement.** Executing `MSR DIT` on a core without FEAT_DIT is
   UNDEFINED (SIGILL). The dev machine (Neoverse N1, v8.2) cannot run it; V1/N2,
   Graviton3+, Apple M-series can.

Consequences: (a) DIT protection is a **property that must hold at each tainted
DIT-listed instruction**, i.e. a classic "expression must be available" dataflow
fact; (b) toggles are cheap but not free, so the objective is minimizing
*executed* toggles, not covered instructions; (c) some tainted instructions are
**uncoverable by DIT** and need to be diagnosed or handled by other means.

---

## 3. Security gaps in the current placement

Ranked by severity.

### G1 (unsound) — **FIXED (P0)**: a tainted callee's exit switch clears the caller's DIT
`f` tainted → entry sets DIT=1. `f` calls `g`; `g` is also tainted → `g` sets
DIT=1 at entry and **DIT=0 before its return**. Back in `f`, every tainted
instruction after the call executes with **DIT=0**. The function-granularity
choice avoided *region*-level nesting but recreated the same hole at call
boundaries within the TU. Any caller→callee pair that are both instrumented is
affected; deeper chains lose protection after the first callee returns.
*Status:* **fixed** — `insertTaintDITSwitches` re-asserts `MSR DIT, #1` immediately
after every non-tail call site inside instrumented functions (cheap, always
sound; lit-covered by `caller_fn` in `taint-analysis-dit.mir`). Eliding
provably-redundant re-asserts is the P3 summary work (§5.3).

### G2 (unsound in principle) — **DIAGNOSED (2026-07-16)**: tainted instructions the spec does not cover
The run collector treats all tainted instructions alike, but per §2.2 a tainted
`SDIV`/`UDIV` (or FP div/sqrt) is *not* protected by DIT=1. Silent false
assurance. Similarly, runs include **address-sensitive loads/stores** (a
secret-dependent address leaks via cache/TLB timing — DIT covers the loaded data
*value*, not the address) and **branches on tainted flags** (control-flow timing)
— DIT provides no guarantee for either. These hazards are NOT COVERED by this
project (speculation is out of scope; the ISB/DSB placeholder for it was removed
2026-07-14).
*Status:* **diagnosed** — a new `TargetInstrInfo::isDITProtected(MI)` hook drives
a `-taint-uncovered-report=<file>` report that emits an `UNCOVERED
<not-dit-covered|secret-address|secret-branch>` line per tainted-but-unprotected
instruction, instead of silently counting them protected. The hook is a
**membership list** against the Arm DIT covered set (`utils/taint_dit_spec.md`):
it returns true only for the enumerated covered integer data-processing, for
loads/stores (data value, by class), and for FP/SIMD data-processing (by class,
minus the explicitly-excluded divide/sqrt), and **defaults to uncovered** so an
unrecognised instruction is flagged rather than assumed protected (the safe
direction). Lit: `taint-analysis-dit-uncovered.mir`. Known limitations: a *raw*
(uncomputed) secret used directly as a store address is under-flagged (only
computed/address-tainted store addresses are caught; loads catch it via the
all-uses-are-address rule); and an unrecognised-but-actually-covered opcode
produces a spurious audit line until the covered switch is extended. Actually
*protecting* these (constant-time rewrite / substitution) remains out of scope —
the report is for audit.

### G3 (leak-adjacent) — **PARTIALLY ADDRESSED**: exceptional exits, unknown callees
Unwinds/`longjmp` out of an instrumented function leave DIT=1 in the unwinder and
beyond — *safe* direction (over-protection), only a perf leak (still open,
accepted). The **caller-side** hazard of external callees toggling DIT off is
covered by the unconditional/summary-gated after-call re-assert (P0 + elision).

**Protection *inside* an external/indirect callee is NOT out of reach — DIT is
inherited.** `PSTATE.DIT` is per-thread state with no callee-saved convention
(§2.4), so a callee entered with DIT=1 executes with DIT=1: `memcpy`, an indirect
target, a closed-source blob all inherit our mode. Under function granularity
every call from inside a tainted function is by construction made with DIT=1, so
a secret handed to an unknown callee **is** covered — to the same standard as our
own code (DIT-listed instructions only; an `SDIV` on the secret inside libc is no
more protected than one in our own function — that's G2, not a call problem).

This is a real asymmetry vs. the deleted ISB/DSB model, where a barrier had to be
*inside* the callee, so an uninstrumented callee was genuinely unprotected. The
old `ESCAPE`-report framing ("secrets handed to external/indirect callees can't
be protected by placement") is inherited from that threat model and is now
**misleading**; `-taint-callsite-report` should be read as an *audit list of
secret-carrying call sites*, not a list of unprotected ones.

Two genuine residuals: (a) an instrumented callee runs `MSR DIT, #0` on its exit —
but only after its own protected work, and the post-call re-assert restores our
mode; (b) **this ambient coverage is automatic ONLY under function granularity.**
See the "Scenario B invariant" box below.

> **DESIGN DECISIONS (2026-07-15) — memory-effects soundness fix.** Discussed and
> locked; see `taint_memory_summary_research.md` for the domain design.
>
> - **Unknown callees (external decl / indirect `BLR`) get blunt TOP in P0.** Any
>   tainted argument at such a call ⇒ assume it wrote a secret to every escaped
>   stack object, every address-taken global, and unknown memory. No refinement
>   in P0. The libc model table and `memory(argmem: write)` narrowing
>   (research §11 vii/viii) are **deferred until we measure** how much blunt TOP
>   over-instruments — the honest baseline first, then decide if the table earns
>   its maintenance. Indirect calls also get blunt TOP in P0; the address-taken-
>   target join (which would also close the "reached-only-indirectly, never
>   instrumented" sub-case) is a later precision option, not P0.
> - **Scenario B ("is the secret protected *during* the call?") gets an explicit
>   assert/report, not an assumption.** Under function granularity a call passing
>   a tainted arg is always inside DIT coverage (the tainted arg register makes
>   the function have tainted runs ⇒ it is instrumented ⇒ entry set DIT=1). That
>   invariant is currently true *by accident of granularity*. Add a verification
>   pass: for every call site with a tainted/pointee-tainted argument, assert the
>   enclosing function is DIT-instrumented (and, once regions exist, that the call
>   lies inside an enabled region). A violation is a leaked secret — report it,
>   and under an assertions build, assert. This turns an implicit invariant into a
>   guardrail the future region work cannot silently break.

### G4 (correctness of scope): protection starts at entry, but secrets may
pre-exist in memory
Function granularity protects everything the function executes, so this is
currently moot; it becomes real once placement is narrowed (§5) — the enable must
dominate *every* tainted instruction on *every* path, including paths through
landing pads and split cold blocks. Any optimal-placement rework must prove
domination, not assume block layout.

---

## 4. Performance gaps

### P1: redundant toggles across the call graph (**the** cost — measured)
In a tainted call chain `f → g → h`, today each function executes 2+ toggles per
activation. A toggle is **~30 cycles and fully serializing** — about **30× a
`bl`+`ret` pair (2.03 cyc)** — so a tainted leaf called per row/pixel (the
Firefox convolve kernels do exactly this) pays ~60 cyc/activation for protection
that costs nothing to *keep* once set. If every call site of `g` already
guarantees DIT=1, `g` needs **zero** toggles. Note even a redundant same-value
`MSR DIT, #1` (the post-call re-assert) costs **12 cycles** — cheaper than a real
toggle, but not free, so the `PreservesDIT` elision is worth real cycles.

### P2: whole-function dwell when taint is localized — **REAL, and the reason for §5**
This section used to hypothesize that dwell is near-free pending measurement on
FEAT_DIT hardware. **The hypothesis was wrong.** With DIT fully on, **some SPEC
2026 benchmarks lose ~15%** (measured by the project owner). Dwell is real,
workload-dependent, and is exactly the cost that narrowing placement to the
tainted region buys back. Data and caveats: `utils/taint_dit_cost_model.md`.

Consequences for this document:
- The objective has **two competing terms**, and §5's design must optimize both:
  `toggles × ~30 cyc` (favours coarse) + `dwell × time_in_DIT` (favours fine).
- A **toggle costs ~30 cycles** and fully serializes (measured, M4), so a region
  costs **~60 cyc to enter and leave**. That is the **admission test** for
  creating a region at all: only narrow if it removes more than ~60 cyc of dwell
  from the covered code. `-taint-region-merge-gap` is today's hand-tuned proxy
  for this test and can now be derived rather than guessed.
- Beware evaluating placement on `firefox_convolve_int`: it is **DIT-insensitive**
  (whole-program DIT = 0.968x), so it cannot demonstrate a win. Finding
  DIT-sensitive workloads is an open project task.
- P1 above (redundant toggles across the call graph) is an **independent** win:
  hoisting a toggle out of a hot leaf removes toggles *without* extending dwell
  over extra secret-free code, so it pays regardless of the dwell number.
- ⚠️ Do not re-derive "dwell ≈ 0" from a microbenchmark. The M4 microkernels in
  `playground/dit_bench/` show ~0 and are **not representative** — see the cost-
  model doc's "History" section.

### P3: one disable per return
Functions with many exit blocks execute at most one, so the *static* count is
harmless; but disables in cold exit blocks are pure code-size. Post-dominator
placement (§5) subsumes this.

### P4: measured region structure of `convolve_pixel_int` — why §5 must be loop-aware
Characterized 2026-07-15 from the taint report + region report + MIR CFG
(`-taint-output`, `-taint-regions-output` on the reference workload).

**Taint is sparse and clustered, not uniform.** Of 298 instructions in
`convolve_pixel_int`, only **85 (28.5%) are tainted**. The public part is
dominated by a large clean preamble — `entry` (35 instrs, 0 tainted) and
`for.body.lr.ph` (30 instrs, 0 tainted) — i.e. ~65 instructions of pure setup
that function granularity wraps in DIT for nothing.

**Static region count bottoms out at 9** (the taint forms 9 CFG-separated
clusters; `-taint-region-merge-gap` sweeps 24→15→9 as gap 0→2→≥16, then
plateaus — cross-block gaps never coalesce). Naive per-region placement =
2×count = 18–48 static toggles.

**But static count is the wrong cost metric, and this function proves it.** Five
of the tainted regions sit inside **self-looping inner-loop blocks** — `bb.12`/
`bb.14` (`vector.body`), `bb.16`/`bb.18` (`vec.epilog.vector.body`), `bb.21`
(`for.body7`) — whose backedges are weighted `0x7c...` vs `0x04...`, i.e. taken
~97% of iterations. A toggle placed *inside* such a block executes **once per
loop iteration**; `convolve_pixel_int` runs per output pixel, so "18 static
toggles" becomes thousands-to-millions of *dynamic* ~30-cyc toggles —
**strictly, catastrophically worse than function granularity's 2.** This is why
`-taint-region-merge-gap` (run coalescing) is the wrong knob: it shrinks the
static count but cannot move a toggle out of a loop.

**The only placement that beats function granularity is loop-hoisting.** Set
`MSR DIT, #1` once *before* the convolution loop nest and `MSR DIT, #0` once
*after*, so the five loop-resident regions are covered by the enclosing region
with **no per-iteration toggles**: ~2–4 executed toggles, covering the loop nest
(~230 instrs) while excluding the 65-instr public preamble. The delta vs
function granularity is exactly:

```
saved dwell   = ~65 preamble instrs × dwell_cost   (the win, workload-dependent)
added toggles = ~2 × 30 cyc ≈ 60 cyc               (one-time per call)
```

**Design consequence for §5:** the objective is *executed* (frequency-weighted)
toggles, and the enable must be **hoisted out of loops** (LICM-style), not merely
placed at region boundaries. This is a lazy-code-motion / partial-redundancy
problem over the machine CFG with block frequencies — the merge-gap proxy does
not model it. `convolve_pixel_int` is the canonical test case: a small secret
kernel inside a hot loop nest behind a large public preamble.

---

## 5. Proposed design: spec-aware optimal placement

Treat "PSTATE.DIT == 1" as a dataflow fact and place toggles by lazy-code-motion
over the machine CFG, extended interprocedurally by the existing fixed-point
framework.

### 5.1 Instruction classifier — **BUILT (Track C, 2026-07-16); it gates placement**
The classifier is the already-shipped `TargetInstrInfo::isDITProtected(MI)`
membership hook (`utils/taint_dit_spec.md`, `classifyDITUncovered`,
`-taint-uncovered-report`). §5 does **not** re-implement it — it *consumes* it.

**Coverability gates region creation — the load-bearing rule for §5.** A tainted
instruction DIT cannot protect must never cause a DIT region to exist or grow:
wrapping a secret `SDIV` in DIT pays a ~30-cyc toggle for zero protection (the
divide stays data-value-timed regardless). So the placement "need" set is
**coverable-tainted only**:

```
Need(MI)     = isTainted(MI) && isDITProtected(MI)     // drives MSR DIT placement
Residual(MI) = isTainted(MI) && !isDITProtected(MI)    // -> -taint-uncovered-report only
```

Consequences the placement pass must honour:
- **Uncoverable tainted instructions do not anticipate/require DIT.** They are
  excluded from down-safety and availability entirely — they can sit inside a DIT
  region (harmless) or outside it (also harmless: DIT wouldn't protect them
  either way), but they must never be the *reason* a toggle is inserted or a
  region extended. A function whose *only* tainted instructions are uncoverable
  needs **zero** toggles (and just emits residual-report lines).
- **`secret-address` and `secret-branch` are residual too**, not just
  divide/sqrt — DIT covers neither, so neither belongs in the Need set.
- **Fixes the current over-count:** today (function granularity) uncoverable
  instructions are swept into the wrapped region and counted as "protected". §5's
  Need/Residual split is what stops the region reports overstating coverage.

This is exactly why Track C precedes Track B: the coverability classifier is not a
side report, it is the **filter on the placement objective's input**.

### 5.2 Intraprocedural placement (dataflow, frequency-aware)
Standard two-pass formulation over MachineBasicBlocks:
- **Down-safety (anticipation):** DIT will be needed on every path from here.
- **Availability:** DIT=1 already established on every path to here.
- Insert `MSR DIT, #1` on the earliest edges where anticipated ∧ ¬available;
  insert `MSR DIT, #0` on edges leaving the "live" region (post-dominance of the
  last need). Use `MachineBlockFrequencyInfo` to break ties: never place a toggle
  inside a loop when the preheader/exit edges dominate/post-dominate the needs
  (hoist out of hot loops for free).
- Function-granularity remains the degenerate result when needs span the whole
  body — so this strictly generalizes the current behavior.
- The existing `TaintedRun`/merge-gap machinery reduces to the seed set; the gap
  parameter becomes irrelevant for DIT (dataflow subsumes gap merging).

### 5.3 Interprocedural protocol (fixes G1, delivers P1)
Extend `TaintSummaryInfo` (fixed-point already walks the TU call graph) with two
bits per function:
- `EntryDIT`: all *internal* call sites of F occur at program points where DIT=1
  is available (external/address-taken/indirect ⇒ false). **Not yet implemented.**
- `PreservesDIT` — **implemented**: F is not DIT-instrumented itself and every
  call it makes is direct to a preserving in-TU callee (externals/indirect ⇒
  false). Computed as a greatest fixed point after taint convergence in
  `TaintFixedPointIteration.cpp`; tail calls count as calls.
Placement rules:
- If `EntryDIT(F)`: omit F's entry enable **and all exit disables** (the caller
  owns the state) — zero toggles in interior functions of tainted chains. (Not
  yet implemented.)
- At a call site inside a DIT-live region: if `!PreservesDIT(callee)`, re-assert
  `MSR DIT, #1` after the call — **implemented** in `insertTaintDITSwitches`
  (re-asserts by default, elides when the callee's summary proves preservation).
- Disables move to the *outermost* frontier: functions whose callers don't need
  DIT. (Not yet implemented.)

### 5.4 Save/restore variant (P3, mixed/unknown call graphs)
For functions callable both with and without DIT (address-taken, exported):
`MRS xN, DIT` at entry / `MSR DIT, xN` before returns preserves the caller's
state exactly. Post-PEI needs a scratch register (register scavenger, or reserve
via spill slot like `AArch64SpeculationHardening` does). Only worth it where the
protocol bits come back unknown; default to the caller-side re-assert otherwise.

### 5.5 Diagnostics & reports
- **Implemented:** call-site escape report — `-taint-callsite-report=<file>`
  emits `ESCAPE external|indirect callee=<name> caller=<fn> bb=<n> [line=<l>]
  tainted-args|pointee-tainted-args` for secrets handed to uninstrumentable
  callees. Lit:
  `taint-analysis-callsite-report.mir`.
- Residual-hazard report (§5.1) — per instruction: opcode, reason
  (`non-DIT-op`, `tainted-address`, `tainted-branch`), source line. (Pending,
  with P1.)
- Extend `-taint-regions-output` to print DIT scopes (enable/disable points +
  frequency estimates) so `taint_region_distance.py`-style tooling can audit.

### Roadmap
| Phase | Work | Payoff |
|---|---|---|
| P0 ✅ done | Re-assert DIT=1 after call sites inside instrumented functions | closes G1 (soundness) |
| P0.5 ✅ done | `-taint-callsite-report` escape diagnostics; `PreservesDIT` summary bit + re-assert elision | G3 escapes visible; removes redundant P0 toggles |
| P1 | `isDITCoveredOpcode` + residual report | closes G2 (no false assurance) |
| P2 | Intraprocedural LCM placement with MBFI | P2/P3 perf, exact scopes |
| P3 | `EntryDIT` summary, entry/exit toggle elision, save/restore fallback | P1 perf, full G3 |

---

## 6. Evaluation plan

- **Static:** toggle count and expected dynamic toggles (Σ toggle-site freq ×
  MBFI) before/after, on `playground/firefox_convolve_int.c` and the Firefox
  TU set; residual-report coverage (every tainted instruction is either covered
  or reported).
- **Soundness check (automatable in lit):** a verifier pass asserting that on
  every CFG path each `Need` instruction is dominated by an enable with no
  intervening disable/`!PreservesDIT` call — this is cheap to check per-function
  and should gate P2/P3.
- **Dynamic:** needs FEAT_DIT hardware (Graviton3/Apple M/N2); `qemu-aarch64
  -cpu max` validates functional behavior only, not timing. Until then report
  static toggle metrics alongside the existing barrier-density stats.
