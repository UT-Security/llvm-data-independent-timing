# Interprocedural Taint Analysis + PSTATE.DIT Hardening — Consolidated Overview

**This is the single entry-point document.** Read it first. It supersedes the stale
`taint_handoff.md` (2026-07-14). Last updated 2026-07-26.
Branch: `interproc_taint`. Target arch: **AArch64 only**. HEAD at time of writing: `71be809`.

The deeper reference docs (`utils/taint_*.md`) are still valid for detail; this doc is the
map. `CLAUDE.md` at the repo root holds the authoritative operating instructions and is kept
in sync with the code.

---

## 1. What this is (in one paragraph)

An LLVM fork that implements **interprocedural taint analysis + PSTATE.DIT hardening** for
AArch64. You declare secret entry points in a taint-source file (which function argument is
secret). At the **MIR level, post-prologepilog**, the pass propagates taint through
registers / stack / global memory across *all* functions of a translation unit (a fixed
point over the call graph), then inserts **PSTATE.DIT (data-independent timing) mode
switches** (`MSR DIT, #1` / `MSR DIT, #0`) so that secret-dependent code runs with
data-operand timing side-channels suppressed. Flag absent ⇒ codegen byte-for-byte unchanged.

## 2. Threat model — read this or you will misunderstand everything

**The channel is data-OPERAND instruction timing** — instruction latency that depends on the
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
  `utils/taint_value_timing_leaks_research.md`.

## 3. How to run it

### Preferred: one-shot clang flag
```
build/bin/clang -O2 -ftaint-harden=<taint-src-file> -c file.c -o file.o
# verify a barrier landed:
build/bin/llvm-objdump -d file.o | grep -E '\bmsr\b.*\bdit\b'
```

### Taint-source file format (one entry per line)
```
function_name,arg_index            # the arg VALUE is secret (data taint)
function_name,arg_index,pointee    # pointer is public; memory loaded THROUGH it is secret
```
0-based indices; `#` comments; C++ needs **mangled** names (get them with
`llvm-nm file.bc | grep X`; strip ONE leading underscore: `__ZN` → `_ZN`).

### Manual / wrapper flow (for the report files — the clang flag does not emit these)
The pipeline is 3 phases (see §5 for *why*). Canonical playground run:
```
clang -O2 -isysroot $(xcrun --show-sdk-path) -S -emit-llvm file.c -o f.ll
opt  -S f.ll -passes=taint-annotate -taint-src=file_secret.txt -o f.ann.ll
llc  -O2 -stop-after=prologepilog f.ann.ll -o f.pe.mir
perl -0pi -e 's/<mcsymbol >//g' f.pe.mir                       # MIR CFI serialization bug
llc  -enable-new-pm -run-taint-interproc -taint-insert-dit [flags] f.pe.mir -o f.hardened.mir
llc  -start-after=prologepilog f.hardened.mir -filetype=obj -o f.o
```
A Firefox TU uses `-stop-before=aarch64-asm-printer` instead of `stop-after=prologepilog`,
plus `sed -i '' 's/nomerge //'`. See `~/Documents/firefox/build_taint.sh` (now `-O2`).

### The flags that matter
| Flag | Meaning / default |
|---|---|
| `-taint-insert-dit` | master switch; implied by `-ftaint-harden`. Without it, analysis runs + reports emit but codegen is untouched (A/B baseline). |
| `-taint-dit-placement={region\|function}` | **default `region`** (fine-grain). `function` = coarse: DIT on at entry, off before each return. |
| `-taint-dit-loop-hoist={0\|1}` | **default 0** = block-minimal (per-iteration toggles, fewest instrs covered). `1` coarsens a need-loop and hoists the enable to the preheader (for serializing-switch HW). |
| `-taint-dit-switch-cyc` (default 0), `-taint-dit-dwell-per-instr` (default 1.0) | cost-model knobs for region merging admission. |
| `-taint-annotation-driven` | **default false (= sound mode).** Opt-in: trust annotations, suppress cross-function memory poison at consumption. See §6. |
| `-taint-call-arg-precise` (default true, hidden) | A/B toggle for "fix B"; `=0` restores the blunt any-live-register call trigger. |
| `-taint-callsite-report=F` | ESCAPE report: secrets passed to callees we can't instrument. |
| `-taint-uncovered-report=F` | tainted instrs DIT can't protect (divide/sqrt, secret-address, secret-branch). |
| `-taint-clobber-report=F` | call sites that make the caller treat memory as secret (taint-explosion sources). |
| `-taint-output=F`, `-debug-only=taint-interproc` | per-instruction taint dump; interproc propagation trace ("caller → callee: arg now tainted"). |

