# The PSTATE.DIT calling convention

**Status 2026-08-30: PROPOSED. This file is the contract; everything else is a
consequence of it.** `dit-unconditional-design.md` gives the instruction sequence,
`dit-callee-ownership.md` gives the shipped static approximation, and
`docs/research/mode-bit-precedent.md` gives the prior art. Read this one first: both of
those are implementations of a convention that had never been written down.

## 0. Why this comes first

Every open cost item in the project is a consequence of an unstated ABI decision.
Today the pass behaves as if **DIT were caller-saved**: a callee may do anything to it,
so the caller re-asserts after any call it cannot prove harmless. Nobody chose that;
it is the default you land on when the question is never asked. It is also the entire
bill:

- 94.7% of full-LTO switches are after-call re-asserts (116,611 sets, ~110k of them
  re-asserts). Measured 2026-08-30.
- Deleting them is worth **-16.89% on LTO CoinSelection** (25/25) and **-3.51% on
  non-LTO SignTransactionECDSA** (23/30). Both are upper bounds; see
  `session-2026-08-30-handoff.md` §4.
- The four re-assert reasons (`indirect`, `external`, `clears-on-exit`,
  `propagates-unresolvable`) are four symptoms of the same missing contract.

**A convention removes all four classes by construction**, with no whole-program
analysis, no LTO, and no user annotation.

**Decided 2026-08-30: DIT is callee-saved.** A function returns DIT as it found it.

## 1. The contract

**DIT is callee-saved.** Stated in two layers, because the guarantee a caller may rely
on and the obligation an instrumented callee must discharge are not the same statement.

Writing `d_in` for DIT at entry and `d_out` at return, with `1 > 0`:

> **OBLIGATION (binds every instrumented callee).** At every exit under the function's
> own control, `d_out == d_in`. A function returns DIT exactly as it found it.
>
> **GUARANTEE (what a caller may rely on, unconditionally).** `d_out >= d_in`. DIT is
> never returned lower than it was at entry.

The obligation is the ABI. The guarantee is what still holds at the exits a function
does not control (§2), and it is the property that lets the caller delete its re-assert.

Because the obligation implies the guarantee, a caller never needs to know which case it
is in. Note also that the two coincide exactly when `d_in = 1`: a function entered with
DIT on must return with DIT on under either reading. They differ only for a function
entered with DIT off that turns it on, and there the obligation requires it to go back
off rather than leaking DIT into its caller.

### 1.1 Obligations

| party | obligation |
|---|---|
| **Caller** | none. Guarantees nothing about `d_in`, requires nothing after the call, and **emits no DIT instruction at any call site**. This is the whole win. |
| **Callee that never writes DIT** | none. Vacuously compliant, `d_out == d_in` trivially. This is every function produced by every other compiler. |
| **Callee that raises DIT** | read `d_in` at entry, restore it at every normal return. |
| **Instrumented function with a tail call** | the tail call must be disabled (§2.1), because a tail call is an exit with no epilogue and restoring is impossible there. |
| **Exits outside the function's control** (`_Unwind_Resume`, `longjmp`) | fall back to the guarantee: DIT is left set, never cleared. See §2.2. |
| **Interior narrowing** (region placement) | may clear only under a guard on `d_in`. See `dit-unconditional-design.md` §6.1. |

### 1.2 What each side may assume

- A callee may assume **nothing** about `d_in`. If it raises DIT, it reads.
- A caller may assume `d_out >= d_in` unconditionally, and `d_out == d_in` from any
  function that returns normally.

### 1.3 Failure direction

Every residual imprecision under this contract leaves DIT **on** longer than intended.
That is a dwell cost, never an exposure. Both layers fail in the safe direction, which
preserves the project's standing invariant that over-approximation is the safe way to be
wrong.

### 1.4 Precedent

This is the ordinary treatment of a mode or control bit, not a novel convention:

- **x86-64 psABI §3.2.1**: MXCSR *control* bits callee-saved, status bits caller-saved;
  the x87 control word callee-saved.
- **AAPCS64 PSTATE.SM**, streaming-compatible interface: read the incoming value, hold
  it across the body, restore before returning. LLVM emits exactly this sequence
  (`AArch64ISelLowering.cpp:8897`), and it is the pattern §8 borrows for storage.

Both are the *restore-the-entry-value* shape rather than the *fixed-value-at-boundary*
shape used for FPCR.NEP and the x86 direction flag. We depart from how AAPCS64 treats
mode bits generally (FPCR is a "global register", neither saved nor destroyed) and adopt
how it treats SME state specifically. See `mode-bit-precedent.md` §5.

## 2. The three exits that cannot run an epilogue

