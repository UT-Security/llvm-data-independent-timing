# Interprocedural Taint Analysis + PSTATE.DIT Hardening - Consolidated Overview

**Start here, then follow the reading order in [`README.md`](README.md)**, the annotated
index of every design doc, measured result, and research note. This doc is the map: what
the project is, how to run it, how the analysis works, and what is measured.
Last updated **2026-08-18**.
Branch: `dit-tainter` (**all work goes here** - `main` is the upstream LLVM mirror and
has never carried taint work). Target arch: **AArch64 only**.

> **Picking this up fresh? Read §7 and §10 first.** The short version: the pipeline
> works and the taint precision is good, but **the performance case is mostly negative.**
> End-to-end runtime is now measured on four real workloads. Fine-grained placement wins
> on exactly one - QuickJS + Octane, where it recovers ~1.07%, essentially the whole
> always-on cost, and only with `-taint-dit-loop-hoist=1`. It loses badly on libsodium
> (+46%..+94%) and SQLCipher (6.4x worse than simply setting the bit process-wide), and
> on SQLCipher's shipping OpenSSL build **there is no prize to recover at all** (headroom
> -0.08%). A gem5 study bounds the total value of every DIT-gated optimization on that
> workload at **~1.4%**. Two method rules now govern every number: baseline against a
> **round-trip control**, and prefer **same-binary, two-configuration** comparisons
> (§7). The dominant *precision* problem remains context-insensitive mod-sets (§9.6).

`CLAUDE.md` at the repo root holds the authoritative operating instructions and is kept
in sync with the code; this doc and the rest of `docs/` hold the reasoning behind them.
It supersedes `handoff.md` (2026-07-14), which is kept only as history.

---

## 1. What this is (in one paragraph)

An LLVM fork that implements **interprocedural taint analysis + PSTATE.DIT hardening** for
AArch64. You declare secret entry points in a taint-source file (which function argument is
secret). At the **MIR level, post-prologepilog**, the pass propagates taint through
registers / stack / global memory across *all* functions of a translation unit (a fixed
point over the call graph), then inserts **PSTATE.DIT (data-independent timing) mode
switches** (`MSR DIT, #1` / `MSR DIT, #0`) so that secret-dependent code runs with
data-operand timing side-channels suppressed. Flag absent ⇒ codegen byte-for-byte unchanged.

## 2. Threat model - read this or you will misunderstand everything

**The channel is data-OPERAND instruction timing** - instruction latency that depends on the
*values* of operands. NOT memory-address timing (cache/TLB), NOT control-flow (branch
prediction), NOT speculation/Spectre. This is exactly what ARM's **PSTATE.DIT** and Intel's
DOIT are built to suppress.

- **Speculation defense is OUT OF SCOPE.** An ISB/DSB "speculation barrier" mode used to
  exist as a placeholder for the DIT toggle mechanism; it was **removed 2026-07-14**. Do not
  reintroduce it.
- **Why taint at all, instead of DIT-everywhere?** DIT is not free (see §7 cost model), so it
  should cover only secret-dependent code. That is the entire value proposition.
- **Motivating attack (why this is real):** the Apple **M4 has a Load Value Predictor
  (LVP)**; DIT disables it (FLOP, USENIX Sec'25). A constant-trained load leaks via timing
  even for a pure copy of secret pixels. Firefox's SVG-filter pixel-stealing (subnormal-FP →
  "fixed" with integer arithmetic) is reopened by the LVP on the integer code. See
  `docs/research/value-timing-leaks.md`.

## 3. How to run it

### Preferred: one-shot clang flag
```
build/bin/clang -O2 -ftaint-harden=<taint-src-file> -c file.c -o file.o
# verify a barrier landed (-i is REQUIRED: objdump prints `msr DIT, #0x1` uppercase):
build/bin/llvm-objdump -d file.o | grep -iE '\bmsr\b.*\bdit\b'
```

On macOS a from-source clang needs the SDK pointed out or `#include <stdio.h>` fails;
see the `build/bin/clang.cfg` one-liner in `CLAUDE.md` §Build.

### Taint-source file format (one entry per line)
```
function_name,arg_index            # the arg VALUE is secret (data taint)
function_name,arg_index,pointee    # pointer is public; memory loaded THROUGH it is secret
```
0-based indices; `#` comments; C++ needs **mangled** names (get them with
`llvm-nm file.bc | grep X`; strip ONE leading underscore: `__ZN` → `_ZN`).

### Manual / wrapper flow (for the report files - the clang flag does not emit these)
The pipeline is 3 phases (see §5 for *why*). Canonical playground run:
```
clang -O2 -isysroot $(xcrun --show-sdk-path) -S -emit-llvm file.c -o f.ll
opt  -S f.ll -passes=taint-annotate -taint-src=file_secret.txt -o f.ann.ll
llc  -O2 -stop-after=prologepilog f.ann.ll -o f.pe.mir
perl -0pi -e 's/<mcsymbol >//g' f.pe.mir                       # MIR CFI serialization bug
llc  -enable-new-pm -run-taint-interproc -taint-insert-dit [flags] f.pe.mir -o f.hardened.mir
llc  -start-after=prologepilog f.hardened.mir -filetype=obj -o f.o
```
**The wrapper flow does NOT get the tail-call disable.** `-taint-no-tail-calls` is
stamped by clang and by the LTO backend, not by `llc`, so an arm built this way keeps
its tail calls and every one of them leaks DIT on. Pass `-disable-tail-calls` to the
FIRST `llc` (the one that lowers from IR - by the `-run-taint-interproc` step the tail
calls already exist), and check the result with `-taint-callsite-report`: a
`DITLEAK tailcall-ungated` line means you forgot.