## 4. How the taint analysis works

**Taint kinds** (parameterized by `TaintKind`, one bitvector each):
- **Data** — the value itself is secret.
- **Pointee** — the value is a pointer to secret memory. A load *through* a pointee-tainted
  pointer yields Data taint. Pointee taint survives pointer arithmetic (`base+offset`).
- **Address** — the value may be used as a secret-dependent address (cache/TLB domain — DIT
  does NOT cover this; it lands in the uncovered report, not a barrier).

**Interprocedural fixed point** (`TaintInterprocPass`, a new-PM module pass):
- Seeds argument taint from the `tainted` / `tainted-pointee` IR attributes (set by the
  `taint-annotate` IR pass from the taint-src file).
- Iterates the call graph to convergence: **caller→callee** propagates argument-register
  taint into callee summaries; **callee→caller** propagates return-value taint and a
  **memory mod-set** (see §6).
- Every consumer replays through the single `replayTaint(MF, TR, ...)` visitor. Do NOT
  hand-roll another replay loop — that is how the replay drifts from `propagateTaintMI`.

**DIT placement** (`insertTaintDITSwitches`): a block/region is a "Need" if it contains a
tainted instruction that is `isDITProtected || isCall`. Region placement covers only
secret-dependent regions (clean preambles / public index math stay DIT-off), carries a
**soundness verifier** (forward AND-meet), and falls back per-function to whole-function
coverage if it cannot prove coverage — so it is always safe. Requires FEAT_DIT (Armv8.4+,
Apple M-series has it; Neoverse N1 does not → SIGILL there).

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
- `WritesSecretToGlobal{g}` — wrote a secret into global `g` (precise; only that global).
- `WritesSecretThroughArgPointee{i}` — wrote a secret through pointer-arg `i` (currently
  applied bluntly, P1a).
- `WritesSecretToUnknown` (**TOP**) — did something to memory we can't pin down.

**At a call:**
- **Direct in-TU callee (has a body):** apply its computed mod-set. TOP or arg-pointee →
  `setExternalMemClobbered()`; specific global → `setTaintedWholeGlobal(g)`.
- **External declaration or indirect call (no body / unknown target):** if a secret is
  *passed* (see fix B below), assume the worst — `taintCallResultDefs` (return may be secret)
  **and** `setExternalMemClobbered()` (it may have written the secret anywhere).

**`ExternalMemClobbered` = TOP landing point.** Once set, every subsequent stack/global/heap
**load** in that function is treated as secret. The call itself runs under DIT, and PSTATE.DIT
**persists across the call**, so an opaque callee **inherits DIT=1** (best-effort protection;
re-asserted after non-preserving calls — gap G1).

**Sound mode (default) vs annotation-driven.** Sound mode consumes the clobber at every load
(over-approximate, never misses a leak). `-taint-annotation-driven` keeps the mod-set
computed (for the reports) but does **not** consume the cross-function poison at loads — it
trusts you to have annotated every function that truly receives a secret. This shifts
soundness from automatic over-approximation to **annotation completeness** (the standard
constant-time-tool contract, cf. FaCT). After the fixes in §8, sound == annotation-driven on
the TUs measured, so annotation-driven is opt-in insurance rather than load-bearing.

## 7. Cost model (why placement granularity matters) — do not skip

`cost = toggles × ~30 cyc + dwell(workload) × time_in_DIT`. The two terms pull opposite ways
— that tension *is* the placement problem.
- **Toggle ≈ 30 cyc, fully serializing** (measured, M4). Floor: a region costs ~60 cyc to
  enter+leave, so only create one if it removes more dwell than that.
