# 08 - seed ground truth

**Status: complete.** Measured 2026-09-01 against compiler `890af850b255`,
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
- **Static evidence only for the seven.** That they are unmarked and not
  `AlwaysEnteredWithDIT` is our pass's own accounting; that they are
  secret-dependent is CryptoMPK's finding plus the source. Confirming they
  *execute* with DIT off needs the gem5 shadow-taint oracle, which was not run.
- **Their ground truth is not perfect either.** `related-work.md` §3a records that
  their `mxor` declassification is unsound at object granularity. Treat 79% as
  agreement between two imperfect analyses, not as a score against truth.
- **Their report is a union across both their drivers** (sign and enc), which is
  why `hydro_secretbox_*` appears in it.
- **No timing here.** Experiment 04 already measured that blanket DIT is free on
  libhydrogen, so there is no cost question to ask of this workload.

## Rerun

```
utils/dit_host_screening/seedcmp/run_seedcmp.sh
```

~4 minutes for the three arms. The taint reports **append**, so the script clears
them first; two runs without that double every file.

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
