# 08 - seed ground truth

> **Compiler note (audit 2026-09-05).** The numbers here were measured under the
> defaults of their date: the inherit contract, no DIT twins, block placement. The
> shipped defaults changed on 2026-09-05 (callee contract, twins, intra-block
> placement; `docs/reference/harden-runbook.md`), and the pre-contract seed files
> protect nothing under them. See the status table in `paper_experiments/README.md`
> before re-running.

**Status: complete, both tiers.** Static comparison and gem5 shadow-taint
confirmation, measured 2026-09-01 against compiler `845f69038a4e`,
libhydrogen as shipped in CryptoMPK's artifact, their shipped taint report as
the ground truth.

**Published artifact:** https://claude.ai/code/artifact/2a789196-2274-42fc-9922-b624f0808762
Source: `figures/agreement.html`. To update the page, republish **that URL**
(`Artifact` with `url=...`); publishing the file without it creates a second
artifact instead of updating this one.

---

## The claim

> Every other experiment here chooses its own seeds, which makes "you picked the
> annotations that make your pass look good" a fair objection and one no amount
> of internal measurement can answer.
>
> CryptoMPK (IEEE S&P 2022) is an independent taint analysis by another group,
> and its artifact ships the **derived taint set** for libhydrogen as
> `(file, line)` pairs. Run our pass on their exact source and compare.
>
> **We agree on 79% of the functions they mark.** We also miss seven, three of
> which are X25519 field arithmetic, including both multiplies - and **our own
> information-loss report says nothing** about the gap. The seed *point* is what
> decides it: two annotations expressing the same intent differ by 32.4% vs
> 79.4% recall.
>
> **The gem5 shadow-taint oracle then settled it dynamically, and it is worse
> than the static count suggested.** At the seed a developer writes first,
> **97.61% of all secret-carrying operations execute with `PSTATE.DIT` clear** -
> 445,276 of 456,194. Three of the seven are confirmed under-taints, one is
> refuted, and the static agreement figures turn out to be an upper bound.

## Why libhydrogen, and only libhydrogen

Their suite has seven targets. libhydrogen is the only one where the comparison
measures what it claims to:

- **The whole library is a single translation unit** (`hydrogen.c` includes every
  `impl/*.h`), so our per-TU analysis and their whole-program LTO analysis see
  the same code. On libsodium - the other target whose report ships - the eight
  relevant TUs mean our cross-TU limit would dominate and we would be measuring
  that instead of precision.
- **It is 4.3k lines of pure C**, so every disagreement can be read by hand.
- Their `libhydrogen_vector.patch` inverts the `__SSE2__` test, which on their
  x86-64 host selects `gimli-core/portable.h`. That is the file their report
  covers, so we select it directly rather than the aarch64 path.

Their build is x86-64 `-O0` LLVM IR; ours is aarch64 `-O2` post-register-allocation
MIR. **Line-level agreement is therefore noisy for reasons unrelated to either
analysis being wrong**, so the headline unit is the enclosing C function, which is
stable across that difference. Both are reported.

## Result: agreement

Restricted to the primitives their two drivers actually invoke. The exclusion is
by public primitive family (`hydro_kx_*`, `_hydro_pwhash_*`, `hydro_kdf_*`), not
by hand: the relocation call graph confirms neither driver reaches any of them.

| seed set | ours | theirs | agree | recall | precision |
|---|---|---|---|---|---|
| `hydro_sign_create,4,pointee` | 16 | 34 | 11 | 32.4% | 68.8% |
| `hydro_sign_keygen,0,pointee` | 45 | 34 | 26 | 76.5% | 57.8% |
| **both** | 46 | 34 | **27** | **79.4%** | 58.7% |

**79% recall against an independent analysis is the positive result**, and it is
the one thing here no internal measurement could have produced.

## Result: the seed POINT decides, not the seed count

Both annotations say "the signing key is secret". One names the `sk` argument;
the other names the keypair buffer, which is the form CryptoMPK's own
`#pragma tainter taint(&key_pair)` takes. They are not interchangeable:

| | `hydro_sign_create,4,pointee` | `hydro_sign_keygen,0,pointee` |
|---|---|---|
| `msr DIT` in the library | 27 | 298 |
| `hydro_sign_final_create` need | 27 | 264 |
| `hydro_sign_final_create` coverage | **12.0%** | 99.2% |
| curve ladder marked | **no** | yes |
| recall vs CryptoMPK | 32.4% | 76.5% |

The argument seed is the one a developer would write first, and it leaves 88% of
the seeded function and the entire scalar multiply unmarked.

**Why.** `hydro_sign_prehash` derives the ephemeral secret by hashing the
long-term key: `hydro_hash_init(&st, zero, sk)` then
`hydro_hash_final(&st, eph_sk, ...)`. `hydro_hash_final` writes a secret
**through an argument pointer**, and the P0 mod-set summary carries no argument
provenance (documented: "no arg-i or per-offset precision"), so the caller never
learns that `eph_sk` became secret. Confirmed by repair - adding one line:

```
hydro_hash_final,1,pointee
```

