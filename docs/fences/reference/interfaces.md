# Reference: passes, flags, file formats, API

Authoritative list of everything this fork adds to LLVM's surface. If a flag or format
is not here, it is not implemented.

---

## 1. Registered passes

| Name | Kind | Registered in | Notes |
|---|---|---|---|
| `ir-taint-analysis` | module analysis | `PassRegistry.def` (`MODULE_ANALYSIS`) | result type `TaintInfo`; runs in its constructor |
| `print<ir-taint-analysis>` | module pass | `PassRegistry.def` (`MODULE_PASS`) | prints the report, optionally writes both report files |
| `taint-fence-insertion` | module pass | `PassRegistry.def` (`MODULE_PASS`) | the only transform |
| `machine-taint-pruning` | `MachineFunctionPass` | `CodeGen.cpp` via `initializeMachineTaintPruningPass` | added in `TargetPassConfig::addMachinePasses()` after `addPreEmitPass()`, gated on `-enable-taint-pruning` |
| `ir-taint-analysis-wrapper` | `ImmutablePass` | `INITIALIZE_PASS` in `IRTaintAnalysis.cpp` | **unused**; nothing constructs it and its initializer is never called |

## 2. Command-line flags

All are LLVM `cl::opt`s, so under clang they need `-mllvm`. **There is no clang driver
flag; nothing in `clang/` was modified.**

| Flag | Defined in | Effect |
|---|---|---|
| `-taint-sources-file=<file>` | `IRTaintAnalysis.cpp` | CSV of taint sources. Without it the analysis warns and produces nothing. |
| `-taint-output-file=<file>` | `IRTaintAnalysis.cpp` | Sensitive-line report. Opened **append**. Written by both the printer and the fence pass. |
| `-taint-summary-file=<file>` | `IRTaintAnalysis.cpp` | Function taint summaries. Opened **truncate**. Written by the printer and the fence pass; **read** by the MIR pass. |
| `-taint-leaky-insts-file=<file>` | `MachineTaintPruning.cpp` | CSV of machine instructions the MIR pass reports. Flushed at process exit. |
| `-enable-taint-analysis` | `PassBuilderPipelines.cpp` | Appends `IRTaintAnalysisPrinterPass(errs())` to the end of `buildPerModuleDefaultPipeline`. Not active at `-O0`. |
| `-enable-taint-pruning` | `TargetPassConfig.cpp` (hidden) | Adds `machine-taint-pruning` to the codegen pipeline. |

## 3. File formats

### 3.1 Taint sources (input)

```
# comments start with '#'; blank lines ignored
FunctionName,ArgumentIndex        # 0-based index
crypto_sign,4
combine_inputs,0
combine_inputs,1
```

Names are matched **exactly** against function definitions in the module. C++ needs
mangled names. Out-of-range indices warn and are ignored. Malformed lines warn and are
skipped.

### 3.2 Sensitive lines report (`-taint-output-file`)

One line per unique `(file, line, function)`, data lines only (no header is emitted):

```
<file>:<line> [<function>] tainted: <callee>(arg<i>,arg<j>) | <source text>
```

The `tainted:` clause appears only for call instructions and names which actual
argument positions were tainted. `<indirect>` replaces the callee name when it does not
resolve. The `| <source text>` suffix appears only when the source file is still
readable at that path.

### 3.3 Function taint summaries (`-taint-summary-file`)

```
# Function Taint Summaries
# Format: function_name,tainted_arg_indices,returns_taint
crypto_sign,4,true
my_strcpy,0;1,false
```

Only functions with **at least one** tainted argument appear. Indices are
`;`-separated. This file is the IR-to-MIR interface.

### 3.4 Leaky machine instructions (`-taint-leaky-insts-file`)

```
filename,line,function,leak_type,instruction
crypto_sign.c,120,crypto_sign,load,"renamable $rax = MOV64rm ..."
```

`leak_type` is one of `store` / `branch` / `load` / `other`, tested in that order. The
instruction text is quoted and newlines are flattened to spaces; no other escaping is
performed.

