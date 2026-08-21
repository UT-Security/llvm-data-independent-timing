# Real signing workload: the prize is under 1%, and the pass overshoots it

**Measured 2026-08-18**, Apple M5, 40 paired reps, arm order rotated. Workload:
`eth-account` signing 25,000 Ethereum transactions through coincurve's vendored
libsecp256k1 — `web3.py`'s actual signing stack, no harness code in the signing
path. Rig: `utils/dit_host_screening/coincurve/`.

> **Supersedes the 2026-08-17 version of this document**, whose oracle was
> under-protecting (§4). Its headline `-1.98%` must not be quoted.

---

## Bottom line

**On the one genuinely real application we have, blanket DIT is the right
engineering choice today.**

- Always-on DIT costs **+2.66%**.
- A perfect hand placement recovers only **0.64%** of that — the rest is
  unavoidable, because the secret is ~19.5% of runtime.
- The pass costs **+5.35%** (with cloning) or **+11.21%** (shipped default):
  **+2.74% and +8.35% WORSE than always-on**, 39-40/40 reps.

So the pass would have to close a 2.7-point gap to win 0.6 points. That is not a
good trade, and it is the honest headline for this workload.

---

## 1. Results

| arm | median | CoV | vs baseline | reps slower | IQR |
|---|---|---|---|---|---|
| `baseline` (round-trip control, 0 switches) | 3.158 s | 0.53% | — | — | — |
| `baseline2` (noise floor) | 3.158 s | 0.96% | −0.01% | 20/40 | −0.30 .. +0.61 |
| `null` (dylib injected, DIT never set) | 3.160 s | 0.50% | +0.20% | 25/40 | −0.30 .. +0.42 |
| **`oracle`** (8 switches: 2 per entry point) | 3.218 s | 0.66% | **+1.91%** | 40/40 | +1.51 .. +2.47 |
| **`always`** (blanket DIT) | 3.239 s | 0.81% | **+2.66%** | 40/40 | +2.19 .. +3.00 |
| **`pass_clone`** (368 switches, cloning) | 3.327 s | 0.63% | **+5.35%** | 40/40 | +5.08 .. +5.89 |
| **`pass_hoist`** (575 switches) | 3.511 s | 0.68% | **+11.21%** | 40/40 | +11.02 .. +11.64 |

| comparison | result | reps |
|---|---|---|
| **`oracle` vs `always` — the entire prize** | **−0.64%** | 3/40 slower |
| `pass_clone` vs `always` | **+2.74% worse** | 39/40 |
| `pass_hoist` vs `always` | **+8.35% worse** | 40/40 |
| noise floor | −0.01% | 20/40 |

Gates: in-band `lvp_chase` control **3.97x**; checksums identical across all
seven arms; harness cost +0.20%, i.e. nil; baseline CoV 0.53%.

### The cost model closes

Secret work is ~19.5% of runtime (§2), and the arms decompose:

```
always-on = DIT on secret + DIT on public = 1.27% + ~1.4% = 2.66%  (measured 2.66%)
oracle    = DIT on secret + toggles       = 1.27% + 0.6%  = 1.91%  (measured 1.91%)
prize     = DIT on the public ~80%                        = 0.64%  (measured 0.64%)
```

`1.27%` is the measured +6.50% on the secret region times its ~19.5% share.

---

## 2. Why the secret fraction is ~19.5%, not 9%

Per transaction (126.6 us total):

| | us | note |
|---|---|---|
| ECDSA signature | 10.9 | `secp256k1_ecdsa_sign_recoverable` |
| **key derivation** | **13.8** | `ec_pubkey_create` + `keypair_create` |
| Python / RLP / keccak | ~102 | public |

`eth_keys`' `CoinCurveECCBackend.ecdsa_sign` constructs a **fresh
`coincurve.PrivateKey` on every call**, and `PrivateKey.__init__` eagerly
computes `PublicKey.from_valid_secret()` and `PublicKeyXOnly.from_valid_secret()`.
Both derive from the **secret key**, so both are genuine secret work — not
false positives.

Measured directly (`probe_ctor.py`, 20,000 constructions): baseline 13.79 us,
under-protecting oracle 13.60 us (**unchanged — it was not protecting this**),
corrected oracle 15.24 us, `pass_hoist` 17.82 us.

**This is the single most important structural fact about the workload**, and it
is what makes the prize small: nearly a fifth of the runtime is secret, so most
of the always-on cost is unavoidable no matter how good the placement is.

---

## 3. Where the pass loses: switching, not protecting

| arm | raw signing (25,000 sigs) | vs baseline |
|---|---|---|
| `baseline` | 288.3 ms | — |
| `oracle` | 306.8 ms | **+6.50%** |
| `always` | 307.3 ms | **+6.58%** |
| `pass_clone` | 316.7 ms | **+9.82%** |
| `pass_hoist` | 416.1 ms | **+44.28%** |

`oracle` and `always` agree to 0.08 points — both hold DIT across the whole
signature, as they must. That agreement is the check that the oracle is not
under-protecting *the signing path*; §4 is about the path it missed entirely.

**Cloning does its job**: +44.28% → +9.82% on the secret region, within ~3 points
of the oracle. It is the toggle-count fix working. It just is not enough,
because the prize it is competing for is only 0.64 points.

---

## 4. The oracle was under-protecting — twice

Both errors were caught the same way: **an arithmetic inconsistency between the
protected region and the whole program.**

**Error 1 (caught 2026-08-17).** Ethereum needs a recovery id, so `eth_keys`
calls `sign_recoverable` → `secp256k1_ecdsa_sign_recoverable`, not
`secp256k1_ecdsa_sign`. The first oracle wrapped only the latter. It read
−0.11% overall while showing +6.80% on the raw signing loop — impossible if it
were really protecting the workload.

