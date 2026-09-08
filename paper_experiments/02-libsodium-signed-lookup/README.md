# 02 - libsodium signed lookup

**Status: complete on both instruments, on the same microbenchmark.** gem5
re-run 2026-09-06 on the current compiler; **silicon measured 2026-09-08 on an
Apple M4**, on the same driver and the same library arms. Both halves now run
the AES-256-GCM 64 B secret lane, gem5's own eight L points and the same four
arms, and both find the crossover -- **f\* = 60.0% in the simulator against
f\* = 29.3% on the hardware**. See "The crossover on silicon" immediately
below. The gem5 numbers here are that 2026-09-06 run: 1,100 gem5 runs, 0 failed, on
this repo's own `gem5-DIT` submodule at the pinned commit and its own `build/`.
The 2026-09-05 run is kept under "Rerun 2026-09-05" and the 2026-09-03 one under
it, so the three are comparable. What moved on 2026-09-06 is the executed switch
count, 38 -> 32 per request, from `-taint-dit-external-preserves` becoming the
default; the headline shape did not move.

The rig changed on 2026-09-03 (see "Why the rig changed"); everything measured
before that is under "Retired signing driver" at the bottom and must not be cited.

**Figures:** `figures/crossover-gem5-vs-m4.{png,pdf}` is the headline, the two
machines side by side. `figures/overhead-vs-secret-fraction.{png,pdf}` and
`figures/predictions-suppressed-vs-L.{png,pdf}` are the gem5 half alone;
`figures/predictability-gem5-vs-m4.{png,pdf}` and
`figures/m4-predictor-width.{png,pdf}` are the mechanism. The gem5 figures come
from `utils/dit_host_screening/signed_lookup/fig_exp02.py` and the three
cross-machine ones from `fig_exp02_silicon.py` beside it; both read `data/` and
nothing else.

**Raw results:** `results/gem5/` holds what the rig actually wrote - one entry per
run for all 1,100 runs, the arm switch counts, the seed file used, and the
analysis reports - before `derive_exp02.py` imports it into `data/`. Its README
says what each file is and where the 1.7 GB of per-run `stats.txt` lives.

---

# The crossover on silicon (Apple M4, 2026-09-08)

The same experiment on hardware. Same driver, byte for byte; same library;
`f_secret` measured the same way with `--nosecret`. What differs is the
instrument (PMC0/PMC1 read from EL0, not simulator statistics) and that
**`MSR DIT` is serialising, because that is what the machine does**.