A Firefox TU uses `-stop-before=aarch64-asm-printer` instead of `stop-after=prologepilog`,
plus `sed -i '' 's/nomerge //'`. See `~/Documents/firefox/build_taint.sh` (now `-O2`).

### The flags that matter
| Flag | Meaning / default |
|---|---|
| `-taint-insert-dit` | master switch; implied by `-ftaint-harden`. Without it, analysis runs + reports emit but codegen is untouched (A/B baseline). |
| `-taint-no-tail-calls={0\|1}` | **default 1 = tail calls are OFF TU-wide whenever `-ftaint-harden` is passed.** A tail call is an exit with no epilogue, so DIT can never be cleared after one and selective placement degenerates to blanket - libsodium's `randombytes_buf` does this through an indirect tail call, so `sodium_init()` alone costs the full always-on penalty (§7 of `docs/design/dit-tailcall-gap.md`). `=0` restores them for A/B; refused under `-ftaint-dit-abi`. Keyed on the FLAG, not on the seeds, so an `-ftaint-harden=<EMPTY>` baseline stays codegen-matched. Stamped at codegen, after the IR pipeline, so TailRecursionElimination is untouched - which is why it is not in `-emit-llvm` output (only the `taint-no-tail-calls` module flag is). |
| `-taint-dit-placement={region\|function}` | **default `region`** (fine-grain). `function` = coarse: DIT on at entry, off before each return. |
| `-taint-dit-loop-hoist={0\|1}` | **default 1** = each need-loop coarsened On with the enable hoisted to the preheader (one toggle, whole loop covered). `0` selects block-minimal coverage: fewest instrs covered, per-iteration toggles. |
| `-taint-dit-switch-cyc` (default **30**), `-taint-dit-dwell-per-instr` (default 1.0) | cost-model knobs for region merging admission. 30 cyc is the measured serializing switch cost; the error is asymmetric (merging costs cheap dwell and is fail-safe, not merging costs a full switch pair), so err high. |
| `-taint-no-modset-gate` | **default false, i.e. the call-site mod-set gate is ON.** The safety valve: applies a callee's memory clobber at every call site of that callee rather than only where a secret is passed. Maximally conservative, much slower - the escape hatch for taint that does not travel by arguments. See §6. |
| `-taint-callsite-report=F` | ESCAPE report: secrets passed to callees we can't instrument. |
| `-taint-uncovered-report=F` | tainted instrs DIT can't protect (divide/sqrt, secret-address, secret-branch). |
| `-taint-clobber-report=F` | call sites that make the caller treat memory as secret (taint-explosion sources). |
| `-taint-dit-precision-report=F` | per-function DIT accounting (need / underdit / collateral / switches). Reachable from clang as `-mllvm -taint-dit-precision-report=`. **Always read the loop-weighted variant** - see `docs/design/dit-precision.md`. |
| `-taint-dit-reassert-report=F` | the post-call `MSR DIT, #1` re-assert sites the analysis cannot prove away. See `docs/design/dit-callee-ownership.md`. |
| `-taint-output=F`, `-debug-only=taint-interproc` | per-instruction taint dump; interproc propagation trace ("caller → callee: arg now tainted"). |

## 4. How the taint analysis works

**Taint kinds** (parameterized by `TaintKind`, one bitvector each):
- **Data** - the value itself is secret.
- **Pointee** - the value is a pointer to secret memory. A load *through* a pointee-tainted
  pointer yields Data taint. Pointee taint survives pointer arithmetic (`base+offset`).
- **Address** - the value may be used as a secret-dependent address (cache/TLB domain - DIT
  does NOT cover this; it lands in the uncovered report, not a barrier).

**Interprocedural fixed point** (`TaintInterprocPass`, a new-PM module pass):
- Seeds argument taint from the `tainted` / `tainted-pointee` IR attributes (set by the
  `taint-annotate` IR pass from the taint-src file).
- Iterates the call graph to convergence: **caller→callee** propagates argument-register
  taint into callee summaries; **callee→caller** propagates return-value taint and a
  **memory mod-set** (see §6).
- Every consumer replays through the single `replayTaint(MF, TR, ...)` visitor. Do NOT
  hand-roll another replay loop - that is how the replay drifts from `propagateTaintMI`.

**DIT placement** (`insertTaintDITSwitches`): a block/region is a "Need" if it contains a
tainted instruction that is `isDITProtected || isCall`. Region placement covers only
secret-dependent regions (clean preambles / public index math stay DIT-off), carries a
**soundness verifier** (forward AND-meet), and falls back per-function to whole-function
coverage if it cannot prove coverage - so it is always safe. Requires FEAT_DIT (Armv8.4+,
Apple M-series has it; Neoverse N1 does not → SIGILL there).

**DIT ownership (2026-08-08): only the frame that turned DIT on may turn it off.** A
function proven `AlwaysEnteredWithDIT` keeps whole-function coverage even under `region`
and never clears the bit - narrowing there would strip the *caller's* protection, and
nothing is lost because the caller's region already covered the callee.
`-debug-only=taint-interproc` prints which functions took that path. Callers still
re-assert `MSR DIT, #1` after every non-tail call unless the callee's `PreservesDIT`
summary bit proves it redundant; `-taint-dit-reassert-report` audits the rest. See
`docs/design/dit-callee-ownership.md`.

**A tail call is BOTH `isReturn()` and `isCall()`** on AArch64, so it needs its own case
in any "before every return" walk. Whole-function placement used to clear DIT
immediately before `b crypto_sign_ed25519`, running the callee that receives the secret
completely unprotected (fixed 2026-08-05; `docs/design/dit-tailcall-gap.md`). The
residual - after a tail call DIT may stay set indefinitely - **is no longer accepted**:
tail calls are disabled TU-wide by default for any hardened build (`-taint-no-tail-calls`,
above). What survives the disable is `musttail` and `MachineOutlinerTailCall`, both
reported as `DITLEAK tailcall`; a `DITLEAK tailcall-ungated` line instead means the flag
did not reach that TU.

