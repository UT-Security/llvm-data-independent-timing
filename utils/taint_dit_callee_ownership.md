# DIT callee ownership: what shipped, and the deferred MRS design

**Status 2026-08-08.** Mode 1 (ownership rule for resolvable calls, plus an audit
report for the rest) is **implemented and tested**. Mode 2 (the runtime `MRS`
read, which is what fixes indirect and cross-TU calls) is **deferred** as out of
scope, and this file is the record so the decision can be discussed rather than
re-derived.

---

## 1. The problem

Every instrumented function used to clear `PSTATE.DIT` before returning. So a
callee reached from inside a caller's DIT-on region destroyed the caller's state,
and the caller had to repair it by re-asserting `MSR DIT, #1` after **every**
non-tail call.

That is correct. The cost is that the switch count then scales with the **call
count** rather than with the number of secret regions:

```asm
cbc_encrypt:
    msr  DIT, #1
.Lloop:                     ; 256 iterations per 4 KB page
    blr  x8                 ; -> rijndael_ecb_encrypt (indirect)
    msr  DIT, #1            ; re-assert: the callee cleared it
    b.ne .Lloop
    msr  DIT, #0
    ret

rijndael_ecb_encrypt:
    msr  DIT, #1
    ...
    msr  DIT, #0            ; clears DIT the CALLER turned on
    ret
```

Three mode switches per 16-byte AES block. `PreservesDIT` cannot help: it
correctly reports "this callee does not preserve DIT", because our own
instrumentation is what makes that true. **We were re-asserting to repair a
clobber we emitted ourselves.**

It is also a soundness gap, not only a cost one. Under region placement a
callee's *internal* region ends clear DIT mid-callee, while the caller's secret
is still live in the frame, and the caller's re-assert only repairs the state
after the call returns. Nothing closes that window.

## 2. The rule

> **Only the frame that turned DIT on may turn it off.**

- Entered with DIT **on** -> this frame did not set it -> must never clear it,
  including at its own internal region ends. Returns with DIT on.
- Entered with DIT **off** -> may set it, and owns clearing it.

## 3. What shipped (Mode 1)

**`AlwaysEnteredWithDIT`**, a summary bit computed after the taint fixed point
(step 3b-2). A function qualifies only when it cannot be reached except through a
secret-passing call: local linkage, address never taken, at least one in-TU call
site, and every such call site passes a secret.

That last condition is what ties the bit to DIT actually being set, and it is not
a new assumption. Step 3c already **verifies** the Scenario-B invariant that a
secret-passing call executes with DIT=1, under both placement policies, asserting
if it ever does not.

Consumers: a qualifying function emits no disable before its returns, and its
callers skip the after-call re-assert. It also bypasses the region emitter, since
region placement narrows coverage by *clearing* DIT, which for a non-owning
function is not merely unprofitable but illegal.

**The entry ENABLE is deliberately kept**, even where provably redundant. The two
directions are not symmetric:

| | if the analysis is wrong |
|---|---|
| skip the **clear** | DIT left on -> dwell cost -> **fail-safe** |
| skip the **set** | secret work runs unprotected -> **fail-dangerous** |

Test: `llvm/test/CodeGen/AArch64/taint-analysis-dit-caller-preserve.mir`, verified
to fail against the pre-fix compiler on exactly the two intended checks, with two
positive controls so "never clear anything" cannot pass.

**`-taint-dit-reassert-report=<file>`** lists every call site where the callee
could not be proven to leave DIT alone, with the reason: `indirect`, `external`,
or `clears-on-exit`. These sites are sound, not hazards. The report is the *cost*,
and it is exactly the list of sites Mode 2 would eliminate.

## 4. What Mode 1 does NOT fix

**Any call-graph edge the analysis cannot resolve**, which is two cases:

1. **Indirect calls.** The caller cannot name the callee.
2. **Cross-TU calls.** Taint is TU-scoped, so a caller sees only a declaration,
   and the callee cannot enumerate its callers to prove they all enter DIT-on.

Case 2 needs no function pointer at all. **A hardened library pays this at every
TU boundary even with zero indirect calls**, which is easy to overlook.

On SQLCipher's crypto the report is four lines:

```
REASSERT external callee=cipher_is_valid caller=cbc_encrypt bb=6
REASSERT external callee=cipher_is_valid caller=cbc_decrypt bb=6
REASSERT indirect callee=<indirect>      caller=cbc_encrypt bb=34
REASSERT indirect callee=<indirect>      caller=cbc_decrypt bb=17
```

The two `external` sites are cold (argument validation, once per operation). The
two `indirect` sites are the entire problem: the per-block AES dispatch through
`cipher_descriptor[]`, a table `register_cipher()` writes at run time, so it is
**not** statically resolvable even in principle. 256 executions per 4 KB page.

## 5. Mode 2, deferred: the runtime `MRS` read

### Why it works where Mode 1 cannot

It removes the need to know the callee. The callee reads its own entry state, so
the property becomes an **invariant by construction** instead of a per-callee
proof, and therefore holds through an unresolvable edge.

```asm
entry:  mrs  x9, DIT
        str  x9, [FI]
        tbnz x9, #24, .Lon      ; already on -> skip the set
        msr  DIT, #1
.Lon:   ...body...
exit:   ldr  x9, [FI]
        tbnz x9, #24, .Lskip    ; entered on -> leave it on
        msr  DIT, #0
.Lskip: ret
```

