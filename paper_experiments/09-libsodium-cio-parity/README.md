# 09 - libsodium, CIO parity

**Status: complete, silicon. The percentage columns below are SUPERSEDED on four
of six rows -- see "Second host, and a measurement correction".** Measured
2026-09-01 on Apple M5 (Mac17,2), root, real kperf cycle counters; replicated
2026-09-02 on Apple M4 (Mac16,10).

**Published artifact:** https://claude.ai/code/artifact/24709335-fc81-4a36-8eca-0c64fcc6cf8a
Source: `figures/cio-parity-corrected.html`, carrying the corrected two-host
tables. To update the page, republish **that URL** (`Artifact` with `url=...`);
publishing the file without it creates a second artifact instead.

`figures/cio-parity.html` is the superseded page, built from the uncorrected M5
percentages before the instrumentation offset was found. Kept because the
headline table above is still those numbers and the two have to stay legible
together; its own artifact is gone.

**Second page, different instrument:**
https://claude.ai/code/artifact/6b5dc30a-1296-4d02-a5e2-b723e6c8ed57
Source: `figures/switch-model.html`. *The Cost Is the Switch* carries the gem5
switch-model counterfactual (serialised vs renamed `MSR DIT`) and all six
benchmarks. It is a separate page rather than a merge because it is a separate
instrument answering a question silicon cannot: the two are complementary, not
alternative readings of one run.

**The paper figure:** `figures/three-machines-region.{png,pdf}`. Grouped bars,
one group per benchmark: coarse-grain DIT (blanket) on each machine, then
fine-grain DIT (region placement, the shipped pass) on each machine, as a
slowdown against each machine's own baseline. Machines are Apple M4, Apple M5,
and ExpeDITe (the gem5 model) under both `MSR DIT` implementations. Regenerate
with `utils/dit_host_screening/cioparity/fig_three_machines.py`, which reads
`m{4,5}_results_ratios.csv` (cntvct rows) and `gem5_switch_model.csv` /
`gem5_argon2id.csv` directly. Cycles, not IPC: the pass adds only its switches
(1-2% more instructions, blanket adds none), so the ratio is almost entirely
cycles, and absolute silicon IPC would carry the unmeasured instruction offset of
the kperf reads.

---

## The claim

> Run the closest prior work's experiment, on their library, with their seeds and
> their drivers, and the framework's **first** question answers itself: blanket
> `PSTATE.DIT` costs between **-0.60% and +1.95%** on all six of CIO's benchmarks.
> There is no headroom. Every selective placement policy therefore spends switches
> to recover something worth nothing, and loses by **12% to 124%**.

This is the **negative control of the paper, run on the workload a reviewer is
most likely to name.** It is not evidence against selective placement; it is the
measurement that says where selective placement does not belong, and why you can
know that before writing any compiler.

Experiments 01 and 02 sweep the secret fraction and find a crossover. This one is
the f -> 100% endpoint, held still: the whole measured region is crypto, there is
no public lane at all, and the prediction from 01/02 - that blanket wins there -
is confirmed on somebody else's benchmark suite.

**libsodium appears twice in this evaluation and that is deliberate.** In
experiment 02 it is the secret lane of a flow that also has a public lane, and
the pass beats blanket by 21%. Here it is the whole program. Same library,
opposite verdict. The library is not the workload; the flow is.

## Headline results

Apple M5, kperf cycles, CIO's drivers at their iteration counts, 15 reps, median
of the per-run means. `A` is the MIR round-trip control, not the stock build.

| benchmark | cycles/op | **blanket** | **pass** | func | old defaults | resolved | MAD |
|---|---|---|---|---|---|---|---|
| ed25519 sign | 37,508 | **+0.17%** | **+11.94%** | +10.57% | +24.75% | +12.02% | 0.12% |
| chacha20-poly1305 encrypt | 3,701 | **-0.54%** | **+114.26%** | +111.83% | +153.44% | +114.32% | 0.33% |
| chacha20-poly1305 decrypt | 3,729 | **-0.60%** | **+123.56%** | +119.03% | +165.98% | +123.43% | 0.48% |
| aes256-gcm encrypt | 2,876 | -0.59% | +35.34% | +33.79% | +50.91% | +35.06% | 0.57% |
| aes256-gcm decrypt | 2,969 | +1.95% | +34.24% | +32.82% | +38.33% | +34.34% | 0.91% |
| argon2id | 191,171,059 | **-0.33%** | **-0.17%** | -0.34% | +0.04% | +0.03% | 0.23% |

`data/results_summary.csv`. Raw: `data/cio_benchmarks_O2_counters.csv`.

> **The percentage columns in this table are understated by 4x (chacha) and
> 13-15x (AES).** Each sample carries a fixed ~3234-cycle cost from the two
> `kpc_get_thread_counters()` calls that bracket the timed region -- 72% of the
> chacha baseline and 87% of the AES one. It cancels in the arm-vs-arm
> differences, so "Where the cost comes from" below is unaffected, but it sits
> in the denominator of every percentage here. Corrected numbers, measured with
> a 21-cycle timer instead, are in the next section. The three CONCLUSIONS in
> this section all survive; it is their magnitudes that do not.

**`data/primitives_13_silicon.csv` corroborates this independently** and was
previously uncited: 13 libsodium primitives, 19 measurements, the same six arms,
same run. Blanket is the smallest cost in every one of them, against the pass's
much larger spread. **It carries the same instrumentation offset as the table
above**, having been measured on the same kperf path, so its percentages are
compressed by the same factor and it corroborates the ORDERING rather than the
magnitudes. Re-measuring it with the 21-cycle timer is the cheap way to turn
three times the measurements into three times the evidence.

**Three readings, in order of importance:**

1. **Blanket is free on every one of them**, and faster than baseline on four.
   The gated optimisations were not paying on this code, so suppressing them
   costs nothing.
2. **Coarser placement beats finer on all six.** `func` < `region` everywhere.
   That is what the cost model predicts when dwell is zero: narrowing coverage
   buys nothing, so you only pay toggles, and fewer toggles is better.
3. **argon2id is free in every arm**, and the toggle rate is the reason:
   measured under gem5 at **1.3 committed switches per million cycles**, against
   43,176 for chacha20-poly1305. Overhead tracks executed switches *per unit
   work*, not a fixed per-call charge, and this is the null endpoint of that
   axis - the benchmark where CIO pays most (27.84x) and we pay nothing.

   **CORRECTION 2026-09-02: the low toggle rate is not amortisation.** An
   earlier version of this reading said "a 191M-cycle operation amortises every
   switch" and called it the cleanest demonstration of the cost model. The
   mechanism is different, and worse: **taint does not reach the hashing kernel
   at all.** `argon2_hash` builds an `argon2_context` on its own stack and
   stores the password pointer into it; storing a pointee-tainted pointer into
   memory does not mark the destination, and passing the struct's address does
   not make that address pointee-tainted. So `argon2_ctx` receives a *clean*
   pointer and `argon2_initialize`, `argon2_fill_memory_blocks` and
   `argon2_fill_segment_ref` - which are essentially all 191M cycles - carry
   **zero switches** and appear in no report. The 438 switches that do execute
   are in the wrapper functions.

   The secret is nonetheless protected here, by luck rather than by design:
   CIO's config marks all five arguments of the entry point, so `argon2_ctx`
   carries a region for an over-tainted *variant flag* which happens to span the
   calls into the kernel. Had it not, the password would have been hashed with
   the mode clear and nothing would have said so. See
   `docs/design/frame-addr-fallback.md` (the gap is known; the whole-frame fix
   was removed on 2026-08-24 for costing +45.32% against the mod-set gate) and
   the new `memory` information-loss category, which now reports it.

   **This row is therefore not evidence for the placement cost model.** It is
   evidence for the toggle-rate model, which is the claim it is cited for
   elsewhere, and that part stands.

