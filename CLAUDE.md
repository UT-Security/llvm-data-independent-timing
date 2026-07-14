# CLAUDE.md

This is an **LLVM fork** (branch `interproc_taint`) implementing **interprocedural
taint analysis + speculative-execution barrier hardening** for AArch64: secret data
entry points are declared in a taint-source file, taint is propagated through
registers/stack/global memory at the MIR level across all functions of a TU, and
ISB/DSB barriers are inserted around secret-dependent regions.

## Build (IMPORTANT: never run builds yourself)

Do **not** invoke `ninja`/`cmake --build`. Builds are long; give the user the exact
command and ask them to run it and paste the output.

- Build dir: `build/` (Debug, all targets). Typical: `ninja -C build clang llc opt`
- All tools referenced below are `build/bin/...`

## How to run the pipeline

### Preferred: one-shot clang flag

```
build/bin/clang -O2 -ftaint-harden=<taint-src-file> -c file.c -o file.o
```

Flag absent ⇒ codegen byte-for-byte unchanged. Verify barriers:
`build/bin/llvm-objdump -d file.o | grep -E '\bisb\b|\bdsb\b'`

### Barrier modes

`-taint-barrier-mode={isb,dit}` (default `isb`; needs `-taint-insert-isb`, or via
clang add `-mllvm -taint-barrier-mode=dit` next to `-ftaint-harden`):
- `isb` — per-region ISB/DSB speculation barriers (transient-execution defense).
- `dit` — **function-granularity** PSTATE.DIT: `MSR DIT, #1` at entry of any
  function containing taint, `MSR DIT, #0` before each return. Defends
  data-dependent *instruction timing*, NOT speculation — different threat model.
  Requires FEAT_DIT (Armv8.4+) at run time; the dev machine (Neoverse N1) lacks
  it ⇒ SIGILL if executed locally; verify via objdump/lit or `qemu-aarch64 -cpu max`.
  Function granularity avoids per-region mode toggles clearing an enclosing
  region's DIT across calls; `MSR DIT, #1` is also re-asserted after every
  non-tail call site (a callee may clear DIT on its exit — gap G1, fixed),
  except when the callee's `PreservesDIT` summary bit proves the re-assert
  redundant (in-TU, uninstrumented, only preserving calls). Secrets passed to
  external/indirect callees can't be protected by placement — audit them with
  `-taint-callsite-report=<file>` (`ESCAPE` lines; works in both modes).
  Remaining gaps + optimal-placement design in `utils/taint_dit_placement.md`.

### Taint-source file format (one per line)

```
function_name,arg_index            # arg value is secret
function_name,arg_index,pointee    # pointer public, memory loaded through it secret
```
0-based indices; `#` comments; C++ needs **mangled** names.

### Wrapper / manual multi-tool flow (debugging, report files)

```
utils/taint_harden_c.sh --opt-level -O2 --region-merge-gap 2 playground/firefox_convolve_int.c
```
Taint source auto-detected as `<basename>_secret.txt`. Steps it performs: clang
`-emit-llvm` → `opt -passes=taint-annotate -taint-src=...` → `llc -stop-after=prologepilog`
→ perl strip of `<mcsymbol >` (MIR CFI serialization bug) → `llc -enable-new-pm
-run-taint-interproc -taint-insert-isb -taint-region-merge-gap=2` → `llc
-start-after=prologepilog -filetype=obj`. Report files: `-taint-output`,
`-taint-regions-output`, `-taint-source-regions-output`,
`-taint-callsite-report` (secret-escape call sites; the clang flag doesn't emit
these). Region spacing: `utils/taint_region_distance.py OUT.hardened.mir`.

## Architecture

