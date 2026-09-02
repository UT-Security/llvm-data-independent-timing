# The frame-address gap: why 97.61% of a signing path ran unprotected

**Status: partly diagnosed. One half fixed behind a default-off flag and
measured worthless on its own; the root cause on the target the oracle measures
is still unidentified.**
Written 2026-09-02 from the experiment 08 oracle result
(`paper_experiments/08-seed-ground-truth`).

---

## 1. The observation

The gem5 shadow-taint oracle, on libhydrogen's signing path with the seed a
developer would naturally write (`hydro_sign_create,4,pointee`):

| arm | under-taint ops | protected | unprotected |
|---|---|---|---|
| null (unhardened control) | 456,194 | 0 | 100% |
| **hardened, natural seed** | **445,276** | 10,918 | **97.61%** |
| hardened, keygen-buffer seed | 126 | 456,068 | 0.03% |

Hardening bought 2.4% of the secret work. Nothing warned: the information-loss
report emitted **zero** records, the ESCAPE report zero, stderr nothing.

## 2. The defect chain

`hydro_sign_prehash` derives the ephemeral secret by hashing the long-term key:

```c
hydro_hash_state st;                        /* a LOCAL, in this frame */
hydro_hash_init(&st, zero, sk);             /* inlined; sk lands in st */
hydro_hash_update(&st, eph_sk, 32);
hydro_hash_final(&st, eph_sk, 32);          /* <-- a real call */
hydro_x25519_scalarmult_base_uniform(eph_pk, eph_sk);
```

1. `hydro_hash_init` is **inlined**, so the secret is written into `st` by code
   in the caller. The analysis gets this right: the stack cell is tainted.
2. `&st` is then passed to `hydro_hash_final`. Post-prologepilog that address is
   a bare `$sp + imm`. **No register carries taint** - the secret is in a memory
   cell, and the pointer to it is an ordinary integer.
3. So the callee is never told its arg-0 pointee is secret. It taints nothing,
   and `computeFunctionMemEffects` records an **empty mod-set** for it.
4. The empty mod-set means the caller learns nothing about `eph_sk`, which the
   callee just filled with secret bytes.
5. `eph_sk` stays public, so the scalar multiply is public, so the entire X25519
   ladder runs with DIT clear.

**On macOS the call-site gate is what suppresses it - and ON LINUX IT IS NOT.**
That difference is measured, not assumed, and it is the most important caveat
here. Same source, same seed file, same compiler, only the target triple varies:

| target | gate on (default) | gate off | curve functions analysed |
|---|---|---|---|
| `arm64-apple-darwin` | coverage 12.0% | **99.2%** | 0 -> **2** |
| `aarch64-unknown-linux-gnu` | coverage 21.0% | **21.0%** | 0 -> **0** |

So on darwin the gate is the last link in the chain, and on Linux the chain is
already broken somewhere upstream of anything a mod-set could carry.
`-DHYDRO_GEM5_SE` is not the variable (Linux with and without it are identical),
so it is a codegen difference - inlining and frame layout are the candidates.

**This matters for what may be claimed.** The oracle's 445,276 was measured on
the *Linux* binary, so the "gate suppresses the write-back" story does NOT
explain it. Do not state the gate as the root cause; on the workload the oracle
actually ran, it is not.

Turning the gate off is not the fix in any case: it costs +51.20% on Bitcoin
Core `ConnectBlockAllEcdsa` against +0.67% with it on, and on Linux here it buys
nothing at all.

## 3. Two sub-gaps, not one

| | direction | shape | status |
|---|---|---|---|
| **A** | caller -> callee | passing `&local_secret` in; the pointer register carries no taint | **fixed** behind `-taint-frame-addr-args` |
| **B** | callee -> caller | the callee writes a secret through a pointer that is the CALLER'S OWN argument, not a frame object | **open** |

**A** is bridged with P1b's per-object frame provenance: if the argument
register is known to point at a frame object holding a tainted cell, the
callee's parameter is marked pointee-tainted. Applied in two places, because
both are needed and they are separate code paths:

- `taintedCallArguments` - so the mod-set gate's call-site test answers "yes".
- the fixed-point iteration - so the callee's parameter is actually marked.