takes `hydro_sign_final_create` from need=27 / coverage 12.0% to need=265 /
coverage 99.2%, brings `hydro_x25519_scalarmult` (need=43) and
`hydro_x25519_core` (need=170) into the analysis, and the library from 27 to 304
switches.

## Result: seven functions we never mark

At any seed set. `AlwaysEnteredWithDIT = 0` for each, so our pass is not claiming
inherited coverage either - this is an analysis gap, not a reporting artifact.

| function | their locations | ours | standalone symbol at -O2 |
|---|---|---|---|
| `hydro_x25519_sc_montmul` | 6 | 0 | yes |
| `hydro_x25519_mul` | 3 | 0 | yes |
| `hydro_hash_final` | 3 | 0 | yes |
| `hydro_secretbox_setup` | 3 | 0 | yes |
| `hydro_sign_p2` | 2 | 0 | inlined |
| `hydro_sign_verify_p2` | 2 | 0 | inlined |
| `hydro_x25519_add` | 1 | 0 | inlined |

Taint reaches `hydro_x25519_core` (need=170) and stops at its call into
`hydro_x25519_mul`. Both are in-TU direct calls, so this is not the cross-TU
limit; the suspected mechanism is pointee taint through frame-address arguments
(the P1b frame-addr fallback).

**Resolved dynamically below**: three of the seven are confirmed under-taints,
one is refuted, three are not exercised by this driver.

## Result: our report is silent about all of it

The whole point of experiment 07's information-loss report is that the compiler
tells you where it lost the secret. Under the seed a developer would actually
write, it does not:

| seed set | info-loss records | SEVERE warnings | ESCAPE records | coverage of the seeded function |
|---|---|---|---|---|
| `create_only` | **0** | **0** | **0** | **12.0%** |
| `keygen_only` | 6 | 1 | - | 99.2% |

**This is the actionable finding.** The report's severity criterion is
consequence at a *call boundary* - cross-TU, tail call, indirect. A secret lost
to an imprecise mod-set inside one TU crosses no boundary the report watches, so
nothing fires. It should: the repair line is exactly the kind the report already
knows how to print.

## Tier 2: the gem5 shadow-taint oracle

The static evidence for the seven was that they carry no taint in our analysis
and are not `AlwaysEnteredWithDIT`. What it could not say is whether they
actually *execute* with DIT clear while holding a secret. A host page oracle
cannot answer that either - everything derived from the key lives in registers
and on the stack. The gem5 tier can: shadow taint on registers and memory,
checked at O3 commit, seeded through an m5 op on the same key the compiler seed
names.

**The null control is the whole basis for reading a zero.** Same code, no
`-ftaint-harden`. If it were quiet, the seed never reached the crypto and every
hardened number below would be vacuous.

| arm | seed | under-taint ops | protected | sites | unprotected |
|---|---|---|---|---|---|
| **null** | none (control) | 456,194 | 0 | 715 | 100% |
| **create** | `hydro_sign_create,4,pointee` | **445,276** | 10,918 | 611 | **97.61%** |
| **both** | `+ hydro_sign_keygen,0,pointee` | 126 | 456,068 | 8 | **0.03%** |
| repair | `+ hydro_hash_final,1,pointee` | 445,276 | 10,918 | 611 | 97.61% |

Counts scale exactly with rounds (445,276 x 5 = 2,226,384; sites 611 -> 612), so
one signature is representative.

### The seven, resolved

`null_ops` is the proof the function runs and touches a secret at all - without
it a zero in a hardened arm cannot be told from "never executed".

| function | null | create | both | verdict |
|---|---|---|---|---|
| `hydro_x25519_mul` | 16,866 | 16,866 | 0 | **under-taint at the natural seed** |
| `hydro_x25519_add` | 5,632 | 5,632 | 0 | **under-taint at the natural seed** |
| `hydro_hash_final` | 12 | 12 | **12** | **under-taint at EVERY seed** |
| `hydro_x25519_sc_montmul` | 117 | 0 | 0 | *refuted* - the caller's region covers it |
| `hydro_secretbox_setup` | 0 | 0 | 0 | never runs on this driver (enc path) |
| `hydro_sign_p2` | 0 | 0 | 0 | inlined; its arithmetic is in its callees |
| `hydro_sign_verify_p2` | 0 | 0 | 0 | inlined; its arithmetic is in its callees |

**Three confirmed, one refuted, three not exercised.** `sc_montmul` is the
instructive one: it carries no taint of its own and is not
`AlwaysEnteredWithDIT`, yet it runs protected because the call sites happen to
sit inside the caller's region. Static evidence could not have told those apart,
which is exactly why this tier exists.

### The seven were the tip

At the natural seed the whole X25519 ladder runs clear, and the top offender is
not among the seven at all:

| function | ops with DIT clear | sites |
|---|---|---|
| `hydro_x25519_umaal` | **291,335** | 101 |
| `hydro_x25519_propagate` | 37,600 | 58 |
| `hydro_x25519_sub` | 28,160 | 110 |
| `hydro_x25519_adc0` | 21,494 | 50 |
| `hydro_x25519_mul` | 16,866 | 6 |
| `gimli_core` | 16,204 | 70 |
| `hydro_x25519_condswap` | 14,376 | 96 |
| ... 12 more | | |

