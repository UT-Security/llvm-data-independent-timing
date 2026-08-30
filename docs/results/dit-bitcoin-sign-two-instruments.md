# Bitcoin Core SignTransactionECDSA on both instruments: the pass's cost is the switch

**Measured 2026-08-26.** Apple M5, exclusive machine, 40 paired reps, arm order
rotated. gem5: `gem5-DIT` at `66e6c23690`, Neoverse V2 FDP config, deterministic.
Rigs: `utils/dit_host_screening/btc/run_sign_ecdsa.py` (silicon),
`utils/dit_host_screening/btc/btc_gem5.py --bench sign` (gem5).

---

## Bottom line

On a ~100% secret workload the taint pass **loses to always-on DIT on silicon by
+4.12%** (29/40 paired reps), and the NOP control says **all of that cost is the
`MSR DIT` instruction itself**, not the code the pass added. Under gem5's
rename-resolved switch the same binary's cost disappears.

Two facts limit how far that can be pushed:

1. gem5 does not reproduce always-on's cost **on this workload**. Blanket DIT
   reads **-1.11%** there against **+3.39%** on silicon. That is not a general
   property of gem5: on Bitcoin's CoinSelection it reproduces +11.06% against
   silicon's +13.01% (`dit-bitcoin-coinsel-gem5.md`). Note also that silicon's
   +3.39% here is itself marginal (26/40, p≈.08), so the honest reading is that
   **both instruments find little or no always-on cost on pure ECDSA signing** --
   not that one of them is broken.
2. The gem5 round-trip artifact is **-0.64%** between two binaries with
   *identical* `simInsts`. Every gem5 effect below is within a factor of two of
   that floor.

---

## 1. Silicon

One benchmark, six arms, one `bench_bitcoin` invocation per arm per rep.
`baseline` is `build-nodit-v2`: the pass ran with an **empty** seed file, so the
MIR made the same round trip and placed nothing (verified: 0 `msr DIT`).
`pass` is `build-gated-v2` (96 switches). `passnop` is `build-nop-v2`: the same
9 seeds and the same placement with `-taint-dit-nop-switches`, so every switch
site holds `HINT #0` at an identical address (verified: 0 `msr DIT`, 96 `nop`,
against 0 `nop` in `baseline` -- so those 96 are the substituted switches and
not alignment padding).

| arm | median vs baseline | reps slower | reading |
|---|---|---|---|
| null (harness, DIT never written) | +1.21% | 23/40 | noise |
| **always** (blanket DIT) | **+3.39%** | 26/40 | marginal, p≈.08 |
| **pass** (96 switches) | **+7.71%** | **34/40** | p≈.00003 |
| **passnop** (same placement, NOPs) | **+1.58%** | 23/40 | **noise** |
| baseline2 (noise floor) | +0.85% | 24/40 | noise |

**pass vs always: +4.12%, 29/40 (p≈.010). The pass loses.**

### The decomposition that matters

| component | median | reps slower |
|---|---|---|
| the switches themselves (`pass - passnop`) | **+6.56%** | 32/40 |
| instructions and layout (`passnop - baseline`) | +1.58% | 23/40 |

The pass's entire cost is the serializing switch. Its extra instructions,
register pressure and changed code layout are indistinguishable from the noise
floor. This is the arm that makes the gem5 comparison interpretable at all:
without it, a gem5/silicon gap could equally have been layout, and nothing would
attribute it.

### Statistics

The paired **sign test** is the headline here, not the median. Baseline spread
across reps is **22.3%** (50,379 to 61,635 ns/op) while nanobench's own within-
process error is **0.10%**, so the variance is per-process core placement, not
per-measurement. It is also not new: the 2026-08-19 run of this benchmark had a
21.0% spread on the same rig. CoinSelection, by contrast, spreads 2.9%.

Do not quote a median from this benchmark without its n/N.

Controls: in-band `lvp_chase` **3.98x** (must be ~4x); noise floor +0.85%.

### Static counts still do not predict dynamic cost

The pass places **96** switches where the 2026-08-19 build placed 178, and costs
the same (+7.71% vs +7.45% recorded). The retuned defaults removed switches from
`ellswift`, `schnorrsig` and `musig` -- paths this benchmark never executes --
while the hot chain `ecdsa_sign -> ecdsa_sign_inner -> nonce_function_rfc6979 ->
sha256_finalize` kept its own.

