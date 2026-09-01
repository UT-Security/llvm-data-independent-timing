# 04 - libsecp256k1 soundness

**Status: complete on gem5.** Re-measured 2026-09-01 against compiler
`3ab5812f6207`, because the pass changed materially that day (meta-instruction
fix, module-wide secret globals, record dedupe) and the earlier figures were
taken before all three.

**Published artifact:** https://claude.ai/code/artifact/f8e0b663-444d-450b-ad3c-1b31cffe44f0
Source: `figures/soundness.html`. To update the page, republish **that URL**
(`Artifact` with `url=...`); publishing the file without it creates a second
artifact instead of updating this one.

---

## The claim

> Experiments 01-03 measure what the pass COSTS. This one measures what it
> **protects**, which is the only reason to pay anything at all.
>
> On production crypto the pass is sound: **4,647,778 of 4,647,818 secret
> operations execute with PSTATE.DIT set**, and **zero** under-taint sites lie
> inside libsecp256k1. The 40 survivors are the harness consuming the signature -
> a declassification point, not a leak.
>
> It is not sound in general, and we know exactly where it breaks: six
> constructed channels leak by design. None of them occurs in libsecp256k1.

## Why this needs a dynamic oracle

The static verifier cannot answer it. It is intraprocedural and treats calls as
transparent, which is the very cross-frame property in question. The gem5
shadow-taint oracle seeds the key with an `m5` op, propagates taint **at commit**
so it sees the real dynamic trace rather than speculation, and flags any
DIT-covered instruction that consumed a secret with the mode clear.

`m5_taint_report` is taken **before** anything touches the signature. Dynamic
taint is transitive, so the signature and everything derived from it are
secret-derived by the model even though they are published; letting that reach
`printf` would bury the real result under the declassification gap.

## Result

| | |
|---|---|
| secret ops **protected** | **4,647,778** |
| secret ops with DIT clear | **40** |
| same, unhardened control | 4,647,818 |
| fraction protected | **99.99914%** |
| under-taint sites **inside libsecp256k1** | **0** |
| where the 40 are | `main()`, `secp_gem5.c:53` - `sink += ...` on the signature |

The unhardened control is what makes this meaningful: it registers **4,647,818**
secret ops running with DIT clear, so the oracle is demonstrably able to see them
and the hardened arm's 40 is not an instrumentation failure.

## What it costs, with the layout term separated

8 `argv[0]` lengths, because in gem5 SE mode `argv[0]` sits on the initial
process stack and its **length moves the initial SP**. The no-DIT baseline alone
spans 2.41% across those paths, so a single path proves nothing.

| term | median | 95% CI | verdict |
|---|---|---|---|
| blanket vs no-DIT | +0.167% | [-0.17, +0.89] | not resolved |
| **layout only (NOPed)** | -0.124% | [-0.95, +0.31] | **not resolved** |
| our placement vs no-DIT | +1.963% | [+1.19, +2.58] | **RESOLVED** |
| our placement vs blanket | +1.935% | [+0.53, +2.52] | **RESOLVED** |

The layout term not resolving is the load-bearing one: the pass's cost here is
**DIT actually running**, not the code-shape change of inserting switches. Its
gates are in `data/nop_gates.csv` and all pass - an inert NOP control does not
fail loudly, it silently reads as "layout is everything", which is how an earlier
result in this project had to be retracted.

Note the direction: on this workload **coarse beats fine**, +0.167% against
+1.963%. libsecp256k1 signing is secret work end to end, so there is no public
lane to uncover and narrowing buys nothing while still paying its toggles. That
is consistent with experiment 03's floor and with 02's high-f end.

## Where it does break

flowprobe constructs channels from a seeded secret to a consumer that computes on
it, with the ground truth stated per channel and positive controls that must come
out clean or the harness is broken.

| channel | mechanism | status |
|---|---|---|
| P1, P2 | direct argument; four-level chain | **protected** (controls pass) |
| C2 | secret global read by a sibling with no call edge | **FIXED 2026-08-31** |
| C1 / C5 | callee returns a POINTER into a secret buffer | 63 ops each, OPEN |
| C3 / C6 | inline asm stores the secret through a pointer | 63 ops each, OPEN |
| C4 / C7 | secret moved through a NEON register tuple | 63 ops each, OPEN |

C5/C6/C7 are the heap variants, added after the module-wide secret-global rule
closed C1/C3/C4 by covering their shared **delivery route** rather than their
mechanisms - all four probe buffers were `static` globals. On the heap they fail
identically, which is how we know the three defects are storage-independent.

**Two ways this probe misleads, both now guarded.** Its `consume_all` used to
accumulate every consumer's secret-derived return into one local, so once ONE
channel started working the first call tainted it and placement blanketed all
seven - reporting everything clean. Note the direction: fixing C2 is what broke
the harness, because while every channel was broken no return was recognised as
secret and it discriminated by accident. And `ditseen[]` samples DIT at *entry*,
which cannot separate "unprotected" from "self-instrumented, enable comes later".
Read the oracle's per-PC list, not `ditseen`.

## The honest scope of the soundness claim

- **Sound on this library, not in general.** Six mechanisms leak by construction.
  The claim is that libsecp256k1 contains none of them, which is a property of
  that code, not of the analysis.
- **One library, one entry point.** ECDSA signing with the key seeded at
  `secp256k1_ecdsa_sign`.
- **Over-approximation is the safe direction throughout**, and it is not free:
  the same run shows 40 protected-but-public ops for every secret one at some
  points. An over-taint hides an under-taint, so a clean oracle result on a
  heavily over-approximating build is weaker evidence than it looks.
- **The oracle had two precision faults** that manufactured under-taints earlier
  in this project's history, both fixed before this run.

## Reproducing

```sh
LLVM_BUILD=... ROUNDS=20 gem5-DIT/benchmarks/taint_oracle/run_secp_gem5.sh
LLVM_BUILD=... gem5-DIT/benchmarks/taint_oracle/build.sh   # flowprobe
```

Both print PASS/FAIL and their own gates. The secp script averages over 8
`argv[0]` lengths and reports a CI with an explicit *not resolved* verdict rather
than a single number.

## Contents

| path | what |
|---|---|
| `data/secp_oracle.csv` | the soundness result and where the survivors are |
| `data/secp_timing.csv` | cost, with the layout term separated and CIs |
| `data/nop_gates.csv` | the four gates that make the layout term trustworthy |
| `data/flowprobe_channels.csv` | the six channels that do leak, and the two controls |
| `figures/soundness.html` | source of the published artifact |
