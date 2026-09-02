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

**Design consequence - better than the boolean origin bit.** Split the mod-set by
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

**CT-Wasm, POPL 2019, §6.4.1** - verbatim:

> "A naïve port of TweetNaCl… would also require declassification in the
> `crypto_sign_open` API. **This function operates on public data - it performs
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
entry state - 90% of redundant re-evaluations eliminated on OpenSSL RSA. They
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
GCC's LCM answers "where must the mode be correct"; it never asks "is switching
profitable." (Confirmed: zero DIT references in any AArch64 `.cpp`/`.h`.)

**The obvious next claim is ALSO taken, and by a paper we must cite.** It is
tempting to say the novelty is that the driver is a *security taint analysis*
rather than a correctness requirement, and that there is an explicit *cost
model* deciding whether switching is worth it. **CryptoMPK has both** - see §3a.
Do not make that claim; §3b is the one that survives.

### 3a. CryptoMPK (IEEE S&P 2022) is the closest structural prior art

Jin et al., *Annotating, Tracking, and Protecting Cryptographic Secrets with
CryptoMPK*. Source, prebuilt binaries and dataset at
`cryptompk.code-analysis.org`; the notes below were taken from the paper AND
from reading the shipped artifact, which is where several of them only appear.

A compiler that annotates crypto secrets, propagates taint over whole-program
LLVM IR, and inserts writes to a per-thread hardware mode register (`WRPKRU`,
Intel MPK) around the code that must run privileged. Same shape as ours, three
years earlier:

| | CryptoMPK | ours |
|---|---|---|
| seeds | `#pragma tainter`, 39 tags over 8 apps | seed file, 21-23 symbols |
| propagation | context-SENSITIVE call tree, field-sensitive points-to, on `-O0` IR | context-insensitive mod-set + call-site gate, on MIR |
| switch | `WRPKRU`, "about 20 to 30 CPU cycles" (their microbenchmark) | `MSR DIT`, 9.7 cyc renamed / 22.6 serializing (our gem5) |
| granularity decision | Q score: weighted ratio of tainted to total memops vs a threshold | admission test in cycles, MBFI-weighted |
| soundness net | SIGSEGV + capstone single-step over the faulting access | verifier on the final MIR + the dynamic oracle |

**Cite it for three things, and concede the first two.**

1. **The cost model is not ours.** `FunctionModifyRunner::countTaint` computes
   `tainted/total` with `CALLFACTOR 30` and `LOOPFACTOR 10` and compares against
   a threshold, exactly to decide per-instruction versus whole-function
   placement. That is our admission test three years early.
2. **Nor is "a security analysis drives the switch."** That is the whole paper.
3. **Context sensitivity by cloning, with the bloat contained.** Algorithm 1
   hashes each context's *transformation scheme* and shares one body across
   contexts that hash alike. Measured in their shipped Nginx libcrypto: 153
   functions replicated into 318 bodies, +18.5% object size, 1,179 s of analysis
   on OpenSSL. This is the answer to "why not just clone" in
   `design/context-insensitivity.md`, with a price attached.

**Where they are weaker, stated only where we can show it.**

- **Every OpenSSL number they report is from a `no-asm` build.** Their scripts
  configure `./config -DOPENSSL_SMALL_FOOTPRINT no-asm shared`, and libsodium
  with `--disable-asm`. They had no choice: an IR pass cannot instrument
  perlasm, and OpenSSL's hot paths are perlasm on x86 exactly as they are on
  aarch64 (`dit-openssl-asm-limit.md`). Two consequences, and the second is the
  one that bites. **Nobody deploys `no-asm` OpenSSL** - it is several times
  slower, so the HTTPS results describe a configuration no server runs. And the
  C fallback `no-asm` selects for AES is the **T-table** implementation, whose
  real leak is cache timing from data-dependent table indices; MPK does not
  address that and neither does DIT. So the Apache and Nginx rows are measured
  on a build that is both slower than deployment and, for the primitive they
  spend the most time in, hardened against the wrong channel. **This is the
  limitation to raise, and it is one we share** - it is why our own reachable
  claim is "the ABI reduces DIT overhead on TLS 1.3 handshake key handling"
  (`dit-abi-nginx-tls.md`) rather than "we harden TLS".

  > **Do not repeat the threshold criticism.** An earlier revision of this file
  > said the shipped scripts pass "0.25, 0.20 and **0.01** depending on the
  > target - a 25x spread, undisclosed." The shipped dataset does not support
  > that. Every `build_protected.sh` in `dataset.tar.gz` (sha256 `ab8125b6...`)
  > passes `-threshold=0.25`, except ccrypt at `-threshold=0.24`; an exhaustive
  > grep of the extracted tree finds no `0.01`, no `0.20` and no `0.5`. A 4%
  > spread across seven targets is not a hand-tuning charge. Checked 2026-08-31.
  > What survives is only that the score itself is **unitless**, where ours is in
  > cycles against a measured switch cost - and that we measured the crossover
  > instead of picking it.
