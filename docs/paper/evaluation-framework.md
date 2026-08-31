# Evaluation framework: benchmarks that locate the public/secret boundary

**Status: the evaluation procedure for the paper.** Written 2026-08-20.
Every number quoted here is measured and linked to its source doc; nothing in
this file is a projection.

---

## 1. What the evaluation is for

The paper does **not** claim "fine-grained DIT beats always-on DIT." That claim
is false as a general statement, and this project has measured it false on real
software. What it claims is stronger and checkable:

> **Whether selective DIT placement pays is decided by the fraction of dynamic
> computation that is secret, by the granularity of the secret regions, and by
> the precision of the taint - and all three are measurable properties of a
> workload, before any compiler work is done.**

The evaluation's job is therefore to *span* that space rather than to find a
winner in it. A benchmark set containing only workloads where the technique wins
is not an evaluation, it is a selection effect - and the reviewer will say so.

The relationship, measured on both sides with the same pass and the same crypto
library:

| workload | secret fraction | pass vs always-on |
|---|---|---|
| Bitcoin Core `CoinSelection` | 0% | **-12.14%** (0/15 slower) |
| gem5 SQLite + ECDSA composite | 2.23% | **-2.66%** |
| coincurve / eth-account | 19.5% | **+2.74% worse** |
| Bitcoin Core `SignTransactionECDSA` | ~100% | +0.89% |

Same pass, same library, opposite verdicts. The denominator is the variable.

---

## 2. The benchmark design pattern

Every benchmark point is built from three parts. Only the third is ours.

```
   [ PUBLIC LANE ]        [ SECRET LANE ]        [ KNOB ]
   real library,          real crypto library,   invocation rate,
   no secret data         holds the key          sets f_secret
```

