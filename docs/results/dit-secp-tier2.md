# libsecp256k1 signing: no under-taints, and coarse beats fine

**Measured 2026-08-31** with the gem5 shadow-taint oracle
(`gem5-DIT/benchmarks/taint_oracle/run_secp_gem5.sh`). Compiler: `919569c5`
(branch `nopctl-gate`, one docs-only commit ahead of `dit-tainter`, so codegen
is `dit-tainter`'s), default flags. Guest: libsecp256k1 ECDSA signing, key seeded by
m5 op, 2 signatures.

> **The performance numbers in §3 were re-measured on 2026-08-31 and every one
> of them changed.** The first run compared arms built to different file names,
> and in gem5 SE mode the binary path is written onto the initial process stack
> as `argv[0]`, so its LENGTH shifts stack alignment for the whole run. That is
> worth up to 0.84% here, more than the effect being measured. §3 now runs every
> arm from one identical path. The soundness result in §2 is unaffected: it
> counts instructions, not cycles. Superseded numbers are listed in §3.1.

Two results. The soundness one closes the question
`dit-flowprobe-undertaints.md` §5 left open (*"are the four channels reachable
in real crypto code?"* - not here). The performance one goes the other way:
blanket DIT beats our placement on this workload by 0.489 points, which is what
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
| no DIT (round-trip control) | 275,721 | - | 0 |
| **coarse**: blanket, `msr DIT` once in `main` | 281,947 | **+2.258%** | 123,694 |
| **fine**: our selective placement | 283,325 | **+2.758%** | 122,159 |

**Fine-grained is 0.489 points worse than blanket here**, and the suppression
counts say why: 123,694 against 122,159. The two arms protect essentially the
same instructions, so selectivity buys no reduction in dwell, and the switch
bill is left with nothing to offset it. Per suppressed op the coarse arm pays
0.050 cycles and the fine arm 0.062; the gap is the toggles, executed at loop
frequency, buying 1,535 fewer suppressions.

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

The round-trip control costs **exactly nothing** here: `-ftaint-harden` with an
empty seed and a plain `-O2` build measure the same cycle count (275,721), the
same `simInsts` (549,035) and the same zero suppressions, which is what should
happen given their `.text` is byte-identical. An earlier version of this
document reported the round-trip as 0.5% *faster* than a plain build. That was
the `argv[0]` artifact, not a codegen effect, and the two arms agreeing to the
cycle once they share a path is the cleanest confirmation of the diagnosis.

### 3.1 Superseded numbers

Do not quote these; they are recorded so the change is auditable.

| arm | first run (confounded) | corrected |
|---|---|---|
| no DIT (round-trip) | 279,505 | 275,721 |
| coarse / blanket | 280,322 (+0.292%) | 281,947 (+2.258%) |
| fine / our placement | 283,188 (+1.318%) | 283,325 (+2.758%) |
| **coarse advantage** | **1.02 points** | **0.489 points** |

The direction survived; the magnitude did not, and both absolute DIT costs were
understated by roughly 2x. The arms were also built hours apart against a
compiler that changed underneath them (the callee-saved PSTATE.DIT ABI series
landed in between), which is why the run now prints its compiler commit.

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
- **The 0.489-point coarse-over-fine gap has NOT been separated from code
  layout.** Coarse and fine are different binaries, so some of the gap is the
  incidental alignment difference of inserting 20 `msr DIT` sites rather than
  one, and none of it is attributable to DIT semantics until the NOP control
  (`-taint-dit-nop-switches`, which emits every switch as `HINT #0` at identical
  size) is run as a fourth arm here. It has not been. Given that this whole
  section was previously wrong by a layout-class artifact, the direction should
  be treated as established and the magnitude as provisional. See
  `dit-alignment-control` (memory).

### One number NOT to quote

52,516 ops ran with DIT set and no secret operand. **That is not a
false-positive rate.** DIT regions cover contiguous code, so untainted
instructions inside a genuine region are expected and correct.

It does however bound what better precision could ever be worth here. Those ops
are 10.15% of everything covered, and DIT's whole cost on this workload is
2.758%, so a perfect analysis could recover at most **~0.28% of runtime** - and
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

Three gates it now enforces, each of which has produced a wrong number before:

- **Every arm runs from one identical path** (`/tmp/dit_arm_bench`). Comparing
  arms where they are built confounds the result with `argv[0]` length. Only the
  length matters, not the text: two same-length names agreed to the cycle.
- **A failed build aborts the run.** It used to be `build_secp.sh >/dev/null
  2>&1` with no exit check, so a compile error left the previous arm's binary in
  place and the comparison came out fresh-versus-stale, still reporting PASS.
- **The compiler commit and clang build time are printed** with the results, so
  a write-up cannot cite a hash the arms were not built with.

---

## Sources

`docs/results/dit-flowprobe-undertaints.md` (the four channels, and the question
this answers), `docs/design/verification.md` (both instruments and why neither
subsumes the other), `docs/design/context-insensitivity.md` (why the channels
exist), `docs/research/related-work.md` §3a (declassification prior art).
