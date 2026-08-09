# CLAUDE.md

> **New here? Read `utils/taint_OVERVIEW.md` first** — the single consolidated
> entry-point doc (how it works, how to run it, limitations, current state). This
> file holds the authoritative operating instructions; the overview is the map.

This is an **LLVM fork** (branch `interproc_taint`) implementing **interprocedural
taint analysis + PSTATE.DIT hardening** for AArch64: secret data entry points are
declared in a taint-source file, taint is propagated through registers/stack/global
memory at the MIR level across all functions of a TU, and **PSTATE.DIT
(data-independent timing) mode switches** are inserted so secret-dependent code runs
with data-operand timing side channels suppressed.

**Threat model: data-operand instruction timing (DIT), NOT speculation.** An
ISB/DSB "speculation barrier" mode used to exist as a placeholder for the DIT toggle
mechanism; it was **removed on 2026-07-14**. Speculation defense is out of scope —
do not reintroduce it. Why taint at all, rather than DIT-everywhere: DIT is not free
(some SPEC 2026 benchmarks lose ~15% with it fully on), so it should cover only
secret-dependent code. See `utils/taint_dit_cost_model.md`.

## Build (IMPORTANT: never run builds yourself)

Do **not** invoke `ninja`/`cmake --build`. Builds are long; give the user the exact
command and ask them to run it and paste the output.

- Build dir: `build/` (Debug, all targets). Typical: `ninja -C build clang llc opt`
- All tools referenced below are `build/bin/...`

**macOS: create `build/bin/clang.cfg` after configuring a fresh build dir.** A
from-source clang does not infer the macOS SDK, so any `#include <stdio.h>` fails with
`'stdio.h' file not found`. Clang reads a default config file sitting next to the
binary, which fixes every invocation at once:

```
printf -- '-isysroot %s\n' "$(xcrun --show-sdk-path)" > build/bin/clang.cfg
```

`build/` is gitignored, so this must be redone per build dir. Verified not to disturb
the test suites (all 1403 `clang/test/Driver` tests pass with it present);
`--no-default-config` opts out of it for a single invocation. The alternative is
`-DDEFAULT_SYSROOT=$(xcrun --show-sdk-path)` at cmake time, or passing
`-isysroot $(xcrun --show-sdk-path)` on every command line. `utils/taint_harden_c.sh`
already auto-detects the SDK itself and needs none of this.

## How to run the pipeline

### Preferred: one-shot clang flag

```
build/bin/clang -O2 -ftaint-harden=<taint-src-file> -c file.c -o file.o
```

Flag absent ⇒ codegen byte-for-byte unchanged. Verify:
`build/bin/llvm-objdump -d file.o | grep -iE '\bmsr\b.*\bdit\b'`
(**`-i` is required** — objdump prints the operand uppercase, `msr DIT, #0x1`, so the
case-sensitive form silently reports zero on a correctly hardened object.)

### Protection: PSTATE.DIT (the only mode)

`-taint-insert-dit` is the master switch at the `llc` level (implied by
`-ftaint-harden`; `-mllvm -taint-insert-dit=0` next to it produces an unprotected
build with otherwise identical codegen, for A/B benchmarking). Without it, the
analysis still runs and the report files are still produced, but codegen is
untouched.

**Placement granularity (`-taint-dit-placement`): DEFAULT is now `region`
(fine-grain).** Region placement covers only the secret-dependent regions — clean
preambles and public loop scaffolding (coordinate/index math) stay DIT-off — tuned by
`-taint-dit-switch-cyc` (default 0 = finest), `-taint-dit-dwell-per-instr`, and
`-taint-dit-loop-hoist` (**default 0 = block-minimal**: DIT wraps only the blocks
containing a secret op, with per-iteration toggles around a need-block in a loop; set
`=1` to coarsen each need-loop On and hoist one enable to the preheader — the right
choice for serializing-switch hardware where per-iteration toggling is costly).
It carries a soundness verifier and falls back per-function
to whole-function coverage if it cannot prove coverage, so it is always safe. See
`utils/taint_dit_placement.md`. Requires FEAT_DIT (Armv8.4+) at run time — Apple
M-series has it (`sysctl hw.optional.arm.FEAT_DIT`), Neoverse N1 does not ⇒ SIGILL
there; verify via objdump/lit or `qemu-aarch64 -cpu max`.

