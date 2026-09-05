# Session handoff, 2026-08-26 .. 08-30: LTO, the EH fix, and where the LTO cost actually is

> **HISTORICAL (2026-08-30).** The current entry points are `CLAUDE.md` and
> `docs/reference/harden-runbook.md`; the LTO/ABI work below was closed on
> 2026-08-31/09-01 and is not shipping.

**Read this first if resuming (as of 2026-08-30).** Everything below is measured unless marked otherwise.
Nothing in this session is committed except the LLVM work (see §5).

---

## 0. Status: both measurement arms COMPLETE (updated 2026-08-30)

`-taint-dit-assume-callees-preserve` (§4) both arms finished and timed. Results in §4.
Raw data committed to `docs/results/data/{nra_timing,sign_timing}.csv` and
`docs/results/data/reassert.txt`.

Nothing is running. Driver scripts were in the session scratchpad (`measure_nra.sh`,
`time_nra.sh`, `time_sign.sh`); recreate if needed.

---

## 1. The headline finding

Full-LTO Bitcoin Core with the pass: **127,740 switches across 3,773 functions,
+25.9% on CoinSelection** - a benchmark where the pass otherwise beats blanket DIT 15/15.

The decomposition is the point:

```
LTO      116,611 sets : 11,129 clears  = 10.5 : 1
non-LTO       76 sets :      19 clears =  4.0 : 1
```

~3,773 entry sets, **~112,800 after-call re-asserts. 88% of the cost is re-asserts,
not taint coverage.** It is `3,773 functions x ~34 switches each`.

Two fixes were tried against the *3,773* and both were near-worthless:

- `operator delete` + `doesNotReturn` + `getMemoryEffects` refinement: **-0.09%**
  (127,793 -> 127,676). Clobber sites are not the unit of damage - one surviving
  clobber poisons a whole function, so retiring 38% of sites recovered 6 functions.
- The `$dit`/verifier merge: **+64 switches**, i.e. nothing.

**Method lesson, recorded because it cost two wrong turns:** both failures came from
measuring *inputs to a saturating stage* (clobber-site counts) instead of the *output*
(set:clear ratio). The ratio took 30 seconds and would have redirected the work.

---

## 2. Reference numbers (all post-merge, current compiler)

| build | dir | switches | notes |
|---|---|---|---|
| non-LTO, pass | `build-gated-v2` | **95** (76/19) | unchanged by the `$dit` merge |
| non-LTO, round-trip control | `build-nodit-v2` | 0 | empty seed file |
| non-LTO, NOP control | `build-nop-v2` | 0 msr / 96 nop | `-taint-dit-nop-switches` |
| C++ instrumented, non-LTO | `build-cxx-v2` | 95 | seeds in another module => no spread |
| full LTO | `build-lto-v2` | **127,740** (116611/11129) | verifier PASSED |
| non-LTO, re-asserts deleted | `build-nra-v2` | **47** (28/19) | UNSOUND, measurement only |

Silicon benchmarks: CoinSelection base 3,015,683 ns/op; LTO 3,917,121.
gem5 rig: `utils/dit_host_screening/btc/btc_gem5.py --bench sign|coinsel`.

---

## 3. Settled results worth keeping

**gem5 is a valid instrument.** CoinSelection blanket DIT: **+11.06% gem5 vs +13.01%
silicon** (85%). Mechanism isolated: **entirely EVES**, the value predictor - IPC
1.769 -> 1.960; DMP, comp-simp and SIP contribute nothing. This corrects
`evaluation-framework.md` §5's "gem5 understated 4.6x", which came from one workload.
Doc: `docs/results/dit-bitcoin-coinsel-gem5.md`.

**SignTransactionECDSA, both instruments.** Silicon 40 reps: blanket +3.39% (26/40),
pass +7.71% (34/40); the NOP control attributes **+6.56% to the switch instruction
itself** and +1.58% (noise) to layout. gem5: pass +0.32% serializing -> -0.59% renamed;
`commitNonSpecStalls` 4,623 vs 1. Doc: `docs/results/dit-bitcoin-sign-two-instruments.md`.

**Wallet secret-fraction sweep**, 8 knob points x 8 arms x 20 reps, closure check passes.
**Crossover at f ~ 45%**, not the framework's ~20%. But the causal variable is region
*count*, not fraction - each signature is one more region, so the two are confounded.
Doc: `docs/paper/bitcoin-secret-fraction-sweep.md`. Rig: `run_wallet_sweep.py`,
`analyze_wallet_sweep.py`; knobs `BTC_BENCH_INPUTS` / `BTC_BENCH_CHAIN` / `BTC_BENCH_SIGN`
in `~/Documents/bitcoin/src/bench/wallet_create_tx.cpp` (uncommitted).