## 5. Why the 3-phase serialize/reparse design (load-bearing)

1. `TaintInterprocPass` needs **all MachineFunctions of the TU resident at once**; the legacy
   PM frees each MF after emission ⇒ serialize to MIR text and reparse (MIRParser
   materializes all MFs together).
2. **AArch64 has no new-PM codegen pipeline** ⇒ lowering must use the legacy PM, driven via
   process-global `start-after`/`stop-after` cl::opts.

So `-ftaint-harden` runs: (1) legacy PM `stop-after=prologepilog` → in-memory MIR text
(+ `<mcsymbol>` strip); (2) MIR reparse + new-PM `TaintInterprocPass` → hardened MIR;
(3) legacy PM `start-after=prologepilog` → object. Analysis runs **post-prologepilog** so it
sees real stack offsets. `-g -O2 -ftaint-harden` is fully supported (dwarfdump clean).

## 6. Cross-function memory: the model, and how external calls are handled

This is the subtle heart of the analysis.

**A callee that writes a secret into caller-visible memory** must taint the caller's later
reload, or the secret leaks unprotected. This is carried by a **`FunctionMemEffects`
mod-set** per function:
- `WritesSecretToGlobal{g}` - wrote a secret into global `g` (precise; only that global).
- `WritesSecretThroughArgPointee{i}` - wrote a secret through pointer-arg `i` (currently
  applied bluntly, P1a).
- `WritesSecretToUnknown` (**TOP**) - did something to memory we can't pin down.

**At a call:**
- **Direct in-TU callee (has a body):** apply its computed mod-set. TOP or arg-pointee →
  `setExternalMemClobbered()`; specific global → `setTaintedWholeGlobal(g)`.
- **External declaration or indirect call (no body / unknown target):** if a secret is
  *passed* (see fix B below), assume the worst - `taintCallResultDefs` (return may be secret)
  **and** `setExternalMemClobbered()` (it may have written the secret anywhere).

**`ExternalMemClobbered` = TOP landing point.** Once set, every subsequent stack/global/heap
**load** in that function is treated as secret. The call itself runs under DIT, and PSTATE.DIT
**persists across the call**, so an opaque callee **inherits DIT=1** (best-effort protection;
re-asserted after non-preserving calls - gap G1).

**A load consumes the clobber** (over-approximate, never misses a leak on this path). An
`-taint-annotation-driven` mode once suppressed that consumption in favour of trusting
per-function annotations - the standard constant-time-tool contract, cf. FaCT - and was
**removed on 2026-08-24**: it was built to suppress a flood that a controlled A/B later
attributed to the `$lr` artifact (see the attribution correction in §8), and after those
fixes it was equivalent to sound mode on every TU measured. Reintroducing it would be a
deliberate change of threat model, not a tuning knob.

**What IS gated, by default, is the mod-set APPLICATION.** A callee's memory clobber is
applied only at call sites that actually pass a secret, and only for a callee whose taint is
argument-sourced (the source condition) - the same rule applying to the `ReturnsTainted`
register summary. This is the pass's answer to context-insensitive summaries: without it
`secp256k1_ecdsa_verify` carries 17 `MSR DIT` for public data and Bitcoin Core's
`ConnectBlockAllEcdsa` costs **+51.20%**; with it, **+0.67%** at no measured loss of coverage
(gem5 `ditSuppressed` 103.1% of a hand oracle). The soundness claim is scoped: *preserves
coverage for argument-carried taint*. `flowprobe` confirmed four channels that escape it by
reading PSTATE.DIT at the consumer - a callee returning a pointer into a secret buffer, a
secret in a global read by a sibling with no call edge, a secret stored through a pointer by
inline asm (`INLINEASM` is not `isCall()`, so the pass cannot see it at all), and a secret
moved through a NEON register tuple. Closing the inline-asm and register-tuple channels is
the next precision work; the sound end state is an origin bit in the fixed point.
`-taint-no-modset-gate` gives up the precision for whole-memory conservatism.

## 7. Cost model (why placement granularity matters) - do not skip

`cost = toggles × ~30 cyc + dwell(workload) × time_in_DIT`. The two terms pull opposite ways
 - that tension *is* the placement problem.
- **Toggle ≈ 30 cyc, fully serializing** (measured, M4). Floor: a region costs ~60 cyc to
  enter+leave, so only create one if it removes more dwell than that.
- **Dwell up to ~15%** on sensitive SPEC 2026 benchmarks with DIT fully on (measured).
- **The READ is free: `MRS DIT` = 1.00 cyc vs `MSR DIT` = 30.34** (measured, M5). That
  30x asymmetry is what makes the ownership rule pay (90.67 → 2.01 cyc per call) and is
  the basis of the deferred runtime `MRS` mode, the only mechanism that fixes indirect
  and cross-TU calls.
- ⚠️ **Do NOT conclude "DIT is free" from microkernels.** `playground/dit_bench/` and
  `firefox_convolve_int` (0.968x) are DIT-*insensitive* - they measure the benchmark's blind
  spot, not DIT. Bad workloads for evaluating a placement *win*. Do not size the *prize*
  from them either: `lvp_chase` measures 4.0x where real workloads measure 1-2%, so
  microbenchmarks **overstate the prize ~200x** (gem5, 2026-08-13).
- The project owner implemented a **non-serializing DIT switch in GEM5** (via register
  renaming) - so on that model set `-taint-dit-switch-cyc` low and prefer the finest groups.

### End-to-end runtime on real workloads - the record

