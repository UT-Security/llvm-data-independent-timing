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