- **Dwell up to ~15%** on sensitive SPEC 2026 benchmarks with DIT fully on (measured).
- ⚠️ **Do NOT conclude "DIT is free" from microkernels.** `playground/dit_bench/` and
  `firefox_convolve_int` (0.968x) are DIT-*insensitive* — they measure the benchmark's blind
  spot, not DIT. Bad workloads for evaluating a placement *win*.
- The project owner implemented a **non-serializing DIT switch in GEM5** (via register
  renaming) — so on that model set `-taint-dit-switch-cyc` low and prefer the finest groups.

Full detail: `utils/taint_dit_cost_model.md`, `utils/taint_dit_placement.md`.

## 8. Key mechanisms & the most recent fixes (this session, 2026-07-24→26)

**Fix #1 — the `$lr` seeding guard (commit `2d81ec4`) — THE flood fix.** The arg-taint
seeding mapped every callee livein's register encoding to an "argument index". `$lr`/x30
(the return address, livein of every function, encoding 30) became a bogus "arg 30"; a caller
reusing x30 as tainted scratch seeded callees' return-address register as secret, cascading
across the call graph. Guard: `ArgIdx <= 7` (AAPCS64 args are only X0–X7 / V0–V7). Effect on
the Firefox `FilterNodeSoftware` TU: **sound-mode instrumentation 78 → 5** (the genuine
secret set). Test: `taint-analysis-lr-not-arg.mir`.

> **Attribution correction:** we *originally* blamed the flood on the blunt-TOP memory model
> (`ExternalMemClobbered`) and built annotation-driven mode to suppress it. Controlled A/B
> (git-stash the fixes, rebuild, compare) proved that WRONG: the flood was the `$lr` artifact;
> `ExternalMemClobbered` was only the amplification channel. `memory/firefox-filter-dit-flood.md`
> is corrected accordingly.

**Fix B — passed-vs-live (commit `2d81ec4`).** A secret counts as reaching a callee only when
genuinely *passed* in an argument register (data or pointee), read on the state *entering* the
call — NOT merely live/clobbered across it (an ABI-compliant callee can't read a caller-saved
register it wasn't handed). Narrows `anyTaintedCallArgument` (gates the external-call
`ExternalMemClobbered`) and the ESCAPE report. Does not change the instrumentation count (the
flood was fix #1); it removes spurious escapes and makes the memory trigger sound. Preserves
the pointee channel (still a real reach). Test: `taint-analysis-call-arg-passed.mir`.

**`-taint-clobber-report` (commit `71be809`).** Lists every call site that makes the caller
treat memory as secret — the *sources* of a taint explosion — with reasons
(`external-arg`/`indirect-arg`/`modset-top`/`modset-argptr`/`modset-global`). Test:
`taint-analysis-clobber-report.mir`.

**Propagation-power result.** A controlled 6-level `noinline` chain (`playground/propagation_test.c`):
one seed flowed through **all 8 functions** via register args (down), memory write+reload
(callee→caller), and return values (up), with **zero false positives**. On real `-O2` Firefox
code, one `GenerateNormal<float>` seed propagates cleanly to `ColorComponentAtPoint`
(`arg 0 now pointee-tainted`). What limits *visible* cross-function spread on real code is
**upstream inlining** removing call edges before the MIR analysis runs — not the analysis. Use
`-O2` (the real target and canonical flag), not `-O3` (over-inlines).

## 9. Limitations (what this does NOT protect — be honest with reviewers)

1. **DIT coverage gaps (intra-procedural).** Even inside an instrumented function DIT does not
   cover: **divide / sqrt** (not DIT-listed), a secret used as a **memory address**
   (cache/TLB timing), or a secret-dependent **branch** (control-flow timing). These are
   surfaced by `-taint-uncovered-report`, NOT protected. They need algorithmic (constant-time)
   rewrites.
2. **Opaque callees.** External decls and indirect calls can't be analyzed → blunt TOP (whole
   memory poisoned) and best-effort inherited-DIT protection only. A callee that does a
   secret-dependent divide/sqrt, secret-addressed access, or clears DIT is outside the
   guarantee. Audit via `-taint-callsite-report` (ESCAPE lines).