## Where the cost comes from

Counters read inside the timed region only (`reg_cyc`/`reg_ins` in the raw CSV).
Absolute instruction counts carry a constant instrumentation offset; differences
between arms cancel it exactly.

| benchmark | executed switches/op | extra cycles/op | cycles per switch |
|---|---|---|---|
| ed25519 sign | 109 | +4,487 | **41.2** |
| chacha20-poly1305 encrypt | 105 | +4,225 | **40.3** |
| chacha20-poly1305 decrypt | 103 | +4,611 | **44.8** |
| aes256-gcm encrypt | 15 | +1,020 | 66.3 |
| aes256-gcm decrypt | 18 | +1,017 | 56.3 |
| argon2id | ~0 (noise) [438 under gem5] | ~0 | n/a |

**The evidence is the CONSISTENCY of the last column across independent
benchmarks (40.3, 41.2, 44.8), not any per-row agreement.** Cycles-per-switch is
derived by dividing measured cycles by measured switches, so multiplying it back
out reproduces the measurement by construction and proves nothing. The AES rows
rest on 15-18 switches and are too noisy to support the claim; they are shown for
completeness, not as support. (**Settled 2026-09-02 under gem5**, which counts
switches exactly rather than inferring them - see the switch-model section
below.)

Blanket adds **0 +/- 10 instructions and 0 +/- 60 cycles** per operation, measured
the same way. So the cost decomposes as switch serialisation alone, with no dwell
term - which is exactly what "blanket is free" implies.

## Head-to-head with CIO

Same library, same seeds, same benchmarks, same `--disable-asm` build.

| | **CIO, published** | **blanket DIT** | **our pass** |
|---|---|---|---|
| ed25519 | 20.32x / 16.73x | **1.0017x** | 1.119x |
| argon2id | **27.84x** | 0.997x | 0.998x |
| libsodium `__text` | +62% / +208% / +266% | 0% | **+0.87%** |

CIO could not use `PSTATE.DIT` and says so: *"at this time the only processors
known to the authors to support PSTATE.DIT are the Apple M-series CPUs"*, and
DOIT *"can only be enabled by the kernel"*. Their 27.84x is the price of doing
in software what the mode does in hardware. **The interesting number is not that
we beat them - it is that BLANKET beats them by ~400x, and our selective
placement then gives back a factor of 70 to protect strictly less code.**

## What is public and what is secret

**Nothing is public, and that is the finding.** The measured region is a single
crypto call. `f_secret` is ~100% by construction. The five framework quantities:

| | value |
|---|---|
| `f_secret` | ~100% (the timed region is one crypto call) |
| `C_public` | **n/a - there is no public lane.** This is why the experiment is a case study and not a crossover |
| `C_secret` | **-0.60% to +1.95%** - the whole point |
| work per region | 2,876 to 191,171,059 cycles/op, a 66,000x span |
| toggles per unit work | 109/37,508 cyc (ed25519) down to ~0/191M (argon2id) |

## Validity gates

All armed, all passing, all in `data/ditprobe_gates.csv`. The `ditprobe` driver is
interleaved with every other benchmark in the same run, same arms, same rotation.

1. **The instrument can see DIT.** Value-predictable pointer chase: 222 -> 873
   ps/hop with the mode on, **3.932x**. A null result is worthless without this.
2. **Negative control flat.** Random-permutation chase, nothing to predict:
   **1.0000x**.
3. **P-core residency.** Dependent-ADD clock probe reads **4597-4598 MHz** on
   every arm. (QoS is a bias, not a binding, so it is checked per rep.)
4. **Mode readback.** `PSTATE.DIT` read inside the region: 0 for every arm, 1 for
   blanket.
5. **Arm order rotates every rep** - a fixed order lets drift look like an effect.
6. **Instruction-count parity.** Blanket vs baseline within **0.23%**: the arms
   run the same work, so the cycle ratios mean something.
7. **`make check` 86/86** on both the control and the hardened library.

## Is the secret actually protected

Static coverage is complete where it can be, and the gaps are the mechanism's,
not the pass's.

- **All 21 seeded entry points carry DIT switches**; taint reaches 108 functions.
- **34 ESCAPE sites** (`data/report_escape.txt`), 28 indirect + 6 external. Every
  one is marked *covered by inherited DIT* - the mode stays on across the call,
  so the callee runs protected. Over-protection, not a hole.
- **9 functions are both instrumented and indirect targets**, the one shape that
  could clear DIT mid-body on an unanalysed edge. All nine are themselves seeded
  entry points, so their placement covers their secret work whoever calls them,
  and the caller re-asserts after every indirect call. Safe **here**, but by a
  property of CIO's seed set rather than by construction.
- **Tail calls are disabled, and the six that survive are audited.** A tail call
  has no epilogue, so one taken with DIT on never restores the mode and the arm
  becomes blanket in disguise - this rig had **13 such sites** before the fix,
  `crypto_sign` among them. The lowering `llc` now runs `-disable-tail-calls`,
  upstream of every arm. Six `TCRETURN`s still survive it (out of 956 returns),
  because `musttail` bypasses the option in SelectionDAGBuilder and
  `MachineOutlinerTailCall` runs downstream of the pass. All six are libc calls
  in functions that carry **no `msr DIT` at all**, so there is no mode to fail to
  restore:

  | function | tail-calls | instrumented |
  |---|---|---|
  | `aegis128l_mac`, `aegis128l_mac.78` | `bzero` | no |
  | `aegis256_mac`, `aegis256_mac.95` | `bzero` | no |
  | `_sodium_keccak1600_ref_extract_bytes` | `memcpy` | no |
  | `sodium_malloc` | `memset` | no |

  None of them is reachable from CIO's seed set, which is why they are
  uninstrumented. The pass's own check agrees independently: **0 `leak-tailcall`
  records** in `data/report_infoloss.txt`, against 13 before. Both MIRs
  (`libsodium.pe.mir`, `libsodium.nar.pe.mir`) show the same 6, so the `N` arm is
  built the same way. **This audit is required, not decorative:** "the flag was
  passed" is not the same claim as "no DIT-on exit tail-calls", and only the
  second one licenses the numbers above.

- **126 UNCOVERED sites** (`data/report_uncovered.txt`): 97 secret-address, 29
  secret-branch. **DIT does not cover these channels at all** - a secret used as
  a memory address is a cache-timing leak and a secret-dependent branch is a
  control-flow leak, whatever the mode bit says.

### And it is dynamically verified (2026-09-01)

The static claims above are now backed by the gem5 shadow-taint oracle, which
answers the one thing no compiler report can: **did a secret ever reach a
DIT-covered instruction while `PSTATE.DIT` was clear?** Nothing faults when that
happens, so it is invisible to static analysis - and invisible to the Tier 1
hardware oracle too, which only sees reads of the raw key *buffer* while
everything derived from the key (the expanded scalar, the SHA-512 state, the
poly1305 accumulator) lives in registers and on the stack.