**The arm this section is about is APPLE'S OWN BRACKET around the crypto call**,
not the compiler pass: read the previous DIT state, `msr DIT, #1`, a speculation
barrier, the call, and clear only if it was clear. That is what
"Writing ARM64 code for Apple platforms" tells a library author to write and
what AWS-LC ships (`armv8_get_dit` / `armv8_set_dit` / `armv8_restore_dit`,
experiment 14's `ditsb`). It is measured with its **instruction-matched NOP
twin** beside it -- the token read a `mov xzr`, both writes and the barrier
`nop`, same 17 instructions at the same addresses -- so `bracket - twin` is the
mode's cost and everything else is layout. The pass is measured too and is in
`data/m4_arms.csv`; it is not the comparison here.

The rig, its gates and its hazards: `utils/dit_host_screening/signed_lookup/silicon/README.md`.

**Figure:** `figures/crossover-gem5-vs-m4.{png,pdf}`.
**Data:** `data/m4_aes_arms.csv` (the matched panel) and `data/m4_arms.csv`
(the chacha lane, and the pass, and the barrier variants).

| | gem5 Neoverse-V2 FDP | Apple M4 |
|---|---|---|
| secret lane | AES-256-GCM, 64 B | **AES-256-GCM, 64 B** |
| blanket, secret-heavy end | -0.4% at f=91.7% | **+12.1%** at f=85.3% |
| blanket, public-heavy end | +31.3% at f=0.6% | **+34.5%** at f=0.1% |
| Apple's bracket, secret-heavy end | +16.6% | **+103.8%** |
| Apple's bracket, public-heavy end | -0.3% | **+0.3%** |
| **the bracket stops winning at** | **f\* = 60.0%** | **f\* = 29.3%** |
| one bracketed call | 206-351 cycles | **333-710 cycles** |
| one mode write | ~14 cycles, renamed | **33.5 cycles (measured)** |
| the same bracket, renamed switch (`--expedite`) | 10-29 cycles, **no crossover** | not available on silicon |

**Both machines have the crossover, and on the SAME microbenchmark they put it
31 points apart: f\* = 60.0% in the simulator, f\* = 29.3% on the hardware.**
That is the result. A library author reading the simulator would conclude the
bracket is the right call for any workload under about 60% secret; on the part
that actually ships, the answer flips at 29%. The simulator's own renamed design
is the flat dashed line -- 10-29 cycles a bracket, never crossing blanket
anywhere in the sweep -- which is what the switch could cost if the hardware
renamed it.

**Why AES-256-GCM and not the driver's own chacha.** gem5 cannot resolve a
chacha20-poly1305 bracket: chacha is a serial ARX chain that does not fill the
reorder window until the message is ~4 KB, and by then the request is 46,000
cycles and a ~300-cycle switch is 0.6% of it, under the model's own layout
noise. Growing the message does not help -- the switch's cost saturates while
the request grows without bound, and that sweep peaks at 0.64%
(`results/gem5-apple-expedite/scout_secret_size.py`). AES-256-GCM has
independent round and GHASH work per block, so it saturates the window at
16-64 bytes and the switch is finally a measurable share of the request.

**Both panels now run that same lane** (2026-09-08). Same driver, same public
lane, same secret op, same L points, same arms, same bracket -- the M4 half
rebuilt with `SECRET_OP=aes SECRET_MLEN=64`. The one thing that still differs is
`HDR_CONST`, and it has to: the public lane's cost IS the load-value predictions
the mode suppresses, so the header must be a value the machine under test can
hold. gem5 holds 62 bits, this M4 holds 36 (measured). A shared constant would
measure the mechanism on one machine and nothing at all on the other.

**On one benchmark the two instruments are 31 points of f apart**, and the
earlier 8-point agreement was an artifact of comparing different secret lanes.
Two terms, both pushing the same way:

| | gem5 | Apple M4 | ratio |
|---|---|---|---|
| one bracket, cyc/request | 206-351 | 333-710 | ~2x dearer |
| the request at L=10, cycles | 1,139 | 314 | 3.6x shorter |
| bracket as a share of that request | 18% | 106% | |
| **f\*** | **60.0%** | **29.3%** | |

AES-GCM on hardware AES is fast, so the secret lane the bracket is amortised
over is short; gem5 sustains IPC 1.07 at L=10 where the M4 sustains 3.89 on the
same instruction stream, so the same public work is a smaller share there. The
simulator is optimistic about hand placement by roughly a factor of two in
secret fraction, on the same code.

**M4, AES-256-GCM 64 B secret lane** -- the matched panel. `data/m4_aes_arms.csv`,
narrow lane, 11 reps, `bracket - twin` cycles per request beside it.

| L | f_secret | blanket | Apple's bracket | bracket's NOP twin | bracket - twin, cyc/req | bracket vs blanket | blanket on the PUBLIC lane alone |
|---|---|---|---|---|---|---|---|
| 10 | 85.26% | +12.1% | +103.8% | -0.7% | 333 | +91.7% | +22.7% |
| 30 | 64.39% | +23.0% | +72.4% | -2.4% | 438 | +49.4% | +29.8% |
| 50 | 49.04% | +28.9% | +51.5% | -0.3% | 437 | +22.6% | +30.8% |
| 100 | 27.41% | +31.0% | +28.8% | -0.4% | 445 | -2.2% | +32.6% |
| 200 | 14.87% | +32.3% | +14.2% | -0.9% | 438 | -18.0% | +34.0% |
| 1,000 | 3.03% | +34.2% | +3.2% | -0.1% | 452 | -31.1% | +34.6% |
| 5,000 | 0.59% | +34.4% | +0.7% | -0.1% | 516 | -33.7% | +34.5% |
| 20,000 | 0.14% | +34.5% | +0.3% | -0.0% | 710 | -34.2% | +34.5% |

**The public-lane column is the cross-check.** It reads +22.7% to +34.5%, which
is the chacha sweep's column to within a few tenths at every length -- the same
public lane, as it must be, since only the secret op changed. And the twin
column stays inside -2.4% to +0.0%, so the bracket's restructuring of the binary
is worth about a point and the rest of its column is the mode.

**The chacha lane stays measured and committed** (`data/m4_arms.csv`, the table
further down). It is worth keeping precisely because the same bracket crosses at
f\* = 51.6% there and f\* = 29.3% here: it is the denominator that moved, not
the switch.

**Where the 480 cycles go.** 33.5 of them are each of the two `msr DIT` writes
and ~11 the token read, which a tight loop over the four instructions prices at
106 cycles all in. In situ it is 4.5x that, and the extra is the speculation
barrier: dropping `sb` (the `bracketnobar` arm -- not a shippable configuration,
the barrier is what makes the mode change apply to what follows) takes 480 down
to ~300. `sb` drains what is actually in flight, and a tight loop has nothing in
it.

**IPC overhead** = unhardened IPC / arm IPC - 1, `ipc_ovh_pct` in the CSV; the
same quantity the gem5 figure plots. 15 reps per cell.

| L | f_secret | blanket | Apple's bracket | bracket's NOP twin | bracket - twin, cyc/req | bracket vs blanket | blanket on the PUBLIC lane alone |
|---|---|---|---|---|---|---|---|
| 10 | 95.9% | -1.6% | +28.2% | +0.0% | 320 | +30.8% | +22.5% |
| 30 | 84.5% | +10.8% | +35.2% | -0.5% | 479 | +22.4% | +29.9% |
| 50 | 72.8% | +17.4% | +31.8% | -0.1% | 504 | +12.7% | +30.5% |
| 100 | 51.5% | +22.6% | +22.4% | +1.2% | 480 | **+0.2%** | +33.7% |
| 200 | 32.6% | +26.0% | +12.9% | -0.2% | 480 | **-10.2%** | +33.9% |
| 400 | 18.2% | +29.9% | +7.6% | +0.3% | 461 | -17.0% | +34.3% |
| 700 | 11.1% | +32.1% | +4.8% | +0.1% | 482 | -20.6% | +34.5% |
| 1,000 | 8.1% | +32.6% | +3.1% | -0.3% | 501 | -22.1% | +34.6% |
| 2,000 | 4.1% | +33.5% | +1.7% | +0.0% | 462 | -23.8% | +34.5% |
| 5,000 | 1.6% | +34.1% | +0.7% | +0.1% | 456 | -24.9% | +34.5% |
| 20,000 | 0.4% | +34.4% | +0.2% | +0.0% | 582 | -25.4% | +34.5% |

**The NOP twin column is the point of having it.** It reads -0.5% to +1.2%
across eleven lengths, so the bracket's own restructuring of the binary is worth
about a point and the rest of its column is the mode. Experiment 09 ran this arm
without a twin and had to borrow a barrier arm for the comparison.

**Reading the last column.** Blanket's cost to the *public lane alone* is flat at
+34.5% from L=400 up: it is a property of the lane, not of the request, and the
full-flow column is just that number diluted by the secret fraction. Same
mechanism gem5 reports, arrived at on hardware.

## What one bracketed call costs

`--chunks N` splits a request into N pieces -- L/N lookups then one AEAD call
over a 100/N-byte slice under its own nonce -- so the public work is the same
lookups in N runs, the secret bytes are the same bytes in N calls, and a
per-call placement pays N times while blanket does not. `(arm - twin) / N` is
then one placement's cost with layout removed. `data/m4_per_call_cost.csv`.

| calls per request | L=200 | | L=1,000 | |
|---|---|---|---|---|
| | bracket | the pass | bracket | the pass |
| 1 | 507 | 1,334 | 476 | 1,335 |
| 2 | 463 | 1,325 | 463 | 1,320 |
| 5 | 456 | 1,339 | 463 | 1,338 |
| 10 | 455 | 1,321 | 459 | 1,327 |
| 25 | **304** | 1,261 | 492 | 1,312 |

**Apple's bracket is 455-510 cycles per call and the pass 1,260-1,340** -- a
factor of 2.7, which is the difference between two mode writes and ~40 of them.
Both are per-call, so interleaving hurts them proportionally and neither has a
fixed per-request term worth speaking of.

**It is not perfectly constant, and the exception says why.** At 25 calls per
request with L=200 there are only 8 lookups between calls and the bracket falls
to 304. `sb` costs what it has to drain, so a bracket in a program with little
in flight is cheaper than the same bracket in a busy one. A single "cycles per
bracket" number is therefore a property of the workload as much as of the part,
and no slope is fitted through these points for that reason.

## Why the canonical lane reads zero here, and what that is really saying

Run the **canonical** gem5 lane on this M4 -- the one every table below the
divider uses -- and blanket DIT costs **nothing**. On the public lane alone --
which is the whole of blanket's bill -- it reads -0.1% to +0.4% across all
eleven lengths, at every q from 0 to 1, and at every table size from 4 KB to
512 KB (`data/m4_arms.csv` lane `wide`, `data/m4_predictability_sweep.csv`,
`data/m4_tblbits_sweep.csv`). So on that lane there is no crossover at all: the
bracket costs its ~350 cycles a call and blanket costs nothing, and blanket wins
at every length.

That is not "DIT is free on Apple silicon". **It is the width of one constant.**

The lane's public side reads a record header on q of its iterations: a constant
value at a data-dependent address, the load a value predictor is meant to take
off the critical path. The canonical header is `0x2545F4914F6CDD1D`, 62 bits.
**This machine's load value predictor holds 36 bits.** Sweep the header's width
on the same lane and the answer is a step function, not a slope:

| bits in the header's value | 8 | 16 | 24 | 32 | 34 | 35 | **36** | **37** | 38 | 40 | 48 | 56 | 62 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| blanket, public lane, q=1 | +45.6 | +45.6 | +45.6 | +45.6 | +45.6 | +45.6 | **+45.6** | **+0.0** | +0.0 | +0.0 | -0.0 | +0.0 | +0.0 |
| blanket, public lane, q=¾ | +34.5 | +34.5 | +34.5 | +34.5 | +34.5 | +34.5 | **+34.5** | **-0.0** | -0.1 | +0.0 | -0.0 | -0.0 | -0.0 |

`data/m4_header_width.csv`, figure `figures/m4-predictor-width.{png,pdf}`. 13
widths, 15 reps each. Nothing in between: a header that fits in 36 bits is
predicted and one that needs 37 is not.

Give the lane a header that fits -- `HDR_CONST = 0xCAFEBABE`, one constant, no
code changed, identical instruction count -- and **the M4 tracks gem5's own q
sweep point for point** (`figures/predictability-gem5-vs-m4.{png,pdf}`):

| q | 0 | ¼ | ½ | ¾ | 1 |
|---|---|---|---|---|---|
| gem5, 62-bit header (`gem5_predictability_sweep.csv`, L=20,000) | +0.0% | +11.4% | +25.1% | +30.9% | +40.3% |
| **M4, 32-bit header** (`m4_predictability_sweep.csv`) | +0.0% | **+12.2%** | **+23.7%** | **+34.4%** | **+45.6%** |
| M4, 62-bit header — the gem5 lane | -0.0% | +0.0% | +0.0% | +0.1% | +0.0% |

**So the paper's claim survives the machine, and gains a caveat with teeth.** The
crossover is real on hardware and it is the same mechanism. But a value
predictor is a finite structure, and *which* public code loses predictions under
DIT depends on what that structure can hold. gem5's EVES/VTAGE keeps 64 bits and
predicts a hash-shaped header; Apple's keeps 36 and predicts a type tag, a
length, a small enum, a flag word, a base offset -- which is what real record
headers mostly are. A microbenchmark that picks a header value at random from
the 64-bit range will measure zero here and 31% there, and neither number is the
machine's fault.

## What this rig can and cannot say

- **Unrooted, so the thread is not pinned.** `kern.sched_thread_bind_cpu` is
  root-only. The PMCs are per-core, so a thread that migrates mid-region
  differences two cores' counters -- which shows up as a backward delta, the
  same "impossible" reading experiment 14 logged 119 of. The region is bracketed
  with `CNTVCT_EL0` and any such sample is **rejected and counted**, never
  dropped quietly: 55 rejects across the 154 crossover cells, 0-4 per cell,
  every one of them in `out/runs-*.jsonl`.
- **argv is equalised across arms, and it had to be.** The request's retired
  instruction count moves by ~5 (0.1%) with the stack alignment a process gets,
  because the AEAD's stack buffers land differently and macOS's `memcpy`
  dispatches on alignment. `--blanket` is 9 characters that only one arm passes,
  and the arms' binaries have names from 18 to 26 characters, so every arm sat
  at a different alignment: nodit read 5,137.2 instructions per request against
  blanket's 5,147.9, a systematic +10 that is not the mode, and padding nodit's
  argv to the same length reproduced blanket's number exactly. The rig now
  reaches the binaries through equal-length symlinks and pads the argument list,
  which is the instruction-count version of the argv[0] control the gem5 rig
  already applies.
- **The secret-heavy end is the soft cell**, exactly as on gem5. Blanket at
  f≈96% has read -1.6%, +1.0% and +3.9% across runs of this sweep; the public
  lane is 4% of that request, so blanket's whole effect there is 4% of a 34%
  number and layout moves it as much as the mode does. The bracket's own
  `bracket - twin` at that point reads 320 against ~480 everywhere else, for the
  `sb` reason above. **The trend and everything below f≈85% are solid; the exact
  values at f>90% are not.**
- **The page-mapping lottery.** Above 64 KB of table a run's cycles come out
  bimodal, two states ~15% apart chosen by the physical pages the kernel hands
  the BSS, identical for every arm. The canonical lane runs at 4 KB, where there
  is no split; where there is one the rig reports the fast cluster's median and
  records the split in `n_hi`. See the rig README.
- **The public lane is synthetic and q=0.75 is a chosen midpoint**, on this
  instrument exactly as on the other one.
- **One machine, one part.** M4, 4 P-cores at 4.40 GHz. The 36-bit width and the
  480-cycle bracket are this part's.

Cross-checks against what the project already measured, on the same binaries:
blanket on the AEAD alone reads **+0.00%** here against experiment 09's
**-0.54%** on M5; one serialising `msr DIT` measures **33.5 cycles** against the
cost model's **~30**, and a same-value write **11.5** against its **12**; the
pass's executed switches come out at **~40 per request** by the
`(pass - nop) / 33.5` route against the **32 committed** writes gem5 counts with
`commit.ditWrites`.

## Reproducing the silicon half

```sh
export ARMS_WORK=$HOME/Documents/libsodium-exp02-m4
export LLVM_BIN=<repo>/build/bin
utils/dit_host_screening/signed_lookup/silicon/build_silicon.sh
# ... the sweeps, then derive_exp02_m4.py and fig_exp02_silicon.py
```

About eight minutes of measurement on a quiet machine; the rig README has the
exact command lines and the reason for every gate.

---

# The gem5 half

## The flow

One request = a PUBLIC lane that walks a lookup table, then a SECRET lane that
seals the gathered record. The knob L, lookups per record, moves only the ratio.

| lane | code | what it is |
|---|---|---|
| **public** | `lookup_lane()` - L serial, value-dependent loads into a 4 KB table: hashed contents, next index from the high bits of a 64-bit state. On 3 iterations in 4 the record's header is read first: data-dependent address, constant value, the way every record's type field is the same. | a pointer chase whose critical path is 75% LVP-predictable loads |
| **secret** | `crypto_aead_chacha20poly1305_ietf_encrypt()` of a 100-byte record carrying the digest - experiment 09's op and message size | ~2.2k cycles, 32 committed switches per op under the pass |

Seeds are the callee contract's **fixpoint** for libsodium,
`benchmarks/crypto/libsodium_secret_contract.txt`, 188 seeds over 71 functions
(copied verbatim into `results/gem5/seeds_used.txt`). The published 2026-09-05
table used the **round-2** file instead - the CIO-parity set plus the first
report's 21 lines, 86 seeds - and that file no longer exists under any name:
gem5-DIT `a20a176016` wrote the fixpoint into BOTH
`libsodium_secret_contract.txt` and `libsodium_secret_contract_r2.txt`, which
are today byte-identical in seed content despite `_r2`'s name and despite the
main file's own header still describing `_r2` as "the round-2 file (86 seeds)".
The round-2 file is recoverable at gem5-DIT `7e8630384c` if that arm is wanted.
It should not matter here: the AEAD path is fully seeded by round 2 and the
oracle frontier below is at the floor with it, so the extra 102 seeds are the
signing path. That is an argument, not a measurement - the A/B has not been
run. The build carries the owned-symbols list it generates from its
own base objects, which is what lets a DIT-on caller name a twin in another
TU. The pass arm is built with `-fno-optimize-sibling-calls` (redundant since
the tail-call disable rides on `-ftaint-harden`, kept so the arm is the same
shape as before).

Three axes, each with its own cost:

- **secret fraction f** (the L sweep) sets what the switches cost: 32 per
  request (38 before the external-callee assumption, 49 before the twins) at
  20-25 cycles each under a serialising `MSR DIT`, nothing under a renamed one;
- **LVP-predictable fraction q** of the public lane sets what blanket costs:
  linear in q, fixed at 0.75 for the canonical lane, swept in
  `data/gem5_predictability_sweep.csv`;
- **switch implementation** decides whether selective placement pays for the
  first.

## Headline results

gem5 Neoverse-V2 FDP (`--eves --dmp --comp-simp`), median over 5 stack offsets.
**IPC overhead** = unhardened IPC / arm IPC - 1, from the `ipc` column of
`data/gem5_arms.csv`; positive is slower. For blanket this equals the cycles
ratio (same instruction stream); for the pass it sits up to 3 points under the
`vs_base_pct` cycles column, because its 32 switches per request are extra
instructions that run at full IPC.

| L | f_secret | blanket | pass, renamed | pass, serialising |
|---|---|---|---|---|
| 10 | 96.5% | +2.3% | +0.9% | **+25.3%** |
| 50 | 81.0% | +11.2% | -1.7% | +19.9% |
| 200 | 45.1% | +19.6% | -0.6% | +10.8% |
| 1000 | 13.5% | +27.3% | -0.4% | +3.0% |
| 5000 | 2.7% | +30.2% | +0.1% | +0.7% |
| 20000 | 0.6% | **+31.0%** | 0.0% | +0.4% |

Spread across offsets (cycles) is under 2.3% everywhere
(`data/gem5_stack_offset_spread.csv` has every offset's cycles); the blanket
cell at L=5,000 is bit-identical across offsets.

**One caveat on the secret-heavy blanket cells, and this run walked into it.**
Blanket at f=96.5% reads +2.3% here against +7.6% on 2026-09-05 and +1.1% on an
earlier layout - the same cell has now been measured at +1.1%, +2.3% and +7.3%
to +7.6% across binary path lengths, while every other cell stayed within about
a point of the run before it. Where the AEAD dominates, blanket's cost depends
on where the AEAD's stack buffers land against the predictors, and five
consecutive one-byte offsets sample one alignment class, not all of them. **The
trend and the four low-f points are solid; the exact value at f>80% is not, and
should not be quoted to two significant figures.** Fixing it needs offsets
sampled across alignment classes rather than five consecutive bytes, which has
not been done.

**Three readings:**

1. **Blanket's cost is the public lane's lost load predictions.** It climbs from
   +2% to +31% as the lane grows, and `data/gem5_value_predictor_by_arm.csv`
   shows why: unhardened makes 124 load-value predictions per request at L=10
   and 15,107 at L=20,000; the pass keeps 12 and 14,993 of them; blanket makes
   zero at every L. Nothing else DIT
   gates in this model moves: comp-simp never simplifies anything here, the DMP
   never scans (the table is L1-resident), branch mispredicts are identical.
2. **Renamed placement is free at every point**, -1.7% to +0.9% of unhardened.
   The public lane runs with DIT clear and keeps its predictions; the secret
   lane pays 32 switches that cost nothing when the mode is renamed.
3. **Serialising placement crosses blanket at f ~= 63% secret**, interpolating
   between the f=45.1% and f=81.0% points. Its cost is the switches, 20-25
   cycles each, and does not depend on q at all. Under a serialising `MSR DIT`
   selective placement therefore has a crossover of its own against blanket;
   under a renamed one it is strictly better. At f=45% the pass costs +10.8%
   against blanket's +19.6% (2026-09-05: +11.8% against +20.2%; before the
   twins, +17.7%).

The same library sits at three verdicts: alone in experiment 09, blanket wins;
here with ed25519 as the secret op, the switch model is irrelevant; here with
chacha20-poly1305, the switch model is everything. The flow and the toggle
density decide, not the library.

## Why the rig changed (2026-09-03)

Three findings, each with its data file:

- **The secret op was an ed25519 signature; it could not show the switch
  implementation.** 52 switches per 74k-cycle signature is 30x too sparse:
  serialising cost +2 pp at f=96% and vanished into layout noise below f=70%
  (`data/gem5_arms_ed25519.csv`). The AEAD op puts the flow in 09's regime.
- **The lookup chain was a constant load.** The table was linear in the index
  and the mix multiplied by 5, so the low-bit index map had an even multiplier
  and contracted to one table entry within 9 steps for every request. The
  stride predictor predicted it at 99.95% and blanket's "public-lane" cost was
  that one constant losing its predictor: +127% at f=2%
  (`data/gem5_arms_constant_chain.csv`). Hashing the table and taking the index
  from the high bits of a 64-bit state fixes it; on that pure chase blanket is
  free, +3.0% at f=97% falling to +0.0% (`data/gem5_arms_pure_hash_chase.csv`).
- **Predictability is a property, so it is set on purpose.** The M4/M5 have a
  load value predictor because real code is full of same-value loads at the same
  PC (FLOP), and DIT switches it off. The lane reads an LVP-predictable header
  on a fraction q of iterations; blanket's cost is linear in q, +0.0 / +11.2 /
  +25.1 / +31.4 / +40.5% at L=20,000 for q = 0, 0.25, 0.5, 0.75, 1
  (`data/gem5_predictability_sweep.csv`), while renamed placement stays free and
  serialising placement's cost does not move. The canonical lane fixes q=0.75.
  Where real applications sit on this axis is what FLOP's counts and experiment
  01's real public lane say; this experiment does not claim it.

The retired numbers below rest on the constant chain; the published artifact
(`figures/crossover.html`) is that era's page.

## Validity gates

All exact, all passing, checked by `run_gem5.py` on every sweep:

1. **Unhardened is bit-identical under both switch models** at every L - the
   `--no-speculative-dit` flag changes nothing but the switches.
2. **Every arm computes the same checksum** at every L - same work, different
   mode.
3. **Exactly two stats dumps per run** - the ROI is the request loop and nothing
   else.
4. **Committed switches are 32 per request at every L** for the pass and 0 for
   the others - `commit.ditWrites`, a committed count. (49 before the twins, 38
   before `-taint-dit-external-preserves` removed the re-assert after each libc
   call; the 32 that remain sit behind the Poly1305 and ChaCha20 implementation
   tables, which an indirect call cannot redirect. The bracket arm executes 2.)
5. **Five stack offsets per cell.** gem5 is deterministic but not
   layout-insensitive: the length of argv[0] alone moved the retired driver's
   L=500 cycles by -4%..+4% for both arms (`data/gem5_stack_offset_sensitivity.csv`).
   Every number here is a median over 5 argv[0] lengths and carries its spread;
   `data/gem5_stack_offset_spread.csv` records all five. The runner roots the
   binary path at a constant-length `/tmp` path so reproductions share one
   argv[0] length.

## Reproducing

One script, from the committed sources:

```sh
WORK=<a fresh dir> ./reproduce.sh    # build, sweep, derive, figures
WORK=<the same dir> ./reproduce.sh sweep derive   # just the numbers
```

`LLVM_BUILD` and `G5` default to this repo's own `build/` and `gem5-DIT`
submodule, so a worktree measures what it built. **Use a fresh `WORK`**, or set
`RESUME=1` only when you know the dir was filled by the same build:
`run_gem5.py --resume` skips on `stats.txt` existing, with no binary hash and no
compiler recorded, so resuming into an older dir silently mixes arms built weeks
apart - and the arm list itself changed on 2026-09-05.

It drives the rig in gem5-DIT (`benchmarks/signed_lookup/build_gem5_linux.sh`,
`run_gem5.py`; gem5 on an aarch64 Linux host, no sysroot, the macOS cross path
is `build.sh`), then `utils/dit_host_screening/signed_lookup/derive_exp02.py`
writes `data/` with a provenance line naming the gem5-DIT and LLVM commits, and
`fig_exp02.py` draws the gem5 figures. Four sweeps, **1,100 gem5 runs**, a ~60 s median
each; on a 160-core box at 100 concurrent processes the sweep is about 20 minutes
and the five-variant build ahead of it about as long again. `JOBS` defaults to
80% of `nproc`. gem5 is deterministic and the runner roots the
binary path at a constant-length `/tmp` path, so another machine reproduces
`data/` up to its simulator and compiler builds. The silicon half is a
separate rig with its own reproduce steps -- see "Reproducing the silicon half"
above. (gem5-DIT's own `run_crossover.py`, the M5/kperf path, is superseded by
it and has not been rerun on this lane.)

## The four arms (2026-09-05; superseded as the headline by the 2026-09-06 run above)

The same four arms as experiment 09, on the secret-fraction sweep: **base**
(`-O2` with our compiler, no seeds), **blanket** (DIT set once for the whole
process), the **Apple bracket** (the driver's AEAD entry points wrapped in
Apple's prologue and epilogue: read the previous DIT state, `msr DIT, #1`,
speculation barrier as `isb sy`, the call, clear only if it was clear; the
same `api_bracket.c` as experiment 09, interposed with the linker's
`--wrap`), and **ExpeDITe** at the defaults of the measurement (callee
contract, twins, intra-block placement, the fixpoint seeds, the owned list),
without the external-callee assumption, which became the default later the
same day: this column is `-taint-dit-external-preserves=0` in today's terms,
and "The external-callee assumption" below is what a default build now
emits. Blanket and the
bracket under the serialising `MSR DIT`, which is what an M4 or M5 does;
ExpeDITe under both. IPC overhead against base, median over five stack
offsets, 300 runs, all gates pass except the known L=10 base-model wobble
(0.3%, the DMP artifact noted in "Validity gates"). The bracket's
instruction-matched NOP twin (every DIT instruction of the wrapper a NOP at
the same address) is beside it, because it is needed to read that column.
Data: `data/gem5_arms_four_arms.csv`.

| L | f secret | base cycles/request | blanket | Apple bracket | bracket's NOP twin | bracket cost, cycles/request | ExpeDITe, serialising | ExpeDITe, renamed |
|---|---|---|---|---|---|---|---|---|
| 10 | 96.8% | 2,494 | +7.3% | -3.4% (2) | -2.2% | -30 | +31.6% (38) | -0.1% |
| 50 | 81.2% | 3,174 | +12.6% | -2.8% (2) | -1.6% | -38 | +24.9% (38) | -0.6% |
| 200 | 45.0% | 5,743 | +20.2% | -1.5% (2) | -0.7% | -44 | +13.5% (38) | +1.2% |
| 1000 | 13.1% | 19,492 | +27.7% | -0.3% (2) | -0.5% | +42 | +3.8% (38) | +0.1% |
| 5000 | 2.6% | 88,098 | +30.6% | +0.2% (2) | -0.4% | +461 | +0.8% (38) | +0.0% |
| 20000 | 0.6% | 344,432 | +31.3% | -0.2% (2) | -0.4% | +358 | +0.5% (38) | +0.2% |

**Reading the bracket column.** The bracket comes out FASTER than the
unhardened build at every length, and so does its NOP twin: the wrapper's
presence shifts the layout of everything linked after it, and on this
pointer-chasing driver that is worth 2 to 3 points at the short lengths,
more than the bracket's two serialising switches (~60 cycles, +2.4% of a
2,494-cycle request at L=10). Within that pair the real bracket is faster
than its NOP twin on both switch models, by 30 to 44 cycles per request at
L <= 200, which no cost model of two serialising writes produces; the
drains the sequence performs (`mrs DIT` and `isb`, both pipeline drains in
gem5) change how the public lookup loop behaves in the simulator by more
than the switches cost. So on this workload the rig cannot resolve the
bracket's own cost; what it can say is that a per-call bracket around an
AEAD that is 3% to 97% of the request is within the layout band of the
unhardened build, where blanket is +7% to +31% and ExpeDITe serialising is
+32% to +0.5%, falling with the secret fraction because its 38 switches per
request sit behind the Poly1305 and ChaCha20 implementation tables.

