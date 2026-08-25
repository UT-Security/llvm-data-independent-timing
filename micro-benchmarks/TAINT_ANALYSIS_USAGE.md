# Taint analysis quick start

Hands-on walkthrough. For the design, the measured results, and the complete flag and
format reference, see [`../docs/fences/README.md`](../docs/fences/README.md) - that is the
authoritative documentation, and this file is only the tutorial path through it.

## 1. Declare the secrets

Taint sources are function arguments, named in a CSV file:

```csv
# comments start with '#'; blank lines ignored
# FunctionName,ArgumentIndex   (0-based)
process_string,0
crypto_sign,4
```

Names are matched **exactly** against function definitions in the module, so C++ needs
mangled names, and a `static` function the optimizer renamed will not match.

## 2. Compile to IR with debug info

```bash
../build/bin/clang -S -emit-llvm -g -O2 hello.c -o hello.ll
```

`-g` is required: the reports resolve line numbers and re-read the original source file
to print the offending line. Without it you still get the tainted instruction list, just
with no line numbers and no source text.

**Use `-O2`, not `-O0`.** This matters more than it looks. At `-O0` clang spills every
argument to an `alloca` in the entry block, so the first user of a secret argument is a
`store` and the taint closure stops there. Measured on this very file with
`process_string,0`: `-O0` reports **1** tainted instruction and inserts **2** fences,
`-O2` reports **22** and inserts **24**. `mem2reg` does not help, because `-O0` also
marks functions `optnone`. A near-empty report on `-O0` IR is expected and means
nothing.

`make ir` now uses a separate `IRFLAGS` (`-O2`) for exactly this reason, kept distinct
from `CFLAGS` (`-O0`), which is the *timing* configuration for the harnesses and must
not be raised - the committed cycle counts were produced with it. Generated `.ll` files
are no longer committed: a checked-in copy shadowed the rule that builds it, so `make`
saw an up-to-date file and a stale `-O0` artifact quietly beat the corrected rule.

## 3. Run the analysis

```bash
../build/bin/opt -passes='print<ir-taint-analysis>' \
  -taint-sources-file=taint_sources.csv \
  -taint-output-file=sensitive_lines.txt \
  -taint-summary-file=summaries.csv \
  -disable-output hello.ll
```

Three outputs:

- **stderr/stdout report** - the taint source arguments, then every tainted instruction
  grouped by function with its source line, then the `SensitiveInsts` list, then a
  deduplicated list of tainted source lines.
- **`-taint-output-file`** - one line per unique `(file, line, function)`. Opened in
  **append** mode, so a whole-library build accumulates into one file and re-running on
  the same module duplicates entries. Delete it between runs.
- **`-taint-summary-file`** - `function_name,tainted_arg_indices,returns_taint`,
  truncated each run. This is the input to the machine-level pass.

`sensitive_lines.txt` in this directory is the committed output for `hello.c` with
`taint_sources.csv` (`process_string,0`); its `#` header lines were added by hand, the
pass emits data lines only.

## 4. Insert fences

```bash
../build/bin/opt -passes='taint-fence-insertion' \
  -taint-sources-file=taint_sources.csv \
  hello.ll -o hello.fenced.ll

grep -c 'fence seq_cst' hello.fenced.ll
```

Two `fence seq_cst` are inserted around **every** tainted instruction (PHIs and
terminators are skipped). This is deliberately maximal placement and it is expensive:
see [`../docs/fences/results/libsodium-fence-cost.md`](../docs/fences/results/libsodium-fence-cost.md)
for the 90x measurement on Ed25519 before using it on anything real.

The fence pass writes the same two report files as the printer if you pass the flags, so
you do not need a separate analysis run to get them.

## 5. Machine-level report (optional)

Needs `summaries.csv` from step 3:

