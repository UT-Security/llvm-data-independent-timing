# Experiment 09 — how to run it

`README.md` in this directory is the paper writeup: what was measured, what it
means, 1300 lines of it. **This file is the operating manual.** Read it before
running anything or changing a rig.

Everything routes through one script:

```sh
./reproduce.sh            # picks the rig from the host
./reproduce.sh silicon    # force Apple silicon (M4/M5)
./reproduce.sh gem5       # force gem5
./reproduce.sh --help
```

---

## What the experiment is

The **negative control of the paper**, run on the closest prior work's own
benchmark suite (CIO), their library, their seeds, their drivers. It is the
f→100% endpoint held still: all crypto, no public lane. The claim it exists to
support is that blanket `PSTATE.DIT` is free here, so every selective placement
is spending switches to recover nothing.

**Read `blanket` first.** If blanket is ~free, that is the result; the pass's
number is the cost of trying anyway. Everything else is detail.

Four arms, each with a NOP twin where it needs one:

| arm | what it is |
|---|---|
| **base** | unhardened, the denominator |
| **blanket** | DIT set once for the whole process, no analysis |
| **Apple bracket** | each public entry point wrapped in Apple's prologue/epilogue, 2 mode writes per call |
| **ExpeDITe** | `-ftaint-harden` at the shipped defaults |

---

## Silicon (M4 / M5)

```sh
./reproduce.sh silicon
```

Toolchain → libsodium source → CIO's drivers → five library arms → run →
report. Every stage skips if its output exists, so a failed run resumes. It
prompts for sudo at the run step (kperf counters).

| knob | effect |
|---|---|
| `LLVM_BIN=<dir>/bin` | reuse a built toolchain; **pass this** or it may build LLVM from scratch (hours) |
| `SKIP_ARGON=1` | drops argon2id: ~60 of the ~70 minutes, and the row least likely to resolve |
| `NO_SUDO=1` | unrooted; ratios stay valid, but you lose the kperf cycle/instruction columns |
| `CIO_REPS=<n>` | default 15, the paper's protocol |
| `PRESET=legacy` | the pre-2026-09-05 arm set on the wllvm archives, for reproducing published numbers |

Nothing is tuned to a part. The residency gate tests `> 4000 MHz` rather than a
clock, and the DIT gate tests **cycles per hop**, not a ratio — that ratio's
ceiling *is* the L1 latency, 4 on M5 and 3 on M4, so a fixed threshold is
unsatisfiable on the shorter one. M5 reads ~4590 MHz / ~3.93×; M4 reads
~4400 / 3.00×. Both pass.

### Counters: check first, and it picks for you

`./reproduce.sh silicon` runs `utils/cio_pmc_check.c` before it measures
anything and selects the counter source from the answer. Run it standalone any
time:

```sh
clang -O1 -o /tmp/pmc_check utils/cio_pmc_check.c && /tmp/pmc_check
```

| source | per-region cost | needs root | when |
|---|---|---|---|
| **PMC** | 1 cycle bare, ~39 `isb`-ordered | **no** | kernel patched with `PMCR0_USEREN_EN` |
| kperf | ~3,400 cycles, ~17,700 instrs | yes | everywhere else |

