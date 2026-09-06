# 01 - Bitcoin Core wallet

**Status: complete, both instruments.** Measured 2026-08-31 on Apple M5
(Mac17,2) and gem5 Neoverse-V2 FDP. **Re-measurement on the current compiler
begun 2026-09-03**: the gem5 half is re-taken (see "Re-measurement" below),
the silicon half is scripted in `reproduce.sh` and waits for the M5.

**Published artifact:** https://claude.ai/code/artifact/692f3b7d-18fe-4707-ab8a-3d5b84478c12
Source: `figures/crossover.html`. To update the page, republish **that URL**
(`Artifact` with `url=...`); publishing the file without the URL creates a
second artifact instead of updating this one.

---

## The claim

> Whether selective `PSTATE.DIT` placement pays is decided by the fraction of
> dynamic computation that is secret - and that fraction is a measurable
> property of a workload, before any compiler work is done.

Demonstrated inside **one unmodified call** into shipped Bitcoin Core wallet
code (`CreateTransaction`), containing a real public lane and a real secret lane
in the order the wallet runs them, with one integer varying the ratio.

## Headline results

| quantity | value |
|---|---|
| pass beats blanket, low f | **-2.89%** at f = 5.6% (1/10 reps slower, p < 0.05) |
| verdict flips | **f ~ 45%** (indistinguishable), blanket wins outright by f = 61% |
| blanket DIT costs, silicon | **-3.7% to -4.0% of IPC**, flat across f = 3.5%..75.2% |
| blanket DIT costs, gem5 | IPC **1.9559 -> 1.7737**, cycles **+10.27%** on the coin-selection kernel |
| the same at 4x the work | IPC **1.9492 -> 1.7578**, cycles **+10.89%** - the prize is not a one-shot artifact |
| the two lanes in one flow, gem5 (2026-09-03) | **the pass beats blanket through f = 54% under both switch models**, by 3.5 points renamed and 2.8 serialising at that f; beyond it the margins (down to 2.0 and 0.8 at f = 83%) sit inside gem5's code-placement floor, so the crossover lies above 54% where silicon's is at 45 to 50% |

**The mechanism**: blanket's cost is flat in f because it protects everything
either way; what grows is the pass's own cost over the secret lane it protects
(`pass vs nop`, +0.20% -> +6.08%). That column is the switch PLUS DIT dwell,
not the toggle bill alone: the gem5 flow isolates serialisation at +1.2% at
K = 400 (known limit 8). The crossover is a fixed prize meeting a growing cost.

## What is public and what is secret

| lane | code | why |
|---|---|---|
| **public** | `AvailableCoins`, the four coin-selection solvers, `CalculateMaximumSignedTxSize` (dummy signer), serialization | never reads the private key; amounts and UTXOs are on-chain data |
| **secret** | `CKey::Sign` -> `secp256k1_ecdsa_sign`, once per input | operates on the private key |

Taint seeds are the **pointee of the private-key pointer argument** at nine
entry points (`../../utils/dit_host_screening/btc/seed9.txt`), not whole
functions.

The public lane's prize is attributed by gem5 feature isolation entirely to
**EVES**, the value predictor: coin selection is a serial value-dependent
dependence chain, and DIT switches the predictor off.

## Contents

| path | what |
|---|---|
| `table1.md` | Table 1 - the crossover, with column definitions and caveats |
| `ipc.md` | what blanket DIT costs in IPC, silicon and gem5, and the method |
| `data/wallet_m5_ipc.csv` | 320 rows = 4 knob points x 8 arms x 10 reps (M5) |
| `data/wallet_sweep_20rep.csv` | 1280 rows = 8 knob points x 8 arms x 20 reps (M5) |
| `figures/crossover.html` | source of the published artifact above |
| `figures/plot_crossover.py` | **Figure 1** generator; asserts its numbers against `table1.md` |
| `figures/crossover.pdf` | Figure 1, vector, for `\includegraphics` |
| `figures/crossover.png` | Figure 1, 300 dpi raster preview |
| `figures/crossover.tex` | the `figure*` block and caption, ready to `\input` |
| `reproduce.sh` | both instruments from the committed sources: clang, arms, sweep, ipc, derive, figures (macOS) and gem5 (Linux) |
| `data/gem5/*.csv` | the 2026-09-03 gem5 re-take on the current compiler: `coinsel_repro`, `coinsel4_repro`, `sign_repro` (one row per arm, model and offset) |
| `data/gem5/flow_runs.csv`, `flow_derived.csv` | the two lanes in one flow under gem5: 270 runs, and the per-K medians (`btc_flow_gem5.py`) |
| `data/gem5/sign_code_placement.csv` | the sign arms relinked behind six pad sizes: the code-placement floor (known limit 7) |
| `figures/gem5-flow-crossover.{png,pdf}` | the flow's crossover under both switch models beside silicon's, and each arm's cost (`utils/dit_host_screening/btc/fig_flow_gem5.py`) |
| `data/derived/` | what `table_wallet_sweep.py` and `ipc_wallet_sweep.py` print for the committed CSVs, for diffing a re-take against `table1.md` |
| `data/provenance.txt` | host, date and the LLVM / Bitcoin Core / gem5-DIT commits behind each stage that has been run |

