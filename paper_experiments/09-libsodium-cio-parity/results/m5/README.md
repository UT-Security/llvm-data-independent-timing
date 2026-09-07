# Experiment 09 on Apple M5 - 2026-09-06

Two rigs on the same machine, because this M5 **cannot produce an m4-grade
parity run**: its kernel does not expose the PMCs to EL0, so the parity rig fell
back to kperf and its per-op table is empty. What is trustworthy here is the
blanket number, measured by a rig built for a machine in exactly this state.

| file | what |
|---|---|
| `blanket.csv` | the blanket rig: one row per primitive x arm x rep |
| `blanket-report.txt` | its table and five gates - **read this one** |
| `blanket-provenance.txt` | host, root, libsodium archive, reps |
| `blanket-isolated/` | a second blanket run, `aes_dec` alone - see below |
| `cio.csv` | the parity run, one row per benchmark x arm x rep |
| `ours.csv` | the ditprobe instrument rows behind gates 1-4 |
| `provenance.txt` | host, toolchain, **arm CFLAGS**, arm set, `cntfrq` |
| `report.txt` | the parity report as run: 23 lines, gates only |
| `report-full.txt` | `FULL=1`: adds the CNTVCT and whole-process tables |

Regenerate any of them:

```sh
OUT=paper_experiments/09-libsodium-cio-parity/results/m5 \
  python3 utils/dit_blanket/blanket_report.py                   # blanket
OUT=paper_experiments/09-libsodium-cio-parity/results/m5/blanket-isolated \
  python3 utils/dit_blanket/blanket_report.py                   # isolated aes_dec
OUT=paper_experiments/09-libsodium-cio-parity/results/m5 \
  python3 utils/taint_libsodium_sudo_report.py                  # parity
```

## The result

Apple M5 (Mac17,2), **rooted**, 21 paired reps, 200,000 operations per rep,
1 KiB messages, deterministic inputs. The arms are one binary run twice with
`BLANKET_DIT=0` and `=1`. Cycles from kperf, read **twice per rep** rather than
per operation, so the ~3,400-cycle pair is under 0.01% of each measurement.

