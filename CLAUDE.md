# CLAUDE.md

> **New here? Read [`docs/README.md`](docs/README.md) first** - the documentation
> index, with a reading order and an annotated map of every design doc, measured
> result, and research note. This file holds the authoritative *operating*
> instructions; `docs/` holds the reasoning behind them.

This is an **LLVM fork** (branch `dit-tainter`) implementing **interprocedural taint
analysis + PSTATE.DIT hardening** for AArch64: secret data entry points are declared
in a taint-source file, taint is propagated through registers/stack/global memory at
the MIR level across all functions of a TU, and **PSTATE.DIT (data-independent
timing) mode switches** are inserted so secret-dependent code runs with data-operand
timing side channels suppressed.

**Threat model: data-operand instruction timing (DIT), NOT speculation.** An ISB/DSB
"speculation barrier" mode used to exist as a placeholder for the DIT toggle
mechanism; it was **removed on 2026-07-14**. Speculation defense is out of scope - do
not reintroduce it. Why taint at all, rather than DIT-everywhere: DIT is not free
(some SPEC 2026 benchmarks lose ~15% with it fully on), so it should cover only
secret-dependent code. See `docs/results/dit-cost-model.md`.

## Build

Running builds is fine. They are long, so start them in the background rather than
blocking on them, and do not run benchmarks while one is in flight.

- Build dir: `build/` (Debug, all targets). Typical: `ninja -C build clang llc opt`
- All tools referenced below are `build/bin/...`
- Touching `llvm/include/llvm/CodeGen/TaintAnalysis.h` rebuilds every taint TU and
  relinks `clang`/`llc`, which are large; expect the link to dominate.

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

Flag absent means codegen is byte-for-byte unchanged. Verify:
`build/bin/llvm-objdump -d file.o | grep -iE '\bmsr\b.*\bdit\b'`
(**`-i` is required** - objdump prints the operand uppercase, `msr DIT, #0x1`, so the
case-sensitive form silently reports zero on a correctly hardened object.)

### Protection: PSTATE.DIT (the only mode)

`-taint-insert-dit` is the master switch at the `llc` level (implied by
`-ftaint-harden`; `-mllvm -taint-insert-dit=0` next to it produces an unprotected
build with otherwise identical codegen, for A/B benchmarking). Without it, the
analysis still runs and the report files are still produced, but codegen is untouched.

**Placement granularity (`-taint-dit-placement`): DEFAULT is `region` (fine-grain).**
Region placement covers only the secret-dependent regions - clean preambles and public
loop scaffolding (coordinate/index math) stay DIT-off - tuned by
`-taint-dit-switch-cyc` (default 0 = finest), `-taint-dit-dwell-per-instr`, and
`-taint-dit-loop-hoist` (**default 0 = block-minimal**: DIT wraps only the blocks
containing a secret op, with per-iteration toggles around a need-block in a loop; set
`=1` to coarsen each need-loop On and hoist one enable to the preheader, the right
choice for serializing-switch hardware where per-iteration toggling is costly). It
carries a soundness verifier and falls back per-function to whole-function coverage if
it cannot prove coverage, so it is always safe. See `docs/design/dit-placement.md`.
Requires FEAT_DIT (Armv8.4+) at run time - Apple M-series has it
(`sysctl hw.optional.arm.FEAT_DIT`), Neoverse N1 does not, so SIGILL there; verify via
objdump/lit or `qemu-aarch64 -cpu max`.

**`-taint-dit-placement=function`** is the opt-in coarse policy: `MSR DIT, #1` at entry
of any function containing taint, `MSR DIT, #0` before each return. Whole-function
coverage avoids per-region toggles clearing an enclosing region's DIT across calls;
`MSR DIT, #1` is also re-asserted after every non-tail call site (a callee may clear
DIT on its exit - gap G1, fixed), except when the callee's `PreservesDIT` summary bit
proves the re-assert redundant (in-TU, uninstrumented, only preserving calls). Secrets
passed to external/indirect callees cannot be protected by placement - audit them with
`-taint-callsite-report=<file>` (`ESCAPE` lines).

`-taint-region-merge-gap` and the coalesced "regions" in the reports **no longer drive
placement** - they feed the report files and are the input the planned
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
`-emit-llvm`, `opt -passes=taint-annotate -taint-src=...`,
`llc -stop-after=prologepilog`, perl strip of `<mcsymbol >` (MIR CFI serialization
bug), `llc -enable-new-pm -run-taint-interproc -taint-insert-dit
-taint-region-merge-gap=2`, then `llc -start-after=prologepilog -filetype=obj`. Report
files: `-taint-output`, `-taint-regions-output`, `-taint-source-regions-output`,
`-taint-callsite-report` (secret-escape call sites; the clang flag does not emit
these), `-taint-dit-precision-report` (DIT accounting - need/underdit/collateral/
switches per function; reachable from clang as `-mllvm
-taint-dit-precision-report=`). Region spacing:
`utils/taint_region_distance.py OUT.hardened.mir`.

