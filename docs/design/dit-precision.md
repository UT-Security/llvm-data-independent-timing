# DIT precision: the number placement should be optimized for

**Added 2026-08-06.** `-taint-dit-precision-report=<file>` answers: of the code
running with `PSTATE.DIT` set, how much of it actually had to?

That ratio is the thing a placement policy can be tuned against. Before this,
placement quality could only be judged by running the binary on gem5 and reading
cycle counts — a loop you cannot put inside a cost function.

## The metric

Per instrumented function, one line:

```
chacha20_encrypt_bytes_ref need=295 underdit=372 collateral=77 total=400 \
    switches=22 precision=79.3 coverage=93.0 \
    wneed=12382 wunderdit=13854 wtotal=14062 wprecision=89.4
```

| Field | Meaning |
|---|---|
| `need` | instructions that **must** execute with DIT=1 (`needsDIT`: coverable-tainted ops plus secret-passing calls) |
| `underdit` | instructions that **do** execute with DIT=1 — mode switches themselves excluded |
| `collateral` | `underdit - need`: public code paying DIT's cost for nothing |
| `total` | all real instructions in the function |
| `switches` | `MSR DIT` instructions emitted |
| `precision` | `need/underdit` — **maximize this** |
| `coverage` | `underdit/total` — how much of the function is in DIT mode |
| `w*` | the same counts weighted by `10^min(loop depth, 4)` |

Summarize and compare policies with `utils/taint_dit_precision.py`:

```
utils/taint_dit_precision.py region=r.txt function=f.txt
```

## Precision alone is a trap — it trades against switch count

The two terms pull in opposite directions, and that tension *is* the placement
problem (`docs/results/dit-cost-model.md`). Whole libsodium, both policies:

| | region | function |
|---|---|---|
| secret instructions | 2,176 | 2,176 |
| under DIT | 3,018 | 4,813 |
| collateral | **842** | 2,637 |
| precision | **72.1%** | 45.2% |
| precision, loop-weighted | **88.1%** | 55.9% |
| mode switches | 189 | **146** |

Region placement is far more precise — 842 instructions of collateral against
2,637 — for only 1.3x the switches. **And it still loses at run time**: aead's
region placement costs +20.5% on serializing-DIT hardware versus +9.4% for
function placement, at indistinguishable protection coverage.

That gap is the metric's most important caveat. `switches` is a *static* count,
and static counts do not know that one toggle sits in a per-iteration loop body
while another runs once at function entry. Region placement concentrates its
extra toggles exactly where they execute most. So:

> **Neither `precision` nor `switches` is trustworthy unweighted.** Read
> `wprecision` for the dwell side, and remember that no static number captures
> executed toggles at all — that needs a dynamic counter (see "What it is not").

The single-TU view shows the same trade at smaller scale: on `chacha20_ref.c`
region wins precision 68.6% vs 64.0% (88.9% vs 87.6% weighted) for twice the
toggles, 40 against 20.

**So do not optimize `precision` on its own.** The objective is
`precision` subject to a switch budget, with the budget set by the target's toggle
cost (`-taint-dit-switch-cyc`).

## Always read the loop-weighted number too

Unweighted precision counts a block once whether it runs once or a million times.
Convolve is the case that proves why:

Whole-TU totals for `convolve_int_gem5.c`:

| | region | function |
|---|---|---|
| precision | **44.4%** | 31.4% |
| precision, loop-weighted | 25.8% | 25.5% |
| switches | 14 | **5** |

Statically region placement looks 13 points better. Weighted by execution
frequency the gap collapses to **0.3 points** — and the measured result is that
region placement is **7.16x slower** than baseline on serializing-DIT hardware,
because its toggles sit inside a per-pixel loop. The unweighted number would have
told you to pick the policy that loses by 7x.

Note also how much lower the weighted precision is than the unweighted one here
(25.8% vs 44.4%): the collateral in this TU is concentrated in the hot loop, which
is precisely the collateral that costs.

## What it is not

- **Static, not dynamic.** These are instruction counts in the emitted code, with
  loop depth as a stand-in for execution frequency. `switches` in particular has
  no weighted variant and is the weakest number here — a toggle in a hot loop and
  one at function entry count the same, which is precisely the difference between
  aead's +9.4% and +20.5%. A real profile would be better; gem5 could supply a
  true dynamic count by tallying committed instructions with `cc_reg::Dit` set,
  and executed mode switches alongside it. Not implemented.
- **`need` is the analysis's opinion.** It inherits every imprecision of the taint
  analysis — context-insensitive mod-sets, TU-scoped propagation. An over-tainting
  bug inflates `need` and makes placement look *better* than it is.
- **It says nothing about soundness.** A policy covering nothing scores
  `precision = 100%`. Read it alongside the region verifier, never instead of it.
- **`coverage` is not 100% even under whole-function placement**: the `MSR DIT, #0`
  precedes the return, so the return itself runs with DIT off. See
  `taint-analysis-dit-precision.mir`.

## Where the collateral actually is

`taint_dit_precision.py` ranks the worst functions. The single largest source in
libsodium under function placement:

```
crypto_aead_aes256gcm_decrypt_detached_afternm  collateral=1,529  precision=1.5%
```

A large function with 23 secret instructions runs 1,529 public ones in DIT mode —
by itself 58% of the whole library's collateral under that policy. Region
placement cuts the same function to 77. Concentration like this is the argument
for per-function policy selection rather than one global choice.

## Implementation

`computeDITAccounting` in `llvm/lib/CodeGen/TaintAnalysis.cpp` walks the emitted
function once via `replayTaint`, tracking DIT-on state from `computeDITOnEntry` —
the same 1-bit dataflow the region-placement soundness verifier uses, extracted so
the two can never disagree about which instructions run with DIT set. It runs
after placement, for both policies, so the numbers are directly comparable.