- **No region formation.** `insertWrpkruInst` wraps *each* tainted instruction
  in its own enable/disable pair; no merging, no hoisting. Measured in their
  shipped Nginx libcrypto: 547 `WRPKRU`, median 45 bytes between consecutive
  switches, 242 of 546 gaps under 32 bytes. At their own 20-30 cyc, a pair costs
  40-60 cycles to protect roughly ten instructions. That is why the Q score has
  to be so aggressive.
- **Their headline overhead is mostly not the isolation.** Table V pairs every
  result with a switches-compiled-out arm but never subtracts. Doing so:
  **median 0.74 points** attributable to privilege switching, under 2.4 points
  in 13 of 16 cases. The rest is relocating crypto buffers onto a pkey-bound
  jemalloc heap.
- **Statistical rigour.** Single run per arm, fixed arm order (protected always
  before baseline), no dispersion anywhere in the paper or the scripts; the
  whole HTTPS result is one `ab -n 1000 -c 20`. They do have a matched
  round-trip baseline and a mechanism-off ablation, which are the two controls
  that matter most, so credit those.

**One thing to take from them outright:** the `mxor` declassification tag.
Annotate plaintext/ciphertext as public and taint flowing out of it dies. Their
measurement on Nginx + OpenSSL: 3.06% of memops labelled with it versus 9.5%
without, a 3.1x precision gain from one extra annotation kind. We have no
analogue. (Their *implementation* of it is unsound in the under-protecting
direction - one declassified field permanently declassifies the whole
`AliasObject` - so take the tag, not the object-level rule.)

#### Can we reuse their benchmark suite? Partly - the annotations, not the workloads

Checked 2026-09-01 against the shipped `dataset.tar.gz` (sha256 `ab8125b6...`,
same artifact as above). Seven targets: `ccrypt-1.11`, `libhydrogen`,
`libsodium`, `apache-2.4.43`+`openssl-1.0.2u`, `nginx-1.17.10`+`openssl-1.0.2u`,
`opensmtpd-6.0.3p1`+`glibc-2.27`, `vsftpd-3.0.3`+`glibc-2.27`.

**The workloads mostly do not transfer, for a reason worth stating in the
paper: their threat model selects a different axis than ours.** MPK protects
where a secret *buffer lives*, so they pick programs with long-lived secret
memory. DIT governs what the *ALU does*, so we need secret-dependent arithmetic
to be hot. The two are independent, and libhydrogen shows it inside one library:

| libhydrogen path | primitive | DIT-relevant ops |
|---|---|---|
| `hydro_secretbox_*`, `hydro_hash_*` | Gimli | **zero multiplies** - only `^ & \| << >>`, all fixed-latency |
| `hydro_sign_*`, `hydro_kx_*` | X25519 on `__uint128_t` limbs | 41 in `hydro_x25519_mul` alone, 17 in `sc_montmul` |

CryptoMPK protects both paths and cannot see that difference. Confirmed by
disassembly, not predicted: `build/bin/clang -O2 -c hydrogen.c`, then count
`mul/umulh/madd` per symbol.

**But the multiplies do not save it, and this was measured, not assumed.**
`libhydrogen_enc` fails our framework's first question *by construction* - no
multiplies, nothing for DIT to slow down. The prediction that `libhydrogen_sign`
would therefore pass it is **wrong**: blanket DIT on the signing loop is free on
M5, 25 paired reps at alternating arm order, 2000 signatures per rep.