## Figure 1

Two panels sharing the x-axis: **(a)** the result, selective placement against
blanket DIT crossing zero at $f \approx 51\%$ with the sign test unable to
resolve $f = 45\%$; **(b)** the mechanism, a flat public-lane prize meeting a
growing cost (the switch plus DIT dwell over the secret lane, known limit 8),
crossing at $f \approx 43\%$. Panel (b) is the
argument for panel (a) - the claim is drawn, not asserted.

    python3 paper_experiments/01-bitcoin-core-wallet/figures/plot_crossover.py

Regenerating writes `crossover.pdf` and `crossover.png`. The script recomputes
every cell with the same definitions `table_wallet_sweep.py` uses and **asserts
them against the values committed in `table1.md`**, so the figure cannot drift
from the table; a mismatch is a failed assertion, not a wrong plot. Error bars
are the order-statistic confidence interval for the median implied by the same
sign test the table reports, so bars and stars cannot disagree. Colors are the
validated categorical slots 1-2 (blue/orange, worst-pair CVD $\Delta E$ 24.7),
and the two series in (b) also differ by dash pattern, so the panel survives
grayscale printing.

## Reproducing

One script, both instruments, from the committed sources:

```sh
paper_experiments/01-bitcoin-core-wallet/reproduce.sh            # every stage this host can run
paper_experiments/01-bitcoin-core-wallet/reproduce.sh clang arms  # macOS: rebuild the compiler, then the three bench_bitcoin arms
paper_experiments/01-bitcoin-core-wallet/reproduce.sh sweep ipc   # macOS, exclusive M5, ~1.5 h
paper_experiments/01-bitcoin-core-wallet/reproduce.sh gem5        # aarch64 Linux with gem5-DIT built
```

The `arms` stage rebuilds the existing `build-{nodit,gated,nop}-v2` dirs with
the rebuilt clang (ccache off), prints each dir's cached compiler and flags, and
checks the switch counts read nodit 0 / gated >0 / nop 0. It refuses to run if
the `BTC_BENCH_*` knobs are missing from `src/bench/wallet_create_tx.cpp`: they
are an uncommitted patch on the M5 and must be restored (and committed) first.
The individual commands it wraps:

Silicon. **Requires an exclusive machine** - nothing else may run during a
native timing measurement.

```sh
BTC_SWEEP_INPUTS=1,4,100,400 BTC_OUT=wallet_m5_ipc.csv \
  python3 utils/dit_host_screening/btc/run_wallet_sweep.py 10 1

python3 utils/dit_host_screening/btc/table_wallet_sweep.py \
  paper_experiments/01-bitcoin-core-wallet/data/wallet_m5_ipc.csv
python3 utils/dit_host_screening/btc/ipc_wallet_sweep.py \
  paper_experiments/01-bitcoin-core-wallet/data/wallet_m5_ipc.csv
```

gem5, for absolute IPC. **Pass the arguments explicitly** - the driver's
defaults (`--iter 50 --warmup 2 --targets 10`) are ~500x the work and do not
reproduce these numbers:

```sh
python3 utils/dit_host_screening/btc/btc_gem5.py --bench coinsel \
  --configs spec,serdit --iter 1 --warmup 1 --targets 1 --tag coinsel_repro
```

## The four arms on the flow (gem5, 2026-09-05)