`secp256k1_ecdsa_verify` carries **zero** switches. It is on the executed path
(CKey::Sign verifies every signature it produces) and holds no secret; before
the mod-set call-site gate it carried 17 and cost +51%.

---

## 2. gem5

Same four arms, two switch models, one deterministic run each (verified: two
runs of an arm agree to the cycle).

    spec     `msr dit` rename-resolved both directions   (the proposal)
    serdit   `msr dit` as a barrier                      (silicon today)

| arm | switches | spec | serdit | switch-model delta |
|---|---|---|---|---|
| base | 0 | 11,534,384 cyc | 11,534,384 cyc | **+0.000%** |
| nodit (round-trip control) | 0 | -0.64% | -0.64% | **+0.000%** |
| blanket (constructor, pre-ROI) | 1 | -1.11% | -0.59% | +0.522% |
| taint | 105 | -1.23% | -0.32% | **+0.918%** |

Pass against **its own** round-trip control: **-0.59% (spec)**, **+0.32%
(serdit)**. `commitNonSpecStalls` reads **4,623** for taint/serdit and **1**
everywhere else -- the stalling exit, counted directly.

### Gates

1. `simInsts` identical across switch models per arm.
2. `ditSuppressed` = 0 in `base` and `nodit`.
3. **The switch model is a no-op on arms with no executed switch** -- `base` and
   `nodit` are cycle-identical between `spec` and `serdit`.
4. Checksums identical across all eight runs.

Gate 3 was added because it failed. An earlier driver selected blanket DIT at
runtime from an env var so that base and blanket could be one binary; that put
an `msr DIT` inside `main()` beside the ROI loop, and `nodit` then moved 0.549%
between switch models while executing no switch at all, with the DMP reporting
19 fills dropped as "issued in a DIT region" in a run with DIT off. A control
that responds to the knob under test bounds nothing. The blanket switch now
lives in a constructor in its own translation unit.

### What the switch-model delta is not

It is **not** all serialization. `blanket` executes its single switch *before*
the ROI and still moves +0.522% between models, so roughly half of `taint`'s
+0.918% is the two models' steady-state gating implementation rather than its
toggles. Attributing the whole delta to serialization overstates it ~2x.

---

## 3. Reading the two together

Silicon and gem5 agree on the **direction** -- a cheaper switch helps this
workload, and helps it specifically because the switch is the whole cost -- and
disagree on **magnitude**. gem5 puts the pass's serializing cost at +0.918%
where silicon's NOP control puts it at +6.56%.

The magnitude gap is not gem5 failing to see DIT costs in general: on
CoinSelection it recovers 85% of silicon's always-on figure
(`dit-bitcoin-coinsel-gem5.md`). It is that **this** workload has almost no
DIT-sensitive work in it, on either instrument, so what remains to be modelled is
the switch alone -- and a Neoverse V2 model's switch cost is not an Apple M5's.

The statement this supports:

> On a ~100% secret workload there is no always-on prize to recover -- silicon
> resolves at most +3.39% (26/40) and gem5 none. The pass nonetheless inserts 96
> switches and pays +7.71%, of which the NOP control attributes +6.56% to the
> `MSR DIT` instruction itself. Non-serializing switch hardware removes that cost
> in gem5. The lesson is placement, not hardware: a workload that fails
> condition (d) should not be instrumented at all.

---

## Reproducing

```
# arms
benchmarks/bitcoin/build_btc_arms.sh                    # gem5, in gem5-DIT
utils/dit_host_screening/btc/seed9.txt                  # the 9 seeds, in-tree

# silicon
BTC_MINTIME=2000 python3 utils/dit_host_screening/btc/run_sign_ecdsa.py 40 1
python3 utils/dit_host_screening/btc/analyze_sign_ecdsa.py

# gem5
python3 utils/dit_host_screening/btc/btc_gem5.py --bench sign --iter 32
```

The seed file is checked in at `utils/dit_host_screening/btc/seed9.txt`. The
2026-08-19 build referenced one in a session scratchpad that no longer exists,
which is why that configuration could not be rebuilt.
