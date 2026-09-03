# The frame-address gap: why 97.61% of a signing path ran unprotected

**Status: gap A fixed behind a default-off flag and measured worthless on its
own. Gap B's NAMING half (B1) is implemented behind `-taint-arg-provenance`,
default off, and measured: it does what it was predicted to do and closes no
leak by itself. B2 (consumption) is
implemented behind `-taint-arg-pointee-args` and closes gap B on both repros -
and libhydrogen turns out to need a THIRD thing neither half provides. The root cause on the target
the oracle measures is still unidentified.**
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

- **Fold in the fixed-frame-object case.** An incoming stack argument slot is a
  fixed frame object with a recoverable argument index, currently sent to TOP
  (`TaintAnalysis.cpp:1739`). Same fix, arguments that arrived on the stack.
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

### B1 as implemented (`-taint-arg-provenance`, default OFF)

Landed 2026-09-02. Seven items, all extensions of existing machinery:

| # | change | where |
|---|---|---|
| 1 | `FrameRefs` becomes `PointerBases`, values tagged `Frame(FI)` or `Arg(k)` | `TaintAnalysis.h` |
| 2 | seed `Arg(k)` for each incoming pointer argument register | `TaintAnalysis.cpp`, entry seeding |
| 3 | `COPY` inherits either kind; every def kills; arithmetic still not followed | `propagateTaintMI` |
| 4 | `TaintedArgPointees`, whole-object, keyed by argument number | `TaintAnalysis.h` |
| 5 | call site resolves to a `PointerBase` and applies per kind | `propagateTaintMI` |
| 6 | a load through a tainted arg pointee reads secret | load path |
| 7 | a tainted arg pointee at exit re-exports as `WritesSecretThroughArgPointee` | `computeFunctionMemEffects` |

**One map, not two.** A tagged value rather than a parallel `ArgRefs` map, so a
def cannot kill one kind and leave a stale base of the other behind. `getFrameRef`
stays deliberately narrow - it returns a frame index only for `Frame`, so a caller
that can only act on a frame index is never handed an argument number.

**The ABI assumption is checked, not assumed.** AAPCS64 puts integer and pointer
arguments in X0-X7 *in order*, so a register's encoding is its argument index only
when every argument takes a GPR. A float or vector argument takes a V register and
shifts the correspondence. The seeding therefore **skips any signature that is not
all integer/pointer**, because naming the WRONG object is an under-taint - the one
direction that loses the secret, and the one place the usual over-approximate
instinct does not apply.

#### Measured: it does what §3c predicted, and no more

`playground/frame_addr_gap/gapB_only.c`, the isolated gap-B repro:

| | off | on |
|---|---|---|
| `mem-effects[produce]` | `arg0` | `arg0` |
| **`mem-effects[via_argptr]`** | **`UNKNOWN(TOP)`** | **`arg0`** |
| call-site decision | `P1a: blunt clobber` | `P1b/B1: named 1 object` |
| `via_argptr` need | 3 | 1 |

The caller's summary stops degrading to TOP and names the argument it was
actually passed - which is the thing B2 will consume. libhydrogen, whole library:

| | blunt fallback | named precisely | `msr DIT` |
|---|---|---|---|
| off | 1,253 | 644 | 318 |
| **on** | **1,010** | **923** | **318** |

**Switch count identical.** B1 is purely substitutive: 279 call sites stop
flooding the caller and start naming an object, and codegen does not move. That
is the predicted shape - and it is also why B1 alone is not worth turning on.

**It closes no leak.** `consume` is still absent from the report in
`gapB_only.c`, and libhydrogen's natural seed still emits 27 switches. Passing a
pointer onward still transfers nothing, because that is B2.

#### A trap worth keeping: never divert a load away from an existing taint source

The first version of item 6 was a separate `else if (CI.K == CellInfo::Arg)`
branch in the load path. It looked right and it **silently under-tainted**:
argument-based loads previously fell through to the unknown/heap branch, which is
where `anyRegUseOfKind(TaintKind::Pointee, ...)` lives - the primary way pointee
taint reaches a loaded value. Intercepting them bypassed it, and the secret
multiply in `produce` fell from **need=12 to need=4** while every test still
passed.

The rule this yields: **an addition to the taint lattice must be additive at the
consumption site too.** Item 6 is now an extra `||` inside the existing branch,
not a branch of its own. Caught only because `gapB_only.c` reports per-function
`need`, which is the argument for keeping a repro whose numbers you know by
heart.

### B2 as implemented (`-taint-arg-pointee-args`, default OFF)