PMC reads Apple's fixed counters (PMC0 cycles, PMC1 retired instructions)
straight from EL0. It needs a kernel patched by
[PacmanPatcher](https://github.com/jprx/PacmanPatcher) — SIP off, matching KDK,
patched kernel collection, boot into 1TR — so it is **not** the default state of
a Mac. `PMC=1` on an unpatched machine falls back to kperf silently and by
design, which is why the check exists: nothing breaks, and nothing tells you you
did not get what you asked for.

**Why it matters.** kperf's ~3,400 cycles is fine on a 35,000-cycle ed25519
signature and hopeless on a 443-cycle AES-GCM encrypt, where it is several times
the thing being measured. Absolute IPC off those counters is the *instrument's*
IPC: it reads 5.06 where the truth is 2.88. Every percentage is compressed by
the same offset. With PMC the numbers need no correction at all.

**Two traps, both already handled, both worth knowing:**

- **The reads must be `isb`-ordered.** A bare `mrs` of a PMC is not ordered
  against surrounding work, so a region-end read can execute before the code it
  measures has retired. Measured: the Apple-bracket arm read 1,301 instructions
  and its NOP twin read 393 — a 3× gap between two objects that disassemble to
  the same 44 instructions. The bracket's own `sb` serialised the read after it;
  the twin, with `nop` there, had nothing. **The arm with a barrier measured
  itself honestly and its control did not**, which is the worst failure mode a
  layout control has. The shim adds the `isb`; do not remove it to save 39
  cycles.
- **The PMCs are per-core, not per-thread.** `kpc_get_thread_counters()`
  accumulates across a migration; a bare `mrs` does not, so a thread that moves
  mid-region yields a delta spanning two cores. The shim drops absurd deltas and
  counts them — **check `reg_drop` is 0 before trusting a PMC run.**

### Sampled PMC accumulation

The `isb` that makes a PMC read correct is a pipeline drain: ~39 cycles
back-to-back, but 73-250 in a real region, because it waits for what is in
flight. Paid per operation on a 264-cycle AES-GCM encrypt that is 40% more work,
landing between the driver's iterations where it disturbs the very predictor and
cache state being measured -- and it need not disturb every arm equally.

So the PMC counters come from **1 region in `CIO_PMC_SAMPLE`** (default 64),
after skipping `CIO_PMC_SKIP` (default 64) warmup regions. The other 63 in 64
run untouched. Columns: `samp_cyc`, `samp_ins`, `samp_n`, `samp_every`,
`pmc_off`, `reg_drop`. The cheap per-op series (`reg_*`) is unchanged.

**Skipping warmup is not optional.** 1-in-64 over a 1025-iteration driver is
only ~16 samples, so one cold sample dominates the mean -- and region 0 is the
coldest call there is. Measured with no skip, aes256gcm-encrypt reported 2,584
instructions per op against a true 1,275, because sample 0 carried ~20,000
one-time instructions spread over 16 samples.

**Which column to use for what:**

| want | use | why |
|---|---|---|
| cycles per op | `samp_cyc/samp_n` | true cycles, read bare -- no clock conversion |
| instruction counts | `samp_ins/samp_n` | exact |
| **IPC** | `samp_ins`/`samp_cyc` | both from PMC, nothing assumed |
| overhead ratios | either, or `mean_ticks` | ratios need no clock at all |
| switch counts per arm | denser sampling | ~16 samples cannot resolve a 6-instruction delta |

**Cycles are read BARE; instructions are read `isb`-ordered.** The hazard is
asymmetric, and treating the two counters alike is what made this hard for a
while. An early read of the INSTRUCTION counter loses everything not yet
retired -- measured, 5,778 against a true 6,007. An early read of a free-running
CYCLE counter is off by at most the reorder window, and the same skew appears at
both boundaries so it cancels. Measured against CNTVCT from 200 to 2,000,000
iterations of work, a bare PMC0 read holds a constant 4.40-4.43 ratio: it IS the
cycle count, at every scale.

The boundary reads are ordered so the drain falls outside the cycle window on
both sides: instructions first at region entry (its `isb` drains, then the cycle
snapshot is taken after it), cycles first at region exit. Instructions get the
drain, which they need; cycles never pay it.

**This is what removed the last assumed constant.** Converting CNTVCT time into
cycles needed a clock, and there is no single one: measured per benchmark it is
4.246 GHz on argon2id and 4.450 on ed25519, 4.8% apart, and that propagates
straight into any IPC built on a constant. With drained cycles the implied clock
came out as 4.25 / 4.45 / 5.05 / 7.30 GHz across the four benchmarks -- the
7.30 being visibly impossible and the tell that the short rows were drain, not
frequency. Read bare it is 4.28-4.42 everywhere, which is DVFS and nothing else.

**Sampling does not make `samp_cyc` a clean cycle count**, and it was never
going to: a sampled region still contains its own end-`isb`. What it removes is
the perturbation of the other 63 regions, which is the part that could bias an
arm comparison. Measured excess of `samp_cyc` over CNTVCT: +143 cycles on a
275-cycle aes-gcm-encrypt (52%), +48 on chacha (4%), ~0 on the longer arms.

That pattern is the point. The excess is not a fixed instrumentation cost --
it is **lost inter-operation overlap**. A short op in a tight loop overlaps
heavily with its neighbours and forcing full retirement gives that up; a
2,325-cycle op has little to lose. So no constant correction exists, and the two
IPCs above are two real quantities rather than a right and a wrong one.

**Mixing PMC instructions with CNTVCT cycles is legitimate here**, which is not
obvious and was got wrong once in this file. The window objection only bites
when the INSTRUCTION count depends on the window -- that is what made the old
kperf IPC read 12-14 on an 8-wide core, because its instruction window bracketed
extra driver calls. Measured directly, this one does not: 1,804.0 instructions
per op whether instrumented per-op or once around 1000 ops. Prefer throughput
IPC for the paper, since CIO's drivers are a loop and that is what they
measure.

`pmc_off` is the null-region floor measured in that process. It is reported,
never applied: it is a floor, and the real drain lengthens with what is in
flight, so subtracting it under-corrects a busy region.

NOPs do retire on this core (exactly +K per K nops) while costing ~1 cycle per
16, so the NOP layout controls are sound: same retired count, no work. The check
verifies this and fails if it ever stops being true.

## gem5

```sh
CIO=<counter-optimization/cio checkout> ./reproduce.sh gem5
```

`SKIP_ARGON=1` drops the six-hour argon2id stage, `SKIP_ALIGN=1` the two
alignment sweeps. The headline needs neither: ~15 min on 160 cores.

Needs a gem5 carrying two patches that are not upstream (PMULL at size=3,
without which AES-GCM panics on GHASH; and `commit.ditCycles`). It refuses to
start against a stock gem5 rather than quietly producing a result that looks
complete.

---

## Why both rigs exist

They answer different questions, and neither is a check on the other:

- **Silicon** is the only rig that can measure Apple's real `sb` barrier (gem5
  does not implement `sb`), and the only one whose cycles are cycles.