| workload | arm | secret ops protected | **DIT clear (under-taint)** | sites in libsodium |
|---|---|---|---|---|
| ed25519 sign | `taint` (region, shipped) | 294,164 | **0** | **0** |
| | `taintfn` (whole-function) | 294,164 | **0** | **0** |
| | *unhardened control* | 0 | *294,164* | - |
| chacha20-poly1305 | `taint` | 7,170 | **0** | **0** |
| | `taintfn` | 7,170 | **0** | **0** |
| | *unhardened control* | 0 | *7,170* | - |

**The null control is what makes the zero mean anything.** The same library with
`-ftaint-harden` removed reports every one of those secret-consuming instructions
running unprotected, which proves the oracle saw the crypto path. A zero from
this tool without its control is worthless - that rule comes from the M3
libsecp256k1 run and it is enforced by the script.

**The three counts matching to the digit is a consistency check, not a
coincidence.** They are three different binaries carrying 0, 134 and 137
`msr DIT`. Hardening changes whether the mode is on, not which instructions
consume the secret, so the same count must appear in all three.

Both hardened arms are equally sound - including `taintfn`, the arm that also
won on time. Rigs: `benchmarks/taint_oracle/{sodium_gem5.c,build_sodium.sh,
run_sodium_oracle.sh}` in the gem5-DIT tree; raw output in `data/oracle_*.txt`.

**What it still does not cover.** Two signatures and two AEAD calls, against the
200 signatures of the libsecp256k1 M3 run; one gem5 configuration; and the two
workloads seeded through `crypto_sign` and
`crypto_aead_chacha20poly1305_ietf_encrypt` only, so the argon2id and AES-GCM
paths of this experiment are **not audited by this oracle**.

For argon2id that gap is now known to be real rather than merely unmeasured: the
analysis loses the password through memory before the hashing kernel (see
reading 3), so a shadow-taint run on that path is the audit most worth adding.
The static half is covered: the `needuncovered` counter added 2026-09-02 reports
**0** instructions that require DIT and run without it, for every placement
policy, over `need = 4,172` - but that counts only what the analysis *knows* is
secret, which is exactly what fails here.

## The switch-model counterfactual (gem5, 2026-09-02)

Every number above is Apple M5, whose `MSR DIT` serialises. That is the only
implementation silicon offers, so the decomposition in "Where the cost comes
from" - **the cost is switch serialisation, with no dwell term** - rests on
cycles-per-switch being consistent across benchmarks, a ratio this README
already says "proves nothing" on its own.

gem5 can turn the mechanism off. `--no-speculative-dit` selects the serialising
path; without it the write is a renamed CC-register write. Same binary, same
input, one mechanism changed, and **all six benchmarks now run** - including the
two AES-GCM rows disclaimed above and argon2id, where silicon could only report
noise.

| benchmark | renamed switch | serialising switch | committed switches/op |
|---|---|---|---|
| chacha20-poly1305 encrypt | **-0.28%** | **+81.85%** | 94 |
| chacha20-poly1305 decrypt | **+2.57%** | **+89.06%** | 98 |
| aes256-gcm encrypt | **-0.76%** | **+29.01%** | 15 |
| aes256-gcm decrypt | **+8.23%** | **+51.06%** | 15 |
| ed25519 sign | **-1.39%** | **+1.70%** | 85 |
| argon2id | **+2.04%** | **+1.97%** | 438 |

**The decomposition is confirmed causally rather than by ratio.** A renamed
switch costs -0.3 to +0.3 cycles where the rig can resolve it; a serialising one costs 19.0 to 37.1. The
comparison is immune to code-layout effects because it is one binary under two
machine configurations.

Three things this settles that silicon could not:

- **The AES-GCM rows.** This README disclaims them ("too noisy to support the
  claim") because 15-18 switches is at the edge of kperf's resolution. gem5
  counts exactly 15 and the cross-model control floor is 0.00%.
- **argon2id's switch count.** `data/results_summary.csv` records
  `pass_switches_per_op = -197187` here - noise divided by noise, rendered
  honestly as "~0 (noise)" in the table above. The real figure is **438**, and
  at 1.35 writes per million cycles the serialising penalty is -0.07 points -
  zero within noise - and `ditCycles` reads 100% in every hardened arm, which is
  the dynamic confirmation that the whole hashing kernel runs with the mode on.
  Together with chacha20's 42,728 that is a 30,000x span on experiment 06's
  toggle-rate axis, whose previous range was 86 to 4,601.
- **Placement granularity is a consequence of serialisation, not a property of
  placement.** Under a serialising switch the three policies spread over 25.3
  points; renamed, they collapse into 3.2. On a core that renames the write the
  policy choice is nearly free.

**Reading 2 above holds on gem5 too, but only once code layout is controlled.**
Inserting a switch moves all downstream code by exactly 4.00 bytes, and the
resulting cache-line displacement is worth **-6.94% to +4.52%** - larger than
the policy differences it is measured against. Comparing raw totals made `fine`
appear to beat `region` on aes256-gcm decrypt; against a per-policy layout twin
(same placement, `HINT #0` in place of each switch, byte-identical addresses)
`fine` is 3.2 points *worse*, executing more switches at the same dwell. The
`func < region` ordering then holds on 4 of 5 benchmarks at three different
alignment settings.

Full rig, data and limits: `utils/dit_host_screening/cioparity/RESULTS.md`.
Required two gem5 patches, both validated and unpushed: PMULL 64x64->128 (absent
from gem5, so AES-GCM could not run at all) and `commit.ditCycles` (cycles with
the mode set - the dwell axis the switch counters cannot provide).

## The hand-placed API bracket, and the compiler's new defaults (gem5, 2026-09-05)

What a careful library author does by hand, and what Apple's corecrypto scope
guards do: set `PSTATE.DIT` once at the entry of each public crypto function
and clear it once at the exit. Nothing inside is touched, nothing is analysed.
It is the arm between blanket (the mode set for the whole process) and the
pass (the mode placed inside the library by the analysis), and on a workload
that is all crypto it is the placement a reviewer will name first.

**The arm.** The unhardened `base` library, the public entry points CIO's
drivers call wrapped by the linker (`-Wl,--wrap`) so `__wrap_f` brackets
`__real_f` with one enable and one clear (`api_bracket.c`; `crypto_sign`,
`crypto_sign_keypair`, `crypto_sign_open`; the AEAD `keygen`/`encrypt`/
`decrypt` pairs; `crypto_pwhash`). Same driver, same library, same layout as
`base`; exactly two committed mode writes per operation, which the runner
gates on.

**The pass arm** is the compiler's defaults since 2026-09-05: the callee
contract and the DIT twins (`docs/design/dit-cloning.md`), the per-TU clang
path, the contract's fixpoint seeds (188 lines,
`benchmarks/crypto/libsodium_secret_contract.txt` in gem5-DIT from PR #101;
the CIO seeds protect nothing under the contract) and the owned-symbols list
`build_arms.sh` now derives from its own base build. 364 switch sites and 85
twins in the library. `taintold` is the pre-flip compiler on the CIO seeds
(inherit contract, no twins), the per-TU form of the `pass` arm above, for
the record. Every arm has its NOP twin; `run_cio_gem5.py` gates all of them.
Data: `data/gem5_api_bracket.csv`, `data/gem5_api_bracket_analysis.txt`.