## 4. Typical invocations

### Analysis only

```
build/bin/clang -S -emit-llvm -g -O0 example.c -o example.ll
build/bin/opt -passes='print<ir-taint-analysis>' \
  -taint-sources-file=sources.csv \
  -taint-output-file=sensitive_lines.txt \
  -taint-summary-file=summaries.csv \
  -disable-output example.ll
```

`-g` is required for line numbers and source text in the reports.

### Fence insertion

```
build/bin/opt -passes='taint-fence-insertion' \
  -taint-sources-file=sources.csv \
  example.ll -o example.fenced.ll
build/bin/llc example.fenced.ll -o example.fenced.s
grep -c mfence example.fenced.s          # sanity check that fences landed
```

### Analysis inside clang's default pipeline

```
build/bin/clang -O2 -g -mllvm -enable-taint-analysis \
  -mllvm -taint-sources-file=sources.csv \
  -mllvm -taint-summary-file=summaries.csv -c example.c -o example.o
```

### MIR-level reporting

Requires a summary file produced by a previous IR run:

```
build/bin/llc -enable-taint-pruning \
  -taint-summary-file=summaries.csv \
  -taint-leaky-insts-file=leaky.csv \
  example.ll -o example.s
```

### After optimization

```
build/bin/opt -passes='default<O2>' \
  -passes-ep-optimizer-last='print<ir-taint-analysis>' \
  -taint-sources-file=sources.csv -disable-output example.ll
```

## 5. C++ API

```cpp
#include "llvm/Analysis/IRTaintAnalysis.h"

TaintInfo &TI = MAM.getResult<IRTaintAnalysis>(M);

// value-level queries
TI.isTainted(V);                    // membership in the tainted set
TI.getTaint(V);                     // TaintLattice::Taint / Notaint; Constants -> Notaint

// result lists
TI.getSourceArguments();            // SmallVector<const Argument *>
TI.getPotentiallyLeakingInfo();     // SmallVector<PotentiallyLeakingInfo>
TI.getSensitiveInstructions();      // SmallVector<SensitiveInstInfo>  (all tainted insts)

// function summaries
TI.isArgTainted(F, I);
TI.doesReturnTaint(F);
TI.getTaintedArgs(F);               // BitVector over formals
TI.getFunctionsWithTaintedArgs();

// output
TI.print(OS);
TI.writeSensitiveLinesToFile(OS);
TI.writeFunctionSummaries(OS);
```

Note that `isTainted` and `getTaint` **disagree on constants**: `getTaint` short-circuits
`Constant` to `Notaint`, while `isTainted` reads the set, which can contain constants
(see `../design/precision-and-soundness.md` §1.3).

## 6. Build and test

```
ninja -C build                       # or: ninja -C build LLVMAnalysis LLVMCodeGen LLVMScalarOpts
build/bin/llvm-lit llvm/test/Analysis/IRTaintAnalysis/
```

Test coverage is one file, `llvm/test/Analysis/IRTaintAnalysis/basic.ll`: it writes a
two-line sources CSV, runs `print<ir-taint-analysis>`, and `FileCheck`s that the source
arguments are reported. It does **not** check propagation results, the fence pass, or
the MIR pass. Extending it is the cheapest available correctness work.

## 7. Files added to LLVM

```
llvm/include/llvm/Analysis/IRTaintAnalysis.h
llvm/lib/Analysis/IRTaintAnalysis.cpp
llvm/include/llvm/Transforms/Scalar/TaintFenceInsertion.h
llvm/lib/Transforms/Scalar/TaintFenceInsertion.cpp
llvm/include/llvm/CodeGen/MachineTaintPruning.h
llvm/lib/CodeGen/MachineTaintPruning.cpp
llvm/test/Analysis/IRTaintAnalysis/basic.ll
```

Touched upstream files (registration only): the three `CMakeLists.txt`,
`InitializePasses.h`, `CodeGen.cpp`, `TargetPassConfig.cpp`, `PassBuilder.cpp`,
`PassBuilderPipelines.cpp`, `PassRegistry.def`.