| workload | always-on DIT | taint-driven placement | verdict |
|---|---|---|---|
| libsodium, M4 (2026-08-03) | 0.998-1.003x, i.e. free | +46% (ed25519 sign) .. +94% (AEAD) | negative |
| **QuickJS + Octane**, M5 (2026-08-10/11) | +1.05% | **-1.07% vs always-on**, with `loop-hoist=1` | **the one win** |
| SQLCipher / libtomcrypt, M5 (2026-08-12) | +8.81% | +37.8% (hoist) .. +57.0% (region) | negative |
| SQLCipher / OpenSSL, M5 - the shipping default | +1.76% | recoverable headroom **-0.08% = zero** | no prize exists |
| Firefox / Chromium, Speedometer 3.1, M5 (2026-08-09/10) | +2.61% / +1.80% | not yet placed | prize measured, uncollected |

**Overhead is amortized by operation length**, because cost tracks *executed toggles*
relative to runtime: on libsodium the same build costs +94% on a 1.28 us AEAD encrypt,
+46% on a 9.64 us ed25519 sign, and **<1% on a 271.6 ms argon2id KDF** - where CIO pays
**27.84x** on that same primitive, the project's strongest head-to-head number
(`docs/results/dit-cost-model.md`).

**Two axes decide everything** (`docs/results/quickjs.md`): the **secret fraction** sets
the size of the prize, and **granularity** decides whether you can collect it. Measured
crossover, with dwell held constant and only region count varied: a region must hold
**~1300 cycles (~0.34 us)** of work to be worth creating. The shipped
`-taint-dit-switch-cyc=0` asserts toggles are free, which is wrong by ~30 cyc, so the
default is far too fine for serializing hardware; likewise `-taint-dit-loop-hoist=0`
(on QuickJS, without `=1` there is no win at all).

**Why SQLCipher loses is the lesson worth internalizing.** Precision is excellent - the
key reaches 2 functions and 11 `MSR DIT` in a 263k-line file, with no context-insensitive
spread. But `cbc_encrypt` dispatches AES through the `cipher_descriptor[]` function-pointer
table once per 16-byte block, so a 4 KB page becomes ~256 regions of ~300-500 cycles each,
3-4x below the crossover. `-taint-dit-loop-hoist=1` cannot fix it: it hoists *within* a
function, and these toggles sit at a callee boundary reached *indirectly*. And with the
oracle corrected to wrap all three provider entry points, protecting only the secret costs
what protecting everything costs, because almost all of always-on's cost is DIT **on the
crypto**, which any correct placement must also pay. **AES is close to the worst possible
motivating workload:** software AES is DIT-expensive only for its T-table data-dependent
loads, whose real leak is *cache* timing, which DIT does not cover; hardware `AESE` is
already constant-time. The value is where secrets flow through general-purpose code never
designed to be constant-time.

**gem5 corroboration (2026-08-13)** runs the identical binary under serializing vs renamed
`MSR DIT`, isolating toggle cost with dwell held constant - something silicon cannot do:
**+0.08% / +12.8% / +19.1%** for **6 / 54 / 63** switch sites, reproducing the M5 ordering
and region:hoist ratio (1.49x vs 1.52x) at about a third the magnitude. It also measures
the prize: **~1.4%**, carried entirely by value prediction. So the shipped placement spends
19% to protect something worth 1.4%.

### Two method rules, each learned by retracting a result

1. **Baseline every placement measurement against a round-trip control**
   (`-ftaint-harden=<empty file>`), never against the stock `-O2` build. The 3-phase MIR
   round-trip changes codegen by itself, with zero `MSR DIT` emitted.
2. **That control is necessary but NOT sufficient.** The artifact is a per-binary codegen
   lottery, not a constant: **+0.58%** (QuickJS), **+0.06%** (SQLCipher native), **+2.65%**
   (gem5, where the zero-DIT `nodit` binary is the *slowest* in the matrix, exceeding the
   entire dwell effect). At ~1% effect sizes only a **same-binary, two-configuration**
   comparison is trustworthy - which is exactly what the gem5 study does.

Three claims have been reported and retracted on metric or oracle errors (QuickJS -6.28%,
SQLCipher +8.15%, "the secret code is 30x more DIT-sensitive"). **Read the retraction
banners in `docs/results/` before quoting any number**, and audit a manual placement for
*coverage* before believing its performance.

Full detail: `docs/results/dit-cost-model.md`, `docs/results/quickjs.md`,
`docs/results/sqlcipher.md`, `docs/design/dit-placement.md`.

## 8. Key mechanisms & the most recent fixes (this session, 2026-07-24→26)

**Fix #1 - the `$lr` seeding guard (commit `2d81ec4`) - THE flood fix.** The arg-taint
seeding mapped every callee livein's register encoding to an "argument index". `$lr`/x30
(the return address, livein of every function, encoding 30) became a bogus "arg 30"; a caller
reusing x30 as tainted scratch seeded callees' return-address register as secret, cascading
across the call graph. Guard: `ArgIdx <= 7` (AAPCS64 args are only X0–X7 / V0–V7). Effect on
the Firefox `FilterNodeSoftware` TU: **sound-mode instrumentation 78 → 5** (the genuine
secret set). Test: `taint-analysis-lr-not-arg.mir`.

> **Attribution correction:** we *originally* blamed the flood on the blunt-TOP memory model
> (`ExternalMemClobbered`) and built annotation-driven mode to suppress it. Controlled A/B
> (git-stash the fixes, rebuild, compare) proved that WRONG: the flood was the `$lr` artifact;
> `ExternalMemClobbered` was only the amplification channel. The flood numbers quoted in
> `docs/research/memory-summaries.md` carry that same correction. Annotation-driven mode was
> removed on 2026-08-24 as a consequence - it was the fix for a misdiagnosis.