| benchmark | base cyc/op | blanket | API bracket, renamed / serialising | pass, renamed / serialising | pass switches/op | old compiler, renamed / serialising (switches) |
|---|---|---|---|---|---|---|
| ed25519 sign | 78,790 | +0.22% | +0.73% / +1.14% | -3.75% / -2.60% | 16 | -0.83% / -0.68% (3) |
| chacha20-poly1305 encrypt | 2,186 | +1.31% | +3.19% / +3.28% | +2.37% / **+43.88%** | 38 | +2.44% / +56.72% (49) |
| chacha20-poly1305 decrypt | 2,290 | +0.70% | +1.50% / +4.33% | +3.29% / **+42.42%** | 39 | +2.45% / +57.85% (51) |
| aes256-gcm encrypt | 1,217 | +0.41% | +0.25% / +3.78% | +0.25% / +12.42% | 6 | -0.25% / +26.06% (12) |
| aes256-gcm decrypt | 1,077 | +8.39% | +9.13% / +23.75% | +10.06% / +36.06% | 6 | +9.32% / +48.79% (13) |

Each policy's own NOP twin, so the layout term is visible:

| benchmark | pass NOP (layout) | pass, real minus NOP, renamed | old NOP | old, real minus NOP, renamed |
|---|---|---|---|---|
| ed25519 sign | -3.74% | -0.01 | -1.51% | +0.68 |
| chacha20-poly1305 encrypt | +2.41% | -0.03 | +2.54% | -0.10 |
| chacha20-poly1305 decrypt | +4.11% | -0.81 | +2.94% | -0.49 |
| aes256-gcm encrypt | -0.03% | +0.28 | -0.46% | +0.21 |
| aes256-gcm decrypt | +5.71% | +4.35 | +0.89% | +8.42 |

Three readings.

- **Renamed: blanket, the bracket and the pass cost the same thing, dwell,
  and the rest is layout.** Every arm is within a few points of blanket, and
  where the pass differs from the bracket its NOP twin differs by the same
  amount: ed25519's -3.75% is a layout win (-3.74% with no switch executing)
  and the two chacha rows are +2.4 and +4.1 of layout. The bracket's two
  switches and the pass's 6 to 39 both execute for nothing on a renamed core.
- **Serialising: the bracket pays two switches per call and the pass pays
  its dispatch.** The bracket sits 1 to 4 points above blanket, and 15 above
  on aes-gcm decrypt, where two serialising drains land on a 1,077-cycle
  operation. The pass pays 38 to 39 switches per AEAD call, all of them
  behind the Poly1305 and ChaCha20 implementation tables that no twin can
  reach (an indirect call is never redirected), and 6 per AES-GCM call for
  the same reason: +42 to +44 on chacha, +12 and +36 on AES-GCM. On ed25519,
  where the calls are direct, the twins leave 16 switches on a 78,790-cycle
  signature and the serialising term is +1.15 points, the bracket's +0.41.
- **What the new defaults changed.** Against the old compiler on the same
  path the pass executes a quarter to a half fewer switches (49 -> 38, 51 ->
  39, 12 -> 6, 13 -> 6) and its serialising cost drops by 13 to 14 points on
  every AEAD row. ed25519 goes the other way, 3 -> 16, because the old
  contract held DIT across the whole signature from one forwarder and the
  new one has each entry into the library toggle for itself; on a renamed
  core that is free, on a serialising one it is +1.15 points.

**The verdict for this experiment does not move: on a workload that is all
crypto, blanket is the answer and the hand-placed bracket is within a few
points of it.** The pass is a placement engine for flows with a public lane
(experiment 02); here it can only match the bracket where the library's
calls are direct, and libsodium's AEAD dispatch is not. The tables are
also the bound: `api` is what the pass would cost if every indirect target
had a twin, which is a property of the library, not of the compiler.

**Coverage** of the pass arm on the signing path, the gem5 shadow-taint
oracle (two signatures, the round-11 protocol): 294,164 secret operations
protected, 0 uncovered, 54,010 wasted, identical to the round-11 library
measured for `docs/design/dit-cloning.md`; the seeds are at their fixpoint
on this path and the twins do not move protection.

**With a different message every operation** (`build_arms.sh VARY_INPUT=1`:
the staged drivers rewrite the message before each iteration's setup,
outside the measured region; keys and nonces already vary per iteration in
CIO's drivers, only the message and the empty additional data were fixed;
the patch is `data/gem5_api_bracket_vary_driver_patch.diff`, so this lane is
NOT byte-identical to CIO's drivers and the fixed-input lane above remains
the parity measurement). Same libraries, same seven arms, all gates passing;
`data/gem5_api_bracket_vary.csv`, `data/gem5_api_bracket_vary_analysis.txt`:

| benchmark | blanket | API bracket, renamed / serialising | pass, renamed / serialising | pass switches/op |
|---|---|---|---|---|
| ed25519 sign | +1.13% | +0.58% / +0.74% | -6.53% / -1.75% | 16 |
| chacha20-poly1305 encrypt | +3.81% | +1.99% / +1.72% | +1.72% / +37.94% | 38 |
| chacha20-poly1305 decrypt | -0.91% | -2.53% / -0.14% | +0.29% / +36.38% | 39 |
| aes256-gcm encrypt | +0.57% | +0.90% / +4.20% | +0.32% / +12.85% | 6 |
| aes256-gcm decrypt | +8.09% | +10.77% / +25.10% | +9.02% / +35.18% | 6 |

Nothing that matters moves. The serialising column is the same story to
within a few points, and the renamed column shuffles inside the layout band
(the patched drivers are different binaries, so every arm's layout term
moved with them: ed25519's pass row is now -6.5% against a NOP twin at
-2.7%). **The aes256-gcm decrypt row was the reason to run this, and it does
not move: blanket +8.09% against +8.39%.** The value-predictable loads that
DIT takes away on that row are not the message. With the message varying,
base still makes 2,346 load-value predictions per 50 operations (3,368 with
it fixed), 1,700 of them stride predictions (2,491), and blanket makes none;
encrypt makes 916 and no stride predictions on either input. A stride
prediction is a load whose value advances by a constant, which is what a
counter mode's block counter does by construction, and the decrypt path's
loop structure exposes it where encrypt's does not. That is a property of
the kernel, not of the driver's input, and every placement that covers the
kernel pays it.

### What the value predictor is predicting on aes256-gcm decrypt, and whether it is secret

The 8% every covering arm pays on that row is the load value predictor
being switched off. The natural worry is that the predicted loads are
public and the pass is over-approximating: if so, a placement that left
them uncovered would beat blanket on this row. So the predictor was traced
(gem5 `--debug-flags=LVP`; the EVES predictor prints each prediction and its
validation with the PC, gem5-DIT-pmull branch `ditcycles`, commit
`4fdc491e5b`; `lvp_pcs.py` aggregates the trace over the measured window).
Two operations of the decrypt driver, base arm:

| PC | correct predictions / 2 ops | kind | where |
|---|---|---|---|
| `0x40c704` | 41 | stride | `crypto_verify_16+0x1c`: `ldr x10, [sp, #0x10]`, the volatile pointer `y` |
| `0x40c70c` | 33 | stride | `crypto_verify_16+0x24`: `ldrh w11, [sp, #0xc]`, the volatile accumulator `d` |
| `0x40c6fc` | 27 | stride | `crypto_verify_16+0x14`: `ldr x9, [sp, #0x18]`, the volatile pointer `x` |
| `_init`, `getrandom` | 15 | vtage | startup, outside the kernel |

