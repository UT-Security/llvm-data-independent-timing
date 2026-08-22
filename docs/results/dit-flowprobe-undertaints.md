# Four under-taints, predicted from the code and confirmed at run time

**Measured 2026-08-21.** Rig `utils/dit_host_screening/xover/flowprobe.c`.
Compiler `~/Documents/llvm-project/build-gfix`.

Four channels carry a secret into a consumer that the analysis believes is
clean. Under `-taint-modset-callsite-gated` the consumer executes with
**`PSTATE.DIT = 0`** while computing on the secret. This is read from the
register at run time, not inferred from switch counts.

---

## 1. What was tested

Six functions with **byte-identical bodies** (`a = a*131 + p[i]` over 32 bytes),
reached by six different channels. One is the positive control.

| case | channel | ground truth |
|---|---|---|
| **P1** | secret passed directly as a `pointee` argument | must be instrumented |
| **P2** | four-level `noinline` call chain | must be instrumented |
| **C1** | callee **returns a pointer** to a buffer holding the secret | must be instrumented |
| **C2** | secret written to a **global**, read by a *sibling* with no call edge | must be instrumented |
| **C3** | secret stored through a pointer by **inline asm** | must be instrumented |
| **C4** | secret moved through a **NEON register tuple** (`vld2q`/`vst2q`) | must be instrumented |

Every function is `noinline`: at `-O2` the inliner otherwise deletes the call
edges before the MIR pass runs and the probe silently tests nothing.

## 2. Result

```
                                 DIT at entry
  p1_consume  (control)               1
  c1_consume  (returned pointer)      0   <-- secret runs unprotected
  c2_reader   (global, sibling)       0   <-- secret runs unprotected
  c3_consume  (inline asm store)      0   <-- secret runs unprotected
  c4_consume  (NEON tuple)            0   <-- secret runs unprotected
```

`p1_consume` reports `need=63 switches=2` in the precision report. The four
consumers do not appear in the report at all, i.e. **zero** switches for the
same 63 instructions.

**`-taint-modset-gate-strict` makes no difference** — configurations A (strict
on, the default) and B (strict off) are identical. The source condition does not
close any of these.

## 3. Why each one happens

| case | mechanism |
|---|---|
| **C1** | Return taint is **Data-only**. `functionReturnsTainted` tests `F.UsesData` and `taintCallResultDefs` sets only `TaintKind::Data`; there is no `ReturnsPointeeTainted` field in `FunctionTaintSummary`. A returned pointer *value* is a public address, so nothing transfers. |
| **C2** | Global taint lives in the per-function `TaintState`. The only cross-function carrier is a callee's `WritesSecretToGlobal` mod-set, applied **at that callee's call sites**. A sibling reader is analysed with its own entry state and never learns the global holds a secret. |
| **C3** | `INLINEASM` is not `isCall()` and carries no MMO, so neither the store handler nor the clobber path runs. The `"memory"` clobber is invisible to the analysis. |
| **C4** | `isSinglePhysReg` rejects register tuples (`$q0_q1`), so taint does not cross a tuple boundary in either direction. |

## 4. The part that took three attempts to measure — and the method rule

The first three probe designs all reported `DIT = 1` everywhere and would have
been written up as "no gap found". Each was masked by a *different* accidental
protection:

1. **`main` held the secret itself.** The test `main` built `sk` on its own
   stack, so it was tainted almost throughout and held DIT across every call.
   The consumers inherited it.
2. **A shared `sink` accumulator.** Secret-derived return values were
   accumulated into a global that `main` then read — genuine taint, correctly
   tracked, and it re-tainted `main`.
3. **The caller absorbed the callee's memory clobber.** With the mod-set applied
   unconditionally, `produce_all`'s clobber poisoned `main`, which then ran
   DIT-on across the call to the consumers.

Only after removing all three does the gap become visible, and it becomes
visible **only with the call-site gate enabled**, because the gate is precisely
what removes mask (3).

> **Method rule: an over-taint elsewhere can hide an under-taint here.**
> Whole-program "is DIT set?" is not a coverage test. Verify at the consumer,
> with the caller proven untainted, or a leak reads as protection.

This is the mirror image of [[dit-measurement-traps]] trap 8 — there, an
under-protecting oracle looked like a win; here, an over-protecting caller looked
like soundness.

## 5. What it means

**Without the gate these four cases are covered — but accidentally**, by the
blunt whole-memory clobber in the caller, not because the taint was tracked.
`main` is instrumented in the no-gate build and absent in the gated build; that
difference is the whole effect.

So the gate's ledger needs a third column. It is documented as buying ~50 points
of performance with "no coverage loss", measured by `ditSuppressed` on
libsecp256k1. That measurement stands — **but it was taken on a workload whose
secret does not travel by any of these four channels.** The right statement is
that the gate preserves coverage *for argument-carried taint*, which is what
libsecp256k1 uses.

**Open, and worth answering before the gate is defaulted on:** are any of these
four channels reachable in real crypto code? C1 (a function returning a pointer
into a secret buffer) and C3 (`asm volatile` as an optimisation barrier) are both
ordinary idioms in that setting.

## 6. Reproduce

```sh
cd ~/Documents/dit-crossover/flowprobe
clang -O2 -ftaint-harden=seed.txt -mllvm -taint-dit-loop-hoist=1 \
      -mllvm -taint-modset-callsite-gated flowprobe_dit.c -o fpA && ./fpA
```

`RESULT.txt` holds the gated and no-gate runs side by side.
