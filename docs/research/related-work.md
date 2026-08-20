# Related work: what is already published, and what our claims must survive

**Compiled 2026-08-19** from three parallel literature agents working over the
local paper cache plus direct fetches. Every load-bearing quote below was verified
against the source text. Where an agent's claim did not survive re-checking it is
marked.

---

## 1. The gate is a 30-year-old idea with four names

Our "apply the callee's mod-set only where the call site passes a secret" is not
novel. Cite these rather than let a reviewer find them:

| name | source | sound? |
|---|---|---|
| **Partial transfer function (PTF)** | Wilson & Lam, PLDI 1995 | yes |
| **Conditional MOD set** `CondIMOD(P,RA)` | Landi/Ryder/Zhang PLDI 1993; Ryder et al. TOPLAS 23(1) 2001 | yes |
| **Summary edge** keyed on the entry fact | Reps/Horwitz/Sagiv, POPL 1995 (IFDS) | yes |
| **Taint-summary rule** ("X tainted if incoming Y tainted") | Arzt & Bodden, StubDroid, ICSE 2016 | yes |
| **Effect / qualifier polymorphism**, `@PolyTainted` | Lucassen & Gifford POPL 1988; Foster et al. PLDI 1999 | yes |

**The distinction that matters.** Every prior version makes the guard **part of
the summary**, so it is sound by construction. Wilson & Lam apply our exact test,
but when no summary matches the call site they **re-analyse the callee**; we
**drop the effect**. Same test, opposite fallback, and the fallback is the entire
soundness argument.

The framing of our imprecision: our mod-set is an **independent-attribute**
abstraction ("writes a secret somewhere") of a transfer relation, having discarded
which input was secret. IFDS summary edges are **relational** (one edge per
(entry fact, exit fact) pair). The gate tries to recover the discarded relation
from outside the summary, which is why it cannot be sound. Sharir & Pnueli (1981)
name the axis: functional vs call-strings.

**Both major compilers already ship the sound version.**
`BasicAAResult::getModRefInfo` splits `MemoryEffects` into `ArgMem`/`Other`/
`InaccessibleMem`, refines `ArgMem` against the actual arguments, and only kills
`Other` when capture analysis proves no escape. GCC's `ipa-modref` builds a
per-call-site `parm_map` from `gimple_call_arg(call, i)` with
`MODREF_UNKNOWN_PARM` keeping the map total, plus per-parameter `EAF_*` flags.
**Our unsoundness is precisely that we apply the `ArgMem` refinement to the
`Other` component too.**

**Design consequence — better than the boolean origin bit.** Split the mod-set by
**source**: effects whose secret came from a parameter are gateable and
conditional on *which* parameter; effects from anywhere else are never gateable.
Strictly better than a per-function boolean, which would disable gating entirely
for a function with both sources. If the unsound gate ships anyway, cite the
**soundiness manifesto** (Livshits et al., CACM 58(2), 2015) for the vocabulary.