### A CORRECTION to this experiment's own method

The static comparison credits us with `hydro_x25519_propagate`, `_sub` and
`_canon` at the `create_only` seed - all three scored "both". The oracle says
those functions run with DIT clear for 37,600, 28,160 and 20 ops at that same
seed. Both are right, and the static method is what is wrong:

**every x25519.h line our analysis marked at `create_only` belongs to an inlined
copy inside `hydro_sign_final_create` / `_final_verify`** (12 lines, verified
against the `MachineFunction` column of the `_src` report). Mapping a source
line back to its enclosing function cannot distinguish an inlined copy from the
standalone function, so it credited us with covering functions we never touched.

**So treat 32.4% and 79.4% as UPPER BOUNDS on agreement.** The oracle has no
such problem: it works on committed PCs.

### What still leaks at the best seed

126 ops at 8 sites, and they are not noise:

- `hydro_hash_update` (560 ops over 5 signatures, 4 sites), inlined into
  `hydro_sign_challenge` - the hash absorbing secret state.
- `hydro_hash_final` (60 ops, 2 sites) - **one of the seven, unfixed by any seed
  we tried**.
- `main` (10 ops) - the harness reading `sig[0]`. The signature is
  secret-*derived* but public by design; we have no declassification tag, which
  is the `sinktaint` analogue `related-work.md` §3a already notes we lack. Not a
  library defect.

## What the "ours only" set is, and is not

46 functions to their 34, of which 12 are ours alone even in driver scope. Most
are the entry points where our seed attaches (`hydro_sign_create`,
`hydro_sign_verify`) plus the RNG (`hydro_random_ratchet`, `_u32`, `_uniform`,
`_check_initialized`).

Outside driver scope the difference is larger and has a single cause: seeding the
keygen output buffer makes the RNG state global secret, and the module-wide
secret-global rule then marks every RNG consumer - 17 `hydro_kx_*` and 8
`_hydro_pwhash_*` functions their drivers never call. That rule is the 2026-08-31
sibling-global fix. **It is not simply imprecision:** the ephemeral signing key is
fresh randomness, and the RNG path is the only route by which any seed reaches it.

## Limits

- **One library.** The reason is stated above and is structural, not effort.
- **The oracle is gem5, not silicon**, and one driver. `hydro_secretbox_setup`,
  `hydro_sign_p2` and `hydro_sign_verify_p2` are simply not exercised by
  `libhydrogen_sign.c`, so three of the seven remain unresolved rather than
  cleared.
- **The guest RNG is a fixed constant.** gem5 SE implements neither `ppoll(2)`
  (libhydrogen blocks on `/dev/random`) nor a readable `/dev/urandom`, so
  `hydro_random_init` cannot run there. Every arm gets the same constant, which
  a simulator comparison needs anyway; the question asked does not depend on
  entropy, only on the dataflow out of the generator.
- **No timing is quoted from the gem5 rig.** SE mode writes `argv[0]` onto the
  initial stack, so binary path length moves every stack address and shifts
  cycles by up to 0.84%. Under-taint PCs are unaffected - a static binary's code
  addresses do not move.
- **Their ground truth is not perfect either.** `related-work.md` §3a records that
  their `mxor` declassification is unsound at object granularity. Treat 79% as
  agreement between two imperfect analyses, not as a score against truth.
- **Their report is a union across both their drivers** (sign and enc), which is
  why `hydro_secretbox_*` appears in it.
- **No timing here.** Experiment 04 already measured that blanket DIT is free on
  libhydrogen, so there is no cost question to ask of this workload.

## Rerun

Static comparison:

```
utils/dit_host_screening/seedcmp/run_seedcmp.sh
```

~4 minutes for the three arms. The taint reports **append**, so the script clears
them first; two runs without that double every file.

Tier 2 (gem5 shadow-taint oracle), in the `gem5-DIT` tree:

```
benchmarks/hydro_oracle/build.sh && benchmarks/hydro_oracle/run_oracle.sh
```

Under a minute per arm. `run_oracle.sh` fails if the null control is quiet.

## Contents

| path | what |
|---|---|
| `data/agreement_by_seed.csv` | line- and function-level agreement, three seed sets |
| `data/agreement_driver_scope.csv` | the same restricted to what their drivers reach |
| `data/function_agreement.csv` | every function, both sides, with a verdict |
| `data/file_agreement.csv` | per-file line counts |
| `data/seed_placement.csv` | what the seed point costs, with the one-line repair |
| `data/undertaint.csv` | the seven we never mark |
| `data/report_silence.csv` | what our own reports said (nothing) |
| `data/cryptompk_ground_truth.txt` | their shipped report, verbatim |
| `figures/agreement.html` | source of the published artifact |
| `data/oracle_summary.csv` | the four oracle arms, 1 and 5 signatures |
| `data/oracle_seven.csv` | each of the seven, resolved dynamically |
| `data/oracle_by_function.csv` | where the 445,276 unprotected ops are |
| `data/oracle_residual.csv` | what still leaks at the best seed |