**Error 2 (caught 2026-08-18).** With signing fixed, the oracle read +0.78%
overall. But signing is 8.6% of runtime at +10%, which predicts +0.9% — while
the *pass* arms showed ~5% overhead that signing could not explain. Chasing that
residual found the per-signature key derivation above.

Corrected oracle: **4 entry points, 8 switches**, verified in the disassembly —
`ecdsa_sign`, `ecdsa_sign_recoverable`, `ec_pubkey_create`, `keypair_create`.

**The detector, worth adopting as standard practice:** measure the protected
region *and* the whole program in the same run, and check the arithmetic closes.
If protection shows up in one and not the other, the placement is covering the
wrong code. Whole-program timing alone cannot distinguish "placed well" from
"placed somewhere irrelevant" — which is `dit-measurement-traps` trap 8, and it
has now bitten this project three times.

**Lesson for the annotation model:** the seed must name every entry point through
which the secret enters *in the application's actual call pattern*. A library may
expose several, the app may use the least prominent one, and it may reach others
through object construction rather than an obvious crypto call.

Void data: `signbench_wrongseed.csv` (error 1), `signbench_2seed.csv` (error 2).

---

## 5. What this means

- **The recoverable prize on real software can be under 1%.** It is bounded by
  `always_on_cost x public_fraction`, and here the secret fraction is high
  enough that most of the cost is unavoidable.
- **This is the opposite of what the constructed composites showed**
  (`dit-oracle-composites.md`), where secret fractions of 0.02-1.7% made the
  prize nearly the whole always-on cost. Both are correct; the difference is
  entirely the denominator, and it is the strongest evidence yet that **secret
  fraction is the variable that decides whether this work pays**.
- **What the project needs is a real application that signs OCCASIONALLY** —
  small secret fraction, DIT-sensitive public code — which is the shape the
  composites had and this workload does not.
- Cloning is validated as a mechanism (§3) even though it does not win here.

Raw data: `utils/dit_host_screening/coincurve/signbench.csv`.

---

## 6. Re-measured with `-taint-modset-callsite-gated` (2026-08-19)

**The verdict is unchanged: blanket DIT still wins here.** 40 paired reps, machine
exclusive, arm order rotated, control 3.90x, noise floor −0.05%, harness +0.23%,
checksums identical. The `nodit4`/`hoist4`/`clone4`/`oracle4` venvs are the same
builds as §1; a `hoistchk4` control rebuilt with the current clang is
byte-identical to `hoist4`, so old and new arms are comparable.

| arm | switches in `.so` | median | vs baseline | raw signing | **vs always-on** |
|---|---|---|---|---|---|
| `baseline` | 0 | 3.168 s | — | 289.1 ms | — |
| `always` | — | 3.251 s | +2.59% | +6.74% | — |
| `oracle` | 8 | 3.226 s | +1.71% | +6.52% | **−0.83%** (1/40) |
| `pass_hoist` | 575 | 3.528 s | +11.33% | +45.40% | **+8.40%** (40/40) |
| `pass_clone` | 368 | 3.340 s | +5.37% | +9.88% | **+2.77%** (40/40) |
| **`pass_gated`** | **39** | 3.449 s | +8.92% | +22.78% | **+6.04%** (40/40) |
| **`pass_clonegated`** | **34** | 3.341 s | +5.44% | +8.99% | **+2.75%** (40/40) |

Everything reproduces §1 closely (always +2.66% → +2.59%, oracle vs always −0.64%
→ −0.83%, clone vs always +2.74% → +2.77%).

### Why the gate barely helps here, having transformed Bitcoin Core

The gate takes this library from **575 switches to 39** — a bigger static cut
than on Bitcoin Core — and moves the workload only from +8.40% to +6.04% against
always-on. Bitcoin Core's `ConnectBlockAllEcdsa` went +51.14% → +0.66%.

**Because this workload never executes the false positives.** It signs; it never
verifies, never touches musig or ellswift. `dit-coincurve-real-application.md`
measured 93.6% of the switches as false positives and called them free at
runtime here — that was right for *this* workload, and Bitcoin Core is the
exception precisely because a node verifies constantly.

What is left is toggling **on the signing path itself**, and there the gate is a
partial fix: raw signing +45.40% → +22.78%, roughly half the toggle cost. The
dominant remaining term is the per-call entry enable in hot inner helpers, which
only cloning eliminates: +9.88%, and the oracle's floor is +6.52%.

**The two mechanisms do not stack.** `pass_clonegated` equals `pass_clone` to
within noise (+2.75% vs +2.77% against always-on; raw signing +8.99% vs +9.88%),
because once cloning has removed the entry enables, the flood-driven
instrumentation the gate targets sits in clones that emit no switches anyway.

### And it could not have rescued this workload anyway

The oracle beats always-on by **0.83%**. That is the entire prize, and it is
capped by the secret fraction (§2), not by placement quality. No precision or
granularity work can win more than 0.83% here. **The conclusion of §5 stands: a
real application that signs *occasionally* is what this project needs, and
Bitcoin Core is that application** — which is why the gate matters there and not
here.

### A methodological note worth keeping

Static switch count did not predict dynamic cost, for the third time in one day:
575 → 39 bought 2.4 points here, while `pass_clone`'s 368 switches beat
`pass_gated`'s 39 outright. (The other two: `-taint-frame-addr-args` +gate cut
counts 975 → 404 while costing +44 points, and the Bitcoin Core gate's win was
confirmed by `ditSuppressed`, not by counting.) **Count switches to understand a
build; measure to know what it costs.**

Raw data: `utils/dit_host_screening/ccbuild/signbench_gated.csv`,
`run_signbench_gated.py`, `analyze_gated.py`, `build_gated.sh`.