| message | nodit | blanket | blanket vs nodit | blanket faster in |
|---|---|---|---|---|
| 4 B | 208.675 ms | 208.570 ms | **-0.05%** | 13/25 |
| 1024 B | 223.479 ms | 223.322 ms | **-0.07%** | 15/25 |

That is indistinguishable from zero, and it is exactly what our own libsodium
screen already says: blanket is free on `x25519`, and libhydrogen's signatures
*are* X25519. The lesson is that the presence of DIT-relevant opcodes is not
sufficient - a 64x64 multiply on M5 is already fixed-latency, so DIT removes
nothing. **Check the primitive against the existing screen before believing an
opcode count.**

**So the whole suite is out on our first question or duplicates work we already
have**: libhydrogen free on both paths, libsodium already experiments 02 and 07,
nginx/Apache already experiment 05 and on a `no-asm` EOL OpenSSL besides, ccrypt
a T-table AES whose real leak DIT does not cover, and the two libcrypt daemons
Linux-only.

Three further blockers, in order of how much they cost to work around:

- **Their drivers have no public lane.** The only non-crypto work in all four
  shipped `src/*.c` is `fopen`/`fseek`/`fread`, which is kernel time, and
  `libhydrogen_enc.c` performs a *single* encrypt call rather than a loop. There
  is no knob that moves the secret fraction, so none of them can produce a
  crossover without being rewritten - at which point they are no longer
  third-party workloads.
- **Every OpenSSL row is `no-asm`** (above), on `openssl-1.0.2u`, EOL since 2019.
  Our experiment 05 already measures nginx + OpenSSL 3.5.4 as shipped and reports
  the reach limit honestly, which is strictly the better result.
- **OpenSMTPD and vsftpd need a Linux target** (`glibc-2.27` libcrypt). The gem5
  rig can run those; the M5 silicon rig cannot.

**What transfers is their taint output, and it is the most useful thing here** -
better than the tags. The artifact ships each target's **derived** taint set as
`(file, line)` pairs, so the comparison is not "did we write the same
annotations" but "did two independent analyses find the same secret-dependent
code". That is a third-party answer to "you chose your own seeds", which no
internal measurement can give. Their `sinktaint` declassification marker is the
analogue we already noted we lack.

**Done: `paper_experiments/08-seed-ground-truth/`.** On libhydrogen - the only
target whose whole library is a single TU, so our per-TU analysis and their
whole-program LTO analysis see the same code - we agree on **79.4%** of the
functions they mark, at 58.7% precision, aggregated to functions because they
analyse `-O0` IR and we analyse `-O2` post-RA MIR. Three findings came out of
it, two against us: the seed *point* moves recall 32.4% -> 76.5% (an argument
seed covers 12.0% of the seeded function, a buffer seed 99.2%, because
`hydro_hash_final` writes a secret through an argument pointer the P0 mod-set
cannot attribute); seven functions including both X25519 multiplies are never
marked at any seed and are not `AlwaysEnteredWithDIT` either; and our
information-loss report emits **zero** records for all of it, because its
severity criterion watches call boundaries and this secret is lost inside one
TU.

**Two defects in the shipped artifact**, found while checking the above:

- `src/libsodium_sign.c` is **byte-identical to `src/libhydrogen_sign.c`** (both
  sha256 `8bafa650...`): it `#include`s `hydrogen.h` and calls `hydro_sign_*`.
  There is no libsodium signing driver in the dataset, so the paper's libsodium
  signing row is measured by something not shipped, or on libhydrogen.
- Every `#pragma tainter` in all four shipped drivers is **commented out**
  (`//#pragma`). The drivers as distributed carry no annotations at all.

Neither changes their conclusions, and neither should be raised as more than a
footnote - but do not cite `libsodium_sign` as a libsodium result.

### 3b. What survives, and it is stronger

**You cannot always-on a memory domain.** "Grant everything" is not weaker
protection, it is no protection, so CryptoMPK's baseline is unprotected code and
selectivity is pure upside on a security story already told.

DIT is the opposite. `MSR DIT, #1` once at process start is *complete* security
at a measured 12.66% (M5) or 3.2% (gem5). Selective placement has to beat that,
and on four of nine Bitcoin Core benchmarks it does not.