Callee-saved is the right contract and it is not free: three exits destroy the frame
without running our epilogue, so the obligation cannot be discharged at them.

### 2.1 Tail call: disable tail calls globally

A tail call has no epilogue, so an instrumented function that raised DIT and exits by
tail call leaks it into its caller's caller. Refusing the tail-call optimization is the
fix, and it has direct precedent: both LLVM and GCC decline to tail-call out of a
function that must restore PSTATE.SM (`mode-bit-precedent.md` §2.4).

**DECIDED 2026-08-30: disable tail calls for the whole translation unit, not
per-function** - and gate it on the ABI, not on hardening. `-ftaint-dit-abi`
implies `-fno-optimize-sibling-calls`; `-ftaint-harden` alone must NOT.

**Why the gating matters, found in review after shipping it the wrong way.**
`disable-tail-calls` is honoured by `TailRecursionElimination.cpp` as well as by
ISel. Applying it whenever hardening is on therefore turns tail RECURSION into
O(n) stack frames in every function of the TU, tainted or not - a stack-overflow
hazard, paid even when the ABI that needs it is switched off, and not overridable
with `-foptimize-sibling-calls`. Measured: a tail-recursive function that plain
`-O2` reduces to a closed form with no call at all emitted `bl` plus a frame under
`-ftaint-harden`.

The per-function form is the obvious design and it is not available to us. Setting
`disable-tail-calls` on *only the instrumented functions* requires knowing which
functions get instrumented, which is known only after the MIR analysis, which runs after
ISel has already formed the tail calls. That is the two-pass compile. The global form
needs no analysis at all, so **it decouples this ABI from the two-pass compile
entirely** - which was the single largest blocker on the critical path.

Mechanics, verified in tree:

- `-fno-optimize-sibling-calls` sets `CodeGenOpts.DisableTailCalls`
  (`CodeGenOptions.def:81`), which adds `"disable-tail-calls"="true"` to every function
  (`CGCall.cpp:2661`). The attribute is IR-level, so it survives into LTO.
- `SelectionDAGBuilder::canTailCall` honors it (`SelectionDAGBuilder.cpp:9073`).
- `llc` has the equivalent `-disable-tail-calls` (`CommandFlags.cpp:346`) for the
  wrapper flow.

**The cost is paid by the whole TU, not just instrumented functions.** That is the
deliberate trade: a simple, analysis-free mechanism in exchange for slower non-secret
code and deeper stacks. It should be measured, not assumed, and the old +27% static
switch figure (414 -> 524) was taken on the per-function form under the caller-saved
design and does not transfer.

#### 2.1.0 The residual cost, now confined to ABI builds

Disabling tail calls TU-wide also disables tail-RECURSION elimination, so a
tail-recursive function in an ABI build gets O(n) stack frames where plain `-O2`
would have produced a loop or a closed form. Measured: `rec(secret, n, acc)`
compiles to no call and no frame at `-O2`, and to `bl` plus a frame under
`-ftaint-dit-abi`.

That is a real limitation of the ABI, not a bug - it is the price of "no analysis
needed", and the alternative is per-function marking, which needs the two-pass
compile this design exists to avoid. It is acceptable only because it is now
**opt-in**: gating it on `-ftaint-harden` instead made every hardened build pay a
stack-overflow hazard for an ABI that was switched off.

#### 2.1.1 Two tail calls survive the flag

Both are real and neither is hypothetical.

1. **`musttail` bypasses it by construction.** `SelectionDAGBuilder.cpp:9073` reads
   `if (!isMustTailCall && Caller->getFnAttribute("disable-tail-calls")...)`. A
   guaranteed tail call is a correctness requirement, so it must be honored, and the
   frame is genuinely gone. Nothing can restore DIT there.
2. **The MachineOutliner can create new ones after our pass.**
   `MachineOutlinerTailCall` (`AArch64InstrInfo.cpp:10142`, selected at `:10453`) emits
   an outlined function that exits with a branch. The outliner runs downstream of the
   taint pass, so the flag cannot reach it.

**Both are already detectable with the reporting we have.** The check is not "was a tail
call requested" but "does an instrumented function still end in a tail-call return",
which is exactly what `DITLEAK tailcall` reports today.

**Status change:** `DITLEAK tailcall` was an *accepted cost* under the caller-saved
design (`CLAUDE.md`, `dit-tailcall-gap.md`). Under this ABI it is **an ABI violation to
audit, and the line should normally be absent.** A non-empty `DITLEAK tailcall` set in a
build compiled with the flag means `musttail` or the outliner, and each occurrence needs
a decision.

### 2.2 EH unwind and longjmp: fall back to the guarantee, and report them

