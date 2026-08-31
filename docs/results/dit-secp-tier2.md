# libsecp256k1 signing: no under-taints, and coarse beats fine on dwell

**Measured 2026-08-31** with the gem5 shadow-taint oracle
(`gem5-DIT/benchmarks/taint_oracle/run_secp_gem5.sh`). Compiler: `8aaf61cf`
(`dit-tainter`), default flags. Simulator: `gem5-DIT` at `3a78a102`. Guest:
libsecp256k1 ECDSA signing, key seeded by m5 op, **20 signatures**, averaged over
8 `argv[0]` lengths.

> **§3 has been corrected TWICE on 2026-08-31 and §2 strengthened once. Read
> §3.1 before quoting anything from an earlier revision.**
>
> The first correction made every arm run from one identical path, because in
> gem5 SE mode `argv[0]` sits on the initial process stack and its LENGTH moves
> the initial SP. That was necessary and **not sufficient**: sharing one path
> removes the bias between arms, but the delta is still a function of that
> arbitrary path, and at the old 2-signature workload the coarse-versus-fine
> verdict flipped sign across path lengths. §3 is now averaged over **eight**
> `argv[0]` lengths at **20** signatures, and reports a confidence interval with
> an explicit *not resolved* verdict rather than a single number.
>
> §2 got **more** trustworthy, not less: the oracle had two precision faults
> that manufactured under-taints, both now fixed, and the result is re-taken at
> 10x the coverage. See §2.1.