**None of them is in the AES-GCM kernel.** They are the three reloads in
the 16-iteration loop of `crypto_verify_16`, libsodium's constant-time
comparison of the computed tag against the received one
(`crypto_verify/verify.c`: `volatile` pointers `x` and `y`, `volatile
uint16_t d; for (i) d |= x[i] ^ y[i];`). `volatile` makes each iteration
reload both pointers and the accumulator from the stack and store the
accumulator back, so the loop is a store-to-load chain through `d` of about
five cycles per iteration; the predictor breaks the chain (the pointers
never change and `d` stays 0 while the tags agree) and the sixteen
iterations overlap. With DIT set the chain serialises: roughly 80 cycles on
a 1,077-cycle operation, which is the 8%. Encrypt never calls it, which is
why its blanket cost is +0.4%. The pass arm predicts the same three loads in
`crypto_verify_16.dit`, the twin the DIT-on decrypt path calls.

**Two of the three loads are public and one is the secret that matters.**
The pointer reloads carry addresses. The accumulator `d` carries the OR of
every `x[i] ^ y[i]` so far: it is 0 exactly while the received tag has
matched the computed one byte for byte, and a predictor that speculates on
it makes the loop faster while the bytes match and squashes at the first
mismatch. That is a timing that depends on how many bytes of a forged tag
were right, the byte-by-byte MAC-comparison oracle that the constant-time
compare exists to prevent, moved from the branch predictor to the value
predictor. It is precisely the class of channel DIT closes on a core that
ties value prediction to the mode, and the analysis agrees: `crypto_verify_16`
is seeded on both arguments (fixpoint file, lines 129-130), the `d` loads,
the byte loads and the XOR/OR are its 17 Needs, and the two pointer reloads
are clean. No placement can keep the two public reloads predicted while
covering the third: they are three instructions apart in a twelve-instruction
loop body, and a toggle per iteration would cost two serialising switches
sixteen times. **On this row the 8% is not over-approximation. It is the
price of closing the tag-comparison channel, and every placement that
protects the tag pays it, hand-placed or not.**

**Is the hoisted enable what covers the two public reloads, and would not
covering them buy the time back?** The first half is yes: region placement
hoists the loop's enable to the preheader, and with the enable sunk into the
body and the loads reordered, the two pointer reloads, `i++` and the bound
could run DIT-off. So that was built by hand
(`utils/dit_host_screening/cioparity/verify16_hand/`: the compare's own code
from the pass binary, three ways, `-Wl,--wrap`ped ahead of the unhardened
library so the compare is the only thing that runs with DIT set):

| `crypto_verify_16` | DIT writes / op | renamed | serialising | load predictions / op |
|---|---|---|---|---|
| no switch (control) | 0 | 1,080.5 cycles | 1,080.5 | 64.5 |
| hoisted, whole loop covered (what the pass emits) | 4 | **+8.38%** | +15.01% | 8.0 |
| per iteration, only the 8 tainted instructions covered | 34 | **+8.19%** | +74.17% | 8.5 |

Leaving the public reloads uncovered recovers nothing: +8.19% against
+8.38% on the renamed model, and +74% on the serialising one for the 34
switches. The loop's critical path is the store-to-load chain through the
tainted accumulator, and once that is covered the predictor has nothing
left to shorten; the trace of the per-iteration variant shows the two
pointer reloads predicted 3 times per two operations instead of 30 to 40,
with every other prediction in the loop suppressed by the mode. The hoist
is a design decision, and on this loop it is free.

A side finding from the same reports, not on the benchmark's path. The
one-shot `crypto_aead_aes256gcm_decrypt` builds the key schedule and the
GHASH table into a local `st` and passes `&st` to
`crypto_aead_aes256gcm_decrypt_detached_afternm`; that is the frame-address
gap (`docs/design/frame-address-gap.md`), the callee sees its state as
public, and the ORIGINAL `_afternm` has 9 Needs in 1,553 instructions (2.6%
coverage). The benchmark never runs that original: the DIT-on chain from
the seeded entry calls the twin, which is covered whole. A user of the
precomputed-state API (`crypto_aead_aes256gcm_*_afternm` with their own
state, entered DIT-off) would get the 2.6%, and the repair is one seed line
on the state argument. The exposure is small on this hardware (`aese` and
`pmull` are constant-time by construction) and the AES-GCM path has not
been oracle-verified in this experiment; it is recorded here so it is not
rediscovered.

**argon2id** is running as this is written (one operation is 326M cycles;
five arms, both models, about six hours) and will be appended to the data
file; on silicon it is the null endpoint for every arm.

