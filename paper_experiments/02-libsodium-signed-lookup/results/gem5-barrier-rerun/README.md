# gem5 rerun with the corrected barrier — and gem5 cannot resolve this bracket

**Run 2026-09-08 on the M4 itself.** gem5 rebuilt from `gem5-DIT` master
(`gem5.fast`, 25.1.0.0, built here — the checked-in Jul 23 binary was stale, the
config now imports `SIPPrefetcher`); the workload cross-compiled to static
aarch64-linux ELF with `util/cross/taint-cross-cc` and the 74 MB sysroot, so the
simulation runs on a Mac without an aarch64 host. 216 simulations, 180 of them
the 5-offset sweep, ~11 minutes.

## What changed and what it did

`api_bracket.c`'s default barrier moved from `isb sy` alone to Apple's
documented no-FEAT_SB fallback `dsb nsh; isb sy`. exp02's gem5 build inherits
the default, so the `api` arm's wrapper is now:

```
mrs  x19, DIT / msr DIT,#1 / dsb nsh / isb / bl <AEAD> / tbnz / msr DIT,#0
```

18 instructions, and `apinop` is 18 too (`API_NOP` became barrier-aware). 2
committed `ditWrites` per request in every cell.

**It changed nothing measurable.** `api - apinop`, cycles per request, median of
5 stack offsets, against the committed `isb`-only numbers:

| L | requests | committed (`isb` alone) | this rerun (`dsb; isb`) |
|---|---|---|---|
| 10 | 1,600 | +50 | **-45** |
| 50 | 1,500 | +31 | **-42** |
| 200 | 1,270 | +58 | **-15** |
| 1,000 | 670 | -9 | **+2** |
| 5,000 | 200 | -42 | **+129** |
| 20,000 | 55 | -625 | **-1,382** |

Both columns straddle zero. Adding `dsb` did not make the bracket dearer,
because **the bracket's own cost is below what this model resolves on this
workload** — and the sign says why: the arm is FASTER than its own
instruction-matched twin at 10 of 12 cells, by 1-4% of a request. `HINT #0` is
not a free issue slot in this model (CLAUDE.md prices it at ~0.25% in the
direction that understates the switch; here it is worth more than that), so
`api - apinop` measures the twin's nops more than it measures the bracket.
Offset spread is 0.14-1.66%, so at L=10 renamed (-3.66%) the effect is outside
the spread — it is real, and it is the wrong sign to be the bracket's cost.

**So `api - apinop` is not a usable measure of the bracket in gem5**, at either
barrier. It was not usable before this change either; the committed column's
+50/+31/+58 at short L and negative values at long L were the same artifact with
a smaller nop count.

## The one clean number gem5 gives

`api` serialising minus `api` renamed is the **same binary** under the two switch
models, so it carries no layout term and no nop term:

| L | 10 | 50 | 200 | 1,000 | 5,000 | 20,000 |
|---|---|---|---|---|---|---|
| cycles per serialising write | **23.0** | **25.9** | 36.9 | 116.8 | 149.1 | -81.4 |

At the short lengths, where 1,270-1,600 requests make per-request granularity
fine, this lands on **23-26 cycles per write** — consistent with
`docs/results/dit-cost-model.md`'s ~30 and with the **33.5 cycles measured on
this M4** by `silicon/dit_switch_cost.c`. Past L=1,000 it degenerates (200 and
55 requests) and at L=20,000 it goes negative, so only the first two columns
should be quoted.

## What this does and does not settle

- **Settles:** the `isb`-vs-`dsb;isb` substitution was not why the gem5 bracket
  column looked free. An earlier note in this repo claimed the `isb` stand-in
  undercharged the barrier by ~8x; that is **withdrawn**. The barrier is not the
  reason — the model cannot see a two-write bracket above its own layout and
  `hint #0` noise, whichever barrier it carries.
- **Settles:** gem5 charges 23-26 cycles for a serialising `msr DIT`, which is
  the right order against silicon's 33.5.
- **Does not settle:** the switch model. `--apple` and `--expedite` are not in
  this checkout's master (branch `dit-flag-restructure @ 455bc87c56a4`, not on
  origin, "not our ref"). This rerun is the corrected barrier under `renamed` and
  `serialising`.
- **Does not settle:** anything about `sb`. gem5 implements none, upstream
  included, so Apple's actual shipping barrier remains unmodelled and unmodellable
  here.

## Reproducing

`run_barrier.py` in this directory, and `sweep.log` is what it printed. It needs
`gem5.fast` built from `gem5-DIT` master and the three arms cross-built as the
header describes; the binaries are not committed (1.4 MB each) and the run dirs
are 216 x ~1 MB of `stats.txt`.