## Code map

| Piece | Where |
|---|---|
| `taint-annotate` IR pass (marks `tainted`/`tainted-pointee` arg attrs from taint-src file) | `llvm/lib/Transforms/Instrumentation/TaintSourceAnnotator.cpp` |
| Interproc MIR taint analysis + DIT mode-switch insertion (`TaintInterprocPass`, new-PM module pass, post-prologepilog, cell-based memory taint, fixed point over call graph, region merging; `insertTaintDITSwitches`) | `llvm/lib/CodeGen/TaintAnalysis.cpp`, `llvm/lib/CodeGen/TaintFixedPointIteration.cpp` |
| `TaintSummaryInfo` - plain per-function summary map, owned and populated by `TaintInterprocPass` (there is no `taint-summary` *analysis*; one existed, always returned an empty summary, and was removed 2026-07-13) | `llvm/include/llvm/CodeGen/TaintSummaryInfo.h` (header-only) |
| `FunctionMemEffects` mod-set summary (callee-to-caller taint through memory) | `TaintSummaryInfo.h`; `computeFunctionMemEffects` in `TaintAnalysis.cpp` |
| Store payload classification (`getNumStoredValueRegs`) | `llvm/include/llvm/CodeGen/TargetInstrInfo.h`, `llvm/lib/Target/AArch64/AArch64InstrInfo.cpp` |
| `TargetInstrInfo::insertTimingModeSwitch` hook (emits `MSR DIT`; the ISB/DSB `insertInstructionBarrier`/`insertDataBarrier` hooks were removed 2026-07-14) | `TargetInstrInfo.h`, `AArch64InstrInfo.cpp` |
| `isDITProtected` membership list - **keep in sync with `docs/reference/dit-spec.md`** | `AArch64InstrInfo.cpp` |
| Taint cl::opts (`-taint-insert-dit` etc. are `extern cl::opt` globals) | `llvm/include/llvm/CodeGen/TaintAnalysis.h` |
| `-ftaint-harden` flag + in-process 3-phase codegen (`RunTaintHardenCodegen`) | `clang/lib/CodeGen/BackendUtil.cpp`; flag in `clang/include/clang/Options/Options.td`, `clang/include/clang/Basic/CodeGenOptions.h`, forwarding in `clang/lib/Driver/ToolChains/Clang.cpp` |
| **SQLCipher, MEASURED 2026-08-12 (100 paired reps, M5) - a DEFINITIVE NEGATIVE: there is no headroom to recover.** With the oracle correctly wrapping all 3 provider entry points (cipher+kdf+**hmac**): libtomcrypt headroom **+0.89% +/-0.19**, OpenSSL (the DEFAULT shipping provider, hardware AES) headroom **-0.08% +/-0.38 = ZERO (48/100)**. Protecting the secret costs what protecting everything costs (oracle +7.85% vs blanket +8.81% on ltc; +1.87% vs +1.76% on OpenSSL) - almost all of always-on's cost is DIT **on the crypto**, which any correct placement must also pay. **A +8.15% 'first positive result' was reported earlier the same day and is RETRACTED** - the oracle had missed the per-page HMAC, so it was protecting less, not costing less. Also: on the OpenSSL build the pass **cannot instrument the AES at all** (it lives in prebuilt `libcrypto.dylib`) - 25 `MSR DIT` sites, zero on any cipher instruction, costing +2.27% for no protection. Software AES is DIT-expensive only because of its **T-table data-dependent loads**, whose real leak (cache timing) DIT does not even cover; hardware `AESE` is already constant-time. **AES is a bad motivating workload for this project** | `docs/results/sqlcipher.md` **gem5 corroboration 2026-08-13** (`gem5-DIT/docs/dit/studies/sqlcipher-dit-placement-2026-08-13.md`): running the identical binary under serializing vs renamed `MSR DIT` isolates **toggle cost with dwell held constant** - **+0.08% / +12.8% / +19.1%** for 6 / 54 / 63 switch sites, reproducing the M5 ordering and region:hoist ratio (1.49x vs 1.52x) at ~1/3 magnitude. The **prize is ~1.4%** (all of it EVES; DMP/SIP/comp-simp inert or negative here), so the shipped placement spends 19% to protect 1.4%. **Microbenchmarks overstate the prize ~200x** (`lvp_chase` 4.0x vs 1-2% real). **The MIR round-trip is a per-binary codegen LOTTERY** (+0.58% QuickJS, +0.06% native, **+2.65%** gem5, where the zero-DIT `nodit` control is the slowest binary in the table) - baselining against it is necessary but NOT sufficient |
| Tests | `llvm/test/CodeGen/AArch64/taint-analysis-*.mir`, `llvm/test/Transforms/TaintAnnotate/taint-annotate.ll` |
| Scratch experiments (not shipping code) | `playground/` |

