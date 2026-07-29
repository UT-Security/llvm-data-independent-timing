# Two soundness bugs found by asking "what happens when a secret is spilled?"

**Found and fixed 2026-07-27.** Both were in the oldest, most-exercised part of the
analysis. Both were found not by review but by *forcing the case and reading the debug
output* — the register allocator's rematerialization defeated the first attempt at a
repro, which is itself worth remembering.

## Bug 1 — `implicit-def` counted as a use (OVER-taint, unbounded)

`MachineInstr::uses()` starts after the **explicit** defs, so it still spans implicit
DEFS. On AArch64 a 32-bit result carries `implicit-def $xN`, and calls carry the `$lr`
clobber. `anyRegUseOfKind` walked `uses()` without an `isDef()` guard, so:

```
dead $w0 = MOVi32imm 1, implicit-def $x0     with $x0 tainted
  -> "set $w0 as tainted"      (should have been "clear")
```

An instruction that merely **overwrites** a tainted register looked like it **read** a
secret, and re-tainted its own defs. Taint could never leave a register once it entered:
a self-sustaining loop. Downstream, a genuinely public value materialized into `w0` and
passed to a call made `taintedCallArguments` report a secret argument, which set
`ExternalMemClobbered` and poisoned all memory.

`taintedCallArguments` already had the guard, with a comment naming the hazard — so the
hazard was known, and this one call site simply never got it.

**Fix:** skip `MO.isDef()` in `anyRegUseOfKind` (checking `isReg()` first — `isDef()`
asserts on non-register operands).
**Test:** `taint-analysis-implicit-def.mir`, with a positive control that taint still
flows through a genuine use operand.
**Measured on libsodium:** tainted instructions **4,354 -> 2,934 (-33%)**, UNCOVERED
261 -> 203, ESCAPE 37 -> 35. Notably it barely moved *which* functions are instrumented
(112 -> 109) and was **not** the source of the TOP flood (`modset-top` 571 -> 566).

## Bug 2 — narrowed reload of a spilled secret (UNDER-taint = leaked secret)

Stack/global cells are keyed `(FrameIndex, offset, size)` and the read path required an
**exact** key match:

```
STRXui $x0, %stack.0     ->  taint stack cell FI=0 off=0 sz=8
$w8 = LDRWui %stack.0    ->  "load from UNTAINTED stack cell"
                         ->  Total tainted regs: 0
```

Spill 8 secret bytes, reload the low 4 from the same slot at the same offset, and the
secret comes back **public** — it would then execute outside any DIT region. Any
narrowing or partially-overlapping reload escaped.

**Fix:** the READ path now tests for **overlap** with any tainted cell of the object
(`isTaintedStackCellOverlapping` and the pointee/global equivalents; size 0 remains the
unknown-extent sentinel covering the whole object). The **CLEAR path deliberately stays
exact-match** — widening a clear would drop taint that a partial public store did not
actually overwrite, which is the unsafe direction. A `contains()` fast path keeps the
common identical-shape access O(1).
**Test:** `taint-analysis-stack-partial-reload.mir`.

## What DOES work when a secret is spilled (verified, not assumed)

On a forced spill of 16 opaque secret values across a call:

- the spill store records a precise cell `(FI, off, size)`; the reload matches and
  re-taints the register (60 taint / 60 tainted-reload in that test);
- callee-saved register saves/restores of *public* incoming values correctly stay clean
  (120 clear / 120 untainted-reload) — the prologue CSR traffic is not confused with
  value spills;
- strong update works: storing a public value into a slot clears it.

This is the payoff of running post-regalloc, and it is the property CIO cites as its own
reason for choosing that level. It does work — the two bugs above were at the edges.

## Remaining coarseness (not a bug, but worth knowing)

After a call that sets `ExternalMemClobbered`, **every** stack reload in that function is
treated as secret regardless of cell — including reloads of public spilled values. Sound,
but coarse, and it is what makes the whole-frame `frameMayHoldSecret()` test in the
frame-address fallback so blunt. Making it per-object requires P1b (precise application
of `WritesSecretThroughArgPointee`); see `taint_frame_addr_fallback.md`.

## Method note

The first repro attempt used `v_i = s * const`, which the register allocator
**rematerialized** instead of spilling — the 4 "spill slots" in that build were just
callee-saved register saves, and the debug output ("store untainted") looked like a bug
in the spill path when it was actually correct behavior on public CSR values. Forcing
non-rematerializable values through `__asm__("" : "=r"(y) : "0"(x))` produced real value
spills and made both bugs visible. Check *what actually got spilled* before drawing a
conclusion from spill-slot debug output.
