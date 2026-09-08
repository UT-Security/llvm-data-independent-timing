# Experiment 02 on real silicon — the crossover on an Apple M4

The gem5 half of `paper_experiments/02-libsodium-signed-lookup` finds a
secret-fraction crossover on a Neoverse-V2 with EVES and VTAGE. This rig runs
the same driver on hardware and finds one too. The two are drawn side by side in
`figures/crossover-gem5-vs-m4.{png,pdf}`.

**The result in one line.** Blanket DIT costs about nothing on a 96%-secret
request and +34.4% of a 0.4%-secret one; **Apple's own bracket around the crypto
call** costs +28.2% and +0.2% of the same two; they cross at **f\* = 52%
secret**, against gem5's 96%. One bracketed call is **455-510 cycles** here and
~50 in the simulator. The compiler pass is measured too (1,260-1,340 cycles a
call, crossing at f\* = 26%) and is in the CSV.

**And the finding that had to come first.** Run the *canonical* gem5 lane on this
machine and blanket DIT reads **+0.0% at every point** — no cost, no crossover,
nothing to place. That is not because Apple silicon has no value predictor. It is
because the canonical lane's record header holds `0x2545F4914F6CDD1D`, and **this
machine's load value predictor holds 36 bits**. Give the same lane a header that
fits and it behaves like gem5's, to within a few points at every q. The whole
difference between "DIT is free here" and "DIT costs a third of the public lane"
is the width of one constant.

## What is the same as gem5, and what is not

