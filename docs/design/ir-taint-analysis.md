# IR-level taint analysis (`TaintAnalysis`)

Files: `llvm/include/llvm/Analysis/TaintAnalysis.h`, `llvm/lib/Analysis/TaintAnalysis.cpp`
Pass names: `taint-analysis` (module analysis), `print<taint-analysis>` (printer)

This is the front end of the whole system: it decides which LLVM IR values in a module
are secret-dependent. Everything downstream (fence insertion, the MIR pass) consumes
its output.

---

## 1. Interface

```
opt -passes='print<taint-analysis>' \
    -taint-sources-file=sources.csv \
    [-taint-output-file=lines.txt] [-taint-summary-file=summaries.csv] \
    -disable-output input.ll
```

The analysis result object is `TaintInfo`. It is constructed once per module and does
all of its work in its constructor, so requesting the analysis runs it.

`TaintAnalysis::run` requests `CallGraphAnalysis` and passes the `CallGraph` into
`TaintInfo`. **The call graph is stored and never read.** Propagation walks LLVM's
use lists and call instructions directly rather than iterating the call graph, so
there is no SCC ordering and no separate treatment of recursion (see §4).

## 2. Taint sources

Sources are declared out of line, in a CSV file, one entry per line:

```
FunctionName,ArgumentIndex     # 0-based; '#' starts a comment; blank lines ignored
```

`identifySources()` walks every function definition in the module (declarations are
skipped), looks the *exact* symbol name up in the CSV map, and marks the named
arguments as sources. An index past the arity of the function produces a warning and
is ignored. If no source is found at all, the analysis warns and returns with an empty
result.

Consequences worth stating in a paper:

- Source declaration is **name-based and per-argument**, not type-based or
  annotation-based. There is no attribute, no pragma, and no clang driver flag.
- Because matching is exact-name, C++ requires mangled names, and static functions
  renamed by the optimizer (`.llvm.1234` suffixes) will silently fail to match.
- Only *arguments* can be sources. Globals, return values of external functions, and
  memory contents cannot be declared secret directly.

## 3. The lattice

`TaintLattice` is the two-point lattice `Notaint < Taint`:

| operation | definition |
|---|---|
| `join(a,b)` | `Taint` if either is `Taint` (least upper bound) |
| `meet(a,b)` | `Taint` only if both are `Taint` |
| `binop(a,b)` | `join(a,b)` |
| `unop(a)` | `a` |
| `constant()` | `Notaint` |

The lattice follows the BAP interval-checker formulation (`checker_taint.ml`), which is
where the naming comes from. There is no declassification element and no
"maybe-tainted" middle, so the analysis cannot express sanitization.

## 4. What propagation actually computes

This is the single most important thing to get right when describing the design,
because the header documents *two* rule sets and only one of them runs.

### 4.1 The rule that runs: forward use-list closure

`runAnalysis()` has three phases:

**Phase 1** - for each declared source argument, seed a worklist with that argument and
run `propagateTaint` to a fixpoint. The transfer function is:

> if `V` is tainted, then **every** `U` in `users(V)` is tainted.

There is no opcode discrimination at all. A value's users are tainted regardless of
which operand position they use it in. The only extra rule is that every `CallInst`
user is additionally handed to `handleCall` (§4.3), and that is done *even when the
call is already tainted*, because a second tainted actual argument at the same call
site still has to reach the callee's formal parameter.

Each newly tainted `Instruction` is also filed under its containing function in
`FunctionTaintedInsts`, which is what makes a function "touched".

**Phase 2** - every tainted instruction in every touched function is emitted twice:
once into `PotentiallyLeakingInsts` (the human report) and once into `SensitiveInsts`
with reason `TaintedValue`. **`SensitiveInsts` is therefore the set of all tainted
instructions**, which matters because that is exactly the list the fence pass consumes
(see `fence-insertion.md`).

**Phase 3** - `computeFunctionSummaries()` derives the per-function summaries that the
MIR pass consumes (§5).

### 4.2 The rule that does not run: the per-opcode transfer function

`computeInstructionTaint()` implements a conventional per-opcode transfer function:
binary ops join both operands, casts and unary ops preserve, `load` takes the taint of
its *pointer* operand, `getelementptr` joins base and all indices, `phi` joins all
incoming values, `select` joins condition and both arms, `icmp`/`fcmp` join both
operands, calls join all actual arguments, and anything else joins all operands.

`checkSensitiveInstruction()` likewise implements a leak classifier that distinguishes
`TaintedBranchCondition` (conditional `br` or `switch` on a tainted condition),
`TaintedLoadAddress`, `TaintedStoreAddress`, and `TaintedDivision`
(`udiv`/`sdiv`/`urem`/`srem` on tainted data).

**Neither function is called anywhere in the tree.** They are compiled, and
`SensitiveReason` has all five enumerators wired through
`sensitiveReasonToString()`, but the analysis populates `SensitiveInsts` exclusively
with `TaintedValue`. So in the shipped configuration:

- there is no distinction between a tainted branch and a tainted `add`;
- "sensitive" means "tainted", not "tainted *and* on a value-dependent-timing path".

