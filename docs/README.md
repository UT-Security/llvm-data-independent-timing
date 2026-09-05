# FastDIT documentation

Interprocedural taint analysis + PSTATE.DIT hardening for AArch64, built as an LLVM
fork (branch `dit-tainter`). Secret data entry points are declared in a taint-source
file, taint is propagated through registers/stack/global memory at the MIR level
across all functions of a TU, and PSTATE.DIT mode switches are inserted so
secret-dependent code runs with data-operand timing side channels suppressed.

**Threat model: data-operand instruction timing (DIT), NOT speculation.** Speculation
defense is out of scope. Operating instructions (build, flags, gotchas) live in
`CLAUDE.md` at the repo root; this folder is the reference material behind them.

> **A second, separate implementation lives in [`fences/`](fences/README.md).** That is
> the x86-64 line: secrets declared per argument in a CSV file, taint propagated over
> LLVM IR use lists, and mitigation by `seq_cst` fences rather than a mode bit. It was
> developed independently on `main` and is **not** the pass documented here - different
> target, different mitigation primitive, no shared code. Everything in *this* directory
> is FastDIT/AArch64 unless it sits under `fences/`.

## Read in this order

| # | Doc | Why |
|---|---|---|
| 1 | [overview.md](overview.md) | The map. What it is, how to run it, how the analysis works, limitations, current state. |
| 2 | [reference/dit-spec.md](reference/dit-spec.md) | What the hardware actually guarantees. Everything else assumes this. |
| 3 | [results/dit-cost-model.md](results/dit-cost-model.md) | What DIT costs. Read before any placement work. |
| 4 | [results/quickjs.md](results/quickjs.md) | The first positive result, and the method rule that makes it trustworthy. |
| 5 | [results/sqlcipher.md](results/sqlcipher.md) | The definitive negative, and the gem5 study that bounds the whole thesis at ~1.4%. |
| 6 | [design/dit-placement.md](design/dit-placement.md) | Where switches go, which gaps remain, and the optimal-placement design. §5.7 is the intra-block default. |
| 7 | [reference/harden-runbook.md](reference/harden-runbook.md) | **The current recipe.** What the 2026-09-05 defaults do, the seed loop, the control arms, the M4/M5 notes. |
| 8 | [design/dit-callee-contract.md](design/dit-callee-contract.md) and [design/dit-cloning.md](design/dit-cloning.md) | The two mechanisms that own every switch today: the callee contract and the twins. |

> **Which docs describe the design as it is (2026-09-05), and which are records.** The
> defaults are the callee contract, the DIT twins, intra-block region placement at
> `switch-cyc=30` with loop hoisting, tail calls off, the mod-set gate; the pass runs
> inside codegen after PEI with no MIR round-trip. Every doc carries a dated status
> line at its top; a doc whose status says PROPOSED, HISTORICAL, SUPERSEDED, RETIRED or
> NOT SHIPPING is a record of how a decision was reached, not a description of the
> compiler. The 2026-09-05 audit added or corrected those lines in: `overview.md`
> (pipeline, flags, current state), `design/dit-placement.md`, `design/dit-abi.md`,
> `design/dit-unconditional-design.md`, `design/frame-addr-fallback.md`,
> `design/stack-arguments.md` (§7), `reference/dit-abi-runbook.md`,
> `reference/firefox-integration.md`, `results/quickjs.md`, `results/dit-cost-model.md`,
> `results/dit-callee-contract-2026-09-04.md`, `results/session-2026-08-30-handoff.md`,
> `handoff.md`, and the experiment READMEs measured before the default flip.

## Overview

- **[overview.md](overview.md)** - the single consolidated entry-point doc: what this
  is, the threat model, how to run it, how the taint analysis works, where the pass
  sits in the pipeline (no MIR round-trip since 2026-08-30), how cross-function memory
  is handled, and the current state and next actions as of 2026-09-05.

## Reference

Stable facts that other docs and the code depend on.

- **[reference/dit-spec.md](reference/dit-spec.md)** - what PSTATE.DIT actually
  guarantees: the covered instruction set, the exclusions that matter (divide and
  sqrt), and the fact that address-dependent timing is not covered. The
  `isDITProtected` membership hook in `AArch64InstrInfo.cpp` is transcribed from this
  doc, so **keep the two in sync**.
