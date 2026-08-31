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

- Build dir: `build/` (Debug, all targets). **Use `ninja -C build` with NO target
  list.** The taint analysis links into `clang`, `llc` AND **`libLTO.dylib`**, and
  `ninja -C build clang llc` leaves libLTO stale - the LTO link then silently runs the
  OLD analysis with no error. That cost two 50-minute measurement builds on 2026-08-30.
  A targeted build must name `LTO` explicitly:
  `ninja -C build clang llc LTO llvm-ar llvm-ranlib llvm-objdump`
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
`-taint-dit-switch-cyc` (**default 30** = the measured serializing switch cost),
`-taint-dit-dwell-per-instr` (default 1.0), and `-taint-dit-loop-hoist` (**default 1**:
each need-loop is coarsened On with one enable hoisted to the preheader; set `=0` for
block-minimal coverage, where DIT wraps only the blocks containing a secret op and a
need-block in a loop toggles every iteration). Both defaults were flipped on 2026-08-24
after measurement - the old `switch-cyc=0` asserted that toggles are free, which nothing
supports, and both flips widen coverage rather than narrowing it, so neither can
introduce a leak. It
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
`-taint-callsite-report=<file>` (`ESCAPE` lines). The same file carries `DITLEAK` lines:
functions that enable DIT and exit without clearing it, which is every tail call in an
instrumented function (a tail call is an exit with no epilogue, so there is nowhere to
put the clear - `docs/design/dit-tailcall-gap.md`). `DITLEAK return` is a placement bug and also
warns on stderr. **`DITLEAK tailcall` was an accepted cost and is now an ABI violation
to audit** - `docs/design/dit-abi.md` makes DIT callee-saved and disables tail calls
TU-wide, so the line should normally be absent; a surviving one means `musttail` or the
MachineOutliner.

The coalesced "regions" in the reports **do not drive placement** - they feed the report
files only. The gap that merges them was `-taint-region-merge-gap` until 2026-08-24 and
is now a fixed constant (2), because placement partitions BLOCKS and prices its own
merges with the frequency-weighted admission test.

**The call-site mod-set gate is ON by default** (since 2026-08-24), together with the
strict source condition and return-call-site gating, which are unconditional. It applies
a callee's memory clobber only at call sites that actually pass a secret: without it
`secp256k1_ecdsa_verify` carries 17 `MSR DIT` for public data and Bitcoin Core's
`ConnectBlockAllEcdsa` costs +51.20%; with it, +0.67%. The soundness claim is scoped -
*preserves coverage for argument-carried taint* - and `flowprobe` confirmed four channels
that escape it (returned pointer into a secret buffer, global read by a sibling with no
call edge, inline asm, NEON register tuple). `-taint-no-modset-gate` is the escape hatch.

### The DIT ABI (settled 2026-08-30) - `docs/design/dit-abi.md`

**To RUN it, read `docs/reference/dit-abi-runbook.md`** - build steps (including the
libLTO trap), the exact flags for LTO and non-LTO, how to read the counts, and what a
gem5 run sees.

**PSTATE.DIT is callee-saved.** An instrumented callee returns DIT exactly as it found
it at every exit it controls; a caller may rely on DIT never coming back lower than it
went in, so **call sites emit nothing**. That removes all four after-call re-assert
classes by construction, with no LTO and no annotation.

Landed: **`-ftaint-dit-abi` implies `-fno-optimize-sibling-calls`** (TU-wide).
**It is gated on the ABI flag, NOT on `-ftaint-harden`** - `disable-tail-calls` is
honoured by TailRecursionElimination too, so applying it to every hardened build turns
tail recursion into O(n) stack frames TU-wide, a stack-overflow hazard paid even with
the ABI off. A tail call is an exit with no epilogue, so the callee cannot restore there;
the per-function form is unavailable because the instrumented set is only known after a
post-PEI pass. `musttail` and `MachineOutlinerTailCall` survive the flag and show up as
`DITLEAK tailcall`, which is now a violation to audit rather than an accepted cost.

Landed and OPT-IN: **`-ftaint-dit-abi`** (the `-mllvm -taint-dit-abi` cl::opt still
exists for llc/A-B runs, but on its own it gives the ABI WITHOUT the tail-call disable,
which is an incomplete configuration - prefer the driver flag), the callee half - entry `MRS` into a
pre-PEI-reserved frame slot, a restore at each return, and **nothing at any call site**.
**Both placements are supported.** The restore form is chosen **per exit**, not per
placement: a guarded clear (`tbnz w, #24` over `msr DIT, #0`) where DIT is provably set
at that return, which is free when the function was entered with DIT already on; the
unconditional `msr DIT, Xt` otherwise, because a clear-only restore would return DIT
lower than entry and guarding an ENABLE is forbidden. Whole-function coverage always
takes the cheap form; so does a region-placed return inside an On block, which is the
common case.