**Fix B - passed-vs-live (commit `2d81ec4`).** A secret counts as reaching a callee only when
genuinely *passed* in an argument register (data or pointee), read on the state *entering* the
call - NOT merely live/clobbered across it (an ABI-compliant callee can't read a caller-saved
register it wasn't handed). Narrows `anyTaintedCallArgument` (gates the external-call
`ExternalMemClobbered`) and the ESCAPE report. Does not change the instrumentation count (the
flood was fix #1); it removes spurious escapes and makes the memory trigger sound. Preserves
the pointee channel (still a real reach). Test: `taint-analysis-call-arg-passed.mir`.

**`-taint-clobber-report` (commit `71be809`).** Lists every call site that makes the caller
treat memory as secret - the *sources* of a taint explosion - with reasons
(`external-arg`/`indirect-arg`/`modset-top`/`modset-argptr`/`modset-global`). Test:
`taint-analysis-clobber-report.mir`.

**Propagation-power result.** A controlled 6-level `noinline` chain (`playground/propagation_test.c`):
one seed flowed through **all 8 functions** via register args (down), memory write+reload
(callee→caller), and return values (up), with **zero false positives**. On real `-O2` Firefox
code, one `GenerateNormal<float>` seed propagates cleanly to `ColorComponentAtPoint`
(`arg 0 now pointee-tainted`). What limits *visible* cross-function spread on real code is
**upstream inlining** removing call edges before the MIR analysis runs - not the analysis. Use
`-O2` (the real target and canonical flag), not `-O3` (over-inlines).

## 8b. The libsodium / CIO head-to-head, and two soundness bugs (2026-07-27→29)

**Setup - SCRIPTED, do not rebuild by hand (2026-08-03).** The original rig lived in an
untracked home directory (`~/Documents/libsodium-stable/`, `~/Documents/cio/`) and **was
lost**. It is now reproducible from a clean machine by two tracked scripts:

```
utils/taint_libsodium_eval.sh     # fetch -> patch -> build -> bitcode -> seed ->
                                  # analyze -> archives -> check -> report
utils/taint_libsodium_bench.sh    # runtime A/B/D/E/C matrix (see §7)
```

`taint_libsodium_eval.sh` fetches libsodium 1.0.21 and CIO's
`libsodium.uarch_checker.config` (from `counter-optimization/cio`, 65 lines / 21
symbols, format **identical to ours**), applies the rename patch, builds whole-library
bitcode (WLLVM + `llvm-link`, `--disable-asm`), derives the pointee-typed seed file, and
emits `libsodium-{baseline,hardened,tuned,func}.a` plus reports in `rpt/`. Stages are
independently runnable (`--list`).

*Verification (2026-08-05): a full clean-machine pass was run* - all nine stages from an
empty directory, into a work dir with a deliberately non-default name - and it
**reproduced every number exactly**: 926 functions, 48 pointee + 17 data attrs across 21
functions, 647/516/611 switches, `__text` +1.09%/+0.85%/+1.00%, ESCAPE 35 / UNCOVERED
168 / CLOBBER 610, and **`make check` 86/86 on both the baseline control and the
hardened library**. The generated seed file is byte-identical to the hand-built one
(65/65 lines), and the benchmark matrix run against the clean archives matches to within
run-to-run noise. The rig is reproducible from scratch.

One benign difference to expect: `libsodium.a.ORIG` differs in size between work dirs
(577,600 vs 564,728 bytes) because WLLVM embeds **absolute bitcode paths** in a section,
so a longer work-dir path makes a bigger archive. The emitted objects are identical
(`__text` 244,596 in both), i.e. codegen is path-independent.

**Three things the script encodes that cost real time to rediscover:**
- **Pre-flight seed names against IR `define` names, NOT `llvm-nm`.** On Mach-O
  `llvm-nm` prints object names with a leading `_`, so a naive check reports **21/21
  MISSING** on a perfectly good seed file. `taint-annotate` ignores unmatched names
  *silently*, so getting this backwards yields a completely unseeded run that looks
  successful. The script hard-fails on any unresolved line and on zero attributes.
- **The 3 renamed statics are all in `crypto_stream/chacha20/ref/chacha20_ref.c`**
  (`chacha20_encrypt_bytes`, `stream_ref`, `stream_ref_xor_ic`). Rename with `\b`
  anchoring - `stream_ref` must not match inside `stream_ref_xor_ic`. They are `static`
  and `stream_ref`/`stream_ref_xor_ic` also exist in `salsa20/ref/`, which is almost
  certainly why CIO patched them: after `llvm-link` merges the module, colliding
  statics get `.N` suffixes and seeding by plain name stops working.
- **`--disable-asm` is required** or hand-written `.S` never enters the bitcode and is
  invisible to the analysis. These numbers describe C-only libsodium.

Two gotchas found the hard way: 3 of CIO's 21 seed names only exist after *their* rename
patches (`chacha20_encrypt_bytes_ref`, `stream_ref_ref`, `stream_ref_xor_ic_ref`) and
`taint-annotate` **silently ignores** unmatched names - always pre-flight the seed list
against `llvm-nm`. And CIO's `arg_index` is an index into SysV GPR arg registers, capped at
5, so their four `crypto_aead_*,8` lines are **dead**: they never seed the AEAD key. Ours
does.

**Pointer args must be typed `pointee`.** Their format has no pointee concept and their
memory domain makes that harmless (every unresolvable load returns TOP, and **TOP = Taint**).
Ours distinguishes: a load through a *data*-tainted pointer is a secret ADDRESS, not secret
data. Transcribing their config literally left 462 `secret-address` UNCOVERED lines; typing
the 48 pointer args as `pointee` cut it to 205 and is the faithful translation.

**Bug A - `implicit-def` counted as a use (OVER-taint).** `MI.uses()` spans implicit defs, so
`dead $w0 = MOVi32imm 1, implicit-def $x0` with `$x0` tainted re-tainted its own defs: taint
could never leave a register. **−33% tainted instructions** once fixed.
**Bug B - narrowed reload of a spilled secret (UNDER-taint = leaked secret).** Cell lookup
required an exact `(FI,offset,size)` match, so spill-8/reload-low-4 returned the secret as
public. Read path now tests overlap; clear path stays exact-match.
Both in `docs/design/spill-soundness-bugs.md`, both regression-tested, each test verified to
**fail against the pre-fix code**. That doc also records what spilling *does* do correctly,
and a method note (rematerialization defeated the first repro).

**Results after the fixes** (libsodium, 109/932 functions instrumented, fallback off):
`__text` **+1.14%** vs unhardened, 711 DIT switches, 48% recall against CIO's alert set.
*(2026-08-03 scripted rebuild reproduces this: 105/926 instrumented, **+1.09%** `__text`,
647 switches, ESCAPE **35** - exact - CLOBBER 610. UNCOVERED came back 168 vs 203, the
one delta not yet explained; the rest is consistent with the `--disable-asm` build
config, 926 vs 932 functions. **`make check` passes 86/86** on the hardened library, with
the baseline whole-bitcode object run first as a control to prove the round-trip is
lossless.)*
With the since-removed `-taint-frame-addr-args=1` fallback: 286/932, +3.94%, **84% recall**,
but **9.1×** tainted instructions. For scale, CIO's own libsodium cost is
**+62%/+208%/+266%** code size and up to **27.84×** runtime. That fallback was **deleted on
2026-08-24**: it reasoned about whole frames rather than objects, so once the mod-set gate
existed nearly every call site looked secret-passing and the gate stopped firing -
`ConnectBlockAllEcdsa` measured **+45.32%** with both against **+0.66%** with the gate alone.
P1b (`docs/design/p1b-frame-provenance.md`) is the per-object replacement and does not rescue
it. The caller→callee half of that gap (`f(&local_secret)`) is therefore **open**.

**Correctness audit (the important part).** Of the 19 CIO functions we don't instrument,
**15 are unreachable from any seed** - artifacts of their blunt domain, not our misses; the
rest are init/abort paths that process no secret, plus one thin forwarding wrapper
(`crypto_stream_chacha20_ietf`) that is a modeling difference, not a leak. The miss direction
is clean. The **false-positive** direction is where the work is: see §9.6.

## 9. Limitations (what this does NOT protect - be honest with reviewers)

1. **DIT coverage gaps (intra-procedural).** Even inside an instrumented function DIT does not
   cover: **divide / sqrt** (not DIT-listed), a secret used as a **memory address**
   (cache/TLB timing), or a secret-dependent **branch** (control-flow timing). These are
   surfaced by `-taint-uncovered-report`, NOT protected. They need algorithmic (constant-time)
   rewrites.
2. **Opaque callees.** External decls and indirect calls can't be analyzed → blunt TOP (whole
   memory poisoned) and best-effort inherited-DIT protection only. A callee that does a
   secret-dependent divide/sqrt, secret-addressed access, or clears DIT is outside the
   guarantee. Audit via `-taint-callsite-report` (ESCAPE lines).