**Ours is therefore the first setting in this literature where the
selective-versus-blanket question is even askable** - every prior selective
system, CryptoMPK included, is measured against no protection at all. That
asymmetry also explains why we needed an instrument they did not: MPK faults on
an unauthorised access, so a memory oracle is complete for a disclosure threat
model; DIT gives no fault and governs *computation*, so a page-based oracle is
structurally incomplete and shadow taint in a simulator is required. See
`design/verification.md`.

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
this problem across three venues - treat as the likely concurrent competitor.

**Published DIT/DOIT costs.** `msr DIT` = **12.0 cyc** Firestorm / **10.0 cyc**
Icestorm (Dougall Johnson); `msr SSBS` 30.08, `isb sy` 28.01 for scale. Phoronix
measured Linux DOITM at **<1% geomean** on Raptor Lake - so *blanket may be nearly
free on current x86*. Our counter is GoFetch fn. 22: **DIT disables the DMP on
M3**, plus Intel's own "may be significantly higher on future processors."

## 5. Prior art for "over-tainting has a runtime cost"

- **CryptoMPK** (§3a) is the precision half rather than the cost half: their
  `mxor` declassification takes labelled memory operations on Nginx + OpenSSL
  from 9.5% to **3.06%**, a 3.1x gain, and they cite DynPTA at 12.79% on the same
  target. They never convert that into time, so quote it as a precision result,
  not a performance one.