The consumption half: a pointer whose base is a tainted `Arg(k)` counts as
passing a secret. Two sites, both generalisations of gap A's rule from `Frame`
to either base - `taintedCallArguments` (so the gate's call-site test says yes)
and the fixed-point iteration (so the callee's parameter is marked). **Inert
without `-taint-arg-provenance`**, since there is then no `Arg` base to find.

**Interior pointers had to be added to make it work at all.** Following COPY
alone is not enough: the real shape is `&csig[NONCEBYTES]`, an argument base plus
a constant, which is `hydro_sign_prehash`'s `eph_sk`. So an add-immediate off an
`Arg` base now propagates that base. At whole-object granularity the displacement
need not be stored - an interior pointer into argument k's object is still
argument k's object - which is GCC keeping `parm_index` while only
`parm_offset_known` depends on the displacement being constant.

**Only for an `Arg` base, never a `Frame` one.** Frame objects are adjacent with
known bounds, so `&frame_A + large` can land in frame_B and attributing it to A
would UNDER-taint. An argument's object has bounds we cannot see, and in
well-defined C arithmetic within an object stays inside it - the same assumption
`parm_offset` makes.

| repro | baseline | B1 | B2 alone | B1+B2 |
|---|---|---|---|---|
| `gapB_only.c` (pointer passed on) | absent | absent | absent | **ANALYSED** |
| `gapB_interior.c` (interior pointer) | absent | absent | absent | **ANALYSED** |

B2-alone being inert is measured, not asserted, and the lit test pins all four
arms so the two halves stay separately measurable.

#### A second ordering trap, same family as the first

The interior-pointer case silently did nothing at first. `add x0, x0, #0x20` -
the exact shape - **reads and writes the same register**, and the provenance
block kills every def before propagating. The source's base was gone before it
could be read. Now captured first, then killed, then set.

Two ordering bugs in two halves of the same feature, both silent, both caught
only by a repro whose expected output was known. **Neither showed up in 46
tests.**

### The `TargetInstrInfo` hook, and a diagnosis I got wrong

`TII::getPointerDisplacementBases` is the non-constant companion to
`isAddImmediate`: given `add xD, xB, xN` or `add xD, xB, wN, uxtw`, it reports
the operands that could be the pointer base. **Both** are reported, because the
ISA does not distinguish base from index; the caller takes a base only when the
candidates agree on one object, since guessing misattributes provenance. The
AArch64 side handles `ADDXrs`/`ADDXrx`/`ADDXrx64` and skips 32-bit operands,
which cannot hold a pointer and are therefore always the index. Subtraction is
excluded: `p - i` is in-object arithmetic but `p - q` is a pointer difference,
and they are the same instruction.

**It is worth having.** libhydrogen, whole library, switches unchanged at 318
throughout:

| provenance follows | blunt fallback | named |
|---|---|---|
| nothing (no B1) | 1,253 | 644 |
| copies only | 1,010 | 923 |
| **+ variable displacement** | **494** | **1,439** |

Blunt fallbacks down 61% from baseline, still with **zero** switch change. That is
the substitutive shape B1 was supposed to have, now at more than double the reach.

**But it did not unblock libhydrogen, and the reason is that I had named the
third gap wrongly.** The previous revision of this section said the third gap was
variable-offset provenance. The hook now exists and `A+B1+B2` still equals
`A only` exactly - 46 switches, 12.0% coverage, 2 curve functions. **That claim
was wrong and is retracted.**

### The actual third gap: an external callee gives TOP, not a per-argument effect

`hydro_hash_final` calls **`memcpy`**, an external declaration. The blunt-TOP
rule fires - an external callee receiving a secret sets `WritesSecretToUnknown` -
so its summary is `arg0 UNKNOWN(TOP)`: it names the state it updates and says
only "somewhere" about the buffer it fills. **There is no precise arg-pointee
fact for B2 to consume.**

Isolated in `playground/frame_addr_gap/gapB_memcpy.c`, which is
`gapB_interior.c` with the fill done by `memcpy` instead of a loop:

| repro | `produce` mod-set | `consume` |
|---|---|---|
| `gapB_interior.c` (loop) | `arg0` | **ANALYSED** |
| `gapB_memcpy.c`, B1+B2 | *(empty)* | absent |
| `gapB_memcpy.c`, A+B1+B2 | **`UNKNOWN(TOP)`** | absent |

Two things that repro taught, both worth keeping:

- **A constant-size `memcpy` does not reproduce it.** The first version used
  `memcpy(out, tmp, sizeof tmp)`, which lowers to plain stores and resolves
  fine. The size has to be runtime-variable to force a real call.
- **The B1+B2 row is empty rather than TOP** because without gap A the frame
  address `&tmp` is not seen as passing a secret, so `memcpy` never looks like it
  receives one. All three gaps are on the same chain.

**A seed cannot express this, and that is the point.** Measured on
`gapB_memcpy.c` with every placement flag on:

| seed set | `produce` mod-set | `consume` |
|---|---|---|
| workload seed only | `UNKNOWN(TOP)` | absent |
| `+ memcpy,1,pointee` | `UNKNOWN(TOP)` | absent |
| `+ memcpy,0,pointee` | `UNKNOWN(TOP)` | absent |
| `+ both memcpy args` | `UNKNOWN(TOP)` | absent |

Nothing moves, because the seed file declares a **source** - "this argument is
secret" - and what is missing is an **effect**: "this function writes through
argument 0 what it reads through argument 1." The format has no way to say the
second, and `memcpy` has no body to derive it from. **This is the sharp
distinction against §3d:** there, a seed could patch a specific hole at a cost;
here it cannot help at all, because the missing fact is a different KIND of fact.

**The fix is not more pointer provenance, and not more seeds.** It is the libc
model table already deferred in `docs/research/memory-summaries.md` as P1: teach
the analysis that `memcpy(dst, src, n)` writes through argument 0 what it reads
through argument 1. A small table, not an analysis, and it is what turns
`hydro_hash_final`'s `UNKNOWN(TOP)` into `arg0 arg1`.

It belongs in the compiler rather than in the seed file for the same reason:
libc's effects are a fixed, knowable set that every user of the pass would
otherwise have to rediscover and redeclare identically. Extending the seed syntax
with an effect form (`memcpy,0,writes-from,1`) would work, but it would push a
compiler-owned fact onto every user.

### The libc model (`-taint-libc-model`, default OFF)

`getLibcMoveModel` recognises `memcpy`/`memmove`/`mempcpy` and applies their
contract instead of collapsing to TOP: dst's bytes come from src, and nothing
else caller-visible changes. So the call makes memory secret **iff the source is
secret**, and when it does it makes exactly ONE object secret - the fact blunt
TOP throws away. If the destination cannot be named, it falls back to TOP, which
is the right answer rather than a failure.

Four things keep it honest. The callee must be a **declaration** (a definition in
this TU is analysed for real). A **secret-dependent length** falls back to TOP,
since which bytes were overwritten would itself be secret. When a declaration
exists its **signature is checked**, so a local function sharing the name is not
given libc semantics. And the `str*` family is **excluded**: `strcat` also reads
dst and the n-forms write a length derived from the source, both different
shapes.

**`Callee` is usually null here, and that is the normal case.** A C `memcpy`
reaches -O2 as the intrinsic `llvm.memcpy.p0.p0.i64`, which ISel lowers to a
libcall against the bare symbol `memcpy`; the module declares only the intrinsic,
so `M.getFunction("memcpy")` is null. Matching falls back to the symbol name,
which is the same reserved-identifier argument `callCannotMakeMemorySecret` uses
for `operator delete`.

**Measured: it produces the missing fact.** On libhydrogen,
`hydro_hash_final`'s summary goes

| | mod-set |
|---|---|
| without | `arg0 UNKNOWN(TOP)` |
| **with** | **`arg0 arg1 UNKNOWN(TOP)`** |

`arg1` is the output buffer - exactly the fact B2 needs and the one whose absence
this whole section was about.

#### Two more ordering bugs, same family, and the pattern is now clear

Finding this required fixing two more instances of the read-before-kill trap,
both of which made a rule silently inert rather than wrong:

- **A CALL implicitly defines x0** and clobbers the caller-saved registers, so
  the provenance kill wipes the very argument registers the call handling then
  wants to name. Both the P1b/B1 application and the libc model were reading
  post-kill. Fixed with a `PreCallArgBases` snapshot taken before the kill.
- **`taintedCallArguments` had the same problem**, which is worse because gap A
  and B2 both live inside it: the frame-address and arg-pointee rules could never
  see a base at a call. A call's arguments are now read from the state ENTERING
  it. The fixed-point iteration gets this right for free by using `replayTaint`'s
  `Pre` hook; the in-function paths had no such luxury.

**Four ordering bugs across four features, every one silent, none caught by the
47 tests.** The shape is always the same: this instruction's defs are killed
before something else reads its sources. Anything added to this block should be
assumed to have the bug until a repro says otherwise.

And one plain omission worth recording because it wasted a cycle: the
`TargetInstrInfo` hook's opcode switch listed `ADDXrs` but not **`ADDXrr`**, and
`ADDXrr` is what `p + i` actually lowers to. The hook was inert on the real
workload while passing every test.

### Which half of the gate fails: MEASURED, and it is not the naming

With every flag on, the caller-side result is still gap A alone - 46 switches,
`hydro_sign_final_create` at 12.0%, 2 curve functions - and the gate still says:

```
call to hydro_hash_final writes secret through a pointer arg
  but this call site passes no secret: clobber suppressed (callsite-gated)
```

Gap A's rule has two halves: resolve the argument to a frame object, then ask
whether that object holds a secret. Instrumented (`[gate]` under
`-debug-only=taint-analysis`), 106 evaluations at that site:

```
[gate] arg0 $x0 base=fi5 objTainted=0
```

**The naming half WORKS. The taint half fails.** `&st` resolves to frame object
5; frame object 5 is never marked secret. No amount of provenance work fixes
this - the object genuinely carries no taint in the analysis's view.

#### Why fi5 is never tainted, traced to the bottom

`hydro_hash_init` is **fully inlined** here (it is analysed as its own function
for other callers, but `hydro_sign_final_create` makes no call to it), and it
does not write the key into `state` directly:

```c
uint8_t block[80];                                  /* a local -> fi4 */
memcpy(block + gimli_RATE + 1, key, 32);            /* secret -> block */
hydro_hash_update(state, block, p);                 /* inlined too */
    for (i = 0; i < ps; i++)
        buf[state->buf_off + i] ^= in[i];           /* block -> state */
```

Following it through the log:

| step | evidence | verdict |
|---|---|---|
| key reaches `block` | `taint stack cell FI=4 off=17 sz=16`, `off=33 sz=16` | works - the constant-size memcpy is expanded to stores, no model needed |
| `in[i]` loaded from `block` | **0** loads attributed to FI=4; 834 land on unknown/heap | **fails here** |
| XOR result stored into `state` | `stack cell FI=5 off=0/16/32/48 (store untainted)` | consequence: the source was not secret |
| gate asks if fi5 holds a secret | `objTainted=0` | consequence |

**The break is a variable-index LOAD from a frame object.** `in[i]` with a loop
counter lowers to a register-offset load whose MMO does not resolve to the
alloca, so it is classified unknown/heap rather than as a cell of fi4. The store
side of the same object resolves fine (the memcpy stores are constant-offset);
it is the read side, at a variable index, that loses the object.

So the fifth link is the mirror image of the third: **provenance work covered
pointer arithmetic for CALL ARGUMENTS, and this is the same problem for LOADS.**
`getCellFromMMO` is where it would be fixed, not in the call-site rules.

#### The honest count

Five links, on one chain, in one function:

| # | link | status |
|---|---|---|
| 1 | secret reaches `block` via constant-offset stores | works |
| 2 | **variable-index load from `block`** | **open - the current blocker** |
| 3 | `&st` passed to `hash_final` (gap A) | flag exists |
| 4 | `hash_final` records writing through arg 1 (libc model) | flag exists, verified |
| 5 | caller passes `eph_sk` onward (B1+B2) | flags exist, verified |

Links 3, 4 and 5 are built and each verified on its own repro. **Link 2 is
upstream of all of them**, which is why none of them has moved this workload.

### The actual fix: `getUnderlyingObject` stops at a PHI

Having established that the gate's TAINT half failed because frame object 5 was
never marked, the chain bottomed out at a **variable-index load**. The cause is
one line of standard LLVM behaviour:

> `getUnderlyingObject` does not look through PHI nodes.

A pointer carried around a loop IS a phi. So `in[i]` inside
`for (i...) buf[...] ^= in[i]` resolves to the phi, not to the local the loop
walks, and the access is classified unknown/heap. Measured in
`hydro_sign_final_create`: **753 of 777 unresolved accesses had a phi as their
underlying object.**

`getUnderlyingObjects` (plural) looks through phis and selects. The fix accepts
its answer **only when every path agrees on one object** - disagreement means the
access could be to either, and Unknown is then correct. Offset precision is
unaffected: the constant-offset check fails for a phi-based pointer, so this
lands on whole-object granularity, which is what an unknown index deserves.

#### Measured on the gem5 shadow-taint oracle

The instrument that had refused to move for every previous change in this file:

| arm | under-taint ops | protected | unprotected | before |
|---|---|---|---|---|
| null (control) | 456,194 | 0 | 100% | unchanged |
| **create** (natural seed) | **368,821** | **87,373** | **80.85%** | *97.61%* |
| **repair** (`+ hydro_hash_final,1,pointee`) | **126** | 456,068 | **0.03%** | *97.61%* |

Two results, and the second is the bigger one:

- The natural seed goes from 97.61% to **80.85%** unprotected, with protected
  operations up **8x** (10,918 -> 87,373).
- **The compiler's own suggested repair now works.** `hydro_hash_final,1,pointee`
  is the line the information-loss report prints; before this fix it changed
  nothing on the Linux target (445,276 either way) while working on darwin, which
  §3d recorded as "a repair that is target-dependent is not a repair". It now
  reaches **0.03%**, matching the keygen-buffer seed. The annotate-recompile loop
  closes on the target the oracle measures.

#### It is unflagged, and the oracle is why that is defensible

This changes the default path. The precision report showed both gains and losses
- `hydro_sign_final_create` need 27 -> 245, but `hydro_sign_final_verify` (119)
and `hydro_sign_verify` (11) disappeared entirely - and a disappearance is
exactly the shape an under-taint takes. The report cannot tell the two apart.

The oracle can, and it says the under-taint count went **down** by 76,455 ops.
**A reclassification that introduced a leak would have moved that number up.**
(The functions that lost coverage take the signature and the PUBLIC key, so
having no secret under an `sk`-only seed is the correct answer, not a loss.)

This is also the fourth time in this file that the precision report and the
oracle disagreed about whether something helped. **Report first, oracle second,
and believe the oracle.**

#### And it is free

libsodium, CIO-parity seed, full cross build, toolchain pinned: **134 switches
before the fix, 134 after.** libhydrogen's baseline moves 27 -> 28. So the
default path gains 8x the protected operations on the signing workload and costs
nothing measurable in switches on the cost workload - which is the shape of a
missing-information bug being fixed, not of a precision knob being traded.

### Re-running the flags against the fixed baseline: they help, and they are still wrong

Every flag in this file was measured against a baseline that was losing taint at
a phi. Re-measured on the oracle now that it is not:

| arm | switches | under-taint ops | unprotected |
|---|---|---|---|
| `create` - natural seed, no flags | 25 | 368,821 | 80.85% |
| `createflags` - natural seed + A + B1 + B2 + libc | 188 | **31,746** | **6.96%** |
| `repair` - natural seed **+ one seed line**, no flags | 279 | **126** | **0.03%** |
| `repairflags` - that seed line **and** all four flags | 188 | 31,746 | 6.96% |

**The flags went from worthless to a 11.6x reduction** in under-taint on the
natural seed - 80.85% to 6.96%. So they were never useless; they were downstream
of a blocker, and against a broken baseline a real fix is indistinguishable from
a no-op. That is worth remembering before concluding any of them was a bad idea.

**And the one-line seed still beats all four of them, by 230x.** 0.03% against
6.96%.

**Worse, the flags make the seed worse.** Row 4 is the seed line *and* the flags:
it lands on 6.96%, not 0.03%. Adding precision to a build that was already
correct **cost 250x in leaked operations** while removing 91 switches
(279 -> 188).

The mechanism is visible in that trade: more precise taint means narrower
placement, and narrower placement gives up the incidental coverage the imprecise
version was providing. A function that was `AlwaysEnteredWithDIT` under a
blunter analysis stops being so, narrows, and the operations that were protected
by inheritance are exposed. The residual is exactly where you would expect -
`hydro_x25519_sub` 11,776 ops, `_propagate` 7,168, `_adc` 5,632 - limb
arithmetic that the coarse version blanketed and the precise version does not.

**This is a sharper form of experiment 07's finding.** There, more faithful
analysis made the pass *slower*. Here it makes it *less protective in absolute
terms*, because precision and blanket coverage pull in opposite directions and
the imprecise arm was winning on coverage by accident.

**Conclusion: all four flags stay OFF, and the reason is now measured rather than
precautionary.** The shipped answer for this workload is the seed line the
information-loss report already prints. That is also the cheapest answer, needs
no new analysis, and is the one the compiler can suggest on its own.

### The residual 126: not a leak, and not fixable by taint analysis

The best arm leaks 126 operations. Attributed:

| ops | site |
|---|---|
| 112 | `hydro_hash_update`, `buf[state->buf_off + i] ^= in[i]`, inlined into `hydro_sign_challenge` at `sign.h:30` |
| 12 | `hydro_hash_final`, `memcpy(out + i * gimli_RATE, buf, gimli_RATE)` |
| 2 | `main`, the harness reading `sig[0]` |

**A hypothesis that was tested and REFUTED.** libhydrogen's 64-byte `sk` is
`[32-byte secret scalar][32-byte PUBLIC key]` - `hydro_sign_prehash` does
`pk = &sk[32]` and `hydro_sign_challenge` absorbs that `pk` into a hash - so
seeding all 64 bytes marks the public key secret. That looked like the whole
residual. Reseeding only the secret half changed the under-taint count **not at
all**: 126 either way, with 32 fewer *protected* ops (the pk bytes no longer
tracked). Recorded because it is a plausible story that happens to be wrong.

**What it actually is: declassification.** `sign.h:30` is
`hydro_hash_update(&st, nonce, ...)`, and two lines earlier
`hydro_x25519_scalarmult_base_uniform(nonce, eph_sk)` computes that nonce from
the ephemeral secret. So `nonce` IS secret-derived - and it is `csig[0..32)`,
**published as half the signature**. The challenge written at `hash.h:110` is the
same: `c = H(R, A, M)`, a public value. The two `main` ops are the harness
reading `sig[0]`.

All 126 are values the scheme **publishes on purpose**. The scalar multiply that
*derives* the nonce from the secret is protected (that is the part that must be
constant-time); absorbing the already-public result into a hash afterwards leaks
nothing, because it is about to be transmitted.

**So the taint analysis is not wrong here, and no amount of provenance work will
change the number.** The oracle counts them because it tracks derivation and has
no notion of a value becoming public. The missing feature is a declassification
tag - CryptoMPK's `sinktaint`, which `related-work.md` §3a already records as
"the one thing to take from them outright" and which we do not have.

**Revised soundness statement for this benchmark.** With the seed line the
information-loss report prints, **every secret-carrying operation that is not a
published output runs with PSTATE.DIT set**, on this path, under this driver, in
gem5. That is as far as this instrument can go, and the residual is a missing
annotation kind rather than a missing analysis.

### Prior art for §3c (partial)

Searched 2026-09-02. **Andromeda, TaintDroid and all GCC quotes below were
verified by reading the source/paper directly**; anything still second-hand is
marked as such. The classic-literature and post-RA/binary questions were still
outstanding when this was written.

### GCC's `ipa-modref` ships this design, and it composes transitively

**This is the closest shipped analogue and it settles the main design question.**
Verified against `gcc/ipa-modref-tree.h` on master. The summary key is the same
triple §3c proposes:

```c
struct modref_parm_map {
  int parm_index;            /* index of parameter we translate to */
  bool parm_offset_known;
  poly_int64 parm_offset;
};
enum modref_special_parms {
  MODREF_UNKNOWN_PARM = -1,  MODREF_STATIC_CHAIN_PARM = -2,
  MODREF_RETSLOT_PARM = -3,  MODREF_GLOBAL_MEMORY_PARM = -4,
  MODREF_LOCAL_MEMORY_PARM = -5
};
```

And the call-site step is exactly item 5 of §3c - reparent the callee's access
onto the caller's own parameter:

```c
if (m.parm_index == MODREF_LOCAL_MEMORY_PARM)
  continue;                                    /* dropped, not escalated */
a.parm_offset += m.parm_offset;                /* compose offsets */
a.parm_offset_known &= m.parm_offset_known;    /* any break -> unknown */
a.parm_index = m.parm_index;                   /* reparent onto caller's parm */
```

**Transitivity is structural, not clever.** Each call edge resolves ONE hop
(reportedly via IPA jump functions: the actual is the caller's own formal,
optionally plus a compile-time-constant offset). Multi-hop reach comes from
running the merge in call-graph postorder so every callee is final before its
callers. **That is a load-bearing confirmation for §3c: we need no per-edge
cleverness, only a bottom-up fixed point - which the pass already has.**

**One refinement worth taking.** GCC keeps `parm_index` while clearing
`parm_offset_known` when the displacement is not statically constant: *"I know
the object but not where in it"* is a distinct, strictly better state than *"I do
not know the object"*. Our depth-0 whole-object proposal is precisely GCC's
degraded state made permanent - which is the honest way to describe it, and it
means the design is a known-good fallback rather than an approximation nobody
has tried.

**What we already have.** GCC's two-way split - callee-private memory *dropped*
(`MODREF_LOCAL_MEMORY_PARM`) versus untraceable memory escalated to TOP
(`MODREF_UNKNOWN_PARM`) - is already implemented here:
`computeFunctionMemEffects` ignores non-fixed frame objects as caller-invisible
and sends unknown/heap to `WritesSecretToUnknown`. No change needed.

**What we do not have, and the code already says so.** A *fixed* frame object is
an incoming stack argument slot, so it HAS an argument index, and we map it to
TOP anyway (`TaintAnalysis.cpp:1739`, comment: "mapping it to the specific
argument is a further provenance refinement"). Under §3c that becomes reachable:
it is an `Arg(k)` whose k is recoverable from the frame layout. **Worth folding
into B1** - it is the same fix, applied to arguments that arrived on the stack
rather than in a register.

### `Arg(k)` has a name, and a 1990s lineage

Second-hand: the search reports reading these primary texts, but my own attempts
to fetch Wilson and Lam hit a 403 and then a bad certificate, so **the quotes
below are unverified by me.** Worth checking before citing.

There is no single field-wide term, but there is a **self-reported lineage** for
exactly this device - a symbolic placeholder for storage a callee cannot name,
instantiated against the caller's own objects at each call site:

| term | paper |
|---|---|
| *non-visible variables* | Landi and Ryder, PLDI 1992 |
| *invisible variables* | Emami, Ghiya and Hendren, PLDI 1994 (citing Landi and Ryder) |
| **extended parameters** | Wilson and Lam, PLDI 1995 (claiming equivalence to the above) |

`Arg(k)` is an extended parameter at depth 0. Three details are directly useful:

- **Wilson and Lam create them LAZILY**, only as the callee actually references
  them, explicitly to avoid Emami et al.'s eager creation for every input pointer.
  Ours are created eagerly at entry for every pointer argument, which is cheaper
  to implement and costs a map entry per argument; worth revisiting if the map
  ever shows up in a profile.
- **Wilson and Lam are deliberately UNTYPED** - raw base/offset/stride triples,
  chosen so "problems related to type casts and unions become irrelevant". That is
  the closest of the three to a machine-level representation, and it is the one to
  read first.
- **Transitive composition is where they differ**, which is the axis §3c cares
  about: Wilson and Lam unbounded (recursive walk up the call graph), Landi and
  Ryder k-limited by construction, Emami et al. unbounded with no stated limit,
  and Choi et al. reported by both others as breaking down past one call-chain
  link. Ours is one hop per edge composed by the bottom-up fixed point, which is
  GCC's arrangement.

**The honest framing for a write-up:** the device is thirty years old and we
should use its name; what is not obviously precedented is computing it after
register allocation. See `related-work.md` §7, where that claim is recorded
scoped and hedged.

### On the soundness direction: it does NOT flip, and we should not claim it does

Searched from three angles. **No source says the conservative direction is
opposite for compiler alias analysis and for security taint analysis.** Every
source retrieved calls the *same* direction safe in both: over-approximate the
may-point-to / may-taint set. Steensgaard's "safe (conservative)" is a superset;
Andersen's "safe approximation" covers all runtime addresses; LLVM's `MayAlias`
default is the same move. **Our refusal to follow pointer arithmetic is textbook
may-analysis conservatism, not an inversion of it**, and writing it up as an
inversion would be our own synthesis rather than a citation.

**Two things do support the choice, and neither is the one to overclaim.**

- **The named critique in the literature targets the OPPOSITE mechanism.**
  Slowinska and Bos, *Pointless Tainting?* (EuroSys 2009), is a sustained,
  measured critique of aggressively propagating taint *through* arithmetic and
  then dereferences - "taint explosion... hard to avoid". No critique of refusing
  to follow arithmetic was found.
- **Cerberus/PNVI reportedly records GCC declining to re-attribute a pointer to a
  different object even when arithmetic makes the bit patterns identical**, which
  is the same instinct reached for a different reason (alias-analysis freedom).

**And one warning worth heeding.** TaintDroid, DFSan and Panorama all *follow*
arithmetic and propagate taint through indexing - mechanically the opposite of
our rule. They are solving value-taint-through-indexing ("does the loaded byte
carry the secret"), where we are solving object-provenance-naming ("which object
does this computed address denote"). In our problem, refusing to name and falling
back to the clobber IS the over-approximating direction. **Do not cite them as
endorsing our mechanism** - a careful reader will notice they do the opposite
concrete thing. The defensible claim is narrower: no prior work found poses this
sub-question directly, and the nearest analogues reach the same conservative
instinct by different mechanisms.

### Cheng and Hwu (PLDI 2000) shipped their results at k = 1

The paper Andromeda defers to. Reportedly substitutes the callee's formal root
with the actual-argument *expression* and re-evaluates in the caller, bottom-up
over the SCC-DAG - the same postorder composition as GCC, twenty years earlier.
Two things matter for us: the substitution is compositional, so a base that is
itself another formal needs **no special case**; and their reported SPEC results
were obtained at **k = 1**, the least expressive non-trivial setting.
**Depth-0/shallow is where practical implementations of this idea land, not a
concession forced by our setting.**

### LLVM's Attributor is not a usable template

`AAPointerInfo` reportedly tracks per-`Argument` accesses with offsets and does
translate a callee's argument summary back to a call site - but as a monotone
fixed point over the live module-wide **SSA def-use graph**, with no portable
summary object to lift out. That precision is inseparable from named
`Value`/`Argument` identity, which post-RA does not have. **GCC's flat POD is the
shape to imitate; LLVM's is not**, and the coarse IR-level alternative
(`writeonly`, `memory(argmem: write)`) carries no offset or index at all.



**The concept has a standard name, and the design is not novel: it is an
`access path` in a `storeless` heap model.** Andromeda (Tripp, Pistoia, Cousot,
Cousot, Guarnieri, FASE 2013) roots every taint fact at a *variable or formal
parameter* rather than at a named heap object, so a call site maps callee facts
to caller facts by substituting actual for formal. **The object is never named -
only a path to it.** That is exactly what `Arg(k)` is.

**It degrades to machine level precisely at depth 0, which is what we need.**
An access path is `x.f1...fn`. Post-RA there are no field names, so only `n = 0`
survives - a path rooted at a formal with no field selectors, i.e. whole-object
granularity keyed by argument index. That is the granularity §3c proposes, and
it is the most this representation can offer here, not a shortcut.

**The soundness direction matches ours.** Andromeda bounds path length at a
constant `c` and appends `*`:

> "a widened access path potentially points to more than one object. In this way,
> the analysis can track a bounded number of access paths **in a sound manner** by
> restricting the length of an access path to some constant c"

Over-approximating at the truncation point, like our refusal to follow pointer
arithmetic. (Andromeda does **not** state a default for `c`.)

**Andromeda does not specify its interprocedural transfer functions** - "Extending
the core language to contain procedure calls is straightforward [5]", where [5] is
Cheng and Hwu, *Modular interprocedural pointer analysis using access paths*,
PLDI 2000. **That is the paper to read for the actual mechanism**, and it has not
been read yet.

**TAJ rejected this design, and the reason does not apply to us** (second-hand).
TAJ (PLDI 2009, same lead author) reportedly discarded threading heap effects
through extra parameters and return values as a scalability bottleneck, using a
whole-program Andersen points-to solution with direct store-site-to-load-site
edges instead. That option is not available post-RA: no allocation sites, no
whole-program points-to. So the alternative to §3c is not "do what TAJ did", it
is "have no answer".

**TaintDroid's JNI boundary is this exact defect** - the closest match found,
and **verified against the OSDI 2010 paper directly**. Native code is unmonitored;
on return, TaintDroid consults a hand-written *method profile*, "a list of
(from, to) pairs indicating flows between variables, which may be method
parameters, class variables, or return values". Where no profile exists, the
default heuristic

> "assigns the union of the method argument taint tags to the taint tag of the
> **return value**. While the heuristic has **false negatives for methods using
> objects**, it covers many existing methods."

A native callee that fills a caller-supplied buffer and returns `void` therefore
gets nothing. **That is gap B in different vocabulary, and their answer is our
seed file** - profiles are "defined as needed" - which is why §3d's conclusion is
a property of this problem shape rather than an idiosyncrasy of this project.

Their coverage figure is worth recording next to ours. Of 2,844 JNI methods in
Android 2.1, **913 are covered by the automatic heuristic**; the rest "may or may
not have information flows that produce false negatives". That is 32% resolved
automatically against our 34% of call sites resolved precisely (644 of 1,897).
The denominators are not the same thing - theirs is methods-not-touching-objects,
ours is call-sites-with-resolvable-provenance - so this is a coincidence of
magnitude and not a shared measurement. It is still the same story: **roughly two
thirds of the boundary is unresolved, and the residue is handed to a human.**

**oo7 has no answer to borrow** (second-hand). It performs no interprocedural
static dataflow at all: taint rides a BAP/Primus microexecution that steps *into*
callees, so no summary and no map-back step ever exists. Worth recording because
`related-work.md` cites oo7 - it is not a precedent for anything in this file.

## 3d. Can more seeds substitute for the fix? Partly, and not safely

Worth answering because it is the cheap option and it is what the tooling
already asks the user to do.

**First, gap B is narrower than §3b suggests.** The blunt clobber DOES cover the
case where the caller loads the buffer itself; it fails only when the caller
passes the pointer ON (`playground/frame_addr_gap/shapes.c`):

| shape | what the caller does with the filled buffer | result |
|---|---|---|
| `inline_consume` | loads it and multiplies | need=11, **coverage 94.1%** - covered |
| `pass_on` | hands the pointer to another function | `consume` **absent** - uncovered |

`ExternalMemClobbered` poisons subsequent *loads*, and that is enough for the
first. Passing a pointer does not consult it, which is the whole of gap B.

**A seed does patch the second** (`seedfix.c`). Adding `consume,0,pointee`:

| seeds | `consume` | total switches |
|---|---|---|
| `pass_on,1,pointee` | absent | 5 |
| `+ consume,0,pointee` | need=8, coverage 90.0% | 8 |

**But four things make it a workaround rather than a fix.**

1. **A seed is a per-FUNCTION attribute, not a per-call-site one.** Seeding
   `consume` instruments it for *every* caller. `seedfix.c` includes
   `public_path`, which calls the same helper with nothing secret anywhere near
   it, and it now pays `consume`'s two switches on every call. The precision
   cost scales with how shared the helper is, and crypto helpers are shared.
2. **You cannot find the site.** This is the finding from experiment 08: on
   libhydrogen the information-loss report emitted **zero** records while 97.61%
   of secret operations ran unprotected. There is nothing to tell the developer
   which function to seed.
3. **The seed that works on libhydrogen works BY ACCIDENT.**
   `hydro_sign_keygen,0,pointee` reaches 0.03% under-taint, but not by expressing
   the `sk -> hash -> eph_sk` dataflow that is broken. It taints the keypair
   buffer, that reaches the RNG state global, and the module-wide secret-global
   rule then makes every RNG consumer secret - including `eph_sk`, which is
   *produced* by `hydro_random_buf`. The tell is in experiment 08's own numbers:
   it drags in 17 `hydro_kx_*` and 8 `_hydro_pwhash_*` functions the driver never
   calls. It depends on the RNG state being a global and on keygen being in the
   same TU; neither is a property of the secret.
4. **The compiler's own suggested repair did not transfer.**
   `hydro_hash_final,1,pointee` - the line the info-loss machinery would print -
   took darwin from 12.0% to 99.2% coverage and did **nothing** on the Linux
   build the oracle measures (445,276 either way). A repair that is
   target-dependent is not a repair.

**So: seeds can cover a specific hole once someone knows it is there, at a cost
that lands on every caller of the seeded function.** They cannot be the answer to
a defect that is invisible, and on the one workload measured end to end the seed
that appears to work is exploiting an unrelated over-approximation.

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

## 5. Cost, and why BOTH flags are DEFAULT OFF

This rule has been tried and rejected once
(`docs/design/p1b-frame-provenance.md` §4): gate + fallback went 408 -> 628
switches on libsecp256k1, and `ecdsa_verify` - a path that handles only public
data - went from 0 switches back to 12.

libsodium, CIO-parity seed (77 lines), full cross build, **toolchain pinned**:

| arm | `msr DIT` | vs base |
|---|---|---|
| `-ftaint-harden` | 134 | - |
| `+ -taint-frame-addr-args` (gap A) | 152 | **+13.4%** |
| `+ -taint-arg-provenance` (B1) | **134** | **0.0%** |

**B1 is free on libsodium**, which is the expected shape for a substitutive
change and matches libhydrogen, where it moved 1,253 blunt fallbacks to 1,010
with the switch count unchanged at 318. Gap A's +13.4% is real and is the cost of
an *additive* rule.

> **A methodology note that nearly cost this number.** An earlier run of the same
> comparison was invalid and was retracted: `taint-cross-cc` reads `LLVM_BUILD` at
> run time, and `LLVM_BUILD=x ./configure` sets it for *configure* only, so during
> `make` it fell back to `~/Documents/llvm-project/build` - an August build
> carrying an **older, since-removed implementation of `-taint-frame-addr-args`**.
> The tell was a re-run failing with `Unknown command line argument
> '-taint-arg-provenance'` from a compiler that had just been built with it. **A
> flag that exists in your tree and is rejected by "your" compiler means the
> toolchain is not pinned** ([[dit-measurement-hygiene]], reached by a new route).
> Export `LLVM_BUILD`, or give `CC` an absolute path.
>
> Re-run pinned, gap A comes back at **152 again**, so the value survived - but it
> survived by luck, and the invalid run could not have told us that. Record the
> retraction, not just the number.

libsecp256k1 has **not** been re-measured, and that is where gap A's false
positives were (`ecdsa_verify` 0 -> 12). **Gap A stays off until it is.** B1 stays
off because it closes no leak on its own; its case is made by B2, not by itself.

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
3. ~~**Close gap B**~~ **DONE, and insufficient.** B1 and B2 both landed and both
   close their repros; libhydrogen does not move because of a third gap.
   The `TargetInstrInfo` hook landed (blunt 1,253 -> 494) and so did the libc
   model, which does produce the missing `arg1` on `hydro_hash_final`. Neither
   closed libhydrogen. DONE. The gate's NAMING half worked and its TAINT half failed,
   because `getUnderlyingObject` stops at a PHI and every loop-carried pointer
   read was classified unknown/heap. Fixed with `getUnderlyingObjects` plus a
   unanimity check; oracle 97.61% -> 80.85%, and the compiler's own repair line
   now reaches 0.03%.
4. **Re-measure `-taint-frame-addr-args` on libsecp256k1** and decide its
   default. Only worth doing after B, since A alone moves nothing.
5. Re-run the experiment 08 oracle after each step. It is the only instrument
   that separated a real fix from a plausible one here, twice.
6. **Finish the prior-art search** (see below): `ipa-modref`'s `parm_map` is the
   closest *shipped* analogue to §3c and is still unread, as is Cheng and Hwu
   PLDI 2000, which is where Andromeda's actual interprocedural mechanism lives.

## 8. Two traps this investigation hit

- **A precision report is not a dynamic result.** `-taint-frame-addr-args`
  improves the darwin report (two curve functions at ~100% coverage) and changes
  the oracle by exactly zero. Always close the loop on the oracle.
- **A conclusion drawn on one target may not hold on another.** The gate is the
  proximate cause on darwin and is irrelevant on Linux, from identical source.
  Pin the target before writing a mechanism down.