**MEASURED 2026-08-31, region placement, M5** (`docs/results/dit-abi-measured.md`):
non-LTO 95 -> 57 switches for **no measurable time change**; full LTO
127,744 -> **15,462** (8.26x) for **-5.40% CoinSelection (25/25)** and **-8.52%
SignTransactionECDSA (27/30)**.

**Default stays OFF, and the reason is not the per-arm delta.** LTO with the ABI is
still slower than non-LTO WITHOUT it (+19.50% / +9.08%), so the ABI only helps in a
configuration nobody should pick on performance grounds, while the default
configuration gains nothing measurable and would pay shrink wrapping, a TU-wide
tail-call disable and a frame slot per function. **Use `-ftaint-dit-abi` if you are
already building with LTO; do not adopt LTO to get it.**

The predictor is **switches per instrumented function**, not the workload: 5.9
non-LTO (carrier costs back what the deleted re-asserts save) versus 51.1 under LTO
(deletion dominates by an order of magnitude).

**Two traps if you touch this.** `TBNZX` hard-codes b5=1, so `TBNZX ..., 24` tests bit
**56**, the guard never fires, and the function strips its caller - use `TBNZW` on the
32-bit subreg, and pin the register WIDTH in tests, because the asm printer shows the
raw operand either way. And any dataflow over the mode must use
`TII->getTimingModeStateAfter`, not `getTimingModeSwitch`: the latter reports
`std::nullopt` for the register-form restore, which reads as "no change" and models DIT
as still set.

New report: **`-taint-nonlocal-report=<file>`** lists the sites where the obligation
degrades to the guarantee and DIT is simply left set - `setjmp`, `musttail`, `unwind`,
and `noscratch`. All are dwell, never exposure.

### Taint-source file format (one per line)

```
function_name,arg_index            # arg value is secret
function_name,arg_index,pointee    # pointer public, memory loaded through it secret
```
0-based indices; `#` comments; C++ needs **mangled** names.

### Wrapper / manual multi-tool flow (debugging, report files)

```
utils/taint_harden_c.sh --opt-level -O2 playground/firefox_convolve_int.c
```
Taint source auto-detected as `<basename>_secret.txt`. Steps it performs: clang
`-emit-llvm`, `opt -passes=taint-annotate -taint-src=...`,
`llc -stop-after=prologepilog`, perl strip of `<mcsymbol >` (MIR CFI serialization
bug), `llc -enable-new-pm -run-taint-interproc -taint-insert-dit`, then
`llc -start-after=prologepilog -filetype=obj`. Report
files: `-taint-output`, `-taint-regions-output`, `-taint-source-regions-output`,
`-taint-nonlocal-report` (DIT obligation degrades to the guarantee),
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

- **A seed must name an entry point in EVERY TU the secret reaches, and an
  indirect dispatch table breaks propagation outright.** SQLCipher's arms carried
  taint on the cipher entry points only, so `sqlcipher_ltc_hmac` got zero switches
  and the per-page HMAC ran with DIT off - coverage topped out at 94.4-95.4% of a
  hand-placed oracle. The tell is exact: `hmac_init.o` and `sha512.o` were
  **byte-identical between the plain and instrumented builds**, i.e. the pass had
  nothing to do in them. Compare object hashes per TU when coverage looks short.
  Note the HMAC could not have been reached by propagation anyway: libtomcrypt
  calls the hash through `hash_descriptor[].process`, a function-pointer table, so
  `hmac_*` -> `sha512_*` needs its own seed line. Fix and measurements:
  `benchmarks/sqlcipher/ltc_hmac_seed.txt` in the gem5 tree, coverage 94.4% ->
  98.4%.