A first sweep of these arms used the seed file the gem5 tree carried as
`libsodium_secret_contract.txt`, which turned out to be the round-2 file (86
seeds, `_r2` after PR #101), not the fixpoint; the pass arm then executed 19
switches per signature and 177 sites. Those numbers are superseded by the
table above.

## Second host, and a measurement correction

**Apple M4 (Mac16,10), 4P+6E, macOS 15.7.3, root, kperf. 2026-09-02.** The whole
rig rebuilt from the committed scripts and re-run. Two runs: one with the
original kperf timing, one with the timer fix described below.

### Codegen reproduced exactly

Placement is host-independent, so this is a real check on the toolchain rather
than a measurement. Every static number matches the M5 run to the digit:
switches 0/521/569/631/749, functions 0/108/108/108/164, `__text`
247312/249460/249644/249972/250136, 23 info-loss records and 0 SEVERE, 34 escape
sites, 126 uncovered. `data/report_{escape,uncovered,infoloss}.txt` and
`data/seed_cio_parity.txt` come out **byte-identical** on both hosts and are
therefore not duplicated; `data/m4_static_policies.csv` carries the M4 table,
and the narrow arm's own reports are in `data/m4_report_*_narrow.txt`.
`make check` 86/86 on both the control and the hardened library.

### The instrumentation offset

`utils/cio_offset_probe.c` times a region against a payload of known cycle cost
and fits a line. On M4: **3234 cycles/region** (3 valid passes, 3229-3249,
+/-0.3%), slope 0.9940-1.0068, so the cost is additive and subtractable. Full
output and method limits in `data/m4_offset_probe.txt`.

That offset is a property of the instrument, not the host, and the M5 run
carries its own (~2600 cycles, inferred: both hosts agree chacha's true cost is
~1100-1250 cycles). Where it lands:

| | kperf baseline | offset as % of it |
|---|---|---|
| ed25519 sign | 38,107 | 8.5% |
| chacha20-poly1305 enc | 4,476 | **72.3%** |
| chacha20-poly1305 dec | 4,502 | **71.8%** |
| aes256-gcm enc | 3,720 | **86.9%** |
| aes256-gcm dec | 3,876 | **83.4%** |
| argon2id | 211,401,385 | 0.002% |

**It cancels in differences and not in ratios.** `P - A` and cycles-per-switch
are clean; `(P - A) / A` is not. So the switch decomposition was always sound and
the percentage table was not.

The fix is to notice the percentage columns are ratios and therefore unit-free:
they never needed cycles. `CHEAP_TIMER=1` times the region with `CNTVCT_EL0`
(21-cycle offset, and 1 ns per tick on M4 -- `cntfrq_el0` is 1 GHz here, NOT the
24 MHz `hw.tbfrequency` the older comments in this tree describe) while the
counter accumulators stay on kperf. The trick is ordering: the expensive read is
moved outside the window the driver differences.

### Corrected results

Apple M4, CNTVCT timing, CIO's drivers at their iteration counts, 15 reps,
median of per-run means. `Z` is the new NOP-switch control.

| benchmark | ns/op | cyc/op | blanket | pass | func | old def | resolved | nopsw | MAD |
|---|---|---|---|---|---|---|---|---|---|
| ed25519 sign | 7,828 | 34,506 | +0.87% | **+9.46%** | +8.84% | +14.57% | +9.00% | -0.38% | 0.53% |
| chacha20-poly1305 enc | 251 | 1,108 | +1.27% | **+243.16%** | +241.17% | +341.34% | +242.88% | -2.40% | 1.56% |
| chacha20-poly1305 dec | 256 | 1,129 | +2.07% | **+262.17%** | +258.59% | +371.64% | +263.06% | +0.24% | 0.38% |
| aes256-gcm enc | 62 | 272 | +0.15% | **+233.89%** | +231.65% | +403.91% | +236.72% | +8.70% | 1.48% |
| aes256-gcm dec | 82 | 360 | +0.81% | **+194.96%** | +190.16% | +240.14% | +197.26% | -1.62% | 0.71% |
| argon2id | 50,489,010 | 222,555,555 | -0.19% | -0.37% | -0.42% | -0.45% | -0.51% | -0.50% | 0.32% |

`data/m4_results_summary.csv` carries both timers. Raw:
`data/m4_cio_benchmarks_{kperf,cntvct}_timed.csv`.

**The method effect, isolated** -- same host, same binaries, only the timer
changed, so this is the correction and nothing else:

| | kperf-timed | CNTVCT-timed | factor |
|---|---|---|---|
| ed25519 | +8.24% | +9.46% | 1.1x |
| chacha enc | +60.78% | +243.16% | **4.0x** |
| chacha dec | +66.18% | +262.17% | **4.0x** |
| aes enc | +15.36% | +233.89% | **15.2x** |
| aes dec | +14.36% | +194.96% | **13.6x** |

**This is a denominator fix, and here is the proof.** Extra cycles per op is a
difference, so the offset must cancel and the timer change must leave it alone.
It does: ed25519 3141 -> 3263, chacha enc 2720 -> 2694, chacha dec 2979 -> 2960,
and cycles-per-switch 33.1 -> 33.4, 24.3 -> 25.2, 28.9 -> 28.5. The numerators
did not move; the ratios did.

### The same table as ratios

Apple **M4** (Mac16,10), corrected CNTVCT timing. Every arm as a multiplier
against arm `A`, the unhardened MIR round-trip control. `data/m4_results_ratios.csv`
carries this for both timers.

| benchmark | ns/op | cyc/op | blanket | pass | func | old def | resolved | nopsw |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| ed25519 sign | 7,828 | 34,506 | **1.0087x** | 1.0946x | 1.0884x | 1.1457x | 1.0900x | 0.9962x |
| chacha20-poly1305 enc | 251 | 1,108 | **1.0127x** | 3.4316x | 3.4117x | 4.4134x | 3.4288x | 0.9760x |
| chacha20-poly1305 dec | 256 | 1,129 | **1.0207x** | 3.6217x | 3.5859x | 4.7164x | 3.6306x | 1.0024x |
| aes256-gcm enc | 62 | 272 | **1.0015x** | 3.3389x | 3.3165x | 5.0391x | 3.3672x | 1.0870x |
| aes256-gcm dec | 82 | 360 | **1.0081x** | 2.9496x | 2.9016x | 3.4014x | 2.9726x | 0.9838x |
| argon2id | 50,489,010 | 222,555,555 | **0.9981x** | 0.9963x | 0.9958x | 0.9955x | 0.9949x | 0.9950x |

The ratio form makes the shape plainer than percentages do:

- **Blanket is 1.00x-1.02x on all six**, and below 1 on argon2id. The mode is free.
- **Selective placement is 2.9x-3.6x on the four short calls and 1.09x on ed25519.**
  That spread IS the finding: cost tracks toggles per unit work, so it disappears
  into a long operation and dominates a short one.
- **The pre-2026-08-24 defaults reach 5.04x** on aes256-gcm encrypt - worse than
  CIO's own published 3.66x `__text` growth, which is a blunt way to say how badly
  the finest-grain placement lost.
- **`func` is below `pass` on every row.** Coarser wins everywhere.
- **`nopsw` sits at ~1.00x**, so code movement costs nothing - except aes256-gcm
  encrypt at 1.0870x, where layout alone is a real part of that row's 3.34x.

`ns/op` is what was measured; `cyc/op` is derived from it at the gate-measured
4,408 MHz.

### The original host, re-measured (M5, 2026-09-02)

Apple **M5** (Mac17,2), same protocol, same seven arms, `CHEAP_TIMER=1`.
`data/m5_results_ratios.csv` carries both timers. This is the run the correction
was for: M4 diagnosed the instrument, M5 is where the published headline came
from, so the before/after is now same-machine rather than cross-host.

| benchmark | ns/op | cyc/op | blanket | pass | func | old def | resolved | nopsw |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| ed25519 sign | 7,993 | 36,690 | **1.0057x** | 1.1342x | 1.1251x | 1.2745x | 1.1307x | 1.0068x |
| chacha20-poly1305 enc | 244 | 1,119 | **1.0070x** | 4.8326x | 4.7516x | 6.0872x | 4.8106x | 0.9972x |
| chacha20-poly1305 dec | 257 | 1,180 | **1.0217x** | 4.9040x | 4.7916x | 6.2127x | 4.9214x | 0.9960x |
| aes256-gcm enc | 55 | 252 | **1.0096x** | 5.2735x | 5.1441x | 7.1021x | 5.2663x | 1.0769x |
| aes256-gcm dec | 75 | 345 | **1.0116x** | 4.1435x | 4.0735x | 4.5052x | 4.1886x | 1.0066x |
| argon2id | 43,119,611 | 197,919,014 | **0.9977x** | 1.0002x | 0.9977x | 1.0008x | 1.0004x | 1.0002x |

**Every conclusion replicates, and two of them are now cross-host facts rather
than single-machine observations:**

- **Blanket is free on both.** 1.0057x-1.0217x here against 1.0015x-1.0207x on
  M4, below 1 on argon2id both times. This is the load-bearing claim and the two
  hosts agree to within two points on every row.
- **Coarser wins on both.** `func` < `pass` on all six rows, both machines.
- **argon2id is free in every arm on both.**

**Where the hosts DIFFER, and it is the switch itself.** M5's pass costs
**4.83x** on chacha where M4's costs 3.43x, and **5.27x** on AES against 3.34x.
Same binaries and the same executed switch counts, so this is not placement: it
is **~43.6 cycles per executed switch on M5 against ~33.4 on M4**, measured from
the timed-region counters on each host. The pre-2026-08-24 defaults reach
**7.10x** here. A cost model calibrated in cycles therefore has to be calibrated
per core, which is an argument for `-taint-dit-switch-cyc` being a knob rather
than a constant.

**The layout term reproduces where it matters.** Arm `Z` is within +/-0.7% of 1.0
on five rows and **1.0769x on aes256-gcm encrypt**, against **1.0870x** on M4 -
the same effect on the same benchmark on a second machine, so instrumenting a
55-tick region really does cost something through code movement alone, and an
`A`-vs-`P` comparison books it as DIT cost. That is the whole reason arm `Z`
exists.

**This host's instrumentation offset is 2,521 cycles**, not M4's 3,234
(`data/m5_offset_probe.txt`, 5 of 5 passes at full clock). Two instruments agree
on it: the probe measures 2,521 directly, and subtracting the two timers'
baselines implies **2,585-2,738** on the four benchmarks where the offset is a
large enough fraction to resolve. Assuming M4's number here would have
over-subtracted by 713 cycles - more than the entire corrected aes256-gcm
baseline of 252.

> The first M5 attempt at this table was **void** and is worth recording. It ran
> while another session held nine `gem5.opt` processes on this 10-core machine:
> the offset probe discarded all five sweeps at a depressed 3,720 MHz, and the
> harness carried on into the timing runs regardless. `utils/run_m5_corrected.sh`
> now refuses to start when anything else is above 50% CPU, and treats the
> probe's `NO VALID PASSES` as fatal. Native timing needs an exclusive machine;
> gem5 is exempt because it is deterministic, native is not.

### Running this on another host

The M4 numbers above came from the corrected timer and the seven-arm set. To get
a comparable set on another machine, build the arms and then:

```sh
sudo -E env LLVM_BIN=<toolchain>/bin CIO_DIR=~/Documents/cio-eval \
  CIO_OPT=-O2 CHEAP_TIMER=1 OURS=ditprobe CIO_REPS=15 \
  ARMS="A:baseline:0 C:baseline:1 P:hardened:0 F:func:0 X:fine:0 N:narrow:0 Z:nopsw:0" \
  bash utils/taint_libsodium_sudo_run.sh
```

Run it **twice**, once with `CHEAP_TIMER=1` and once without, so the host's own
instrumentation offset is visible rather than assumed; then
`sudo -E utils/cio_offset_probe.c` compiled per its header measures that offset
directly. The offset is a property of the instrument and differs per host - do not
carry M4's 3,234 cycles over to another machine.

**Data file naming.** The unprefixed files in `data/` are the original M5 run
(kperf-timed, 2026-09-01). Everything from a later host takes a host prefix, so
runs never collide:

| | file |
|---|---|
| original M5 | `cio_benchmarks_O2.csv`, `ditprobe_gates.csv`, `results_summary.csv`, ... |
| M4 | `m4_cio_benchmarks_{kperf,cntvct}_timed.csv`, `m4_results_summary.csv`, `m4_results_ratios.csv`, ... |
| re-run on M5 | `m5_*` (done 2026-09-02, section above). The unprefixed originals are untouched: they are what the published headline table was computed from, and keeping them is how the correction stays auditable |

Two things worth checking on any new host, because both bit here: gate 1 is a
ratio whose ceiling is that core's L1 load-to-use latency, so read it as
cycles/hop and not as a multiple; and `CNTFRQ_EL0` is not `hw.tbfrequency` (1 GHz
vs 24 MHz on M4), so confirm the tick length before trusting any absolute number
from the corrected timer.

### What the NOP-switch control bought

Arm `Z` is arm `P` with all 521 `msr DIT` emitted as `nop` -- same instruction
count, same addresses, byte-identical but for 521 opcodes
(`utils/taint_libsodium_nopsw.sh`, which validates that before letting you use
it). It splits the cost that `A`-vs-`P` conflates:

| benchmark | Z vs A (layout) | P vs Z (switch execution) | P vs A (total) |
|---|---|---|---|
| ed25519 sign | -0.38% | +9.88% | +9.46% |
| chacha20-poly1305 enc | -2.40% | +251.59% | +243.16% |
| chacha20-poly1305 dec | +0.24% | +261.30% | +262.17% |
| aes256-gcm enc | **+8.70%** | +207.18% | +233.89% |
| aes256-gcm dec | -1.62% | +199.81% | +194.96% |
| argon2id | -0.50% | +0.13% | -0.37% |

Layout is negligible except on **aes256-gcm encrypt, where it is +8.70%** --
about 6x that row's MAD. On the shortest region code movement is a real effect,
and an `A`-vs-`P` comparison books it as DIT cost. That is what the control is
for, and it is the reason to keep arm `Z` armed rather than treat it as a
one-off.

### Gates

All four pass; `data/m4_ditprobe_gates_{kperf,cntvct}_timed.csv`, 25 reps each.

| gate | M4 | M5 |
|---|---|---|
| 1. instrument sees DIT | 1.00 -> 3.00 cyc/hop (2.996x) | 1.02 -> 4.01 (3.932x) |
| 2. negative control (Perm) | 1.0000x | 1.0000x |
| 3. P-core residency | 4403-4415 MHz | 4597-4598 MHz |
| 4. mode readback | 0, C=1 | 0, C=1 |

**Gate 1 had to be restated, and the old form was unsatisfiable here.** It was
`PASS (>3.5x)`, fitted to M5. But Const-off is value-predicted at ~1 cycle/hop
and Const-on falls back to L1 load-to-use, so the ratio's CEILING is the L1
latency in cycles: 4 on M5, 3 on M4. A `>3.5x` test can therefore never pass on
a 3-cycle-L1 core no matter how well DIT works, and it printed
"FAIL - instrument cannot see DIT" on a run where DIT plainly worked. The gate
now asserts cycles/hop (predicted <2, and DIT-on must reach Perm), which is the
portable form. Compare this gate across hosts as cycles/hop, never as raw
picoseconds or as a ratio.

## Reproducing

Library and arms:

```sh
utils/taint_libsodium_eval.sh            # fetch -> patch -> build -> bitcode ->
                                         # seed -> analyze -> archives -> check
```

The run (needs root for kperf, and an idle machine):

```sh
sudo -E env CIO_OPT=-O2 OURS=ditprobe CIO_REPS=15 \
  bash utils/taint_libsodium_sudo_run.sh
```

`utils/taint_cio_parity.sh` is the non-root variant. `utils/cio_arm_shim.h`
supplies the blanket arm and the counters; `utils/cio_ditctl.c` is its
DYLD-injected equivalent for non-sudo runs.

Four pieces of this rig existed only in an untracked home directory on the M5
machine and had to be rebuilt to run it again. They are now in the repo, which is
the point:

```sh
utils/ditprobe.c                         # the gate instrument (gates 1-4)
utils/taint_cio_eval_setup.sh            # CIO's drivers + the eval_util.h port
utils/taint_libsodium_narrow.sh          # arm N (indirect-call-resolved IR)
utils/taint_libsodium_nopsw.sh           # arm Z (NOP-switch control)
```

`ditprobe.c` is the odd one out because the rigs build drivers from
`$BENCH_DIR/<name>/<name>.c` and `crypto-dit-benchmarks` is not part of this
repo. Both `taint_libsodium_sudo_run.sh` and `taint_libsodium_bench.sh` therefore
**install it into the benchmark checkout on every run**, overwriting what is
there, the same way `taint_cio_parity.sh` copies `utils/cio_ditctl.c` rather than
trusting the work dir. Edit `utils/ditprobe.c`; the copy under `$BENCH_DIR` is a
build artifact.

`taint_libsodium_eval.sh` builds arms A/C/P/F by default; `fine` (X) needs
`POLICIES_OVERRIDE`, and N and Z come from the two scripts above. The M4 run:

```sh
LLVM_BIN=<toolchain>/bin \
POLICIES_OVERRIDE="hardened:-taint-dit-placement=region;func:-taint-dit-placement=function;fine:-taint-dit-placement=region -taint-dit-switch-cyc=0 -taint-dit-loop-hoist=0" \
  bash utils/taint_libsodium_eval.sh          # then narrow.sh, then nopsw.sh

sudo -E env LLVM_BIN=<toolchain>/bin CIO_DIR=~/Documents/cio-eval \
  CIO_OPT=-O2 CHEAP_TIMER=1 OURS=ditprobe CIO_REPS=15 \
  ARMS="A:baseline:0 C:baseline:1 P:hardened:0 F:func:0 X:fine:0 N:narrow:0 Z:nopsw:0" \
  bash utils/taint_libsodium_sudo_run.sh
```

`CHEAP_TIMER=1` is the corrected timer and is OFF by default, so the original
numbers above stay reproducible byte-for-byte.

### Reproducing the gem5 numbers

One command regenerates every `data/gem5_*` file from a clean machine:

```sh
CIO=<counter-optimization/cio checkout> utils/dit_host_screening/cioparity/reproduce.sh
```

`SKIP_ARGON=1` drops the six-hour argon2id stage; `SKIP_ALIGN=1` drops the two
alignment sweeps that back the layout-spread claim only. The headline needs
neither and takes about fifteen minutes on 160 cores.

It needs a gem5 carrying two patches that are not upstream (PMULL at size=3,
without which AES-GCM panics on GHASH; and `commit.ditCycles`), and it refuses
to start against a stock gem5 rather than quietly producing a four-benchmark
result that looks complete. Every gate in `run_cio_gem5.py` is a hard stop.

**It reproduces bit-for-bit, and that had to be earned.** The first run from a
differently named work directory shifted 67 of 80 cells by 0.2% (median) to
2.6% (worst) and failed a gate. Two paths were leaking into the binary: CIO's
drivers use `assert()`, whose `__FILE__` is the absolute source path, so a
longer work-dir name lengthened `.rodata` and moved every address after it; and
the equal-width `argv[0]` trick fixed the file name but not the directory
prefix. Both are now pinned (drivers compile from a bare relative name;
`argv[0]` lives under `/tmp/cio_<hash>/`), and two sweeps from two work dirs
are byte-identical. The rig's own documented trap - a 0.84% shift from an
argv[0] length change - was only half-closed until this.

## Known limits

- **CIO's seeds over-taint, and it inflates the UNCOVERED count.** Their config
  marks *all five* arguments of `crypto_sign`, including `mlen` (message length)
  and the output buffer. So `CBZX $x1` in `sodium_memzero` is a branch on a
  length reported as a secret branch. The 49 AES-GCM secret-address sites are
  genuine; the total of 126 is not 126 real leaks. Faithful to their config, and
  the reason to re-seed with key-only annotations before quoting it.
- **The AEAD drivers use AD = 0, not 100.** CIO commented out their own
  additional-data parsing (`additional_data_sz = 0; //strtol(...)`). Their
  Makefile passes `AD_LEN=100` and the driver discards it. We reproduce their
  behaviour exactly; the label "msg 100, AD 100" is wrong in their Makefile, not
  in the measurement.
- **libsodium 1.0.21, not their 1.0.18-RELEASE.** Their seed config resolves
  21/21 against it, and 1.0.21 is what the rest of this evaluation uses.
- **`reg_n` counts 1025 regions, not 1000** - the counter accumulator includes
  the 25 warmup iterations while the timing samples do not. The switch counts and
  cycles-per-switch are diluted ~2.4%, conservatively. The runtime percentages
  are unaffected.
- **The `-O0` control run** (`data/cio_benchmarks_O0.csv`) exists because CIO
  build their eval drivers unoptimised. It changes nothing: largest shift
  **+0.8pp**. This closes the objection that their `-O0` harness makes the
  head-to-head unfair.
- **`aes256gcm` runs on ARM** despite `--disable-asm`, because libsodium's
  armcrypto backend is intrinsics, not `.S`. The timed call is
  `crypto_aead_aes256gcm_encrypt`, not `_afternm`, so each iteration re-expands
  the key inside the timed window - CIO's choice, preserved.
- **Two pieces of arm N and the gate instrument are RECONSTRUCTIONS, not the
  originals.** `ditprobe` and the indirect-call-resolution recipe were never
  committed and the M5 machine's copies are gone. The narrow arm is rebuilt to
  hit M5's recorded statics exactly (749 switches / 164 functions / 250136 bytes
  / 16 info-loss records, and the script fails if it ever stops matching), but
  "same numbers" is the evidence, not "same method" -- the original may have
  resolved those calls another way. `ditprobe` is rebuilt from its four metric
  names and its output format; the mechanism it gates is confirmed present on
  both hosts, but it is not the original code.