| | gem5 rig | this rig |
|---|---|---|
| driver | `benchmarks/signed_lookup/signed_lookup_gem5.c` | the same file, byte for byte (`signed_lookup_gem5.c` here, sha256 checked by `build_silicon.sh`), plus `hdr_const_param.patch` |
| library | libsodium 1.0.21, `--disable-asm`, `-march=armv8.4-a`, contract fixpoint seeds, twins, owned list | the same, via `utils/taint_libsodium_arms.sh` |
| arms | nodit, blanket, api (Apple's bracket), apinop, pass, nop | nodit, blanket, **bracket, bracketnop, bracketnobar**, pass, nop |
| the bracket's barrier | `isb sy` -- gem5 has no FEAT_SB | Apple's real `sb`, checked at build time |
| `MSR DIT` | both models: serialising and renamed | **serialising only** — that is what the hardware does |
| instrument | simulator statistics | PMC0/PMC1 read from EL0 (`pmc_ipc.h`) |
| f_secret | measured with `--nosecret` | the same |
| layout control | 5 stack offsets | the NOP twins, equalised argv, and the fast-cluster median (below) |

`hdr_const_param.patch` is the only change to the driver and it is two lines: it
wraps `#define HDR_CONST` in `#ifndef`, so the value becomes a `-D`. The default
is unchanged, so a build with no `-D` is the gem5 driver exactly. It should go
upstream into gem5-DIT — the simulator can then sweep the same axis and say how
many value bits VTAGE is really using.

## The two lanes

`HDR_CONST` is the value every record header holds — the constant a value
predictor is meant to learn and take off the critical path.

| lane | `HDR_CONST` | bits | what it measures |
|---|---|---|---|
| `wide` | `0x2545F4914F6CDD1D` | 62 | the canonical gem5 lane. On an M4: blanket +0.0% at every q and every L |
| `narrow` | `0xCAFEBABE` | 32 | a header this machine can predict. Blanket +34.5% on the public lane at q=¾ |

Any value up to 36 bits behaves identically, and every value from 37 up behaves
identically to the wide one — `m4_header_width.csv` is the step function, and it
is a step, not a slope: 13 widths, 15 reps each, +45.6% through 36 bits and
±0.05% from 37.

## The Apple bracket

`api_bracket.c` is experiment 09's file, shared unmodified -- this rig is its
third consumer, which is what makes "the same bracket on both instruments" a
fact rather than a claim. The arm links the **unhardened** library and adds only
the wrapper, so the library, the driver and the layout are the `base` arm's:

```
mrs  x19, DIT          ; the token: what the caller's DIT was
msr  DIT, #0x1         ; enable
sb                     ; speculation barrier (Apple's own; 0xd50330ff)
bl   _crypto_aead_chacha20poly1305_ietf_encrypt
tbnz w19, #0x18, .+8   ; was it already set?
msr  DIT, #0x0         ; clear only if it was clear
```

Apple's ld64 has no `--wrap`, which is how the gem5 rig interposes, so the
driver's calls are renamed at the preprocessor onto `expedite_api_*`
(`api_bracket.c`'s `API_MACRO_RENAME` path). The driver source still does not
change. `api_bracket.c` itself is compiled WITHOUT those renames, or
`API_REAL(f)` would expand to the wrapper's own name and every bracketed call
would recurse.

Three arms, and the second one is not optional:

| arm | what |
|---|---|
| `bracket` | the sequence above |
| `bracketnop` | its **instruction-matched twin**: the token read a `mov xzr`, both writes and the barrier `nop`. 17 instructions at the same addresses, none touching DIT, and because the read now yields 0 the restore takes the same branch the real bracket takes. `bracket - bracketnop` is the mode's cost; the twin itself reads -0.5% to +1.2%, which is the layout term |
| `bracketnobar` | Apple's sequence minus `sb`. **Not a shippable configuration** -- the barrier is what makes the mode change apply to what follows -- but it splits the bill: 480 cycles becomes ~300 without it |

Build-time gates: the wrapper must disassemble to `mrs DIT` / `msr DIT,#1` /
`sb` / call / `tbnz` / `msr DIT,#0`; the twin must touch DIT nowhere and
disassemble to the same length; and the part must actually execute `sb`, which
is probed rather than assumed.

## The instrument

`pmc_ipc.h` reads Apple's fixed performance counters straight out of the system
registers, which this host allows because its kernel carries `PMCR0_USEREN_EN`
(PacmanPatcher; `boot-args` has `enable_skstb=1`). It is a drop-in for the gem5
rig's `kperf_ipc.h` — the driver's quoted `#include "kperf_ipc.h"` resolves to a
two-line shim next to it — so the driver source needs no change to be measured a
thousand times more cheaply than kperf's ~3,400-cycle-per-boundary call.

Instructions are read `isb`-ordered and cycles bare; the reasons are in the
header and they are not symmetric. Exactness check: 21,000,012 instructions over
a 3,000,000-iteration loop.

**Root is not required and buys one thing:** `kern.sched_thread_bind_cpu`. The
PMCs are per-core, so an unpinned thread that migrates mid-region differences two
cores' counters. Without root the rig instead brackets the region with
`CNTVCT_EL0` (1 GHz on this part — read `CNTFRQ`, and do not use
`hw.tbfrequency`, which reads 24 MHz here) and rejects any sample whose implied
clock is not a P-core's 3.4–5.0 GHz. A migration reads hundreds of GHz or
negative and cannot survive that. **Rejects are counted and printed next to the
numbers**, never dropped quietly; across the committed sweeps the rate is 0–3 in
15.

Three things this bought that are worth knowing before trusting a number here:

- **The warmup is sized in time, not in requests.** A fresh process starts at a
  low DVFS point and takes tens of milliseconds to reach 4.4 GHz. At the gem5
  driver's default of 50 warmup requests every run reads 2.2–3.1 GHz and fails
  the clock gate; at ~30 ms of warmup every run reads 4.40–4.41 and passes.
- **argv is equalised across arms.** The request's retired instruction count
  moves by ~5 (0.1%) with the stack alignment the process gets, because the
  AEAD's stack buffers land differently and macOS's `memcpy` dispatches on
  alignment. `--blanket` is 9 characters only one arm passes, and the arms'
  binaries are named from 18 to 26 characters, so every arm sat at a different
  alignment: nodit read 5,137.2 instructions per request against blanket's
  5,147.9 -- a systematic +10 that is not the mode, and padding nodit's argv to
  the same length reproduced blanket's number exactly. The rig now reaches the
  binaries through equal-length symlinks (`bin/argv/`) and pads the argument
  list with an ignored 9-character token. This is the instruction-count version
  of the argv[0] control the gem5 rig applies by rooting its binaries at a
  constant-length path.
- **The page-mapping lottery.** Once the lane's tables leave L1 — 128 KB of table
  upward — a run's cycles come out *bimodal*: two states about 15% apart, each
  reproducible to 0.05% within itself, decided before the process runs and
  identical for every arm (nodit and blanket split 11/10 and 12/9 over 21 runs
  each, and their two states agree pairwise to 0.05%). It is the physical pages
  the kernel hands a 1 MB BSS. **It is the same size as the effect being
  measured**, so a plain median over an even split is a coin toss. `med()`
  therefore clusters the samples at the largest relative gap and reports the fast
  cluster's median — the lottery only ever adds cost, so the fast state is the
  machine's floor and is the state both arms must be compared in. `n_hi` records
  how many samples landed in the slow cluster and all of them stay in the JSONL.
  The canonical lane runs at 4 KB, where there is no split at all and this is the
  plain median.

## The gates

All fatal except the clock gate, which rejects and counts.

1. **`dit` readback** — blanket must exit with `PSTATE.DIT` set, every other arm clear.
2. **`pub_dit`** — the fraction of requests entering the PUBLIC lane with DIT
   already set: 1.0 for blanket, 0.0 for everything else. This is the gate that
   catches a tail call out of a hardened entry point turning selective placement
   into blanket in disguise. It has read 1.000 in this project before.
3. **public-lane checksum** — every arm must agree. It has to be the public lane:
   libsodium draws a fresh AEAD key per process, so the ciphertext byte the full
   run folds into `sink` differs between two runs of the *same* arm.
4. **instructions** — nodit and blanket are one binary and must retire the same
   count within 0.02% (two runs of one binary differ by ~0.005%; it can be this
   tight only because argv is equalised, see above), and the bracket and the
   pass must both retire more than nodit -- if they do not, the driver's calls
   were never renamed onto the wrapper and the arm is the baseline in disguise.
5. **implied clock** — 3.4–5.0 GHz, or the sample is a migration.
6. **static counts**, at build time: the pass archive must contain `msr DIT` and
   its NOP twin must contain none, and both lanes of an arm must disassemble to
   the same instruction count.

## Running it

```sh
export ARMS_WORK=$HOME/Documents/libsodium-exp02-m4     # library build root
export LLVM_BIN=<repo>/build/bin                        # the taint toolchain
utils/dit_host_screening/signed_lookup/silicon/build_silicon.sh          # ~1 min
ARMS=base LANES="b8 b16 b24 b32 b34 b35 b36 b37 b38 b40 b48 b56 b62" \
  utils/dit_host_screening/signed_lookup/silicon/build_silicon.sh link   # width sweep

R=utils/dit_host_screening/signed_lookup/silicon/run_crossover_m4.py
for lane in narrow wide; do
  python3 $R --lane $lane --L 10 30 50 100 200 400 700 1000 2000 5000 20000 \
            --reps 15 --roi-ms 40 --warm-ms 30 --out <dir>
done
python3 $R --hdr-sweep --q 4 --out <dir>          # and --q 3
python3 $R --q-sweep --lane narrow --out <dir>    # and --lane wide, --mech-L 200
python3 $R --tblbits-sweep 9 11 12 13 14 15 16 --lane narrow --q 4 --out <dir>
python3 $R --chunks-sweep 1 2 5 10 25 --lane narrow --mech-L 200 --out <dir>  # per-call cost

python3 utils/dit_host_screening/signed_lookup/silicon/derive_exp02_m4.py <dir>
python3 utils/dit_host_screening/signed_lookup/fig_exp02_silicon.py
```

The whole thing is about six minutes of measurement. Close the user applications
first (`docs/` calls this a quiet-machine run); VS Code and a terminal are fine.

## Contents

| path | what |
|---|---|
| `signed_lookup_gem5.c` | the gem5 driver, byte-identical, vendored at the pinned commit. Never edited — `build_silicon.sh` checks its sha256 |
| `hdr_const_param.patch` | the only change: `HDR_CONST` becomes a `-D`. Applied into the build directory, not into the source |
| `pmc_ipc.h` | the instrument: PMC0/PMC1 from EL0, the clock gate, the pinning attempt |
| `kperf_ipc.h` | two lines, so the driver's quoted include finds `pmc_ipc.h` |
| `dit_switch_cost.c` | what one serialising `msr DIT` costs on this part — 33.5 cycles, 11.5 for a same-value write. The runner divides by it rather than quoting a constant |
| `build_silicon.sh` | library arms, both lanes, the static gates |
| `run_crossover_m4.py` | the sweeps and the run gates |
| `derive_exp02_m4.py` | raw JSON -> the committed CSVs in `paper_experiments/02-.../data/` |
| `bin/argv/` | equal-length symlinks to the arms' binaries, so argv[0] is the same number of bytes for every arm |
| `out/` | raw runs. `runs-*.jsonl` holds every sample including the rejected ones |