The same four arms as experiments 09 and 02, on the wallet flow (coin
selection, then one `CKey::Sign` per input, K inputs, one ROI): **base**
(libsecp256k1 through the pass with an empty seed, coin selection plain C++,
as before), **blanket** (DIT set once before main), the **Apple bracket**
(base with the two entry points `CKey::Sign` hands the private key,
`secp256k1_ecdsa_sign` and `secp256k1_ec_pubkey_create`, wrapped in Apple's
prologue and epilogue: read the previous DIT state, `msr DIT, #1`,
speculation barrier as `isb sy`, the call, clear only if it was clear; the
same `api_bracket.c` as the libsodium rigs, the linker's `--wrap`), and
**ExpeDITe** at the shipped defaults (callee contract, twins, intra-block
placement) with `seed_flow_contract.txt`, which is `seed9.txt` plus the
nonce-derivation hops the contract needs, and the owned list. Blanket and
the bracket under the serialising `MSR DIT`, which is what an M4 or M5 does;
ExpeDITe under both. Cycles per flow against base, median over five
`argv[0]` offsets, 450 runs, every gate passing (one checksum per K and
offset across all five arms and both models; base and the bracket's NOP
twin identical across models). Data: `data/gem5/flow_four_runs.csv`,
`flow_four_derived.csv`.

| K (inputs signed) | f secret | base cycles/flow | blanket | Apple bracket (switches/flow) | ExpeDITe, serialising (switches/flow) | ExpeDITe, renamed |
|---|---|---|---|---|---|---|
| 0 | 0.0% | 29,883,476 | +7.04% | +0.32% (0) | +0.27% (0) | +0.14% |
| 1 | 1.4% | 30,304,427 | +6.44% | -0.34% (4) | -0.41% (156) | -0.44% |
| 4 | 3.8% | 31,056,157 | +6.53% | -0.08% (16) | -0.21% (630) | -0.24% |
| 10 | 10.4% | 33,347,933 | +5.93% | -0.37% (52) | -0.19% (1816) | -0.29% |
| 25 | 22.4% | 38,526,136 | +5.14% | -0.55% (134) | +0.21% (4599) | -0.23% |
| 50 | 37.0% | 47,466,535 | +3.93% | -1.13% (274) | +0.30% (9289) | -0.22% |
| 100 | 54.2% | 65,188,312 | +2.68% | -1.38% (554) | +0.65% (18645) | -0.17% |
| 200 | 70.5% | 101,394,298 | +1.19% | -2.00% (1122) | +0.41% (37545) | -0.53% |
| 400 | 83.0% | 176,013,120 | +0.45% | -2.29% (2316) | +0.74% (76534) | -0.46% |

**Reading.**

- **Blanket's cost is the public lane's prize, diluted.** +7.0% at K = 0,
  where the flow is coin selection alone, falling to +0.45% at K = 400 as
  signing takes over the flow. Same shape as the 2026-09-03 re-take.
- **The bracket is never above the layout band and is negative from K = 1.**
  Two serialising switches per bracketed call (4 per input, since
  `CKey::Sign` brackets the signature and the pubkey check; 2,316 per flow at
  K = 400) never show as a cost, because DIT on across a libsecp256k1
  signature is FASTER in gem5 than DIT off: the bracket beats its own NOP
  twin by 25k to 2.7M cycles per flow, about 2,400 cycles per bracketed
  call, and blanket shows the same thing from the other side (its K = 400
  cost is 1.3M cycles less than the public-lane prize alone). That is the
  value predictor mispredicting on the constant-time bignum code when it is
  allowed to run, a gem5 property already recorded for the signing lane
  ("no prize, and a switch cost that one bench cannot resolve"); on the M5
  the same lane is the one where DIT costs the safegcd inversion 23%
  (`utils/dit_inv_bench_README.md`), so this column does not transfer to
  silicon as a win. What does transfer: a bracket around the secret entry
  points touches nothing in the public lane, so it pays none of blanket's
  prize at any K.
- **ExpeDITe lands within +0.7% of base at every K under either model**,
  below blanket through K = 100 (f = 54%) on the serialising model and at
  every K on the renamed one. Its serialising cost is its switch count:
  156 per flow at one input rising to 76,534 at 400, about 190 per input,
  which at ~30 cycles each is the +0.7%. Those are re-asserts inside the
  library's twins after `memcpy`/`memset` and the calls the tables reach;
  `-taint-dit-external-preserves` would remove most of them and is not in
  this table, since it is opt-in.

**With `-taint-dit-external-preserves`** (the `taintx` arm, its library
`obj/taintx`, 90 more cells, gates pass):

| K | f secret | blanket | Apple bracket (sw/flow) | ExpeDITe, serialising (sw/flow) | ExpeDITe, renamed | + external-callee flag, serialising (sw/flow) | + flag, renamed |
|---|---|---|---|---|---|---|---|
| 0 | 0.0% | +7.04% | +0.32% (0) | +0.27% (0) | +0.14% | +0.33% (0) | +0.15% |
| 1 | 1.4% | +6.44% | -0.34% (4) | -0.41% (156) | -0.44% | -0.51% (144) | -0.46% |
| 4 | 3.8% | +6.53% | -0.08% (16) | -0.21% (630) | -0.24% | -0.21% (582) | -0.29% |
| 10 | 10.4% | +5.93% | -0.37% (52) | -0.19% (1,816) | -0.29% | -0.15% (1,624) | -0.40% |
| 25 | 22.4% | +5.14% | -0.55% (134) | +0.21% (4,599) | -0.23% | +0.08% (4,095) | -0.27% |
| 50 | 37.0% | +3.93% | -1.13% (274) | +0.30% (9,289) | -0.22% | -0.06% (8,245) | -0.48% |
| 100 | 54.2% | +2.68% | -1.38% (554) | +0.65% (18,645) | -0.17% | +0.16% (16,521) | -0.60% |
| 200 | 70.5% | +1.19% | -2.00% (1,122) | +0.41% (37,545) | -0.53% | -0.10% (33,213) | -0.95% |
| 400 | 83.0% | +0.45% | -2.29% (2,316) | +0.74% (76,534) | -0.46% | +0.20% (67,438) | -0.84% |

The flag removes 12% of the pass's executed switches here (76,534 -> 67,438
per flow at K = 400) and takes the serialising column from +0.74% to +0.20%;
the per-switch cost by subtraction stays 24 to 30 cycles. It is not the
lever it was on libsodium, and the library's re-assert report
(`data/gem5/flow_secp_reassert_report.txt`) says why: of its 56 re-assert
sites, 49 follow an INDIRECT call (the nonce function is a function pointer,
`noncefp`, and so are the context's callbacks; an indirect call is never
redirected to a twin), 2 follow `memcpy` (in `secp256k1_sha256_write`, the
two sites the flag removed, executed once per hash block) and 5 follow in-TU
callees. The rest of the ~170 switches per input are the entry and exit
toggles of instrumented originals that the PUBLIC verify path calls from
DIT-off code: `secp256k1_gej_add_ge_var` (11 sites),
`secp256k1_ge_set_all_gej_var` (4), the rfc6979 and sha256 helpers, which
the analysis instruments because they handle a secret somewhere in the TU
and then toggles wherever they are called, `secp256k1_ecdsa_verify`
included, which never sees one. That is the context-insensitivity cost the
design docs already name as the dominant one; the levers here are a twin
for the nonce callback and a per-call-site answer for the `_var` helpers.