`_Unwind_Resume` destroys the frame without running our epilogue, and `longjmp` restores
no PSTATE bit (DIT is not in `jmp_buf`). Neither can be fixed from inside the function,
and unlike the tail call there is no flag that removes them: they are language features,
not optimizations.

Both leave DIT **set**, so both satisfy the guarantee and neither can strip a caller's
protection. The residual is dwell, bounded in practice because the next enclosing
instrumented frame restores its own entry value on the way out. An unwind that passes
through nothing but uninstrumented frames propagates further, which is the honest limit.

**DECIDED 2026-08-30: report these rather than attempt to fix them.** They join the
existing audit family (`ESCAPE`, `DITLEAK`, `REASSERT`, `FRAMEREF`) as a new `NONLOCAL`
line, so an auditor gets a list of the exact sites where the obligation degrades to the
guarantee instead of an unquantified caveat in a design document.

Proposed format, matching the existing reports:

```
NONLOCAL unwind    callee=g caller=F bb=7  line=142 (DIT left set: _Unwind_Resume skips the epilogue)
NONLOCAL setjmp    callee=_setjmp caller=F bb=3 line=88 (DIT left set: longjmp skips the epilogue)
NONLOCAL musttail  callee=g caller=F bb=0  line=12  (musttail bypasses -fno-optimize-sibling-calls)
```

Detection, all available at the MIR level where the pass runs:

| kind | marker |
|---|---|
| `unwind` | the call's block has an EH successor, i.e. the function has landing pads and this call can throw |
| `setjmp` | the call site carries `returns_twice`, or is `Intrinsic::eh_sjlj_setjmp` |
| `musttail` | a `TCRETURN*` surviving in an instrumented function, which is the `DITLEAK tailcall` check |

New report file, `-taint-nonlocal-report=<file>`, following `deriveReportPath` /
`openTaintReport` like every other report. Emitting it is the one piece of new
machinery this ABI requires beyond the entry read and exit restore.

### 2.3 What the layering buys

Writing only "restore exactly" would make all three of these ABI violations requiring
their own mechanism. Writing only "never lower" would give up the dwell property that
motivates the whole pass. Stating both, with the guarantee as the floor, lets a caller
reason unconditionally while the obligation still pins normal returns.

## 3. Why uninstrumented code is already compliant

This is the observation that makes the convention deployable, and it inverts a rule the
pass ships today.

**Nothing writes PSTATE.DIT.** From `mode-bit-precedent.md` §5: zero mentions across
AAPCS64's 3,635 lines, zero across all 412 abi-aa issue and PR records, zero in
aadwarf64, one in ACLE (a feature-name table), zero in BoringSSL and libsodium, and no
`insertTimingModeSwitch` at the LLVM merge-base. DIT is neither caller-saved nor
callee-saved because no ABI has ever mentioned it.

Therefore an arbitrary external or indirect callee satisfies `d_out = d_in` **vacuously**.

**Consequence, and it is a correction rather than a refinement:** the current rule that
`calleeLeavesDITSet` returns false for an external declaration or an indirect target is
not a conservative approximation. It is a false belief about the world, and it is the
sole source of the `external` and `indirect` re-assert classes.

The functions that genuinely can lower DIT are the ones doing hardening, and there are
four known: Apple `timingsafe_enable_if_supported` / `_restore_if_supported` (shipping,
macOS 15.2+), corecrypto, Go `crypto/subtle.WithDataIndependentTiming`, and OpenSSL
PR #28764. Three already implement the callee half by hand, and Go's is explicitly
bidirectional at the cgo boundary. The convention proposed here is the one the ecosystem
is independently converging on.

## 4. Where the convention does not reach

abi-aa #405, Richard Earnshaw (Arm): the AAPCS defines behavior at public **conforming**
interfaces, and there is no requirement that all interfaces conform "if there is
agreement between all related parties." Peter Smith, same thread: a PLT stub, lazy
binding, or a `--wrap` interposed wrapper can run code between caller and callee.

So this is a **private convention**, legitimate without amending AAPCS64, and
correspondingly:

- the whole-function fallback stays for edges we do not control;
- the `ESCAPE` audit stays;
- the claim is soundness against a hardened build plus arbitrary non-DIT-writing code.
  It is **not** a claim about hostile code, and DIT is not a security boundary against
  code that deliberately clears it.

## 5. Cost model, MEASURED

Full results and the decision: `docs/results/dit-abi-measured.md`.

Cost moves from **O(call sites)** to **O(instrumented functions)**: the after-call
re-asserts go, and a carrier read, a frame slot and an exit restore arrive. So the
ABI wins exactly where switches-per-instrumented-function is high.