- **A stride chase does not test gate 1, and it looks like it does.** Rebuilding
  `ditprobe` the obvious way -- a value-predictable chase as a STRIDED sequence
  of loaded pointers -- returns a flat 1.00x gate on M4. The predictor is a
  CONSTANT-value predictor: only a self-referencing node, whose loaded value
  never changes, is predicted. Every other shape measured flat (2/4/8/64/512-node
  cycles, and the random permutation). A stride build therefore reports a clean
  1.00x that reads as a hardware finding and is really a broken instrument. The
  measured table is in the header of `ditprobe.c`.
- **Gate 1's replacement threshold is fitted to two hosts.** `<2 cyc/hop`
  predicted, and DIT-on must reach Perm. Better grounded than the `>3.5x` it
  replaces, which was unsatisfiable on a 3-cycle-L1 core, but two data points is
  two data points.
- **The AES switch counts are not resolvable, on either host.** In-region
  instruction differencing over a ~300-cycle region gave 26 and 22 switches/op
  in one M4 run and 26 and 14 in the other, from IDENTICAL binaries. So
  cycles-per-switch for the AES rows (22.5-48.8 across runs) is noise and should
  not be quoted -- the same objection this README already raises against its own
  M5 AES rows, now confirmed twice. The three surviving rows agree at 25-33
  cycles per switch across both hosts and both timers.
