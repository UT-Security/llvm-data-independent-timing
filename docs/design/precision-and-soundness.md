# Precision and soundness: where the analysis over- and under-approximates

This is the doc to cite in a paper's limitations section. Every item below was derived
by reading the committed implementation; items marked **(unmeasured)** are predicted
consequences that have not been quantified on a workload yet.

A taint analysis used for *security* must over-approximate: missing a secret flow is a
missed leak. Several items here are under-approximations, which means the current
implementation is a detector with known blind spots, not a sound one.

---

## 1. Over-approximation in the IR analysis (false positives)

### 1.1 No operand discrimination

The transfer function is "every user of a tainted value is tainted"
(`ir-taint-analysis.md` §4.1). Position in the instruction is irrelevant, so
`store %public, ptr %secret_ptr` and `store %secret, ptr %public_ptr` are treated
identically, and an `icmp` that merely tests a tainted pointer against null is as
tainted as a multiply on the secret itself.

### 1.2 Tainting a pointer taints all of its uses, module-wide and without ordering

`handleCall` marks **every pointer-typed actual argument** at a call site tainted as
soon as **any** argument at that site is tainted, on the theory that it may be an
output buffer. The tainted value is then run through the same use-list closure, so:

- every load and store through that pointer becomes tainted, including accesses to
  unrelated fields of the same object;
- because use lists carry no ordering, uses that execute *before* the tainting call are
  tainted too;
- read-only inputs are tainted alongside genuine outputs.

### 1.3 Constants and globals can be tainted, which escapes the function

The pointer-tainting rule in §1.2 inserts the argument `Value` into `TaintedValues`
whatever it is, including a `GlobalVariable` or a constant expression. A call like
`printf(@.str.fmt, %secret)` therefore taints `@.str.fmt`, and the closure then taints
**every instruction in the module that uses that string constant**, in functions that
never touch the secret. Those functions become "touched" and appear in the reports.
**(unmeasured)** This is the most plausible single explanation for taint volume beyond
the secret's real dataflow, and it is a cheap fix: skip `Constant` operands in the
pointer-tainting rule.

A related inconsistency: `getTaint()` returns `Notaint` for any `Constant`, but
`isTainted()` reads the set directly and will answer true for a tainted constant. The
two queries disagree. Only `isTainted()` is on the live path.

### 1.4 Call results are tainted by arguments, not by the callee's behaviour

The call result is tainted whenever any argument is tainted, regardless of whether the
callee actually returns secret-dependent data. `FunctionReturnsTaint` is computed but
is not used to gate this.

### 1.5 Context-, flow- and path-insensitivity

One taint state per function; no call strings; no program-point ordering; no branch
conditions considered. A function called once with a secret and a hundred times with
public data is tainted everywhere.

### 1.6 No kill and no declassification

The lattice has no declassification element, and the closure is monotone. There is no
way to say "this value was secret and is now safe to publish" (a MAC comparison result,
a public ciphertext).

## 2. Under-approximation in the IR analysis (missed flows)

### 2.1 Secret flow through memory is only recovered at call boundaries

There is no memory model, no alias analysis, and no store-to-load tracking. The
sequence

```
store i64 %secret, ptr %slot     ; the StoreInst is tainted; %slot is NOT
%reload = load i64, ptr %slot    ; NOT tainted
```

loses the secret entirely inside a single function. The only path that recovers memory
flow is §1.2's pointer tainting at a call site, which is why the analysis behaves much
better on code that passes buffers to functions than on code that keeps secrets in
local structures. **This is the largest soundness gap in the IR analysis.**

### 2.2 Indirect calls do not enter the callee

`handleCall` propagates to formal parameters only when `getCalledFunction()` resolves.
An indirect call gets the pointer-tainting rule and nothing else, so a secret passed
through a function pointer is not followed. No devirtualization or call-graph
approximation is attempted, and the requested `CallGraph` is not consulted.

### 2.3 Calls to declarations do not propagate

