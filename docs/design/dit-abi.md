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
per-function.** `-ftaint-harden` implies `-fno-optimize-sibling-calls`.

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

## 5. Cost model

Cost moves from **O(call sites)** to **O(instrumented functions)**: two instructions per
instrumented function (one entry read, one exit restore), zero per call site.

Projected against the measured upper bound, full-LTO Bitcoin Core:

```
  today (caller-saved)          127,740 MSR DIT
  re-asserts deleted, unsound    15,272 MSR DIT   (measured 2026-08-30)
  this ABI                       ~15,272 MSR DIT  + ~1 MRS per instrumented function
```

**The switch count does not grow when the callee half is added, and an earlier draft of
this file was wrong to project ~23,000.** The exit restore *replaces* the existing
`msr DIT, #0` one for one, and the entry read is `MRS`, a different instruction that is
not a mode switch at all (1.00 cyc measured on M5, versus 30.34 for `MSR DIT`). So the
measured 15,272 is close to the real sound figure, an **8.4x reduction**, and the added
cost is one cheap read per instrumented function plus, for functions that did not
already need one, a callee-saved register spill and reload in the prologue and epilogue.

**The surviving exits are cheaper than today's.** The guarded clear is free whenever the
function was entered with DIT already on (OPEN item 1), and otherwise costs what today's
clear costs. Measured on M5: the branchless
restore `msr DIT, x19` and today's unconditional `msr DIT, #0` are the same price to
within 0.03 cyc. So the 8.4x count reduction is the whole win, and it is not diluted by
a more expensive exit. A guarded clear would make a fraction of exits free on top of
that, but is rejected on speculation grounds; see OPEN item 1.

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
   - **B3, over-provision at ISel and elide late. RECOMMENDED, and it needs no two-pass
     compile.** `ENTRY_PSTATE_SM` is created in *every* streaming-compatible function and
     erased when `use_empty()` (`AArch64ISelLowering.cpp:3339`), so the instrumented set
     never has to be known in advance. One adaptation: our uses are created post-PEI, so a
     **placeholder use at each return must be emitted at ISel** to keep the value live
     through RA. See `dit-unconditional-design.md` §5.2.
   - **B1, pre-RA virtual register decided in advance.** Superseded by B3, which is the
     same mechanism without the scheduling problem.
   - **B2, frame slot behind a hard frame pointer.** This is what **GCC shipped** for
     PSTATE.SM (`dd8090f40079fa41ee58d9f76b2e50ed4f95c6bf`). A hard FP defeats the
     "post-PEI stack push is a miscompile" objection. In LLVM it needs a small pre-PEI
     slot reservation driven by an annotator-set attribute, because we lack GCC's
     `TARGET_USE_LATE_PROLOGUE_EPILOGUE`. That is the **AArch64 SLH shape**: a coarse
     pre-ISel attribute consumed by a late pass, with an assert in the late pass.

**Only piece 3 needs the two-pass compile now.** Moving the tail-call fix to a global
flag took it off that dependency, so pieces 1 and 2 can land immediately and
independently. Piece 3 takes the guarded clear (OPEN item 1, settled by Apple's shipping
sequence); its storage should be B3, over-provision and elide, which **removes the
two-pass compile from the project entirely** - no piece of this ABI now needs it.
