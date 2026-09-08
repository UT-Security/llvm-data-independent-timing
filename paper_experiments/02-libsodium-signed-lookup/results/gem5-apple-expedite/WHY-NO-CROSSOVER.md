# Why `--apple` shows no bracket cost, and it is not the flush

`--apple` **is** serialising: it flushes, the redundant-write skip works, and its
per-write cost is the right order. The bracket disappears anyway, for three
reasons that multiply. Measured 2026-09-08, gem5-DIT `b76c101ad4`.

## The flush is real

From the sweep's own statistics, median of 5 offsets, per request:

| L | `api@apple` squashed insts | `api@expedite` | its NOP twin |
|---|---|---|---|
| 10 | **80.6** | 0.16 | 1.04 |
| 200 | **99.8** | 28.1 | 26.0 |
| 1,000 | **102.9** | 28.1 | 32.7 |
| 20,000 | **115.4** | 30.1 | 58.6 |

2 committed `ditWrites` per request in every cell, so `--apple` squashes ~40
instructions per write and `--expedite` squashes none. Flush-after is doing
exactly what it says.

## Reason 1 — the instructions are 0.43x silicon

`dit_switch_cost_gem5.c` here is the gem5 twin of
`utils/dit_host_screening/signed_lookup/silicon/dit_switch_cost.c`: the same
long loop of writes against the same loop without them, differenced, 200,000
iterations, ROI delimited by m5 ops so there is no counter offset at all.

| | gem5 `--apple` | Apple M4 | ratio |
|---|---|---|---|
| `msr DIT` (changes the mode) | 21.5 | **33.5** | 0.64x |
| `msr DIT` (redundant) | 7.5 | **11.5** | 0.65x |
| `mrs DIT` (the token read) | 3.0 | **11.0** | 0.27x |
| `sb` | **0.0** | **28.0** | 0x |
| **Apple's bracket, isolated** | **46.0** | **106.0** | **0.43x** |

`--expedite` for reference: 1.5 cycles per write, and `sb` 4.3. That arm being
nearly free is correct and expected — the renamed switch is the design that has
nothing to pay.

**The `sb` row is the biggest single gap.** `Sb64` is modelled (`IsSerializeAfter`,
a rename-stall drain) and it does cost 4.3 cycles under `--expedite`, where the
pipeline is not already empty. But under `--apple` it costs **&minus;3** — the
flushing `msr DIT` ahead of it has already drained the machine, so the drain
finds nothing to do. On the M4 the two do not compose that way: `sb` still costs
28 cycles in the same tight loop, and 140-170 in situ. Something about Apple's
speculation barrier is not "wait for the ROB to empty".

## Reason 2 — the model misses the in-situ amplification

Both instruments cost more inside a real instruction stream than in a loop. The
M4 costs *much* more:

| | isolated | in situ, per bracketed call | amplification |
|---|---|---|---|
| gem5 `--apple` | 46 | ~65 | **1.4x** |
| Apple M4 | 106 | **455-510** | **4.5x** |

(gem5 in situ = `api@apple - api@expedite` at L=10, 58.7 cycles/request, plus
`--expedite`'s own ~6; it is the only layout-free handle the sweep has.)

So even with the per-instruction costs corrected, the model would still be ~3x
short. That is a property of the out-of-order model rather than of the DIT
implementation: on silicon a serialising write costs what it has to drain, and
the M4 evidently has far more in flight at the bracket boundary than this model
puts there.

## Reason 3 — the baseline request is 2.2x longer

At L=10 a request is **2,481 cycles** under gem5 and **1,131** on the M4, on the
same driver at the same L. So the same absolute bracket cost is 2.2x less
visible before any of the above.

## The three multiply

Bracket cost as a fraction of a secret-heavy request:

| | bracket | request | as % |
|---|---|---|---|
| gem5 `--apple` | ~65 cyc | 2,481 | **2.6%** |
| Apple M4 | ~480 cyc | 1,131 | **42%** |

**16x**, which is the whole of "no crossover in gem5". Blanket's curve is
already right — +30.75% against the M4's +34.4% at the public-heavy end — so the
panel is missing one term, not two.

## What would move it

In order of size:

1. **Price `sb`.** It is 0 cycles after a flushing write and 28 on silicon in the
   same loop. Whatever Apple's `sb` does, "stall rename until the ROB drains" is
   not it, because that is satisfiable for free once the mode write has already
   flushed. A cost that does not collapse when the pipeline is already empty
   would recover the largest missing piece.
2. **Price `mrs DIT`.** 3 cycles against 11.
3. **Price `msr DIT`.** 21.5 against 33.5 — closest of the three, and the
   redundant/changing *ratio* is already right (0.35 in the model, 0.34 on the
   M4), so only the scale is off.
4. **The amplification** is the deep one and may not be a DIT question at all.

## Reproducing

```sh
# built with gem5-DIT util/cross/taint-cross-cc, run on any host
for m in "" --apple --expedite; do
  for k in empty pair same sb mrs; do
    gem5.fast --outdir=out_${m}_${k} configs/example/arm/fdp_neoverse_v2_binary.py \
      --binary dit_switch_cost_gem5 --arguments "--kind $k --n 200000" \
      --eves --dmp --comp-simp $m
  done
done
# cycles/write = (pair - empty) / n / 2, from the FIRST stats dump
```

The silicon side is `silicon/dit_switch_cost.c`, which reads 33.51 / 11.50 on
this M4.