3. **Soundness rests on an ABI-compliance assumption** — a callee that scavenges caller-saved
   registers it wasn't passed would need in-process code execution, which defeats DIT anyway.
4. **Cross-TU scope.** Interprocedural analysis is one TU/module. Cross-TU taint is not
   tracked — annotate the entry function in *each* TU that receives the secret. Also
   incompatible with LTO for that TU (lowers to object eagerly).
5. **Channel-3 memory gap:** an external callee that reads a secret *global* on its own (no
   secret argument) and re-exports it is not caught by the argument path.
6. **P1 memory precision deferred:** the mod-set is blunt (whole-object, weak updates, every
   truncation → TOP). Precise arg-i / per-offset provenance + a libc model table are P1;
   note that at the MIR stage, `getUnderlyingObject` often can't reach the `Argument` through
   optimized code (`memory/mir-stage-too-late-for-provenance.md`).
7. **Inlining flattens visible propagation** at higher opt (see §8). The analysis is correct;
   the call edges are just gone before it runs.
8. **FEAT_DIT required at runtime** (Apple M-series yes; Neoverse N1 → SIGILL). Verify via
   objdump/lit or `qemu-aarch64 -cpu max`.

## 10. Current state

- **Tests:** 22 lit tests pass — `llvm/test/CodeGen/AArch64/taint-analysis-*.mir` +
  `llvm/test/Transforms/TaintAnnotate`. Run:
  `build/bin/llvm-lit -sv llvm/test/CodeGen/AArch64/taint-analysis-*.mir llvm/test/Transforms/TaintAnnotate`
- **Placement defaults:** `region` granularity, `loop-hoist=0` (block-minimal).
- **Sound mode is the default**; annotation-driven is opt-in.
- **Unpushed commits:** `2d81ec4` (fix #1 + fix B), `71be809` (clobber report).
- **Validated:** flood 78→5 on `FilterNodeSoftware`; `FilterProcessingScalar` (2nd TU) does
  not flood (2/2, 6/7 with surface workers) — sound == annotation-driven on both. Holds at
  `-O2` (5 genuine functions, `ColorComponentAtPoint` reached via propagation).

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

## 12. Deeper reference docs (this doc is the map; these are the territory)

- `utils/taint_dit_spec.md` — what PSTATE.DIT actually guarantees (covered set; excludes
  divide/sqrt; address-timing not covered). `isDITProtected` is transcribed from this.
- `utils/taint_dit_placement.md` — placement state, gaps (G1/G2/G3), optimal-placement design.
- `utils/taint_dit_cost_model.md` — toggle (~30 cyc) + dwell (~15% SPEC), the measured numbers.
- `utils/taint_value_timing_leaks_research.md` — motivation: LVP, Firefox, THOR/AMX.
- `utils/taint_memory_summary_research.md` — mod-set summary design + P1 refinements.
- `utils/taint_ct_call_handling.md`, `utils/taint_cio_and_ct_literature.md` — prior art
  (CIO/Jasmin/FaCT/DECLASSIFLOW); read before any novelty claim.
- `utils/taint_firefox_integration.md` — Firefox integration guide.

## 13. Working preferences (carry these forward)

- **Never** add `Co-Authored-By` / session-link trailers to commit messages in this repo.
- Builds are allowed (`ninja -C build llc` / `clang` / `opt`); ~1–2 min each.
- Keep `CLAUDE.md` and the `utils/taint_*.md` docs in sync with code changes in the same turn.
- Over-approximation is always the safe direction: a spurious barrier costs performance, a
  missing one costs the secret. Any "can't classify" path must fall back to treating every
  register use as secret.
- Never classify instructions by mnemonic string (missed `STNP`/`STGP`/`STXP` store-pairs is
  an under-taint = leaked secret). Use the `getNumStoredValueRegs` opcode hook.
