# 09 - libsodium, CIO parity

**Status: complete, silicon. The percentage columns below are SUPERSEDED on four
of six rows -- see "Second host, and a measurement correction".** Measured
2026-09-01 on Apple M5 (Mac17,2), root, real kperf cycle counters; replicated
2026-09-02 on Apple M4 (Mac16,10).

**Published artifact:** https://claude.ai/code/artifact/842a1394-e976-4587-861c-076657829a48
Source: `figures/cio-parity.html`. To update the page, republish **that URL**
(`Artifact` with `url=...`); publishing the file without it creates a second
artifact instead of updating this one.

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

**Three readings, in order of importance:**

1. **Blanket is free on every one of them**, and faster than baseline on four.
   The gated optimisations were not paying on this code, so suppressing them
   costs nothing.
2. **Coarser placement beats finer on all six.** `func` < `region` everywhere.
   That is what the cost model predicts when dwell is zero: narrowing coverage
   buys nothing, so you only pay toggles, and fewer toggles is better.
3. **argon2id is free in every arm.** A 191M-cycle operation amortises every
   switch. Overhead tracks executed switches *per unit work*, not a fixed
   per-call charge - and this is the cleanest demonstration we have, because it
   is the benchmark where CIO pays most (27.84x) and we pay nothing.

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
| argon2id | ~0 (noise) | ~0 | n/a |

**The evidence is the CONSISTENCY of the last column across independent
benchmarks (40.3, 41.2, 44.8), not any per-row agreement.** Cycles-per-switch is
derived by dividing measured cycles by measured switches, so multiplying it back
out reproduces the measurement by construction and proves nothing. The AES rows
rest on 15-18 switches and are too noisy to support the claim; they are shown for
completeness, not as support.

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
paths of this experiment are unaudited.

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

Three pieces of this rig existed only in an untracked home directory on the M5
machine and had to be rebuilt to run it again. They are now in the repo, which is
the point:

```sh
utils/taint_cio_eval_setup.sh            # CIO's drivers + the eval_util.h port
utils/taint_libsodium_narrow.sh          # arm N (indirect-call-resolved IR)
utils/taint_libsodium_nopsw.sh           # arm Z (NOP-switch control)
```

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
- **Three counter claims were made and retracted** during this work: that blanket
  raises IPC (whole-process artifact; the timed region is +/-2%), that timed-region
  IPC was 12-14 (instrumentation asymmetry, since fixed), and that DIT removes
  value-predictor flushes (flush counts move -0.1% to -7.3%). The switch-count
  decomposition above replaced all three and is what should be cited.
