# FastDIT compile-time scalability: the dispatch-loop wall

**RESOLVED 2026-08-10.** `quickjs.c` now compiles in **733 s (10.7x baseline)**,
down from "never finished" (killed at 23 min, then at 420 s). Three changes, all
in this doc's "fix directions"; 29/29 taint tests still pass. Jump to
"What actually fixed it" for which of them mattered.

**Date:** 2026-08-10. Found while trying to use QuickJS as the single-TU proxy for
a browser (see `docs/research/browser-history-leaks.md` step 2). This was the
first time the pass had been pointed at a real translation unit, and it did not
scale. Everything below up to "What actually fixed it" describes the state as
found; the fix follows.

## The measurement

Same taint source (one narrow entry point) for every file, `-O2`, hard 420 s cap:

| file | lines | funcs | base | taint | ratio |
|---|---|---|---|---|---|
| `cutils.c` | 633 | 29 | 0.85 s | 1.09 s | 1.3x |
| `dtoa.c` | 1620 | 16 | 1.82 s | 2.32 s | 1.3x |
| `libunicode.c` | 1910 | 32 | 2.56 s | 3.25 s | 1.3x |
| `libregexp.c` | 2529 | 19 | 2.62 s | 3.38 s | 1.3x |
| `quickjs-libc.c` | 4153 | 108 | 3.29 s | 4.92 s | 1.5x |
| **`quickjs.c`** | **54694** | **940** | **68.9 s** | **>420 s (killed; also killed at 23 min earlier)** | **TIMEOUT** |

So the overhead is a flat, cheap **1.3-1.5x up to ~100 functions**, then a cliff.
It is not a gentle superlinearity in TU size - it is one pathological function.

## The cause

`sample` on the running compiler puts **100%** of the stack here:

```
BackendConsumer::HandleTranslationUnit -> emitBackendOutput
  -> PassManager<Module> -> TaintInterprocPass::run
    -> TaintAnalysis::run(MachineFunction&, ...)      <- INTRAprocedural
      -> TaintState::join
        -> DenseSetImpl<const Value*>::insert -> DenseMapBase::grow
```

It is the **intra**procedural dataflow join, not the interprocedural fixed point
and not the MIR round-trip.

Why that function and not the others:

| function | basic blocks | max fan-in |
|---|---|---|
| **`JS_CallInternal`** | **1890** | **209** (`indirectgoto.backedge`) |
| `js_create_function` | 559 | 27 |
| `resolve_labels` | 550 | 47 |
| `js_parse_postfix_expr` | 313 | 86 |

`JS_CallInternal` is 38,532 bytes, **130x the median function** in the TU.
QuickJS dispatches bytecode with **computed goto**, so all ~209 opcode handlers
branch back to a single hub block. The fixed point in `TaintAnalysis.cpp:1505`
re-joins **every** predecessor from scratch on each visit:

```cpp
for (const MachineBasicBlock *P : B->predecessors()) {
  if (First) { NewIn = OUT[P]; First = false; }   // full TaintState copy
  else       { NewIn.join(OUT[P]); }              // 7 DenseSet merges each
}
```

and the hub is re-pushed whenever any of its 209 predecessors changes. Cost is
roughly blocks x fan-in x revisits, with a `TaintState` copy (7 `DenseSet` +
4 `SparseBitVector`) at every step. `TaintState::join` (`TaintAnalysis.h:190`)
merges element-by-element with no capacity reservation and no empty-source
early-out.

**This shape is not exotic - every bytecode interpreter looks like this**, and a
switch-based dispatch has the same fan-in. Any real-world C target with a big
dispatch loop will hit it.

## Fix directions as originally hypothesised