3. **Soundness rests on an ABI-compliance assumption** - a callee that scavenges caller-saved
   registers it wasn't passed would need in-process code execution, which defeats DIT anyway.
4. **Cross-TU scope.** Interprocedural analysis is one TU/module. Cross-TU taint is not
   tracked - annotate the entry function in *each* TU that receives the secret. Also
   incompatible with LTO for that TU (lowers to object eagerly).
5. **Channel-3 memory gap:** an external callee that reads a secret *global* on its own (no
   secret argument) and re-exports it is not caught by the argument path.
6. **Mod-sets are context-INSENSITIVE, and that is now the dominant false-positive source.**
   A callee's mod-set is per function, so once *any* caller passes a secret into
   `crypto_hash_sha512_update`, every other caller of it absorbs `ExternalMemClobbered` -
   e.g. `crypto_auth_hmacsha512`, which handles no seeded secret, is instrumented anyway.
   Measured on libsodium: **48 of 63** (fallback off) and **169 of 199** (fallback on) of
   the functions we instrument but CIO does not are outside the seed call-graph closure
   entirely. Bigger than every other over-taint source measured. See
   `docs/design/context-insensitivity.md`.
7. **P1 memory precision deferred:** the mod-set is blunt (whole-object, weak updates, every
   truncation → TOP). Precise arg-i / per-offset provenance + a libc model table are P1;
   note that at the MIR stage, `getUnderlyingObject` often can't reach the `Argument` through
   optimized code - measured, only **17 of 583** secret-writing call sites resolve to an
   argument at all (`docs/design/context-insensitivity.md`).
8. **Inlining flattens visible propagation** at higher opt (see §8). The analysis is correct;
   the call edges are just gone before it runs.
9. **FEAT_DIT required at runtime** (Apple M-series yes; Neoverse N1 → SIGILL). Verify via
   objdump/lit or `qemu-aarch64 -cpu max`.
10. **No declassification mechanism, and that decides whether any win generalizes.** A
    function that returns a secret-derived value (a password *verify* returning a boolean)
    taints its caller, and on QuickJS an arbitrary taint source flowing back through a
    return value produced **13,222** `MSR DIT` - 618 of them inside the interpreter -
    versus **6** when nothing secret re-enters the caller. Same program, same pass, 2200x
    apart. See `docs/results/quickjs.md`.