- **[reference/firefox-integration.md](reference/firefox-integration.md)** - how to
  put the pass into a real Firefox build: the one-flag model, the toolchain
  requirement, taint-source file format, the two correctness constraints, and the
  recommended compiler-wrapper integration.

## Design and internals

How the analysis is built, which bugs were found in it, and what is still open.

- **[results/dit-secp-tier2.md](results/dit-secp-tier2.md)** - the four
  under-taint channels do NOT occur in libsecp256k1 ECDSA signing: 464,796 of
  464,800 secret-operand instructions run protected, with a null control that
  reports 464,800 to prove the instrument is looking. Answers the question
  `results/dit-flowprobe-undertaints.md` left open. Read its scope section.

- **[design/verification.md](design/verification.md)** - how the two instruments
  that check the hardening work: a static verifier on the FINAL MIR (placement
  integrity, fatal) and a dynamic oracle (analysis soundness, two tiers). Why
  neither subsumes the other, with the two bugs each found that the other could
  not see. **Read before trusting any "the pass is sound" claim.**

- **[design/dit-placement.md](design/dit-placement.md)** - the central design doc for
  where DIT switches go: what exists today, the placement constraints the spec
  imposes, the remaining security and performance gaps, and the proposed spec-aware
  optimal placement with its evaluation plan. §5.7: intra-block placement is the
  default since 2026-09-05 (`-taint-dit-sub-block`; `=0` is block placement).
- **[results/dit-abi-nginx-tls.md](results/dit-abi-nginx-tls.md)** - **the ABI on a
  deployed server.** nginx + OpenSSL, TLS 1.3, session reuse off so every connection
  is a full handshake. Hardening the reachable C key schedule costs **+0.65%** of
  server CPU; with the ABI **+0.21%**, so the ABI removes two thirds (13/15 sign
  test). Narrow by necessity - see the assembly limit below. Method notes worth
  reading: server CPU not throughput (the client burns 7x the server's CPU), and
  `getrusage` not `ps -o time=` (10 ms granularity returned exactly one tick).
- **[results/dit-openssl-asm-limit.md](results/dit-openssl-asm-limit.md)** - **a
  scope limit with no workaround inside the compiler.** OpenSSL implements AES,
  AES-GCM, ChaCha20-Poly1305, P-256, bignum and SHA on aarch64 as hand-written
  perlasm, so the pass cannot instrument the crypto in the most widely deployed TLS
  stack - and unlike the SQLCipher prebuilt-library case, building from source does
  not fix it. `no-asm` makes it reachable but is a strawman. What IS reachable is
  the C TLS 1.3 key schedule in `ssl/tls13_enc.c`. Reason libsodium works: its
  primitives are C.
- **[results/dit-abi-measured.md](results/dit-abi-measured.md)** - **the ABI's
  numbers and the default decision.** Region placement, Apple M5, pinned toolchain.
  Non-LTO 95 -> 57 switches for no measurable time; full LTO 127,744 -> 15,462
  (8.26x) for **-5.40% CoinSelection (25/25)** and **-8.52% signing (27/30)**. The
  predictor is switches-per-instrumented-function (5.9 vs 51.1), not the workload.
  Default stays OFF because LTO+ABI is still slower than non-LTO without it.
- **[design/dit-callee-contract.md](design/dit-callee-contract.md)** and
  **[results/dit-callee-contract-2026-09-04.md](results/dit-callee-contract-2026-09-04.md)** -
  **`-taint-dit-contract=callee`: every function protects its own secrets.** A call
  is never a Need, unseen callees are UNCOVERED obligations with a repair, seeding
  is monotone. Measured opt-in: mbedTLS +6.17% renamed vs +3.50% inherit at 99.88%
  vs 99.93%; libsodium's shipped seeds protect nothing until the report's 21 lines
  are pasted. Two findings that outrank it: glibc's `memcpy` blinds the oracle (the
  libsecp256k1 nonce derivation was never protected), and callee-saved restores
  reload the caller's secrets.