Two results. The soundness one closes the question
`dit-flowprobe-undertaints.md` §5 left open (*"are the four channels reachable
in real crypto code?"* - not here). The performance one goes the other way:
blanket DIT beats our placement on this workload by **1.94 points**
[+0.53, +2.52], which is what the framework predicts at `f_secret` near 100%.
The NOP control says that cost is DIT running rather than the code-shape change
of inserting the switches.

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

At **20** signatures, with the oracle faults of §2.1 fixed:

| arm | secret ops protected | secret ops with DIT clear | distinct sites |
|---|---|---|---|
| hardened (default flags) | **4,647,778** | **40** | 2 |
| no `-ftaint-harden` (control) | 0 | 4,647,818 | 7,495 |

**Zero under-taint sites inside libsecp256k1.** Every instruction in the library
that computed on secret-derived data ran with `PSTATE.DIT` set.

This claim was originally made at 2 signatures. It now holds at 20, which
matters because raising the count is what exposed the oracle faults below: at 20
the *unfixed* oracle reported 6 sites, 2 of them inside the library, all of
which turned out to be its own artifacts.

### The two that remain are in the driver, and are not a leak

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

### 2.1 Two oracle faults that manufactured under-taints

Both were fixed on 2026-08-31 (`gem5-DIT`: `sim,cpu-o3: separate the taint
oracle's address and data channels`), and both are mirror images of faults the
compiler side had already fixed.

**Address and data were one bit.** The propagation OR-ed every source register
into one `src_tainted`, so a pointer that had once touched a secret made every
access through it a finding. `PSTATE.DIT` covers data-operand timing and nothing
else; a secret-dependent *address* is the cache/TLB channel, which DIT does not
cover. They are now separate channels and only the data channel can be an
under-taint.

**Load-pair payload granularity**, which is what actually produced the false
findings. One `memTainted()` over the whole access was given to every
destination of a multi-destination load, so on `ldp_uop x22, x21, [ureg0]` a
tainted neighbour contaminated a clean register. Concretely: `ok`, which is
secret-derived by design as the API return code, tainted the adjacent spill slot
of `&msg`, and every subsequent use of that pointer - including the argument
move into the callee - was reported. That accounted for 4 of the 6 sites,
including both library ones. Each destination now takes the taint of its own
byte slice. This is the same class as
`TargetInstrInfo::getNumStoredValueRegs`.

| | unfixed oracle | fixed |
|---|---|---|
| under-taint ops | 112 | **40** |
| distinct sites | 6 | **2** |
| sites inside libsecp256k1 | 2 | **0** |
| ops consuming a secret ADDRESS | (not tracked) | 120 |

**Validated three ways**, because reasoning about it was wrong twice: the
unhardened control still reports 4,647,818 under-taints across the same 7,495
sites, so the data channel is intact; the slice mapping is self-checking, since
reversing it would make the genuinely tainted half of the pair come back clean
and the `ok`-derived sites disappear, and they survive; and `flowprobe_gem5`,
whose four channels are deliberate, still reports all four, corroborated by the
guest's own `mrs DIT` readings.

**The lesson generalises past this bug.** An over-approximating oracle is not
"safe" the way an over-approximating analysis is. A spurious finding costs
credibility and, worse, buries the real ones: here 4 false sites sat alongside
2 true ones and pointed at the wrong functions.

---

## 3. Coarse versus fine on this workload: coarse wins

The oracle answers a soundness question. The performance question is separate,
and on this workload it goes the other way.

Measured over **8 `argv[0]` lengths** (18/22/26/30/34/38/42/46) at **20
signatures**, `ROUNDS=20`. Percentages are against the round-trip control at the
same offset; the interval is a 95% t-interval over the eight offsets.

| term | median | mean | 95% CI | offsets +ve | verdict |
|---|---|---|---|---|---|
| **coarse**: blanket vs no-DIT | +0.167% | +0.360% | [−0.17, +0.89] | 7/8 | **not resolved** |
| **layout only**: NOPed vs no-DIT | −0.124% | −0.319% | [−0.95, +0.31] | 3/8 | **not resolved** |
| **fine**: our placement vs no-DIT | +1.963% | +1.886% | [+1.19, +2.58] | 8/8 | **resolved > 0** |
| **fine vs coarse** | +1.935% | +1.526% | [+0.53, +2.52] | 7/8 | **resolved > 0** |

Three things follow, and the third is the reason the NOP arm exists.

**Our placement costs about 1.9% here, and that is real.** It is the only term
positive at every offset, and its interval clears zero comfortably.

**Blanket DIT is not distinguishable from free on this workload.** Its interval
spans zero. That is not "blanket is cheap" stated loosely - it is that this rig
cannot resolve a term that small, and neither figure should be quoted as a value.

**The cost is DIT running, not the code-shape change of inserting the
switches.** The NOP arm (`-taint-dit-nop-switches`, every `MSR DIT` emitted as
`HINT #0` at identical size, so identical instruction count and identical
addresses) is *indistinguishable from the unhardened build*: median −0.124%,
interval spanning zero, negative at 5 of 8 offsets. So none of fine's 1.9% is
attributable to layout.

That last point is worth stating against the precedent, because it does not
generalise. On SQLCipher, NOPing all 121 HMAC/SHA switches still cost +17.10 pp
serializing and +4.05 pp renamed - under a renamed switch, the majority of the
total - because region placement split a hot compression loop there. Here the 20
sites sit where the restructuring does not hurt. **Whether a placement's cost is
dwell or layout is a property of the workload, not of the pass**, and only the
NOP arm distinguishes them.

**Why the offsets are not optional.** The no-DIT baseline alone spans **2.41%**
across the eight lengths. Each path character adds 2 bytes to the guest's initial
stack frame (the filename counts once as `AT_EXECFN` and again as `argv[0]` in
`src/arch/arm/process.cc`), and the SP is then rounded down to 16, so the initial
SP is a step function of path length. Every stack address moves with it, which
changes pointer values and therefore what the machine's value-based
optimisations can fold - so it perturbs each arm differently rather than adding a
constant offset. A single-path measurement of a sub-2% effect on this rig is not
a measurement.

Coarse beating fine is the framework's Q2 answer, not a surprise. `f_secret` on ECDSA signing
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

Do not quote these. Recorded so the changes are auditable.

| revision | no-DIT | coarse | fine | coarse advantage | fault |
|---|---|---|---|---|---|
| first | 279,505 | 280,322 (+0.292%) | 283,188 (+1.318%) | 1.02 pts | arms built to different file names, hours apart, against a compiler that changed underneath them |
| second | 275,721 | 281,947 (+2.258%) | 283,325 (+2.758%) | 0.489 pts | one shared path, so unbiased, but a single sample of a quantity whose spread is ~2 pp |
| **current** | - | **not resolved** | **+1.963%** | **+1.935% [+0.53, +2.52]** | 8 offsets, 20 signatures, CI reported |

The direction survived both corrections; no magnitude did. Both earlier
revisions also used `ROUNDS=2` (549k instructions), where the effect is the same
size as stack-layout perturbation and the coarse-versus-fine sign flips with the
file name. `ROUNDS` now defaults to 20.

A preliminary reading taken between the second and current revisions - that
roughly 88% of fine's cost was layout - is **retracted**. It came from a single
offset at `ROUNDS=2`; at 20 signatures over 8 offsets the layout term is
indistinguishable from zero.

---

## 4. The control is what makes the number mean anything

A zero from an instrument that is not looking is worthless. Built without
`-ftaint-harden`, the identical code reports **4,647,818** under-tainted ops
across 7,495 sites:

| function | sites |
|---|---|
| `secp256k1_u128_accum_mul` | 2,980 |
| `secp256k1_sha256_transform_impl` | 1,765 |
| `secp256k1_u128_rshift` | 426 |
| `secp256k1_u128_mul` | 237 |
| `secp256k1_i128_accum_mul` | 189 |
| `secp256k1_fe_mul_inner` | 187 |

(Site counts are from the 2-signature run; the ranking is unchanged at 20.)

The instrument sees the whole signing path. The hardened arm's 40 is a real 40,
and the control's site count is **identical** before and after the §2.1 oracle
fixes, which is what shows those fixes removed false positives rather than
blinding the instrument. `run_secp_gem5.sh` asserts this rather than leaving it
to the reader: the control must report more than 10,000 and must protect nothing,
and the NOP arm must carry zero `MSR DIT`, an instruction count equal to the
hardened arm's, and zero suppressions.

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
- **Coarse's own cost is not resolved here**, only fine's. Blanket's interval
  spans zero, so "blanket is nearly free on ECDSA signing" is a statement about
  this rig's resolution, not a measured value.
- **The layout question IS now settled for this workload**, by the NOP arm: the
  layout term is indistinguishable from zero, so fine's cost is dwell. That does
  not carry to other workloads - on SQLCipher the same control showed layout was
  the majority of the cost. Run the NOP arm per workload; see
  `dit-alignment-control` (memory).

### One number NOT to quote

525,342 ops ran with DIT set and no secret operand. **That is not a
false-positive rate.** DIT regions cover contiguous code, so untainted
instructions inside a genuine region are expected and correct.

It does however bound what better precision could ever be worth here. Those ops
are 10.16% of everything covered, and DIT's whole cost on this workload is
1.963%, so a perfect analysis could recover at most **~0.20% of runtime** - and
§3 shows that even a perfect one still loses to blanket, because the problem is
not precision. That is the measured argument for NOT building a declassification
annotation, which the shape of §2 might otherwise suggest.

A separate 120 ops consumed a secret **address** with DIT clear. That is the
cache/TLB channel, which `PSTATE.DIT` does not cover at all, so it is neither an
under-taint nor something this placement could have fixed. It is reported on its
own line precisely so it stops being counted as one (§2.1).

---

## 6. Reproducing

```
cd gem5-DIT/benchmarks/taint_oracle
./run_secp_gem5.sh            # ROUNDS=20, 5 arms x 8 argv[0] offsets
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
