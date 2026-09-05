# 02 - libsodium signed lookup

**Status: gem5 complete, re-run 2026-09-05 on the compiler's new defaults
(the callee contract and the DIT twins, `docs/design/dit-cloning.md`); silicon
not yet re-measured.** The numbers below are that run; the 2026-09-03 run on the
previous compiler is under "Rerun 2026-09-05" for comparison.
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

Seeds are the callee contract's round-2 file for libsodium
(`benchmarks/crypto/libsodium_secret_contract.txt` as the gem5 tree carried
it on 2026-09-05: the CIO-parity set plus the first report's 21 lines, 86
seeds; the round-11 fixpoint of 188 replaced it in gem5-DIT PR #101 and is
`libsodium_secret_contract.txt` now, the round-2 file being `_r2`). The
AEAD path is fully seeded by round 2, and the oracle frontier below confirms
it at the floor, so the numbers stand. The build carries the owned-symbols list it generates from its
own base objects, which is what lets a DIT-on caller name a twin in another
TU. The pass arm is built with `-fno-optimize-sibling-calls` (redundant since
the tail-call disable rides on `-ftaint-harden`, kept so the arm is the same
shape as before).

Three axes, each with its own cost:

- **secret fraction f** (the L sweep) sets what the switches cost: 38 per
  request (49 before the twins) at 20-25 cycles each under a serialising
  `MSR DIT`, nothing under a renamed one;
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
`vs_base_pct` cycles column, because its 38 switches per request are extra
instructions that run at full IPC.

| L | f_secret | blanket | pass, renamed | pass, serialising |
|---|---|---|---|---|
| 10 | 96.8% | +7.6% | -1.0% | **+28.4%** |
| 50 | 81.2% | +12.6% | -0.5% | +22.1% |
| 200 | 45.0% | +20.2% | -0.4% | +11.8% |
| 1000 | 13.1% | +27.7% | +0.2% | +3.2% |
| 5000 | 2.6% | +30.6% | +0.1% | +0.7% |
| 20000 | 0.6% | **+31.3%** | +0.4% | -0.1% |

Spread across offsets (cycles) is under 2.3% everywhere
(`data/gem5_stack_offset_spread.csv` has every offset's cycles); the blanket
cell at L=20,000 is bit-identical across offsets.

**One caveat on the two secret-heavy blanket cells.** Reproducing this table
from a binary path of a different constant length moved blanket at f=97% from
+1.1% to +7.3% and at f=81% from +9.0% to +12.6%, while every other cell stayed
within a point. Where the AEAD dominates, blanket's cost depends on where the
AEAD's stack buffers land against the predictors, and five consecutive one-byte
offsets sample one alignment class, not all of them. The trend and the other
four points do not depend on it; the exact value at f>80% does.

**Three readings:**

1. **Blanket's cost is the public lane's lost load predictions.** It climbs from
   +7% to +31% as the lane grows, and `data/gem5_value_predictor_by_arm.csv`
   shows why: unhardened makes 113 load-value predictions per request at L=10
   and 15,092 at L=20,000, blanket makes zero at every L. Nothing else DIT
   gates in this model moves: comp-simp never simplifies anything here, the DMP
   never scans (the table is L1-resident), branch mispredicts are identical.
2. **Renamed placement is free at every point**, within 1% of unhardened. The
   public lane runs with DIT clear and keeps every prediction; the secret lane
   pays 38 switches that cost nothing when the mode is renamed.
3. **Serialising placement crosses blanket between 45% and 81% secret.** Its
   cost is the switches, 20-25 cycles each, and does not depend on q at all.
   Under a serialising `MSR DIT` selective placement therefore has a crossover
   of its own against blanket; under a renamed one it is strictly better. The
   twins moved the crossover up from "near 50%": at f=45% the pass now costs
   +11.8% against blanket's +20.2% where before it was +17.7%.

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
4. **Committed switches are 38 per request at every L** for the pass and 0 for
   the others - `commit.ditWrites`, a committed count. (49 before the twins;
   the 38 that remain sit behind the Poly1305 and ChaCha20 implementation
   tables, which an indirect call cannot redirect.)
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
export LLVM_BUILD=~/Documents/llvm-data-independent-timing/build   # the taint clang
./reproduce.sh              # build, sweep, derive, figures
./reproduce.sh sweep derive # just the numbers
```

It drives the rig in gem5-DIT (`benchmarks/signed_lookup/build_gem5_linux.sh`,
`run_gem5.py`; gem5 on an aarch64 Linux host, no sysroot, the macOS cross path
is `build.sh`), then `utils/dit_host_screening/signed_lookup/derive_exp02.py`
writes `data/` with a provenance line naming the gem5-DIT and LLVM commits, and
`fig_exp02.py` draws `figures/`. Four sweeps, 770 gem5 runs, about 25 minutes at
150 processes on a 160-core box. gem5 is deterministic and the runner roots the
binary path at a constant-length `/tmp` path, so another machine reproduces
`data/` up to its simulator and compiler builds. Silicon (`run_crossover.py`,
M5, root for kperf) has not been rerun on this lane.

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

### The libc model (2026-09-05, `data/gem5_arms_preserving.csv`)

The pass arm rebuilt with `-taint-dit-preserving-symbols=utils/dit_preserving_libc.txt`
(external functions that never write PSTATE.DIT get no re-assert after them;
`docs/results/dit-preserving-symbols-2026-09-05.md`), everything else as
`gem5_arms.csv`. IPC overhead vs unhardened, renamed / serialising, switches
per request in brackets:

| L | shipped twins | + libc model |
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

## Known limits

- **Silicon is not re-measured.** The M5 crossover below is the retired lane's.
- **The public lane is synthetic**, and q=0.75 is a chosen midpoint, not a
  measured property of any application. Experiment 01's coin selection is the
  real-application public lane.
- **The secret op's switch count is the shipped policy's.** 38 per AEAD op;
  denser placement (the `fine` policy) would raise serialising's cost further,
  and the 38 are the AEAD's implementation-table dispatch, which no twin
  reaches (`docs/design/dit-cloning.md` §5.1).
- **The layout noise is real** and largest where the secret lane dominates; the
  spread column is part of every result.

## Contents

| path | what |
|---|---|
| `reproduce.sh` | build, sweep, derive, figures, from the committed sources |
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