- **[reference/harden-runbook.md](reference/harden-runbook.md)** - **how to harden a
  library with the shipped defaults (callee contract + twins, since 2026-09-05) and run
  it on an M4/M5**: the two-build recipe with the owned list, the seed loop to its
  fixpoint, what to check in the objects, the six control arms, and what the M-series `MSR DIT`, which serialises like gem5's serialising model, means for reading the numbers.
- **[design/dit-cloning.md](design/dit-cloning.md)** - **DIT twins**
  (`-taint-dit-clone-seeded`, default on since 2026-09-05): every seeded function and everything it
  reaches in its TU gets a `<name>.dit` copy that is entered DIT-on by construction
  and emits no switch; calls from DIT-on code are redirected to it, across TUs
  through the seed file and the owned list, no LTO. libsodium signing under the
  callee contract: 10,400 executed DIT writes -> 41 (inherit 6) at identical
  coverage, +21% text.
- **[results/dit-twin-narrowing-2026-09-05.md](results/dit-twin-narrowing-2026-09-05.md)** -
  narrowing twins (`-taint-dit-twin-narrow`, opt-in): a twin that clears at its
  top and re-enables at its first secret. Does not pay on libsodium: nothing
  narrows at the shipped switch cost, and with switches free 0.7% of wasted
  coverage costs 3x the writes and +5 points renamed on signing. Two analysis
  fixes it exposed (twin argument taint, NEON register tuples) apply everywhere.
- **[results/dit-external-preserves-2026-09-05.md](results/dit-external-preserves-2026-09-05.md)** -
  the external-callee assumption (`-taint-dit-external-preserves`, opt-in): a
  callee outside the build never writes PSTATE.DIT, so no re-assert after it. libsodium 358 -> 214
  switch sites at identical coverage; ed25519 and AES-GCM down to the
  hand-placed bracket's two switches per call; argon2id's 395,758 re-asserts
  after `memcpy` gone. Renamed-model cost unchanged: it was never the switches.
- **[results/dit-intra-block-default-2026-09-05.md](results/dit-intra-block-default-2026-09-05.md)** -
  intra-block placement as the default, measured on libsodium: switch counts
  and oracle identical (the hot code is whole twins), timing inside the layout
  band; the substantive result is the stack-passed pointee seeding bug the
  flip exposed and fixed.
- **[results/returns-pointee-2026-09-04.md](results/returns-pointee-2026-09-04.md)** -
  **the `ReturnsPointeeTainted` summary bit and the seeded-callee return gate.**
  A callee returning a pointer INTO secret memory returned a public pointer
  (flowprobe C1/C5); the bit, the address-of-secret-global and store-through-
  pointer rules, module-wide pointer-holding globals, and a return gate that
  honours seeds close both. libsodium byte-identical, libsecp256k1 +9 sites for
  no coverage change, mbedTLS +15.8% sites with the record layer's
  secret-dependent error codes newly covered; dynamic cost on the resumption
  workload measured inside.
- **[reference/dit-abi-runbook.md](reference/dit-abi-runbook.md)** - **how to RUN the
  ABI.** Build steps (`ninja -C build` with no target list, because the analysis also
  links into `libLTO.dylib` and a targeted build leaves it stale and silently wrong),
  the exact LTO invocation (`llvm-ar`, `-lto_library`, and the module flag that carries
  the request into the linker), how to read the switch/carrier/guard counts, what a gem5
  run sees and where it overstates cost, and the known limitations.
- **[design/dit-abi.md](design/dit-abi.md)** - **THE CONTRACT, read before the two
  below.** Settled 2026-08-30: **PSTATE.DIT is callee-saved.** Obligation on every
  instrumented callee, `d_out == d_in` at every exit it controls; guarantee to every
  caller, `d_out >= d_in`, unconditionally. Call sites emit nothing, which removes all
  four re-assert classes by construction with no LTO and no annotation. Two decisions
  came with it: tail calls disabled TU-wide (no two-pass compile needed) - **which as of
  2026-09-01 is no longer the ABI's, it rides on `-ftaint-harden` for every hardened
  build, see `-taint-no-tail-calls`** - and `_Unwind_Resume` / `longjmp` / `musttail` /
  MachineOutliner sites degrade to the guarantee and are reported as `NONLOCAL` lines
  rather than fixed. Supersedes the framing in `dit-tailcall-gap.md` and
  `dit-unconditional-design.md` §2.