- **Under `CHEAP_TIMER=1` the absolute column is TIME, not cycles**, and
  `reg_cyc`/`reg_ins` stay on kperf. So that run gives trustworthy percentages
  while the absolute cycles/op figures should come from the kperf-timed run.
  argon2id's two absolutes differ by 5% (211.4M vs 222.6M cycles) because
  wall-clock includes preemption over a 50 ms region where kperf counts thread
  cycles only.
- **The per-TU flag is coarser than the configuration evaluated here.** These
  arms come from whole-library bitcode (`llvm-link` -> one MIR -> the pass over
  everything at once), which is what produces 521/569/631 switches. Hardening
  per translation unit with `clang -ftaint-harden`, the shipped user-facing
  flag, yields 134 static switches and - measured under gem5 - **3 committed
  writes per signature against this configuration's 85**, because taint cannot
  cross a TU boundary so the mode is set once and inherited. Both are sound;
  they are not the same operating point, and the numbers here belong to the
  whole-library one.
- **Three counter claims were made and retracted** during this work: that blanket
  raises IPC (whole-process artifact; the timed region is +/-2%), that timed-region
  IPC was 12-14 (instrumentation asymmetry, since fixed), and that DIT removes
  value-predictor flushes (flush counts move -0.1% to -7.3%). The switch-count
  decomposition above replaced all three and is what should be cited.
