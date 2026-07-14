# DIT Barrier Placement: Current State, Gaps, and an Optimal-Placement Design

Status of the `-taint-barrier-mode=dit` backend, what the Arm spec lets us rely
on, where the current placement is unsound or wasteful, and a concrete design for
spec-aware optimal placement of `MSR DIT` mode switches.

---

## 1. What exists today

| Piece | Where |
|---|---|
| Mode flag `-taint-barrier-mode={isb,dit}` (`TaintInsertISB` stays the master switch) | `llvm/lib/CodeGen/TaintAnalysis.cpp` (cl::opt), enum in `llvm/include/llvm/CodeGen/TaintAnalysis.h` |
| Generic hook `insertTimingModeSwitch(MBB, MI, DL, Enable)` | `llvm/include/llvm/CodeGen/TargetInstrInfo.h`, default in `TargetInstrInfo.cpp` |
| AArch64 impl: `MSR DIT, #1/#0` via `MSRpstateImm4` (pstatefield encoding 26) | `llvm/lib/Target/AArch64/AArch64InstrInfo.cpp` |
| Placement policy (function granularity) | `insertTaintBarriers` in `TaintAnalysis.cpp` |
| Lit test | `llvm/test/CodeGen/AArch64/taint-analysis-dit.mir` |

**Current policy:** if a function contains ≥ 1 tainted run (`collectTaintedRuns`),
insert `MSR DIT, #1` as the very first instruction of the entry block and
`MSR DIT, #0` immediately before every `isReturn()` instruction (covers
`RET_ReallyLR` and `TCRETURN*` tail calls). Additionally (P0 fix for G1),
`MSR DIT, #1` is re-asserted immediately after every non-tail call site, since a
callee may clear DIT on its own exit. Untainted functions are untouched.
The tainted-run computation (value taint, pointee-tainted loads, address-sensitive
memory accesses, merged across gaps of `-taint-region-merge-gap` clean
instructions) is shared with ISB mode and unchanged.

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
*Status:* **fixed** — `insertTaintBarriers` re-asserts `MSR DIT, #1` immediately
after every non-tail call site inside instrumented functions (cheap, always
sound; lit-covered by `caller_fn` in `taint-analysis-dit.mir`). Eliding
provably-redundant re-asserts is the P3 summary work (§5.3).

### G2 (unsound in principle): tainted instructions the spec does not cover
The run collector treats all tainted instructions alike, but per §2.2 a tainted
`SDIV`/`UDIV` (or FP div/sqrt) is *not* protected by DIT=1. Silent false
assurance. Similarly, runs include **address-sensitive loads/stores**
(`isAddressSensitiveMemoryAccess`) and branches on tainted flags — DIT provides
no guarantee for either; those hazards are what the ISB mode (or software
rewriting) is for.
*Fix:* classify per-instruction coverability (§5, `isDITCoveredOpcode`) and emit
a diagnostic/report section for uncoverable tainted instructions instead of
counting them as protected.

### G3 (leak-adjacent) — **PARTIALLY ADDRESSED**: exceptional exits, unknown callees
Unwinds/`longjmp` out of an instrumented function leave DIT=1 in the unwinder and
beyond — *safe* direction (over-protection), only a perf leak (still open,
accepted). The **caller-side** hazard of external callees toggling DIT off is
covered by the unconditional/summary-gated after-call re-assert (P0 + elision).
What remains, now **diagnosed** instead of silent: secrets handed to callees the
analysis cannot instrument. `-taint-callsite-report=<file>` emits an `ESCAPE`
line for every call site passing tainted/pointee-tainted args to an external
declaration or through an indirect call (`BLR`) — including the sneaky case of
in-TU functions reached *only* indirectly, which never receive arg-taint
propagation and are never instrumented. Protection *inside* such callees is out
of placement's reach: annotate the target in its own TU, substitute a
constant-time implementation, or accept the reported risk.

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

### P2: whole-function dwell when taint is localized — **NOT A GAP (measured)**
This section used to hypothesize that dwell is near-free and ask for measurement
on FEAT_DIT hardware. **Measured 2026-07-14 on Apple M4** — the hypothesis was
right, and stronger than stated: **dwell cost is zero** (≤1% on every ALU,
multiply, load, store, pointer-chase and streaming kernel; whole-program DIT on
`firefox_convolve_int` is 0.968x, i.e. not a slowdown). A **toggle costs ~30
cycles** and fully serializes. Full data: `utils/taint_dit_cost_model.md`.

Consequences for this document:
- The objective is confirmed as **minimizing executed toggles** — §5's lazy-code-
  motion design is aimed at exactly the right thing.
- **Narrowing a region to the tainted instructions is a pessimization**, not an
  optimization: it can only add ~30-cycle toggles to save dwell that is free.
  Wherever this doc reads as "shrink the region", read "**coarsen and hoist the
  toggles out**" (§5's LCM does this naturally — it sinks enables and hoists
  disables to minimize *executed* toggles, not covered instructions).
- P1 below is therefore the **whole** performance story, not merely the dominant
  one.
- Caveat: one microarchitecture. Arm does not architecturally promise cheap DIT;
  re-run `playground/dit_bench/run.sh` on Neoverse V1/N2 or Graviton3 before
  generalizing. A core with real dwell cost puts this gap back.

### P3: one disable per return
Functions with many exit blocks execute at most one, so the *static* count is
harmless; but disables in cold exit blocks are pure code-size. Post-dominator
placement (§5) subsumes this.

---

## 5. Proposed design: spec-aware optimal placement

Treat "PSTATE.DIT == 1" as a dataflow fact and place toggles by lazy-code-motion
over the machine CFG, extended interprocedurally by the existing fixed-point
framework.

### 5.1 Instruction classifier (new target hook)
`AArch64InstrInfo::isDITCoveredOpcode(const MachineInstr &)` — returns whether
the opcode is in the Arm ARM DIT list. Start conservative: integer
data-processing minus `SDIV`/`UDIV`, moves, NEON/crypto per the list; loads and
stores are *covered for data, not address*. Drives two things:
- **Need set:** `Need(MI) = isTainted(MI) && isDITCoveredOpcode(MI)` — the
  instructions that must execute under DIT=1.
- **Residual report:** tainted instructions with `!isDITCoveredOpcode` (tainted
  divides, address-sensitive accesses, tainted-flag branches) go to a
  `-taint-dit-residual-output` report — visible, not silently "protected".

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
  `MSR DIT, #1` after the call — **implemented** in `insertTaintBarriers`
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
  callees. Mode-independent (ISB and DIT). Lit:
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