- **[design/dit-callee-ownership.md](design/dit-callee-ownership.md)** - the
  ownership rule adopted 2026-08-08: *only the frame that turned DIT on may turn it
  off*. An instrumented callee used to clear DIT on exit, so callers re-asserted after
  every call and the switch count scaled with the CALL count. Shipped:
  `AlwaysEnteredWithDIT` fixes resolvable in-TU calls (under the inherit contract only;
  since 2026-09-05 the callee contract and the twins do this job by default), and
  `-taint-dit-reassert-report=<file>` audits the rest. **Deferred (out of scope, for
  advisor discussion): the runtime `MRS` mode**, the only thing that fixes indirect
  *and* cross-TU calls. Measured: `MRS DIT` = 1.00 cyc vs `MSR DIT` = 30.34, so per
  call 90.67 -> 2.01.
- **[design/dit-unconditional-design.md](design/dit-unconditional-design.md)** -
  **PROPOSED, not implemented.** How to DELETE the after-call `MSR DIT` re-assert rather
  than cheapen it. One invariant ("no instrumented function returns with DIT cleared
  relative to entry") lets `calleeLeavesDITSet` return true for every callee, including
  indirect and cross-TU, so call sites emit nothing. The load-bearing section is the
  speculation analysis: **guarding an ENABLE is forbidden** (a mispredicted skip runs
  secret work at DIT=0 and the footprint survives the squash), **guarding a CLEAR is
  safe** (a wrong-path clear cannot un-gate, because gem5 defers the publish to
  `commitHead`). Also: the gem5 `DitCC` register is a suppression gate, not a value, so
  `MRS` cannot read it; the proposal is to split off an exact `DitVal`.
- **[design/dit-tailcall-gap.md](design/dit-tailcall-gap.md)** - the tail-call gap
  fixed 2026-08-05. Whole-function placement cleared DIT *before* a tail call, so the
  callee receiving the secret ran unprotected (found on libsodium `crypto_sign`). Also
  records the permanent residual: after a tail call DIT may stay set indefinitely, so
  **an instrumented function does not restore DIT on every exit path**.
- **[design/dit-precision.md](design/dit-precision.md)** - the DIT precision metric
  (`-taint-dit-precision-report`): per function, how many instructions MUST run with
  DIT versus how many DO. `precision = need/underdit` is the number placement should
  maximize, but only against a switch budget, and **always read the loop-weighted
  variant** - unweighted, convolve's region placement looks 13 points better while
  being 7.16x slower.
- **[design/context-insensitivity.md](design/context-insensitivity.md)** -
  context-insensitive mod-sets are **the dominant false-positive source** (measured on
  libsodium: 169 of 199 FPs). Also records that P1b is a far smaller lever than
  assumed: only 17 of 583 secret-writing call sites resolve provenance to an argument.
- **[design/taint-domain.md](design/taint-domain.md)** - the analysis domain as a
  product lattice (2026-09-03): one abstract value (Data, Pointee) for registers and
  memory cells alike, one memory map, provenance kept apart from pointee taint and why,
  the proof that the old `Address` kind was redundant, the byte-identical differential
  verification, and the audit of every place "unknown" still reads as clean (Phase 2's
  worklist).
- **[design/spill-soundness-bugs.md](design/spill-soundness-bugs.md)** - two spill
  soundness bugs fixed 2026-07-27 (`implicit-def` counted as a use; narrowed reload of
  a spilled secret), plus what spilling *does* handle correctly.
- **[design/frame-addr-fallback.md](design/frame-addr-fallback.md)** - the
  `-taint-frame-addr-args` prototype, **deleted 2026-08-24**: the `f(&local_secret)`
  under-taint it closed, why the whole-frame approach cost +44 points once the mod-set
  gate existed, and why that under-taint is now an open gap.
- **[design/scalability.md](design/scalability.md)** - the compile-time wall, found
  and fixed 2026-08-10. `-ftaint-harden` could not compile `quickjs.c` (54k lines, 940
  functions); now **733 s = 10.7x** baseline with 29/29 tests passing. Overhead is a
  flat 1.3-1.5x up to ~100 functions. Cause was `JS_CallInternal`: 1890 blocks, a
  209-predecessor computed-goto hub. **The decisive fix was skipping
  `propagateTaintMBB` when `IN` is unchanged** (the transfer is a pure function of IN,
  so it cannot change OUT); `join` early-out plus reserve helped; the RPO worklist did
  *not* (the CFG is irreducible, one giant SCC, so there is no useful topological
  order). Still open: **taint spread** - one narrow taint source produced 13,222
  `MSR DIT`, 618 of them inside `JS_CallInternal`.

## Results

Measured numbers. These are the claims the project stands on, with their caveats.

- **[results/dit-cost-model.md](results/dit-cost-model.md)** - **the cost model, and
  read it before any placement work.** Toggle is ~30 cyc serializing (measured, M4);
  the read is free (`MRS DIT` = 1 cyc, M5); dwell runs up to ~15% on sensitive SPEC
  2026 benchmarks. Both terms matter and they pull opposite ways, and that tension
  *is* the placement problem. Also carries the **2026-08-03 libsodium negative**:
  blanket DIT is free (1.00-1.02x) while taint-driven placement costs +46%..+94% at
  the shipped defaults, roughly half that when tuned for serializing switches. Do NOT
  conclude "DIT is free" from the ~0 microkernels; that blind spot is documented in
  the doc's History section. Benchmarks in `playground/dit_bench/`.
- **[results/quickjs.md](results/quickjs.md)** - **the first positive result for the
  performance claim (2026-08-10/11).** QuickJS + Octane on M5, end-to-end wall time:
  always-on ~+1.0%, fine-grained's true DIT cost +0.06% +/-0.21, so fine-grained
  recovers ~1.07%, essentially the whole always-on cost.
  - **MUST-READ METHOD RULE: baseline fine-grained against a round-trip control
    (`-ftaint-harden=<empty file>`), never the stock -O2 build.** The 3-phase MIR
    round-trip alone cost +0.58% +/-0.24 with zero `MSR DIT` emitted. That artifact
    was ~10x the real DIT cost and, charged to DIT, produced two retracted claims.
    **Scoped 2026-08-30:** the clang path no longer round-trips and its empty-seed
    build is byte-identical to `-O2`; the rule still binds the `llc` wrapper.
  - A **-6.28% figure was first reported and is WRONG** as an end-to-end number: it
    came from the Octane score, which covers only the timed `run` sections (~14% of
    wall) and excludes setup, GC, *and* the secret work.
  - Make-or-break factors: taint containment (an arbitrary taint source gave 13,222
    switches, 618 in the interpreter, so **declassification decides generalisation**)
    and **`-taint-dit-loop-hoist=1`** (without it there is no win at all: 3/8, in-loop
    toggles eat everything). Caveats: constructed workload, secret fraction 0.04%, n=8.
  - **Granularity crossover, measured 2026-08-11** (dwell held constant, only region
    count varies): fine-grained wins at 8k-128k DIT regions (-0.49%/-0.35%), **loses
    above ~550k regions**, and is 10x worse than always-on at 32.8M regions (+9.78%).
    Crossover is ~40k regions/sec = **~1 us of work per region**. **Two axes: secret
    fraction sets the size of the prize (~1% here), granularity decides whether you
    can collect it.**

- **[results/sqlcipher.md](results/sqlcipher.md)** - **the definitive negative, and
  the study that bounds the thesis.** With a correct oracle (all three provider entry
  points, `cipher` + `kdf` + **`hmac`**) the recoverable headroom on M5 is **+0.89%**
  on the deprecated libtomcrypt provider and **-0.08%, i.e. ZERO**, on the DEFAULT
  shipping OpenSSL/hardware-AES build. Almost all of always-on's +8.6% is DIT *on the
  crypto*, which any correct placement must also pay. A **"+8.15% first positive
  result" was reported and RETRACTED the same day** - the oracle had wrapped 2 of 3
  entry points, so it was protecting *less*, not costing less. **Audit a manual
  placement for coverage before believing its performance.** Two findings that stand:
  the pass **structurally cannot instrument prebuilt `libcrypto.dylib`** (25 sites,
  none on a cipher instruction), and software AES is DIT-expensive only because of
  **T-table data-dependent loads**, whose real leak - cache timing - DIT does not
  cover, so **AES is a bad motivating workload**.
  - **gem5 corroboration, 2026-08-13** (full study:
    `gem5-DIT/docs/dit/studies/sqlcipher-dit-placement-2026-08-13.md`). Running the
    identical binary under serializing vs renamed `MSR DIT` isolates **toggle cost
    with dwell held constant**, which silicon cannot do: **+0.08% / +12.8% / +19.1%**
    for **6 / 54 / 63** switch sites. It reproduces the M5 ordering and the
    region:hoist ratio (1.49x vs 1.52x) at about a third the magnitude, so the
    granularity result no longer rests on one machine.
  - **The prize is ~1.4%**, all of it value prediction; DMP, SIP and comp-simp are
    inert or net-negative on this ROI. **The shipped placement spends 19% to protect
    something worth 1.4%.** No placement policy can recover more than the gated
    optimizations are worth - and **microbenchmarks overstate that value ~200x**
    (`lvp_chase` 4.0x vs 1-2% on real code).
  - **The round-trip control is necessary but NOT sufficient.** Under gem5 the
    zero-`MSR DIT` `nodit` binary is the **slowest in the entire matrix** (+2.65% vs
    plain), exceeding the whole dwell effect. The artifact is a per-binary codegen
    lottery (+0.58% QuickJS, +0.06% native, +2.65% gem5), so at ~1% effect sizes only
    a **same-binary, two-configuration** comparison is trustworthy.

## Research

Literature reviews and motivation. These carry "verified" and "refuted" sections;
respect the refuted ones.

- **[research/value-timing-leaks.md](research/value-timing-leaks.md)** - **why this
  project exists.** Non-crypto value-dependent-timing leaks. The Apple M4 has a Load
  Value Predictor and DIT disables it (FLOP, USENIX Sec'25); DIT-for-secret-regions is
  that paper's own recommendation, and whole-process DIT costs 4.5% on Speedometer, so
  fine-grained taint-driven placement is the cheap version. Also covers the Firefox
  subnormal-FP pixel-stealing leak, its integer "fix", and how the LVP reopened it.
- **[research/browser-history-leaks.md](research/browser-history-leaks.md)** - what
  Narayan et al. (WOOT'18) exploited, the 2010 `:visited` mitigation and the
  data-operand-timing assumption it rests on, Chrome's 2025 partitioning fix (Firefox
  and WebKit have NOT shipped it), and which hardware reopenings DIT does *not* cover
  (DVFS, GPU compression, cache). Carries **the benchmark reframe**: fine-grained wins
  when the *public* code has headroom and the secret code has none, which is the
  browser's shape. **§6 holds the always-on browser DIT measurements** (2026-08-09/10):
  +2.61% (+/-0.51) Firefox and +1.80% (+/-0.16) Chromium on Speedometer 3.1 / M5,
  20/20 reps slower in both. `RENDERER_ONLY=1` matches FLOP's actual methodology and
  gives Firefox +2.45%, Chromium +2.12%, so confining DIT to the renderer does NOT
  reduce the cost. An apparent "`MSR DIT` intermittently fails to gate the LVP"
  finding was **retracted** (noise in the ratio's denominator; across 800 rounds in 40
  processes DIT never once failed).
- **[research/memory-summaries.md](research/memory-summaries.md)** - literature and
  design behind interprocedural taint through memory. The callee-to-caller gap was
  fixed as blunt-TOP P0 on 2026-07-15 via a `FunctionMemEffects` mod-set plus
  caller-side `ExternalMemClobbered`. Contains the P1 argument-provenance design of
  record (2026-07-20). Repro: `playground/callee_memory_gap.c`.
- **[research/ct-call-handling.md](research/ct-call-handling.md)** - what the Spectre
  and constant-time hardening tools do *at a call*, where this design sits relative to
  them, whether anyone else carries a memory-effects summary, and the hard question of
  whether secret-awareness is paying for itself.
- **[research/cio-and-ct-literature.md](research/cio-and-ct-literature.md)** - CIO and
  the broader constant-time / speculative-execution literature, with explicit
  verified, refuted, and open-question sections.
- **[research/mode-bit-precedent.md](research/mode-bit-precedent.md)** - prior art for
  saving and restoring a hardware mode bit per function. **LLVM already emits our exact
  sequence for `PSTATE.SM`** (reproduced with this tree's `llc`), storage is a *pre-RA
  virtual register* rather than a register claim, and AAPCS64 already specifies the
  callee-preserves contract ("PSTATE.SM on normal return: unchanged"). GCC has had a
  generic framework since 1998 whose `TARGET_MODE_BACKPROP` is our loop-hoist and whose
  `TARGET_MODE_EH_HANDLER` is the lattice element our verifier lacks. Nobody has ever
  classified a *timing* mode bit as caller- or callee-saved: 0 hits across AAPCS64, all
  412 abi-aa records, the psABI, aadwarf64 and ACLE. Also the hazard list and two action
  items (`isDITProtected` staleness; the post-`MSR` barrier).
- **[research/tail-call-precedent.md](research/tail-call-precedent.md)** - what everyone
  else does about tail calls. **Arm SME is the answer**: `PSTATE.SM`/`ZA` are PSTATE mode
  bits with our bracketing discipline, and LLVM and GCC both *forbid tail-call
  optimization* when a mode change must be undone after the call, with a predicate that
  is our ownership rule. The CT literature is silent because nobody else toggles DIT from
  the compiler; the three shipping DIT-bracketing systems (Apple corecrypto, Apple
  `timingsafe_*`, Go) all reinvented `AlwaysEnteredWithDIT` and all suppress tail calls by
  accident. Our accept-the-leak choice has one precedent (Go, deliberately) and it came
  with an observability obligation. Includes a method-documented silence list.

## Historical

- **[handoff.md](handoff.md)** - **superseded, kept as history** (2026-07-14). Its state
  of the work and next actions are obsolete; the M4 rationale, the corrected threat
  model, and the prior-art reading order are the parts still worth reading.

## Where the non-doc material lives

| What | Where |
|---|---|
| Harden-a-C-file wrapper | `utils/taint_harden_c.sh` |
| libsodium build + benchmark rig | `utils/taint_libsodium_eval.sh`, `utils/taint_libsodium_bench.sh` |
| Browser always-on DIT rig | `utils/taint_browser_dit_bench.sh`, `utils/browser_dit/` |
| DIT precision / region-spacing analysis | `utils/taint_dit_precision.py`, `utils/taint_region_distance.py` |
| DIT cost microbenchmarks | `playground/dit_bench/` |
| Scratch experiments (not shipping code) | `playground/` |
| Tests | `llvm/test/CodeGen/AArch64/taint-analysis-*.mir`, `llvm/test/Transforms/TaintAnnotate/` |

## Everything else (indexed 2026-09-05)

Documents that existed but were never listed above, with their own titles. Each
carries a dated status line at its top.

Design and method:

- [design/frame-address-gap.md](design/frame-address-gap.md) - the frame-address gap: why 97.61% of a signing path ran unprotected; the PHI look-through fix (2026-09-02) and the B1/B2/libc-model flags (default off).
- [design/p1b-frame-provenance.md](design/p1b-frame-provenance.md) - P1b: per-object pointee taint (built 2026-08-19/20).
- [design/source-condition.md](design/source-condition.md) - the source condition that makes the mod-set gate sound (unconditional since 2026-08-24).
- [design/stack-arguments.md](design/stack-arguments.md) - stack-passed secrets: the leak, the fix, the seeded-index-8 case, and the 2026-09-05 both-kinds seeding (§7).
- [design/taint-domain-refactor-handoff.md](design/taint-domain-refactor-handoff.md) - the Phase 1 handoff that became `taint-domain.md`.
- [paper/evaluation-framework.md](paper/evaluation-framework.md) - the evaluation procedure for the paper: which control arm prevents which mistake.
- [paper/bitcoin-secret-fraction-sweep.md](paper/bitcoin-secret-fraction-sweep.md) - the secret-fraction sweep inside one Bitcoin Core wallet call.

Results (each dated; those before 2026-09-05 were measured under the inherit contract, no twins, block placement):

- [results/oracle-pointer-taint-2026-09-03.md](results/oracle-pointer-taint-2026-09-03.md) - the oracle's store rule fixed; every earlier coverage denominator was ~40% inflated.
- [results/phase2-unknown-tainted-2026-09-04.md](results/phase2-unknown-tainted-2026-09-04.md) - U1/U2/U5 flipped to "unknown means tainted": byte-neutral on mbedTLS.
- [results/pr21-verification-2026-09-03.md](results/pr21-verification-2026-09-03.md) - independent verification of the product-lattice refactor.
- [results/dit-abi-committed-switches.md](results/dit-abi-committed-switches.md) - committed switches under the callee-saved ABI (not shipping).
- [results/dit-relaxed-ownership.md](results/dit-relaxed-ownership.md) - relaxed callee ownership; the flag is gone.
- [results/dit-switch-cyc-confirmation.md](results/dit-switch-cyc-confirmation.md) - the `switch-cyc=30` default confirmed, and what the knob actually prices.
- [results/dit-modset-callsite-gated.md](results/dit-modset-callsite-gated.md) - gating the mod-set on the call site: 3.7x fewer switches, no coverage lost.
- [results/dit-gem5-composite.md](results/dit-gem5-composite.md) - the composite under gem5: the pass beats always-on by 2.66%.
- [results/dit-oracle-composites.md](results/dit-oracle-composites.md) - oracle placement vs always-on on four composite hosts.
- [results/dit-pass-vs-oracle.md](results/dit-pass-vs-oracle.md) - the pass reaches the oracle: CPython and SQLite.
- [results/dit-cpython-case-study.md](results/dit-cpython-case-study.md) - CPython: what is protected and where the switches landed.
- [results/dit-sqlite-tce.md](results/dit-sqlite-tce.md) - the SQLite TCE composite and what it caught.
- [results/dit-host-screening.md](results/dit-host-screening.md) - five real applications, always-on DIT cost on M5.
- [results/dit-bitcoin-core-screen.md](results/dit-bitcoin-core-screen.md) - Bitcoin Core clears condition (d): +14.4% on coin selection.
- [results/dit-bitcoin-coinsel-gem5.md](results/dit-bitcoin-coinsel-gem5.md) - gem5 reproduces CoinSelection's always-on cost.
- [results/dit-bitcoin-sign-two-instruments.md](results/dit-bitcoin-sign-two-instruments.md) - SignTransactionECDSA on both instruments: the cost is the switch.
- [results/dit-coincurve-timing.md](results/dit-coincurve-timing.md) - a real signing workload: the prize is under 1%.
- [results/dit-coincurve-real-application.md](results/dit-coincurve-real-application.md) - web3.py to coincurve, built with the pass.
- [results/dit-upstream-gem5.md](results/dit-upstream-gem5.md) - upstream gem5 cannot model DIT at all.
- [results/session-2026-08-19-findings.md](results/session-2026-08-19-findings.md) - session record, 2026-08-15 to 08-19.
- [results/session-2026-08-30-handoff.md](results/session-2026-08-30-handoff.md) - session record, 2026-08-26 to 08-30 (LTO, the EH fix); historical.

Research memos (literature and surveys, not descriptions of the compiler):

- [research/related-work.md](research/related-work.md) - what is published and what our claims must survive.
- [research/cio-taint-implementation.md](research/cio-taint-implementation.md) - how CIO's taint analysis works, the evidence behind `taint-domain.md`.
- [research/decrypt-then-parse-literature.md](research/decrypt-then-parse-literature.md), [research/decrypt-then-parse-libraries.md](research/decrypt-then-parse-libraries.md), [research/decrypt-then-parse-applications.md](research/decrypt-then-parse-applications.md) - the decrypt-then-parse memos behind experiment 10.
- [research/real-world-instances.md](research/real-world-instances.md) - deployed software exhibiting each (host, secret) pairing.
- [research/workload-candidates.md](research/workload-candidates.md) - workload candidates for the fine-grained DIT win.
