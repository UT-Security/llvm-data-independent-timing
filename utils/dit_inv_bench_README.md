# DIT cost of constant-time modular inversion (safegcd)

**Result 2026-08-14, Apple M5.** libsecp256k1's constant-time safegcd inversion
costs **+23.3%** under DIT. Its variable-time sibling costs **+0.24%**. The
entire difference is the `volatile` compiler barrier at `modinv64_impl.h:176`.

## Reproduce

```sh
git clone --depth 1 https://github.com/bitcoin-core/secp256k1.git
cp dit_inv_bench.c secp256k1/src/
cd secp256k1
clang -O2 -DECMULT_WINDOW_SIZE=15 -DECMULT_GEN_KB=86 -I src -I include \
      src/dit_inv_bench.c src/precomputed_ecmult.c src/precomputed_ecmult_gen.c \
      -o dit_inv_bench
./dit_inv_bench --reps 30 --burnin 5 --inv 20000 --hops 20000000 > out.csv
```

For the fixed variant, replace `modinv64_impl.h:176-187`:

```c
    uint64_t c1, c2;                       /* was: volatile uint64_t c1, c2; */
    ...
    c1 = zeta >> 63; __asm__ volatile("" : "+r"(c1));
    mask1 = c1;
    c2 = g & 1;      __asm__ volatile("" : "+r"(c2));
    mask2 = -c2;
```

## Numbers

| build | inv DIT-off | inv DIT-on | DIT cost | inner loop |
|---|---|---|---|---|
| `volatile` (shipped) | 1395 ns | 1720 ns | **1.233x** | 33 insts, 2 `stur` + 2 `ldur` |
| no `volatile` | 1086 ns | 1088 ns | 1.002x | 28 insts, 0 mem |
| `__asm__ volatile("" : "+r")` | 1087 ns | 1089 ns | 1.002x | 29 insts, 0 mem |

Discriminators, same binary as the shipped row:

| arm | DIT cost | reading |
|---|---|---|
| `scalar_inverse` (CT safegcd) | 1.233x | the subject |
| `scalar_inverse_var` (VT safegcd) | 1.002x | not the algorithm |
| `scalar_mul` | 1.006x | not bignum arithmetic |
| 16-bit vs 256-bit inputs | 1.2317 vs 1.2316 | not operand magnitude |

## Why the `volatile` is expensive

It forces a store+reload per iteration, 590 iterations per inversion, so ~1180
store/reload pairs. Fixed stack slot, values always `0`/`-1` and `0`/`1`, on the
serial dependency chain, L1-resident. That is all three conditions for the load
value predictor at once. The LVP was hiding roughly half the cost; DIT switches
the LVP off and exposes the rest.

## Design notes

- **One binary, DIT toggled at runtime.** No cross-binary comparison, so the
  MIR round-trip codegen lottery (`dit-measurement-traps` trap 7b) cannot apply.
- **Controls run in-band** (trap 5): `chase_const` must read ~4.0x and
  `chase_perm` must stay flat, or a null result is meaningless. The robust
  trap-6 gate is `const/perm under DIT`, which must land in 0.997-1.003.
- Paired round-robin, burn-in discarded, checksums compared across arms.

## Where this goes in the paper

**Not** as a supporting workload for "fine-grained beats always-on" - see the
three reasons below. It belongs in two other places:

1. **The mitigation-interaction result.** Constant-time software and DIT are not
   additive: the `volatile` idiom that makes crypto constant-time manufactures
   exactly the value-predictable loads DIT de-optimizes, so the hardened
   implementation is penalized ~100x more than its unhardened sibling
   (+23.3% vs +0.24%). Generalizes to any library using `volatile` as an
   optimization barrier.
2. **The false-assurance section**, alongside SDIV/FP and AES T-tables. BEEA's
   leak is iteration count and branch direction; ARM DIT's covered-instruction
   list contains only CFINV and NOP under "Branches", so DIT around a BEEA
   inversion is security theatre. Cite Garcia-Brumley, USENIX Sec 2017
   (CVE-2016-7056, P-256 keys from ~50 signatures).

Why it is **not** the fine-grained benchmark:

- the 23% is inside the **secret** region, which always-on and fine-grained
  protect identically, so it cancels out of the fine-vs-always delta;
- an inversion microbenchmark is 100% secret, so there is no public region to
  leave unprotected - it fails condition (d) by construction;
- the 23% is a **fixable software artifact**, not an architectural property, so
  it disappears the moment the `volatile` is fixed.

## Caveats

- The barrier variant passes the upstream suite at 64 iterations with
  `-DVERIFY`. **`ctime_tests` was not run** - it needs valgrind or MSan.
- M5 P-core only. LVP is a P-core feature, so expect a much smaller effect on
  E-cores.