**`-taint-dit-placement=function`** is the opt-in coarse policy: `MSR DIT, #1` at
entry of any function containing taint, `MSR DIT, #0` before each return. Whole-
function coverage avoids per-region toggles clearing an enclosing region's DIT
across calls; `MSR DIT, #1` is also re-asserted after every non-tail call site (a
callee may clear DIT on its exit — gap G1, fixed), except when the callee's
`PreservesDIT` summary bit proves the re-assert redundant (in-TU, uninstrumented,
only preserving calls). Secrets passed to external/indirect callees can't be
protected by placement — audit them with `-taint-callsite-report=<file>` (`ESCAPE`
lines). Remaining gaps + optimal-placement design in `utils/taint_dit_placement.md`;
measured costs (toggle ≈ 30 cyc; dwell is workload-dependent and real) in
`utils/taint_dit_cost_model.md`.

`-taint-region-merge-gap` and the coalesced "regions" in the reports **no longer
drive placement** — they feed the report files and are the input the planned
cost-model-driven region placement will consume.

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
-run-taint-interproc -taint-insert-dit -taint-region-merge-gap=2` → `llc
-start-after=prologepilog -filetype=obj`. Report files: `-taint-output`,
`-taint-regions-output`, `-taint-source-regions-output`,
`-taint-callsite-report` (secret-escape call sites; the clang flag doesn't emit
these), `-taint-dit-precision-report` (DIT accounting — need/underdit/collateral/
switches per function; reachable from clang as `-mllvm -taint-dit-precision-report=`). Region spacing: `utils/taint_region_distance.py OUT.hardened.mir`.

## Architecture

| Piece | Where |
|---|---|
| `taint-annotate` IR pass (marks `tainted`/`tainted-pointee` arg attrs from taint-src file) | `llvm/lib/Transforms/Instrumentation/TaintSourceAnnotator.cpp` |
| Interproc MIR taint analysis + DIT mode-switch insertion (`TaintInterprocPass`, new-PM module pass, post-prologepilog, cell-based memory taint, fixed-point over call graph, region merging; `insertTaintDITSwitches`) | `llvm/lib/CodeGen/TaintAnalysis.cpp`, `TaintFixedPointIteration.cpp` |
| `TaintSummaryInfo` — plain per-function summary map, owned and populated by `TaintInterprocPass` (there is no `taint-summary` *analysis*; one existed, always returned an empty summary, and was removed 2026-07-13) | `llvm/include/llvm/CodeGen/TaintSummaryInfo.h` (header-only) |
| Store payload classification (`getNumStoredValueRegs`) | `llvm/include/llvm/CodeGen/TargetInstrInfo.h`, `llvm/lib/Target/AArch64/AArch64InstrInfo.cpp` |
| Taint cl::opts (`-taint-insert-dit` etc. are `extern cl::opt` globals) | `llvm/include/llvm/CodeGen/TaintAnalysis.h` |
| `-ftaint-harden` flag + in-process 3-phase codegen (`RunTaintHardenCodegen`) | `clang/lib/CodeGen/BackendUtil.cpp`; flag in `clang/include/clang/Options/Options.td`, `clang/include/clang/Basic/CodeGenOptions.h`, forwarding in `clang/lib/Driver/ToolChains/Clang.cpp` |
| Firefox integration guide | `utils/taint_firefox_integration.md` |
| **Context-insensitive mod-sets — the dominant false-positive source** (measured on libsodium: 169 of 199 FPs). Also records that P1b is a far smaller lever than assumed: only 17 of 583 secret-writing call sites resolve provenance to an argument | `utils/taint_context_insensitivity.md` |
| Two spill soundness bugs fixed 2026-07-27 (`implicit-def` as use; narrowed reload) + what spilling *does* do correctly | `utils/taint_spill_soundness_bugs.md` |
| **DIT precision metric** (`-taint-dit-precision-report`): per function, how many instructions MUST run with DIT vs how many DO. `precision = need/underdit` is the number placement should maximize — but only against a switch budget, and always read the loop-weighted variant (unweighted, convolve's region placement looks 13 points better while being 7.16x slower) | `utils/taint_dit_precision.md`, `utils/taint_dit_precision.py` |
| **Tail-call DIT gap fixed 2026-08-05** — whole-function placement cleared DIT *before* a tail call, so the callee receiving the secret ran unprotected (found on libsodium `crypto_sign`). Also records the permanent residual: after a tail call DIT may stay set indefinitely, so **an instrumented function does not restore DIT on every exit path** | `utils/taint_dit_tailcall_gap.md` |
| **DIT callee-ownership rule, 2026-08-08.** "Only the frame that turned DIT on may turn it off." An instrumented callee used to clear DIT on exit, so callers re-asserted after *every* call and the switch count scaled with the CALL count. Shipped: `AlwaysEnteredWithDIT` fixes resolvable in-TU calls, and `-taint-dit-reassert-report=<file>` audits the rest. **Deferred (out of scope, for advisor discussion): the runtime `MRS` mode**, the only thing that fixes indirect *and* cross-TU calls. Measured: `MRS DIT` = 1.00 cyc vs `MSR DIT` = 30.34, so per call 90.67 -> 2.01 | `utils/taint_dit_callee_ownership.md` |
| `-taint-frame-addr-args` prototype (default OFF): the `f(&local_secret)` under-taint, measured cost | `utils/taint_frame_addr_fallback.md` |
| **libsodium/CIO head-to-head rig — SCRIPTED.** Build + seed + analyze + archives + `make check`; then the runtime A/B/D/E/C matrix. The old hand-built copies under `~/Documents/libsodium-stable/` and `~/Documents/cio/` were **lost** — do not look for them, run the scripts | `utils/taint_libsodium_eval.sh`, `utils/taint_libsodium_bench.sh` (work dir `~/Documents/libsodium-1.0.21/`; benchmark drivers `~/Documents/crypto-dit-benchmarks/`, untracked, `BENCH_DIR=` overridable) |
| **End-to-end runtime, MEASURED 2026-08-03 — a negative on libsodium.** Blanket DIT is free (1.00–1.02x); taint-driven placement costs +46%..+94% at the *shipped defaults*, ~half that when tuned for serializing switches. The defaults (`-taint-dit-switch-cyc=0`) assert toggles are free; on M4 they are ~30 cyc. No measured workload yet justifies fine-grained placement | `utils/taint_dit_cost_model.md` (table + caveats) |
| `TargetInstrInfo::insertTimingModeSwitch` hook (emits `MSR DIT`; the `insertInstructionBarrier`/`insertDataBarrier` ISB/DSB hooks were removed 2026-07-14) | `llvm/include/llvm/CodeGen/TargetInstrInfo.h`, `llvm/lib/Target/AArch64/AArch64InstrInfo.cpp` |
| DIT placement: state, gaps, optimal-placement design | `utils/taint_dit_placement.md` |
| **What PSTATE.DIT actually guarantees** (covered instruction set, exclusions = divide/sqrt, address-timing not covered). The `isDITProtected` membership hook is transcribed from this — keep in sync | `utils/taint_dit_spec.md` |
| **DIT cost model: toggle ≈ 30 cyc serializing (measured, M4); dwell up to ~15% on sensitive SPEC 2026 benchmarks.** Both terms matter — they pull opposite ways, and that tension *is* the placement problem. Read before any placement work; do NOT conclude "DIT is free" from the ~0 microkernels (blind spot, see the doc's History) | `utils/taint_dit_cost_model.md`, benchmarks in `playground/dit_bench/` |
| **Why this project exists (motivation literature):** non-crypto value-dependent-timing leaks. **The Apple M4 has a Load Value Predictor and DIT disables it (FLOP, USENIX Sec'25) — DIT-for-secret-regions is the paper's own recommendation, and whole-process DIT costs 4.5% on Speedometer, so fine-grained taint-driven placement is the cheap version.** Also: Firefox subnormal-FP pixel-stealing → integer "fix" → reopened by the LVP; THOR/AMX zero-skip demonstrated today | `utils/taint_value_timing_leaks_research.md` |
| Callee→caller taint through memory — **FIXED (blunt-TOP P0, 2026-07-15)** via `FunctionMemEffects` mod-set summary + caller-side `ExternalMemClobbered`; precision refinement (libc table, arg-i provenance) is P1 | `FunctionMemEffects` in `TaintSummaryInfo.h`, `computeFunctionMemEffects` in `TaintAnalysis.cpp`; literature/design `utils/taint_memory_summary_research.md`; repro `playground/callee_memory_gap.c` |
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
- **`MachineInstr::uses()` spans implicit DEFS.** It starts after the *explicit*
  defs, so `implicit-def $xN` (which every 32-bit AArch64 result carries) and a
  call's `$lr` clobber appear in it. Any "does this instruction read a secret?"
  walk MUST guard with `MO.isDef()` — check `isReg()` first, `isDef()` asserts on
  non-register operands. Without the guard an instruction that merely *overwrites*
  a tainted register looks like it *read* one and re-taints its own defs, so taint
  can never leave a register (fixed 2026-07-27, was inflating tainted-instruction
  counts by ~33%; test `taint-analysis-implicit-def.mir`).
- **A tail call is BOTH `isReturn()` and `isCall()`.** On AArch64 `TCRETURN*`
  satisfies both, so any "before every return" walk that tests `isReturn()` first
  will also fire on a tail call — which is where the caller hands its secret
  arguments to the callee. That is exactly how whole-function placement came to
  clear `MSR DIT, #0` immediately before `b crypto_sign_ed25519`, running the
  whole signing operation unprotected (fixed 2026-08-05, test
  `taint-analysis-tailcall.mir`). A tail call has no instruction after it at which
  state could be restored, so it needs its own case, not the return case.
  `utils/taint_dit_tailcall_gap.md`.
