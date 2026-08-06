# The tail-call DIT gap: why a tail call can't both protect and restore

**Found 2026-08-05 on gem5, running hardened libsodium.** Whole-function placement
cleared `PSTATE.DIT` immediately *before* a tail call, so the callee — which is
exactly who receives the secret — ran with DIT=0. Fixed in
`emitFunctionGranularityDIT`; test `llvm/test/CodeGen/AArch64/taint-analysis-tailcall.mir`.

This doc exists because the **residual limitation is permanent**, not because the
bug was interesting. Read §3 before assuming placement restores DIT state.

## 1. The bug

`crypto_sign` is a one-line wrapper that tail-calls `crypto_sign_ed25519`, which
lives in another TU (`sign_ed25519.o`) and is therefore uninstrumented. Generated
code, verbatim from `libsodium.a`:

```
taintfn   crypto_sign:  msr DIT, #0x1 ; msr DIT, #0x0 ; b crypto_sign_ed25519
taint     crypto_sign:  msr DIT, #0x1 ;                 b crypto_sign_ed25519
```

On AArch64 a tail call (`TCRETURN*`) is **both** `isReturn()` and `isCall()`.
`emitFunctionGranularityDIT` clears DIT "before every return" and tested
`isReturn()` first, so the clear landed on the tail call — an **under-taint**, the
direction this project treats as a security bug. The entire signing operation ran
unprotected.

Measured on gem5 with comp-simp enabled (`ditSuppressed`, blanket ceiling 82,626):
`taintfn` suppressed **0**. Reproducer: `benchmarks/tailcall_dit_gap/` in the
gem5-DIT tree.

### Verification of the fix (all measured, not projected)

| Check | Before | After |
|---|---|---|
| `llvm/test/.../taint-analysis-*.mir` + TaintAnnotate | 24/24 (gap uncovered) | **25/25** |
| `crypto_sign` codegen | `msr DIT,#1 ; msr DIT,#0 ; b` | `msr DIT,#1 ; b` |
| `libsodium.a` taintfn `msr DIT` count | 149 | 142 (7 clears removed) |
| reproducer, `taintfn` ditSuppressed | 0 | **82,606** of 82,626 (99.98%) |
| ed25519, `taintfn` ditSuppressed | 0 | **127,445** of 128,298 (99.3%) |
| ed25519 checksums across variants | agree | agree |

`taint` (region) is unchanged by the fix at 191 `msr DIT` and 127,770 (99.6%) —
it never emitted the clear. Its coverage is still leak-derived; see §6.

## 2. Why there is no clean fix at the tail call

A tail call destroys the frame and control **never returns** to the caller. So
there is no instruction after it at which DIT could be restored. The choice is
binary and permanent for the rest of the caller's continuation:

| Choice | Callee | Caller's continuation |
|---|---|---|
| Clear before the branch | **unprotected** — the hole | DIT correctly restored |
| Leave DIT set | protected | **DIT leaks**, possibly forever |

Taint over-approximation is always the safe direction, so the fix leaves DIT set.

A tempting third option — clear here and let an instrumented callee re-assert at
its own entry — is **worse than it looks**: the callee's prologue may spill the
secret argument registers *before* its `MSR DIT, #1` executes, opening a window
where secret stores run unprotected. Leaving DIT set has no window.

## 3. The residual limitation (this is the part that persists)

**After a tail call from an instrumented function, `PSTATE.DIT` may remain set
indefinitely.**

- If the tail callee is in-TU and instrumented, it clears DIT before its own
  return, so the leak ends there. Harmless.
- If the tail callee is **external, uninstrumented, or indirect**, nothing ever
  clears DIT. The rest of the program runs in DIT mode — a performance cost with
  no upper bound, and a divergence from the "DIT is off outside protected
  regions" discipline the rest of the placement assumes.

This is a *cost*, not a hole, which is why it is accepted. But it means:

- **DIT state is not a reliable invariant across a tail call.** Do not write
  analyses or verifiers that assume an instrumented function restores DIT on
  every exit path. It does on `RET`; it does not on `TCRETURN`.
- **The leak is currently silent.** It should be surfaced in
  `-taint-callsite-report` as an ESCAPE-class line. Not yet implemented.
- On serializing-DIT hardware (Apple M4, where `MSR DIT` is ~30 cyc — see
  `taint_dit_cost_model.md`) the leak is nearly free, since the cost there is the
  toggle, not the dwell. On hardware where DIT disables value prediction, the
  dwell is what costs, and a leak past a hot uninstrumented callee could be
  expensive. No workload has been measured where this dominates.

## 4. The proper fix, and why it isn't implemented

To get protection **and** restoration, the tail call must not form: mark
secret-passing calls `notail` so a real `bl` + epilogue exists, at which point the
existing post-call re-assert logic applies.

That cannot be done where the bug lives. The taint pass runs **post-prologepilog**
— by then the frame is gone and cannot be resurrected. `notail` has to be decided
before instruction selection, which means a genuine two-pass compile: analyze,
then re-codegen from IR with the annotation. The 3-phase `-ftaint-harden` pipeline
cannot express this, because phase 3 resumes at `start-after=prologepilog`.

If this is ever attempted it should be flag-gated (`-taint-dit-tailcall=`) and
justified by a measurement showing the leak actually costs cycles.

## 5. Precision left on the table

The shipped fix is coarser than the ideal rule. It skips the clear before **every**
tail call; ideally it would clear before tail calls that do *not* pass a secret,
restoring state in that case.

The reason it doesn't: `emitFunctionGranularityDIT` has no `TaintFacts`. It is also
the fallback path (`fallbackToFunctionGranularity`), which runs precisely when
region placement could not prove coverage and has no facts to hand. Threading a
replay into it is possible but was not worth an untested precision refinement in a
security-relevant path. `needsDIT` already classifies a secret-passing call as a
need (Scenario B), so the information exists on the region path if this is ever
revisited.

## 6. Region placement

Region placement already leaves DIT set before the tail call — the safe direction —
so it did not have the hole. It has the same silent leak, and its coverage numbers
on tail-calling wrappers should be read with care: on `crypto_sign` its measured
99.6% suppression coverage came from the leak, not from precise placement.
