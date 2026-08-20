# Composite under gem5: the pass beats always-on by 2.66%

**Measured 2026-08-19**, gem5 (fork), SQLite + ECDSA composite, secret fraction
**2.23%**. Rig: `utils/dit_host_screening/g5comp/`.

First clean positive on a workload with BOTH real public work and a realistic
secret fraction.

---

## Gates

Cycle counts in gem5 are deterministic, so both gates are exact rather than
statistical:

| gate | result |
|---|---|
| `simInsts` identical across machine configs, all 5 arms | **OK** (135,121,192 etc.) |
| `off` arm DIT activity (`compSimplifier.ditSuppressed`) | **0** |

Both gates failed on the first attempt and caught two real defects — see §3.

## Results

| arm | switches | serializing | speculative | spec − ser |
|---|---|---|---|---|
| `off` | 3 (never fire) | — | — | −0.04% |
| **`always`** | 1 | **+3.23%** | +3.17% | −0.11% |
| **`oracle`** | 2 / signature | **+0.17%** | +0.29% | +0.07% |
| **`hoist`** | 128 | **+0.57%** | +0.47% | −0.15% |
| `region` | 140 | +0.63% | +0.50% | −0.17% |

| vs always-on | serializing | speculative |
|---|---|---|
| oracle | **−3.06%** | −2.87% |
| **hoist** | **−2.66%** | −2.70% |
| region | −2.60% | −2.67% |

**The pass beats always-on by 2.66% and captures 87% of the oracle's 3.06%.**

## 1. Serializing vs speculative `MSR DIT`

The fork models `MSR DIT` two ways: serializing (matches Apple silicon) and a
renamed CC-register write (essentially free). On this workload the difference is
**−0.04% to −0.17% across every arm — inside the noise the `off` arm sets.**

That is not a refutation of the mechanism, it is a statement about this
placement: at 128 switches and a 2.23% secret fraction the toggle term is
already negligible, so making toggles free recovers nothing.

Where it does matter, measured on the coverage driver (`gem5cov/outab`), is a
placement that over-toggles: the `relaxed` arm goes **+3.09% serializing →
+0.24% speculative**, a **13x collapse** from an identical binary.

> **Rule:** renaming the switch rescues placements that over-toggle. It does not
> improve a placement that is already toggle-thin.

## 2. The secret fraction decides the verdict

| workload | secret fraction | pass vs always-on |
|---|---|---|
| coincurve / eth-account (silicon) | **19.5%** | **+2.74% WORSE** |
| this composite (gem5) | **2.23%** | **−2.66% BETTER** |

Same pass, same crypto library, opposite verdicts. At 19.5% most of always-on's
cost is unavoidable (it is DIT over genuinely secret work) and the pass's toggle
overhead swamps the small remainder. At 2.23% the public work dominates,
always-on wastes 3.23%, and the pass recovers most of it.

**This is the project's central relationship, now measured on both sides rather
than asserted from one.**

## 3. Two defects the gates caught

**The baseline was not a baseline.** The first composite was built against the
secp256k1 tree hand-patched with oracle wraps for the coincurve experiment, so
`comp_nodit` had `msr DIT` compiled into `secp256k1_ecdsa_sign` and
`secp256k1_ec_pubkey_create`. Every arm was oracle-plus-something. The tell was
**847,278 DIT suppressions in an arm where DIT is never set** (should be 0, and
was 0 in the coverage driver). Quarantined as `outc_VOID_oracle_contaminated`.

**The driver perturbed itself.** `host_sqlite.c` called `clock_gettime` to report
its own secret fraction; gem5 SE returns *simulated* time, so cycle counts fed
back into control flow and `simInsts` differed between configs for an identical
binary. Now compiled out under `-DGEM5_NO_SELF_TIMING`.

The symptom that exposed both: **speculative appeared slower than serializing**,
which is impossible — a renamed write is strictly cheaper than a serializing one
for the same instruction stream. Any such result means the comparison is broken.

> **Adopt as standard for gem5 A/B work:** assert `simInsts` identical across
> configs, and assert the unprotected arm reports zero DIT activity. Both are
> exact, both are free, and both failed here.

Raw data: `utils/dit_host_screening/g5comp/outc2/`.