11. **Compile time is 10.7x on a large TU** after the 2026-08-10 fix (`quickjs.c`, 54k
    lines / 940 functions: 733 s; a flat 1.3-1.5x up to ~100 functions). Taint *spread* on
    such TUs is still open. See `docs/design/scalability.md`.
12. **Measurement itself is a limitation at these effect sizes** - the MIR round-trip
    perturbs codegen by +0.06%..+2.65% with zero `MSR DIT` emitted, which can exceed the
    entire signal. See the two method rules in §7.

## 10. Current state

- **Tests:** 29 lit tests pass - 28 `llvm/test/CodeGen/AArch64/taint-analysis-*.mir` +
  `llvm/test/Transforms/TaintAnnotate` (as of 2026-08-11). The whole
  `llvm/test/CodeGen/AArch64` suite was last clean on 2026-08-08 (3894 discovered, 3890
  pass, 4 pre-existing XFAIL). Run:
  `build/bin/llvm-lit -sv llvm/test/CodeGen/AArch64/taint-analysis-*.mir llvm/test/Transforms/TaintAnnotate`
- **Placement defaults (retuned 2026-08-24):** `region` granularity, `loop-hoist=1`,
  `switch-cyc=30`. Both flips are fail-safe (each widens coverage, never narrows it) and
  were measured: on the libsodium composite `switch-cyc=30` removes 27-31% of the pass's
  overhead at every resolvable region size and flips the 512 B verdict against blanket DIT
  from a loss to a win; loop hoisting is what takes CPython and SQLite from +0.91%/+0.35%
  to indistinguishable from the hand oracle. The previous defaults asserted that toggles
  are free, which no measurement supports.
- **The call-site mod-set gate is ON by default**, with its two companion rules (strict
  source condition, return-call-site gating) unconditional. It is what takes Bitcoin Core's
  `ConnectBlockAllEcdsa` from +51.20% to +0.67%. Its soundness claim is scoped:
  *preserves coverage for argument-carried taint*. `-taint-no-modset-gate` is the way out.
  See §6.
- **Removed 2026-08-24** (five knobs, ~230 lines): `-taint-frame-addr-args` (superseded by
  P1b; measured +44 points against the gate), `-taint-annotation-driven` (built for a flood
  that a controlled A/B later attributed to the `$lr` artifact; equivalent to sound mode on
  every TU measured), `-taint-call-arg-precise` (A/B for a settled question),
  `-taint-region-merge-gap` (now a report-granularity constant), and
  `-taint-dit-relaxed-ownership` (measured ~0 on every library; its local-linkage
  precondition cannot fire in a shared library). The negative results they carry live in
  `docs/design/frame-addr-fallback.md`, `docs/results/dit-relaxed-ownership.md` and §8.
- **Shipped since this doc's previous revision:** the DIT ownership rule and
  `AlwaysEnteredWithDIT` (2026-08-08), the tail-call leak fix (2026-08-05), DIT precision
  accounting (2026-08-06), and the compile-time fix that made `quickjs.c` compilable at all
  (2026-08-10).
- **Validated:** flood 78→5 on `FilterNodeSoftware`; `FilterProcessingScalar` does not
  flood (sound == annotation-driven on both, at `-O2`); libsodium head-to-head vs CIO
  measured end to end (§8b), `make check` 86/86 on the hardened library; QuickJS, SQLCipher
  and both browsers measured end to end (§7).

### Next actions, in priority order

1. **Retune the shipped placement defaults from the measured crossover.**
   `-taint-dit-switch-cyc=0` encodes "toggles are free", which is false by ~30 cyc; the
   end-to-end data say a region needs **~1300 cycles** of work before it is worth creating,
   and `-taint-dit-loop-hoist=0` is the wrong default on serializing hardware (on QuickJS
   it is the difference between a 1.07% win and no win at all). This is the cheapest
   remaining improvement and it is fully specified by existing measurements.
2. **Interprocedural hoisting, and with it the deferred runtime `MRS` mode.** SQLCipher is
   the motivating case: the toggles that matter sit at a callee boundary reached through
   `cipher_descriptor[]`, so no intraprocedural transform can remove them. Placing the
   enable in the *caller* before the block loop is the missing piece, and `MRS DIT` = 1.00
   cyc vs `MSR DIT` = 30.34 is what makes the runtime variant cheap enough to work through
   indirect and cross-TU calls. See `docs/design/dit-callee-ownership.md`.
3. **Declassification.** Without it the QuickJS result does not generalize: any realistic
   secret-consuming API returns a secret-derived value, which taints the caller and floods
   the instrumentation (§9.10).