| Piece | Where |
|---|---|
| `taint-annotate` IR pass (marks `tainted`/`tainted-pointee` arg attrs from taint-src file) | `llvm/lib/Transforms/Instrumentation/TaintSourceAnnotator.cpp` |
| Interproc MIR taint analysis + barrier insertion (`TaintInterprocPass`, new-PM module pass, post-prologepilog, cell-based memory taint, fixed-point over call graph, region merging) | `llvm/lib/CodeGen/TaintAnalysis.cpp`, `TaintFixedPointIteration.cpp` |
| `TaintSummaryInfo` — plain per-function summary map, owned and populated by `TaintInterprocPass` (there is no `taint-summary` *analysis*; one existed, always returned an empty summary, and was removed 2026-07-13) | `llvm/include/llvm/CodeGen/TaintSummaryInfo.h` (header-only) |
| Store payload classification (`getNumStoredValueRegs`) | `llvm/include/llvm/CodeGen/TargetInstrInfo.h`, `llvm/lib/Target/AArch64/AArch64InstrInfo.cpp` |
| Taint cl::opts (`-taint-insert-isb` etc. are `extern cl::opt` globals) | `llvm/include/llvm/CodeGen/TaintAnalysis.h` |
| `-ftaint-harden` flag + in-process 3-phase codegen (`RunTaintHardenCodegen`) | `clang/lib/CodeGen/BackendUtil.cpp`; flag in `clang/include/clang/Options/Options.td`, `clang/include/clang/Basic/CodeGenOptions.h`, forwarding in `clang/lib/Driver/ToolChains/Clang.cpp` |
| Firefox integration guide | `utils/taint_firefox_integration.md` |
| DIT placement: state, gaps, optimal-placement design | `utils/taint_dit_placement.md` |
| **DIT cost model: toggle ≈ 30 cyc serializing (measured, M4); dwell up to ~15% on sensitive SPEC 2026 benchmarks.** Both terms matter — they pull opposite ways, and that tension *is* the placement problem. Read before any placement work; do NOT conclude "DIT is free" from the ~0 microkernels (blind spot, see the doc's History) | `utils/taint_dit_cost_model.md`, benchmarks in `playground/dit_bench/` |
| **KNOWN UNSOUNDNESS** — callee→caller taint through memory (missing barrier); literature + design recommendation | `utils/taint_memory_summary_research.md`, repro in `playground/callee_memory_gap.c` |
| Tests | `llvm/test/CodeGen/AArch64/taint-analysis-*.mir`, `llvm/test/Transforms/TaintAnnotate/taint-annotate.ll` |
| Scratch experiments (not shipping code) | `playground/` |

### Why the 3-phase design (load-bearing constraints)

1. `TaintInterprocPass` needs **all MachineFunctions of the TU resident at once**;
   the legacy PM frees each MF after emission ⇒ must serialize to MIR text and
   reparse (MIRParser materializes all MFs together).
2. **AArch64 has no new-PM codegen pipeline** (`buildCodeGenPipeline` is
   X86/AMDGPU-only) ⇒ lowering/emission must use the legacy PM, driven via the
   process-global `start-after`/`stop-after` cl::opts (saved/restored RAII-style).

So `-ftaint-harden` runs: (1) legacy PM `stop-after=prologepilog` → in-memory MIR
text (+ `<mcsymbol >` strip); (2) MIR reparse + new-PM `TaintInterprocPass` →
hardened MIR text; (3) legacy PM `start-after=prologepilog` → object.

## Constraints & gotchas

- **AArch64 only.** Analysis runs post-prologepilog (sees real stack offsets); that
  is why re-lowering uses `-start-after=prologepilog`.
- Interproc scope = one TU/module; cross-TU taint is not tracked — annotate the
  entry function in each TU receiving the secret.
- `-ftaint-harden` is incompatible with LTO for that TU (lowers to object eagerly).
- MIR round-trip landmines, all handled and verified: CFI `<mcsymbol >` (textual
  strip); call-site debug info `callSites:` block-number-vs-layout-position mismatch
  (fixed at root in `llvm/lib/CodeGen/MIRParser/MIRParser.cpp` `parseMachineInst` —
  resolves via `PFS.MBBSlots`); jump tables are safe (compression/relaxation rerun
  in phase 3). `-g -O2 -ftaint-harden` is fully supported: barriers identical to the
  non-debug build, `llvm-dwarfdump --verify` clean, `DW_TAG_call_site` preserved.
- `taint-annotate` runs at the **OptimizerLast** extension point so attributes
  survive the -O2 middle-end.
- **Never classify instructions by mnemonic string.** A store's payload-register
  count used to come from `TII->getName(...).starts_with("STP")`, which silently
  missed `STNP*`/`STGP*`/`STXP*`/`STLXP*` (all store-pairs) and so never examined
  their *second* value register — an under-taint, i.e. a missing barrier. It is now
  the `TargetInstrInfo::getNumStoredValueRegs` hook (opcode switch in
  `AArch64InstrInfo`), which returns `std::nullopt` for shapes it cannot classify so
  callers over-approximate. Beware `STGPostIndex`: that is `STG` + `PostIndex`, a
  *single* tag store, not a pair — exactly the trap a prefix test falls into.
- Taint over-approximation is always the safe direction: a spurious barrier costs
  performance, a missing one costs the secret. Any "can't classify" path must fall
  back to treating every register use as secret.
- **KNOWN UNSOUND (open, as of 2026-07-14): callee→caller taint through memory.**
  `FunctionTaintSummary` carries no memory-effects component, so a callee that writes a
  secret into caller-visible memory (`copy(buf, secret)`, `memcpy(dst, secret_src, n)`)
  leaves the caller's later reload untainted ⇒ **no barrier**. Affects external, indirect
  *and* plain in-TU direct calls — it is a summary-domain deficiency, not an
  external-code problem, and no amount of fixed-point iteration fixes it. Return values
  are fine (external/indirect returns are conservatively tainted when any arg is tainted).
  Repro: `playground/callee_memory_gap.c`. Literature review + recommended design (coarse
  alias-case-free mod-set summary; TOP default for unknown callees; libc model table):
  `utils/taint_memory_summary_research.md`.

### Internal structure (post-2026-07-13 cleanup)

- The three taint kinds (data / pointee / address) are parameterized by
  `TaintKind`, not written out longhand — `TaintState::regs(K)` selects the
  bitvector, and one `updateWithAliases` holds the subreg/superreg walk.
- Every consumer of a converged `TaintResult` goes through the single
  `replayTaint(MF, TR, TSI, AA, Post, Pre)` visitor, which hands out `TaintFacts`
  (the four booleans consumers actually need) rather than a per-instruction
  `TaintState` copy. Do not hand-roll another `TR.IN.find(&MBB)` replay loop — that
  is how the replay drifts out of step with `propagateTaintMI`.
- Report files go through `deriveReportPath` / `openTaintReport`
  (`TaintAnalysis.h`); `openTaintReport` returns null both for an unrequested
  report (empty path) and a failed open.

## Testing

```
build/bin/llvm-lit -sv llvm/test/CodeGen/AArch64/taint-analysis-*.mir llvm/test/Transforms/TaintAnnotate
```
End-to-end reference: harden `playground/firefox_convolve_int.c` and compare
per-symbol barrier placement between the clang flag and the wrapper — they must
match exactly. Expected at -O2 (as of 2026-07-13, commit b596a05): `isb` mode
emits 14 ISB + 14 DSB; `dit` mode emits one `msr DIT, #0x1` at entry and one
`msr DIT, #0x0` before the return. Count mnemonics, not `grep` hits on the
objdump output — the file paths themselves often contain "isb"/"dit" and inflate
naive counts.

All 12 tests pass as of 2026-07-13 (`taint-analysis-store-pair.mir` was added then,
covering a secret in the *second* register of an `STP`/`STNP` store-pair — it fails
on any build that classifies stores by mnemonic prefix).

The three previously-failing tests were fixed on 2026-07-13; the old note blaming
all three on stale CHECK strings was only right about one of them:
- `taint-analysis-memory.mir` — stale CHECK strings (`taint mem` → `taint unknown
  mem value ...`), refreshed.
- `taint-analysis-pointee-spill.mir`, `taint-analysis-region-bundling.mir` — the
  test MIR was **invalid** and llc aborted in the MachineVerifier before FileCheck
  ever ran. `ADDWri` takes `GPR32sp` and `STRXui`/`LDRXui` take `GPR64`, but the
  tests declared plain `gpr32`/`gpr64sp` vregs. Fixed by declaring the vregs
  `gpr32common`/`gpr64common`, which are subclasses of both the plain and `sp`
  variants and so satisfy every operand constraint. Watch for this when
  hand-writing MIR tests: a register-class mismatch fails as an llc abort with an
  empty FileCheck stdin, which looks nothing like a CHECK mismatch.
