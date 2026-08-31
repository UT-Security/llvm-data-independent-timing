# P1b: per-object pointee taint

**Built 2026-08-19/20.** The pass could tell that a register was *a* frame
address (`TaintKind::FrameAddr`) but not *which* object it pointed at. Everything
downstream therefore reasoned about the whole frame, and that single missing fact
is what produced three separate problems over the preceding day:

- a callee's `WritesSecretThroughArgPointee{i}` collapsed to a whole-caller
  `ExternalMemClobbered` (P1a, "blunt clobber pending P1b");
- `isFrameAddrToSecret` asked whether the **frame** held a secret, so one secret
  anywhere made every frame address a secret pointer - which made
  `-taint-frame-addr-args` antagonistic with the mod-set gate (+44 points);
- `silentpayments_recipient_scan_outputs` passing unresolvable pointers to a
  seeded function made it non-argument-sourced, which cost the source condition
  the entire verification win until the pointer-type fix.

## 1. The capability

`FrameObjectMap` maps `(base register, byte offset)` back to a frame object. The
frame layout is fixed by the time this pass runs, so it is built once per function
from `MachineFrameInfo` plus `TargetFrameLowering::getFrameIndexReference`, and
queried by range containment - an interior offset (`&buf[8]`) resolves to the
containing object, which is what a pointee query wants.

`TaintState::FrameRefs` carries `Register -> FrameIndex` through the dataflow:
every def kills the register's provenance, an add-immediate off SP/FP establishes
it, a COPY inherits it. Deliberately narrow - arithmetic on an existing frame
pointer is **not** followed, because a computed offset can leave the object and
mis-attributing provenance is the one direction that under-taints. On merge the
map **intersects**: a register keeps provenance only if every incoming path agrees
which object, and disagreement drops to unknown.

**Absent provenance always falls back to the pre-P1b conservative path**, so P1b
is never less conservative than P1a.

Two guards worth knowing: `getFrameIndexReference` asserts on a frame that has not
been finalised, so the map stays empty unless `MFI.isCalleeSavedInfoValid()` - MIR
loaded directly by a lit test never ran prologue/epilogue insertion. And scalable
(SVE) offsets are skipped, since they cannot be compared against a constant.

## 2. Go/no-go: does anything actually resolve?

`-taint-frameref-report` measures it before committing to the rest. Bitcoin Core's
`secp256k1.c`, 9,200 frame objects:

| | count | |
|---|---|---|
| frame addresses computed off SP/FP | 5,355 | |
| **resolved to a specific object** | **4,010** | **74.9%** |
| unresolved (fall back to blunt) | 1,345 | |

The first version of this measurement read 56.8%, because it counted prologue
stack adjustments (`sub sp, sp, #N` matches the add-immediate shape but computes
no frame address). Excluding defs into SP/FP themselves gives the real figure.

## 3. What it changed

`secp256k1.c`, gate + source condition + return gate:

| | switches | functions | `ecdsa_verify` |
|---|---|---|---|
| before P1b | 187 | 21 | 0 |
| **after P1b** | **114** | **17** | **0** |

A 39% reduction in emitted switches with verification still untouched.

gem5, serializing, 40 iterations:

| arm | signing coverage vs oracle | verify suppressions | verify cycles vs `off` |
|---|---|---|---|
| oracle | 100% | - | - |
| gate before P1b | 103.1% | 80 | +2.09% |
| **gate after P1b** | **100.42%** | **80** | **+1.90%** |

Coverage stays **above** the hand oracle, so there is no under-protection relative
to the reference placement - but the margin narrows from +3.1% to +0.42%, which is
worth watching rather than celebrating. Verification is unchanged at its floor of
2 switches per call.

## 3b. Native silicon: Bitcoin Core

**Apple M5, 15 paired reps, machine exclusive** (control 3.97x, noise floor
+0.04%, harness −0.55%). `pass_gated` is the previously measured 178-switch gate
whose soundness rested on the unsound suppression rule; `pass_p1b` is the sound
configuration at 114 switches.

### vs blanket always-on DIT

| benchmark | `pass_gated` | **`pass_p1b`** | reps slower | |
|---|---|---|---|---|
| CoinSelection | −11.76% | **−12.14%** | 0/15 | win |
| TxGraphTrim | −5.77% | **−7.86%** | 0/15 | win |
| WalletCreateTxUsePresetInputsAndCoinSelection | −2.36% | **−2.80%** | 3/15 | win |
| WalletAvailableCoins | −2.21% | **−2.21%** | 2/15 | win |
| ComplexMemPool | −2.89% | **−2.10%** | 4/15 | win |
| SignTransactionECDSA | +6.34% | **+0.89%** | 9/15 | tie |
| ConnectBlockAllEcdsa | +0.52% | **+0.53%** | 14/15 | small loss |
| SignTransactionSchnorr | +2.54% | **+1.50%** | 15/15 | small loss |
| WalletCreateTxUseOnlyPresetInputs | −2.46% | +6.12% | 11/15 | noisy (CoV 5.5%) |

**5 wins, 1 tie, 2 losses - neither loss above 1.5%.** The sound configuration is
also the best one measured: it converts `SignTransactionECDSA` from a +6.34% loss
into a tie and halves the Schnorr loss.

### P1b's own effect, head to head with the previous gate

| benchmark | median | reps slower | IQR |
|---|---|---|---|
| SignTransactionSchnorr | **−1.01%** | 0/15 | −1.18 .. −0.92 |
| SignTransactionECDSA | −5.35% | 4/15 | −9.38 .. +0.92 |
| ConnectBlockAllEcdsa | −0.01% | 7/15 | −0.33 .. +0.36 |
| six others | −0.19% .. +1.85% | 7-9/15 | spans 0 |

Only Schnorr is a clean result, and it is a win. `SignTransactionECDSA`'s −5.35%
is on the noisiest benchmark with an IQR spanning ±9 points and should be read as
a direction, not a number. Verification is unchanged, which is expected: it was
already at its floor of 2 switches per call.

## 4. Negative result: P1b does NOT rescue `-taint-frame-addr-args`

The hope was that per-object provenance would make the frame-address fallback
affordable, since the whole-frame test was what made it antagonistic with the
gate. It did not.

| | before P1b | after P1b |
|---|---|---|
| fallback alone | 975 | **1013** |
| gate + fallback | 408 | **628** |

Both got *worse*, and `ecdsa_verify` went from 0 back to 12 in the combination.
The mechanism: P1b now taints **specific stack cells** where P1a set an opaque
`ExternalMemClobbered`, so `anyTaintedStackCellForFI` finds real per-object taint
and more frame addresses resolve to genuinely-secret objects. Precision in one
direction created more (accurate) taint in the other.

So the caller→callee half of the frame-address gap - passing `&secret_local` in -
remains open, and the fallback remains the wrong instrument for it. P1b closed the
callee→caller half (applying a callee's arg-pointee mod-set to the object actually
passed) and that is what it should be credited with.