- **Serberus** (S&P'24) - the best-controlled ablation. Widening the taint *source*
  set alone: geomean **21.3% → 65.8%**, worst case **646.3%**.
- **SplittingSecrets** (UW) - ed25519 on M1: **verify** 2.43x (no secrets) /
  **2.44x** (precise) / **67.83x** (all-secret). Same shape as our Bitcoin Core
  verify result. They gave up on static analysis for our reason: *"Achieving such
  precision is not feasible through static analysis alone."*
- **SpecTaint** (NDSS'21) - three false positives on a hot path ≈ **60%**.
  *"This also demonstrates the importance of high precision."*
- **ConTExT** (NDSS'20) - over-annotating `EVP_CIPHER_CTX`: **338%**.
- **Blade** (POPL'21) - 80.2% → 5.0% geomean; **and a published reversal**:
  Baseline-S 25.8% vs Blade-S 26.6%, the smart analysis *slower*, because
  *"the sharp increase in the number of protects… ends up being slower than using
  (fewer) fences."* That is our thesis, published and measured.
- **ERIM** (USENIX'19) - the crossover model: 0.04%-1.0% at 100k switches/s, up to
  **144%** at 89.3M/s. Also: rare switches are individually *more* expensive
  (i-cache eviction), and their prescribed unimplemented fix is our loop hoisting.

## 6. Five things our claims must survive

1. **Alignment.** Marinaro et al. (AsiaCCS'24) found NOP substitution recovered the
   performance of "unnecessary" hardening on ARM. **Addressed**: our NOP control
   attributes 100.2% of the cost to switches
   (`docs/results/dit-modset-callsite-gated.md` §5c).
2. **Sign inversion in selSLH.** Spectre Declassified (S&P'23): selSLH masks
   *public* loads, so over-tainting is free performance there. **Do not cite its
   "80-95% of protections saved" as support** - the sign is opposite to ours, and
   those are static counts from an unimplemented pass.
3. **Blanket may be cheap on current hardware** (Phoronix <1% on x86). Lead with
   GoFetch's M3 DMP fact.
4. **The Spectre SoK states the opposite of our thesis**, which is our framing:
   *"mitigation tools can afford to be less precise than verification or detection
   tools."* Our +51% is a direct counterexample - when the mitigation is a mode
   switch that disables microarchitectural optimisations, imprecision *is* the cost.
5. **Precision may buy less than expected.** SelectiveTaint's full cloning-based
   context sensitivity (up to 50 min/binary) improved speedup only 1.53x → 1.77x
   and still over-tainted ~10x vs dynamic truth. Answer with a measured ablation.

## 7. What is genuinely unclaimed

- **Imprecision priced in cycles.** The entire summary-analysis literature measures
  precision in alarms or points-to set size. Ours drives a code transformation, so
  imprecision has a runtime price. Nobody in that literature can make this
  measurement. **A prior cost model does exist** (CryptoMPK's Q score, §3a) but it
  is a unitless ratio against a hand-set threshold, and they never measure what
  their own over-tainting costs; the distinction to claim is calibration in
  cycles against a measured switch cost, plus a measured crossover.
- **Blanket beating an *automatic* selective placement.** Careful: **CryptoMPK
  IS an automatic selective placement** (§3a), so the old form of this claim -
  "every selective system published is manual annotation (SpectreGuard,
  PROSPECT, SplittingSecrets, ConTExT) or hardware tags" - is false and must not
  be repeated. What survives is narrower and better: nobody has compared a
  selective placement against a BLANKET one, because in every prior setting
  blanket is not a defence at all. For MPK, "grant everything" is no isolation;
  for DIT, the mode bit set once is complete protection at a measured cost. Ours
  is the first setting where the comparison exists to be made.
- **A per-parameter interprocedural memory-effect summary computed POST-REGISTER
  ALLOCATION.** Searched 2026-09-02; **this is an unfound-in-search claim, not a
  proven absence**, and it must be written that way. The search found the two
  halves shipped separately and never together:

  | system | interprocedural summary? | over what? | when |
  |---|---|---|---|
  | Spike (PLDI 1997) | yes, reusable, call-graph propagated | **registers only** | post-RA, post-link |
  | PLTO | yes | **scalar stack-depth bounds** | post-RA |
  | Alto | - | "rudimentary and conservative" aliasing, by its own admission | post-RA |
  | GCC `ipa-modref` | yes, per-parameter, precise | **memory objects** | GIMPLE, **pre-RA** |

  So: register-scoped summaries exist post-RA; per-parameter memory-effect
  summaries exist pre-RA; nobody was found combining them. **I verified the GCC
  half myself** - `ipa-modref` is a tree/GIMPLE pass, confirmed from its source -
  and the post-RA half is second-hand from a search that reports reading the Spike
  and PLTO papers in full. LLVM's own structure corroborates the gap from the
  other side: `MachineFunctionPass` is strictly per-function with no module-level
  cross-function analysis facility, which is why `TaintInterprocPass` had to be
  built as a novel module pass inserted after PEI.

  **State it scoped, and never as "nobody has done this".** The defensible form is
  that the combination was not found, that its two halves are shipped separately
  by named systems, and that the reason is structural rather than accidental: the
  named IR objects a memory summary needs are exactly what register allocation
  destroys. Given this file's own history of retracted novelty claims (§8), assume
  this one is wrong until someone has read Spike's summary representation
  directly.

- **Secret fraction as the deciding variable.** Never stated in this form. Closest:
  PROSPECT's sweep (blanket 110% → 145% as the secret fraction rises, precise flat
  at 100%) and SpectreGuard's *"secrets that are accessed infrequently will have
  negligible overhead."*

## 8. Corrections made during verification

- **2026-08-30: two novelty claims retracted after reading CryptoMPK (§3a).**
  §3 claimed novelty for "a security taint analysis drives the switch" and "an
  explicit cost model decides whether switching is worth it". CryptoMPK has
  both, at S&P 2022. §7 claimed "every selective system published is manual
  annotation or hardware tags, there is no automatic selective placement to
  compare against"; CryptoMPK is one. The paper was found by reading its
  artifact, not its abstract - several of the sharpest points (the per-benchmark
  threshold spread, the absence of region formation, the unpublished
  switches-only build arm) appear only in the shipped source and binaries.
  **Lesson: read the artifact of the closest prior work before writing a
  novelty claim.**
- `msr DIT` **does** have a published cycle cost (12.0/10.0); an agent's claim that
  none exists was wrong.
- oo7 reports **72%** (arXiv, coreutils) and **430%** (TSE, SPECint) for nominally
  the same blanket strategy. **Never quote a single canonical "blanket costs X"** -
  the spread across this corpus is >10x. Always pair the number with benchmark and
  placement rule.
- Every 5x+ selectivity win in this literature is **gem5**; real-hardware results
  cluster at **1.7-2x**. Concede this ourselves - it converts our
  real-app-vs-benchmark gap from a weakness into a contribution.