*(Kept for the record. The ranking below turned out to be wrong: #1 did almost
nothing and #3 was the one that mattered. See "What actually fixed it".)*

1. **RPO worklist order.** The worklist is currently LIFO
   (`WorkQ.pop_back_val()`). Reverse-post-order is the standard fix and usually
   cuts revisit counts by a large factor on reducible CFGs.
2. **Early-out on empty/equal state** in `join` and `operator!=`. With a narrow
   taint source most blocks carry no taint at all, so this may collapse most of
   the work. Cheap and safe.
3. **Avoid the full-copy per visit** - join into a reused buffer rather than
   `NewIn = OUT[P]` plus N joins; skip predecessors whose OUT did not change.
4. **Reserve capacity before bulk insert** in `join`.

**A size-based bail-out is NOT an acceptable fix here**, even though
over-approximation is normally the safe direction. Falling back to
whole-function coverage for huge functions would put DIT *on* `JS_CallInternal` -
which is precisely the hot public code the experiment needs DIT *off* for. It
would be sound but would guarantee a null result.

## Consequence for the benchmark plan

QuickJS is otherwise an excellent proxy: preliminary 4-rep read shows
**always-on DIT costs +5.27%** on Octane in `qjs` (vs 2.1-2.6% in the browsers),
because a pure interpreter is nearly all serial dispatch and pointer chasing. It
is a single TU, so whole-program taint needs no cross-TU story. The only thing in
the way is this compile-time wall.

Fixing it was worth doing on its own terms: "does it scale to real code" is a
question any reviewer asks, and before this the honest answer was "the first real
TU we tried did not finish".


---

## What actually fixed it

Three changes were made together, and only profiling separated them:

| change | verdict |
|---|---|
| `join`: early-out on bottom + `mergeSet` with `reserve` (`TaintAnalysis.h`) | **worked** - `TaintState::join` went from 100% of the profile to a minor component |
| RPO worklist order instead of LIFO (`TaintAnalysis.cpp`) | **little effect** - see below |
| **skip `propagateTaintMBB` when `IN` is unchanged** | **decisive** - this is what made it terminate |

**Why RPO did not help.** QuickJS dispatches with computed goto, which lowers to
`indirectbr`, making `JS_CallInternal`'s CFG **irreducible**: the ~209 opcode
handlers and the dispatch hub form a single ~1890-block SCC. RPO pays off by
visiting predecessors before successors, which an SCC that size makes impossible.
Inside one giant cycle RPO is close to an arbitrary order. It was kept anyway
(harmless, and it helps ordinary CFGs), but it is not what unblocked this.

**The decisive fix.** The loop re-ran the block transfer function on *every* pop:

```cpp
bool InChanged = (NewIn != IN[B]);
if (InChanged) IN[B] = std::move(NewIn);
TaintState NewOut = propagateTaintMBB(*B, IN[B], ...);   // ran unconditionally
```

The transfer is a pure function of `IN` and the block's instructions, so an
unchanged `IN` cannot produce a different `OUT`. On a hub with 209 predecessors
that is re-popped constantly, this meant walking thousands of instructions over
and over to recompute an identical result. Guarding it on
`InChanged || FirstVisit` is what took the compile from unbounded to 733 s.

`FirstVisit` is load-bearing, not decoration: without it a function whose state is
bottom everywhere would never propagate past the entry block, because `OUT` would
equal its zero-initialised value and successors would never be pushed. That is
the case the old `else if (InChanged)` push was covering.

**Phase breakdown for `quickjs.c`** (sampled across the compile): the MIR
print/parse round-trip cost about **90 s** - real but bounded, and *not* the
ceiling; it is gone from the clang path since 2026-08-30 (the pass runs in the
pipeline after PEI) and survives only in the `llc` wrapper. Everything past
that is the taint analysis.

## Still open: taint spread, which is a different problem

Compiling is not the same as producing a useful configuration. The feasibility
build used one narrow taint source (`js_string_repeat,1`) and produced
**13,222 `MSR DIT` instructions**, including **618 inside `JS_CallInternal`** -
the hot interpreter loop that the experiment needs to stay DIT-*off*.

So taint from a single builtin propagates back into its caller and then spreads
across the interpreter's generic value handling. This is the same shape as the
SQLCipher result (`docs/design/context-insensitivity.md`): over-approximation swallows
the program. Whether a *realistic* secret stays contained is the next question,
and `-taint-dit-precision-report` is the instrument. If it does not, fine-grained
placement on QuickJS degenerates to always-on plus toggle overhead - which would
itself be a publishable negative, but is not the result being hoped for.
