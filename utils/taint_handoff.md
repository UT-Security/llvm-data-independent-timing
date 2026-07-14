# Session handoff — taint analysis / DIT hardening

**Written:** 2026-07-14, on the Linux dev box (Neoverse N1, **no FEAT_DIT**).
**For:** a fresh Claude Code session on an **Apple M4 Mac mini** after cloning this repo.
**Branch:** `interproc_taint`.

Read this file first. It is the entry point. Everything it references is committed.

## Why the M4 matters — the one thing this machine could not do

The M4 is Apple M-series = **FEAT_DIT hardware**. The Linux dev box is Neoverse N1 and
**lacks FEAT_DIT**, so `MSR DIT` SIGILLs there and DIT timing cannot be measured. The M4
unblocks the two measurements the whole DIT design is currently gated on (see
"Next actions"). This is the reason to move the work there.

## Threat model — corrected (the docs elsewhere are being fixed)

The project's real and only target is **PSTATE.DIT** — data-independent timing, i.e. the
data-operand-timing / instruction-centric channel (silent stores, computation
simplification; same family as the CIO paper). The ISB/DSB "speculation barrier" mode in
the code and in CLAUDE.md is a **placeholder / stand-in** for the DIT toggle mechanism, not
a goal. Do not treat speculation defense as in scope.

**Why the taint analysis exists — HALF OF THIS WAS MEASURED FALSE (2026-07-14, M4).**
The original argument was: DIT is not free — toggling costs a pipeline flush, *and* while it
is ON the core loses hardware optimizations — so DIT-everywhere (what the arm64 kernel /
BoringSSL do by hand) is expensive, and the project's value is enabling DIT only around
secret instructions.

The measurement (`utils/taint_dit_cost_model.md`) says:
- **Toggle cost: real** — ~30 cycles, a full pipeline flush, ~30× a call/ret. ✅
- **Dwell cost (running with DIT=1): ZERO** — ≤1% on every ALU, multiply, load, store,
  pointer-chase and streaming kernel tried, and whole-program DIT on the project's own
  `firefox_convolve_int` reference workload is **0.968x**, i.e. not a slowdown. ❌

So the objective function is just `toggles × 30 cyc`. On M4, DIT-everywhere is *not* the
expensive baseline we avoid — it is nearly free, and the "secret-awareness pays for itself
on performance" answer is **not supported on this hardware**. Read the cost-model doc before
planning any placement work; what still justifies the taint analysis (portability to cores
with nonzero dwell cost, toggle placement, and audit/correctness — e.g. tainted `SDIV` is
*not* DIT-covered, so DIT-everywhere is silent false assurance) is spelled out there.

## State of the work

- **Shipped & pushed** (commit `cb64535`): a −465-line dedup of the taint pass, a store-pair
  under-taint **security fix** (`getNumStoredValueRegs` hook), and 8→12 passing lit tests.
  Details in the git log.
- **Known-unsound, OPEN:** callee→caller taint through memory. A callee (external *or* in-TU
  direct) that writes a secret into caller-visible memory leaves the caller's reload
  untainted ⇒ **missing DIT region** = leaked secret. Reproduce with
  `playground/callee_memory_gap.c` (has a comment block showing the unprotected disasm).
- **Granularity problem:** DIT is currently applied at **function** granularity (`MSR DIT,#1`
  at entry of any function containing taint). That runs a whole function in DIT mode for one
  secret op — nearly the cost of DIT-everywhere. The project needs **region** granularity.

## The three research reports (committed, read in this order)

1. `utils/taint_memory_summary_research.md` — general interprocedural-taint-through-memory
   literature. Recommends a coarse **mod-set summary**: `{writes-secret-through-arg i}` +
   `{writes-secret-to-global g}` + `{writes-secret-to-unknown-memory}`, TOP default for
   external decls and unresolved indirect calls, refined by a libc model table and LLVM's
   `memory(...)`/`writeonly`/`argmemonly` attributes. This is the fix for the open bug.