| arm | switches | instrumented fns | ratio | ABI vs baseline |
|---|---|---|---|---|
| non-LTO | 95 -> **57** (-40%) | 16 | 5.9 | CoinSel +0.08%, Sign -0.05% |
| full LTO | 127,744 -> **15,462** (-87.9%) | 2,498 | 51.1 | CoinSel **-5.40%** (25/25), Sign **-8.52%** (27/30) |

**CORRECTION 2026-08-31: that ratio is NOT the predictor.** It was a proxy that
happened to correlate, and a libsodium f-sweep falsified it - at a ratio of 5.5,
essentially Bitcoin's, the ABI is worth **5.3 points** at high secret fraction and
is what makes selective placement beat blanket there.

The mechanism is **re-asserts EXECUTED per unit of work**, since a re-assert is
paid per executed call site rather than per call site in the binary. LTO scores
high on the static ratio because merging multiplies executed call sites, which is
why the proxy held there and broke on libsodium. To predict a new workload, ask
how often control crosses an instrumented call boundary. Full data:
`docs/results/dit-abi-measured.md` §3.

The sound build lands within 1.2% of the unsound upper bound (15,272), because
under LTO the carrier is nearly free in switch terms: the entry read is `MRS`, not
a mode switch, and the guarded clear replaces the exit clear one for one.

**The default stays OFF**, but the reason is narrower than it was. Enable
`-ftaint-dit-abi` where control crosses instrumented call boundaries often: a high
secret fraction, a high call rate into hardened code, or LTO. Real applications
sit near 1-2% secret fraction, where it measures neutral, which is why the default
is off - a judgement about typical workloads, not a claim that it does not help.

## 6. Relationship to the annotation proposal

If non-lowering is the **default** for everything, there is nothing left for a user to
annotate in the `external` and `indirect` classes, which is where a side-file annotation
scheme was aimed. The ABI subsumes it.

