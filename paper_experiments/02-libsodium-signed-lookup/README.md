# 02 - libsodium signed lookup

**Status: gem5 complete on the current rig (2026-09-03); silicon not yet re-measured.**
The rig changed on 2026-09-03 (see "Why the rig changed"); everything measured
before that is under "Retired signing driver" at the bottom and must not be cited.

**Figures:** `figures/overhead-vs-secret-fraction.{png,pdf}` and
`figures/predictions-suppressed-vs-L.{png,pdf}`, regenerated from `data/` by
`utils/dit_host_screening/signed_lookup/fig_exp02.py`.

---

## The flow

One request = a PUBLIC lane that walks a lookup table, then a SECRET lane that
seals the gathered record. The knob L, lookups per record, moves only the ratio.

| lane | code | what it is |
|---|---|---|
| **public** | `lookup_lane()` - L serial, value-dependent loads into a 4 KB table: hashed contents, next index from the high bits of a 64-bit state. On 3 iterations in 4 the record's header is read first: data-dependent address, constant value, the way every record's type field is the same. | a pointer chase whose critical path is 75% LVP-predictable loads |
| **secret** | `crypto_aead_chacha20poly1305_ietf_encrypt()` of a 100-byte record carrying the digest - experiment 09's op and message size | ~2.2k cycles, 49 committed switches per op under the pass |

Seeds are the CIO-parity set verbatim (`benchmarks/crypto/libsodium_secret.txt`,
with the chacha rename patch applied). The pass and nop arms are built with
`-fno-optimize-sibling-calls` so no tail call can leak the mode.

Three axes, each with its own cost:

- **secret fraction f** (the L sweep) sets what the switches cost: 49 per
  request at 20-25 cycles each under a serialising `MSR DIT`, nothing under a
  renamed one;
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
`vs_base_pct` cycles column, because its 49 switches per request are extra
instructions that run at full IPC.

| L | f_secret | blanket | pass, renamed | pass, serialising |
|---|---|---|---|---|
| 10 | 96.9% | +1.1% | +0.3% | **+41.6%** |
| 50 | 81.5% | +9.0% | +0.6% | +32.3% |
| 200 | 44.9% | +19.1% | -1.6% | +17.3% |
| 1000 | 13.5% | +27.5% | +0.1% | +5.2% |
| 5000 | 3.2% | +30.4% | -0.1% | +1.3% |
| 20000 | 0.8% | **+30.8%** | +0.0% | +0.5% |

Spread across offsets (cycles) is under 2.7% everywhere; the blanket cells at
large L are bit-identical across offsets.

**Three readings:**

1. **Blanket's cost is the public lane's lost load predictions.** It climbs from
   +1% to +31% as the lane grows, and `data/gem5_value_predictor_by_arm.csv`
   shows why: unhardened makes 120 load-value predictions per request at L=10
   and 15,096 at L=20,000, blanket makes zero at every L. Nothing else DIT
   gates in this model moves: comp-simp never simplifies anything here, the DMP
   never scans (the table is L1-resident), branch mispredicts are identical.
2. **Renamed placement is free at every point**, within 1.6% of unhardened. The
   public lane runs with DIT clear and keeps every prediction; the secret lane
   pays 49 switches that cost nothing when the mode is renamed.
3. **Serialising placement crosses blanket just under 45% secret.** Its cost is the
   switches, 20-25 cycles each, and does not depend on q at all. Under a
   serialising `MSR DIT` selective placement therefore has a crossover of its
   own against blanket; under a renamed one it is strictly better.

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
  free, +0.5% to -0.0% (`data/gem5_arms_pure_hash_chase.csv`).
- **Predictability is a property, so it is set on purpose.** The M4/M5 have a
  load value predictor because real code is full of same-value loads at the same
  PC (FLOP), and DIT switches it off. The lane reads an LVP-predictable header
  on a fraction q of iterations; blanket's cost is linear in q, +0.0 / +11.6 /
  +25.0 / +30.8 / +42.5% at L=20,000 for q = 0, 0.25, 0.5, 0.75, 1
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
4. **Committed switches are 49 per request at every L** for the pass and 0 for
   the others - `commit.ditWrites`, a committed count.
5. **Five stack offsets per cell.** gem5 is deterministic but not
   layout-insensitive: the length of argv[0] alone moved the retired driver's
   L=500 cycles by -4%..+4% for both arms (`data/gem5_stack_offset_sensitivity.csv`).
   Every number here is a median over 5 argv[0] lengths and carries its spread.

## Reproducing

Rig: `benchmarks/signed_lookup/` in gem5-DIT. gem5 on an aarch64 Linux host,
no sysroot (the macOS cross path is `build.sh`):

```sh
export LLVM_BUILD=~/Documents/llvm-data-independent-timing/build
benchmarks/signed_lookup/build_gem5_linux.sh                 # bin/gem5_{base,blanket,taint}
python3 benchmarks/signed_lookup/run_gem5.py --offsets 5     # -> $WORK/gem5_arms_off5.csv = data/gem5_arms.csv
python3 benchmarks/signed_lookup/run_gem5.py --offsets 5 --pred 0,1,2,3,4 --L 200,20000   # the q sweep
```

150 gem5 processes at once on a 160-core box; the canonical sweep is 210 runs
and takes about 6 minutes. Silicon (`run_crossover.py`, M5, root for kperf) has
not been rerun on this lane.

Figures: `/tmp/mplvenv/bin/python utils/dit_host_screening/signed_lookup/fig_exp02.py`
(needs matplotlib; see the script header for the venv).

## Known limits

- **Silicon is not re-measured.** The M5 crossover below is the retired lane's.
- **The public lane is synthetic**, and q=0.75 is a chosen midpoint, not a
  measured property of any application. Experiment 01's coin selection is the
  real-application public lane.
- **The secret op's switch count is the shipped policy's.** 49 per AEAD op;
  denser placement (the `fine` policy) would raise serialising's cost further.
- **The layout noise is real** and largest where the secret lane dominates; the
  spread column is part of every result.

## Contents

| path | what |
|---|---|
| `data/gem5_arms.csv` | **canonical**: 6 L x 4 arms, both switch models, median of 5 offsets |
| `data/gem5_value_predictor_by_arm.csv` | what the value predictor did under each arm, per L (figure 2's input) |
| `data/gem5_value_predictor.csv` | public lane alone, predictor totals with and without DIT |
| `data/gem5_predictability_sweep.csv` | q = 0..1 at L=200 and 20,000 |
| `data/gem5_arms_q50.csv` | full L sweep at q=0.5, before the lane was fixed at 0.75 |
| `data/gem5_arms_pure_hash_chase.csv` | q=0: blanket free on the public lane |
| `data/gem5_arms_constant_chain.csv` | bug era: the collapsing chain, +127% |
| `data/gem5_arms_ed25519.csv` | retired secret op on the collapsing chain: switch model irrelevant |
| `data/gem5_stack_offset_sensitivity.csv` | argv[0] length vs cycles: the reason for 5 offsets |
| `data/retired-signing-driver/` | the 2026-08-31/09-01 silicon and gem5 data for the retired driver |
| `figures/overhead-vs-secret-fraction.{png,pdf}` | figure 1: IPC overhead vs secret fraction, three arms |
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
| `figures/crossover.html` | source of the published artifact above |