**B** remains because `getFrameRef` resolves only to frame objects. When the
callee's mod-set says "writes a secret through arg 1" and the caller passed a
pointer *derived from its own incoming argument*, P1b cannot name the object, so
it falls back to a blunt `ExternalMemClobbered`. That poisons subsequent *loads*
in the caller, but the caller does not load `eph_sk` - it passes the pointer on,
and passing a pointer does not consult the clobber. The fix is to extend pointer
provenance beyond frame objects to argument-derived pointers, which the store
side already models (`CellInfo::Arg`).

## 3b. The two gaps as runnable code

Both reproduce in a single TU, so neither is the cross-TU limit. Sources in
`playground/frame_addr_gap/`. **A function absent from the precision report was
never analysed** - its secret-dependent instructions run with DIT clear.

### Gap A: the caller taints its own frame object and passes the address

```c
__attribute__((noinline)) uint64_t consume(const uint64_t *p) {
    uint64_t a = 1;
    for (int i = 0; i < 4; i++) a = a * p[i] + 3;   /* secret multiply */
    return a;
}
uint64_t entry(const uint64_t *key) {               /* seed: entry,0,pointee */
    uint64_t local[4];
    for (int i = 0; i < 4; i++) local[i] = key[i] ^ 0x55;  /* taints local */
    return consume(local);                          /* passes &local */
}
```

The analysis gets the first half right - `taint stack cell FI=1 off=0 sz=8`. Then
`&local` is `ADDXri $sp, imm`, an ordinary integer, and the call passes nothing
the callee can be told about:

```
default                consume ABSENT from the report      <- multiply unprotected
-taint-frame-addr-args consume need=8 switches=2 coverage=90.0
                       caller entry -> callee consume: arg 0 now pointee-tainted
                       (frame object 1 holds a secret, via $x0)
```

### Gap B: the write-back target is the caller's own argument

Same callee reached two ways. Only one works.

```c
__attribute__((noinline)) void produce(uint64_t *out, const uint64_t *key) {
    for (int i = 0; i < 4; i++) out[i] = key[i] * 3;   /* secret through arg0 */
}

uint64_t via_local(const uint64_t *key) {      /* seed: via_local,0,pointee */
    uint64_t buf[4];                           /* a FRAME OBJECT */
    produce(buf, key);
    return consume(buf);
}

uint64_t via_argptr(uint64_t *buf, const uint64_t *key) {  /* seed: ...,1,pointee */
    produce(buf, key);                         /* buf is OUR OWN ARGUMENT */
    return consume(buf);
}
```

`produce`'s summary is precise and correct in both cases - `mem-effects[produce]:
arg0`. What differs is whether the caller can name the object it passed:

```
via_local   call to produce writes secret through a pointer arg
            (P1b: tainted 1 caller object(s) precisely)     <- works
via_argptr  call to produce writes secret through a pointer arg
            (P1a: blunt clobber, provenance unknown)        <- fails
            mem-effects[via_argptr]: UNKNOWN(TOP)