ACLE already supplies the vocabulary: `__arm_in`, `__arm_out`, `__arm_inout`, and
`__arm_preserves` ("the callee does not read the incoming state and returns with the
state unchanged"). **The one-line statement of this proposal is: make
`__arm_preserves("dit")` the universal default.**

The residue an annotation would still be useful for is much smaller and more defensible:
indirect target sets, declassification, and callees that deliberately lower DIT.

## 7. OPEN, and these are decisions rather than research

1. ~~**Restore form.**~~ **DECIDED 2026-08-30: GUARDED CLEAR.** `tbnz x19, #24, .skip` /
   `msr DIT, #0`. Settled by Apple's own shipping implementation, not by our benchmark.

   ### The decisive evidence: Apple ships exactly this sequence

   `/usr/lib/system/libsystem_platform.dylib`, public API since macOS 15.2, used by
   corecrypto:

   ```asm
   _timingsafe_enable_if_supported:
           mrs   x8, DIT
           ubfx  x0, x8, #24, #1        ; entry state -> opaque token
           msr   DIT, #0x1              ; UNCONDITIONAL enable
           sb                           ; speculation barrier
           ret

   _timingsafe_restore_if_supported:
           tbnz  w0, #0x0, .Lskip       ; token says "was on" -> skip the clear
           msr   DIT, #0x0
   .Lskip: ret
   ```

   The restore **is** our guarded clear. The silicon vendor guards a DIT clear with a
   branch in a shipping security API, which is as close to authoritative as we get short
   of doing the microarchitectural experiment ourselves. It also independently confirms
   both halves of `dit-unconditional-design.md` §3: **unconditional enable, guarded
   clear.**

   ### Performance, for the record

   Harness `playground/dit_bench/dit_exitform.{S,c}`, M5, best of 11, cycles:

   | pattern | branchless | guarded | branch-only control |
   |---|---|---|---|
   | entered DIT=0 (clear executes) | 34.45 | 34.45 | 0.80 |
   | entered DIT=1 (clear skipped) | 34.43 | **-0.01** | -0.02 |
   | alternating (predictable) | 34.64 | 17.03 | 0.41 |
   | random (~50% mispredict) | 34.66 | 20.08 | **4.59** |

   Guarded wins everywhere: free when entered DIT-on, and still 58% of branchless at a
   50% mispredict rate. Branchless and today's unconditional `msr DIT, #0` are the same
   price to within 0.03 cyc, so **the immediate and register MSR forms cost the same on
   M5** and gem5's non-renamed `Msr64` model of `MSR DIT, Xt` is a gem5 artifact.

   **MEASUREMENT TRAP, recorded because it briefly inverted the conclusion.** A first run
   used a 4,096-entry cyclic "random" pattern that the predictor simply LEARNED, reporting
   a ~1 cycle mispredict. Widening to 65,536 entries and adding a **branch-only control**
   (the same `tbnz` guarding a trivial `add`) exposed the real ~9 cycles per mispredict.
   Never quote a mispredict cost without a branch-only control on the same pattern.

   ### Why a mispredicted guard does not leak

   The dangerous direction is "predicted not-taken, should have skipped", which
   speculatively clears DIT while the caller's secret is live. It is safe because **the
   clear's effect is not published until commit**, so a squashed clear never un-gates.
   That is a property of the gem5 model (`dit-unconditional-design.md` §3.2, which calls
   it load-bearing) and the Apple sequence above is consistent with it: Apple puts no
   barrier on the restore path, and would have to if a speculative clear could un-gate.

2. ~~**Should the ENABLE carry a speculation barrier?**~~ **CLOSED 2026-08-30: no, not
   for our target machine.** Recorded because the question is natural and someone will
   re-raise it from the Apple disassembly.

   Apple's `_timingsafe_enable_if_supported` ends `msr DIT, #1` with **`sb`**, and
   `AArch64InstrInfo::insertTimingModeSwitch` emits no barrier at all. The apparent
   asymmetry is real but does not apply to us: **the gem5 mode-switch mechanism this
   project targets applies the switch to instructions already in flight**, so there is no
   window in which code behind the enable executes at the old mode. That mechanism is the
   hardware contribution of the wider project, not an assumption made here.

   **This is a result, not a caveat.** A machine whose DIT write only affects instructions
   issued after it must either serialize or fence, which is why Apple pays a full
   speculation barrier on every enable. A mechanism that reaches in-flight instructions
   pays neither. The Apple sequence is the concrete baseline that shows what the barrier
   would have cost, and it is quotable.

   **Scope, so nobody rediscovers this as a bug:** on stock Apple silicon, an enable
   emitted by this pass carries no barrier and therefore inherits whatever in-flight
   visibility that implementation provides. Our correctness argument is stated against the
   project's own switch mechanism. Objects built here still *run* on M-series hardware,
   which is where the timing measurements come from, but the speculation-window claim is
   scoped to the modelled machine.

2. **Keep the static optimization?** `AlwaysEnteredWithDIT` proves `d_in = 1` and lets a
   function skip both the read and the restore. It remains valid on top of the dynamic
   default. Keep both mechanisms, or collapse to the dynamic one for simplicity?
3. **Fixed value at a public boundary?** In addition to non-lowering internally, should
   an exported symbol have a *defined* DIT state at entry and return, the shape AAPCS64
   uses for FPCR.NEP and the x86-64 psABI for DF? This is what would make a hardened
   shared library composable. Leaning yes.
4. **Invert the marker.** If callee-saved is universal, no attribute is needed to opt
   in; one is needed only to mark a function that deliberately lowers DIT. Inverting the
   default is cheaper and fails safe.
5. **EH handler entry.** SME mandates PSTATE.SM = 0 on handler entry and libunwind was
   taught to enforce it. Do we want an analogous rule for DIT, or is leave-as-is
   acceptable? Leave-as-is is safe under §1 and abi-aa #394 shows Arm has not solved the
   general problem either. Leaning leave-as-is.

## 8. What implementing this looks like

The contract is satisfied by `dit-unconditional-design.md` §4's sequence. Three pieces:

1. **Global tail-call disable** (§2.1). A driver change, no analysis. Independent of
   everything else.
2. **The `NONLOCAL` report** (§2.2). New but small, and it follows the existing report
   machinery exactly.
3. **Entry read plus exit restore, with somewhere to keep the entry value**
   (`dit-unconditional-design.md` §5). Two options, and the prior art now favours the
   second:
   - **B2, frame slot. CHOSEN 2026-08-30, on implementation review.** A pre-PEI pass
     reserves one 8-byte object in each candidate function; the post-PEI pass emits
     `mrs`+`str` at entry and `ldr`+guarded-clear at each return, using a scratch
     register **at those two points only**. This is what GCC shipped for PSTATE.SM.
   - ~~B3, over-provision a vreg at ISel and elide late~~ and ~~B1, pre-RA vreg~~:
     **rejected, see §5.1 below.**

   ### 5.1 Why the frame slot beats the register (reversal, 2026-08-30)

   B3 was recommended in an earlier draft of this file on the strength of the shipped
   `ENTRY_PSTATE_SM` pattern. Working out the implementation reversed it.

   **B3's advantage evaporates.** The carrier must be live across calls, so register
   allocation assigns a **callee-saved** register, and PEI then spills and reloads it in
   the prologue and epilogue. That is one store and one load: **exactly the memory
   traffic B2 pays with an explicit frame slot.** B3 wins only in a leaf function with a
   spare CSR, and a leaf function is the least likely to need a carrier at all, since the
   thing that forces one is a call.

   **B3's costs do not evaporate.** Two new pseudo instructions, emission at ISel, and a
   placeholder use at every return that must survive to post-PEI and be recognized there.
   `ENTRY_PSTATE_SM` can elide on `use_empty()` only because SME creates its uses at ISel
   too; ours appear post-PEI, so the placeholder is load-bearing and fragile.

   **And §5.1's own objection to a scratch register is what makes B2 work.** That
   objection is exact: *"x9 is free at entry and at exit, not across them."* With a frame
   slot the value does not have to cross anything. A scratch register is needed at the
   entry read and at each return, and at both points `LivePhysRegs` can prove one free.

   So B2 is the same cost, much less machinery, and matches an upstream implementation of
   the identical problem. Equal cost with less mechanism is the whole argument.

   ### 5.2 The fallback must not clear - and must be LOUD

If no scratch register can be proven free at entry or at some return, the function
falls back to **emitting the entry enable and no clear at all**.

The tempting fallback, today's unconditional `msr DIT, #0`, is **wrong under this
ABI**: a function entered with DIT on would return with it off and strip its
caller's protection, and under the ABI the caller emits no re-assert to repair it.
Leaving DIT set instead satisfies the §1 guarantee and can never expose a secret.

**But "safe" is not the whole story, and an earlier draft of this section stopped
there.** A function that never clears has degenerated to **BLANKET mode for that
function**. It is safe and it is also fatal to any selective-placement measurement
taken on that build - and it was silent unless someone passed
`-taint-nonlocal-report`. An arm of a libsodium f-sweep was withdrawn on
2026-08-31 for exactly this: one function's fallback (in
`crypto_aead_chacha20poly1305_ietf_encrypt`) turned the whole arm into blanket, so
its f* values were invalid.

