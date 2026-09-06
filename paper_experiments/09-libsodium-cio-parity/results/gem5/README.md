# Experiment 09 on gem5 - 2026-09-06

Apple's DIT bracket against ExpeDITe, under both switch models. Raw results and
the tables they produce. The writeup is `../../rerun-2026-09-06.md`.

Regenerate the tables from the raw JSONL with:

```sh
python3 utils/dit_host_screening/cioparity/analyze.py \
  paper_experiments/09-libsodium-cio-parity/results/gem5/results.jsonl
```

| file | what |
|---|---|
| `results.csv` | one row per benchmark x arm x switch model, 70 rows, with a header block naming every arm |
| `results.jsonl` | the same runs with every parsed counter, which `analyze.py` reads |
| `analysis.txt` | the arms table, the decomposition and the per-switch table, as printed |
| `provenance.txt` | host, gem5 and compiler revisions, seed file md5, concurrency |
| `crypto_verify_16.txt` | why aes256gcm-decrypt has a dwell term: counters, LVP attribution, and the annotated disassembly with the pass's per-instruction verdict |

argon2id is not here. It runs as a separate stage (one operation is 326M
simulated cycles) and lands in `results.csv` when it finishes.

## How it was measured

gem5 is **this repo's own `gem5-DIT` submodule** at master `ce05d32089`, built
from those sources, on the Neoverse V2 FDP model with `--eves --dmp --comp-simp`.
Arms built by `utils/dit_host_screening/cioparity/build_arms.sh` (the per-TU
`-ftaint-harden` path) with compiler `00f86ba06b5e` at the shipped defaults -
callee contract, DIT twins, intra-block region placement, external-preserves -
against the 188-line contract fixpoint seed file and the derived owned list.
CIO's six drivers are used byte-for-byte and sha256-verified.

All 70 cells ran concurrently. gem5 is deterministic, so there are no
repetitions: a settled region is exact and the 15-rep median the silicon rig
needs is replaced by gates.

### The two switch models

| cfg | flags | what it is |
|---|---|---|
| `spec` | `--eves --dmp --comp-simp` | `msr DIT` is a renamed CC-register write |
| `serdit` | the same plus `--no-speculative-dit` | the write serialises, which is what real silicon does |

Nothing else is passed. In particular no clear-shadow flag:
`--dit-clear-publish` stays at its default `frontier`, which is the policy the
submodule pin already ran (`983bbe2966`, "early publish of the DIT clear"). PR
#108 named that policy and added `commit` / `writeback` / `rename` beside it; it
did not change the default path.

### The arms

| arm | what it is | mode writes / call |
|---|---|---|
| `base` | unhardened, the denominator | 0 |
| `blanket` | DIT set once before `main`, no analysis | 0 |
| `api` | Apple's documented sequence: `mrs` DIT token, `msr DIT,#1`, `isb sy`, the call, a clear only if the token was 0 | 2 |
| `apiisb` | the same without the token read, clearing unconditionally | 2 |
| `apiisbnop` | `apiisb` with `HINT #0` at the barrier's address | 2 |
| `taint` | `-ftaint-harden` at the shipped defaults | 2-32 |
| `taintnop` | `taint` with every switch a `HINT #0` | 0 |

`apidsb` (Apple's no-FEAT_SB fallback, `dsb nsh; isb sy`) is deliberately
absent: every M-series has FEAT_SB, so it is a path no Apple device takes.

Apple's guide specifies `sb`, the speculation barrier, "to ensure that
subsequent instruction timing reflects the updated DIT state". gem5 does not
implement `sb`, so `api` substitutes `isb sy`; `api_bracket.c` carries the real
instruction behind `API_BARRIER_SB` as a raw `.inst 0xd50330ff` for silicon.

`apiisb` exists for a simulator-specific reason. gem5 decodes `mrs DIT` as
`Mrs64`, `IsSerializeBefore` - a full pipeline drain - where an M5 reads it in
about 1 cycle. So `api` charges Apple's bracket for a cost that only exists in
simulation, and `apiisb` is what sizes that overcharge. `apiisb` minus
`apiisbnop` is then the `isb` on its own.

## Reading these numbers

**Subtract each arm's own NOP twin before quoting anything.** `taintnop` is
identical placement, identical instruction count, same addresses, with no mode
switch executing, and it is large and **signed**: -6.33 on aes-gcm encrypt and
-2.34 on ed25519, where the hardened build is genuinely faster with the mode
inert, against +6.10 on chacha decrypt. `taint`'s -6.16% on aes-gcm encrypt is
layout, not a DIT win.

`analysis.txt` prints a per-switch table. Only the chacha rows carry enough
switches to resolve it (~23 cycles serialising, ~0 renamed). Its ed25519 figure
of 332.9 cycles is 2 switches divided into a 78,790-cycle operation - division
noise, not a measurement.

Two cells are inside the layout band and should not be quoted as barrier costs
without a second run: `apiisbnop` on chacha encrypt reads higher renamed
(+7.36) than serialising (+4.63), and `apiisb` sits above `api` on chacha
despite executing strictly fewer instructions.

## Gates

Every gate passed. A gate failure is a hard stop, not a footnote, because a
failed control invalidates everything downstream of it.

- every driver reported the crypto verified
- `base`, `blanket` and `taintnop` are cycle-identical across the two switch
  models - none executes a switch, so the model must not move them
- `simInsts` identical across models for every arm: the same binary doing the
  same work
- `taint` and `taintnop` instruction counts agree within 0.5%, so their
  difference is a pure switch term
- every arm exits with DIT clear except `blanket`
- arms that must toggle committed a nonzero number of DIT writes

Four notes, not failures, are recorded on the AES rows:
`compSimplifier.ditSuppressed` is 0 there, meaning the mode is on but no
DIT-gated optimisation was eligible. That is a real finding rather than an
instrument problem - see `crypto_verify_16.txt`, where the whole cost of the
aes256gcm-decrypt row turns out to be EVES value prediction in one function.
