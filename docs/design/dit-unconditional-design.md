# The unconditional design: deleting the after-call re-assert

**Status 2026-08-27: PROPOSED, nothing implemented.** This is the design record for
removing the post-call `MSR DIT` re-assert, which is the largest remaining per-call
cost in the pass and the one `PreservesDIT` provably cannot reach. It supersedes the
`-taint-dit-preserve-abi` forward references in `TaintAnalysis.cpp` and
`taint-analysis-dit-reassert-report.mir`, which name a flag that has never existed.

Read `docs/design/dit-callee-ownership.md` first for Mode 1 (shipped) and the original
Mode 2 sketch, and `docs/research/mode-bit-precedent.md` for the prior art this design
now rests on - LLVM already emits this sequence for `PSTATE.SM`, and AAPCS64 already
specifies the contract. This document differs from that sketch in one substantive way: **the
caller emits nothing at all**, rather than a cheaper guarded re-assert. Getting there
required working out which DIT writes may be speculated on, which is §3 and is the
load-bearing part of the file.

## 1. What is being removed

`clobbersDIT` emits an enable after every call whose callee cannot be proven to leave
DIT alone:

```cpp
static bool calleeLeavesDITSet(const Function *Callee, const TaintSummaryInfo *TSI) {
  if (!TSI || !Callee)
    return false; // external or indirect: assume it clears
  const FunctionTaintSummary &S = TSI->getSummary(*Callee);
  return S.PreservesDIT || S.AlwaysEnteredWithDIT || isDITClone(Callee);
}
```

Three buckets, and `-taint-dit-reassert-report` already labels them:

| reason | fixable by per-callee analysis? |
|---|---|
| `clears-on-exit` (in-TU instrumented callee that owns DIT) | already elided when provable |
| `external` (a declaration, so another TU) | no - taint is TU-scoped |
| `indirect` (function-pointer call) | **no, not even in principle** |

On SQLCipher's crypto the report is four lines, of which the two that matter are the
per-block AES dispatch through `cipher_descriptor[]`, a table `register_cipher()`
writes at run time: **256 executions per 4 KB page**, each paying one ~30 cyc write.

## 2. The invariant

> **No instrumented function returns with `PSTATE.DIT` cleared relative to its entry
> state.**

One-directional deliberately. "Restores exactly" would be stronger than needed and
would exclude tail calls (§6).

Given that invariant, after *any* call:

- the callee was instrumented, so by the invariant DIT is not cleared; or
- the callee was not instrumented, so it executed no `MSR DIT` at all - DIT appears in
  no ABI (AAPCS64 has zero mentions of it, see `docs/research/tail-call-precedent.md`
  §1.2), so nothing else on the machine writes it.

Either way DIT is still set, **and the caller does not need to know which case holds.**
`calleeLeavesDITSet` becomes `return true;` for every callee, and the re-assert is
**deleted, not cheapened**: zero instructions at the call site, zero speculation
window, and it works through an indirect edge because it never asks who was called.

### Where the win actually is

Worth stating plainly because it is easy to get backwards. On SQLCipher the callee
(`rijndael_ecb_encrypt`) is **not ours to instrument** - it emits nothing today and
would emit nothing under this design. The entire win is deleting the *caller's*
re-assert: one 30 cyc write per 16-byte block becomes zero instructions. The
callee-side read and restore are paid only by functions the pass actually instruments,
which in that loop is none of them.

## 3. Which DIT writes may be guarded by a branch, and which may not

**This is the section to read before changing anything here.** A guard is a branch, and
a branch mispredicts.

### 3.1 Guarding an ENABLE is forbidden

```asm
        mrs  x9, DIT
        tbnz x9, #24, .Lskip     ; predicted TAKEN
        msr  DIT, #1             ; ...so this never executes
.Lskip: ...secret work...        ; runs speculatively with DIT genuinely OFF
```

If the predictor guesses "already on" and is wrong, the secret work executes in the
speculative window with DIT off, so value prediction, comp-simp and DMP tagging all run
on secret operands. The branch then resolves and squashes - but a squash undoes
architectural state, not predictor or timing footprints. **That is a leak, and it is the
Spectre-shaped one.**

