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

### 5.6 Refined implementation design (Track B, 2026-07-16) — the plan of record

Design pass over the three in-tree mode-switch idioms (§5 named them). Base
structure is `RISCVInsertVSETVLI`'s (a boolean mode). **Two corrections found
while implementing increment (a) (2026-07-16), before any code — read these; they
change the formulation:**

**Correction 1 (soundness — the `Need` set).** `isDITProtected(call)` is *false*
(a call is not in the DIT covered set), so the original
`Need = isTainted && isDITProtected && classifyDITUncovered==null` would DROP
secret-passing calls — breaking Scenario B (the callee must run with DIT on to
inherit it). Corrected definition:
```
Need(MI) = isTaintedInstruction(MI, F) && (isDITProtected(MI) || MI.isCall())
```
This also (correctly) keeps a **secret-address load** in the Need set: its data
*value* is DIT-covered (the LVP channel) even though its address is not, so it
must run under DIT — the `classifyDITUncovered != null` test would have wrongly
excluded it and under-protected the exact value-timing channel the project
targets. (The load is *also* a residual for its address — an instruction can be
both a Need and a residual; the two are not mutually exclusive.)

**Correction 2 (objective — dwell is the live range; earliest ≠ minimal).** The
original "enable at *earliest* anticipated" is anticipation/down-safe placement,
which enables as early as `ANTIN` holds. But `ANTIN` is *true in a clean preamble
that flows into a need* (a need is coming on all paths), so earliest placement
**covers the public preamble** — the exact thing we narrow (convolve's
`entry → for.body.lr.ph → loop`: `ANTIN` true from `entry`). Loop-hoisting cannot
fix it (the enable is already *before* the loop). Root cause: **dwell IS the live
range of the `DIT==1` fact**, so minimizing dwell needs LCM's *lateness/latest*
(sink the enable down to just-before-need), which the "no live range" dismissal
missed. Corrected objective and staging:
- The dwell-minimal seed is the **minimal need-region** (just-before-first-need to
  just-after-last-need) — which the existing `collectTaintedRuns` already computes
  per block. Coarsen *upward* from there (loop-hoist + admission-merge), rather
  than start maximal (earliest) and fail to shrink.
- **Increment (a) = anticipation-coarse scaffolding**, deliberately: it builds the
  `ANTIN` backward lattice + availability + the emit + the verifier, degenerating
  to function granularity (byte-identical when whole-function tainted) and
  narrowing only the *trailing* clean epilogue (`¬ANTIN` blocks). It does NOT
  narrow the preamble — that is (b). This is sound and never worse than function
  granularity (coarse ⇒ toggles at coarse boundaries, never per-iteration), and
  the `ANTIN` lattice it builds is the first LCM pass, reused by (b).
- **Increment (b) = lateness + loop-hoist together** (they are coupled: you sink
  the enable to the latest safe point, which is the loop *preheader* for a
  loop-resident need, never *into* the loop). This is where the preamble is
  excluded and the real dwell win lands.
- (c) admission-merge, (d) interproc — unchanged.

**(b) as implemented (2026-07-16) — the `On(b)` set, simpler than a general
lateness dataflow but capturing the two wins directly.** Rather than the LCM
latest pass, (b) defines the DIT-on block set as
```
On(b) = HasNeed(b)  OR  (b is in a loop that (transitively) contains a Need)
```
computed from a locally-built `MachineLoopInfo` (`MachineDominatorTree MDT(MF);
MachineLoopInfo MLI(MDT)` — no MFAM plumbing). Two consequences:
- **Preamble excluded:** a clean preamble block (no need, not in a need-loop) is
  Off, so DIT is not enabled over it — the (a) gap closed.
- **Loop-hoist for free:** the *whole* outermost need-loop is On, so the Off→On
  boundary is the loop *preheader*, and the enable is placed at the preheader's
  end (executed once) — never at the loop header (which the backedge re-enters
  every iteration). Loop nests hoist to the outermost preheader because a need in
  an inner loop marks every enclosing loop as a need-loop.
Emit: enable at each Off→On boundary — hoisted to the preheader when the On-entry
is a natural loop header; when the header has no unique preheader (≥2 external
entry edges), placed at the end of *each external predecessor* (each entered
once, so still no per-iteration toggle and no whole-function fallback); disable at
each On→Off boundary (the Off side is a loop exit, outside the loop, entered once
— no hoist needed); re-assert after non-terminator clobbers; the (a) return/
tail-call rules unchanged. Insertions at a block start go PAST leading EH labels
/ CFI (`regionEntryInsertPt`) so they cannot displace a landing-pad `EH_LABEL`. A
Need in an **irreducible** cycle (which `MachineLoopInfo` does not model, so it
cannot be hoisted) triggers the graceful fallback to function granularity for
that function — detected by `blockInCycle` on a non-header On-entry. The
soundness verifier (unchanged) validates every emit and is the last-resort net.