This is the difference between the system as implemented and the system the headers
describe, and it is the direct cause of the fence cost reported in
`../results/libsodium-fence-cost.md`. Selective placement is the obvious next step and
the classifier for it already exists.

### 4.3 Interprocedural handling (`handleCall`)

At a call site where **any** actual argument is tainted, two things happen for every
argument position `i`:

1. **Conservative output tainting.** If the actual argument is pointer-typed and not
   already tainted, it is marked tainted and pushed on the worklist, on the theory that
   it may be an output buffer that receives secret data. This is the only mechanism by
   which secret flow *through memory* is recovered.
2. **Formal-parameter propagation.** If the actual argument is tainted and the callee
   has a body in this module and `i` is in range, the callee's formal parameter `i` is
   tainted and the callee is marked touched.

Indirect calls (`getCalledFunction()` returns null) get rule 1 only: no callee is
entered. Calls to declarations (external functions) likewise get rule 1 only.

The trailing loop in `handleCall` that scans the callee's `ret` instructions is a
no-op: it `break`s without recording anything. Return-value taint is nonetheless
handled, because the `CallInst` is a user of the tainted actual argument and so is
already tainted by the §4.1 rule. The consequence is coarser than a real summary: the
call result is tainted whenever *any* argument is tainted, whether or not the callee
actually returns secret-dependent data.

The analysis is **context-insensitive** (one taint state per function, no call
strings), **flow-insensitive** (use lists carry no program-point ordering, so a use
that executes *before* the call that taints a buffer is tainted too), and
**path-insensitive**.

### 4.4 Multiple sources and attribution

`TaintedValues` is shared across all sources, so Phase 1 does not re-explore a value
that an earlier source already tainted. The resulting *set* is unaffected by this
(the transfer function is monotone and the users of an already-tainted value were
already walked), but `ValueToSource` records only the **first** source that reached a
value. Per-source attribution in the reports is therefore first-writer-wins, not a
set of contributing sources. Do not present the "tainted by" column as a complete
provenance.

## 5. Function summaries (the IR to MIR interface)

`computeFunctionSummaries()` walks every function definition and records:

- `FunctionTaintedArgs[F]`: a `BitVector` over the formal parameters, bit `i` set if
  formal `i` is in `TaintedValues`. **Stored only if at least one bit is set.**
- `FunctionReturnsTaint[F]`: true if any `ret` returns a tainted value.

`writeFunctionSummaries()` serializes this as CSV:

```
# Function Taint Summaries
# Format: function_name,tainted_arg_indices,returns_taint
crypto_sign,4,true
```

with the argument indices `;`-separated. Only functions with a non-empty tainted-arg
set appear, so a function that merely *returns* taint is absent from the file
entirely, and the MIR pass will therefore never see it.

`TaintAnalysisWrapperPass` (an `ImmutablePass` holding a `TaintInfo` via
`setTaintInfo`/`getTaintInfo`) exists in the header and is `INITIALIZE_PASS`-registered,
and `initializeTaintAnalysisWrapperPassPass` is declared in `InitializePasses.h`.
Nothing constructs it and nothing calls the initializer. **The real IR-to-MIR handoff
is the CSV file, not this pass.** The wrapper is dead scaffolding for an in-memory
handoff that was never wired up.

## 6. Outputs

| Output | Written by | Mode | Content |
|---|---|---|---|
| stdout/stderr report | `print()` | - | source arguments; all tainted instructions grouped by function, each with debug line and the fetched source text; the `SensitiveInsts` list; a deduplicated list of tainted source lines |
| `-taint-output-file` | `writeSensitiveLinesToFile()` | **append** | one line per unique `(file, line, function)`: `file:line [function] tainted: callee(argN,...) \| <source text>` |
| `-taint-summary-file` | `writeFunctionSummaries()` | truncate | the CSV of §5 |

Notes:

- The report resolves source text by re-reading the original file off disk at the
  recorded debug line, so it needs `-g` and the sources still in place.
- `-taint-output-file` is opened with `OF_Append`, so successive compilations
  accumulate into one file (which is what a whole-library build wants) and repeated
  runs on the same module duplicate entries.
- `writeSensitiveLinesToFile` emits data lines only. The `#`-prefixed header in the
  committed `micro-benchmarks/sensitive_lines.txt` was added by hand.
- For call instructions the report names which callee argument positions were tainted,
  which is what makes the report usable for deriving the next iteration of the
  sources CSV.

## 7. Where this sits in the pipeline

Two entry points exist:

- **`opt`**: request `print<taint-analysis>` or run `taint-fence-insertion`.
- **clang's default pipeline**: `-enable-taint-analysis` (a `cl::opt` in
  `PassBuilderPipelines.cpp`) appends `TaintAnalysisPrinterPass(errs())` at the very
  end of `buildPerModuleDefaultPipeline`, after the annotation-remarks pass. It is
  reachable as `clang -O2 -mllvm -enable-taint-analysis -mllvm -taint-sources-file=...`.
  Because it hooks the *default* pipeline, it does not run at `-O0`.

There is **no clang driver flag**; nothing in `clang/` was modified.