The comparison that settles it: an *unconditional* `msr DIT, #1` has no such window.
Guarding the enable would trade a security property for cycles, which is the wrong
direction for this project ("a spurious barrier costs performance, a missing one costs
the secret").

The other mispredict direction is harmless: predict "not on", execute a redundant
enable, get squashed.

### 3.2 Guarding a CLEAR is safe

```asm
exit:   tbnz x9, #24, .Lskip     ; skip the clear if entered with DIT on
        msr  DIT, #0
.Lskip: ret
```

| mispredict | consequence | verdict |
|---|---|---|
| predicted skip, clear was needed | DIT stays on longer than intended | performance leak, the class §6 already accepts |
| predicted execute, clear was skippable | a speculative clear **cannot un-gate** | safe |

The second row depends on a specific existing gem5 property: the clear's publish is
deferred to immediately before `commitHead`, so a wrong-path clear never un-gates
anything. **That deferred publish is load-bearing for this design.** Anyone
"optimizing" it into an earlier publish breaks §3.2 silently.

### 3.3 The rule

**Never guard an enable. Only ever guard a clear.**

Two independent arguments produce this same constraint, which is a good sign it is real:

1. Speculation, above.
2. Read imprecision. If the entry read can ever answer "already on" when it was not,
   a guarded enable would skip and under-taint, whereas a guarded clear would only skip
   and leak. `TaintSummaryInfo.h:137` already states the static form of this for
   `AlwaysEnteredWithDIT`: eliding the entry enable "would be the unsafe direction and
   is deliberately not done here; that needs a must-analysis."

## 4. The sequence

```asm
entry:  mrs  x9, DIT           ; speculative, no side effect, 1.00 cyc on M5
        save x9                ; §5 - the unsolved part
        msr  DIT, #1           ; UNCONDITIONAL. Immediate form, renamed, no branch.
        ...body...
exit:   restore x9
        tbnz x9, #24, .Lskip   ; guarding a CLEAR, so safe per §3.2
        msr  DIT, #0
.Lskip: ret
```

Call sites emit nothing.

**The exit restore is BRANCHLESS in the leading design** (chosen 2026-08-27, pending the
gem5 answer in §7): `msr DIT, x9`, an unconditional restore of the entry value.

It has a structural safety property the guarded form has to argue for. At the exit DIT is
1 (the unconditional entry enable put it there) and x9 is 0 or 1, so the write is always
**a no-op or a clear, never an enable**. A stale or conservative gate in its shadow
therefore always errs toward over-suppression, and the §3.1 hazard cannot arise for it by
construction. No predictor to reason about.

That premise depends on §6.1: it holds only if the body did no interior narrowing, or if
every interior clear is guarded on x9.

The cost is a gem5 port. `MSR DIT, Xt` decodes to `Msr64`, which is
`IsSerializeAfter, IsNonSpeculative` and is **not** renamed, so it is the expensive form
there today. §7.1 argues the port is tractable; the effort estimate is out for review.

**Guarded-clear alternative**, retained as a tuning option: `tbnz x9, #24, .Lskip` before
an `msr DIT, #0`. Safe per §3.2, roughly 2x cheaper per call because it skips the write
entirely when entered DIT-on, and it needs **no gem5 work at all** (immediate form, already
renamed, already deferred-publish). Against it: the branch is data-dependent on the
*caller's* DIT state, so for a shared helper called from both inside and outside a secret
region it alternates, and a mispredict flush can cost more than the write it saves - and
the cases where the dynamic check is needed are correlated with the cases the static
`AlwaysEnteredWithDIT` rule could not prove, which is also where the entry state is least
stable. Every guard in one function tests the same x9, so they all skip or all fire
together; the variance is per-invocation, not per-guard.

The entry read is safe to speculate: it has no side effect on the mode, and a squashed
read has no committed effect.

## 5. Storage: the answer is a pre-RA virtual register

**Superseded twice. Read the whole section before acting on any earlier draft of it.**

**LLVM already solves this exact problem, for a PSTATE bit, on this target.** A
`__arm_streaming_compatible` function reads its incoming `PSTATE.SM` at entry, holds it
across the body, and restores it before returning. `AArch64ISelLowering.cpp:8897`:

```cpp
  if (Attrs.hasStreamingCompatibleInterface()) {
    SDValue EntryPStateSM =
        DAG.getNode(AArch64ISD::ENTRY_PSTATE_SM, DL,
                    DAG.getVTList(MVT::i64, MVT::Other), {Chain});
    // Copy the value to a virtual register, and save that in FuncInfo.
    Register EntryPStateSMReg =
        MF.getRegInfo().createVirtualRegister(&AArch64::GPR64RegClass);
    ...
    FuncInfo->setPStateSMReg(EntryPStateSMReg);
  }
```

lowered to a literal system-register read at `:3342` (`BuildMI(... AArch64::MRS ...)
.addImm(AArch64SysReg::SVCR)`). Verified by building it with this tree's own `llc`
(`-mattr=+sve,+sme`, a streaming-compatible body calling a normal callee):

```asm
	stp	x30, x19, [sp, #64]
	mrs	x19, SVCR
	tbnz	w19, #0, .LBB0_2
	smstart	sm
.LBB0_2:
	...
	tbnz	w19, #0, .LBB0_4
	smstop	sm
.LBB0_4:
	ldp	x30, x19, [sp, #64]
	ret
```

**Storage is a virtual register created during ISel.** Because it is live across a call,
register allocation assigns a callee-saved GPR (x19), and the ordinary PEI machinery
spills it and emits `.cfi_offset w19, -8` for free. With no intervening call, RA picks a
caller-saved register instead. That is strictly better than anything below: it costs a CSR
only when one is genuinely needed, it cannot fail, and CFI is correct by construction.

### 5.1 Why the two earlier proposals in this file were wrong

Both are recorded because they are plausible and someone will re-derive them.

**"Claim an unspilled CSR at PEI" - REJECT.** In-tree, PEI claims
(`UnspilledCSGPR` -> `ExtraCSSpill`) exist for *scratch*; nothing claims one to hold a
value live across a body. Three hazards:

- **"Unspilled" does not mean free.** `SavedRegs` is seeded from `isPhysRegModified`, so a
  CSR that is only *read* - a `swiftself` context in x20, `swiftasync` in x22 - passes the
  test. That was a real bug, fixed in `d78597ec08b9`: *"The code assumed that when saving
  an additional CSR register we would have a free register throughout the function. This
  was not true if this CSR register is also used to pass values as in the swiftself
  case."* The `isPhysRegUsed(ExtraCSSpill)` guard is the fix.
- **It competes with the register scavenger.** `AArch64FrameLowering.cpp` has exactly one
  `CreateSpillStackObject`/`addScavengingFrameIndex` pair, while `TagStoreEdit::emitLoop`
  already creates two post-RA virtual registers live simultaneously.
- **On Darwin the claim can be silently reverted.** `AArch64FrameLowering.cpp:2705`:
  if the claimed register cannot be paired for compact unwind,
  `SavedRegs.reset(UnspilledCSGPR); ExtraCSSpill = AArch64::NoRegister;`. A pass that
  still emitted `mrs x27, DIT` would clobber the caller's x27 with no spill. **Assert on
  the final `SavedRegs`, never on the request.**

**"A leaf function needs no storage" - FALSE.** x9-x15 survive *calls*, and a leaf
function makes none, but the register allocator may have used x9 for an ordinary value in
the body. x9 is free *at* entry and *at* exit, not *across* them. Proving one dead
function-wide needs a `LivePhysRegs` sweep that fails under any real register pressure.

**And a post-PEI stack push is a miscompile, not missing CFI.** PEI computed every frame
offset against a fixed SP, so `str x9, [sp, #-16]!` at entry shifts every SP-relative
reference in the body. Safe only under a frame pointer, which AArch64 does not guarantee.
No red zone to fall back on either.

### 5.2 The cost, and it is the reason this file still has an open question

The pre-RA vreg needs the value materialized **before register allocation**. This pass
runs on MIR reparsed *after* prologepilog, because the analysis needs real stack offsets -
a load-bearing constraint (`CLAUDE.md`, "Constraints and gotchas"). Those are incompatible
in one pass.

So the storage choice is really a scope choice:

| | storage | analysis stays post-PEI | cost |
|---|---|---|---|
| **B1** | pre-RA vreg, the LLVM SME pattern | **no** - needs a two-pass compile | cleanest, cannot fail |
| **B2** | frame slot created in a late pass, the GCC pattern (`TARGET_USE_LATE_PROLOGUE_EPILOGUE` + a hard-FP-relative slot) | yes | one L1-hot load at exit, on the dependency path of a commit-blocking write |

**B1 shares its blocker with `dit-tailcall-gap.md` §4.** If the two-pass compile is built
for one, the other is nearly free. That is the strongest argument for building it, and it
is a scope decision rather than a technical one.

## 6. Interaction with tail calls: the invariant survives

An instrumented function that exits via a tail call has no epilogue, so it cannot
restore. It therefore leaves DIT set if it was entered with DIT off.

**That violates "restores exactly" but satisfies the §2 invariant**, which is why §2 is
worded one-directionally: the function returns with DIT *set*, not cleared, so a caller
that dropped its re-assert is still sound. The cost is the same unbounded leak
`dit-tailcall-gap.md` §3 documents, now reported as a `DITLEAK tailcall` line.

Consequence: **this design and the `notail` fix are independent for soundness.** Mode 2
does not require `notail`, and `notail` would improve this design only by converting
leaks into restores. They are *not* independent for effort: `notail` and §5's B1 share
the two-pass compile.

## 6.1 Interaction with region narrowing: this one is a real constraint

Region placement narrows by *clearing* DIT around clean stretches. An interior clear is
illegal when this frame does not own DIT - which is exactly why `insertTaintDITSwitches`
routes `!OwnsDIT` functions past the region emitter today: *"every one of those clears
would strip the caller's protection while the caller's secret is live in the frame."*

Under a **dynamic** entry read, ownership is not known at compile time. Two ways out:

1. **Guard every interior clear on x9.** Each is a guarded *clear*, so safe per §3.2, and
   the matching re-enable after the clean stretch stays unconditional, which is safe
   regardless of ownership because an enable only adds suppression. Keeps narrowing.
2. **Force whole-function coverage** on any function using the dynamic mechanism. Simpler,
   gives up narrowing - which on the compression-round shapes measured 2026-08-27 costs
   nothing, since region already degenerates to whole-function coverage there
   (`docs/results/sqlcipher.md`).

Option 1 also restores §4's premise: if x9 is 1, no interior clear fires, so DIT is 1 at
the exit and the branchless restore really is a no-op-or-clear. **The two properties are
linked, not independent.**

This is the constraint most likely to be discovered late, as a soundness-verifier failure
long after the design was settled.

## 7. gem5: split the gate from the value

> **7.1 OPEN, out for review 2026-08-27: can `Msr64` be made non-serializing too?**
> The branchless restore in §4 emits `MSR DIT, Xt`, which decodes to `Msr64`
> (`aarch64.isa:457`, body `MiscDest_ud = XOp1`, no `DitCC` operand, reaching
> `dit_reg::Dit` via `ISA::setMiscReg`) and carries `IsSerializeAfter, IsNonSpeculative`
> (`data64.isa:394-396`). The argument that it is portable: the register value is unknown
> at decode and rename but known at **execute**, and neither consumer needs it earlier -
> the `DitCC` gate needs no value at all (conservative 1 at rename is correct for both
> directions), and the architectural value is needed only at the deferred publish before
> `commitHead`, which is after execute by construction. So there is no window where the
> value is needed but unknown.
>
> The doc's warning against "dropping those flags for performance" is about *removing*
> protection, not about porting the existing mechanism to a second opcode. Note also that
> `check_tag_set.py` cannot see the `setMiscReg` path, so this edit would pass the script
> unchanged - an argument for giving `Msr64` a real `DitCC` destination instead.
>
> **The answer feeds back into §5.** If `Msr64` stays commit-blocking, a spill-slot load
> lands directly ahead of it and B1 (register-resident) is strongly preferred. If the port
> makes it cheap, B2's exit load matters less and the two-pass question relaxes.


The design is measurable on native M5 today (`MRS DIT` = 1.00 cyc, measured 2026-08-08
*with a data dependency forced on the result*, so not an OoO artifact). It is **not**
measurable in the gem5 renamed configuration, and that is a modelling limit rather than
a hardware one.

In gem5 `MRS x, DIT` decodes to `Mrs64`, which is `IsSerializeBefore`: it drains and
reads committed state. And the renamed `DitCC` cannot substitute, because it is **not a
value, it is a suppression gate** - any `msr DIT, #imm` publishes 1 at rename, including
a write of 0, "because rename cannot know the live value"
(`gem5-DIT/docs/dit/design/dit-data-independent-timing.md` §9.9). Reading it would
answer "on" in the shadow of any DIT write.

The fix is to stop conflating two objects:

| | purpose | semantics | speculation |
|---|---|---|---|
| `DitCC` (exists) | suppression gate | conservative **1** on any DIT write | publish at rename, unchanged |
| `DitVal` (new) | architectural value | **exact** 0/1 | ordinary renamed register |

`MRS DIT` then reads `DitVal` through the rename map: one cycle, speculative,
squash-safe for free. Gating semantics are untouched, so every existing measurement
stays valid.

Two requirements:

1. **Every writer must maintain `DitVal`**, including those that reach DIT only through
   `ISA::setMiscReg`: `Msr64` (the register form), `Eret`/`Eret64`/`Eretaa`/`Eretab`,
   `MsrCpsrImm`/`MsrCpsrReg`. They are already `IsSerializeAfter, IsNonSpeculative` and
   are rare (exception returns, not hot code), so writing `DitVal` at commit costs
   nothing. Miss one and the mirror goes stale after any interrupt.
2. **`util/dit/check_tag_set.py` cannot verify this.** It detects writers by matching
   `setDestRegIdx(...ditRegClass[dit_reg::Dit])` in the class body, so it never sees the
   `setMiscReg` path - it would print the same writer set and pass. Hand-check required.

Behind an opt-in `BaseO3CPU` param, per the pattern the non-spec-stall note already
set: gate it so every existing result stays reproducible and the default does not move.

## 8. What has to be measured

0. **The published per-call figures assume an optimization §3.1 FORBIDS - do not quote
   them for this design.** `docs/results/dit-cost-model.md`'s 1.03 cyc row is
   `mrs` + `tbnz` with **no `MSR` executing at all**, which requires skipping the entry
   enable as well as the exit clear. §3.1 forbids guarding the enable. Derived safe
   ceilings, per call, for a callee the pass instruments:

   | | per call |
   |---|---|
   | today (3x `MSR`, measured) | 90.67 |
   | guarded clear + unconditional enable | `mrs` 1.00 + `msr #1` 30.34 + skip ~ **31.4** |
   | fully branchless (`msr DIT, Xt` restore) | `mrs` 1.00 + `msr #1` 30.34 + `msr Xt` ~ **62** |

   **~2.9x, not 45x.** These are arithmetic from the existing measurements, not new
   measurements. Note this table is about *instrumented* callees only: in the case that
   actually costs (SQLCipher's 256 indirect dispatches per page) the callee is not ours
   to instrument and emits nothing either way, so the gain there is the deleted
   caller-side re-assert - one ~30 cyc write per 16-byte block becomes zero
   instructions, identical for both variants.

1. **Does a redundant enable cost full price?** §4 makes the entry enable
   unconditional, so a function entered with DIT already on pays an `msr DIT, #1` that
   changes nothing. `docs/results/dit-cost-model.md`'s 30.34 cyc was a state-changing
   write. If a redundant write is cheap this design is close to free; if it is full
   price, the entry enable becomes the dominant term and §3.1 needs revisiting - most
   likely by asking for an architectural "set DIT, no-op if set" with no skip path to
   mispredict, rather than by guarding it.
2. **Price the removal before building it.** `-taint-dit-reassert-report` already emits
   exactly the site list this design deletes, split by `indirect` / `external` /
   `clears-on-exit`. Run it on SQLCipher and libsodium: the first two buckets are what
   §2 recovers, and `clears-on-exit` is the part that was already reachable.
3. **Native M5 for the performance claim, gem5 serializing for coverage only.** Do not
   quote a renamed-model number until `DitVal` exists; it would measure the model's
   safety margin, not the design.
4. **The DIT=1 gate on any benchmark.** `dit-cost-model.md` records that a first
   attempt measured the guarded clear with DIT off, the `tbnz` fell through, the 30-cyc
   write executed, and the ownership path read as 52 cycles - the opposite conclusion.
   Assert and print DIT=1 during the run.

## 9. Residual limitations

- **Version skew.** An object compiled by a pre-this-design build of the pass clears
  DIT on exit and breaks §2. Within one build that cannot happen; mixing hardened
  objects across pass versions would need the invariant treated as an ABI promise.
- **Hand-written asm that writes DIT.** Outside the pass's view entirely. Apple's
  corecrypto already follows a compatible discipline (it reads DIT and only clears if it
  enabled), so the convention is de facto shared rather than novel, but it is not
  enforceable.
- **Inline asm** is not `isCall()`, so the pass cannot see `asm volatile ::: "memory"`
  at all - the same pre-existing gap the mod-set gate has.