| primitive | base cyc/op | blanket | vs M4 |
|---|---|---|---|
| ed25519 sign | 42,872 | +0.58% | +0.93% |
| ed25519 open | 89,174 | +0.42% | *(folded into M4's ed25519 row)* |
| chacha20-poly1305 encrypt | 5,907 | -0.03% | +0.37% |
| chacha20-poly1305 decrypt | 5,939 | +1.65% | +2.07% |
| aes256-gcm encrypt | 775 | +1.13% | -1.32% |
| aes256-gcm decrypt | 814 | +3.81% long run, -0.92% short (see below) | +4.82% |
| *(control: LVP-predictable chase)* | *289* | *+190.55%* | *-* |

**Zero to +3.81%.** The M4's independent PMC run agrees on every row within
about a point and puts the cost in the same place, AEAD decryption. Two
machines, two counter sources and two rigs, one conclusion: **blanket
`PSTATE.DIT` is close to free on libsodium primitives**, which is the negative
control this experiment exists to provide.

All five gates pass, including gate 2, which had failed on `ed25519_open` in an
earlier run before the rig's inputs were made deterministic:

```
1. instrument     counter pair < 0.01% of total            PASS
2. same work      instructions/op match on every row       PASS
3. clock          A and C within 0.01 GHz on every row     PASS
4. DIT was on     control slowed +190.55%                  PASS
5. resolvable     5 of 7 rows clear 3x MAD                 PASS
```

Gate 3 is why this run was worth doing rooted. The arms are confirmed to have
run at the same frequency on every row, so these are cycle ratios and not time
ratios carrying a DVFS difference.

## The number for aes256-gcm decrypt depends on how long the rig has been running

Seven rooted runs on this machine, gate 3 passing in all of them:

| run shape | wall time | A (DIT off) | C (DIT on) | overhead |
|---|---|---|---|---|
| 7 primitives, ITERS=200k | ~10 min | 809-814 | 840-845 | **+3.8%** |
| 1 primitive, ITERS=2M | ~5 min | 809.5 | 828 | **+2.3%** |
| 1-2 primitives, ITERS=200k | ~2 s | 834-848 | 826-841 | **-0.9%** |

Arm A reads ~810 cyc/op in every run lasting minutes and 834-848 in runs lasting
seconds. The swing is 4.5% and it lands almost entirely on the unhardened arm,
so it sets the answer.

**Two explanations were tested and both are wrong.** Neither is inherited state
from a neighbouring benchmark: running a 200,000-iteration pointer chase inside
the measured process (`chase+aes_dec`) changes nothing at a matched clock,
-0.92% against -1.31%. And it is not the core clock: a 400-rep run spanning
4.607 to 4.391 GHz, a 5.12% swing covering both regimes above, held arm A flat
at 809-810 the whole way and moved the ratio by 0.36 points, with
correlation r = -0.169. The clock accounts for under a tenth of the effect.

What remains is that something saturates within the first minutes of sustained
load. **This file does not name it**, because two named mechanisms have already
failed here and a third guess is worth less than the measurement.

**The mitigation is `SOAK`**, a fixed load run before the first measurement so
every run reports from the steady state rather than from wherever it happens to
be seconds in. It defaults to 180 s. A soaked number and an unsoaked one are not
comparable, and every run in the table above is unsoaked.

**Gate 3 does not catch any of this and cannot**: it compares the two arms
*within* a run, and in all seven they match to 0.01 GHz. The variance is
entirely between runs.

## Reading it, and what not to claim

**`aes_enc` sits at the resolution floor.** Two rooted runs put it at +1.48%
(resolved) and +1.13% (below floor, arm-A MAD 0.69%). The honest statement is
"around +1%, at the floor", not a resolved value.

**Do not compare `cio.csv`'s per-region percentages against m4's table.** They
are not the same measurement. `reg_cyc` charges a kperf pair at every region
boundary, so base aes256-gcm encrypt reads **2,866 cyc/op here against the M4's
PMC-measured 255**. That offset sits in both arms, so it does not bias the
direction, but it divides every percentage by roughly an order of magnitude. The
parity run's placement arms (bracket, ExpeDITe and their NOP twins) are recorded
in `cio.csv` for completeness and **are not quotable** until this machine can
read PMCs from EL0.

**The whole-process table in `report-full.txt` is not a second opinion.** It
covers the entire process including setup, which for aes256-gcm is roughly 98%
of the cycles, and it reports blanket at **-8.8%** on that benchmark. That
figure is an artifact of measuring mostly dyld and key setup and contradicts
both rigs above; it is kept only because it is what the run printed.

**`blanket-isolated/` has no control row**, so its gate 4 prints a warning. The
PSTATE.DIT readback still confirms the mode was set (arm A exits 0, arm C exits
1 on every rep), and arm C lands within 0.6% of the full run's arm C, so the row
is sound. A run with no control and no readback would not be.

## Against M4

`../m4/` is the reference: rooted, pinned to cpu 9, pure-PMC counters, zero
migration drops, and the per-op table this run could not produce. Where the two
disagree the M4 is the better instrument. The one row differing in sign is
aes256-gcm encrypt, -1.32% there against +1.13% here, both at their respective
resolution floors.

**Read the aes256-gcm decrypt agreement carefully.** M4's run is a long one -
the full parity rig over six benchmarks - so it sits in the same regime as this
machine's long runs, where the M5 also reads +3.8%. Short runs on this M5 read
-0.9%. The two machines agreeing at +4.82% and +3.81% is therefore agreement
about a shared condition, not two independent confirmations of a value. The
other rows cost far more per operation, so the effect is proportionally much
smaller on them.

To bring this machine up to m4 grade, the kernel needs `PMCR0_USEREN_EN`
(PacmanPatcher: SIP off, matching KDK, patched kernel collection, boot into
1TR). Until then, `./reproduce.sh blanket` is what this machine can answer
honestly, and `./reproduce.sh silicon` will keep producing an empty per-op
table.