Functions without a body in the module (anything not in the translation unit, or in the
linked bitcode) get the pointer-tainting rule only. The analysis is whole-module, not
whole-program, so its coverage is exactly as good as how much of the program was
combined into one module.

### 2.4 Sources are limited to named arguments

Secret globals, secret return values from external functions (`getrandom`, a KDF in
another library), and secret memory cannot be declared. Symbol-name matching also
breaks on C++ mangling mismatches and on internal-linkage renaming.

### 2.5 `memcpy` and other intrinsics get no special handling

An intrinsic call is just a call: the destination pointer is tainted by §1.2 (which
happens to be right for `memcpy`), but there is no modelling of the length, of partial
overwrites, or of intrinsics that should be treated as pure.

## 3. Soundness gaps specific to the MIR pass

These matter because the MIR pass is the only component that can see spills and the
calling convention, which is precisely where an IR-level analysis is blind.

### 3.1 Argument index to register mapping ignores types

The x86-64 seeding maps tainted argument index `i` to the `i`-th name in
`{RDI, RSI, RDX, RCX, R8, R9}`. That is only correct when every argument up to `i`
occupies an integer register slot. Floating-point and vector arguments consume XMM
slots, aggregates are split or passed by reference, and `sret`/`this` shift the
positions. In all of those cases the pass seeds the **wrong register** or none, and the
secret starts untainted. **(unmeasured)**

### 3.2 Registers have no subregister or aliasing awareness

`DenseSet<Register>` compares register numbers. Tainting `RAX` does not taint `EAX`,
`AX` or `AL`, and a 32-bit def of `EAX` does not taint `RAX`. On x86-64 32-bit
subregister defs are pervasive, so taint is expected to be dropped frequently. The fix
is to canonicalize through `TargetRegisterInfo` super-register classes or to iterate
`MCRegAliasIterator`. **(unmeasured, and the most likely reason the MIR results would
not reproduce the IR results.)**

### 3.3 Stack-passed arguments are not seeded

Arguments at index 6 and above are logged and skipped, so a secret in the seventh
parameter position enters the function untainted.

### 3.4 Only frame indices model memory

Heap and global memory are not tracked at all. A secret stored through a pointer to
heap memory and reloaded is lost. Frame-index tracking also has no offset or size
model, so a partially overlapping access to a tainted slot is not related to it.

### 3.5 The seeding fallback is not the analysis on non-x86-64

On every non-x86-64 target the pass marks **all** live-ins tainted, which combined with
the no-kill propagation taints essentially the whole function. The pass is only
meaningfully selective on x86-64.

### 3.6 `couldLeak` reports every load unconditionally

Clause 2 of `couldLeak` returns true for any `mayLoad()` instruction regardless of
taint. The reported set is therefore
`{instructions with a tainted operand} union {all loads in summarized functions}`. Any
precision number computed from that file must state which clause produced the hit.

## 4. Mitigation-level limitations

- Fences are placed around **all** tainted instructions, not a classified subset; the
  classifier exists and is not called (`fence-insertion.md` §2).
- PHIs and terminators are skipped, so a secret-dependent conditional branch, the
  canonical timing leak, never gets a fence.
- `fence seq_cst` is a memory-ordering barrier. It serializes, but it does not make a
  variable-latency instruction constant-time, does not remove a data-dependent cache
  access, and is not an architectural data-independent-timing mode
  (`fence-insertion.md` §3).
- Measured cost of the maximal placement is **90x** on Ed25519 signing
  (`../results/libsodium-fence-cost.md`).

## 5. The short version, if you need one paragraph

The implemented analysis is a **context-, flow- and path-insensitive forward closure
over LLVM's use lists**, seeded from named function arguments, with interprocedural
flow at direct call sites and with secret flow through memory recovered only by
conservatively tainting pointer arguments at tainted call sites. It over-approximates
badly in the pointer and constant directions and under-approximates through
same-function memory, indirect calls, and (in the machine-level pass) subregisters and
the calling convention. The mitigation it drives is maximal, unclassified
`seq_cst` fencing, whose measured cost bounds what an unselective placement can be
worth.
