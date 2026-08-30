# The tail-call DIT gap: why a tail call can't both protect and restore

**STATUS 2026-08-30: the accepted-cost framing here is SUPERSEDED by**
`docs/design/dit-abi.md`. DIT is now callee-saved, so a tail call out of an
instrumented function is an ABI violation rather than a tolerated leak. The fix is
global (`-ftaint-harden` implies `-fno-optimize-sibling-calls`), not the per-function
`disable-tail-calls` this file's §4 anticipated, so it no longer needs the two-pass
compile. `musttail` and `MachineOutlinerTailCall` survive the flag and are reported.
The mechanics below are still accurate; only the verdict changed.


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
- **The leak is reported, as of 2026-08-27.** `reportUnbalancedDITExits`
  (`TaintAnalysis.cpp`) runs after placement in *both* modes and writes a
  `DITLEAK`-class line into `-taint-callsite-report`:

  ```
  DITLEAK tailcall callee=secret_consumer caller=tail_caller bb=0 (DIT left set: a tail call has no epilogue to restore it)
  DITLEAK tailcall callee=<indirect> caller=indirect_tail_caller bb=0 (DIT left set: a tail call has no epilogue to restore it)
  ```

  It is a **diagnostic, not a gate** - leaving DIT set is the correct outcome
  here, so failing or falling back would reintroduce the under-taint §1
  removed. Functions that legitimately exit with DIT set are excluded:
  `AlwaysEnteredWithDIT` and `.dit` clones do not own the bit and may not clear
  it.

  Why the existing verifier could not catch this: it asks only *"does every Need
  run with DIT=1"*, and an enable with no matching clear is exactly what makes
  that pass, so the class is invisible to it by construction. `computeDITOnEntry`
  is equally blind - an intra-function dataflow that seeds the entry boundary Off
  and never models the state a function hands back. Test:
  `llvm/test/CodeGen/AArch64/taint-analysis-dit-exit-balance.mir`.

  The check also reports a second class, `DITLEAK return`, on `errs()` as well as
  to the file. That one is a placement bug, not an accepted cost: `needsDIT` is
  `isDITProtected(MI) || MI.isCall()` and a `RET` is neither, so a plain return
  is never a Need and the disable-before-return always fires for a function that
  owns DIT. Currently unreachable, which is the point of checking it.
- On serializing-DIT hardware (Apple M4, where `MSR DIT` is ~30 cyc — see
  `docs/results/dit-cost-model.md`) the leak is nearly free, since the cost there is the
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

**Both constraints above are confirmed upstream, and the fix has two implementations to
copy.** Arm SME's `PSTATE.SM`/`ZA`/`ZT0` are PSTATE mode bits with this exact bracketing
discipline, and both LLVM (`AArch64ISelLowering.cpp:9401`, commit `5fae000f3610`) and GCC
(`aarch64_function_ok_for_sibcall`) refuse tail-call optimization when a mode change must
be undone after the call - with a predicate that is our ownership rule, not a blanket ban.
Neither compiler attempts it after ISel/expand, which confirms the two-pass cost. See
`docs/research/tail-call-precedent.md` for the rule to copy, why the `not_tail_called`
attribute is insufficient (it misses indirect callees, which is our worst case), and the
literature position on accepting the leak instead.

## 5. Precision left on the table - FUNCTION MODE ONLY, and effectively unreachable

`emitFunctionGranularityDIT` is coarser than the ideal rule. It skips the clear before
**every** tail call; ideally it would clear before tail calls that do *not* pass a
secret, restoring state in that case.

**Region placement already implements the ideal rule, so on the shipped default there is
no gap here.** Its disable-before-return is guarded by `if (!NeedSet.count(&MI))`, and a
tail call that passes no secret is not a Need, so it gets the clear. Measured 2026-08-27
on a function whose block contains a secret `MADD` and whose exit is a tail call passing
only a public value:

```
REGION (default)                    FUNCTION
  msr DIT, #1                         msr DIT, #1
  madd x2, x0, x1, xzr                madd x2, x0, x1, xzr
  msr DIT, #0     <- restores
  b public_sink                       b public_sink
DITLEAK: 0                          DITLEAK: 1
```

That leaves the refinement worth having only where function granularity actually runs
under a region-always policy, which is **the per-function fallback**
(`fallbackToFunctionGranularity`) - measured to fire 0 times on the repro and on
`firefox_convolve_int`. It does *not* apply to the other path that reaches
`emitFunctionGranularityDIT`, namely a `!OwnsDIT` function (`AlwaysEnteredWithDIT` or a
`.dit` clone): those must not clear DIT on any exit path, tail call or not, so there is
nothing to refine.

**Conclusion: do not implement.** The reason originally given still holds -
`emitFunctionGranularityDIT` has no `TaintFacts`, and it is the fallback path, which runs
precisely when region placement could not prove coverage - but the decisive argument is
now that the gap is unreachable on the shipped configuration. Revisit only if a
function-placement workload shows `DITLEAK tailcall` lines that cost measurable cycles.

**What region placement is leaning on here.** Clearing DIT before a tail call is sound
only insofar as "this call passes no secret" is sound, and `needsDIT` decides that from
the call instruction's tainted register uses. A callee that reaches a secret another way
(the four channels in `docs/results/` on the mod-set gate: returned pointer into a secret
buffer, global read by a sibling with no call edge, inline asm, NEON register tuple) runs
unprotected. That is the general Scenario-B question - it applies equally to a non-tail
call in an Off block - not a tail-call issue.

## 6. Region placement

Region placement already leaves DIT set before the tail call — the safe direction —
so it did not have this hole. It did have its own tail-call bugs: `std::next(C)` on a
`TCRETURN` clobber inserted an `MSR` past the block terminator and aborted the
MachineVerifier on any ordinary `-O2` sibling call, and the disable-before-return
fired before a secret-passing tail call (both fixed in `0ef6cce5fe2e`; the re-assert
loop now skips terminator clobbers and the disable is skipped when the return is
itself a Need).

It has the same leak, now reported rather than silent (§3), and its coverage numbers
on tail-calling wrappers should be read with care: on `crypto_sign` its measured
99.6% suppression coverage came from the leak, not from precise placement.