**Not** byte-identical to (a) on loop-free code: (a)'s backward `ANTIN` marked a
clean preamble that all forward paths reach a need as anticipated (On), so it
enabled at function/region entry; (b)'s `On = HasNeed ∨ in-need-loop` leaves that
preamble Off and enables only at the need. (b) is strictly *narrower*/better. It
degenerates to function granularity only when the whole function is On (e.g.
whole-function taint). (a)'s remaining tests (`@whole`, `@narrows`,
`@tailcall_secret`) still pass because none has a clean loop-free preamble
*before* the need.

**Deferred to (d) (interproc):** a residual-only callee (needs=0) emits no DIT
switches in region mode and therefore *does* preserve DIT, but its `PreservesDIT`
summary bit is still `false` (it is computed from `functionHasTaintedRuns`, the
function-granularity notion), so callers emit a spurious post-call re-assert.
Sound, perf-only; fix belongs with the (d) `EntryDIT`/`PreservesDIT` rework.

Everything below is the base structure; apply the two corrections above to it.

(`AArch64SMEPeepholeOpt` gives the local pair-cancellation intuition for the
admission pass; `X86VZeroUpper` the any-path-forward-insert-at-first-hazard
template for disables.)

**Dataflow (two boolean fixed points over the machine CFG, seeded by one
`replayTaint` walk).** Local facts per block: `NEED(b)` = ∃ MI with
`isTaintedInstruction(MI,F) && isDITProtected(MI) && classifyDITUncovered(...)==
nullptr` (the §5.1 coverability gate — *reuse `classifyDITUncovered`, do not
re-derive*); `CLOBBER` = each call with `!PreservesDIT(callee)`.

- **Anticipation** (backward, AND-meet = down-safe): `ANTIN(b)=NEED(b) ∨ ANTOUT(b)`;
  `ANTOUT(b)=⋀_{s} ANTIN(s)` (a return/exit successor contributes false). No kill
  term — a clobber does not remove a *future* need.
- **Availability** (forward, AND-meet = domination): `AVIN(b)=⋀_{p} AVOUT(p)` (entry:
  `EntryDIT(F)`); `AVOUT(b)=enableAfterLastClobber(b) ∨ (AVIN(b) ∧ ¬hasClobber(b))`.
  GEN depends on where enables are placed ⇒ availability and the enable frontier
  are mutually dependent — resolve with VSETVLI's worklist (`computeIncomingVLVTYPE`
  pattern), recomputing to a per-function fixed point.
- **Frontier:** enable `MSR DIT #1` where `ANTIN(b) ∧ ¬AVOUT(p)` (earliest
  anticipated ∧ ¬available), or after a clobber that a Need follows; disable
  `MSR DIT #0` where `AVOUT(b) ∧ ¬ANTIN(s)` (latest available ∧ ¬anticipated) and
  before returns. **Degeneracy invariant (the increment-(a) test): whole-function
  taint ⇒ byte-identical to today's `insertTaintDITSwitches`.**

**Loop hoisting (the frequency win).** Down-safety deliberately won't hoist into a
maybe-zero-trip loop; hoisting is a separate frequency-gated relaxation. **Gate on
exact `MachineLoopInfo` membership (primary, PGO-independent), MBFI only to size
the trade:** for an enable in block `q` inside loop `L` with preheader `PH`, hoist
to `end(PH)` iff `freq(block(q)) > freq(PH)`; symmetrically sink in-loop disables
to `L`'s exit blocks (post-dominator frontier). Recurse outward over the loop
nest. This is what turns convolve's thousands-of-dynamic toggles into ~2 (§P4).

**Admission test (replaces `-taint-region-merge-gap`).** A *post-pass* (a
frequency-weighted cost compare is not a lattice meet), analogous to
`coalesceVSETVLIs`. For each DIT-off corridor between a dwell-motivated disable
`d` and enable `e` with no clobber inside: merge (drop `d`,`e`) iff
`60cyc·max(freq(d),freq(e)) ≥ Σ_{b∈corridor} freq(b)·|b|·c_dwell`. This is the
*derived* merge-gap: static "≤N clean instrs" → "toggle pair costs more than the
dwell saved," frequency built in (cold long gaps merge, hot short gaps split; the
P≈40-64 crossover falls out). **Never merge across a clobber-driven re-assert** —
tag dwell-motivated toggles at creation so the post-pass can tell them from
correctness toggles. `-taint-region-merge-gap` becomes a transitional override
(threshold→∞ recovers pre-admission behavior).

**Interprocedural.** `PreservesDIT` (built) = the *transparency* function of the
availability lattice: `TRANSP(call):=PreservesDIT(callee)`, so a `!PreservesDIT`
call is a CLOBBER and gap G1's re-assert folds into availability with zero new
machinery, now demand-driven (only when a Need follows). `EntryDIT` (new) =
internal ∧ ¬address-taken ∧ every call site direct in-TU ∧ DIT available at every
call site ⇒ F omits its entry enable and exit disables (caller owns the state; the
P1 zero-toggle interior). Because availability-at-call-site is a placement result
that feeds `AVIN(entry)`, `{placement, EntryDIT}` is a **coupled greatest fixed
point** — seed true, retract (monotone; retraction only adds toggles) when a
caller leaves DIT unavailable at a call site or F becomes externally reachable,
bounded like the existing `MaxIterations=100`. External/indirect ⇒ false (sound:
whatever the callee leaves is what the caller continues with).

