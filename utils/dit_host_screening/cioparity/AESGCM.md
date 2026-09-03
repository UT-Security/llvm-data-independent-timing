# AES-GCM under both switch models, after implementing PMULL64 in gem5

**Status: complete, gem5. Cross-model control gate passes exactly.** Measured
2026-09-02 on the Neoverse-N1 host. Simulator: gem5-DIT branch `pmull64`
(`2acf7637bc` + the PMULL patch), build config byte-identical to the unpatched
build. libsodium 1.0.21 `--disable-asm`, whole-library bitcode, CIO's own
drivers and seeds. Data in `data/aesgcm_results.{csv,jsonl}`,
`data/aesgcm_analysis.txt`; validation in `data/pmull64_validation.txt`.

## Why this needed a simulator patch

libsodium's `aes256gcm` ships only `aesni/` (x86) and `armcrypto/` (ARM) with no
software fallback, and the armcrypto backend self-enables crypto with
`#pragma clang attribute push(target("neon,crypto,aes"))`, so `-march` cannot
steer around it. gem5 decoded PMULL only for `size == 0`; GHASH uses `size == 3`
(`Vd.1Q, Vn.1D, Vm.1D`, 64x64 -> 128 carry-less multiply), which fell to
`Unknown64` and panicked. Two files, +29/-5, and no new arithmetic — the generic
carry-less multiply and `bigger_type_t<uint64_t> = __uint128_t` were already
there. Validated byte-identical to real N1 hardware, which implements
FEAT_PMULL; see `data/pmull64_validation.txt`.

## Results

| benchmark | arm | switches/op | renamed | serialising |
|---|---|---|---|---|
| aes256-gcm encrypt | base | 0 | +0.00% | +0.00% |
| | blanket | 0 | +0.15% | +0.15% |
| | nop | 0 | -1.07% | -1.07% |
| | **taint** | **15** | **-0.75%** | **+28.94%** |
| | taintfn | 15 | -0.59% | +30.28% |
| | fine | 26 | +0.48% | +48.62% |
| aes256-gcm decrypt | base | 0 | +0.00% | +0.00% |
| | blanket | 0 | +8.64% | +8.64% |
| | nop | 0 | +0.51% | +0.51% |
| | **taint** | **15** | **+8.46%** | **+51.38%** |
| | taintfn | 15 | +11.76% | +55.79% |
| | fine | 18 | +1.19% | +47.70% |

### Decomposition

| benchmark | layout | renamed | **serialising** | total | sw/op | wr/Mcyc | floor |
|---|---|---|---|---|---|---|---|
| aes256-gcm decrypt | +0.51 | +7.94 | **+42.92** | +51.38 | 15 | 13,787 | 0.00% |
| aes256-gcm encrypt | -1.07 | +0.31 | **+29.69** | +28.94 | 15 | 12,303 | 0.00% |

### Cycles per switch

| benchmark | renamed | serialising |
|---|---|---|
| aes256-gcm decrypt | 5.8 | 36.9 |
| aes256-gcm encrypt | 0.3 | 24.4 |

## What this settles that silicon could not

**Experiment 09 disclaims its own AES-GCM rows.** Its README says they "rest on
15-18 switches and are too noisy to support the claim; they are shown for
completeness, not as support." gem5 counts committed switches exactly, and it
reads **15 switches/op** for the shipped `taint` arm on both encrypt and
decrypt — inside the 15-18 window silicon estimated, now with no noise. The
cross-model control floor is **0.00%** on both benchmarks, so the serialisation
column is not bounded by drift.

So the AES-GCM rows become usable: **serialisation is +29.69 and +42.92 points
of a +28.94 / +51.38 total.** The conclusion the paper drew from chacha20 and
ed25519 holds here too, on the two benchmarks it previously had to set aside.

## AES-GCM decrypt is the one workload here with a real dwell term

Everywhere else in this study blanket DIT is free and the whole cost is switch
serialisation. Decrypt is different: **blanket costs +8.64%** with zero switches
and zero committed DIT writes. The mode merely being *on* is expensive, which is
a dwell term, not a toggle term.

`ditSuppressed` is 0 for every AES-GCM arm — the hot path is AES/PMULL hardware
crypto with nothing comp-simp-eligible — so whatever the mode costs here, that
counter cannot see it.

**The `renamed` column for decrypt (+7.94) is mostly dwell, not switch cost.**
`(taint - nop)` contains both, and blanket shows dwell alone is +8.64.

### RETRACTED 2026-09-02: "finer placement wins here"

An earlier version of this file claimed `fine` beats `taint` under the renamed
model (+1.19% vs +8.46%) because narrower regions spend less time with the mode
on, and called it the "coarser always wins" result inverting. **That is wrong,
and so were two successive mechanisms offered for it** (first the DIT-gated
prefetcher, then the value predictor staying live over public code). Two controls
settled it:

**1. Disable every DIT-gated optimisation.** With `--eves --dmp --comp-simp` all
off, so the mode can suppress nothing, `fine` is *still* 6.69% faster than
`base` and `taint`:

| config | base | nop | fine | taint | blanket |
|---|---|---|---|---|---|
| no DIT-gated opts | 1180 | 1180 | **1101** | 1180 | 1182 |
| `--eves` only | 1095 | 1091 | 1101 | 1180 | 1182 |
| all three | 1096 | 1094 | 1101 | 1180 | 1182 |