So the fallback now warns on stderr, with a **specific** blocker token, because
the four causes have unrelated fixes:

| token | cause | fix direction |
|---|---|---|
| `no-slot` | no pre-PEI reservation (the `llc` entry point) | wire the pre-PEI callback, or refuse the flag |
| `slot-negative-offset` | `getFrameIndexReference` picked FP, i.e. a VLA frame | use the unscaled `LDUR`/`STUR` form, which takes a signed 9-bit offset |
| `slot-out-of-range` | past the 12-bit scaled range | materialize the address |
| `slot-scalable-offset` | SVE frame | unsupported |
| `no-prologue` | naked, or no frame at all | refuse |
| `no-scratch` | x9-x15 all live across the span | widen the candidate set, or spill |

**Treat any of these on a hot function as invalidating the measurement**, not as a
tolerable cost.
### 9.1 Piece 1: TU-wide tail-call disable. LANDED, on by default.

`clang/lib/Frontend/CompilerInvocation.cpp`, end of `ParseCodeGenArgs`:

```cpp
  if (!Opts.TaintHarden.empty())
    Opts.DisableTailCalls = true;
```

Placed in `ParseCodeGenArgs` rather than the driver so it applies to a direct
`-cc1` invocation too, which is how the tests drive it. `CodeGenOpts.DisableTailCalls`
adds `"disable-tail-calls"="true"` to every function (`CGCall.cpp:2661`), an
IR-level attribute, so it survives into LTO.

Verified both directions: 2 attributes with the flag on a 2-function TU, 0 without.

### 9.2 Piece 2: the `NONLOCAL` report. LANDED, opt-in.

`-taint-nonlocal-report=<file>`, emitted by `reportNonlocalDITExits` in
`TaintAnalysis.cpp` next to the existing `reportUnbalancedDITExits`. Three kinds:

| kind | detection |
|---|---|
| `setjmp` | callee carries `Attribute::ReturnsTwice` |
| `musttail` | a surviving `TCRETURN`, i.e. both `isReturn()` and `isCall()` |
| `unwind` | the call's block has an EH-pad successor |
| `noscratch` | §5.2's fallback fired: the function could not establish a carrier |

Only emitted for functions that OWN DIT. A function entered with DIT already set
never took on the obligation, so nothing there degrades.

### 9.3 Piece 3: the carrier. LANDED behind `-taint-dit-abi`, default OFF.

Four `TargetInstrInfo` hooks, mirroring the existing `insertTimingModeSwitch` so
`TaintAnalysis.cpp` stays target-independent:
`createTimingModeSaveSlot`, `getTimingModeSaveSlot`, `insertTimingModeSave`,
`insertTimingModeRestore`. AArch64 implements all four; the slot lives in
`AArch64FunctionInfo::TimingModeSaveIndex`.

Reservation runs pre-PEI through a new `TargetPassConfig::setPrePrologEpilogCallback`,
symmetric to the existing post-PEI one, wired in `CodeGenTargetMachineImpl.cpp`
**only when the taint post-PEI callback is set**, so no other pipeline is touched.
The pass (`TaintDITSlotReserve`) is a no-op unless `-taint-insert-dit` is on and
`moduleHasTaintSources` is true.