- **gem5** is the only rig that can turn switch serialisation *off* — the
  renamed-`MSR DIT` counterfactual. That is the whole reason this experiment has
  a simulator arm.

Same arms, seeds, drivers and compiler configuration on both, so a number that
differs between them differs because of the machine.

---

## Things that will bite you

**A pre-2026-09-05 toolchain silently builds the wrong arm.** Older clang
accepts `-ftaint-harden` and ignores `-taint-owned-symbols`,
`-taint-dit-contract` and `-taint-dit-clone-seeded`, producing the pre-contract
arm under the name `taint` — a wrong number that looks right. Both rigs now
refuse to start on such a toolchain. Do not work around it.

**The silicon library build is per-TU clang, not the wllvm path.**
`utils/taint_libsodium_eval.sh` builds one whole-library bitcode module, and the
shipped defaults are per-TU concepts with no meaning there: on whole-program IR
there are no unseen callees for the contract, the owned list is a no-op, and the
twins exist to name a clone across a TU boundary. The parity arms come from
`utils/taint_libsodium_arms.sh`. Do not "simplify" one into the other.

**Mach-O symbol naming is a silent failure.** `llvm-nm` prints `_crypto_sign`;
the pass matches the IR name `crypto_sign`. An unstripped owned list matches
*nothing*, so every cross-TU callee is filed as external and the callee contract
degrades to no ownership — with no diagnostic. `taint_libsodium_arms.sh` strips
the leading `_` and dies if the list comes back under 500 entries.

**`HINT #0` disassembles as `nop`.** Grepping for the literal `hint #0` reports
zero on a perfectly NOPed control arm, which reads as a build that never
happened.

**Never edit a rig script while a run is in flight.** bash reads scripts
incrementally; an in-place rewrite shifts byte offsets under the running shell.
Use an atomic replace (write a temp file, `os.replace`) or wait.

**Do not change `PRESET=legacy` or the wllvm archives.** They back the published
M5/M4 numbers in `README.md` and must stay reproducible.

---

## Checking a run

Every gate is fatal to the row it touches; a result with a failed gate is not a
result.

1. **DIT visible** — ditprobe Const goes from ~1 cyc/hop to L1 load-to-use
2. **Negative control** — ditprobe Perm stays flat
3. **P-core residency** — CoreMHz > 4000 on every arm
4. **Mode readback** — DIT at exit is 1 for blanket, 0 for everything else
5. **Resolvability** — between-arm range must clear 3× the worst within-arm MAD
6. **Rotation** — arm order rotates every rep, so drift cannot fake an effect

A row printed `unresolvable` means no arm differs beyond noise. On ed25519 and
argon2id that *is* the finding; do not mine per-switch costs out of those rows.

Re-read a finished run without re-running it:

```sh
OUT=<run dir> python3 utils/taint_libsodium_sudo_report.py
```

---

## Comparing silicon against gem5

gem5's numbers are in `data/gem5_api_bracket.csv`; `cfg=serdit` is the
serialising `MSR DIT`, which is what real silicon does, and `cfg=spec` is the
renamed counterfactual. `dit_writes` there is a **per-ROI total** — divide by
`roi_n` for switches per op.

Expect the *percentages* to differ while the *mechanism* agrees. Measured on an
M4 on 2026-09-05: a serialising `MSR DIT` costs ~26–41 cycles of real silicon
against the ~20–25 gem5 models, but the M4 runs these ops 1.9–4.2× faster in
cycles, so an equal switch cost lands as a much larger fraction. Subtract each
arm's own NOP twin before drawing any conclusion — on chacha decrypt the twin
came in 7% *faster* than base, so the raw number understates the switch cost.

**Open discrepancy, not yet resolved:** argon2id's pass arm measured +0.17% on
M4 against gem5's +7.58%, and the macOS build emits 239 static switch sites
where gem5 reports 364. gem5's argon2id switches are re-asserts after `memcpy`
inside a twin, and the Darwin build does not appear to emit them. Seeds (188),
twins (85) and defined functions (909 vs ~912) all match, so the analysis agrees
and the placement does not. Do not cite the argon2id row from a silicon run
until this is understood.