`fine` reads 1101 in all three configurations and `taint` reads 1180 in all
three: **neither arm gains anything from the optimisations**, while `base` gains
7.2%. Same committed work (1495 insts vs base's 1477 - `fine` executes MORE),
higher IPC (1.36 vs 1.25). The advantage is a codegen/layout lottery on that
binary, not placement.

**2. Measure dwell directly** (`commit.ditCycles`, added for this question):

| arm | cyc/op | ditCycles/op | under DIT |
|---|---|---|---|
| base | 1090 | 0.0 | 0.0% |
| nop | 1091 | 0.0 | 0.0% |
| blanket | 1175 | 1175.0 | 100.0% |
| taint | 1180 | 1171.0 | **99.2%** |
| fine | 1102 | 1086.0 | **98.5%** |

`taint` and `fine` dwell within **0.7 points** of each other. The finer policy
buys no reduction in time under the mode on this workload - its lower static
collateral (2,466 vs 3,084) does not translate into dynamic dwell. Note also
that `taint` at 99.2% means region placement is effectively BLANKET on this
function, which is consistent with f_secret ~ 100%.

Sharpest form: **`fine` executes more switches (18 vs 15) at the same dwell - it
is strictly worse placement - yet measures 6.7% faster.**

**Root cause in the rig, and it is not fixed.** `nop` is emitted from
`taint.mir`, so it controls for TAINT's layout only. `fine` and `taintfn` have
their own placement and therefore their own layout, and nothing measures it.
`nop` licenses `taint`-vs-`base`; it does NOT license `fine`-vs-`taint`. Per-
policy layout controls (`finenop`, `taintfnnop`, emitted from `fine.mir` and
`taintfn.mir` with `-taint-dit-nop-switches` on the object stage) are required
before any cross-policy ranking from this rig is quotable.

**What this does NOT touch:** the renamed-vs-serialising headline. That compares
the SAME binary under two machine configurations, so layout cancels exactly.

Sign agreement with silicon is worth recording: exp 09 measured blanket at
**+1.95%** on aes decrypt (the only benchmark of six where blanket cost
anything) and **-0.59%** on encrypt. We get +8.64% and +0.15% — same ordering,
same sign, magnitudes inflated as gem5 is everywhere in this project.

## A gate that needs widening

Gates 5 and 6 in `run_cio_gem5.py` treat `compSimplifier.ditSuppressed > 0` as
the witness that the mode is active. **16 of 24 cells failed on that**, and all
16 were false alarms: AES-GCM offers comp-simp nothing to suppress, so the
counter reads 0 whether or not the mode is on.

Settled independently with `-DDIT_READBACK` gate builds:

| arm | `PSTATE.DIT` at exit | expected |
|---|---|---|
| blanket | **1** | 1 |
| taint | **0** | 0 |

So the mode is genuinely set in blanket and genuinely restored in the pass arm.
The real witnesses are `commit.ditWrites` for placement arms (750 over 50
regions = 15/op, non-zero as required) and a mode readback for blanket. Every
other gate passed, including the cross-model control and the `nop`-vs-`taint`
instruction-count identity. **`ditSuppressed` should be treated as sufficient but
not necessary** — the gate as written is unsound on any workload whose hot path
is not comp-simp-eligible, and it would have blocked this experiment.

## Limits

- **Absolute AES-GCM cycles are not a claim about silicon.** PMULL64 is modelled
  with the generic `SimdMultOp` latency, not a measured FEAT_PMULL latency. The
  switch-model *delta* is unaffected, being a difference between two runs of one
  binary in which every PMULL is modelled identically.
- **The patch is ungated.** It decodes PMULL64 regardless of whether the config
  advertises FEAT_PMULL. Permissive in the direction that cannot cause a false
  pass here, but it must be gated before going near upstream.
- **Verification is a round-trip, not a known-answer test.** The drivers decrypt
  their own ciphertext, which would not catch an error self-consistent in both
  directions. libsodium's AES-GCM KAT under SE mode is the missing check.
- Each iteration re-expands the key inside the measured region, because the timed
  call is `crypto_aead_aes256gcm_encrypt` rather than `_afternm` — CIO's choice,
  preserved.
- The AEAD drivers pass AD = 100 and discard it (`additional_data_sz = 0;`
  commented out upstream); reproduced exactly.

## Reproducing

```sh
git -C ~/Documents/gem5-DIT worktree add ~/Documents/gem5-DIT-pmull -b pmull64 master
# apply the two-file patch, then
cd ~/Documents/gem5-DIT-pmull && scons build/ARM/gem5.opt -j60

cd utils/dit_host_screening/cioparity
CIO=<cio checkout> WORK=~/Documents/libsodium-cioparity-aes ./build_arms_wl.sh
G5=~/Documents/gem5-DIT-pmull WORK=~/Documents/libsodium-cioparity-aes \
  python3 run_cio_gem5.py --benches aesni256gcm_encrypt,aesni256gcm_decrypt --jobs 16
```

Validation: `pmull_test/pmull64_test.c` built with `-march=armv8.2-a+crypto`
runs both natively and under gem5; `pmull_test/cmp_bitidentity.py` diffs the two
simulators on chacha20.