```

`getFrameRef` resolves frame objects only, so for `via_argptr` P1b falls back to
`ExternalMemClobbered`. That poisons subsequent *loads* in `via_argptr` - but it
never loads `buf`, it passes the pointer on, and passing a pointer does not
consult the clobber. With the working caller deleted (`gapB_only.c`), `consume`
is absent from the report under **both** arms: `-taint-frame-addr-args` does not
help, because there is no frame object to resolve.

### They compose, and one good caller hides the other

In `gapB.c` with both callers and `-taint-frame-addr-args`, `consume` comes out
covered - **but only because `via_local` instrumented the shared body**. The
analysis is context-insensitive, so one well-analysed caller silently fixes the
function for every other caller. That is why `gapB_only.c` exists, and it is a
general warning for anything measured this way: a defect can be masked by an
unrelated call site.

### Where each one is in libhydrogen

| gap | libhydrogen site |
|---|---|
| A | `hydro_x25519_core` passing limb arrays on its own frame to `hydro_x25519_mul` |
| B | `hydro_hash_final` filling `eph_sk`, which points into `csig` - the caller's own argument, not a frame object |

## 3c. Proposed fix for gap B: generalise provenance from Frame to Arg

**The concept is already half-built.** `CellInfo` on the STORE side already has
an `Arg` kind carrying an `ArgNo`, and that is what lets
`computeFunctionMemEffects` record `WritesSecretThroughArgPointee` precisely.
What is missing is the symmetric half on the DATAFLOW side:

| | exists | missing |
|---|---|---|
| pointer provenance | `FrameRefs`: reg -> frame index | reg -> "the object argument k points at" |
| memory cells | `TaintedStackCells`, `TaintedGlobalCells` | tainted cells keyed by `ArgNo` |

So the caller can *classify a store* as "through arg k" but cannot *remember*
that the object arg k points at now holds a secret, and cannot recognise a
register as pointing at it.

### The changes

1. **`TaintState::FrameRefs` becomes a `PointerBase` map**: reg -> `Frame(FI)`
   *or* `Arg(k)`. One variant added to an existing map.
2. **Seed it at entry**: each pointer-typed incoming argument register k starts
   with base `Arg(k)`.
3. **Propagate it exactly as frames are propagated**: a `COPY` inherits the
   base; every other def kills it; **pointer arithmetic is NOT followed**. Same
   conservatism, same reason - a computed offset can leave the object, and
   attributing to the wrong object is the under-taint direction.
4. **Add `TaintedArgCells`**, whole-object granularity keyed by `ArgNo`, matching
   what `anyTaintedStackCellForFI` does for frames.
5. **P1b application at the call site**: where it currently does
   `getFrameRef(reg)` and falls back to `setExternalMemClobbered()`, it consults
   the general base and, for `Arg(k)`, sets the arg cell instead.
6. **Load path**: an MMO classifying as `CellInfo::Arg` with a tainted `ArgNo`
   returns secret.
7. **`computeFunctionMemEffects`**: a tainted `Arg(k)` cell at exit becomes
   `WritesSecretThroughArgPointee.insert(k)`. **This is what makes it compose
   upward, and it needs no new consumer** - callers already read that field.

### Stage it in two halves, and measure them apart

This is the part the project's own history argues for: the `+44 points` and
`408 -> 628` verdicts in `p1b-frame-provenance.md` §4 cannot now be attributed to
a mechanism, because two effects were changed at once.

- **B1, naming.** Items 1-7 above, used ONLY to replace the blunt fallback.
  Expected to *reduce* switches: it substitutes a named object for
  `ExternalMemClobbered`, which today poisons every subsequent load in the caller
  AND re-exports as TOP to that caller's callers.
- **B2, consumption.** Let a pointer whose base is a tainted `Arg(k)` count as
  pointee-tainted when passed onward - the same rule as gap A's fix, generalised
  from `Frame` to any base. **This is the half that actually closes the leak**,
  and the additive half that costs switches.

### Why B1 is probably a WIN, not a cost

Unlike gap A, this is substitutive rather than additive. Counting the two
outcomes of the P1b application on libhydrogen (summed over the fixed point, so
read the ratio, not the absolute):

| outcome at a call site whose callee writes through a pointer arg | count |
|---|---|
| `P1b: tainted 1 caller object(s) precisely` | 644 |
| `P1a: blunt clobber, provenance unknown` | **1253** |

**Two thirds of these call sites already degrade to a whole-caller clobber.**
Not all 1253 will have `Arg(k)` provenance - some are heap, some genuinely
unresolvable - but every one that does is a flood replaced by a name. The
corroborating figure: 47 of 53 functions with a mod-set on libhydrogen export
`UNKNOWN(TOP)`.

### What to watch

- **`Arg(k)` is context-insensitive** - "the object arg 0 points at" is a
  different object per caller. But that is exactly the existing
  `WritesSecretThroughArgPointee` semantics, and P1b resolves it per caller at
  the next level down, so the imprecision is bounded at one level rather than
  compounding.
- **Heap pointers remain unnamed.** B does not fix a secret written into
  `malloc`ed memory whose pointer is not argument-derived. That stays TOP, and
  it is the right answer until there is a heap-object abstraction.
- **Verify on the ORACLE, not the precision report.** Gap A improved the report
  and moved the oracle by exactly zero. The gate for B is
  `paper_experiments/08-seed-ground-truth` going from 445,276 toward the 126 the
  keygen-buffer seed already achieves, plus `gapB_only.c` showing `consume`
  analysed at all.
- **Gap B is target-independent**, unlike the call-site gate: `gapB_only.c`
  leaves `consume` unanalysed on both `arm64-apple-darwin` and
  `aarch64-unknown-linux-gnu`. That is the reason to believe it is the real cause
  on the Linux binary the oracle measures, where the gate is irrelevant.

## 4. Measured effect of the A fix, and why it is not enough

libhydrogen, natural seed, `-mllvm -taint-frame-addr-args`, **darwin object**:

| | off | on |
|---|---|---|
| `msr DIT` in the library | 27 | 46 |
| `hydro_x25519_mul` | not analysed | need=104, **coverage 99.3%** |
| `hydro_x25519_sc_montmul` | not analysed | need=79, **coverage 100%** |
| `hydro_sign_final_create` | coverage 12.0% | coverage 12.0% (gap B) |

Two of experiment 08's seven come back under coverage. **And it is worth nothing
dynamically.** Run through the gem5 oracle on the Linux guest, the flagged arm is
bit-for-bit the same result as the unflagged one:

| arm | under-taint ops | sites |
|---|---|---|
| create (natural seed) | 445,276 | 611 |
| create + `-taint-frame-addr-args` | **445,276** | **611** |
| create + `-taint-no-modset-gate` | **445,276** | **611** |

Gap A is real and the fix closes it, but the 445,276 lives downstream of gap B:
the ephemeral key never becomes secret at all, so the whole ladder is public no
matter what the curve helpers can pass between themselves. **B is the
load-bearing half. Do A second, or not at all.**

## 5. Cost, and why the flag is DEFAULT OFF

This rule has been tried and rejected once
(`docs/design/p1b-frame-provenance.md` §4): gate + fallback went 408 -> 628
switches on libsecp256k1, and `ecdsa_verify` - a path that handles only public
data - went from 0 switches back to 12.

Re-measured on the current tree, it is much cheaper than that verdict suggests.
libsodium, CIO-parity seed (77 lines), full cross build:

| | `msr DIT` |
|---|---|
| `-ftaint-harden` | 134 |
| `+ -taint-frame-addr-args` | **152** (+13.4%) |

+13.4% is not +54%. But **libsecp256k1 has not been re-measured**, and that is
where the false positives were, so the old verdict stands until it is. The flag
stays off.

**The decision rule is not "does it improve coverage".** It will, always -
adding taint always does. It is: does the coverage it adds correspond to real
secret dependence the oracle can confirm, at a switch cost the workloads can
carry? On the evidence so far, gap A fails the first half of that test.

## 6. What should ship regardless: say something

The strongest finding from experiment 08 is not the miss, it is the **silence**.
97.61% of secret operations unprotected and every report empty.

The information-loss report already has the right shape for this - where, why,
what it cost, and a pasteable repair - and its severity criterion is consequence
at a call boundary. A frame-address argument IS a call boundary event:

```
taint-stop frame-addr  in=hydro_sign_final_create callee=hydro_hash_final arg=0
  severity  severe
  action    the argument is the address of a stack object holding a secret, and
            pointee taint does not transfer through a bare frame address
  cost      the callee runs unprotected, and whatever it writes back stays
            public in this caller
  repair    seed the callee's parameter:
              hydro_hash_final,0,pointee
