# Bitcoin Core clears condition (d): +14.4% on coin selection

**Measured 2026-08-19**, Apple M5, 15 paired reps, arm order rotated.
Rig: `utils/dit_host_screening/btc/`.

Bitcoin Core was selected by a candidate survey (`docs/research/real-world-instances.md`)
as the one application clearing every structural criterion. This is the cheap
always-on screen that decides whether it is worth instrumenting.

---

## Bottom line

**Yes — and by the largest margin of any real application tested.**
`CoinSelection` costs **+14.39%** under always-on DIT, 15/15 reps, which ties
Lua for the highest DIT sensitivity measured in this project and is the first
time that figure has come from production C++ rather than a benchmark
microkernel.

Critically, the wallet path has a **naturally in-band secret fraction of ~0.4%**
with no workload construction: `WalletCreateTxUsePresetInputsAndCoinSelection`
costs 15.6 ms per transaction of which signing is ~58 us. That is the shape the
whole project has been looking for, in shipped upstream code.

---

## 1. Why Bitcoin Core is instrumentable

Verified at configure time, not assumed:

```
secp256k1 configure summary
  ECDH ................................ OFF
  ECDSA pubkey recovery ............... ON
  schnorrsig .......................... ON
  assembly ............................ OFF      <-- the criterion that killed OpenSSL
```

`SECP256K1_ASM` defaults to `AUTO`, probes for x86_64, fails on aarch64 and
falls back to `OFF`, so the entire crypto path compiles to portable `__int128`
C reachable by an MIR pass. Bitcoin Core also vendors libsecp256k1, LevelDB,
crc32c and minisketch as in-tree subtrees and links **no OpenSSL at all**.

The secret surface is nine named entry points, all in `src/key.cpp`, all direct
calls with no vtable dispatch: `secp256k1_ecdsa_sign` (arg 4),
`ecdsa_sign_recoverable` (4), `schnorrsig_sign32` (4), `ec_seckey_tweak_add`
(2, BIP32 derivation), `keypair_create` (3), `ec_pubkey_create` (3),
`ellswift_create` (3), `ellswift_xdh` (5), `ec_seckey_verify` (2).

Built with `-DENABLE_IPC=OFF` (avoids a Cap'n Proto dependency; multiprocess is
irrelevant to benchmarking) and Homebrew boost 1.92.

---

## 2. Results

Same rig as the five-host screen: ONE binary, DIT injected by
`DYLD_INSERT_LIBRARIES` at process load, three arms (`base` / `null` / `dit`),
`lvp_chase` in-band every rep. In-band control **3.87x**, PASS.

Each row is an **independent benchmark** from Bitcoin Core's own `bench_bitcoin`
suite (193 total, 11 selected). The class column is ours, and is verified by
which source file the benchmark lives in — only `sign_transaction.cpp`,
`verify_script.cpp` and `wallet_create_tx.cpp` call signing at all.

| benchmark | class | base ns/op | harness | **DIT cost** | slower |
|---|---|---|---|---|---|
| **CoinSelection** | PUBLIC | 3,152,475 | −0.93% | **+14.39%** | 15/15 |
| **TxGraphTrim** | PUBLIC | 9,345,083 | −0.35% | **+8.39%** | 15/15 |
| WalletAvailableCoins | PUBLIC | 26,170,792 | −0.34% | +2.39% | 13/15 |
| ComplexMemPool | PUBLIC | 34,739,416 | −0.14% | +2.25% | 14/15 |
| MemPoolAddTransactions | PUBLIC | 68,237,291 | +0.07% | +2.21% | 15/15 |
| DeserializeBlockTest | PUBLIC | 737,590 | −0.05% | +0.41% | 12/15 |
| CCoinsCaching | PUBLIC | 171 | −2.18% | +0.36% | 8/15 |
| **WalletCreateTx…CoinSelection** | MIXED | 15,603,708 | +0.37% | **+3.13%** | 15/15 |
| ConnectBlockAllEcdsa | MIXED | 26,908,438 | +0.05% | +0.20% | 10/15 |
| SignTransactionECDSA | SECRET | 57,624 | +0.58% | +2.57% | 8/15 |
| SignTransactionSchnorr | SECRET | 43,320 | +0.01% | +1.91% | 15/15 |

**PUBLIC-work DIT cost: median +2.25%, range +0.36% to +14.39%.**

