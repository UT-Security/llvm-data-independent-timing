# The crossover rig: when does selective DIT placement beat setting the bit?

Measures the boundary rather than looking for a winner. Every benchmark pairs a
**public lane** (real application code holding no secret) with a **secret lane**
(a deployed crypto library), and sweeps the ratio between them. Results:
`docs/results/` and the evaluation write-up.

**The finding this rig exists to support:** the pass emits a roughly constant
**~0.8 us of mode switching per protected region** independent of region size,
across five orders of magnitude. So `tau = T/R`, and the crossover is

```
R* = T * f / [ (c_P - phi) (1 - f) ]
```

which predicts the measured verdict on two public lanes and two crypto libraries
with nothing fitted.

## Layout

| file | what it is |
|---|---|
| `host_sqlite.c`, `secret_payload.{c,h}` | SQLite + libsecp256k1 ECDSA composite. Three dials: `period` -> f, `sigs`/batch-oracle -> R, `verifies` -> phi |
| `host_sqlite_sodium.c`, `host_lua_sodium.c`, `sodium_payload.{c,h}`, `work_sodium.lua` | the same shape with libsodium, on two public lanes. For an AEAD, R is just the message size |
| `host_lua_typeb.c`, `work_typeb.lua` | the instruction-level regime: public code computing **on** secret data inside the Lua interpreter, with `K` setting the interleaving granularity |
| `flowprobe.c` | six functions with byte-identical bodies reached by six different taint channels; finds under-taints |
| `run_native.py`, `run_sodium.py`, `run_typeb.py` | native sweep drivers (exclusive machine, rotated arms, pre-flight gate) |
| `run_gem5.py` | simulator sweeps: f, optimization count, granularity, phi |
| `analyze.py` | tables, crossover location, coverage, switch-model comparison |
| `build.sh`, `build2.sh`, `scripts/` | build the arms |

## Arms

Only the instrumented library differs between arms; the public lane is
bit-identical throughout. `off` / `always` / `oracle` / `batch` share ONE binary
and take the DIT mode from `argv`, so no codegen difference can contaminate the
comparison that matters most.

| arm | build |
|---|---|
| `plain` | stock -O2, no pass |
| `nodit` | `-ftaint-harden=<empty>` — **the baseline**, never `plain` |
| `hoist` | `region` + `loop-hoist=1` |
| `gated` | `hoist` + `modset-callsite-gated` |
| `hoist0` | `region` + `loop-hoist=0` — the shipped default |
| `swcyc30` | `hoist` + `switch-cyc=30` — **the best measured configuration** |
| `func` | `placement=function` |
| `nopctl` | `gated` with every switch emitted as `HINT #0` — the alignment control |

`hoist`, `gated` and `hoist0` are indistinguishable wherever there is signal
(<=10%, usually <=3%); prefer reporting one plus an ablation rather than a
best-of. `relaxed-ownership` is inert on a shared library — its precondition is a
local-linkage, address-not-taken callee, which exported functions never satisfy.

## Controls, none of which are optional

- **Baseline is the round-trip control**, not the stock build. On both targets the
  `nodit` object is byte-identical to `plain` for the instrumented TU here, so the
  pipeline's own codegen perturbation is exactly zero — verified, not assumed.
- **`nopctl`** separates switch cost from code layout. It has caught a case where
  the residual was entirely alignment.
- **`dit_probe` in band.** The probe prints `const/perm`, so a HEALTHY reading is
  **~0.26** — the constant chase is the FAST one when the predictor works. Gating
  on the reciprocal once rejected a perfectly quiet machine and threw away a
  sweep. With DIT injected it must read ~1.00, and noise in that ratio is
  one-directional, so take the MINIMUM of several probes.
- **Settle before measuring.** After a heavy load the times decay for minutes;
  waiting for stability is not enough, because two consecutive samples can match
  while still 6x above the quiet value. Gate on the known-good signature itself.
- **Native runs need the machine to itself.** gem5 does not.
- Rotated arm order, duplicate baseline arm, identical checksums, geometric mean
  over per-rep ratios.

## Simulator

Use **master**, not `taint-gem5-bridge`. The bridge predates the removal of a
pipeline drain at DIT region exit (~170 cycles), and region-exit cost is exactly
what this measures — on the bridge the same sweep placed the crossover at 49.8%
serialising / 60.2% renamed, against 46.8% / 76.2% on master. The correction is
microarchitectural, so architectural quantities are unchanged: executed switch
counts are identical on both.

Gates: instruction counts identical across machine configurations, and zero
suppression events in unprotected arms. Both have failed on first use before.

## Reproducing

```sh
scripts/build_sodium_arms.sh      # taint arms over the libsodium whole-library bitcode
scripts/build_sodium_hosts.sh     # link both public lanes against every arm
python3 run_sodium.py --lane sqlite --grid R       # region-size sweep
python3 run_sodium.py --lane lua    --grid deploy  # sizes taken from real deployments
python3 run_typeb.py                               # instruction-level interleaving
python3 run_gem5.py --sweep fsweep --jobs 9        # simulator
python3 analyze.py <results.jsonl> --kind gem5|native|opt
```

libsodium arms start from the whole-library bitcode and CIO-parity seed that
`utils/taint_libsodium_eval.sh bitcode seed` produces (926 functions, 48 pointee
+ 17 data attrs across 21 functions). Note that seed declares the **plaintext and
its length** secret, not just the key — a much wider source set than a key-only
annotation, and itself a dial on tau.
