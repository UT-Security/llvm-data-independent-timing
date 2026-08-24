# Fence-based taint hardening: documentation index

> **This is one of two independent hardening lines in this repository.** The other is
> FastDIT ([`../README.md`](../README.md)): AArch64, taint propagated at the MIR level,
> mitigation by flipping the `PSTATE.DIT` mode bit. This line is x86-64, taint over IR
> use lists, mitigation by `seq_cst` fences. **No shared code**, and the two were
> developed on separate branches.
>
> Because both lines defined a class named `llvm::TaintAnalysis`, this line's was
> renamed to **`IRTaintAnalysis`** (pass `ir-taint-analysis`) when the branches were
> merged. The FastDIT line kept `TaintAnalysis`. Command-line flag names were not
> changed. If you are reading older notes that say `print<taint-analysis>` for this
> line, the current name is `print<ir-taint-analysis>`.

Taint-driven timing side-channel analysis and hardening, built as an LLVM fork.
Secrets are declared as function arguments in a CSV file; taint is propagated
interprocedurally across a module at the IR level; sequentially-consistent fences are
inserted around secret-dependent instructions; and a post-register-allocation machine
pass re-derives taint from per-function summaries and reports what it finds.

**Target: x86-64. Threat model: secret-value-dependent instruction timing. Speculation
is out of scope.** Nothing in `clang/` was modified; all flags are `-mllvm` flags.

## Read in this order

| # | Doc | Why |
|---|---|---|
| 1 | [overview.md](overview.md) | The map. What it is, the architecture, how to run it, the 90x result, what is and is not implemented. |
| 2 | [design/ir-taint-analysis.md](design/ir-taint-analysis.md) | The analysis itself, including which of the two documented transfer functions actually runs. |
| 3 | [results/libsodium-fence-cost.md](results/libsodium-fence-cost.md) | The measured cost, and the caveats that keep it honest. |
| 4 | [design/fence-insertion.md](design/fence-insertion.md) | The mitigation, and what a `seq_cst` fence does and does not buy. |
| 5 | [design/precision-and-soundness.md](design/precision-and-soundness.md) | The limitations section, over-approximations kept separate from soundness gaps. |
| 6 | [design/machine-taint-pruning.md](design/machine-taint-pruning.md) | The machine-level pass: why it exists, and why its results should not be trusted yet. |
| 7 | [reference/interfaces.md](reference/interfaces.md) | Every pass name, flag, file format, and API call. Look things up here. |

## Design and internals

- **[design/ir-taint-analysis.md](design/ir-taint-analysis.md)** - the module analysis.
  Two-point lattice; sources declared by `FunctionName,ArgumentIndex`; three phases
  (per-source closure, collect, summarize). **The live transfer function is "every user
  of a tainted value is tainted"** - the per-opcode rule set and the leak classifier in
  the same file are compiled and never called. Interprocedural flow enters direct
  callees with a body in the module and conservatively taints every pointer argument at
  a tainted call site, which is the only mechanism recovering secret flow through
  memory. The requested `CallGraph` is never read.
- **[design/fence-insertion.md](design/fence-insertion.md)** - the only transform. Two
  `fence seq_cst` (`mfence` on x86-64) around every tainted instruction, PHIs and
  terminators skipped, which means **secret-dependent branches are never fenced**. The
  placement is deliberately maximal, and that is the whole explanation for the measured
  cost.
- **[design/machine-taint-pruning.md](design/machine-taint-pruning.md)** - post-RA
  reporting pass, gated on `-enable-taint-pruning`, seeded from the summary CSV.
  Flow-insensitive over physical registers and frame indices, no kill. Reports every
  load in a summarized function unconditionally. x86-64 gets positional SysV argument
  register seeding; every other target falls back to tainting all live-ins.
- **[design/precision-and-soundness.md](design/precision-and-soundness.md)** - the
  limitations doc. Over-approximations (pointer tainting escaping through *constants
  and globals* is the notable one, and it can spread taint module-wide) are kept
  separate from soundness gaps (same-function flow through memory, indirect calls,
  stack-passed arguments, and x86-64 subregister aliasing).

## Results

- **[results/libsodium-fence-cost.md](results/libsodium-fence-cost.md)** - Ed25519
  signing, 1000 iterations, `rdtsc`/`rdtscp`: unfenced 45,676 median cycles, fenced
  4,110,384, i.e. **90.0x**. Also records what must not be read into the numbers (the
  1.31x system-vs-our-build gap is a build artifact), and what is missing for
  reproduction: the libsodium bitcode build scripts, the libsodium sources CSV, and the
  inserted-fence count. Five other cryptographic workloads have harnesses and no
  results.

## Reference

- **[reference/interfaces.md](reference/interfaces.md)** - pass names, the six
  command-line flags, all four file formats, the `TaintInfo` API, build and test
  commands, and the exact list of added and touched files.

## Where the non-doc material lives

| What | Where |
|---|---|
| The analysis | `llvm/lib/Analysis/IRTaintAnalysis.cpp`, `llvm/include/llvm/Analysis/IRTaintAnalysis.h` |
| The mitigation | `llvm/lib/Transforms/Scalar/TaintFenceInsertion.cpp` |
| The machine pass | `llvm/lib/CodeGen/MachineTaintPruning.cpp` |
| Tests | `llvm/test/Analysis/IRTaintAnalysis/basic.ll` (one test, source identification only) |
| Benchmark harnesses and Makefile | `micro-benchmarks/` |
| Committed cycle counts | `micro-benchmarks/{output,unfenced_output,fenced_output}.txt` |
| Functional demo and its report | `micro-benchmarks/hello.c`, `micro-benchmarks/sensitive_lines.txt` |
| Quick-start tutorial | `micro-benchmarks/TAINT_ANALYSIS_USAGE.md` |
