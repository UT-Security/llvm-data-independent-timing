# Host screening: five real applications, always-on DIT cost on M5

**Measured 2026-08-14**, Apple M5, native. Rig: `utils/dit_host_screening/`.
This executes step 1 of `docs/research/workload-candidates.md` - screen hosts by
always-on DIT cost *before* paying any instrumentation cost, because that number
is the entire prize fine-grained placement can ever recover.

---

## Bottom line

**Four hosts have a larger prize than QuickJS, the workload the project's only
positive result was built on.** Lua is 13x it, CPython 6.5x, SQLite 5.6x.

**The rig reproduces the QuickJS number to two decimal places** (+1.08% here vs
**+1.05%** documented in `docs/results/quickjs.md`), on an independent harness.
That calibration is the reason to believe the other four rows.

**`dit-headroom-needs-serial-chains` holds, with one addition that explains the
whole table: in an interpreter you cannot escape the serial chain, because the
VM dispatch loop *is* one.** Fully parallel guest code still pays +20%. That is
why every interpreter here scores high while the compiled SVG filters scored
1.00x, and it makes "is the host an interpreter?" the single best predictor of
condition (d).

---

## 1. Method

Three arms, **the same binary in all three**. The taint pipeline is never
involved, so the MIR round-trip codegen lottery (`dit-measurement-traps` trap
7b) cannot apply at all - the usual reason ~1% effects are untrustworthy is
absent by construction.

| arm | how |
|---|---|
| `base` | run stock, no dylib |
| `null` | `DYLD_INSERT_LIBRARIES=dit_off.dylib` - identical interposition and malloc traffic, DIT never written |
| `dit` | `DYLD_INSERT_LIBRARIES=dit_on.dylib` - PSTATE.DIT set on every thread |

`dit_all_threads.c` is reused verbatim from the browser rig. 12 reps, 2 burn-in
discarded, paired round-robin with arms adjacent per host.

**Controls ran in-band every rep** (trap 5). `dit_probe` never touches DIT
itself, so its timing depends only on whether the injection worked - it
validates the whole path exactly as each host experiences it.

| gate | required | measured |
|---|---|---|
| positive control, const chase null->dit | ~4.0x | **3.79x** PASS |
| robust gate, const/perm under DIT (trap 6) | 0.997-1.003 | **0.9994** PASS |
| no DIT ratio below 1.00x (trap 3) | all > 0 | **PASS** |
| PSTATE.DIT actually set per arm | 0 / 0 / 1 | **PASS** |

---

## 2. The screening table

| host | workload | base s | null s | dit s | harness | **always-on DIT** | slower |
|---|---|---|---|---|---|---|---|
| **lua** 5.4.7 | binary-trees | 1.877 | 1.908 | 2.185 | +1.23% | **+14.52%** | 12/12 |
| **cpython** 3.16.0a0 | pyperformance x4 | 10.356 | 10.367 | 11.066 | -0.05% | **+7.00%** | 12/12 |
| **sqlite** (plain) | speedtest1 `--testset main` | 4.005 | 4.016 | 4.251 | +0.14% | **+6.09%** | 12/12 |
| **git** 2.53 | `rev-list --all --count`, 580k commits | 2.346 | 2.351 | 2.409 | -0.06% | **+2.48%** | 12/12 |
| **quickjs** 2025-04-26 | Octane subset | 13.309 | 13.323 | 13.460 | -0.01% | **+1.08%** | 11/12 |

CoV of the null arm: 0.19-1.77%.

**Run the null arm.** It is +1.23% on Lua. Charging that to DIT would have
reported Lua at +15.9% instead of +14.52%. It was ~0 on the other four, so one
host's null does not license skipping another's - the same lesson the browser
rig recorded for Chromium vs Firefox.

**The `+4.4% unencrypted SQLite` figure was real and understated.** It was
recorded in an old session note and could not be found anywhere in `docs/`. It
re-measures at **+6.09%**. SQLite's public code is strongly DIT-sensitive, which
sharpens rather than dissolves the tension with `docs/results/sqlcipher.md`:
that ROI showed ~0.89% recoverable headroom because its workload was
crypto-dominated, so the DIT-sensitive SQLite portion was a thin slice of it.
**Implication: an encrypted-SQLite workload with lots of query work per unit of
crypto should expose a prize the page-scan ROI hid.** That is a re-run on rigs
that already exist, not new work.

---

## 3. Per-workload breakdown (14 reps)