Note this also makes **skipping the entry set safe**, which §3 ruled out for the
static case. The objection there was to a static *assumption*; a runtime read is a
*measurement*, so it does not apply. On the hot path this executes zero `MSR`s.

A useful simplification: no new analysis is needed. Apply the runtime check to
every instrumented function that is not already `AlwaysEnteredWithDIT`. If it is
really always entered DIT-off, the read returns 0 and behavior is identical to
today plus ~2 cycles.

### Measured, on M5 (`playground/dit_bench/dit_own_bench.c`)

| | cycles |
|---|---|
| `msr DIT` (write) | 30.34 |
| **`mrs DIT` (read)** | **1.00** |
| today: 3x `msr` per call | **90.67** |
| ownership, entry state in a frame slot | **2.01** |
| ownership, entry state in a register | **1.03** |

The read is **30x cheaper than the write**, and stays 1.00 with a data dependency
forced on it, so it is not being hidden by out-of-order execution. Also worth
recording: on M5 a *redundant* `msr DIT, #1` when DIT is already 1 still costs
30.36 cyc. There is **no same-value discount** on M5, unlike the 12 cyc measured
on M4, so the re-assert is full price.

### Where to store the entry state: frame slot

| | frame slot | reserved GPR |
|---|---|---|
| per call | 2.01 cyc | 1.03 cyc |
| nesting (mixed calls mixed) | correct | **caller's saved value clobbered** |
| recursion | correct | **breaks immediately** |
| failure mode | n/a | reads stale 0 -> **clears while caller's secret is live** |
| global cost | 16 bytes/frame, hardened builds only | a GPR taxed across all code |

The 1-cycle difference is noise against the 90.67 it replaces. What is not noise
is that the register version needs an invariant ("a mixed function never calls a
mixed function") that is unverifiable through indirect calls and whose failure
mode is a **silent security bug**, not a slowdown.

### Why it was deferred: the blast radius

1. **The slot must be reserved before PEI**, and the taint pass runs after it.
   That needs a new pass in `AArch64PassConfig::addPostRegAlloc()` and a
   `DITSaveFI` field in `AArch64FunctionInfo`'s MIR serialization, so the frame
   index survives the phase-1 -> phase-2 round trip. Both are AArch64 target code
   and MachineFunctionInfo serialization, a much larger surface than anything the
   taint work has touched so far.
2. **The conditional restore needs a `tbnz`, so basic blocks must be split
   post-PEI**, after every CFG-sensitive pass has run.
3. Neither is blocked by a hard problem; both are places where a subtle bug would
   be expensive to find.

Alternatives considered and rejected:

- **Post-PEI `str x9,[sp,#-16]!` / `ldr x9,[sp],#16`.** Self-contained, no target
  changes, but it means hand-rolling CFI. This project has already been bitten by
  CFI round-tripping (the `<mcsymbol >` strip), and `-g -O2 -ftaint-harden` is a
  supported configuration that must stay `llvm-dwarfdump --verify` clean.
- **IR-level alloca in `taint-annotate`.** Needs a volatile store to survive PEI's
  dead-object elimination, which puts a store in *every* function of a hardened
  build.

### The cheaper alternative, if Mode 2 stays out of scope

**Function cloning.** For a resolvable callee, emit a `foo.dit` variant with no
switches at all and have DIT-on call sites call it. Purely static, no storage, no
`MRS`. It cannot work for an indirect site, because the call site cannot pick the
clone, so it is strictly weaker: it would leave SQLCipher at 1 switch per block
(~30 cyc) instead of 0, versus ~90 today. Roughly two thirds of the win for a
fraction of the blast radius.

## 6. What is measured vs projected

**Measured:** the instruction-level costs above; the report's output on real
SQLCipher crypto; Mode 1's codegen change, verified by a test that fails against
the pre-fix compiler.

**Projected, NOT measured:** the end-to-end effect on SQLCipher runtime. The
arithmetic says the toggle term is the same order as the encryption itself, but
that has not been confirmed with a gem5 or silicon A/B. **Do not quote a
speedup for Mode 2 without running one.**

## 7. For discussion

1. Is the AArch64 target-code change (pre-PEI pass + `MachineFunctionInfo`
   serialization) acceptable, or should this stay out of target code?
2. If it stays out, is cloning-for-resolvable-edges worth doing on its own, given
   it leaves the indirect case untouched?
3. Mode 2 implies an ABI-level claim: "in a hardened build, no function clears
   DIT it did not set." Code we do not compile (libc, syscall stubs) is assumed
   never to execute `MSR DIT`. That is true of every current toolchain and DIT is
   not part of any ABI, but it is an **assumption and belongs in the threat
   model**, not buried in the pass.
4. Before building Mode 2, is it worth measuring the end-to-end SQLCipher win
   first, so the effort is justified by a number rather than by arithmetic?

See also `taint_dit_placement.md` (placement state and gaps),
`taint_dit_cost_model.md` (the toggle/dwell terms and the `MRS` measurement),
`taint_dit_tailcall_gap.md` (the other DIT state-management bug).