### Finer interleaving: N secret pieces per request (2026-09-05)

The request above is L public lookups then ONE secret AEAD call, so the
sequence is already public, secret, public, secret; what the sweep does not
vary is how many bracketed calls a request contains. A per-call placement
pays per call: the Apple bracket two serialising switches, ExpeDITe its
per-entry count. `--chunks N` splits each request into N pieces, L/N
lookups then one AEAD call over a 100/N-byte slice under its own nonce, so
the secret bytes are the same in N calls and the public work the same in N
runs; N=1 is the request as it always was, bit for bit. The same five
binaries, five stack offsets, 500 runs, all gates pass but the known
base-model wobble. IPC overhead against base; switches per request in
brackets; `data/gem5_arms_interleaving.csv`.
L = 200
| pieces per request | f secret | base cycles/request | blanket | Apple bracket (sw/req) | bracket NOP twin | bracket - twin, cycles/req | ExpeDITe, serialising (sw/req) | ExpeDITe, renamed |
|---|---|---|---|---|---|---|---|---|
| 1 | 45.4% | 5,799 | +19.0% | +0.8% (2) | -0.2% | +58 | +12.4% (38) | -0.4% |
| 2 | 56.2% | 6,637 | +17.5% | +2.3% (4) | +2.3% | -1 | +25.9% (76) | +0.5% |
| 5 | 81.6% | 11,279 | +10.2% | +3.7% (10) | +2.7% | +106 | +37.3% (190) | +1.1% |
| 10 | 90.3% | 18,692 | +7.9% | +4.9% (20) | +2.4% | +481 | +45.5% (380) | +1.6% |
| 25 | 96.9% | 43,349 | +2.4% | +0.5% (50) | -1.4% | +839 | +44.8% (950) | -2.1% |