2. `utils/taint_cio_and_ct_literature.md` — the CIO paper (Flanders/Kohlbrenner ASPLOS'24)
   dissected. **Same threat model as ours.** Closest prior work on level (post-regalloc
   MIR, chosen for spills — endorses our level choice verbatim). But CIO analyzes the whole
   *binary* in BAP (no per-function summary needed), rejected DIT on hardware-availability
   grounds, and paid **27.84× worst-case** for software instruction substitution instead.
   With FEAT_DIT available we solve CIO's problem far more cheaply — that is the story.
3. `utils/taint_ct_call_handling.md` — what the CT/Spectre tools do at a call. **Jasmin
   selSLH independently re-derived our exact mod-set summary** (per-function effect + TOP
   for the one opaque callee); FaCT confirms the arg-indexed half; **DECLASSIFLOW (CCS'23)
   published both the summary idea AND our exact "does not model memory contents"
   limitation.** ⇒ the memory fix is a bug fix, not a paper contribution. §5/§6 of that doc
   are the must-reads before any novelty claim.

## Next actions, in priority order

1. ~~**MEASURE (M4-only, the reason for the move).**~~ **DONE 2026-07-14.** Results and
   implications: `utils/taint_dit_cost_model.md`; benchmarks: `playground/dit_bench/`.
   `toggle ≈ 30 cyc (serializing)`, `dwell ≈ 0`. See the corrected premise above.
2. **Fix the soundness hole** (mod-set memory-effects summary, report #1). Independent of the
   cost model; it is a leaked secret today. **← now the top priority.**
3. ~~**Replace function-granularity with cost-model-driven region placement.**~~
   **CONTRAINDICATED by the measurement.** With dwell ≈ 0, narrowing a region can only *add*
   ~30-cycle toggles to save dwell that costs nothing. The correct direction is the
   **opposite**: *coarsen* — hoist toggles up and out of the call graph (set DIT once at the
   outermost tainted point; callees inherit it, since PSTATE.DIT survives calls and AAPCS64
   has no callee-saved rule for it), so a tainted leaf in a hot loop stops paying ~60
   cyc/activation. That is `taint_dit_placement.md` §5 (lazy code motion, objective =
   minimize *executed toggles*) plus the `PreservesDIT` summary bit — the design was already
   right; the measurement confirms its objective and kills the region-narrowing framing.
   The mode-switch-minimization idioms are still the right prior art:
   `AArch64/SMEPeepholeOpt.cpp` (back-to-back smstart/smstop — closest cousin),
   `RISCV/RISCVInsertVSETVLI.cpp`, `X86/X86VZeroUpper.cpp`.
4. **Re-run `playground/dit_bench/run.sh` on Graviton3 / Neoverse V1-N2.** The zero-dwell
   result is one microarchitecture; it is the single load-bearing assumption behind (3), and
   a core with real dwell cost restores the original premise verbatim.
5. **Before any novelty claim:** read Serberus/LLSCT (S&P'24) and DECLASSIFLOW (CCS'23) end
   to end — surfaced but not fully verified. Also note `AArch64SpeculationHardening.cpp` is
   already in-tree (AArch64, post-RA, a "taint" register, DSB/ISB) — have the "how is this
   different" paragraph ready. Add: with dwell measured at ~0, a reviewer *will* ask why not
   just DIT-everywhere — `taint_dit_cost_model.md` §"What still justifies the taint analysis"
   is the answer, and it is not a performance answer on M4.

## Also worth revisiting (not blocking)

The 3-phase serialize/reparse in `clang/lib/CodeGen/BackendUtil.cpp` may be unnecessary:
`MachineOutliner` is an in-tree `ModulePass` (`runOnModule`) that reaches every
`MachineFunction` via `MachineModuleInfo` with no MIR round-trip. If that pattern works
here, a large fragile chunk of `BackendUtil.cpp` and the `<mcsymbol>` strip could go away.

## Working-preferences that live in ~/.claude on the Linux box (recreate on the Mac if wanted)

- **Never** add `Co-Authored-By: Claude` / session-link trailers to commit messages.
- **Never** run builds (`ninja`/`cmake --build`) yourself — long; give the exact command and
  let the human run it and paste output. Build dir `build/`, e.g. `ninja -C build clang llc opt`.
- Keep `CLAUDE.md` and the `utils/taint_*.md` docs in sync with code changes in the same turn.
- Verification recipe used this session: harden `playground/firefox_convolve_int.c` and
  diff the `.text` section vs a saved baseline in both modes; run the
  `llvm/test/CodeGen/AArch64/taint-analysis-*.mir` lit suite (currently 12/12).