**LTO is sound but expensive.** The merged `$dit` + final-MIR verifier passed over the
whole-program module with `tailduplication`, `postmisched`, `machine-outliner` etc all
downstream. The LTO problem is cost, not correctness.

---

## 4. The re-assert measurement: RESULTS

`-taint-dit-assume-callees-preserve` (hidden, default off, `TaintAnalysis.cpp`) makes
`calleeLeavesDITSet` return true unconditionally: every after-call re-assert is deleted
with **no callee-side entry read**, so the binary is **UNSOUND and must never ship or be
quoted as a hardened result**. Every number below is therefore an **upper bound** - the
real design pays a callee-side save/restore this build skips.

| config | benchmark | switches | pass vs control | after deletion | prize |
|---|---|---|---|---|---|
| LTO | CoinSelection | 127,740 -> **15,272** | +26.59% | +5.21% | **-16.89%** (25/25) |
| non-LTO | SignTransactionECDSA | 95 -> **47** | +8.45% | +3.35% | **-3.51%** (23/30) |
| non-LTO | CoinSelection | 95 -> 47 | +0.14% | +0.10% | -0.04% (see trap) |

Sets go 116,611 -> 6,233, so **94.7% of LTO sets were re-asserts.** Three reads:

1. **LTO never wins.** Even with every re-assert gone (+5.21%) it is worse than the
   non-LTO pass on the same benchmark (+0.14%).
2. **The non-LTO prize alone justifies the two-pass compile.** Deletion removes 60% of
   the pass's total overhead on signing (+8.45% -> +3.35%), independent of LTO.
3. **Benchmark trap:** the non-LTO build instruments only C (`CMAKE_C_FLAGS`), so all 95
   switches are in `secp256k1.c.o`, which **CoinSelection never executes**. Its -0.04%
   (14/25, a coin flip) measures nothing. Check the instrumented objects are on the
   benchmark's hot path.

