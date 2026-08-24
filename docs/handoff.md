# Session handoff — taint analysis / DIT hardening

> ⚠️ **HISTORICAL (written 2026-07-14). Do not use this as guidance.**
> The current entry points are [`README.md`](README.md) and [`overview.md`](overview.md).
> Most of what follows has been overtaken: the branch is now `dit-tainter`, the
> callee→caller memory bug is FIXED, region placement is the default (not function
> granularity), the lit suite is 29 tests (not 12), the flood attribution here and
> elsewhere was corrected (it was the `$lr` seeding bug, not the memory model), and
> end-to-end runtime has since been measured on four real workloads - one win and three
> negatives (`docs/results/`). Kept for the parts that still hold: the M4 rationale, the
> corrected threat model, and the prior-art reading order.

**Written:** 2026-07-14, on the Linux dev box (Neoverse N1, **no FEAT_DIT**).
**For:** a fresh Claude Code session on an **Apple M4 Mac mini** after cloning this repo.
**Branch:** `interproc_taint` (the work now lives on `dit-tainter`).

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

**Why the taint analysis exists:** DIT is NOT free. Toggling it costs a pipeline flush, and
while it is ON the core loses hardware optimizations. So DIT-everywhere (what the arm64
kernel / BoringSSL do by hand) is expensive. The project's entire value is enabling DIT
**only around instructions that operate on secrets** — hence taint. This answers the
research report's "is secret-awareness paying for itself?" question in the affirmative:
yes, because the secret-agnostic baseline (DIT always on) is the expensive thing we avoid.

**Both halves are now backed by numbers** (`docs/results/dit-cost-model.md`):
- **Toggle:** ~30 cycles, fully serializing, ~30× a `bl`+`ret` (measured, M4).
- **Dwell:** **up to ~15% on some SPEC 2026 benchmarks** with DIT fully on (measured by
  the project owner). This is the cost secret-aware placement exists to avoid.

```
cost = toggles × ~30 cyc  +  dwell(workload) × time_in_DIT
       \__ favours coarse __/   \__ favours fine-grained __/
```

The two terms pull opposite ways — that tension *is* the placement problem. The toggle
number gives the floor: a region costs ~60 cyc to enter+leave, so it must save more than
that in dwell to be worth creating.

⚠️ **Do not conclude "DIT is free" from a microbenchmark.** The M4 microkernels in
`playground/dit_bench/` show ~0 dwell, and an earlier version of the cost-model doc wrongly
concluded from them that coarse placement was optimal. They are scalar-integer,
small-working-set loops that contain none of the patterns DIT penalizes — they measured the
benchmark's blind spots, not DIT. Ground truth is SPEC 2026. Note this also makes
`firefox_convolve_int` (0.968x, DIT-insensitive) a **bad workload for evaluating placement**:
it has nothing to lose, so it cannot show a win.

**Horizon:** this project is forward-looking 5+ years. Future cores add *more*
data-dependent optimizations for DIT to suppress, so the dwell term — and the value of
fine-grained placement — trends **up**. Never let a favourable measurement on today's
silicon retire the premise.

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

1. `docs/research/memory-summaries.md` — general interprocedural-taint-through-memory
   literature. Recommends a coarse **mod-set summary**: `{writes-secret-through-arg i}` +
   `{writes-secret-to-global g}` + `{writes-secret-to-unknown-memory}`, TOP default for
   external decls and unresolved indirect calls, refined by a libc model table and LLVM's
   `memory(...)`/`writeonly`/`argmemonly` attributes. This is the fix for the open bug.