The bracket against its NOP twin, for the record:

| K | Apple bracket, serialising | its NOP twin | bracket minus twin, cycles per flow |
|---|---|---|---|
| 0 | +0.32% (0) | +0.32% | +0 |
| 1 | -0.34% (4) | -0.25% | -24,944 |
| 4 | -0.08% (16) | -0.07% | -3,128 |
| 10 | -0.37% (52) | -0.21% | -54,751 |
| 25 | -0.55% (134) | -0.15% | -153,008 |
| 50 | -1.13% (274) | -0.49% | -301,830 |
| 100 | -1.38% (554) | -0.43% | -619,207 |
| 200 | -2.00% (1122) | -0.70% | -1,315,077 |
| 400 | -2.29% (2316) | -0.73% | -2,743,476 |

**Provenance.** LLVM `dit-tainter` at the 2026-09-05 defaults (code of
`9d596a3c2858`); Bitcoin Core `15a7a4ed7`; gem5-DIT from the rig worktree
(before master's early-clear change, #106), the same generation as every
other table taken that day; host beckham, arms built natively. The pass
library has 114 `msr DIT` sites, the bracket binary four.

## Re-measurement on the current compiler, 2026-09-03: the gem5 half

**Why.** The 2026-08-31 arms came from a compiler of 2026-08-18 and a seed
file that no longer exists (`../README.md`, "Compiler changes"). This re-take
rebuilds the gem5 arms from the committed `seed9.txt` on the current compiler
and runs them with the recorded arguments. **Table 1 is not touched by it**:
the crossover is a silicon result, and the silicon stages of `reproduce.sh`
wait for the M5.

**Setup.** LLVM `fdfdef5e15b1` (the `dit-tainter` tip); Bitcoin Core
`15a7a4ed7` (master, 2026-08-18 - the M5 tree's commit was never recorded;
from this commit to 2026-09-03 master changed two lines of the coin-selection
link closure and nothing in `src/secp256k1`); gem5-DIT `493bc1f0b8`. Host:
an aarch64 Linux box without FEAT_DIT, the arms built natively
(`util/cross/taint-native-cc`) and run only under gem5. Driver arguments as
recorded: `coinsel` 1/1/1, `coinsel4` 1/1/4, `sign --iter 32`. **Every
number is a median over 5 `argv[0]` lengths** and carries its spread
(max/min - 1 over the offsets), the rule experiment 02 follows: gem5 SE puts
the binary path on the initial stack, so its length moves every stack address,
and a single-offset delta under 2% is layout, not DIT. All gates pass at every
offset on every bench: `simInsts` identical across switch models,
`ditSuppressed` 0 in base and plain, base/plain cycles identical across
models, one checksum per bench. The two lanes' kernels now live in
`coinsel_kernel.h` and `sign_kernel.h` in gem5-DIT, shared with the flow driver
below, and the numbers here are from the drivers rebuilt on those headers.
Raw rows: `data/gem5/*.csv`.

### Coin selection: the prize reproduces, at two thirds the recorded size

| run | arm | cycles | IPC | blanket vs base | spread |
|---|---|---|---|---|---|
| `coinsel`, 2026-08-31, Mac cross build, 1 offset | base | 30,522,406 | 1.955873 | | |
| | blanket | 33,656,493 | 1.773742 | +10.27% cycles, -9.31% IPC | |
| `coinsel`, 2026-09-03, this re-take, 5 offsets | base | 29,765,977 | 1.9892 | | 0.20% |
| | blanket | 31,723,057 | 1.8665 | **+6.57% cycles, -6.17% IPC** | 0.26% |
| `coinsel4`, 2026-08-31, Mac cross build, 1 offset | base | 112,404,358 | 1.949240 | | |
| | blanket | 124,647,767 | 1.757778 | +10.89% cycles, -9.82% IPC | |
| `coinsel4`, 2026-09-03, this re-take, 5 offsets | base | 109,449,516 | 1.9839 | | 0.18% |
| | blanket | 117,746,897 | 1.8441 | **+7.58% cycles, -7.05% IPC** | 0.19% |

Blanket DIT still costs the coin-selection kernel its value predictions
(2.7 million suppressed operations), identically under both switch models
because the single switch sits in a constructor before the ROI, and the
spread over offsets is a tenth of the effect. The prize is present and in the
same direction; its magnitude is two thirds of the recorded one at one
selection and 0.70x at four. The work-scaling control behaves as it did on
the recorded build: four selections cost 3.68x the cycles of one (record:
3.68x) and the prize grows with them, +7.58% against +6.57% where the record
had +10.89% against +10.27%, so the prize lives in the selection loop and not
in one-shot setup on this build too. **The two
binaries are not the same program to the instruction**: 59.2 million retired
against 59.7 million, a 0.8% gap from a different static glibc and libstdc++,
a Linux-generated config header and a later compiler. Which of those moves a
value-predictor prize by three points is not separated here (known limit 6);
the honest statement is "reproduces at +6.6% on an independent build", not
"the compiler cost three points".

### ECDSA signing: no prize, and a switch cost that one bench cannot resolve

32 signing iterations, both switch models, 5 offsets. **The baseline is the
library built through the pass with an empty seed**, and blanket is that same
library plus the DIT constructor - the arrangement the silicon rig has always
used (`baseline` = `build-nodit-v2`, `always` = the same binary with
`dit_on.dylib`). The gem5 sign rig had built both from a stock `-O2` library
instead; that was harmless while the empty-seed build was byte-identical to
stock, and stopped being harmless on 2026-09-01, when the tail-call disable
began riding on `-ftaint-harden`. It was changed for this re-take. Stock
`-O2` is kept as `plain`, a reference for what building through the pass
costs and never a baseline.

| arm | cycles, median | IPC | vs base | spread | committed `msr DIT` | non-spec stalls |
|---|---|---|---|---|---|---|
| base (empty seed, through the pass) | 11,487,058 | 2.7297 | | 0.75% | 0 | 2,199 |
| blanket, renamed switch (`spec`) | 11,632,105 | 2.6956 | +1.26% | 0.68% | 0 (constructor, before the ROI) | 2,199 |
| blanket, serialising switch (`serdit`) | 11,638,529 | 2.6942 | +1.32% | | 0 (constructor, before the ROI) | 2,199 |
| taint, renamed switch (`spec`) | 11,751,233 | 2.6687 | **+2.30%** | 1.55% | 4,622 | 2,199 |
| taint, serialising switch (`serdit`) | 11,802,628 | 2.6571 | **+2.75%** | | 4,622 | 6,821 |
| plain (stock `-O2`, reference only) | 11,316,294 | 2.7655 | -1.49% | 0.67% | 0 | 2,199 |

Three readings:

1. **There is no prize on signing; the pass's own cost is inside gem5's
   code-placement floor.** Blanket costs +1.3% (positive at all five
   offsets, +0.6% to +1.4%; the 2026-08-31 run read -0.6% at one offset).
   Blanket and base are the same code at the same addresses, so that number
   is resolved. The taint arm is a different binary, and
   `code_placement_probe.sh` in gem5-DIT - relinking base and taint behind
   six pad sizes from 0 to 8 KB, nothing else changed
   (`data/gem5/sign_code_placement.csv`) - moves base by 0.9%, taint by 2.9%,
   and **taint vs base from -3.1% to +2.3%**. The +2.3% / +2.75% in the table
   is one placement of seven. gem5 cannot resolve a pass-vs-base or
   pass-vs-blanket difference on this kernel below about 3 points; silicon's
   +4.12% loss (`../../docs/results/dit-bitcoin-sign-two-instruments.md`) is
   outside that floor, gem5's reading of the same comparison is not. Known
   limit 4 stands, and limit 7 is new.
2. **The switch's own cost cannot be read off this bench.** Serialising minus
   renamed on the taint arm is 1.6, 45.3, 23.2, 14.9 and 9.7 cycles per
   committed switch across the five offsets: 4,622 switches in an 11.5
   million cycle run put the whole effect inside the layout spread. An earlier
   draft of this section quoted 26.4 cycles from one offset; that number was
   layout. The flow below has up to 59,000 switches per run and resolves it
   at **31 to 37 cycles per switch**.
3. **Building through the pass costs 1.5% before any DIT is placed.** `base`
   retires 60,260 more instructions than `plain` and takes +0.9% to +2.2%
   longer across offsets (median +1.5%): the tail-call disable that rides on
   `-ftaint-harden`. That cost is in the baseline and in blanket alike, so the
   taint arm's cost above is placement plus switches and nothing else.
   Documents that call the empty-seed control "byte-identical to plain -O2"
   describe the state before 2026-09-01.

The pass protects 5.3 million operations against blanket's 10.8 million: the
driver's per-iteration `ecdsa_verify` is public and is correctly left clear.

### The two lanes in one flow: the pass wins through f = 54% on gem5 under either switch, and the rest is inside the floor

`btc_flow_gem5.cpp` puts the same two lanes in one ROI in the wallet's order -
coin selection, then one `CKey::Sign` per input - with K, the inputs signed,
as the only knob, exactly as Table 1 does on silicon. K = 0 runs the public
lane alone, so `f_secret` is measured the way `BTC_BENCH_SIGN=0` measures it.
Arms as above (base = empty seed, blanket = base + constructor, taint = the
pass); both switch models; 5 offsets per cell; 270 runs; every gate passes at
every K and offset. Runner: `utils/dit_host_screening/btc/btc_flow_gem5.py`;
rows and derived table: `data/gem5/flow_runs.csv`, `data/gem5/flow_derived.csv`;
figure: `figures/gem5-flow-crossover.{png,pdf}` (`fig_flow_gem5.py`, which
takes its palette and the silicon curve from `plot_crossover.py`).

| K | f_secret | base cycles | blanket vs base | pass vs blanket, renamed [min..max] | pass vs blanket, serialising [min..max] | switch cost | spread |
|---|---|---|---|---|---|---|---|
| 0 | 0% | 29,990,792 | +6.71% | -6.14% [-6.40..-5.78] | -6.25% [-6.57..-5.94] | | 0.76% |
| 1 | 1.4% | 30,423,866 | +6.09% | -6.11% [-6.48..-5.99] | -6.24% [-6.46..-6.19] | -0.14% | 0.82% |
| 4 | 3.5% | 31,081,950 | +6.48% | -6.20% [-6.24..-6.02] | -6.00% [-6.24..-5.74] | +0.21% | 0.60% |
| 10 | 10.1% | 33,368,502 | +5.87% | -5.68% [-5.96..-5.50] | -5.60% [-5.82..-5.49] | +0.09% | 0.49% |
| 25 | 22.3% | 38,590,190 | +4.98% | -5.16% [-5.24..-5.11] | -4.86% [-4.95..-4.79] | +0.31% | 0.33% |
| 50 | 36.9% | 47,489,629 | +3.94% | -4.37% [-4.46..-4.32] | -3.94% [-3.98..-3.82] | +0.45% | 0.29% |
| 100 | 54.0% | 65,244,544 | +2.71% | -3.48% [-3.67..-3.44] | -2.83% [-2.95..-2.73] | +0.68% | 0.22% |
| 200 | 70.4% | 101,303,138 | +1.40% | -2.55% [-2.83..-2.28] | -1.69% [-1.77..-1.44] | +0.88% | 0.51% |
| 400 | 83.0% | 176,192,522 | +0.41% | **-1.97%** [-2.03..-1.67] | **-0.79%** [-0.87..-0.48] | +1.21% | 0.44% |

Medians over 5 offsets; `[min..max]` is pass vs blanket at each offset;
`switch cost` is the taint arm serialising vs renamed; `spread` is the largest
max/min - 1 over offsets of any arm. Negative pass vs blanket = the pass wins.

**Readings.**

1. **The knob spans the same range as silicon's.** One input is 1.4% secret,
   400 inputs are 83%; Table 1's K runs 3.7% to 75%. The selection kernel is
   29.99 million cycles and one `CKey::Sign` sequence about 365,000, so
   `f_secret` follows K the same way on both instruments.
2. **Blanket's cost dilutes with f, from +6.7% to +0.4%**, because it costs
   on the selection it protects needlessly and nothing on the signing it
   protects legitimately. On silicon `C_whole` stays flat at 3 to 4% because
   there blanket costs the secret lane too (implied `C_secret` +4.5%); on
   this model signing under DIT is close to free (+1.3% above). That is the
   first of the two reasons the gem5 curve differs from the silicon one.
3. **The pass beats blanket wherever gem5 can resolve the comparison, under
   both switch models, and the switch decides by how much.** The taint arm
   is a different binary from base and blanket, so its margins carry the
   code-placement floor of known limit 7, about 3% of the signing work: some
   0.3 points of the flow at f = 10%, 1.6 at f = 54%, 2.5 at f = 83%. The
   margins through f = 54% - 6.1 to 3.5 points renamed, 6.2 to 2.8
   serialising - clear it; those at f = 70% and 83% (2.5 and 2.0 renamed,
   1.7 and 0.8 serialising) do not. So **the crossover, if the flow has one,
   lies above f = 54% under both switch models**, where silicon's lies at 45
   to 50%; its position beyond that is not resolved on gem5 with one binary
   per arm. What is resolved on the same-binary column: the serialising
   switch bill grows with K to +1.2% of the flow at K = 400, from 59,292
   committed switches, which is the whole of the gap between the two models.
   Silicon crosses earlier because its switch costs more and its blanket
   costs the secret lane: the crossover's position is a property of the
   switch implementation and of what DIT costs the secret lane, not of the
   lanes themselves. This is the counterfactual Table 1's caveat 5 asked
   for, with the resolution it comes at.
4. **The serialising switch costs 31 to 37 cycles.** Serialising minus renamed
   on the taint arm, divided by its committed switches, reads 34.8, 36.5,
   34.1, 31.3 and 36.6 cycles at K = 25, 50, 100, 200 and 400 (medians over
   offsets; the per-offset range at K = 100 is 30.9 to 34.6). Below K = 25 the
   switch count is too small to resolve against layout. Experiment 02
   measured 20 to 25 cycles on the same model with 49 switches per request;
   experiment 09 measured 40 to 45 on the M5.
5. **What it does not show.** The public lane here is the selection kernel,
   not the wallet call: `AvailableCoins`, the dummy-signed size estimate and
   serialisation are absent, so blanket's +6.7% at K = 0 is the kernel's
   prize, where silicon's whole-call prize is 3 to 4% against 13% on the
   kernel alone. The curve's shape and the switch counterfactual transfer;
   its absolute margins do not. Silicon remains the magnitude instrument.
   And every column that compares the taint arm to another binary carries
   limit 7; a link-placement sweep of the flow, as the probe does for the
   sign bench, is the remedy and has not been run.

### Rig defects found by this re-take, all fixed

- `btc_gem5.py` ran every arm at one `argv[0]` length. Its deltas on the sign
  bench are all under 2%, and the taint arm's serialising-minus-renamed gap
  moved from 121,861 to 7,277 cycles when only the driver's code layout
  changed. It now takes `--offsets N` (and `--resume`), stages each offset at
  a path one byte longer, gates every offset, and reports medians with the
  spread, as `run_gem5.py` in experiment 02 does.
- No rig sampled code placement. `code_placement_probe.sh` in gem5-DIT now
  does for the sign arms, and it found the floor recorded as known limit 7:
  the spread it exposes is wider than any pass-vs-base effect the sign bench
  measures. It is a probe, not yet a sweep the runners apply.
- The gem5 sign rig's baseline and blanket were built from stock `-O2`, not
  from the empty-seed build the silicon rig baselines on. Both now come from
  the empty-seed library; stock `-O2` is the separate `plain` reference arm.
- `btc_gem5.py` staged every arm at an equal-length path keyed on arm and
  config alone, so two drivers running at once re-linked each other's
  binaries; a sign run loaded the coin-selection binary. The slot now hashes
  the run directory too (still 8 hex characters, so gate 3 is unchanged).
- Its `ditSwitches` column matched no stat and read 0 on every arm; it now
  reads `commit.ditWrites`, the committed count.
- It now records `iter/warmup/targets`, `numInsts` and gem5's own `ipc`
  (`ipc.md` section 5 asked for both).
- `util/cross/objdump_dit.sh` defaulted to a macOS path and printed 0 for
  every arm when that objdump was missing; it now takes `LLVM_BUILD` and fails
  loudly.
- `ipc_wallet_sweep.py` still printed "absolute IPC is NOT measurable" and
  the pre-argv[0]-gate gem5 numbers that `ipc.md` retracted on 2026-09-01.

## Validity gates, all passed

in-band `lvp_chase` **3.99x** · noise floor -0.00%..+0.30% · harness null
-0.04%..+0.34% · NOP arm carries zero `msr DIT` · arithmetic closes (implied
C_secret +4.52%, spread 0.60 pp vs +3.39% measured) · gem5 `simInsts` identical
across switch models, `ditSuppressed = 0` in base · equal-length `argv[0]`.

## Known limits

1. **Region count and secret fraction are confounded** - each signature adds
   both. Separating them needs the `BTC_BENCH_CHAIN` axis; not yet run.
2. **The region boundary is inside libsecp256k1**, per `secp256k1_ecdsa_sign`
   call, not at the C++ `SignTransaction` phase boundary the design doc
   describes - this build instruments C only. Measurement valid, prose wrong.
3. **Closure is acceptable, not tight** (+4.52% vs +3.39%).
4. **gem5 cannot confirm magnitude here** - it reads ~zero on ECDSA signing.
5. **Older `coinsel` / `coinsel4` gem5 stats predate the equal-length-path
   gate.** Both have been re-taken (`coinsel_repro`, `coinsel4_repro`); quote
   the gated numbers in `ipc.md`, not the originals in `gem5-btc/coinsel*/`.
6. **The 2026-09-03 gem5 re-take is a different binary, not only a different
   compiler.** It was built natively on Linux against that host's static glibc
   and libstdc++ and a Linux-generated `bitcoin-build-config.h`; the 2026-08-31
   arms were cross-compiled on the Mac against a Debian sysroot. The two differ
   by 0.8% in retired instructions on the coin-selection kernel, so a shift in
   the gem5 prize between them cannot be attributed to the compiler alone.
7. **gem5's model is sensitive to code placement at the percent level on
   the signing kernel.** Relinking the sign arms behind pads of 0 to 8 KB
   moves base by 0.9% and taint by 2.9%, and taint vs base from -3.1% to
   +2.3% (`data/gem5/sign_code_placement.csv`; `code_placement_probe.sh` in
   gem5-DIT). `argv[0]` offsets move the stack, not the code, and do not
   sample this. Same-binary comparisons (blanket vs base, serialising vs
   renamed) are immune; every comparison against the taint arm carries it,
   in this experiment's sign bench and its flow alike. A link-placement
   sweep is the remedy and has not been run.
8. **Table 1's last column is not the switch cost.** It is `pass/passnop`, and
   `passnop` emits `HINT #0` in place of every inserted `msr DIT`, so
   `PSTATE.DIT` is never set and that arm loses the PROTECTION along with the
   switch. `pass - passnop` is therefore the switch plus DIT dwell over the
   protected regions, and the gem5 flow above isolates serialisation alone at
   +1.21% at K = 400 against the +5.98% that column reports on silicon. The
   crossover is unaffected - `pass vs blanket` compares two arms that both
   protect - but the column was named `switch cost` from 2026-08-31 to
   2026-09-02 and is now `pass vs nop`.