**CORRECTION - the DIT verifier cannot validate this deletion.** An earlier draft of
this doc claimed "VERIFIER PASSED, so every deleted re-assert was unnecessary" and that
the verifier would fatal on sites needing the §2 invariant. Both are wrong.
`AArch64DITVerifier.cpp` is a `MachineFunctionPass` doing intra-function one-bit
dataflow whose comment states calls are treated as **transparent** by design ("Whether a
callee clears DIT is decided by the taint pass from its summaries... This pass exists to
catch damage done AFTER placement, not to re-litigate it"). That is the same assumption
the flag injects, so it could never have fatalled. The switch counts and timings stand;
the soundness claim does not.

**What does partition the re-asserts is the report's own reason token**, which is
strictly better than the verifier would have been. On `secp256k1.c` (53 re-asserts, the
whole non-LTO switch set):

```
by reason:  20 indirect   18 propagates-unresolvable   15 clears-on-exit   0 external
by callee:  20 <indirect>   14 scalar_set_b32   5 scalar_mul   5 ecmult_gen_gej   9 singletons
```

`clears-on-exit` = the callee is instrumented and genuinely clears, so those 15 are
load-bearing and no annotation removes them. The 20 `indirect` are recoverable and
benign: 8 are `util.h:103` (`cb->fn(...)`, the cold `ARG_CHECK` error callback whose
defaults abort), the rest are the nonce / ellswift function pointers whose targets sit
in the same TU. **Zero external callees** - libsecp256k1 is self-contained C.

Open: the report shows 53 re-asserts but sets moved 76 -> 28 (48). Deleting re-asserts
perturbs later placement decisions, but that has not been confirmed as the cause.

---

## 5. Code state

**`~/Documents/llvm-project`, branch `dit-clone`, HEAD `bf44296f0a1b`.**
One commit of mine (`524e87cbe1bc`) + a merge of `origin/dit-tainter`. Uncommitted:
the measurement flag in `TaintAnalysis.cpp`.

My commit does three things:
- **Removes the MIR round-trip.** `-ftaint-harden` ran codegen 3x through MIR text; the
  pass is now a module pass immediately after `PrologEpilogInserter`, via a new
  `TargetPassConfig` post-PEI hook + `addPassesToEmitFileWithPostPrologEpilogModulePasses`.
  This fixed a hard blocker: `EH_LABEL` prints with an empty MCSymbol, so any C++ TU with
  a landing pad aborted (Bitcoin died at 13/386); and `LandingPadInfo`/`TypeInfos`/
  `FilterIds` are not serialized at all, so fixing only the symbol would have emitted
  objects with EH labels and **no exception tables**.
- **Wires LTO.** `LTOBackend::codegen` uses the same entry point. Modules are
  self-describing (`moduleHasTaintSources` scans for the attributes), so no seed path is
  plumbed through the linker. `splitCodeGen` forced to 1 partition when seeds are present.
- **Seeds marked `noinline` at PipelineStart.** Without it a seeded `static` function at
  -O2 produced **zero** switches, and LTO internalization erased every seed in the module.

**Position is deliberate: post-PEI, not pre-AsmPrinter.** Hooking pre-AsmPrinter gave 227
static switches vs 105 for the same input; `081f4f3b` independently measured the same
effect (libsecp256k1 signing 26 -> 111) and rejects it as fragmentation.

**`~/Documents/gem5-DIT`** - untracked `benchmarks/bitcoin/` (both gem5 drivers,
`build_btc_arms.sh`, `build_coinsel_arms.sh`, `coinsel_stubs.cpp`). Merged to
`origin/master` earlier including the toggle-asymmetry fix; rebuilt.

**This worktree (`dit-tainter`)** - 3 new results docs, 6 rig scripts, 4 CSVs, and an
edited `evaluation-framework.md`. All uncommitted.

**`~/Documents/bitcoin`** - `src/bench/wallet_create_tx.cpp` modified (the 3 sweep knobs).

---

## 6. Open, in priority order

1. **Finish the measurement arm** (§0) and decide whether the prize justifies §5's
   two-pass compile.
2. **The two-pass compile is now wanted by three things**: `notail` (`disable-tail-calls`
   needs to mark functions before codegen, and the round trip it used to rely on is
   gone), `dit-unconditional-design.md` §5.2 B1 (pre-RA vreg, the LLVM `PSTATE.SM`
   pattern), and the re-assert deletion. Build it once.
3. **`disable-tail-calls` is now the applicable fix, not the cheaper one.** Its parked
   condition was "if LTO devirtualizes the dispatch tables". Measured: it does not -
   indirect tail branches 834 -> 799 (-4%), indirect calls 7,761 -> **8,477 (+9%)**. And
   the leak it fixes is 406x worse under LTO: functions that set DIT and never clear go
   **3 -> 1,218**, all of them exiting via a branch rather than `ret` (so not
   `AlwaysEnteredWithDIT`).
4. **libsodium says do not use LTO.** doc.libsodium.org/installation: *"Since different
   files are compiled for different CPU classes, and to prevent unwanted optimizations,
   link-time optimization (LTO) should not be used."* Corroborated in source - three weak
   symbol barriers named `_sodium_dummy_symbol_to_prevent_{memcmp,compare,memzero}_lto`.
   Two consequences: our pass runs *after* the LTO IR pipeline, so LTO could weaken a
   constant-time construct before we ever see it and the verifier would not notice (it
   checks DIT placement, not constant-timeness); and those barriers are external calls,
   which in our taxonomy are both clobbers and re-assert sites. **Any paper claim resting
   on LTO needs a dynamic constant-time check (ct-fuzz/dudect-style) on an LTO build.**
5. **Should the pass move later?** Answered no - see §5. But `docs/design/verification.md`
   and the verifier are the reason it is safe to stop worrying about position.
6. **`docs/results/dit-browser-filters.md` is still missing** and cited twice by
   `evaluation-framework.md` as the Skia negative control.

---

## 7. Research findings that change the framing

From a literature/industry sweep (three agents completed; two reports had the safety
classifier time out, so **spot-check quotes against primary sources before citing**):

- **Nobody ships automatic compiler-driven constant-time hardening.** BoringSSL, OpenSSL,
  libsodium, HACL\*, Jasmin/libjade, Mbed TLS all hand-write it; the only compiler
  involvement is *barriers to stop the compiler*. BoringSSL: *"There is no language or
  compiler support for expressing constant-time constraints in C... an inherently moving
  target."*
- **Declassification is nearly absent in research tools** (Constantine, SC-Eliminator,
  Raccoon, dudect: none; ct-fuzz: input-only, paper says post-input declassification is
  unimplemented). Only ct-verif has `public_out()`/`declassified_out()`. But **BoringSSL
  has 168 declassify sites against 27 secret-marking sites** - a 6:1 ratio suggesting the
  "mark the secret and propagate" model is inherently declassification-heavy. Ours has none.
- **Hand-written library models are the approach StubDroid (ICSE'16) criticises**;
  inferred summaries took one app from 41.7% -> 0% FPs and 7,309 MB -> 451 MB.
- **GCC's `ipa-modref`** is the named prior art for replacing our module-wide
  `ModuleUnknownMemTainted` bool: per-parameter bases, offset/size ranges, sentinel
  bases, bounded per-function collapse. **IFDS cannot represent a memory TOP at all.**
- **`FunctionSummary::ParamAccess`** (`ModuleSummaryIndex.h:840`) + `generateParamAccessSummary`
  (`StackSafetyAnalysis.cpp:1129`) is an in-tree template for carrying per-parameter
  analysis facts through ThinLTO. **Nobody has expressed taint over ThinLTO summaries** -
  searched Discourse, arXiv, DBLP. Publishable gap, but the analysis is MIR-level and the
  summary index is IR-level, which is the real obstacle.
