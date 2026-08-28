# What everyone else does about tail calls

**Question.** A tail call is an exit with no epilogue: the frame is gone and control
never comes back, so there is no instruction at which `PSTATE.DIT` could be restored.
We resolved that by leaving DIT set (`docs/design/dit-tailcall-gap.md`). Is that a
defensible position in the literature, and is the deferred fix - force `notail` on
secret-passing calls - established practice?

**Answer, in one line.** Leaving the bit set is defensible as an *accepted cost* with
one real precedent (Go, deliberately), but it is the minority answer. Forcing `notail`
is the majority answer, and there is a same-architecture, same-kind-of-object
implementation of it in our own checkout: **Arm SME.**

Surveyed 2026-08-27. Provenance is marked throughout, because the negative results
(who is silent) are load-bearing and only worth as much as the method behind them:

- `[V]` verified by me directly in this checkout or a fetched primary source.
- `[R]` reported by the survey from a primary source it fetched, not re-verified here.
- `[INF]` inference, asserted by no source.

---

## 1. The decisive precedent: Arm SME mode bits `[V]`

`PSTATE.SM`, `PSTATE.ZA` and `ZT0` are PSTATE mode bits managed by exactly our
enable/disable bracketing discipline, on exactly our architecture. **LLVM forbids
tail-call optimization whenever a mode change would have to be undone after the call.**

`llvm/lib/Target/AArch64/AArch64ISelLowering.cpp:9401`, in
`AArch64TargetLowering::isEligibleForTailCallOptimization`:

```cpp
  // SME Streaming functions are not eligible for TCO as they may require
  // the streaming mode or ZA/ZT0 to be restored after returning from the call.
  SMECallAttrs CallAttrs =
      getSMECallAttrs(CallerF, getRuntimeLibcallsInfo(), CLI);
  if (CallAttrs.requiresSMChange() || CallAttrs.requiresLazySave() ||
      CallAttrs.requiresPreservingAllZAState() ||
      CallAttrs.requiresPreservingZT0() ||
      CallAttrs.caller().hasStreamingBody() || CallAttrs.caller().isNewZA() ||
      CallAttrs.caller().isNewZT0())
    return false;
```

Commit `5fae000f36107a64f7f5b0ac5233803ab2bd82cd`, Sander de Smalen, 2022-09-17,
D131579, whose message is our problem statement with the nouns swapped:

> When a streaming mode change is (or may be) required for a call, it will need to
> restore the original mode after the call, which prevents the use of tail-call
> optimization. The same holds true for a call that requires the lazy-save mechanism
> to be set up before the call, and possibly restored after.

Pinned by `llvm/test/CodeGen/AArch64/sme-streaming-body.ll:282`, `@disable_tailcallopt`,
carrying `"aarch64_pstate_sm_body"`. The IR says `tail call void
@streaming_compatible_callee()`; the codegen is

```
stp/str spills ; smstart sm ; bl streaming_compatible_callee ; smstop sm ; reloads ; ret
```