```bash
../build/bin/llc -enable-taint-pruning \
  -taint-summary-file=summaries.csv \
  -taint-leaky-insts-file=leaky.csv \
  hello.ll -o hello.s
```

Output is `filename,line,function,leak_type,instruction`, written at process exit. Note
that this pass reports **every load** in a summarized function whether or not it is
tainted, so the row count is not a leak count. Read
[`../docs/fences/design/machine-taint-pruning.md`](../docs/fences/design/machine-taint-pruning.md)
before interpreting it.

## 6. Inside clang's pipeline

The analysis printer can be attached to clang's default pipeline with `-mllvm` flags.
There is no clang driver flag, and this does not run at `-O0`:

```bash
../build/bin/clang -O2 -g -mllvm -enable-taint-analysis \
  -mllvm -taint-sources-file=taint_sources.csv \
  -mllvm -taint-summary-file=summaries.csv -c hello.c -o hello.o
```

## 7. Benchmarks in this directory

| target | what it builds |
|---|---|
| `make all` | harnesses against the system libsodium (`-lsodium`) |
| `make unfenced` | harnesses against our bitcode libsodium, pass not run |
| `make fenced` | harnesses against our bitcode libsodium with fences inserted |
| `make ir` | `-O2 -g` LLVM IR for every `eval_*.c`, for the analysis (not committed) |

**The harnesses are x86-64 only.** `START_CYCLE_TIMER` / `STOP_CYCLE_TIMER` use
`rdtsc`/`rdtscp` with `cpuid` serialization and `%rax`-`%rdx` clobbers, so they do not
compile for AArch64 at any optimization level. To emit IR on an arm64 host, cross-target:

```bash
make ir XTARGET='--target=x86_64-apple-macos13' SYSROOT="-isysroot $(xcrun --show-sdk-path)"
```

Both variables default to empty, so a Linux x86-64 build is unaffected.

Each harness times one cryptographic operation with `cpuid`/`rdtsc` ... `rdtscp`/`cpuid`
and writes one cycle count per iteration:

```bash
./eval_ed25519_unfenced 1000 25 "test message" unfenced_output.txt
./eval_ed25519_fenced   1000 25 "test message" fenced_output.txt
```

`LIBSODIUM_DIR` in the `Makefile` is hard-coded to a Linux absolute path and the
bitcode-build scripts for the two libsodium archives are **not** in this repository, so
`fenced`/`unfenced` need that flow rebuilt first.

## 8. Building the compiler

```bash
ninja -C ../build                        # or: ninja -C ../build LLVMAnalysis LLVMCodeGen LLVMScalarOpts
../build/bin/llvm-lit ../llvm/test/Analysis/IRTaintAnalysis/
```

## Troubleshooting

**"Warning: No taint sources identified"** - the CSV was not passed, a function name
does not match the IR exactly, or the named function is only a declaration in this
module. Check the actual names with `grep "^define" hello.ll`.

**"Warning: Argument index N out of range"** - the index is 0-based and must be less
than the function's arity.

**No line numbers** - recompile with `-g`, and make sure the source file is still at the
path recorded in the debug info, since the report re-reads it from disk.

**Report file keeps growing** - `-taint-output-file` is append-mode by design. Delete it
before a fresh run.

**Far more tainted instructions than expected** - expected behaviour, and the mechanisms
are enumerated in
[`../docs/fences/design/precision-and-soundness.md`](../docs/fences/design/precision-and-soundness.md).
The usual culprit is that every pointer argument at a tainted call site is tainted,
including format strings and other constants, whose uses then taint unrelated functions.

## References

- [`../docs/fences/README.md`](../docs/fences/README.md) - documentation index
- [`../docs/fences/reference/interfaces.md`](../docs/fences/reference/interfaces.md) - all flags,
  formats, and the `TaintInfo` API
- LLVM new pass manager: https://llvm.org/docs/NewPassManager.html
- LLVM IR reference: https://llvm.org/docs/LangRef.html