Emitted sequence, verified end to end:

```asm
        sub  sp, sp, #48
        stp  x20, x19, [sp, #16]
        stp  x29, x30, [sp, #32]
        mrs  x9, DIT              ; read the incoming value
        str  x9, [sp, #8]         ; into the carrier slot
        msr  DIT, #1              ; enable
        bl   _sink_a              ; <- no re-assert
        bl   _sink_b              ; <- no re-assert
        eor  x0, x0, x20          ; secret work, DIT on
        ldr  x9, [sp, #8]         ; reload while the frame is still up
        ldp  x29, x30, [sp, #32]  ; epilogue - these reloads are Needs too
        ldp  x20, x19, [sp, #16]
        add  sp, sp, #48
        tbnz x9, #24, .Lcont      ; entered with DIT on -> skip the clear
        msr  DIT, #0
.Lcont: ret
```

The exit is the **guarded clear** §7 item 1 selected, and its two halves sit in
**different places**: the reload must precede the epilogue, because the frame slot
needs the frame; the mode switch must FOLLOW it, because the epilogue reloads
callee-saved registers that may still hold secrets and those reloads are Needs. A
single insertion point necessarily gets one of the two wrong.

### 9.3.1 The slot reservation is gated on the ABI flag, and why that matters

`TaintDITSlotReserve` checks `TaintInsertDIT && TaintDITAbi`, not just the former. An
earlier version checked only `TaintInsertDIT` and therefore reserved a carrier in every
function of every hardened build, including the default `region` configuration that
never touches it.

Measured on the two-call test case: **32-byte frame with the ABI off, 48 with it on.**
So the cost is real (one 8-byte object, 16 after alignment, per instrumented function)
and it must not be paid by a configuration that cannot use it. Verified both ways: with
the flag off the output contains no `mrs` at all and the frame is unchanged.

### 9.3.2 The verifier earned its keep: the switch cannot precede the epilogue

The first working version put the whole restore before the epilogue, since that is
where the frame slot is valid. The final-MIR verifier rejected the build:

```
PSTATE.DIT placement is unsound: 3 instruction(s) that must execute with DIT set
are reachable with it clear.
  two_calls bb.2: $fp, $lr = frame-destroy LDPXi $sp, 4, implicit $dit
  two_calls bb.2: $x20, $x19 = frame-destroy LDPXi $sp, 2, implicit $dit
```

Those reloads restore callee-saved registers that were holding secrets, so the pass
had pinned them as Needs. Today's unconditional clear sits immediately before the
`ret`, i.e. AFTER the epilogue, so it never had this problem; moving the switch
earlier silently narrowed coverage. Hence the two-insertion-point design.

**The branchless form would have hidden this.** `getTimingModeSwitch` only
recognises `MSRpstateImm4`, so an `MSR DIT, Xt` is invisible to the verifier: it
models DIT as still on and reports nothing. The guarded form is built from
instructions the verifier can see, so its coverage is actually checked. That is an
argument for the guarded form independent of the cycle measurement, and it only
became visible by building both.

A consequence for the scratch register: it must now survive from the reload to the
switch, across the epilogue. `findTimingModeScratchAcross` starts from the liveness
at the reload point (which already rejects anything the epilogue USES) and
additionally excludes anything the epilogue DEFINES, bailing entirely on a call or
regmask in between.

### 9.4 Four things the implementation got wrong first, kept so they are not redone

1. **`storeRegToStackSlot` emits a FrameIndex operand, and we run post-PEI.**
   Frame-index elimination has already happened, so the operand survives to
   MCInstLower and aborts with "unknown operand type". The address is now resolved
   explicitly via `TargetFrameLowering::getFrameIndexReference` into a base
   register and a scaled offset, with a safe bail (returning false, so no restore
   is emitted) for a scalable, misaligned, or out-of-range offset.

2. **The insertion point must be computed ONCE.** `afterPrologue` skips
   `FrameSetup` instructions. Calling it a second time, after the save was
   inserted, stops at the freshly emitted `mrs` - which is not FrameSetup - and
   places the enable BEFORE the read. The function then saves the 1 it just wrote
   and can never restore anything else. Silent: still safe, since it only ever
   leaves DIT set, but the ABI is inert.

3. **The save cannot precede the prologue and the restore cannot follow the
   epilogue.** Both go through the frame, and SP is not adjusted before the
   prologue nor still adjusted after the epilogue. Hence `afterPrologue` and
   `beforeEpilogue`, which walk the `FrameSetup`/`FrameDestroy` runs.

