# 02 - libsodium signed lookup

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
| **secret** | `crypto_sign_ed25519()` - one ed25519 signature over the gathered digest | operates on the private key |

**Not the public `crypto_sign` wrapper, deliberately.** That is a
two-instruction forwarder which enables DIT and TAIL-CALLS the implementation;
a tail call has no epilogue, so the mode is never cleared and **100% of the
public lane ran protected** (measured `pub_dit=1.000`). The pass arm was
byte-for-byte blanket. `crypto_sign_ed25519` does real work after its own inner
call, so it returns normally and placement can clear there.

Seeds are `benchmarks/signed_lookup/seed.txt` in the gem5-DIT tree: the
project's CIO-parity set plus five lines one layer deeper, taken verbatim from
`-taint-info-loss-report`. That took `ref10/sign.c` from **0 to 24** switches and
SHA-512 from **0 to 14**; the loop reaches a fixpoint in one round.

## The five quantities

| | value |
|---|---|
| `f_secret` | 88.2% / 64.1% / 41.1% / 13.9% / 4.0% (measured per point via `--nosign`) |
| `C_public` | +0.15% at L=500 rising to **+30.60%** at L=60,000 (`data/public_lane_penalty.csv`) |
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
- **The secret lane is seeded one layer deep.** The report names the next wall
  (`crypto_hash_sha512`, the `ge25519_*` group). Going further widens placement
  inside signing; where to stop is a choice, not an oversight.
- **More precise seeding made the pass slower**, +3.27 pp at f=88%, and moved f*
  from 66% to 59%. Real placement inside SHA-512 and the curve arithmetic means
  toggles inside the secret lane. Following the report is not free.

## Contents

| path | what |
|---|---|
| `data/silicon_crossover.csv` | 4 M5 runs x 5 knob points, with and without the tail-call disable |
| `data/gem5_arms.csv` | 4 arms x 4 knob points, both switch models, committed DIT writes |
| `data/public_lane_penalty.csv` | `C_public` and the per-lookup normalisation |
| `data/gem5_value_predictor.csv` | the mechanism: predictions with and without DIT |
| `figures/crossover.html` | source of the published artifact above |