### Reading the table

- **The PUBLIC rows are condition (d).** They never touch a private key, so any
  DIT applied to them is pure waste that selective placement could remove.
  `CoinSelection` (branch-and-bound tree search) and `TxGraphTrim` (cluster
  mempool graph work) are where the prize is.
- **`ConnectBlockAllEcdsa` at +0.20% is a useful negative.** "Validation" is not
  uniformly sensitive; picking the wrong benchmark inside the same application
  would have produced a null result and a wrong conclusion.
- **`CCoinsCaching` should be discarded**, not interpreted. At 171 ns/op its
  harness arm reads −2.18%, which is noise at that scale. It is exactly why the
  null arm exists.

### What this is NOT

**These are 11 isolated microbenchmarks, not an end-to-end application run.**
They establish that Bitcoin Core *contains* DIT-sensitive code and roughly where.
They do not establish what a running node spends its time in. The end-to-end
figure comes from `-reindex-chainstate` over real mainnet data (separate doc).

---

## 3. Why the wallet path matters

| | coincurve / eth-account | Bitcoin Core `WalletCreateTx` |
|---|---|---|
| secret fraction | **19.5%** (forced: `eth_keys` rebuilds a key per signature) | **~0.4%** (natural) |
| recoverable prize | 0.64% | to be measured |
| workload origin | harness code we wrote | shipped upstream benchmark |

The coincurve result failed not because placement was bad but because the secret
fraction was too high for any placement to matter. Bitcoin Core inverts that, and
— uniquely among surveyed candidates — offers the fraction as a **knob** in one
binary: ~0% (reindex under default `-assumevalid`), ~0.4% (`WalletCreateTx`),
higher by raising the wallet send rate, with `-assumevalid=0` as a separate axis
that re-enables the script interpreter and signature verification.

That makes the **secret-fraction-versus-recoverable-prize curve** measurable
within a single real application, which is the experiment this project has been
unable to run until now.

---

## 4. End-to-end reindex: INCONCLUSIVE, not null

**Measured 2026-08-19**, `-reindex-chainstate` over the mainnet chain to height
200,000 (7.3M txs, 3.7 GB), 12 reps, arm order rotated, control 3.92x PASS.

| arm | median | CoV | min | max |
|---|---|---|---|---|
| base | 22.11 s | 6.72% | 20.96 | 25.83 |
| null | 23.32 s | 7.29% | 20.79 | 25.48 |
| dit | 23.52 s | 6.97% | 21.20 | 25.23 |

| comparison | result | reps slower |
|---|---|---|
| harness cost (base -> null) | **+3.12%** | 7/12 |
| always-on DIT (null -> dit) | **-0.04%** | 6/12 |
| IQR on that comparison | **-6.87% .. +9.91%** | |

**Do NOT read the -0.04% as "no DIT cost on reindex."** The rig cannot resolve
the effect here:

- CoV is **6.7-7.3%**, against 0.3-0.5% on every other measurement in this
  project. An effect of 1-3% is invisible inside that.
- The IQR spans **+-7-10%** and 6/12 reps slower is a coin flip.
- **The null arm reads +3.12%** — an arm that loads a dylib which never writes
  the DIT bit. If a no-op registers +3.12%, a real +3% cannot be distinguished
  from zero.

The cause is the workload, not the rig: reindex is **I/O-dominated** (`sys` was
9 s of a 26 s wall, reading 3.7 GB of blocks and writing LevelDB), so disk and
page-cache variance swamp a few-percent CPU effect.

This is exactly the `dit-measurement-traps` trap 5 situation — a null result and
an unresolving measurement look identical — except that here the null arm tells
us which one we have. **The result is "unresolved", and condition (d) rests on
the microbenchmarks in §2, which have the precision to support it.**

To resolve it: put the datadir on a RAM disk and raise `-dbcache` so LevelDB
stays resident (attacks the variance source directly), then ~30 reps. Averaging
~200 noisy I/O-bound runs would also work arithmetically but produces a figure
that is hard to defend.

## 5. Next

1. Instrument the nine `src/key.cpp` entry points; oracle vs pass on
   `WalletCreateTx`.
2. End-to-end `-reindex-chainstate` over mainnet to height 200,000 for the
   pure-public-work headline.
3. Sweep the fraction knob to draw the curve.

Raw data: `utils/dit_host_screening/btc/btc_screen.csv`.