**G4 domination — discharged structurally.** Availability's AND-over-preds meet
means `AVIN(b)=true` ⟺ DIT established and unclobbered on *every* incoming path =
the domination obligation, no block-layout assumptions. EH/landing-pad/cold-split
edges are ordinary CFG edges and participate in the meet automatically. Critical
edges that can't be realized: split if a real preheader/exit exists, else place at
`begin(b)` accepting extra dwell (over-approx = safe; post-PEI CFG is rigid so
this is the default).

**Soundness verifier (the hard gate at every increment).** A forward 1-bit replay
(enable→on, disable→off, `!PreservesDIT` call→off, AND-meet at joins) that
**asserts DIT-on at every Need MI**. Cheap, per-function, lit-runnable; a single
missed domination is a leaked secret, so it gates every stage.

**Staging** (all behind `-taint-dit-placement={function|region}`, default
`function` so the 14 lit tests and current codegen are untouched until opted in;
verifier green at every step):
- **(a)** Intraproc boolean dataflow (anticipation + availability + frontier), no
  hoist/merge. Test: whole-function-taint ⇒ byte-identical to `function` mode;
  uncoverable-only function ⇒ zero toggles.
- **(b)** Loop hoisting (`MachineLoopInfo`+MBFI). Test: convolve-shaped hot loop ⇒
  enable in preheader, `CHECK-NOT` in the loop body, nothing on the backedge.
- **(c)** Admission-test merging. Test: short-cold gap merges, long/hot gap splits;
  threshold→∞ reduces to (b).
- **(d)** Interproc `EntryDIT` + coupled fixed point. Test: internal tainted chain
  ⇒ interior callee has no entry enable/exit disable; Scenario-B assert stays
  silent; verifier confirms callee Needs still dominated by the caller's enable.

**Top risks:** post-PEI CFG rigidity (mitigate: existing boundaries + safe
in-block fallback); MBFI unreliability without PGO (mitigate: loop *membership* is
the correctness gate, MBFI only sizes the cost trade); availability/GEN coupling
non-convergence (mitigate: VSETVLI worklist pattern); the Need-gate must match
`classifyDITUncovered` exactly (reuse it; test interleaved coverable/uncoverable).

### Roadmap
| Phase | Work | Payoff |
|---|---|---|
| P0 ✅ done | Re-assert DIT=1 after call sites inside instrumented functions | closes G1 (soundness) |
| P0.5 ✅ done | `-taint-callsite-report` escape diagnostics; `PreservesDIT` summary bit + re-assert elision | G3 escapes visible; removes redundant P0 toggles |
| G2 ✅ done (Track C) | `isDITProtected` membership hook + `-taint-uncovered-report` (`classifyDITUncovered`) | closes G2 (no false assurance) |
| B(a) ✅ done | region placement scaffold behind `-taint-dit-placement=region`; Need set + soundness verifier + graceful fallback | machinery; narrows trailing epilogue |
| B(b) ✅ done | loop-aware `On(b)` placement: preamble excluded, enable hoisted to loop preheader / multi-entry pred edges; irreducible→fallback | the dwell win (convolve: 67-instr preamble now DIT-off, no per-iteration toggle) |
| **B(c) ← NEXT** | **admission test: merge Off corridors between On regions when `60·freq(toggle) ≥ Σ gap dwell` (MBFI-weighted); deprecate `-taint-region-merge-gap`. §5.6 base structure.** Purely a perf optimization — merging only extends coverage, so it CANNOT leak (verifier always passes); lower-risk than (a)/(b). On M4 (dwell≈0) it coarsens; on DIT-sensitive cores it stays narrow. Needs `MachineBlockFrequencyInfo` + a tunable `dwell_per_instr` cl::opt calibrated by the measured P≈40–64 crossover. | cost-model-driven placement |
| B(d) | `EntryDIT` summary (coupled greatest fixed point with placement), entry/exit toggle elision for internal tainted chains; fixes the deferred residual-only-callee `PreservesDIT` spurious re-assert | P1 interior-zero-toggle perf |

**Current impl state (2026-07-16):** all of the above through B(b)+review-fixes are
committed and pushed on `interproc_taint` (HEAD `2e585cc`). Region mode is behind
`-taint-dit-placement=region` (default `function` = shipped, untouched). 15 lit
tests pass (`taint-analysis-*.mir`); the soundness verifier + graceful fallback to
function granularity are the safety net under region mode. Reviews (workflow
`/code-review high`) run per increment have each caught real bugs (P0: 4 leaks;
B(a): tail-call crash; B(b): EH-label/irreducible/multi-entry) — all fixed.

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
