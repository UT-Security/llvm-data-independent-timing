# Fence insertion (`TaintFenceInsertion`)

Files: `llvm/include/llvm/Transforms/Scalar/TaintFenceInsertion.h`,
`llvm/lib/Transforms/Scalar/TaintFenceInsertion.cpp`
Pass name: `taint-fence-insertion` (module transform)

This is the only pass in the tree that *changes* code. It is the mitigation half of
the system.

---

## 1. What it does

```
opt -passes='taint-fence-insertion' -taint-sources-file=sources.csv input.ll -o out.ll
```

1. Request `TaintAnalysis` on the module (this is what actually runs the analysis).
2. Opportunistically re-emit the two report files if `-taint-output-file` /
   `-taint-summary-file` were given, so a build that only runs the transform still
   produces the reports. Both `cl::opt`s are defined in `TaintAnalysis.cpp` and
   `extern`-declared here, so the flag names are shared with the printer.
3. Take `TaintInfo::getSensitiveInstructions()`, copy the instruction pointers into a
   worklist first (to avoid iterator invalidation while inserting), and for each one:
   - skip it if it is a `PHINode` or a terminator, since there is no legal insertion
     point around those;
   - otherwise insert `fence seq_cst` **immediately before** it, and a second
     `fence seq_cst` **immediately after** it (`getNextNode()`, or before the block
     terminator if the instruction is last in its block).
4. Return `PreservedAnalyses` preserving only `CFGAnalyses`. No basic blocks are
   created or destroyed, so the CFG claim holds.

## 2. The mitigation is deliberately maximal

Two facts compose into the defining property of the current implementation:

- `SensitiveInsts` is the set of **all** tainted instructions, not a classified subset
  (see `ir-taint-analysis.md` §4.1-4.2).
- Every entry gets **two** `seq_cst` fences.

So the emitted code contains roughly `2 x |tainted instructions|` full barriers, and
tainted instructions include ordinary arithmetic that has no memory or timing
relevance at all.

`IRBuilder::CreateFence` defaults to `SyncScope::System`, and
`X86TargetLowering::LowerATOMIC_FENCE` lowers a system-scope `seq_cst` fence to
**`mfence`** on any subtarget that has it and does not set `avoidMFence`, falling back
to a locked stack operation (`lock or $0, (%rsp)`) otherwise. Either way each fence is
a full hardware barrier, so the machine-code cost is two barriers per tainted
instruction.

This is why the measured cost is 90x rather than a few percent
(`../results/libsodium-fence-cost.md`). The maximal placement is a property of the
implementation, not of the technique: the classifier needed to reduce it
(`checkSensitiveInstruction`, distinguishing tainted branches, tainted addresses and
variable-latency division) is already written and simply is not called.

## 3. What the fence does and does not address

`fence seq_cst` is a *memory ordering* barrier. It orders memory operations and, on
real hardware, has the side effect of draining the store buffer and serializing
execution around the fenced instruction. It does **not**:

- make a variable-latency instruction (integer divide, some SIMD paths) take constant
  time;
- remove a data-dependent cache access, so it does not close an address-timing
  channel;
- remove a secret-dependent branch;
- disable a value-dependent microarchitectural optimization the way an explicit
  data-independent-timing mode bit (ARM `PSTATE.DIT`, Intel DOIT) does.

What it buys is that the fenced instruction cannot overlap in the pipeline with its
neighbours, which suppresses timing variation that leaks *through overlap* rather than
through the instruction's own latency. State this precisely in a paper: the mitigation
here is coarse serialization, not a constant-time guarantee.

## 4. Known limitations

- **PHIs and terminators are skipped**, so a secret-dependent conditional branch (the
  canonical timing leak) is never fenced. Combined with §2 this means the current
  configuration fences the instructions least likely to matter and skips the one most
  likely to.
- **No fence coalescing.** Adjacent tainted instructions each get their own pair, so a
  run of `n` consecutive tainted instructions gets `2n` fences where `n+1` would give
  the same separation.
- **No cost model and no budget.** Placement is unconditional.
- **Atomic ordering is fixed** at `SequentiallyConsistent`; there is no flag to weaken
  it.
- The pass runs as a **module** pass over whatever IR it is handed, so its effect
  depends on where it is scheduled relative to the optimizer. Running it before the
  optimizer risks the fences constraining later transforms; running it after means the
  optimizer has already moved secret data around. The committed evaluation ran it over
  per-file bitcode (see `../results/libsodium-fence-cost.md` §2).
