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
problem (`taint_dit_cost_model.md`). Measured on libsodium's `chacha20_ref.c`:

| | region | function |
|---|---|---|
| precision | **68.6%** | 64.0% |
| precision, loop-weighted | **88.9%** | 87.6% |
| mode switches | 40 | **20** |

Region placement wins precision by 4.6 points — and by only **1.3 points** once
weighted by loop depth — while emitting **twice** the toggles. On hardware where
`MSR DIT` serializes that trade is a loss, which is exactly what the gem5 numbers
show: aead's region placement costs +20.5% there versus +9.4% for function
placement, at indistinguishable coverage.

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
  loop depth as a stand-in for execution frequency. A real profile would be
  better; gem5 could supply a true dynamic count by tallying committed
  instructions with `cc_reg::Dit` set, which is not implemented.
- **`need` is the analysis's opinion.** It inherits every imprecision of the taint
  analysis — context-insensitive mod-sets, TU-scoped propagation. An over-tainting
  bug inflates `need` and makes placement look *better* than it is.
- **It says nothing about soundness.** A policy covering nothing scores
  `precision = 100%`. Read it alongside the region verifier, never instead of it.
- **`coverage` is not 100% even under whole-function placement**: the `MSR DIT, #0`
  precedes the return, so the return itself runs with DIT off. See
  `taint-analysis-dit-precision.mir`.

## Implementation

`computeDITAccounting` in `llvm/lib/CodeGen/TaintAnalysis.cpp` walks the emitted
function once via `replayTaint`, tracking DIT-on state from `computeDITOnEntry` —
the same 1-bit dataflow the region-placement soundness verifier uses, extracted so
the two can never disagree about which instructions run with DIT set. It runs
after placement, for both policies, so the numbers are directly comparable.