2. `docs/research/cio-and-ct-literature.md` — the CIO paper (Flanders/Kohlbrenner ASPLOS'24)
   dissected. **Same threat model as ours.** Closest prior work on level (post-regalloc
   MIR, chosen for spills — endorses our level choice verbatim). But CIO analyzes the whole
   *binary* in BAP (no per-function summary needed), rejected DIT on hardware-availability
   grounds, and paid **27.84× worst-case** for software instruction substitution instead.
   With FEAT_DIT available we solve CIO's problem far more cheaply — that is the story.
3. `docs/research/ct-call-handling.md` — what the CT/Spectre tools do at a call. **Jasmin
   selSLH independently re-derived our exact mod-set summary** (per-function effect + TOP
   for the one opaque callee); FaCT confirms the arg-indexed half; **DECLASSIFLOW (CCS'23)
   published both the summary idea AND our exact "does not model memory contents"
   limitation.** ⇒ the memory fix is a bug fix, not a paper contribution. §5/§6 of that doc
   are the must-reads before any novelty claim.

## Next actions, in priority order

1. **MEASURE — toggle half DONE (2026-07-14, M4), dwell half OWNED BY SPEC 2026.**
   `toggle ≈ 30 cyc, fully serializing` → `docs/results/dit-cost-model.md`, benchmarks in
   `playground/dit_bench/`. Dwell is **not** ~0 (the microkernels' ~0 is a benchmark blind
   spot — read the doc's "History" section before trusting any dwell microbenchmark).
2. **Fix the soundness hole** (mod-set memory-effects summary, report #1). Independent of the
   cost model; it is a leaked secret today.
3. **Replace function-granularity with cost-model-driven region placement.** Still the goal —
   the dwell term (up to ~15% on sensitive SPEC 2026 benchmarks) is what it buys. The toggle
   measurement now gives the **admission test**: a region costs ~60 cyc to enter+leave, so
   only create one if it removes more than ~60 cyc of dwell. The existing
   `-taint-region-merge-gap` knob was the hand-tuned proxy for exactly this; it has since
   been derived (`-taint-dit-switch-cyc`, default 30, with `-taint-dit-dwell-per-instr`)
   and the static knob was removed on 2026-08-24. Orthogonal and free: **hoist toggles out of hot leaves** across the call graph
   (callees inherit DIT; AAPCS64 has no callee-saved rule for it) — that removes toggles
   without extending dwell, so it wins regardless of the dwell number (`PreservesDIT`,
   placement doc §5.3). Mode-switch-minimization prior art: `AArch64/SMEPeepholeOpt.cpp`
   (back-to-back smstart/smstop — closest cousin), `RISCV/RISCVInsertVSETVLI.cpp`,
   `X86/X86VZeroUpper.cpp`.
4. **Find real-world DIT-sensitive workloads** where coarse placement measurably hurts —
   an explicit project goal, and a prerequisite for evaluating (3) at all, since
   `firefox_convolve_int` is DIT-insensitive (0.968x) and cannot show a win. Reducing the
   SPEC 2026 15% to a set of *code patterns* is the key sub-task: patterns are what make the
   region-admission test statically computable. (Deliberately deferred, not dropped.)
5. **Before any novelty claim:** read Serberus/LLSCT (S&P'24) and DECLASSIFLOW (CCS'23) end
   to end — surfaced but not fully verified. Also note `AArch64SpeculationHardening.cpp` is
   already in-tree (AArch64, post-RA, a "taint" register, DSB/ISB) — have the "how is this
   different" paragraph ready.

## Also worth revisiting (not blocking)

The 3-phase serialize/reparse in `clang/lib/CodeGen/BackendUtil.cpp` may be unnecessary:
`MachineOutliner` is an in-tree `ModulePass` (`runOnModule`) that reaches every
`MachineFunction` via `MachineModuleInfo` with no MIR round-trip. If that pattern works
here, a large fragile chunk of `BackendUtil.cpp` and the `<mcsymbol>` strip could go away.

## Working-preferences that live in ~/.claude on the Linux box (recreate on the Mac if wanted)

- **Never** add `Co-Authored-By: Claude` / session-link trailers to commit messages.
- ~~**Never** run builds yourself~~ **superseded**: `CLAUDE.md` now says running builds is
  fine, just start them in the background. Build dir `build/`, e.g.
  `ninja -C build clang llc opt`.
- Keep `CLAUDE.md` and the `docs/` tree in sync with code changes in the same turn.
- Verification recipe used this session: harden `playground/firefox_convolve_int.c` and
  diff the `.text` section vs a saved baseline in both modes; run the
  `llvm/test/CodeGen/AArch64/taint-analysis-*.mir` lit suite (currently 12/12).
