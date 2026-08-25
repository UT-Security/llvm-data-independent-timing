# Measured cost of taint-driven fence insertion (libsodium, x86-64)

Source data: `micro-benchmarks/output.txt`, `micro-benchmarks/unfenced_output.txt`,
`micro-benchmarks/fenced_output.txt` (all committed).
Workload: **Ed25519 signing** (`crypto_sign`), 1000 measured iterations, 25 warmup.

This is the headline number of the current implementation, and it is a negative
result: fencing every tainted instruction costs **90x**.

---

## 1. Numbers

Per-iteration cycle counts, from the three committed data files:

| configuration | n | min | p50 | p90 | p99 | max | mean | sd | cv |
|---|---|---|---|---|---|---|---|---|---|
| system libsodium | 1000 | 34,732 | **34,922** | 35,150 | 70,186 | 131,328 | 35,796 | 6,539 | 18.3% |
| our build, unfenced | 1000 | 45,410 | **45,676** | 45,828 | 50,388 | 54,872 | 45,851 | 830 | 1.8% |
| our build, fenced | 1000 | 4,106,546 | **4,110,384** | 4,114,146 | 4,122,354 | 4,256,380 | 4,111,188 | 7,036 | 0.2% |

Ratios on medians:

- **fenced / unfenced = 90.0x** (+4,064,708 cycles per signature). This is the pass's
  cost, and it is the only comparison where the two binaries differ solely by the pass.
- fenced / system = 117.7x.
- unfenced / system = 1.31x. This is a **build-configuration artifact**, not a result:
  it is the gap between the distribution's libsodium and our own bitcode build of it.
  Do not attribute any part of it to the analysis.

The fenced configuration's coefficient of variation is 0.17%, an order of magnitude
tighter than unfenced (1.8%) and two orders tighter than the system build (18.3%). That
is the expected signature of pervasive serialization: the pipeline can no longer
overlap work, so run-to-run variation collapses. It is not evidence that the mitigation
made anything constant-time in the sense that matters, only that the measurement got
quieter.

## 2. What was compared

`micro-benchmarks/Makefile` builds three variants of each harness:

| target | library linked | meaning |
|---|---|---|
| `eval_%` | `-lsodium` (system) | distribution build |
| `eval_%_unfenced` | `$(LIBSODIUM_DIR)/bitcode/libsodium_unfenced.a` | our clang, pass not run |
| `eval_%_fenced` | `$(LIBSODIUM_DIR)/bitcode/libsodium_fenced.a` | our clang, `taint-fence-insertion` run |

Harness flags: `-fomit-frame-pointer -O0 -Werror -std=c18`. Both archives come from a
bitcode build of libsodium with the same clang, differing only in whether
`taint-fence-insertion` was run over the bitcode; the exact per-translation-unit
sequence is inferred from the archive names, since the build scripts are not in this
repository (see the caveats below).

**Reproducibility caveats, stated plainly:**

- The libsodium bitcode build scripts are **not committed in this repository**. Only
  the `.a` paths appear, and `LIBSODIUM_DIR` is hard-coded to a Linux absolute path
  (`/home/rgangar/Documents/libsodium-stable`). Reproducing the archives requires
  rebuilding that flow.
- The **taint sources CSV used for the libsodium build is not committed either.** The
  in-tree `micro-benchmarks/taint_sources.csv` contains `process_string,0`, which
  targets the `hello.c` demo, not libsodium. For Ed25519 the intended source is the
  secret key argument of `crypto_sign` (index 4).
- The **number of fences inserted was not recorded**, so cost per fence cannot be
  derived from these files. Recording `FencesInserted` (the pass already counts it,
  under `LLVM_DEBUG`) is the first thing to add before the next run.
- Only Ed25519 has committed cycle data. The other five harnesses exist and are built,
  but no result files for them are in the tree.
- **The libsodium bitcode was almost certainly built optimized, not at `-O0`.** The
  analysis finds almost nothing on `-O0` IR (1 tainted instruction vs 22 on the same
  file - `../design/precision-and-soundness.md` §2.1), so an `-O0` library would have
  received too few fences to cost 90x. Whatever optimization level the missing build
  scripts used, it was not `-O0`. Reproducing this number requires recovering it.

## 3. Measurement method

Each harness (`micro-benchmarks/eval_*.c`) is a standalone timing rig:

- `cpuid; rdtsc` to start and `rdtscp; cpuid` to stop, i.e. serialized on both sides so
  the counter read cannot drift into neighbouring work.
- key generation is outside the timed region; only the operation under test is timed;
- `num_warmup` iterations are discarded, then `num_iter` per-iteration counts are
  written one per line to a file;
- a correctness check runs every iteration (for Ed25519, `crypto_sign_open` must verify
  the signature just produced), so a configuration that broke the library would fail
  rather than produce fast wrong numbers.

Invocation:

```
./eval_ed25519_fenced <num_iter> <num_warmup> <message> <cycle_counts_file>
```

The committed files were produced with `<num_iter>=1000`, `<num_warmup>=25`.

## 4. Interpretation

The 90x is a property of **unselective placement**, not of the technique:

- The fenced set is *all* tainted instructions, because `SensitiveInsts` is populated
  with `TaintedValue` for every tainted instruction and the leak classifier is never
  called (`../design/ir-taint-analysis.md` §4.2).
- Each one gets **two** `seq_cst` fences, which lower to `mfence` on x86-64, so the
  emitted barrier count is about twice the tainted instruction count.
- Ed25519 signing is almost entirely secret-dependent arithmetic, so on this workload
  "tainted" covers nearly the whole computation. It is the worst case for this
  placement policy, and a reasonable one to report as such.

Three consequences for the design:

1. **Classification is the highest-value next change.** Fencing only tainted branches,
   tainted memory addresses, and tainted variable-latency operations would cut the
   fenced set by orders of magnitude on this workload. The classifier already exists.
2. **Fence coalescing is nearly free.** Consecutive tainted instructions currently get
   `2n` fences where `n+1` would preserve the same separation.
3. **A fence is the wrong primitive if the goal is data-operand timing.** An
   architectural data-independent-timing mode (ARM `PSTATE.DIT`, Intel DOIT) is set
   once around a region instead of twice per instruction, which changes the cost from
   per-instruction to per-region. That is a different mitigation, and the 90x here is
   the argument for it.

## 5. Workloads available but not yet measured

`micro-benchmarks/` contains harnesses for the following. Generated `.ll` files are no
longer committed; `make ir` emits them at `-O2` (see the `IRFLAGS` note in the
`Makefile`). The harnesses are **x86-64 only** - the `rdtsc`/`rdtscp` + `cpuid` timer
macros do not compile for AArch64 - so an arm64 host must cross-target to emit IR.

Note also that harness IR is **not** the analysis target: the secret is `crypto_sign`'s
`sk` argument, which lives inside libsodium, so the archive bitcode is what has to be
analyzed and fenced.

| harness | operation timed | natural secret |
|---|---|---|
| `eval_ed25519.c` | `crypto_sign` | secret key (arg 4) |
| `eval_aesni256gcm_encrypt.c` / `_decrypt.c` | AES-256-GCM (AES-NI) | key |
| `eval_chacha20_poly1305_encrypt.c` / `_decrypt.c` | ChaCha20-Poly1305 | key |
| `eval_argon2id.c` | Argon2id | password |

`hello.c` is the self-contained functional demo (no external calls, so the whole flow
is visible to a whole-module analysis); `sensitive_lines.txt` is its committed report.