**Both lanes must be real libraries.** The public lane is a real workload
(B-tree descent, interpreter dispatch, branch-and-bound search); the secret lane
is real deployed crypto (libsecp256k1, libsodium, SQLCipher's provider). The
*only* constructed quantity is the ratio between them, and that is the
independent variable rather than a modelling choice.

This is what makes the design defensible: a reviewer can object to a ratio, but
cannot object that the code is synthetic.

### 2.1 Public lanes, and why each is in the set

Condition (d): the public lane's DIT sensitivity is the **size of the prize**.
Measured always-on cost, silicon unless noted:

| lane | library | mechanism | always-on DIT |
|---|---|---|---|
| branch-and-bound search | Bitcoin Core `CoinSelection` | production C++ tree search | **+14.4%** |
| interpreter dispatch | Lua 5.4.7 | bytecode dispatch chain | **+14.52%** |
| interpreter dispatch | CPython 3.16 | bytecode dispatch chain | **+7.00%** |
| B-tree / index descent | SQLite | serial pointer chase | **+6.09%** |
| DOM/JS rendering | Firefox / Chromium | mixed | +2.61% / +1.80% |
| real web application | Django + PyJWT | mostly C-layer work | **+1.2%** |
| **SIMD raster** | **Skia CPU filters** | **embarrassingly parallel** | **-0.03% +/-0.10** |

The last row is the **negative control and it is load-bearing.** It is a real,
widely deployed library whose code has *no* DIT sensitivity at all, measured to
a tenth of a percent with a verified 4x in-band positive control. A framework
that cannot say "there is no prize here" is not a framework. See
`docs/results/dit-browser-filters.md`.

The Django row is the calibration row: the same interpreter family reads +9.87%
on a pyperformance composite and **+1.2% on a real application** - benchmarks
overstate real software by ~7x, and the paper must label which is which.

### 2.2 Secret lanes

Condition (b): a **low** number here is a feature. If protecting the secret is
nearly free, then all of always-on's cost is potentially recoverable.

| lane | library | always-on DIT on the secret work |
|---|---|---|
| ECDSA sign | libsecp256k1 | ~free |
| AEAD / hashing | libsodium (13 primitives) | +0.1% |
| KDF | libsodium argon2id | +0.2% |
| page crypto | SQLCipher libtomcrypt / OpenSSL | granularity trap, see below |
| **constant-time inversion** | **libsecp256k1 safegcd** | **+23.3%** |

The last row is the counterexample that must be reported: the `volatile`
optimisation barrier that makes the code constant-time manufactures exactly the
value-predictable loads DIT de-optimises, so the *hardened* implementation is
penalised ~100x more than its unhardened sibling. Constant-time software and DIT
are not additive.

### 2.3 The knob

The knob must change **only** f_secret. If it also changes the public work, the
sweep confounds the two and the curve means nothing.

| benchmark | knob | changes only f_secret? |
|---|---|---|
| SQLCipher | `PRAGMA cache_size` | **yes** - same DB, same B-tree, same queries; only the page-decrypt rate moves |
| SQLite + libsecp256k1 | signatures per query batch | yes |
| CPython + coincurve | signatures per request | yes |
| Bitcoin Core | wallet send rate, `-assumevalid` | yes, and it is native to the application |
| SQLCipher (rejected knob) | database size at fixed cache | **no** - also changes B-tree depth |
| Bitcoin wallet | inputs per transaction | **yes, and verified rather than assumed** - `BTC_BENCH_SIGN=0` reruns each point without the real signatures, so f_secret is measured; the public lane moves only 14.21->15.53 ms while the secret lane grows 14x |

---

## 2.4 What we are looking for - the candidate checklist

Use this when hunting new benchmarks. It is ordered so the cheapest
disqualifier comes first, and every entry has cost someone here real time.

### Structural: can the pass even reach the code?

Check these BEFORE measuring anything. Each one has already killed a candidate.

| requirement | why | what it killed |
|---|---|---|
| **AOT compiled, no JIT on the hot path** | an MIR pass cannot instrument code generated at runtime | V8 / Node / Cloudflare Workers, LuaJIT / Kong / OpenResty |
| **not Rust** | rustc uses its own bundled LLVM; `-ftaint-harden` never runs | Gecko's Stylo, WebRender's frontend |
| **crypto built from source, in-tree** | a prebuilt `libcrypto.dylib` cannot be instrumented | SQLCipher's default OpenSSL provider (25 switches, none on any cipher instruction) |
| **no hand-written assembly on the crypto path** | perlasm bodies are opaque to the pass | verify at configure time: libsecp256k1 `assembly ... OFF`, libsodium `--disable-asm` |
| **secret enters through nameable entry points** | the seed must name every entry point the secret really passes through | eth-account entered via `sign_recoverable`, not `ecdsa_sign`; and again via per-signature key derivation |
| **direct calls on the hot path** | loop hoisting is intraprocedural and cannot move a region boundary that sits behind a function pointer | SQLCipher's `cipher_descriptor[]` indirect call |
| **embeddable, if a secret must be grafted** | needed to add a native `sign()` to an interpreter | git is not embeddable, and shells out to gpg |

### Behavioural: is there anything to win?

| condition | test | disqualifying result |
|---|---|---|
| **(d) public code is DIT-sensitive** | run public-only workload under blanket DIT | ~0% => stop, blanket is already free |
| **(a) secret fraction is small** | count secret-region entries; calibrate against a no-crypto build | >20% => prize collapses |
| **(b) secret code is DIT-insensitive** | run the crypto alone under DIT | large => that cost is unavoidable at any placement |
| **(c) regions are coarse** | cycles of work per region | <~1300 cyc => toggles eat the prize |

### The knob

A candidate is only a *benchmark* if it has a knob that moves the secret
fraction **without moving the public work**. Prefer knobs already in the
application: SQLCipher `PRAGMA cache_size`, Bitcoin Core wallet send rate and
`-assumevalid`, a signing rate in a request loop. Reject a knob that also
changes the public work - growing a database also deepens its B-tree.

### Anti-patterns: things that look promising and are not

- **Image / SIMD filters.** Embarrassingly parallel by construction, so OoO
  already hides the latency and there is nothing for value prediction to
  recover. Measured to +/-0.1% in a real browser on real Skia code. Do not
  spend more time here.
- **Bulk crypto as the whole workload.** No public code means no prize; both
  arms pay the same and the comparison cancels.
- **I/O-bound workloads.** Bitcoin Core `-reindex-chainstate` had CoV 6.7-7.3%
  and a null arm reading +3.12%, so a few-percent effect is unresolvable.
- **Adaptive benchmarks.** MotionMark's controller put the harness floor
  (+1.95%) above the effect. Prefer fixed work and wall time.
- **Microbenchmarks as evidence of size.** `lvp_chase` reads 4.0x; real
  applications land at 1-2%. Overstatement ~200x.

---

## 2.5 Running the sweeps on more machines

gem5 is **deterministic**: the same binary, config and input produce identical
cycle counts on any host, so a sweep can be split across machines and the
results pooled without a normalisation term. This is the one part of the
evaluation that parallelises freely - native timing does not, because it
requires an exclusive machine.

Rules for distributing a gem5 sweep:

1. **Distribute the binaries, do not rebuild per machine.** The MIR round-trip
   is a per-binary codegen lottery; two independently built "identical" binaries
   are not identical. Ship `benchmarks/sqlcipher/bin/` and check `shasum` before
   pooling anything.
2. **Ship the database too**, and give every parallel run its own copy - SQLite
   opens read-write and concurrent runs will corrupt a shared file.
3. **`QDB` must be a `file:...?vfs=unix-none` URI.** A bare path makes SQLite
   take fcntl locks that gem5's syscall emulation cannot service; the run fails
   with `journal failed: 3`.
4. **Re-check both gates on every machine**, not just the first: `simInsts`
   identical across switch models, and zero `ditSuppressed` in the unprotected
   arms.
5. **Pool only within a switch model.** `spec` and `serdit` are different
   machines, not different samples.

Command shape:

```
utils/dit_host_screening/sqlc_gem5.py \
    --caches 16,1024,1792,1920,2048 --arms plain,blanket,nodit,hoist \
    --configs spec,serdit --jobs <cores-2>
```

Split by `--caches` across machines; every point is independent.

---

## 3. The five quantities every benchmark point reports

This is the methodological contribution. The summary-analysis literature measures
precision in alarms or points-to set size; here imprecision drives a code
transformation, so it has a price in cycles, and these five numbers are what let
a reader predict a verdict instead of guessing it.

| # | symbol | what it is | how to measure |
|---|---|---|---|
| 1 | **f_secret** | fraction of dynamic cycles inside secret-tainted regions | count secret-region entries directly (SQLCipher: pager cache misses = page decrypts) and time the region separately |
| 2 | **C_public** | always-on DIT cost of the public lane alone | run the public lane with no secret, DIT injected process-wide |
| 3 | **C_secret** | always-on DIT cost of the secret lane alone | run the crypto in isolation under DIT |
| 4 | **R** | cycles of real work per DIT region | region count from the pass; cycles from gem5 or region timing |
| 5 | **T** | toggles executed per unit work | `MSR DIT` executions, and static switch count for context |

**Static switch counts do not predict dynamic cost.** Three cases in one day:
575->39 switches bought 2.4 points; 975->404 *cost* +44 points; and the real
Bitcoin Core win was confirmed by `ditSuppressed`, not by counting. Count to
understand a build; measure to know what it costs.

---

## 4. The decision procedure for readers

Four questions, in the order that fails fastest, each with a measured threshold.
A reader can run these against their own code without adopting our compiler.

**Q1. Is your public code DIT-sensitive at all?**
Run the workload with `PSTATE.DIT` set process-wide. If the cost is ~0%, **stop**.
Blanket DIT is already free and no placement policy can beat free. This kills most
candidates in a single measurement: libsodium (+0.1%), argon2id (+0.2%), Skia
filters (-0.03%), `firefox_convolve_int` (0.968x) all fail here.

**Q2. What fraction of your runtime is secret?**
- Above ~20%: most of blanket's cost is DIT over genuinely secret work that no
  placement can avoid. The recoverable prize collapses - on coincurve it was
  **0.64%** against an always-on cost of 2.66%.
- Below ~3%: public work dominates, and nearly the whole always-on cost is
  recoverable.

**Q3. How much work happens per DIT region?**
Below **~1300 cycles per region**, toggles eat the prize. Measured crossover from
a granularity sweep holding total secret work fixed while varying region count
4096x. At 9.7 cyc/switch (renamed) or 22.6 (serializing) against a dwell cost of
0.0039 cyc per suppressed op, the arithmetic is lopsided by three orders of
magnitude. SQLCipher's default configuration sits 3-4x *below* this line, which
predicted its loss before it was measured.

Also ask whether the region boundary is **movable**. Loop hoisting is
intraprocedural; if the boundary sits at a callee entered through a function
pointer, hoisting cannot reach it.

**Q4. How precise is your taint?**
False positives are pure waste, and they can dwarf everything else.
`secp256k1_ecdsa_verify` carried 17 switches for code holding no secret, costing
**+51%** on a benchmark where blanket DIT costs **-0.02%**. Call-site gating took
it to **+0.67%** with no coverage lost. Confirm precision with dynamic
suppression counts on a **no-secret** workload, never with static counts.

### The ceiling on all of it

DIT can only cost what the optimizations it disables are worth. gem5 feature
isolation on real workloads: **1-2% total**, carried by one mechanism that differs
per workload (EVES +1.44% on SQLCipher, DMP +1.89% on SPEC intspeed, SIP +0.78% on
gapbs). Microbenchmarks overstate this by ~200x - `lvp_chase --mode const` reads
4.0x on the same silicon. **Any cost/benefit argument built on the 4x figure is
wrong by that factor.**

---

## 5. Controls that must ship with every number

Each of these caught a real defect in this project. They are part of the
contribution, not boilerplate.

| control | what it prevents | how it failed here |
|---|---|---|
| **round-trip baseline** (`-ftaint-harden=<empty>`) | crediting MIR round-trip codegen to DIT | two retracted numbers; the artifact is workload-dependent (+0.58% QuickJS, 0.00% SQLCipher). Under gem5 the artifact is now known to be `argv[0]` path length and measures **0.00%** once arms share a path; whether the same mechanism inflates the NATIVE figures is untested |
| **null arm** (harness loaded, no DIT write) | crediting the harness to DIT | +0.30% on Chromium, +3.12% on Bitcoin reindex |
| **NOP substitution** (`-taint-dit-nop-switches`) | crediting code alignment to DIT | answers Marinaro et al. AsiaCCS'24; attributed 100.2% of a +51% to switches. **Gate it: the NOP arm must carry ZERO `msr DIT` AND be the same size as its twin.** Same-count/same-size/same-address are all true of a byte-identical arm, so checking only those hides an inert control - which is exactly what happened in the crossover rig |
| **rotated arm order** + duplicate baseline | drift masquerading as an arm effect | 1.3% bias on an instruction-identical arm |
| **in-band positive control** (`lvp_chase`) | a null result from a broken instrument | reads 4.00x; without it a table of 1.00x ratios is uninterpretable |
| **identical checksums across arms** | arms doing different work | standard in every rig |
| **region + whole-program arithmetic closes** | "placed somewhere irrelevant" reading as "placed well" | **trap 8, bit three times** |
| **linearity gate** (2x work -> 2x time) | measuring enqueue instead of execution | added for the fixed-work filter bench; reads 2.00 |
| **gem5: `simInsts` identical across configs** | a driver perturbing itself | driver called `clock_gettime`; gem5 SE returns simulated time |
| **gem5: zero `ditSuppressed` in the unprotected arm** | a contaminated baseline | baseline was silently running oracle placement (847,278 suppressions) |
| **coverage vs oracle** (`ditSuppressed` ratio) | under-protection reading as a win | must be >=100% of oracle |
| **machine exclusive** for native timing | contention | one run discarded |

> **The detector worth stating as a rule:** measure the protected REGION and the
> WHOLE PROGRAM in the same run and check the arithmetic closes. Whole-program
> timing alone cannot distinguish "placed well" from "placed somewhere
> irrelevant." An under-protecting oracle looks exactly like a win.

**Which instrument answers which question.** Revised 2026-08-26, when a second
workload was measured both ways. gem5 tracks always-on cost well on code whose
DIT sensitivity is real: Bitcoin `CoinSelection` reads **+11.06%** in gem5
against **+13.01%** on silicon (85%, different microarchitectures). The earlier
"gem5 understates 4.6x" figure came from a single workload and is a property of
that workload, not of gem5.

Where gem5 reports ~zero, check whether silicon resolves anything either:
Bitcoin `SignTransactionECDSA` reads -1.11% in gem5 against a **marginal** +3.39%
(26/40, p≈.08) on silicon, i.e. both instruments find little or no prize. Use
gem5 for *which placement is better*, for magnitude on DIT-sensitive public code,
and for the renamed-switch counterfactual that does not exist in hardware; use
silicon to decide whether a small effect is real at all.

> **The "-0.64% cross-binary resolution floor" is retracted as a floor** (2026-08-31).
> It was a **-0.64% round-trip artifact between binaries with identical
> `simInsts`**, read as irreducible. The mechanism is now identified: gem5 SE mode
> writes the binary path onto the initial process stack as `argv[0]`, so its
> LENGTH shifts stack alignment for the whole run. On libsecp256k1 one
> byte-identical binary measured 287,318 / 285,068 / 284,936 cycles at a 1-, 36-
> and 18-char name, a 0.84% spread from the file name alone; only the length
> matters, not the text. The Bitcoin arms `btc_sign_base` (13 chars) and
> `btc_sign_nodit` (14) differ, so they were never comparable. `btc_gem5.py` now
> runs every arm from an equal-length path, and with that fix the two secp arms
> with identical `.text` agree **to the cycle** (275,721 both) where they
> previously differed by 561. **The -0.64% must be re-measured before it is
> quoted again; it has not been.** See `dit-secp-tier2.md` §3.1.

Sources: `docs/results/dit-bitcoin-coinsel-gem5.md`,
`dit-bitcoin-sign-two-instruments.md`.

---

## 6. The benchmark set

| # | benchmark | public lane | secret lane | knob | status |
|---|---|---|---|---|---|
| 1 | **SQLCipher cache sweep** | SQLite B-tree descent | AES-256-CBC + HMAC per page | `PRAGMA cache_size` | **in progress** |
| 2 | SQLite + ECDSA | SQLite queries | libsecp256k1 sign | signatures per batch | gem5 point at 2.23%; curve missing |
| 3 | CPython + coincurve | interpreter + Django | libsecp256k1 via coincurve | signatures per request | both endpoints measured; middle missing |
| 4 | Bitcoin Core | wallet + mempool + validation | libsecp256k1 | **inputs per transaction** (`BTC_BENCH_INPUTS`) | 9 benches measured; **both endpoints now on gem5 too**; knob unpinned and demonstrated to span f_secret 4%-75%, sweep not yet run under the full rig -- see `bitcoin-secret-fraction-sweep.md` |
| 5 | **Skia filters** | CPU raster | n/a | n/a | **done - negative control** |
| 6 | libsodium | n/a | 13 primitives | n/a | done - fails Q1 |

Benchmark 1 is the exemplar because it carries the fraction story and the
granularity story in the same figure, on one unmodified real application, with a
knob that moves nothing but the decrypt rate.

---

## 7. Traps this project hit, stated so readers do not repeat them

1. **Under-protecting oracle** - bit three times. An oracle that misses an entry
   point protects less and therefore looks faster.
2. **Metric choice** - the same experiment read -6.28% using a benchmark's own
   score and +1.0% end-to-end, a 15x overstatement, because the score excluded
   setup, GC and the secret work.
3. **Benchmark selection inside a host** - picking the two most DIT-sensitive
   pyperformance bodies was itself cherry-picking; a real app on the same
   interpreter reads 8x lower.
4. **Insensitive is not null** - a "the pass matches the oracle" result measured
   at 0.02% secret fraction cannot distinguish good placement from bad.
5. **Adaptive benchmarks** - MotionMark's controller put the harness floor
   (+1.95%) *above* the effect. Fixed-work beats adaptive when the effect is
   small.
6. **Timer clamping** - Firefox clamps `performance.now()` to 1 ms by default,
   coarser than the effect being measured.
7. **Ratio gates with a noisy denominator** - gate on absolute times, never on a
   ratio whose denominator is the fast measurement.

---

## Sources

`docs/results/` - `dit-secret-fraction-decides` (memory), `dit-gem5-composite.md`,
`dit-coincurve-timing.md`, `dit-bitcoin-core-screen.md`,
`dit-modset-callsite-gated.md`, `dit-host-screening.md`,
`dit-oracle-composites.md`, `sqlcipher.md`, `quickjs.md`, `dit-cost-model.md`,
`dit-browser-filters.md`.
`docs/research/` - `real-world-instances.md`, `related-work.md`.
`docs/design/` - `context-insensitivity.md`, `source-condition.md`,
`p1b-frame-provenance.md`, `stack-arguments.md`.