### Why the 3-phase design (load-bearing constraints)

1. `TaintInterprocPass` needs **all MachineFunctions of the TU resident at once**; the
   legacy PM frees each MF after emission, so it must serialize to MIR text and
   reparse (MIRParser materializes all MFs together).
2. **AArch64 has no new-PM codegen pipeline** (`buildCodeGenPipeline` is X86/AMDGPU
   only), so lowering/emission must use the legacy PM, driven via the process-global
   `start-after`/`stop-after` cl::opts (saved/restored RAII-style).

So `-ftaint-harden` runs: (1) legacy PM `stop-after=prologepilog` to in-memory MIR text
(+ `<mcsymbol >` strip); (2) MIR reparse + new-PM `TaintInterprocPass` to hardened MIR
text; (3) legacy PM `start-after=prologepilog` to object.

## Constraints and gotchas

- **AArch64 only.** Analysis runs post-prologepilog (sees real stack offsets); that is
  why re-lowering uses `-start-after=prologepilog`.
- Interproc scope = one TU/module; cross-TU taint is not tracked - annotate the entry
  function in each TU receiving the secret.
- `-ftaint-harden` is incompatible with LTO for that TU (lowers to object eagerly).
- MIR round-trip landmines, all handled and verified: CFI `<mcsymbol >` (textual
  strip); call-site debug info `callSites:` block-number-vs-layout-position mismatch
  (fixed at root in `llvm/lib/CodeGen/MIRParser/MIRParser.cpp` `parseMachineInst`,
  resolves via `PFS.MBBSlots`); jump tables are safe (compression/relaxation rerun in
  phase 3). `-g -O2 -ftaint-harden` is fully supported: barriers identical to the
  non-debug build, `llvm-dwarfdump --verify` clean, `DW_TAG_call_site` preserved.
- `taint-annotate` runs at the **OptimizerLast** extension point so attributes survive
  the -O2 middle-end.
- **Never classify instructions by mnemonic string.** A store's payload-register count
  used to come from `TII->getName(...).starts_with("STP")`, which silently missed
  `STNP*`/`STGP*`/`STXP*`/`STLXP*` (all store-pairs) and so never examined their
  *second* value register - an under-taint, i.e. a missing barrier. It is now the
  `TargetInstrInfo::getNumStoredValueRegs` hook (opcode switch in `AArch64InstrInfo`),
  which returns `std::nullopt` for shapes it cannot classify so callers
  over-approximate. Beware `STGPostIndex`: that is `STG` + `PostIndex`, a *single* tag
  store, not a pair - exactly the trap a prefix test falls into.
- Taint over-approximation is always the safe direction: a spurious barrier costs
  performance, a missing one costs the secret. Any "cannot classify" path must fall
  back to treating every register use as secret.
- **`MachineInstr::uses()` spans implicit DEFS.** It starts after the *explicit* defs,
  so `implicit-def $xN` (which every 32-bit AArch64 result carries) and a call's `$lr`
  clobber appear in it. Any "does this instruction read a secret?" walk MUST guard with
  `MO.isDef()` - check `isReg()` first, `isDef()` asserts on non-register operands.
  Without the guard an instruction that merely *overwrites* a tainted register looks
  like it *read* one and re-taints its own defs, so taint can never leave a register
  (fixed 2026-07-27, was inflating tainted-instruction counts by ~33%; test
  `taint-analysis-implicit-def.mir`).
- **A tail call is BOTH `isReturn()` and `isCall()`.** On AArch64 `TCRETURN*` satisfies
  both, so any "before every return" walk that tests `isReturn()` first will also fire
  on a tail call, which is where the caller hands its secret arguments to the callee.
  That is exactly how whole-function placement came to clear `MSR DIT, #0` immediately
  before `b crypto_sign_ed25519`, running the whole signing operation unprotected
  (fixed 2026-08-05, test `taint-analysis-tailcall.mir`). A tail call has no
  instruction after it at which state could be restored, so it needs its own case, not
  the return case. See `docs/design/dit-tailcall-gap.md`.