- **`region` is the right policy even on a TU that is secret work end to end - the
  old "use `-taint-dit-placement=function` there" advice was measured at
  `switch-cyc=0` and is OBSOLETE.** Re-derived 2026-08-27 from the existing
  `gem5-sqlc3` placement sweep (`~/Documents/dit-browser-bench/gem5-sqlc3/`,
  arms defined in `utils/dit_host_screening/sqlc_gem5.py`), overhead vs the
  `nodit` baseline at matched config and cache:

  | cache | `hmacfix` region, sw=0 | **`hmacsw30` region, sw=30 (shipped)** | `hmacfn` function |
  |---|---|---|---|
  | 16 serializing | 38.11% | **16.10%** | 17.25% |
  | 1024 serializing | 37.21% | **15.74%** | 16.89% |
  | 1792 serializing | 31.85% | **13.23%** | 14.75% |
  | 1920 serializing | 23.76% | **10.30%** | 10.42% |
  | 16 renamed | 4.55% | **1.17%** | 1.19% |
  | 1024 renamed | 3.17% | 1.45% | **-0.10%** |
  | 1792 renamed | 2.24% | 1.41% | **1.01%** |
  | 1920 renamed | 1.01% | 0.97% | **-0.02%** |

  **Region with the shipped `switch-cyc=30` beats function placement at all four
  cache points on serializing hardware**, ties at cache 16 renamed, and loses by
  0.41-1.55 pp at the other three renamed points. Coverage is a wash: `ditSuppressed`
  is within 0.2-0.8% of function placement's, both ~97-99% of the oracle. **The
  +38.11% that motivated the old advice is `switch-cyc=0`**, i.e. the pre-2026-08-24
  default asserting toggles are free. Do not quote it as region's cost.

  **Why region needs no compression-round special case.** On a loop whose body is
  secret end to end, every block is a need-block, so `admitOffCorridors` early-returns
  at its `all_of(On)` check and region placement *degenerates to whole-function
  coverage by construction*. Verified 2026-08-27: on a synthetic compression-round MIR
  (hot loop, secret `MADD`, non-preserving call in the body), `region` and
  `-taint-dit-placement=function` emit **byte-identical** code - entry enable,
  per-iteration post-call re-assert, exit clear - and `switch-cyc` from 0 to 100,000 is
  inert because there is no corridor to merge. The old 888,967-vs-224,289 switch gap
  was `switch-cyc=0` splitting blocks that 30 now merges, not a policy difference.
  Judge a placement by how often it **leaves** a region, not by switch count.

  **What the admission test still cannot reach**, and it is not corridors:
  `TaintAnalysis.cpp:2826` refuses any one-sided corridor (`!HasOnPred || !HasOnSucc`)
  at any switch cost, correctly, because a leading preamble or trailing epilogue has no
  toggle pair to save. So the residual switches are entry enables, exit clears, and
  post-call re-asserts, reachable only by callee ownership (cloning, or Mode 2) - which
  is what [[dit-switch-cyc-revision]] already identified as the next highest-value
  target.
- **"Coverage >= 100% of oracle" is not reachable and should not be gated on.** A
  wrapper oracle holds DIT across untainted code in the same call - `find_hash`,
  `sha512_init`, loop bookkeeping - which a precise analysis correctly leaves
  alone. Adding full genuine HMAC coverage moved the ratio only ~2 points of the
  ~5 that were "missing", so the rest is oracle slack. Treat a shortfall as a
  pointer to a suspect TU, not as a number to drive to 100.
- **The NOP control is not neutral; substitute a real op as well.**
  `-taint-dit-nop-switches` emits `HINT #0`, which controls for layout but not for
  the issue slot. Against `mul xzr, xzr, xzr` at the same addresses with the same
  instruction count, the NOP build is consistently **slower by ~0.25%** (both
  gem5 switch models, two cache points) - so a NOP baseline overstates itself and
  therefore *understates* DIT cost. There is no `mul` mode in the pass yet; the
  measurements were done by rewriting the switch sites in the assembly.
- **Most of what looks like switch cost can be codegen.** With all 121 SQLCipher
  HMAC/SHA switches turned into NOPs and no DIT executing at all, the instrumented
  build still cost **+17.10/+16.57 pp serializing and +4.05/+2.52 pp renamed** -
  under a renamed switch, the majority of the total. Region placement splits
  blocks inside a compression loop and the restructuring is expensive by itself.
  Cheaper switches do not remove it.
- **A gem5 ROI delimited by `m5_reset_stats` does NOT give exactly equal
  instruction counts across machine configs.** The marker lands as a scheduled
  event, so a ROB-scale number of in-flight instructions commit on either side.
  Measured across a 40-run sweep the discrepancy is always exactly 0 or +400 and
  never negative - a fixed offset that does not scale with the region (400 out of
  85M and out of 887k alike). The identity gate therefore takes a 0.01%-of-ROI
  tolerance; a real divergence scales with the workload. Do not read the constant
  as contamination.

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

All 33 tests pass as of 2026-08-27. The whole `llvm/test/CodeGen/AArch64` suite was
last run clean on 2026-08-27 (3898 discovered, 3894 pass, 4 pre-existing XFAIL, 0
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