- **Memory cells are keyed `(FI/GV, offset, size)`; the READ path must test
  OVERLAP, not equality.** Spilling 8 secret bytes and reloading the low 4 from the
  same slot used to miss the cell entirely and return the secret as public — an
  under-taint. The CLEAR path deliberately stays exact-match (widening a clear
  would drop taint a partial public store never overwrote). Fixed 2026-07-27; test
  `taint-analysis-stack-partial-reload.mir`. Both bugs and what spilling *does*
  do correctly: `utils/taint_spill_soundness_bugs.md`.
- **Callee→caller taint through memory — FIXED (blunt-TOP P0, 2026-07-15).** Was: a
  callee writing a secret into caller-visible memory (`copy(buf, secret)`, `memcpy(...)`)
  left the caller's reload untainted. Now `FunctionTaintSummary` carries a
  `FunctionMemEffects` mod-set (`WritesSecretToGlobal` set + `WritesSecretToUnknown`
  TOP bit), computed by `computeFunctionMemEffects` in the interproc fixed point. At a
  call the caller applies it: a direct in-TU callee contributes its precise mod-set; an
  **external decl or indirect call receiving a secret is blunt TOP**. TOP sets the
  caller-state `ExternalMemClobbered`, which poisons every subsequent stack/global/heap
  load. Own-frame (non-fixed FrameIndex) callee writes are ignored (caller-invisible) —
  that is the precision. **P0 is deliberately blunt: no arg-i or per-offset precision,
  weak updates only, every truncation → TOP.** Measured cost on `firefox_convolve_int`
  at -O2: 1 → 2 instrumented functions (`run_kernel_int` newly instrumented via
  `convolve_pixel_int`'s TOP mod-set), `isb=0 dsb=0`, checksum unchanged. Repro
  `playground/callee_memory_gap.c` now protected. **P1 (deferred, pending more numbers):**
  libc model table + `memory(argmem: write)` narrowing + arg-i provenance to cut TOP.
  Design/literature: `utils/taint_memory_summary_research.md`.
- Also fixed on the way (latent, independent of the callee bug): the stack load path
  never consulted any poison bit, so an unknown-size store followed by a known-size load
  under-tainted. `ExternalMemClobbered` on the stack path closes it.

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
per-symbol DIT placement between the clang flag and the wrapper — they must match
exactly. Under the **default `region` placement** a tainted function's clean
preamble is DIT-off and the `msr DIT, #0x1` sits at the loop preheader, not the entry
(add `-mllvm -taint-dit-placement=function` for the old whole-function reference: one
`msr DIT, #0x1` at entry, one `msr DIT, #0x0` before each return).

**Exception since 2026-08-08 — a function that is `AlwaysEnteredWithDIT` gets
whole-function coverage even under `region`, and that is correct, not a
regression.** It was entered with DIT already on, so it does not own DIT and may
not clear it; region placement narrows *by clearing*, so narrowing there would
strip the caller's protection. Nothing is actually lost: the caller's region
already covered the callee, so those "narrowed" stretches were running DIT-on
regardless. In `firefox_convolve_int.c` this applies to `convolve_pixel_int`
(internal, address-never-taken, sole call site passes the pointee-tainted
`source`), so expect **one entry enable and no preheader enable** there;
`run_kernel_int` is unaffected and still shows the preheader form. `-debug-only=
taint-interproc` prints which functions took this path. See
`utils/taint_dit_callee_ownership.md`. **No `isb`/`dsb`
anywhere** in either mode (the ISB/DSB mode was removed 2026-07-14 — its old
expectation was 14 ISB + 14 DSB). Count mnemonics, not `grep` hits on the objdump
output — file paths themselves often contain "isb"/"dit" and inflate naive counts.

`playground/firefox_convolve_int.c` is a good *correctness* reference but a **bad
performance benchmark for placement**: it is DIT-insensitive (whole-program DIT =
0.968x), so it cannot show a placement win. See `utils/taint_dit_cost_model.md`.

All 28 tests pass as of 2026-08-08 (and the whole `llvm/test/CodeGen/AArch64`
suite: 3894 discovered, 3890 pass, 4 pre-existing XFAIL, 0 failures). Recently
added:
- `taint-analysis-dit-caller-preserve.mir`, `taint-analysis-dit-reassert-report.mir`
  (2026-08-08) - the DIT callee-ownership rule and the re-assert audit report.
  The first was committed XFAIL and verified to fail against the pre-fix compiler
  on exactly its two intended checks before the fix landed. Both carry positive
  controls, because the obvious wrong implementations ("never clear anything",
  "report every call site") would otherwise pass. See
  `utils/taint_dit_callee_ownership.md`, which also records the deferred `MRS`
  mode for indirect and cross-TU calls.
- `taint-analysis-dit-precision.mir` (2026-08-06) — the DIT accounting numbers.
  Pins a non-obvious one: under whole-function placement `coverage` is NOT 100%,
  because `MSR DIT, #0` precedes the return so the return runs with DIT off.
- `taint-analysis-tailcall.mir` (2026-08-05) — the tail-call DIT gap in the
  gotchas above. Verified to FAIL against the pre-fix `llc` (`CHECK-NOT:
  MSRpstateImm4 26, 0` matched, i.e. the clear was being emitted before
  `TCRETURNdi`) and to pass after. `taint-analysis-dit-region.mir` gained the
  matching `FUNC` checks: it already had a `tailcall_secret` case, but only
  `REGION` checked it, which is why the suite never saw the bug. See
  `utils/taint_dit_tailcall_gap.md`.
- `taint-analysis-implicit-def.mir`, `taint-analysis-stack-partial-reload.mir`
  (2026-07-27) — the two spill soundness bugs in the gotchas above. Each was
  verified to FAIL against the pre-fix code, not just to pass after it. See
  `utils/taint_spill_soundness_bugs.md`.
- `taint-analysis-store-pair.mir` (2026-07-13) — a secret in the *second* register
  of an `STP`/`STNP` store-pair; fails on any build that classifies stores by
  mnemonic prefix.

`taint-analysis-memory.mir` was also updated on 2026-07-27: a source-level local is
now resolved to its frame object (`getCellFromMMO`'s alloca case) and tracked as a
precise stack cell, where it previously fell into the unknown-memory set.

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
