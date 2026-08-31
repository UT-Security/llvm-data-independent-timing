# libsecp256k1 signing: no under-taints, and coarse beats fine

**Measured 2026-08-30** with the gem5 shadow-taint oracle
(`gem5-DIT/benchmarks/taint_oracle/run_secp_gem5.sh`). Compiler: `dit-tainter`
at `d32cd11`, default flags. Guest: libsecp256k1 ECDSA signing, key seeded by
m5 op, 2 signatures.

Two results. The soundness one closes the question
`dit-flowprobe-undertaints.md` §5 left open (*"are the four channels reachable
in real crypto code?"* - not here). The performance one goes the other way:
blanket DIT beats our placement on this workload by 1.02 points, which is what
the framework predicts at `f_secret` near 100%.

---

## 1. Why this needed a different instrument

`dit-flowprobe-undertaints.md` found four channels that carry a secret into a
consumer the analysis believes is clean. All four are **derived-secret
propagation**: a returned pointer, a global read by a sibling, an inline-asm
store, a NEON register tuple. None of them is a read of the seeded buffer.

The host-side oracle cannot see any of them. It protects the secret's pages, so
it observes loads and stores of the *key buffer* and nothing else; everything
derived from the key lives in registers and on the stack. Its result on this
same workload (`docs/design/verification.md` §4.1) was that the raw key is read
at exactly two sites, both protected. True, and silent about the question.

Shadow taint in the simulator tracks the derived values, which is what makes it
the instrument for this.

---

## 2. Result

| arm | secret ops protected | secret ops with DIT clear | distinct sites |
|---|---|---|---|
| hardened (default flags) | **464,796** | **4** | 2 |
| no `-ftaint-harden` (control) | 0 | 464,800 | 7,495 in 66 functions |

**Zero under-taint sites inside libsecp256k1.** Every instruction in the library
that computed on secret-derived data ran with `PSTATE.DIT` set.

### The four that remain are in the driver, and are not a leak

Both sites are in the benchmark's own `main`, at the call site:

```c
if (secp256k1_ecdsa_sign(ctx, &sig, msg, seckey, NULL, NULL)) ok++;
```
```
subs   w0, #0
csinc  x22, x22, x22, eq
```

That is the API's success/failure return code. Dynamic taint marks it
secret-derived because it was computed inside secret-handling code, and it is
genuinely a function of the key (the call fails for an invalid seckey). It is
also published by design.

**This is the declassification gap, on real code.** Without a declassification
annotation the oracle cannot distinguish "the pass wrongly left this
unprotected" from "this value is released deliberately", and the same applies to
the ciphertext, the signature, and any other public output. Cited as the
concrete argument for building it: see `related-work.md` §3a for CryptoMPK's
`mxor` tag and the 3.1x precision it bought them.

---

## 3. Coarse versus fine on this workload: coarse wins

The oracle answers a soundness question. The performance question is separate,
and on this workload it goes the other way.

| arm | cycles | vs no DIT | DIT suppressions |
|---|---|---|---|
| no DIT (round-trip control) | 279,505 | - | 0 |
| **coarse**: blanket, `msr DIT` once in `main` | 280,322 | **+0.292%** | 123,351 |
| **fine**: our selective placement | 283,188 | **+1.318%** | 122,089 |

**Fine-grained is 1.02 points worse than blanket here**, and the suppression
counts say why: 123,351 against 122,089. The two arms protect essentially the
same instructions, so selectivity buys no reduction in dwell, and the switch
bill is left with nothing to offset it.

That is the framework's Q2 answer, not a surprise. `f_secret` on ECDSA signing
is about 100%: there is no public work for selective placement to spare. This
is the losing end of the curve, and it is worth citing precisely because it is
one unmodified crypto library rather than a constructed composite - nobody can
object that the workload was chosen to lose.

The general answer is on the crossover rig, which has a public lane this does
not: selective wins at every secret fraction from 0.018% to 60% on M5
(`c_P = 12.66%`, `f* = 73-78%`) and crosses at 49.8% under gem5. See
`dit-crossover-measured` (memory) and `evaluation-framework.md` §4.

**The blanket arm is worth understanding as a baseline.** It is one instruction:
`msr DIT, #1` at the top of `main`, never cleared, on otherwise identical
codegen. Complete protection, no analysis, no risk of a missed channel. That
baseline does not exist for a memory-isolation scheme - "grant everything" is
not weaker protection, it is none - which is why CryptoMPK and every prior
selective system is measured against unprotected code and can only win. See
`related-work.md` §3b.

Note the round-trip control is the right baseline for both arms: going through
the taint flow with an empty seed is **0.5% faster** than not going through it
at all on this workload, so comparing against a plain build would understate
DIT's cost by roughly 40%.

---

## 4. The control is what makes the number mean anything

A zero from an instrument that is not looking is worthless. Built without
`-ftaint-harden`, the identical code reports **464,800** under-tainted ops
across 7,495 sites:

| function | sites |
|---|---|
| `secp256k1_u128_accum_mul` | 2,980 |
| `secp256k1_sha256_transform_impl` | 1,765 |
| `secp256k1_u128_rshift` | 426 |
| `secp256k1_u128_mul` | 237 |
| `secp256k1_i128_accum_mul` | 189 |
| `secp256k1_fe_mul_inner` | 187 |

The instrument sees the whole signing path. The hardened arm's 4 is a real 4.
`run_secp_gem5.sh` asserts this rather than leaving it to the reader: the
control must report more than 10,000, and must protect nothing.

---

## 5. Scope, stated so the result is not overread

- **One library, one operation, one input.** ECDSA signing only. Verification,
  key generation, ECDH and the Schnorr path are untested.
- **The secret enters by argument pointee**, which is exactly the channel the
  mod-set call-site gate was measured on (`dit-modset-callsite-gate`). This
  says the four channels do not arise on this path. It does **not** exonerate
  the gate in general. C1 (returning a pointer into a secret buffer) and C3
  (`asm volatile` as an optimisation barrier, see
  `dit-constant-time-volatile-barrier`) are ordinary idioms in crypto code.
- **Dynamic, so it exhibits counterexamples and cannot prove absence.** The
  static verifier (`design/verification.md` §3) covers all paths, but only for
  what the analysis already decided to protect.

### One number NOT to quote

52,516 ops ran with DIT set and no secret operand. **That is not a
false-positive rate.** DIT regions cover contiguous code, so untainted
instructions inside a genuine region are expected and correct.

It does however bound what better precision could ever be worth here. Those ops
are 10.15% of everything covered, and DIT's whole cost on this workload is
1.318%, so a perfect analysis could recover at most **~0.13% of runtime** - and
§3 shows that even a perfect one still loses to blanket, because the problem is
not precision. That is the measured argument for NOT building a declassification
annotation, which the shape of §2 might otherwise suggest.

---

## 6. Reproducing

```
cd gem5-DIT/benchmarks/taint_oracle
./run_secp_gem5.sh            # ROUNDS=2 by default; four arms, a few minutes each
```

Builds all four arms, runs them, prints the coarse-versus-fine comparison,
symbolizes any remaining under-taint site, and fails if one lands inside
libsecp256k1. `SECP=` points at another checkout;
`LLVM_BUILD=` at another compiler.

---

## Sources

`docs/results/dit-flowprobe-undertaints.md` (the four channels, and the question
this answers), `docs/design/verification.md` (both instruments and why neither
subsumes the other), `docs/design/context-insensitivity.md` (why the channels
exist), `docs/research/related-work.md` §3a (declassification prior art).