Selective context sensitivity (Introspective PLDI'14, Zipper OOPSLA'18, Scaler
FSE'18, Bean, Eagle, Turner) is **not** what we do: all of them choose where to
*add* contexts on top of a sound context-insensitive baseline, and none is
unsound. SelectiveTaint (USENIX'21) hits our exact problem in binary taint
analysis and solves it by **cloning**, which is the reviewer's first question.

## 2. Our exact bug is already published

**CT-Wasm, POPL 2019, §6.4.1** — verbatim:

> "A naïve port of TweetNaCl… would also require declassification in the
> `crypto_sign_open` API. **This function operates on public data—it performs
> public-key verification, but relies on helper functions that are used by other
> APIs that compute on secrets.** Since CT-Wasm does not support polymorphism over
> secrets, the results of these functions would need to be declassified. Trading-off
> bytecode size for security, we instead refactor this API to a separate untrusted
> module and **copy these helper functions** to compute on public data."

Signature verification, poisoned by shared signing helpers, fixed by hand-cloning
at **+85% code size**, with label polymorphism named as future work. **Frame our
contribution as automating what Watt et al. did by hand.**

**LightSLH** (arXiv 2408.16220, App. A) is the mechanism: summaries keyed on
**entry state including argument taint**, reused when a later call has the same
entry state — 90% of redundant re-evaluations eliminated on OpenSSL RSA. They
frame it as an analysis-time optimisation; framing it as the fix for
over-tainting *cost* is ours.

**Raccoon** (USENIX Sec'15 §7) states the open problem we close: taint that is
"flow-insensitive, path-insensitive, and **context-insensitive**… For large
programs, this over-approximation is a significant source of overhead."

## 3. Mode-switch placement by a compiler is established practice

LLVM ships `SIModeRegister` (AMDGPU FP rounding), `MachineSMEABIPass` +
`SMEPeepholeOpt` (**AArch64 PSTATE.SM/ZA**), `RISCVInsertReadWriteCSR`,
`X86VZeroUpper`. GCC has `optimize_mode_switching` on lazy code motion
(Knoop/Rüthing/Steffen PLDI'92), covering SH4 FPSCR, RISC-V vsetvl and **AArch64
SME PSTATE.SM**; its `TARGET_MODE_BACKPROP` hook is our hoisting algebra already
formalised.

**So "a compiler places hardware-mode-bit writes around regions" is not novel.**
What is: the driver is a *security taint analysis* rather than a correctness
requirement, and there is an explicit *cost model* deciding whether switching is
worth it. GCC's LCM answers "where must the mode be correct"; it never asks "is
switching profitable." State the claim that way. (Confirmed: zero DIT references
in any AArch64 `.cpp`/`.h`.)

## 4. DIT/DOIT specifically: the gap is real, and so is the objection

**"Let's DOIT"** (Arranz-Olmos et al., eprint 2025/759) is the closest work and
states the gap for us: *"there is very little work directly related to Intel's
DOIT or Arm's DIT execution modes… we are not aware of any prior works"*, and
*"we… leave Arm's DIT instruction set to future work."* They do instruction
*selection*, never mode *placement*, and recommend the blanket baseline.

**The objection, stated in our own toolchain's forum before we published.** LLVM
RFC "Constant-Time Coding Support" (Trail of Bits, 8 Aug 2025), reply by an Arm
toolchain engineer:

> "Arm's current guidance is that the operation of setting DIT can potentially have
> a performance cost, so you don't want to set it right inside each crypto kernel
> anyway – better to **set it once, call multiple crypto functions in a tight
> cluster**… then unset DIT when it's all done."

**Our measured switch cost is what settles this**, and it must be engaged head-on.
Note also that the same thread's participant `mhaeuser` (Marvin Häuser) is working
this problem across three venues — treat as the likely concurrent competitor.

**Published DIT/DOIT costs.** `msr DIT` = **12.0 cyc** Firestorm / **10.0 cyc**
Icestorm (Dougall Johnson); `msr SSBS` 30.08, `isb sy` 28.01 for scale. Phoronix
measured Linux DOITM at **<1% geomean** on Raptor Lake — so *blanket may be nearly
free on current x86*. Our counter is GoFetch fn. 22: **DIT disables the DMP on
M3**, plus Intel's own "may be significantly higher on future processors."

## 5. Prior art for "over-tainting has a runtime cost"

- **Serberus** (S&P'24) — the best-controlled ablation. Widening the taint *source*
  set alone: geomean **21.3% → 65.8%**, worst case **646.3%**.
- **SplittingSecrets** (UW) — ed25519 on M1: **verify** 2.43x (no secrets) /
  **2.44x** (precise) / **67.83x** (all-secret). Same shape as our Bitcoin Core
  verify result. They gave up on static analysis for our reason: *"Achieving such
  precision is not feasible through static analysis alone."*
- **SpecTaint** (NDSS'21) — three false positives on a hot path ≈ **60%**.
  *"This also demonstrates the importance of high precision."*
- **ConTExT** (NDSS'20) — over-annotating `EVP_CIPHER_CTX`: **338%**.
- **Blade** (POPL'21) — 80.2% → 5.0% geomean; **and a published reversal**:
  Baseline-S 25.8% vs Blade-S 26.6%, the smart analysis *slower*, because
  *"the sharp increase in the number of protects… ends up being slower than using
  (fewer) fences."* That is our thesis, published and measured.
- **ERIM** (USENIX'19) — the crossover model: 0.04%-1.0% at 100k switches/s, up to
  **144%** at 89.3M/s. Also: rare switches are individually *more* expensive
  (i-cache eviction), and their prescribed unimplemented fix is our loop hoisting.

## 6. Five things our claims must survive

1. **Alignment.** Marinaro et al. (AsiaCCS'24) found NOP substitution recovered the
   performance of "unnecessary" hardening on ARM. **Addressed**: our NOP control
   attributes 100.2% of the cost to switches
   (`docs/results/dit-modset-callsite-gated.md` §5c).
2. **Sign inversion in selSLH.** Spectre Declassified (S&P'23): selSLH masks
   *public* loads, so over-tainting is free performance there. **Do not cite its
   "80-95% of protections saved" as support** — the sign is opposite to ours, and
   those are static counts from an unimplemented pass.
3. **Blanket may be cheap on current hardware** (Phoronix <1% on x86). Lead with
   GoFetch's M3 DMP fact.
4. **The Spectre SoK states the opposite of our thesis**, which is our framing:
   *"mitigation tools can afford to be less precise than verification or detection
   tools."* Our +51% is a direct counterexample — when the mitigation is a mode
   switch that disables microarchitectural optimisations, imprecision *is* the cost.
5. **Precision may buy less than expected.** SelectiveTaint's full cloning-based
   context sensitivity (up to 50 min/binary) improved speedup only 1.53x → 1.77x
   and still over-tainted ~10x vs dynamic truth. Answer with a measured ablation.

## 7. What is genuinely unclaimed

- **Imprecision priced in cycles.** The entire summary-analysis literature measures
  precision in alarms or points-to set size. Ours drives a code transformation, so
  imprecision has a runtime price. Nobody in that literature can make this
  measurement.
- **Blanket beating an *automatic* selective placement.** No prior art, because
  every selective system published is manual annotation (SpectreGuard, PROSPECT,
  SplittingSecrets, ConTExT) or hardware tags. There is no automatic selective
  placement to compare against.
- **Secret fraction as the deciding variable.** Never stated in this form. Closest:
  PROSPECT's sweep (blanket 110% → 145% as the secret fraction rises, precise flat
  at 100%) and SpectreGuard's *"secrets that are accessed infrequently will have
  negligible overhead."*

## 8. Corrections made during verification

- `msr DIT` **does** have a published cycle cost (12.0/10.0); an agent's claim that
  none exists was wrong.
- oo7 reports **72%** (arXiv, coreutils) and **430%** (TSE, SPECint) for nominally
  the same blanket strategy. **Never quote a single canonical "blanket costs X"** —
  the spread across this corpus is >10x. Always pair the number with benchmark and
  placement rule.
- Every 5x+ selectivity win in this literature is **gem5**; real-hardware results
  cluster at **1.7-2x**. Concede this ourselves — it converts our
  real-app-vs-benchmark gap from a weakness into a contribution.
