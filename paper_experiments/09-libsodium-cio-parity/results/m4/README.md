# Experiment 09 on Apple M4 — 2026-09-06

Raw results and the table they produce. Regenerate the table from `cio.csv` with:

```sh
OUT=paper_experiments/09-libsodium-cio-parity/results/m4 \
  python3 utils/taint_libsodium_sudo_report.py
```

| file | what |
|---|---|
| `cio.csv` | one row per benchmark × arm × rep, 540 rows |
| `ours.csv` | the ditprobe instrument rows behind gates 1–4 |
| `provenance.txt` | host, toolchain, flags, arm set, `cntfrq` |
| `report.txt` | the report as run |
| `report-full.txt` | `FULL=1`: adds the CNTVCT table (see the warning below) |

## How it was measured

Apple M4 (Mac16,10), **rooted**, thread **pinned to cpu 9**, 15 reps per
benchmark, drivers at `-O2`, arms built by `utils/taint_libsodium_arms.sh` at
the compiler's shipped defaults (callee contract, DIT twins, intra-block
placement, round-11 fixpoint seeds, owned list).

**Cycles and instructions both come from the PMCs** — PMC0 read bare, PMC1
`isb`-ordered, sampled 1 region in 64 after a 64-region warmup skip. Nothing is
converted at an assumed clock and kperf is not started at all, so no other
instrument executes inside the measured window. `IPC = samp_ins / samp_cyc`.

Every gate passes:

```
1. DIT visible          Const 1.00 -> 3.00 cyc/hop        PASS
2. negative control     Perm flat                         PASS
3. P-core residency     CoreMHz 4393-4413                 PASS
4. mode readback        DitBit A=0.0 C=1.0                PASS
   implied clock        3.50-4.29 GHz                     PASS
   thread pinned        cpu 9                             PASS
   migration drops      0 of 7,288 samples (0.000%)       PASS
```

## The result

Blanket `PSTATE.DIT`, against the unhardened build:

| benchmark | blanket | instrs vs base |
|---|---|---|
| ed25519 sign | +0.93% | 0.00% |
| chacha20-poly1305 encrypt | +0.37% | 0.00% |
| chacha20-poly1305 decrypt | +2.07% | 0.00% |
| aes256-gcm encrypt | −1.32% | 0.00% |
| aes256-gcm decrypt | +4.82% | 0.00% |
| argon2id | −1.08% | −0.04% |

**−1.32% to +4.82%**, and blanket adds *exactly zero* instructions on every
benchmark — it is the same binary with the mode set by a constructor, so the
cycle ratio is the whole story.

## All arms

Cycles per operation and overhead against base; each hardened arm followed by
its NOP twin, which is the same code at the same addresses with every mode
switch replaced by a `nop`. **Subtract the twin before believing any number.**

| benchmark | base cyc / IPC | blanket | bracket (`sb`) | *bracket twin* | ExpeDITe | *ExpeDITe twin* |
|---|---|---|---|---|---|---|
| ed25519 sign | 34,351 / 4.36 | +0.93% | +1.29% | *−0.17%* | +1.86% | *+0.80%* |
| chacha20-poly1305 enc | 1,057 / 4.67 | +0.37% | **+18.47%** | *+0.29%* | **+114.91%** | *+0.85%* |
| chacha20-poly1305 dec | 1,169 / 4.45 | +2.07% | **+16.75%** | *+1.44%* | **+96.09%** | *−5.02%* |
| aes256-gcm enc | 255 / 4.93 | −1.32% | **+86.53%** | *+5.09%* | **+70.81%** | *−1.45%* |
| aes256-gcm dec | 298 / 5.02 | +4.82% | **+115.32%** | *+4.15%* | **+102.45%** | *+0.08%* |
| argon2id | 213,094,554 / 3.13 | −1.08% | +0.24% | *−0.63%* | −0.92% | *−0.97%* |

IPC shows the mechanism directly: aes256-gcm decrypt falls **5.02 → 2.35** under
the bracket on 11 extra instructions out of 1,498. That is two `sb` pipeline
drains, not extra work.

## Reading it, and what not to claim

**argon2id is `unresolvable` on every arm.** Between-arm range 1.3% against a
within-arm MAD of 1.72%: nothing there is distinguishable from noise. The −0.92%
for ExpeDITe means "this benchmark cannot tell", not "it is free". It still
contradicts gem5's +7.58% — see the open discrepancy in `../../CLAUDE.md` — but
the honest statement from silicon is *unresolvable*.

**The AES NOP twins are noisy in this run**, +5.09% and +4.15% where other runs
put them near 1%. On a 255-cycle operation with 2.38% MAD that is layout
sensitivity at the resolution floor. Subtract them and the bracket lands at ~81%
and ~111%, consistent with the other runs; do not quote the AES twin values to
two digits.

**Do not use `report-full.txt`'s CNTVCT table as a second opinion.** CNTVCT
measures TIME, so it carries whatever clock the machine picked, and the arms of
one benchmark do not all run at the same clock — measured on aes256-gcm decrypt
in an earlier run, 3.44 GHz on base and the twins against 4.42 on the bracket
and the pass, which made the hardened arms look 34 points cheaper than they are
in cycles. PMC cycles are DVFS-immune and are the like-for-like comparison
against gem5, whose numbers are cycles at a fixed clock.

## Against gem5

`../../data/gem5_api_bracket.csv`, `cfg=serdit` (the serialising `MSR DIT`,
which is what this silicon does). Expect the *mechanism* to agree and the
*percentages* to differ: the M4 runs aes256-gcm encrypt in 255 cycles where
gem5's model takes 1,217, so an equal absolute switch cost lands as a much
larger fraction.