4. **Hand-written call-bearing MIR silently loses taint.** A two-`BL` block
   written by hand reported zero tainted instructions while the identical C
   compiled end to end reported four. Root cause not chased; the lesson is
   recorded in the test, which now uses MIR generated by
   `llc -stop-after=prologepilog`. Do not debug placement against hand-written MIR
   with calls in it.

### 9.5 What is NOT done

- **`unwind` detection is untested.** The `setjmp` and `musttail` kinds are
  covered; no test exercises an EH-pad successor yet.

### 9.5.1 Region placement, and the per-exit choice of restore form

Region placement is the shipped default (`switch-cyc=30`), so the ABI was inert
without this. **Ownership is not relaxed**: a function provably always entered
with DIT on still routes past the region emitter, because narrowing inside a
region its caller deemed secret is an under-taint no exit restore can repair.
What the ABI changes is the two boundaries.

**The choice of restore form is per EXIT, not per placement.** An earlier draft of
this file said function granularity gets the guarded clear and region gets the
unconditional write. That was the wrong cut. The guarded clear can only clear or
do nothing - it can never set - so it is correct exactly when DIT is provably 1 at
that exit:

| entry | at exit | guarded clear does | correct? |
|---|---|---|---|
| 0 | 1 | falls through, clears | yes |
| 1 | 1 | branch taken, skips | yes |
| 0 | 0 | clears (redundant) | yes |
| **1** | **0** | skips, leaves 0 | **no - returns lower than entry** |

Only the last row breaks, and repairing it needs an ENABLE, which may never be
guarded (§3.1). So `emitDITExitRestores` runs the existing `computeDITOnEntry`
dataflow over the emitted code and picks per return: **guarded clear where DIT is
provably set, unconditional `MSR DIT, Xt` otherwise.**

Whole-function coverage satisfies the premise at every exit, so it always takes
the cheap form - but so does a region-placed function whose return sits in an On
block, which is the common case. Measured on the two-call example, region and
function granularity emit the *same* `tbnz w9, #24` exit.

Two alternatives, both rejected: forcing the premise with an unconditional
`msr DIT, #1` before the guard costs that write PLUS the guard, strictly worse
than just writing the saved value; and guarding the enable is the forbidden case.

Four things region placement forced:

- **`calleeLeavesDITSet` returns true under the ABI**, as a contract rather than an
  analysis result. Suppressing only the emission was not enough: the region
  emitter's own soundness verifier models a clobbering call as clearing, so every
  Need after a call read as uncovered and the whole function fell back to
  whole-function granularity.
- **The carrier save needs two insertion points too.** Region placement puts its
  enable at the very top of the entry block, *ahead of the prologue*. The read must
  precede that enable; the store must follow the prologue, because SP is still the
  caller's before it. A single point put `str x9, [sp, #8]` above the caller's
  stack pointer.
- **`afterPrologue` scans for the LAST FrameSetup**, not the first non-FrameSetup.
  The original form stopped at that pre-prologue enable and returned a point where
  the frame did not yet exist.
- **The split point must be computed after the reload is emitted.** A return whose
  block has no epilogue of its own has nothing before it to split at, so an
  emptiness check placed before the reload silently emitted no restore at all -
  observed on a function with a public early exit and a secret path, where BOTH
  returns lost their restore. The reload itself guarantees a predecessor.

### 9.5.2 A leak the tests initially passed over: TBNZX vs TBNZW

`AArch64::TBNZX` hard-codes b5=1, so its immediate is only the low five bits of the
bit number: `TBNZX ..., 24` tests bit **56**. Bit 56 of an `MRS DIT` result is
always zero, so the guard never took, the clear always ran, and a function entered
with DIT on returned with it off - stripping its caller. A leak, not a slowdown.

Two things hid it. The asm printer shows the raw operand, so the `.s` read
`tbnz x9, #24` and a CHECK for that passed. And the final-MIR verifier could not
see it either: it is intraprocedural and treats calls as transparent, and this is a
cross-frame property. It was found by disassembling the object.

Fixed by using `TBNZW` on the 32-bit subregister. The test now pins the register
**width**, which is the part that distinguishes correct from broken.

### 9.6 Tests

| test | covers |
|---|---|
| `clang/test/CodeGen/taint-dit-abi.c` | pieces 1 and 3 end to end, with a TODAY arm pinning the re-asserts the ABI deletes and separate FUNCTION and REGION arms for the two exit forms |
| `llvm/test/CodeGen/AArch64/taint-analysis-nonlocal-report.mir` | piece 2, `setjmp` and `musttail`, with `plain` as a negative control |

`llvm/test/CodeGen/AArch64/taint-analysis-*.mir` plus `TaintAnnotate`: 36 tests, all
passing.