- **Memory cells are keyed `(FI/GV, offset, size)`; the READ path must test OVERLAP,
  not equality.** Spilling 8 secret bytes and reloading the low 4 from the same slot
  used to miss the cell entirely and return the secret as public, an under-taint. The
  CLEAR path deliberately stays exact-match (widening a clear would drop taint a
  partial public store never overwrote). Fixed 2026-07-27; test
  `taint-analysis-stack-partial-reload.mir`. See
  `docs/design/spill-soundness-bugs.md`.
- **Callee-to-caller taint through memory is FIXED (blunt-TOP P0, 2026-07-15).**
  `FunctionTaintSummary` carries a `FunctionMemEffects` mod-set (`WritesSecretToGlobal`
  set + `WritesSecretToUnknown` TOP bit). At a call the caller applies it: a direct
  in-TU callee contributes its precise mod-set; an **external decl or indirect call
  receiving a secret is blunt TOP**, which sets `ExternalMemClobbered` and poisons every
  subsequent stack/global/heap load. Own-frame (non-fixed FrameIndex) callee writes are
  ignored (caller-invisible). **P0 is deliberately blunt: no arg-i or per-offset
  precision, weak updates only, every truncation goes to TOP.** P1 (libc model table,
  `memory(argmem: write)` narrowing, arg-i provenance) is deferred. Design and
  literature: `docs/research/memory-summaries.md`; repro
  `playground/callee_memory_gap.c`.

### Internal structure (post-2026-07-13 cleanup)

- The three taint kinds (data / pointee / address) are parameterized by `TaintKind`,
  not written out longhand - `TaintState::regs(K)` selects the bitvector, and one
  `updateWithAliases` holds the subreg/superreg walk.
- Every consumer of a converged `TaintResult` goes through the single
  `replayTaint(MF, TR, TSI, AA, Post, Pre)` visitor, which hands out `TaintFacts` (the
  four booleans consumers actually need) rather than a per-instruction `TaintState`
  copy. Do not hand-roll another `TR.IN.find(&MBB)` replay loop - that is how the
  replay drifts out of step with `propagateTaintMI`.
- Report files go through `deriveReportPath` / `openTaintReport` (`TaintAnalysis.h`);
  `openTaintReport` returns null both for an unrequested report (empty path) and a
  failed open.

## Testing

```
build/bin/llvm-lit -sv llvm/test/CodeGen/AArch64/taint-analysis-*.mir llvm/test/Transforms/TaintAnnotate
```

All 29 tests pass as of 2026-08-11. The whole `llvm/test/CodeGen/AArch64` suite was
last run clean on 2026-08-08 (3894 discovered, 3890 pass, 4 pre-existing XFAIL, 0
failures).

**End-to-end reference:** harden `playground/firefox_convolve_int.c` and compare
per-symbol DIT placement between the clang flag and the wrapper - they must match
exactly. Under the default `region` placement a tainted function's clean preamble is
DIT-off and the `msr DIT, #0x1` sits at the loop preheader, not the entry (add
`-mllvm -taint-dit-placement=function` for the old whole-function reference: one
`msr DIT, #0x1` at entry, one `msr DIT, #0x0` before each return).

**Exception since 2026-08-08 - a function that is `AlwaysEnteredWithDIT` gets
whole-function coverage even under `region`, and that is correct, not a regression.**
It was entered with DIT already on, so it does not own DIT and may not clear it; region
placement narrows *by clearing*, so narrowing there would strip the caller's
protection. Nothing is actually lost: the caller's region already covered the callee.
In `firefox_convolve_int.c` this applies to `convolve_pixel_int` (internal,
address-never-taken, sole call site passes the pointee-tainted `source`), so expect
**one entry enable and no preheader enable** there; `run_kernel_int` is unaffected and
still shows the preheader form. `-debug-only=taint-interproc` prints which functions
took this path. See `docs/design/dit-callee-ownership.md`.

**No `isb`/`dsb` anywhere** in either mode (the ISB/DSB mode was removed 2026-07-14).
Count mnemonics, not `grep` hits on the objdump output - file paths themselves often
contain "isb"/"dit" and inflate naive counts.

`playground/firefox_convolve_int.c` is a good *correctness* reference but a **bad
performance benchmark for placement**: it is DIT-insensitive (whole-program DIT =
0.968x), so it cannot show a placement win. See `docs/results/dit-cost-model.md`.

**Writing MIR tests:** a register-class mismatch fails as an llc abort in the
MachineVerifier with an empty FileCheck stdin, which looks nothing like a CHECK
mismatch. `ADDWri` takes `GPR32sp` and `STRXui`/`LDRXui` take `GPR64`; declaring vregs
`gpr32common`/`gpr64common` satisfies both the plain and `sp` variants. Tests that pin
a fix should be verified to **FAIL against the pre-fix build**, not just to pass after
it, and should carry positive controls where the obvious wrong implementation would
otherwise pass.