4. **~~Attack the context-insensitivity FP source (§9.6)~~ DONE, and it is now the
   default.** The call-site mod-set gate plus the strict source condition and
   return-call-site gating shipped on by default 2026-08-24 (+51.20% -> +0.67% on Bitcoin
   Core's `ConnectBlockAllEcdsa`). **What remains** is closing the four channels
   `flowprobe` found - start with inline asm (`INLINEASM` is not `isCall()`, so the pass
   is blind to `asm volatile ::: "memory"`) and NEON register tuples, both contained; then
   `ReturnsPointeeTainted` for a callee returning a pointer into a secret buffer. The
   sound end state is an origin bit in the fixed point, which is what would let the gate
   stop being a scoped claim.
5. **Provenance recovery, THEN P1b.** Only **17 of 583** secret-writing call sites resolve
   provenance to an argument (566 are TOP), so P1b has almost nothing to act on until more
   stores resolve to arg-*i*, which likely means analyzing at IR and carrying facts to MIR.
   See `docs/design/context-insensitivity.md`.
6. **Find a workload where the prize is both real and collectable.** The blocking gap has
   moved: a DIT-sensitive workload was found (QuickJS, and the browsers at +1.8-2.6%
   always-on), but on SQLCipher the prize turned out not to exist, and the gem5 study bounds
   it there at ~1.4%. The browsers are the strongest remaining candidate - the prize is
   measured and placement has not been attempted. The shape to look for
   (`docs/research/browser-history-leaks.md`): **public code with DIT headroom, secret code
   with none**, at a granularity above ~1300 cycles per region.

## 11. Code map (where things live)

| Piece | Where |
|---|---|
| `taint-annotate` IR pass | `llvm/lib/Transforms/Instrumentation/TaintSourceAnnotator.cpp` |
| Interproc MIR analysis + DIT insertion | `llvm/lib/CodeGen/TaintAnalysis.cpp`, `TaintFixedPointIteration.cpp` |
| `TaintSummaryInfo` / `FunctionMemEffects` (header-only) | `llvm/include/llvm/CodeGen/TaintSummaryInfo.h` |
| Taint cl::opts, `TaintState`, `CallArgTaint`, public helpers | `llvm/include/llvm/CodeGen/TaintAnalysis.h` |
| `-ftaint-harden` flag + 3-phase codegen | `clang/lib/CodeGen/BackendUtil.cpp`; flag in `clang/include/clang/Options/Options.td` |
| `insertTimingModeSwitch` (emits `MSR DIT`) | `llvm/lib/Target/AArch64/AArch64InstrInfo.cpp` |
| Store payload classification (`getNumStoredValueRegs`) | `AArch64InstrInfo.cpp` (never classify stores by mnemonic prefix) |
| Tests | `llvm/test/CodeGen/AArch64/taint-analysis-*.mir` |
| Scratch experiments (not shipping) | `playground/` |
| Browser always-on DIT rig | `utils/taint_browser_dit_bench.sh`, `utils/browser_dit/` |
| DIT precision / region-spacing analysis | `utils/taint_dit_precision.py`, `utils/taint_region_distance.py` |
| **libsodium/CIO rig - SCRIPTED** (the old `~/Documents/libsodium-stable/` + `~/Documents/cio/` copies were lost; do not look for them) | `utils/taint_libsodium_eval.sh` (build+analyze+archives+`make check`), `utils/taint_libsodium_bench.sh` (runtime A/B/D/E/C). Default work dir `~/Documents/libsodium-1.0.21/`. |
| Runtime benchmark drivers (kperf cycles, P-core pinning) - **untracked, outside the repo**; vendor or pin it or this rig will be lost the same way | `~/Documents/crypto-dit-benchmarks/` (`perf.c`, `libcpupin.dylib`, per-primitive drivers); override with `BENCH_DIR=` |

## 12. Deeper reference docs (this doc is the map; these are the territory)

`docs/README.md` is the full annotated index with a reading order. The essentials:

- `docs/reference/dit-spec.md` - what PSTATE.DIT actually guarantees (covered set; excludes
  divide/sqrt; address-timing not covered). `isDITProtected` is transcribed from this, so
  keep the two in sync.
- `docs/results/dit-cost-model.md` - toggle (~30 cyc) + dwell (~15% SPEC), the measured
  numbers, and the libsodium negative.
- `docs/results/quickjs.md` - the one positive result, the granularity crossover, and the
  round-trip-control method rule.
- `docs/results/sqlcipher.md` - the definitive negative, and the gem5 study bounding the
  prize at ~1.4%.
- `docs/design/dit-placement.md` - placement state, gaps (G1/G2/G3), optimal-placement design.
- `docs/design/dit-callee-ownership.md` - the ownership rule, `AlwaysEnteredWithDIT`, and the
  deferred runtime `MRS` mode.
- `docs/design/dit-tailcall-gap.md` - the tail-call leak and its permanent residual.
- `docs/design/dit-precision.md` - the precision metric, and why to read the loop-weighted
  variant.
- `docs/design/scalability.md` - the compile-time wall and the fix that made large TUs viable.
- `docs/design/context-insensitivity.md` - **the dominant false-positive source**, measured,
  with the correction that P1b is a much smaller lever than assumed (17 of 583 sites).
- `docs/design/spill-soundness-bugs.md` - the two 2026-07-27 bugs, what spilling *does* do
  correctly, and how to force a real spill (rematerialization defeats the naive attempt).
- `docs/design/frame-addr-fallback.md` - the `-taint-frame-addr-args` prototype: the
  `f(&local_secret)` gap, why the frame is more trackable than assumed, measured cost.
- `docs/research/value-timing-leaks.md` - motivation: LVP, Firefox, THOR/AMX.
- `docs/research/browser-history-leaks.md` - the browser threat model, the always-on browser
  DIT measurements, and the benchmark reframe that says where fine-grained can win.
- `docs/research/memory-summaries.md` - mod-set summary design + P1 refinements.
- `docs/research/ct-call-handling.md`, `docs/research/cio-and-ct-literature.md` - prior art
  (CIO/Jasmin/FaCT/DECLASSIFLOW); read before any novelty claim.
- `docs/reference/firefox-integration.md` - Firefox integration guide.
- `docs/handoff.md` - **historical only** (2026-07-14); superseded by this doc.

## 13. Working preferences (carry these forward)

- **Never** add `Co-Authored-By` / session-link trailers to commit messages in this repo.
- Builds are allowed (`ninja -C build llc` / `clang` / `opt`); ~1–2 min each.
- Keep `CLAUDE.md` and the `docs/` tree in sync with code changes in the same turn.
- Over-approximation is always the safe direction: a spurious barrier costs performance, a
  missing one costs the secret. Any "can't classify" path must fall back to treating every
  register use as secret.
- Never classify instructions by mnemonic string (missed `STNP`/`STGP`/`STXP` store-pairs is
  an under-taint = leaked secret). Use the `getNumStoredValueRegs` opcode hook.