| host / workload | null s | dit s | DIT cost | slower |
|---|---|---|---|---|
| lua / spectralnorm | 1.349 | 1.697 | **+26.19%** | 14/14 |
| lua / binary_trees | 1.890 | 2.203 | **+16.42%** | 14/14 |
| cpython / richards | 2.639 | 2.933 | **+11.11%** | 14/14 |
| cpython / go | 2.477 | 2.667 | +7.70% | 14/14 |
| cpython / float | 2.664 | 2.839 | +7.30% | 14/14 |
| lua / fannkuch | 0.658 | 0.685 | +4.50% | 13/14 |
| cpython / nbody | 2.560 | 2.628 | +2.84% | 13/14 |

**A prediction failed here and the correction is the interesting part.** I
predicted spectralnorm would be ~zero, on the grounds that it is "flat float
arrays, embarrassingly parallel" - the same shape as the SVG filters. It came
back the *highest* in the table. The mislabelling was mine: `s = s + A(i,j)*u[j]`
is a loop-carried FP accumulator, i.e. a serial dependency chain, just an
arithmetic one rather than a pointer one.

---

## 4. Mechanism, isolated directly (Lua, 200M iterations, 12 reps)

Two candidate explanations for spectralnorm, with opposite predictions, so they
separate cleanly. `utils/dit_host_screening/micro.lua`.

| workload | null s | dit s | DIT cost |
|---|---|---|---|
| `serial_add` - serial FP accumulator | 0.730 | 0.965 | **+32.39%** |
| `par_add` - same work, 4 independent accumulators | 0.752 | 0.903 | **+20.06%** |
| `serial_div` - serial accumulator + float divide | 0.974 | 1.303 | **+33.93%** |
| `par_div` - divides, chain broken | 0.988 | 1.181 | **+19.53%** |

- **serial vs parallel: +12 to +14 points.** The dependency chain is the factor.
- **add vs divide: ~0 points** (32.39 vs 33.93; 20.06 vs 19.53). The float
  divide is irrelevant, which kills the other hypothesis outright.
- **But parallel guest code still pays +20%.** That is the floor contributed by
  the interpreter's own dispatch loop, which the guest program cannot break.

That floor is the finding. It says condition (d) is a property of the *host*
rather than of the guest workload, and it predicts the screening table: every
interpreter scores high regardless of what it is running, compiled
embarrassingly-parallel code scores ~1.00x.

---

## 5. Caveat that must not be dropped

**These numbers are the size of the prize (condition d) and nothing more.** They
do not show fine-grained beating always-on. Conditions (a), (b) and (c) depend
on a payload and on instrumentation, neither of which is exercised here. A large
always-on cost is necessary, not sufficient.

> **CORRECTED 2026-08-14, same day.** This section originally argued that these
> 6-14% numbers meant `dit-prize-is-one-to-two-percent`'s 1-2% ceiling "should
> be scoped to gem5-modelled features on SPEC/gapbs rather than stated as a
> universal cap". **That reasoning was backwards.** A real Django + PyJWT
> application, measured on the same silicon with the same rig, lands at
> **+1.21% always-on** - squarely inside the 1-2% band. The interpreter
> *benchmarks* are the outliers, not the ceiling. See
> `docs/research/real-world-instances.md` §6, which also rules out build
> configuration (PGO vs non-PGO) as the explanation: the cause is that
> `richards`/`go` are bytecode-dispatch-bound while a real request spends much
> of its time in CPython's C layer.
>
> **Every number in this document is a BENCHMARK figure and must be labelled as
> such.** For an application claim, quote +1.2%.

**Neither Lua nor CPython has a native secret.** They are hosts, not candidates.
The payload has to be grafted, and the realistic pairings are: CPython + a web
app signing session cookies or JWTs (the strongest real deployment story), Lua +
Redis scripting or a game engine, SQLite + an encrypted-column workload.

---

## 6. Follow-up (done same day)

`docs/results/dit-oracle-composites.md` adds a real secret payload to four of
these hosts and measures oracle placement vs always-on. **Oracle recovers
essentially the entire always-on cost on all four**, residuals at or below the
rig's noise floor. That is the performance claim's first support beyond QuickJS.

## 7. Next

1. **CPython + libsecp256k1 signing.** Best (host, payload) pair on the table: a
   6.5x bigger prize than QuickJS, AOT-compiled so the pass can reach all of it,
   a real deployment story, and a payload whose declassification is defined by
   the protocol rather than by a harness trick.
2. **Re-run SQLCipher with a query-heavy / crypto-light workload** to test §2's
   implication against the existing negative.
3. **Attribute the cost to a feature under gem5** (EVES / DMP / comp-simp) for
   the winning host, the way `sqlcipher-dit-placement` did.

Raw data: `utils/dit_host_screening/{sweep,mechanism}.csv`.
