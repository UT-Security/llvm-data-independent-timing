# Upstream gem5 cannot model DIT at all

**Measured 2026-08-18.** Upstream gem5 master `3b60ac6`, built at
`~/Documents/gem5-upstream` (`build/ARM/gem5.opt`).

Answers the standing reviewer question "why does this work need a gem5 fork?"
with a hard result rather than an assertion.

---

## 1. `MSR DIT` is an unknown instruction upstream

Running the coverage driver (`utils/dit_host_screening/gem5cov/cov_driver.c`)
in the arm that executes `msr DIT, #1`:

```
src/arch/arm/faults.cc:785: panic: Attempted to execute unknown instruction (inst 0xd503415f)
```

`0xd503415f` was confirmed by assembling it with the project's own clang:

```
$ echo 'msr DIT, #1' | clang -c -target aarch64-linux-gnu -march=armv8.4-a -
0: d503415f    msr DIT, #0x1
```

So it is exactly the enable. **FEAT_DIT is not implemented in upstream gem5** -
a binary that touches PSTATE.DIT cannot run there at all, let alone be measured.

Confirmed from the source side too: `compSimplifier`, `ditSuppressed` and
`EVESValuePredictor` appear nowhere in upstream `src/`. The DIT gate, the
computation simplifier and the EVES value predictor are all fork-only.

## 2. The fork does not perturb the workload

The same static binary, in the arm that executes no DIT instructions:

| | simInsts |
|---|---|
| fork (`gem5-DIT`, `--eves --dmp --comp-simp`) | 13,268,748 |
| **upstream master** (`se.py --cpu-type=O3CPU --caches --l2cache`) | **13,268,748** |

Exact agreement. Whatever the fork adds, it does not change what the program
executes, so the fork's DIT modelling is additive rather than a different
machine.

## 3. Consequences

- **Every DIT number in this project necessarily comes from the fork.** There is
  no upstream cross-check available for any of them, because the instruction
  under study does not exist upstream. This is a real limitation and belongs in
  the paper's threat-to-validity section, not as a footnote.
- **What upstream CAN validate is the non-DIT baseline**, and it does, exactly.
  That is worth stating: it rules out "the fork's baseline is wrong" as an
  explanation for any result.
- Anyone reproducing this work needs the fork. `gem5.fast` in the fork was
  rebuilt 2026-08-16 and reproduces the fork's own coverage numbers
  bit-identically (`dit-pass-vs-oracle.md` §7).

Logs: `utils/dit_host_screening/gem5cov/outup/`.