**L = 1000**:

| pieces per request | f secret | base cycles/request | blanket | Apple bracket (sw/req) | bracket NOP twin | bracket - twin, cycles/req | ExpeDITe, serialising (sw/req) | ExpeDITe, renamed |
|---|---|---|---|---|---|---|---|---|
| 1 | 13.5% | 19,556 | +27.3% | -0.1% (2) | +0.0% | -9 | +3.2% (38) | -0.7% |
| 2 | 18.7% | 20,426 | +26.5% | +0.4% (4) | +0.6% | -36 | +8.1% (76) | -0.2% |
| 5 | 37.0% | 25,009 | +21.9% | +1.7% (10) | +0.8% | +221 | +16.7% (190) | +0.2% |
| 10 | 55.6% | 32,549 | +17.6% | +2.0% (20) | +0.8% | +383 | +25.0% (380) | +0.2% |
| 25 | 81.5% | 55,323 | +13.0% | +3.1% (50) | +2.9% | +104 | +37.2% (950) | +1.2% |

**Reading.** Splitting the secret work into pieces raises the secret
fraction (each AEAD call carries its fixed setup and tag cost, so 25 calls
of 4 bytes are far more crypto than one of 100), which is why blanket's
overhead FALLS with N: it is an overhead on the public lane, and the public
lane shrinks. The bracket's cost rises with N as its two switches per piece
say it should, from within noise at one piece to +3% to +5% at 10 and 25,
and its NOP twin tracks it to within the layout band, so at 25 pieces per
request a bracket around every 4-byte AEAD is still 2 to 4 points cheaper
than blanket. ExpeDITe on the serialising model rises 19 times faster, +3%
to +37% at L=1000 and +12% to +45% at L=200, because every AEAD call still
pays its 38 switches behind the Poly1305 and ChaCha20 implementation tables
(950 per request at 25 pieces, against the bracket's 50); on the renamed
model it stays within 2% throughout. So on this workload the answer to
"does interleaving hurt the bracket" is yes, by the per-call switch cost,
and it hurts a per-entry-point placement with a higher switch count per
call by the same mechanism, proportionally more.

## Rerun 2026-09-05: the compiler's new defaults

The taint clang's defaults changed to the callee contract and the DIT twins
(llvm-data-independent-timing `fa4aa84e36a0`; `docs/design/dit-cloning.md`,
`docs/reference/harden-runbook.md`). Same driver, same runner, same gates,
same 770 runs; the only changes are the pass library (contract seeds, owned
list, twins) and the compiler. Every gate passes: unhardened bit-identical
under both switch models at every L, one checksum per L across arms, two
dumps per run, 38 committed switches per request at every L, spread at most
2.28%.

| L | f_secret | blanket 09-03 -> 09-05 | pass renamed 09-03 -> 09-05 | pass serialising 09-03 -> 09-05 |
|---|---|---|---|---|
| 10 | 96.8% | +7.3% -> +7.6% | +0.2% -> -1.0% | **+41.2% -> +28.4%** |
| 50 | 81.2% | +12.6% -> +12.6% | -0.9% -> -0.5% | +32.0% -> +22.1% |
| 200 | 45.0% | +20.2% -> +20.2% | -1.0% -> -0.4% | +17.7% -> +11.8% |
| 1000 | 13.1% | +27.7% -> +27.7% | -0.3% -> +0.2% | +5.1% -> +3.2% |
| 5000 | 2.6% | +30.6% -> +30.6% | +0.2% -> +0.1% | +1.4% -> +0.7% |
| 20000 | 0.6% | +31.3% -> +31.3% | +0.4% -> +0.4% | +0.6% -> -0.1% |

What moved is the serialising column, by the switch count: 49 -> 38 per
request, a 22% cut, and the cost fell 25-35% at every point where it was
measurable. The remaining 38 are the Poly1305 and ChaCha20 implementations,
reached through function tables that an indirect call cannot redirect to a
twin; the AEAD entry, the stream call and `sodium_memzero` are the ones that
went. Blanket does not move (its library is unhardened; the +0.3 at L=10 is
the documented alignment sensitivity). Renamed placement stays within 1%.

The oracle frontier is unchanged to the operation: the pass arm protects
3,591 of 3,596 secret operations per request at every L (99.86%; 3,585 of
3,590 before), the five survivors being `main` folding the published
ciphertext into its checksum, and its wasted coverage is 1,088 (1,083
before) against blanket's 1,805 / 12,335 / 226,085. Oracle run dirs:
session scratchpad `exp02_oracle/`; compare with
`gem5-DIT/benchmarks/signed_lookup/frontier_compare.py`.

Not moved: the retired-driver data and the three frozen-evidence files.

### Narrowing twins (2026-09-05, `data/gem5_arms_twin_narrow{,0}.csv`)

The pass arm rebuilt twice with the twins narrowing (`-taint-dit-twin-narrow`;
`gem5_arms_twin_narrow0.csv` adds `-taint-dit-twin-switch-cyc=0`, DIT off at
the top of every twin whose entry holds no secret), everything else as
`gem5_arms.csv`; `docs/results/dit-twin-narrowing-2026-09-05.md`. IPC overhead
vs unhardened, renamed / serialising, switches per request in brackets:

| L | shipped twins | narrowing, default cost | narrowing, cost 0 |
|---|---|---|---|
| 10 | -1.0 / +28.4 (38) | +3.5 / +30.5 (38) | -2.0 / +33.0 (45) |
| 50 | -0.5 / +22.1 (38) | -1.2 / +23.7 (38) | -2.2 / +25.9 (45) |
| 200 | -0.4 / +11.8 (38) | +0.1 / +12.3 (38) | -1.4 / +14.0 (45) |
| 1000 | +0.2 / +3.2 (38) | +0.1 / +3.4 (38) | -0.6 / +3.9 (45) |
| 5000 | +0.1 / +0.7 (38) | -0.2 / +0.6 (38) | +0.0 / +1.0 (45) |
| 20000 | +0.4 / -0.1 (38) | +0.2 / +0.5 (38) | +0.7 / +0.5 (45) |

At the default cost the binary's switch count and coverage are the shipped
arm's and the renamed differences are layout; at cost 0 seven more switches
per request buy nothing on the renamed model and cost 2 to 5 points on the
serialising one. The twins stay whole by default.

### The external-callee assumption (2026-09-05, `data/gem5_arms_external_preserves.csv`)

The pass arm rebuilt with `-taint-dit-external-preserves`
(a callee outside the build is assumed never to write PSTATE.DIT, so no re-assert after it;
`docs/results/dit-external-preserves-2026-09-05.md`), everything else as
`gem5_arms.csv`. IPC overhead vs unhardened, renamed / serialising, switches
per request in brackets:

| L | shipped twins | + external assumption |
|---|---|---|
| 10 | -1.0 / +28.4 (38) | -2.4 / +23.4 (32) |
| 50 | -0.5 / +22.1 (38) | -0.1 / +18.0 (32) |
| 200 | -0.4 / +11.8 (38) | -1.0 / +9.9 (32) |
| 1000 | +0.2 / +3.2 (38) | -0.7 / +2.4 (32) |
| 5000 | +0.1 / +0.7 (38) | -0.1 / +1.0 (32) |
| 20000 | +0.4 / -0.1 (38) | +0.2 / +0.7 (32) |

Six of the 38 switches per request were re-asserts after glibc movers inside
the AEAD twins; the 32 that remain are the Poly1305/ChaCha20 implementation
table calls. Gates 210/210, coverage unchanged.

### Intra-block placement, the default since later on 2026-09-05 (`data/gem5_arms_intra_block{,_ext}.csv`)

Same rig, the pass arm with `-taint-dit-sub-block` (now the default; `=0`
is the block placement of every table above), without and with the
external-callee assumption. IPC overhead vs unhardened, renamed /
serialising, switches per request:

| L | block | block + ext | intra-block | intra-block + ext |
|---|---|---|---|---|
| 10 | -1.0 / +28.4 (38) | -2.1 / +23.8 (32) | +0.1 / +32.0 (38) | -1.7 / +24.8 (32) |
| 50 | -0.5 / +22.1 (38) | -0.1 / +18.0 (32) | -0.6 / +24.9 (38) | -0.1 / +19.2 (32) |
| 200 | -0.4 / +11.8 (38) | -1.0 / +9.9 (32) | +1.2 / +13.5 (38) | -1.1 / +10.0 (32) |
| 1000 | +0.2 / +3.2 (38) | -0.7 / +2.4 (32) | +0.1 / +3.8 (38) | -0.1 / +2.1 (32) |
| 5000 | +0.1 / +0.7 (38) | -0.1 / +1.0 (32) | +0.0 / +0.8 (38) | +0.0 / +0.1 (32) |
| 20000 | +0.4 / -0.1 (38) | +0.2 / +0.7 (32) | +0.2 / +0.5 (38) | +0.2 / +0.0 (32) |

Identical switch counts, gates 210/210 in both, coverage unchanged; the
differences are the layout band of a library that changed in four
functions. `docs/results/dit-intra-block-default-2026-09-05.md`.

## Known limits

- **Silicon IS now measured**, on an M4, 2026-09-08 -- see "The crossover on
  silicon" at the top. The M5 crossover further down is the retired lane's and
  is superseded by it.
- **`f_secret` is a ratio of CYCLES, on the unhardened build.**
  `f = (c - c_pub) / c`, where `c_pub` is the same binary run with `--nosecret`,
  i.e. the crypto call skipped. Not instructions, not bytes, not wall time. It
  is therefore a property of the workload and not of any placement, so every arm
  moves along one fixed axis -- and it is why the same L sits at a different f on
  the two machines: gem5 sustains IPC 1.07 at L=10 where the M4 sustains 3.89, so
  the public lane is a larger share of the model's operation. The figures label
  the axis "Secret fraction of operation cycles" for exactly this reason.
- **The figures say OPERATION where the rig says request.** The driver's
  docstring opens "One request = a PUBLIC lane that gathers records..." and the
  CSV columns are still `requests` and `cyc_per_request` -- that schema is
  published and stays. But "request" implies a server's unit of work, and this
  public lane is a dependence chain built to have the right shape rather than any
  application's real public code, so the figures do not claim it. Same unit
  either way: L value-dependent loads plus one AEAD call over the digest of what
  they gathered. Experiment 01's coin selection is the real-application public
  lane.
- **The public lane is synthetic**, and q=0.75 is a chosen midpoint, not a
  measured property of any application. Experiment 01's coin selection is the
  real-application public lane.
- **The secret op's switch count is the shipped policy's.** 38 per AEAD op;
  denser placement (the `fine` policy) would raise serialising's cost further,
  and the 38 are the AEAD's implementation-table dispatch, which no twin
  reaches (`docs/design/dit-cloning.md` §5.1).
- **The layout noise is real** and largest where the secret lane dominates; the
  spread column is part of every result.
- **The two panels ARE now the same microbenchmark** (2026-09-08), down to the
  L points, with `HDR_CONST` the one deliberate exception (see above -- a shared
  constant measures the mechanism on one machine and nothing on the other). The
  earlier figure compared AES on gem5 against chacha on the M4 and read the two
  crossovers 8 points apart; matched, they are 31 apart. **Do not cite the 52%
  against gem5's 60%** -- that pairing is not a comparison.
- **The M4's AES arms are three, not nine.** `nodit`, `blanket`, `bracket` and
  its twin -- what gem5 runs. The pass, the no-barrier split and the `dsb;isb`
  fallback are measured on the chacha lane only, so `switches_per_req` and the
  `sb`-cost decomposition are not available on the matched lane.
- **AES-256-GCM needs hardware AES.** `crypto_aead_aes256gcm_is_available()`
  returns 1 on this M4 and the driver dies loudly if it ever returns 0, so the
  arm cannot silently fall back to a different primitive.
- **`--expedite` is only near zero when the barrier is adjacent to the write.**
  gem5 drops a barrier's ordering only when it sits immediately behind the
  `msr DIT` in program order. `api_bracket.c` emits the pair from one
  `asm volatile` so the compiler cannot separate them, and
  `barrier_fused` in `data/gem5_aes_arms.csv` is the check: it must be one per
  bracket entry. It read 0 in the first AES sweep, where clang had scheduled an
  argument reload into the gap, and every `--expedite` arm paid a drain it
  should not have -- 9 to 17 points.

## Contents

| path | what |
|---|---|
| `reproduce.sh` | build, sweep, derive, figures, from the committed sources |
| `results/gem5/` | **the raw run**: one entry per run for all 1,100 runs, the runner's own CSVs, the arm switch counts, the seed file used, and the pass build's info-loss and precision reports. `data/` is the derived import of this |
| `data/m4_aes_arms.csv` | **the silicon half of the headline figure**: the AES-256-GCM 64 B secret lane, gem5's own eight L points, four arms, 11 reps. The matched panel |
| `data/m4_arms.csv` | the chacha20-poly1305 lane: 11 L x 7 arms x 2 lanes on an Apple M4 -- blanket, Apple's bracket and its NOP twin, the bracket without its barrier, the pass and its twin -- full flow and public lane alone, 15 reps |
| `data/m4_per_call_cost.csv` | what ONE placement costs per call, from the `--chunks` sweep: Apple's bracket 455-510 cycles, the pass 1,260-1,340 |
| `data/m4_header_width.csv` | the 36-bit step: what width of constant this machine's value predictor holds |
| `data/gem5_aes_arms.csv` | **the gem5 half of the headline figure**: the AES-256-GCM secret lane under `--apple` and `--expedite`, with `bracket_cyc_per_req`, `barrier_fused` and `rename_serializing` beside every cell |
| `data/gem5_apple_arms.csv` | the same arms on the chacha lane, where the bracket never rises above the model's layout noise. Kept because that null result is why the op changed |
| `data/m4_predictability_sweep.csv` | q = 0..1 on both lanes, at L=200 and L=20,000 |
| `data/m4_tblbits_sweep.csv` | 4 KB to 512 KB: the wide lane's zero is not an L1-residency artifact |
| `figures/crossover-gem5-vs-m4.{png,pdf}` | **the headline**: the crossover on both machines, side by side, one file |
| `figures/crossover-panel-{a-m4,b-gem5,legend}.{pdf,png}` | the same pair as three separate files, for a LaTeX side-by-side: no titles, no subtitles, no in-panel legend. Silicon is (a) |
| arm names on the figures | **Coarse**, **Fine**, **ExpeDITe** -- `blanket` -> Coarse, the bracket under the baseline flush-after switch -> Fine, and the same placement under the renamed switch -> ExpeDITe. The CSV columns keep the rig's own names (`blanket`, `bracket`/`api`, `model=expedite`); the mapping lives in `fig_exp02_silicon.py`'s `ARM` table |
| `figures/latex/crossover.tex` | the `figure*` that places them, with the caption. `\input` it into a paper; needs `graphicx` and `subcaption`. Its `\includegraphics` paths are relative to THIS directory (`figures/...`), not to `latex/` |
| `figures/latex/crossover_standalone.tex` | the compile check: renders the float alone in a USENIX-shaped document, so a broken include or an overfull box surfaces here and not in the paper. `cd figures/latex && tectonic -X compile crossover_standalone.tex` |
| `figures/latex/overleaf/` | **to just LOOK at the figure**: `bash overleaf/make_zip.sh` builds a flat, self-contained zip (`main.tex` + the three panel PDFs, no subdirectories). Overleaf: New Project -> Upload Project -> the zip, set `main.tex` as the main document, compile. The PDFs are not committed there on purpose -- the script copies the real ones, so the bundle cannot show stale panels |
| `figures/predictability-gem5-vs-m4.{png,pdf}` | blanket's public-lane cost vs q, both machines and both lanes |
| `figures/m4-predictor-width.{png,pdf}` | the step function at 36 bits |
| `utils/dit_host_screening/signed_lookup/silicon/` (repo root) | the silicon rig and its README |
| `data/gem5_arms.csv` | **canonical**: 6 L x 4 arms, both switch models, median of 5 offsets |
| `data/gem5_value_predictor_by_arm.csv` | what the value predictor did under each arm, per L (figure 2's input) |
| `data/gem5_value_predictor.csv` | public lane alone, predictor totals with and without DIT |
| `data/gem5_stack_offset_spread.csv` | the cycles at each of the 5 offsets behind every median |
| `data/gem5_predictability_sweep.csv` | q = 0..1 at L=200 and 20,000 |
| `data/gem5_arms_q50.csv` | full L sweep at q=0.5 |
| `data/gem5_arms_pure_hash_chase.csv` | q=0: blanket free on the public lane |
| `data/gem5_arms_constant_chain.csv` | **frozen evidence**, bug era: the collapsing chain, +127%; its driver was never committed |
| `data/gem5_arms_ed25519.csv` | **frozen evidence**: the retired secret op on the collapsing chain, switch model irrelevant; its driver was never committed |
| `data/gem5_stack_offset_sensitivity.csv` | **frozen evidence**: argv[0] length vs cycles on the retired driver, the reason for 5 offsets |
| `data/retired-signing-driver/` | the 2026-08-31/09-01 silicon and gem5 data for the retired driver |
| `figures/overhead-vs-secret-fraction.{png,pdf}` | figure 1: IPC overhead vs secret fraction, three arms (the bracket arms are measured and kept in `results/gem5/`, but are experiment 09's comparison, not this one's) |
| `figures/predictions-suppressed-vs-L.{png,pdf}` | figure 2: load-value predictions per request under each arm; blanket makes none, the pass keeps the public lane's |
| `figures/crossover.html` | the retired driver's published artifact |
| `utils/dit_host_screening/signed_lookup/fig_exp02.py` (repo root) | regenerates both figures from `data/` |

---

# Retired signing driver (measured 2026-08-31 to 09-01)

> Everything below is the experiment as it stood before 2026-09-03: ed25519
> `crypto_sign_ed25519` called directly, the lookup chain that collapses to one
> table entry. Kept as history. Its data is in `data/retired-signing-driver/`.

**Status: complete on both instruments.** Measured 2026-08-31/09-01 on Apple M5
(Mac17,2) and gem5 Neoverse-V2 FDP.

**Published artifact:** https://claude.ai/code/artifact/52f2f6b1-8324-4907-a6d0-a3548558a895
Source: `figures/crossover.html`. To update the page, republish **that URL**
(`Artifact` with `url=...`); publishing the file without the URL creates a
second artifact instead of updating this one.

---

## The claim

> The same claim as experiment 01, approached from the opposite direction. There
> the public lane is fixed and the secret lane grows, so blanket is flat and the
> pass's toggle bill climbs - the crossover is met as f RISES. Here the secret
> lane is fixed at one signature and the public lane grows, so **the pass is
> roughly flat and blanket's cost scales with the public lane** - the crossover
> is met as f FALLS.

Two workloads, opposite parameterisations, both finding a crossover.

## Headline results

| quantity | value |
|---|---|
| crossover, silicon | **f* = 59%** |
| pass beats blanket, low f | **-21.07%** at f = 4.0% |
| blanket beats pass, high f | +6.11% at f = 88.2% |
| blanket costs, silicon | +0.78% at f=88% -> **+32.64%** at f=4% |
| blanket costs, gem5 | +6.07% -> **+107.76%** (same shape, ~3.3x inflated) |
| pass vs blanket, gem5 | wins at **every** point: -2.9%, -26.3%, -44.3%, -51.8% |
| switch model, pass | serialising costs only **+0.38 to +1.54 pp** over renamed |

**The mechanism, measured**: blanket does not get more expensive. It is flat at
**~2.06 ns per lookup at every L**; the no-DIT baseline falls from 1.96 to
**1.58 ns**. One lookup costs ~2.06 ns when it must execute serially - each
address depends on the previous load - and the only escape is to break the
dependence, which value prediction does. gem5 confirms it on the counter silicon
cannot expose: **3,959,291 predictions -> 59**, IPC halved.

## What is public and what is secret

| lane | code | why |
|---|---|---|
| **public** | `lookup_lane()` - L value-dependent loads into a hot 4 KB table, chained so each address depends on the last | never touches the key; a serial value-dependent chain, the same shape as coin selection in experiment 01 |
| **secret** | `crypto_sign()` - one ed25519 signature over the gathered digest, through libsodium's public wrapper | operates on the private key |

**Through the public `crypto_sign` wrapper, which needs the tail-call disable.**
Built normally that wrapper is a two-instruction forwarder which enables DIT and
TAIL-CALLS the implementation; a tail call has no epilogue, so the mode is never
cleared and **100% of the public lane ran protected** (measured `pub_dit=1.000`).
The pass arm was byte-for-byte blanket. The pass and nop arms are therefore
built with `-fno-optimize-sibling-calls`, which gives the wrapper a real return:
in that build it is `msr DIT,#1 / bl crypto_sign_ed25519 / msr DIT,#1 /
msr DIT,#0 / ret`, and the info-loss report carries no `leak-tailcall` record.

**The numbers on this page were measured with the driver calling
`crypto_sign_ed25519` directly**, the workaround used before the tail-call
disable existed. The driver moved to the wrapper on 2026-09-02 and has not been
re-measured since; the difference is one frame and two executed switches per
request, well inside the run-to-run spread, but it is a different binary.

Seeds are `benchmarks/signed_lookup/seed.txt` in the gem5-DIT tree: the
project's CIO-parity set plus five lines one layer deeper, taken verbatim from
`-taint-info-loss-report`. That took `ref10/sign.c` from **0 to 24** switches and
SHA-512 from **0 to 14**; the loop reaches a fixpoint in one round.

## The five quantities

| | value |
|---|---|
| `f_secret` | 88.2% / 64.1% / 41.1% / 13.9% / 4.0% (measured per point via `--nosign`) |
| `C_public` | +0.15% at L=500 rising to **+30.60%** at L=60,000 (`data/retired-signing-driver/public_lane_penalty.csv`) |
| `C_secret` | ~0. Full-flow blanket tracks `C_public` to within 0.6-2 pp at every point |
| work per region | 7 instructions per lookup; ~211,000 per signature (gem5) |
| toggles per unit work | **exactly 49 committed DIT writes per signature**, constant in L |

That last number is why the switch model barely matters here, and it is a
*committed* count rather than a static one - see `commit.ditWrites` in gem5,
added by this study.

## The coverage/cost frontier (added 2026-09-03)

Experiment 10 asked what selective placement leaves unprotected and found that
on a crypto-heavy flow it cannot reach blanket's coverage at any setting. The
same gem5 shadow-taint oracle was then pointed at THIS flow, which has a real
public lane, and the answer inverts. Seed: the AEAD key. Per request, from
`(iter=4) - (iter=0)`; `f_secret` derived from the blanket arm, where
protected + wasted is every executed operation. Data:
`data/oracle_frontier.csv`.

| L | `f_secret` | pass uncovered | pass wasted | blanket wasted | blanket / pass |
|---|---|---|---|---|---|
| 64 | 66.54% | 4 | 1,083 | 1,805 | 1.7x |
| 1,000 | 22.54% | 4 | 1,083 | 12,335 | 11.4x |
| 20,000 | **1.56%** | **4** | **1,083** | **226,085** | **208.8x** |

**The pass's over-protection is constant in the public lane; blanket's is
linear in it.** The pass covers the secret lane and nothing else, so its cost
does not change when the public lane grows 350x. Blanket covers everything, so
its waste *is* the public lane. That is the whole cost argument in one table,
and it is an instruction count rather than a timing measurement, so no layout
or `argv[0]` artifact can touch it.

**On this flow the pass is also effectively sound**: 3,585 of 3,589 secret
operations per request protected (99.89%), and the four survivors are `main`
folding the published AEAD ciphertext into its checksum - a declassification
point, exactly like the 40 survivors experiment 04 reports at the signature.

**The contrast with experiment 10 is the decision rule.** There, on a pure-PSK
TLS resumption that is 65% secret by instruction count, blanket wastes only
1.34x what the pass does and the pass cannot get below 2,495 genuinely
uncovered operations at any configuration - so blanket is the right answer and
experiment 10 says so. Here, at f = 1.56%, blanket costs 209x the
over-protection to buy back four operations that are published anyway. The
framework's Q1 ("is blanket already free?") decides which regime you are in,
and both regimes now have a measured example.

Reproduce: `benchmarks/signed_lookup/signed_lookup_gem5.c` carries the oracle
hooks under `-DTAINT_ORACLE` (inert otherwise, so the arms above are
unaffected); build libsodium with `build_native_sodium.sh base pass`, link with
`-DGEM5_BUILD -DTAINT_ORACLE`, and run under
`configs/example/arm/fdp_neoverse_v2_binary.py --eves --dmp --comp-simp`.

## Validity gates

All fatal, all passing.

1. **Blanket costs something** - the framework's first question, answered
   natively before any gem5 time was spent. Up to +11.92% on the public lane.
2. **Counters armed** - a run reporting `ipc=na` aborts rather than silently
   reporting timing as IPC.
3. **Each arm ran in the mode it claims** - the binary reads `PSTATE.DIT` back.
   `sudo` sanitises `DYLD_*`, so blanket sets the bit in-process; the library
   injection would have become a second baseline.
4. **Retired instructions match** between arms, +/-0.01%.
5. **Arm order rotates every rep** - a fixed order lets drift look like an effect.
6. **`pub_dit` = 0 for every arm but blanket** - the gate that caught the leak
   above. Without it the pass arm is blanket in disguise and every number looks
   plausible.
7. **Layout separated from DIT** - the `nop` arm emits every switch as `HINT #0`
   at the same address. Measured -0.92% to +0.67%: not the effect.

## Reproducing

Rigs live with the harness, in the **gem5-DIT** tree under
`benchmarks/signed_lookup/`.

Silicon. **Requires an exclusive machine**, and root for kperf.

```sh
benchmarks/signed_lookup/build_native_sodium.sh base   # and pass, nop
benchmarks/signed_lookup/build_native.sh
sudo python3 benchmarks/signed_lookup/run_crossover.py     # TIME_ONLY=1 to skip kperf
```

gem5:

```sh
SEEDS=$PWD/benchmarks/signed_lookup/seed.txt ./benchmarks/crypto/build_libsodium.sh base
SEEDS=$PWD/benchmarks/signed_lookup/seed.txt EXTRA_CFLAGS="-fno-optimize-sibling-calls" \
  ./benchmarks/crypto/build_libsodium.sh taint
benchmarks/signed_lookup/build.sh base blanket taint
```

## Known limits

- **The public lane is synthetic.** A dependence chain built to have the right
  shape, not a real application's public code the way coin selection is in 01.
  The mechanism reads cleanly; the absolute numbers transfer less well.
- **The two instruments sit at different points on the f axis.** gem5 reads
  f=17.3% at L=60,000 where silicon reads 4.0%, because the machines weight the
  lanes differently. `f* = 59%` (silicon) and "no crossover below 95.7%" (gem5)
  **cannot be compared** until one is swept to match the other's f.
- **They also disagree in kind at high f**, not only in magnitude: silicon has the
  pass losing, gem5 has it winning, because gem5's blanket costs 6.07% even when
  95.7% of the work is signing. Its model finds prediction opportunity inside
  ed25519 that the M5 does not.
- **The driver changed after the measurement.** It now calls `crypto_sign`
  rather than `crypto_sign_ed25519` (see above); the tables were taken with the
  direct call. Rerun `run_crossover.py` before citing them against the current
  rig.
- **The secret lane is seeded one layer deep.** The report names the next wall
  (`crypto_hash_sha512`, the `ge25519_*` group). Going further widens placement
  inside signing; where to stop is a choice, not an oversight.
- **More precise seeding made the pass slower**, +3.27 pp at f=88%, and moved f*
  from 66% to 59%. Real placement inside SHA-512 and the curve arithmetic means
  toggles inside the secret lane. Following the report is not free.

## Contents

| path | what |
|---|---|
| `data/retired-signing-driver/silicon_crossover.csv` | 4 M5 runs x 5 knob points, with and without the tail-call disable |
| `data/retired-signing-driver/gem5_arms.csv` | 4 arms x 4 knob points, both switch models, committed DIT writes |
| `data/retired-signing-driver/public_lane_penalty.csv` | `C_public` and the per-lookup normalisation |
| `data/retired-signing-driver/gem5_value_predictor.csv` | the mechanism: predictions with and without DIT |
| `data/oracle_frontier.csv` | **the coverage/cost frontier**: oracle under-taint and over-protection per request at three secret fractions |
| `figures/crossover.html` | source of the published artifact above |
