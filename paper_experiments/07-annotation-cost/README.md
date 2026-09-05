# 07 - annotation cost

> **Compiler note (audit 2026-09-05).** The numbers here were measured under the
> defaults of their date: the inherit contract, no DIT twins, block placement. The
> shipped defaults changed on 2026-09-05 (callee contract, twins, intra-block
> placement; `docs/reference/harden-runbook.md`), and the pre-contract seed files
> protect nothing under them. See the status table in `paper_experiments/README.md`
> before re-running.

**Status: complete on silicon.** Measured 2026-08-31/09-01, libsodium 1.0.21 on
Apple M5, with SQLCipher as a second data point.

**Published artifact:** https://claude.ai/code/artifact/ac6058f5-25ba-4a38-bf2e-6a385652ffb3
Source: `figures/annotation.html`. To update the page, republish **that URL**
(`Artifact` with `url=...`); publishing the file without it creates a second
artifact instead of updating this one.

---

## The claim

> Every other experiment measures what the pass costs the *machine*. This one
> measures what it costs the *developer* - and finds that the two are in tension.
>
> The compiler can tell you exactly where it lost the secret and what to paste to
> fix it. Following that to a fixpoint on libsodium's signing path takes **one
> round and five seed lines**, and takes SHA-512 from **0 to 14** switches.
>
> **And it makes the pass slower.** +3.27 points at high secret fraction, moving
> the crossover from f*=66% to f*=59%. Precision is not free, and the instinct
> that following the report can only help is wrong.

## The loop

`-taint-info-loss-report` emits one record per site where the analysis lost the
secret, with four fields - where, why, **what it cost**, and **the annotation
that repairs it**, pasteable:

```
taint-stop cross-tu  in=crypto_sign src=crypto_sign.c callee=crypto_sign_ed25519
  severity  moderate
  cost      no placement happens inside the callee - it runs entirely protected
  repair    seed the TU that defines it:
              crypto_sign_ed25519,4,pointee
```

| round | seeds | added | library switches | `ref10/sign.c` | SHA-512 | `ge25519_*` | new repairs offered |
|---|---|---|---|---|---|---|---|
| 0 | 68 | - | 146 | **0** | **0** | **0** | 5 |
| 1 | 73 | +5 | 164 | **26** | **14** | **4** | 0 |
| 2 | 73 | 0 | 164 | 26 | 14 | 4 | **fixpoint** |

One round. It converges immediately because SHA-512 is self-contained and
libsodium's `ref10` is one large amalgamated TU, so neither emits any escape
records of its own. Before round 1 the entire signing operation ran under DIT
inherited from a forwarder, with **no placement inside it at all**.

## The cost of that precision

From experiment 02's crossover, re-run against both seed sets:

| f_secret | secret lane **inherited** | secret lane **placed** | change |
|---|---|---|---|
| 88.2% | +2.96% | **+6.23%** | +3.27 |
| 64.1% | -0.24% | **+1.03%** | +1.27 |
| 41.1% | -5.60% | -4.06% | +1.54 |
| 13.2% | -8.64% | **-13.51%** | **-4.87** |
| 4.4% | -21.70% | -21.46% | +0.24 |

Real placement inside SHA-512 and the curve arithmetic means **toggles inside the
secret lane**. Where signing is 88% of the work those are pure added cost and the
pass's bill more than doubles. **f\* moves 66% -> 59%.** The one band it helps is
the middle-low, where it gains 4.87 points.

So the annotation axis is not monotone. More faithful analysis is more expensive,
and where to stop is a judgement about the workload, not a bug.

## What the developer has to read

| state | total records | SEVERE | actionable cross-TU | unseedable indirect | already-seeded noise |
|---|---|---|---|---|---|
| before per-argument suppression | 41 | 7 | 10 | 15 | **9** |
| after suppression | 32 | 7 | 10 | 15 | 0 |
| after also disabling tail calls | **19** | **0** | 9 | 10 | 0 |

**Nine of the original 41 records told the user to seed a callee they had already
seeded** - on every rebuild, forever - because a seed is a parameter attribute
that lives with the function body, so a TU that only *declares* a seeded callee
could not tell "never annotated" from "annotated elsewhere". Matching is now per
**argument**, not by name, so a partially seeded callee is still reported and
lists only the missing arguments.

**Fifteen of 41 are `indirect`** and cannot be seeded at all - there is no callee
name. That is the floor on what annotation can reach, and LTO does not lift it
(measured: LTO does not devirtualize; indirect calls went *up* 9%).

**Seven were SEVERE**, all thin forwarders that enable DIT and tail-call, leaking
the mode forever. `-fno-optimize-sibling-calls` - a plain compiler flag, not the
callee-saved ABI - takes those to **zero at no measurable cost**.

## A missing seed is invisible without the report

SQLCipher, before any of this tooling existed:

| seed set | coverage vs a hand-placed oracle |
|---|---|
| cipher entry points only | 94.4% |
| cipher + kdf + **hmac** | **98.4%** |

The per-page HMAC ran with DIT off and nothing said so. The tell was exact -
`hmac_init.o` and `sha512.o` were **byte-identical** between the plain and
instrumented builds. And it could never have been reached by propagation:
libtomcrypt calls the hash through `hash_descriptor[].process`, a function
**pointer table**, so it needed its own seed line no matter how good the analysis
was. That is the case the information-loss report exists for.

## Limits

- **Two libraries.** libsodium in depth, SQLCipher as a corroborating point.
- **The cost figures are experiment 02's workload**, so they inherit its limits -
  in particular its synthetic public lane.
- **The report cannot verify the other TU.** It knows the seed file *this*
  invocation was given names a callee. A build that hardens some directories and
  not others still has a real gap and nothing detects it.
- **"Fixpoint" is scoped to the signing path.** The whole-library report still
  carries 19 records on paths this workload never executes.

## Contents

| path | what |
|---|---|
| `data/seed_depth.csv` | the fixpoint loop, per round |
| `data/seed_depth_cost.csv` | what the extra precision cost, per secret fraction |
| `data/report_burden.csv` | how many records the developer actually has to read |
| `data/coverage_gain.csv` | SQLCipher: a missing seed, and why propagation could not find it |
| `figures/annotation.html` | source of the published artifact |