```

That costs no performance, needs no policy decision, and turns a silent 97.61%
into a line the developer can act on. **It should land before either half of the
placement fix.**

## 7. Order of work

1. **Report the gap** (§6). Unconditional, no cost, closes the tooling hole, and
   it is the only item here that is unambiguously right regardless of how the
   rest lands.
2. **Find the Linux-target root cause.** The oracle measures the Linux binary and
   neither the gate nor gap A explains its 445,276. Until that is named, nothing
   else can be verified against the instrument that matters. Start by diffing
   what taints in `hydro_sign_final_create` between the two targets - 21.0% vs
   12.0% coverage says the two builds lose the secret in different places.
3. **Close gap B** (§3c), in two measured halves: B1 naming, expected to reduce
   switches by substituting a named object for a whole-caller clobber at the
   two-thirds of sites that take the blunt fallback today; then B2 consumption,
   which is what actually closes the leak. The target is libhydrogen's natural
   seed reaching the 0.03% the keygen-buffer seed already achieves.
4. **Re-measure `-taint-frame-addr-args` on libsecp256k1** and decide its
   default. Only worth doing after B, since A alone moves nothing.
5. Re-run the experiment 08 oracle after each step. It is the only instrument
   that separated a real fix from a plausible one here, twice.

## 8. Two traps this investigation hit

- **A precision report is not a dynamic result.** `-taint-frame-addr-args`
  improves the darwin report (two curve functions at ~100% coverage) and changes
  the oracle by exactly zero. Always close the loop on the oracle.
- **A conclusion drawn on one target may not hold on another.** The gate is the
  proximate cause on darwin and is irrelevant on Linux, from identical source.
  Pin the target before writing a mechanism down.