**The tail call is demoted precisely so the mode clear can be emitted after it.** Also
`sme-shared-za-interface.ll` (*"Ensure that we don't use tail call optimization when a
lazy-save is required"*) and `sme-new-za-zt0-no-tail-call.ll`.

### 1.1 The rule is not a blanket ban, and its predicate is ours `[V]`

`llvm/lib/Target/AArch64/AArch64SMEAttributes.cpp:110`:

```cpp
bool SMECallAttrs::requiresSMChange() const {
  if (callee().hasStreamingCompatibleInterface())
    return false;
  // Both non-streaming
  if (caller().hasNonStreamingInterfaceAndBody() &&
      callee().hasNonStreamingInterface())
    return false;
  // Both streaming
  if (caller().hasStreamingInterfaceOrBody() &&
      callee().hasStreamingInterface())
    return false;
  return true;
}
```

A tail call between two functions with the **same** mode interface is allowed: no
change means no restore means no problem. TCO is refused unconditionally when
`caller().hasStreamingBody()`, i.e. `__arm_locally_streaming` - a function that turns
the mode on at its own entry and off before its own exit. **That is
`-taint-dit-placement=function`.** `hasStreamingCompatibleInterface` is the
inherit-and-do-not-touch case, i.e. `AlwaysEnteredWithDIT`.

GCC reached the same answer independently in `aarch64_function_ok_for_sibcall` `[R]`,
and its formulation is the one to copy because it is stated as a predicate rather than
a list:

```c
  if (aarch64_fntype_pstate_sm (fntype) & ~aarch64_cfun_incoming_pstate_sm ())
    return false;
```

Refuse the sibcall only when the callee needs a mode the caller was not *entered* with.
The helper's own comment draws our distinction explicitly `[R]`:

```c
/* Return the state of PSTATE.SM on entry to the current function.
   This might be different from the state of PSTATE.SM in the function
   body.  */
```

Mode-on-entry versus mode-in-body is `AlwaysEnteredWithDIT` versus our placement, and
the sibcall rule is stated in terms of mode-on-entry. GCC also arrived at part of this
as a bug fix, in a patch whose description could be pasted into our §1 `[R]`: *"foo
cannot tail-call bar because foo needs to restore ZT0 after the call. I'd forgotten to
update the ok_for_sibcall rules to handle this when adding SME2."*

### 1.2 AAPCS64 solves it structurally, and says nothing about DIT `[R]`

AAPCS64 makes the mode a declared part of the **subroutine interface** (a "PSTATE.SM
interface": non-streaming, streaming, or streaming-compatible), splitting the
obligations - entry state is the caller's responsibility, return state is the callee's -
and then defines "normal return" so the tail call falls out:

> For example, if the call from S1 to S2 is a "tail call", X will be the return address
> supplied by S1's caller. The act of resuming execution at X is then a normal return
> from S2 to S1 and a normal return from S1 to S1's caller.

So the general answer to "how do you restore state at an exit with no epilogue" is: you
do not. You make the mode an interface contract, and the tail-callee's own return
discharges the obligation for both frames.

**AAPCS64 contains zero mentions of `PSTATE.DIT`** across all 3,635 lines of
`aapcs64.rst` `[R]`. DIT is neither caller-saved nor callee-saved. We have no ABI to
appeal to and none to violate.

---

## 2. The constant-time literature is silent, and the silence is structural

Not an oversight: **nobody else toggles DIT/DOIT from the compiler**, so the
toggle-placement question, and therefore the tail-call question, never arises.

- **cio** (ASPLOS'24) anticipates this design and declines to build it `[R]`, §10:
  *"cio still works well, since the only thing that would change is that our transforms
  can switch to toggling the feature/mode bit."* Its §9.6 Limitations covers inline asm
  and `.S` files only, as `docs/research/cio-and-ct-literature.md` already recorded.
- **Serberus** (S&P'24) `[R]`: DOIT is a background assumption about the machine
  ("assumes ... (iii) Intel's DOIT Mode"), not something the compiler places.
- **Let's DOIT** (TCHES 2025(3):644-667) `[R]`, §3: *"We assume that the DOIT mode is
  enabled via the `IA32_UARCH_MISC_CTL` flag, **before running Jasmin-compiled
  code**."* Its developer recommendation is *"Enable the DOIT/DIT mode when possible on
  the target platform."*
- **The LLVM community explicitly declined compiler-emitted DIT** `[R]`, RFC
  "Constant-Time Coding Support", discourse.llvm.org/t/87781, 46 posts, 68 mentions of
  DIT, **zero** mentions of tail calls. Post #16, statham-arm, making our own argument:

  > I wouldn't want the compiler to emit a DIT-setting instruction ... because I believe
  > Arm's current guidance is that the operation of setting DIT can potentially have a
  > performance cost, so you don't want to set it right inside each crypto kernel
  > anyway - better to set it once, call multiple crypto functions in a tight cluster
  > ... then unset DIT when it's all done.

  Post #26, mhaeuser, which is our placement problem stated as an open question:

  > agnostic code may define the boundaries of a secrecy code section (basically a "DIT
  > on" block) in a way that may be good for one but bad for the other side of the
  > trade-off (toggle latency vs runtime penalty), which might even be different based
  > on microarchitecture.

**Consequence for the writeup.** Toggle placement is unclaimed territory. That is good
for the novelty claim and it means there is no precedent to lean on for the tail-call
decision specifically.

---

## 3. The shipping DIT-bracketing systems all reinvented our ownership rule `[R]`

Three independent production implementations, none of them a compiler, and all three
implement `AlwaysEnteredWithDIT`.

**Apple corecrypto**, `apple-oss-distributions/xnu` `osfmk/corecrypto/cc_internal.h`:

```c
	// DIT might have already been enabled by another corecrypto function, in
	// that case that function is responsible for disabling DIT when returning.
	//
	// This also covers when code _outside_ corecrypto enabled DIT before
	// calling us. In that case we're not responsible for disabling it either.
	if (cc_is_dit_enabled()) return false;
```

exposed as `CC_ENSURE_DIT_ENABLED`, built on `__attribute__((cleanup))`.

**Apple's public API**, macOS 15.2+, `libplatform` `include/timingsafe.h`: a
`timingsafe_token_t` returned by `timingsafe_enable_if_supported()` and consumed by
`timingsafe_restore_if_supported()`, recording whether DIT was already on. Note also
`OS_SWIFT_UNAVAILABLE_FROM_ASYNC("Not supported for async.")` - Apple forbids the
dynamic extent from spanning a suspension point, the language-level form of our problem.
Apple pairs the DIT enable with a speculation barrier, which we deliberately do not
(the ISB/DSB mode was removed 2026-07-14 and speculation is out of scope).

**Go**, `crypto/subtle/dit.go`: `alreadyEnabled := setDITEnabled()` plus a `defer` that
clears only if it owned the bit.

**All three destroy tail-call position as a side effect**, and none of them appears to
have chosen it deliberately: `cleanup`, a real restore call, and `defer` each leave
something that must run after the call. Measured `[R]`: `__attribute__((cleanup))` at
`-O2` on arm64 turns `b _callee` into `bl _callee` plus the cleanup call. **The only
shipping production DIT-bracketing system solves the tail-call problem by making every
bracketed function non-tail-calling - the `notail` fix, obtained free from C scoping.**

---

## 4. Our choice has one precedent, and it came with an obligation `[R]`

Go **deliberately widened** the same leak in 1.26: `WithDataIndependentTiming` now
leaks DIT into every goroutine spawned inside the region and all their descendants, for
their lifetime. golang/go#76477, rolandshoemaker:

> the worst case is that additional code would now run with data independent timing,
> causing it to run slower. I think this is acceptable, it will not break any
> cryptography, and while perf regressions in uncommon cases are unfortunate, we can
> call this out explicitly in release notes.

The objections raised were ours, verbatim. mknyszek:

> accidentally spawning a goroutine with the DIT bit set will just have regular
> instructions executing more slowly without an obvious cause. That is, **it's not
> obviously visible in CPU profiles**

DanielMorsing:

> **Having the cost be hidden in the runtime makes the failure mode hard to diagnose**
> ... an accumulating, silent performance issue

**The accepted resolution was observability, not correctness.** aclements: *"I agree
with @mknyszek that these states should be visible in the backtrace."*

That is what `reportUnbalancedDITExits` / the `DITLEAK` report class does as of
2026-08-27. It is what brings us level with the precedent rather than short of it.

Two things still temper the position. Go's leak is bounded by a dynamic extent the
runtime owns and always unwinds; ours past an external or indirect tail callee has no
terminating event at all. And no *compiler* in the survey chose this answer.

---

## 5. What everyone else does instead, and why it is unsound for us

The majority non-ban strategy is: **run the restore before the tail branch, omit the
transfer.** The clearest academic statement is the only paper found that treats the
general shape head-on - Burow, Zhang, Payer, "SoK: Shining Light on Shadow Stacks",
IEEE S&P 2019, §VI, in a subsection literally titled "Tail Call Optimizations" `[R]`:

> To keep the shadow stack in sync, we execute the normal shadow stack epilogue before
> tail calls, though we omit the jump through the return address in these cases.
> Consequently, fault epilogues fall back to LBP for tail calls, as there is no jmp to
> modify.

That second sentence generalizes: an instrumentation technique that presupposes a
control-transfer instruction it can rewrite does not survive a tail call.

In-tree instances of the same move, all `[V]` unless noted:

| Site | Mechanism |
|---|---|
| `StackProtector.cpp:669` | canary check hoisted above the teardown; **this was a bug first** - before D133860 (2022-09-16) it handled only `musttail`, so an ordinary `tail` call plus `sspreq` silently lost the check |
| `AArch64PointerAuth.cpp:180` | matches `AArch64::RET` exactly, so `TCRETURN*` gets a standalone `AUTIASP` before the branch |
| `AArch64PrologueEpilogue.cpp` | ShadowCallStack pops at `getFirstTerminator()`; skipped entirely when LR is never spilled |
| `SafeStack.cpp:388`, `AddressSanitizer.cpp:1138`, `EntryExitInstrumenter.cpp:148` | treat a terminating `musttail` call as the function's exit, via `BasicBlock::getTerminatingMustTailCall()` |
| `X86VZeroUpper.cpp:206` | `IsControlFlow = IsCall \|\| IsReturn` - a tail call needs no special case, being covered twice |

**Why this is sound for all of them and unsound for us** `[INF]`. Canary, unsafe-stack
pointer, shadow-stack slot, ASan redzone, PAC on LR: every one of those belongs to a
frame that is genuinely dead by the time the branch executes, or is re-established by
the callee's own prologue. `MSR DIT, #0` is different in kind, because the callee is
the party that still needs the protection. That is the entire content of the 2026-08-05
libsodium `crypto_sign` under-taint.

**This asymmetry has to be stated explicitly in any writeup.** A reviewer who knows the
shadow-stack and canary precedents will otherwise ask why we did not simply do what
they did.

One more datapoint on the danger of the near-miss: pac-ret's restore-before-branch
needed an *extra* security measure that the return path does not.
`AArch64Subtarget.cpp:610` `[V]`:

> LR may require explicit checking because if FEAT_FPAC is not implemented and LR was
> tampered with, then `<authenticate LR>` will not generate an exception on its own.
> Later, if the callee spills the signed LR value ... the valid PAC replaces the higher
> bits of LR thus hiding the authentication failure.

This is the same class of hazard as our §2 "let the instrumented callee re-assert at its
own entry" option, and the resolution was a dedicated check before the branch, not a ban.

**SLH takes a fourth path worth knowing about** `[R]`: it avoids the problem by not
making the state caller-owned at all. The predicate rides in the high bits of SP, so it
flows into the callee at a call and back out at a return.
`X86SpeculativeLoadHardening.cpp:2037`: *"For tail calls, this is all we need to do."*
Not available to us - DIT is architectural state with no ABI slot (§1.2) - but it is the
software analogue of AAPCS64's answer.

---

## 6. Interprocedural taint analysis: silent, with one theory paper that is not

**Debray and Proebsting, "Interprocedural Control Flow Analysis of First-Order Programs
with Tail Call Optimization", ACM TOPLAS 19(4), 1997** `[R]`. §1:

> Tail call optimization complicates the analysis because returns may transfer control
> to a procedure other than the active procedure's caller.

Their model is a pushdown automaton over the flow graph; a tail call block emits no
return-site continuation at all (production `B -> p`). They name the wrong model
outright, in §5:

> Note that **a naive analysis that handles tail calls as if they were calls that
> returned to an empty basic block immediately following the call site** would infer
> that basic block B2 had three predecessors ...

and state the consequence for interprocedural dataflow in §6.1:

> Because the point to which a call returns is not obvious in the presence of tail call
> optimization, it is not obvious how to apply these analyses to systems with tail call
> optimization ... **the analysis can infer spurious pointer aliases by propagating
> information from one call site back to a different call site.**

**Relevance to us.** Our propagation does not currently model a tail-call edge as a
call-with-return-site, and per this paper it must not start. Worth citing if the
propagation is ever revisited.

Binary analysis platforms treat tail-call recovery as a distinct, opt-in problem `[R]`:
angr has `detect_tail_calls=False` by default with a recursive `_get_tail_caller` that
walks predecessors to find the real call site; Ghidra's "Shared Return Calls" analyzer
is off by default, and issue #4573 records that without it Ghidra *"will assume that the
called function is part of the caller function ... and can mess with the stack
analysis."*

---

## 7. The silence list

Full texts fetched, extracted to plain text, and grepped; every `tail` hit was
disambiguated by hand against `detail`/`tailor`/`entail`. `[R]` throughout - the survey
did the fetching; I did not repeat it.

| Source | Venue | `tail` | `sibling` | Verdict |
|---|---|---|---|---|
| cio | ASPLOS'24 | 9, all `detail`/`tailored` | 0 | SILENT |
| Serberus | S&P'24 | 6, all `detail` | 0 | SILENT |
| Constantine | CCS'21 | all `detail`/`tailor` | 0 | SILENT |
| Let's DOIT | TCHES'25 | 0 | 0 | SILENT |
| Reps-Horwitz-Sagiv (IFDS) | POPL'95 | 2, both `details` | 0 | SILENT |
| FlowDroid | PLDI'14 | 9, all `detail` | 0 | SILENT |
| Schwartz-Avgerinos-Brumley DTA SoK | S&P'10 | 4, all `detail` | 0 | SILENT |
| libdft | VEE'12 | 2 | 0 | SILENT |
| TaintDroid | OSDI'10 | 4, all `details` | 0 | SILENT |
| obliv-clang | arXiv 2606.16187 | 5, all `detail` | 0 | SILENT, and it *enumerates* the LLVM passes it disables for security; TCO is not among them |
| Obelix | arXiv 2509.18909 | 3 | 0 | SILENT |
| SplittingSecrets | arXiv 2601.12270 | 3, all `detail` | 0 | SILENT (rejects DIT as a mechanism) |
| RISC-V Zicfiss backward-CFI spec | current | leaf/non-leaf only | 0 | SILENT |
| LLVM CT-coding RFC thread | 2025-26 | 1 real hit, "the tail of a function" | 0 | SILENT despite 68 DIT mentions |
| Zipper Stack | ESORICS'20 | 0 relevant | 0 | SILENT, and conspicuously: dedicated section on setjmp/longjmp and C++ exceptions, nothing on tail calls |
| GCC `-finstrument-functions` docs | current | - | - | SILENT (suppression is emergent, not documented) |

**Not silent, for the record:** SoK Shadow Stacks (§5); Debray-Proebsting (§6); Silhouette
(USENIX Sec'20) on the forward-edge side only; SafeStack/CPI (OSDI'14) claims support for
tail calls in §3.3, backed by the `musttail` handling verified in `SafeStack.cpp:388`.

**Could not access:** Arm KA005181 ("How is instruction timing affected by FEAT_DIT"),
Apple's arm64 DIT guidance page, Intel's DOIT ISA guidance (403), Abadi et al. CFI
TISSEC'09 (404). All JS-rendered or paywalled; not quoted.

**NOT CHECKED - do not cite as silent.** Two sweeps did not complete: the remaining
IFDS/summary systems (SPARTA, Pysa/Pyre, CodeQL dataflow, DOOP, Sharir-Pnueli
call-strings) and the second tier of CT systems (SC-Eliminator, FaCT, Blade, Raccoon,
Escort, ct-verif, Binsec/Rel, Jasmin, Vale, Swivel, CT-Wasm).

---

## 8. If the `notail` fix is ever built

Ordered by what the survey actually changes.

1. **State the rule as a predicate, GCC-style, not as a ban.** Forbid the tail call iff
   the caller owns DIT *and* the callee's required DIT-on-entry is not implied by the
   caller's DIT-on-entry. That permits `.dit`-clone-to-`.dit`-clone tail calls for free
   and reduces to a ban only for the mode-owning case, which is exactly LLVM's
   `caller().hasStreamingBody()` and GCC's `aarch64_cfun_incoming_pstate_sm`.

2. **`not_tail_called` (the Clang attribute) is insufficient** `[R]`. AttrDocs says it
   *"prevents tail-call optimization on statically bound calls"* only, and the docs show
   a worked counter-example where the same callee reached through a function pointer is
   still tail-called. Our worst case *is* the indirect tail callee. Use `notail` on the
   `CallInst`, which is per-call-site.

3. **The two-pass compile is a real cost, not a shortcut we skipped** `[V]`. LLVM
   enforces the SME rule in `isEligibleForTailCallOptimization` at ISel; GCC in
   `TARGET_FUNCTION_OK_FOR_SIBCALL` at expand. Neither attempts it later, and there is
   no post-PEI pass anywhere in the tree that converts a `TCRETURN` back into a `bl`
   plus epilogue. §4 of `dit-tailcall-gap.md` is confirmed by both compilers.

4. **objtool is the prior art for a verifier** `[R]`:
   `tools/objtool/Documentation/objtool.txt` rule 4 requires the frame pointer at a
   sibling-call branch to hold the value it had on entry, and warning 6 is *"sibling
   call from callable instruction with modified stack frame"*. Same shape as
   `reportUnbalancedDITExits`.

## 9. Open questions this survey did not settle

- **Does the leak cost measurable cycles on any workload?** Nothing here answers that,
  and `dit-tailcall-gap.md` §4 already gates the fix on it. On serializing-DIT hardware
  the cost is the toggle and not the dwell, so `notail` trades a nearly-free dwell for
  2-3 guaranteed switches plus a real frame per site. It could easily be a net loss.
- Whether the second-tier CT systems in §7 are also silent.
- Whether Arm has ever published guidance on where to clear DIT (KA005181 unread).
