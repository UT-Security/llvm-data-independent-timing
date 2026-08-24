# MIR-level taint pruning (`MachineTaintPruning`)

Files: `llvm/include/llvm/CodeGen/MachineTaintPruning.h`,
`llvm/lib/CodeGen/MachineTaintPruning.cpp`
Pass name: `machine-taint-pruning` (`MachineFunctionPass`), enabled by
`-enable-taint-pruning`

A machine-level re-run of the taint analysis, seeded from the IR pass's function
summaries. It is **analysis and reporting only**: `runOnMachineFunction` returns
`false` and `getAnalysisUsage` calls `setPreservesAll()`. It inserts nothing and
mitigates nothing.

The point of having it is that the IR analysis cannot see the things codegen
introduces: register allocation, spills, stack slots, and the calling convention.
Whether a secret ends up in a register or spilled to the frame is only knowable here.

---

## 1. Where it runs

`TargetPassConfig::addMachinePasses()` adds it immediately after `addPreEmitPass()`,
guarded by the hidden `-enable-taint-pruning` flag, and before the IPRA
register-usage collection. So it sees **post-register-allocation,
post-prologue/epilogue MIR**: physical registers only, real frame indices, real
calling-convention live-ins.

## 2. Seeding: summaries to argument registers

`loadTaintSummaries()` parses the CSV written by the IR pass
(`-taint-summary-file`, format `function_name,arg0;arg1,returns_taint`) into a
process-static `StringMap`. It runs once per process, on first invocation.

Only functions whose summary line has a **non-empty** tainted-argument list are stored.
`runOnMachineFunction` looks the current function up by name and returns immediately if
it is absent, so the pass is a no-op on every function the IR analysis did not mark.
A function that only *returns* taint never appears in the map and is never analyzed.

`initializeTaintFromSummary()` then turns argument *indices* into registers:

- **x86-64** (`Triple::isX86() && isArch64Bit()`): index `i` is mapped to the `i`-th
  System V integer argument register by **name lookup** over
  `{RDI, RSI, RDX, RCX, R8, R9}`, matched against `TRI->getName()` for each entry in
  `MachineRegisterInfo::liveins()`. Indices `>= 6` (stack-passed arguments) are logged
  and **not tracked**.
- **every other target**: if the function has any tainted argument, **all** live-ins
  are marked tainted. This is the conservative fallback, and it is the only path for
  AArch64.

The x86-64 mapping is positional over *all* arguments, so it silently mismatches
whenever an argument does not consume an integer register slot. `f(double x, void *p)`
passes `p` in `RDI`, but `p` is index 1, so the pass seeds `RSI`. See
`precision-and-soundness.md` §3.

## 3. Propagation

`propagateTaint()` maintains two function-wide sets: `DenseSet<Register> TaintedRegs`
and `DenseSet<int> TaintedFrameIndices`. It iterates all blocks in layout order,
repeatedly, until no set changes, capped at **100 iterations**.

Per instruction (debug instructions skipped):

- if any register **use** operand or any frame-index operand is tainted, then every
  register **def** operand becomes tainted;
- additionally, if that instruction `mayStore()`, every frame-index operand it names
  becomes tainted (secret spilled or stored to the frame);
- independently, if the instruction `mayLoad()` from a tainted frame index, all of its
  defs become tainted (secret reloaded).

Properties:

- **Flow-insensitive.** The sets are per function, not per program point. There is no
  meet over predecessors and no notion of "tainted here but not there". The iteration
  to a fixpoint is only there because layout order is not a valid dataflow order.
- **No kill.** A register redefined by a public value stays tainted for the rest of the
  analysis. This is the sound direction (over-approximation).
- **Whole-register granularity.** `DenseSet<Register>` has no subregister or aliasing
  awareness. Tainting `RAX` does not taint `EAX`, and a def of `EAX` does not taint
  `RAX`. On x86-64, where 32-bit subregister defs are everywhere, this is an
  under-approximation. See `precision-and-soundness.md` §3.
- **Memory beyond the frame is invisible.** Only frame indices are tracked, so a
  secret stored through a pointer to the heap is lost.
- The 100-iteration cap can in principle truncate the fixpoint on a very large
  function, which would also under-taint.

## 4. Reporting

`couldLeak(MI)` returns true if:

1. **any** operand of `MI` is a tainted register or tainted frame index, **or**
2. `MI.mayLoad()`, unconditionally.

Clause 2 means **every load in a summarized function is reported**, tainted or not, on
the stated grounds that memory taint cannot be tracked precisely. It dominates the
report volume.

`reportLeakingInstr()` records one row per hit, classifying `leak_type` in this
priority order:

| test (first match wins) | `leak_type` |
|---|---|
| `MI.mayStore()` | `store` |
| `MI.isConditionalBranch()` | `branch` |
| `MI.mayLoad()` | `load` |
| otherwise | `other` |

Because `mayStore` is tested first, a read-modify-write x86 instruction is classified
`store`, not `load`.

Rows accumulate in a process-static `std::vector<LeakyInst>` and are written to
`-taint-leaky-insts-file` by the destructor of a static `LeakyInstsWriter` object at
process exit. The CSV is:

```
filename,line,function,leak_type,instruction
```

with the instruction text quoted and newlines flattened to spaces. Nothing else is
escaped, so a comma inside the instruction text lands inside the quoted field but a
quote character would not be escaped.

## 5. Engineering caveats

All of the pass's cross-function state is **process-global static**:
`FunctionTaintSummaries`, `SummariesLoaded`, `LeakyInstructions`, `OutputWritten`. This
is fine for a one-shot `llc`/`clang -c` invocation and is what makes the whole-library
report accumulate correctly, but it means the pass is not safe under a
multiple-modules-per-process or multithreaded host, and the summary file is effectively
fixed for the lifetime of the process.
