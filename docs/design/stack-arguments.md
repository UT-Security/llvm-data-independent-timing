# Stack-passed secrets: a leak in the shipped default, and the fix

**Found and fixed 2026-08-19**, while checking whether the mod-set call-site gate
could be made sound. It is not a gate problem — it is a pre-existing leak that the
gate would have widened.

## 1. The leak

AAPCS64 passes arguments past the eighth, and large aggregates, in the **outgoing
argument area**: the caller stores them to `$sp + imm` and then loads the argument
registers, typically **overwriting the register that held the secret**. By the time
the call is reached, no register carries it.

`taintedCallArguments` inspected only argument registers — it filtered on
`TRI->getEncodingValue(reg) > 7`. So it reported the call as passing nothing
secret, and:

- the callee's summary never learned it receives a secret, so **the callee was
  analysed as clean and emitted no DIT at all**;
- `-taint-callsite-report` did not record the escape to an external callee;
- the callee's return value was not tainted.

Fifteen lines of C reproduce it. Identical bodies, secret in argument 8 (stack)
versus argument 0 (register):

| | `msr DIT` in the callee |
|---|---|
| secret in a **stack** argument | **0** |
| secret in a **register** argument | 2 |

This is under-taint, i.e. a **leak**, not imprecision. It is independent of
`-taint-modset-callsite-gated` and predates it.

**Why the pass did not simply crash into it earlier.** The outgoing-arg store hit
the Unknown branch and set `ExternalMemClobbered`, so the *caller's* later memory
reads stayed conservative. That accidental cover made the gap invisible from the
caller's side while leaving the callee entirely uncovered.

## 2. The fix, in three pieces

The transfer is caller → summary → callee, and all three had to change.

**(a) Recognise the store.** The outgoing-arg store carries a `PseudoSourceValue`
of kind `Stack` (`:: (store (s64) into stack)`). That is **not** a
`FixedStackPseudoSourceValue` — that kind covers *incoming* arguments and spill
slots — and it has no IR `Value`, which is why it previously fell through to
Unknown. `getCellFromMMO` now returns a new `CellInfo::OutgoingArg` kind, and a
tainted store to it sets `TaintState::OutgoingArgSecret`.

The bit is **one-directional**: an untainted store to the area does not clear it,
because the area holds several arguments at different offsets and clearing on any
one would lose a secret written at another. It is cleared where it is *consumed* —
at the call — so it cannot leak into the next call in the block.

**(b) Report it at the call.** `taintedCallArguments` returns `Data = true` when
the bit is set. Reported as Data rather than Pointee because what was stored is the
argument value itself; a pointer-to-secret stored there also lands here, which
over-approximates Data for that case — the safe direction, and every consumer of
this predicate is asking "does this call pass a secret at all".

**(c) Seed the callee.** New summary flag `FunctionTaintSummary::StackArgTainted`.
When set, the callee seeds **every incoming fixed frame object** as tainted.

A flag rather than an argument index, deliberately: recovering *which* index would
mean re-running ABI argument assignment at the MIR level, which is target-specific
and does not belong in target-independent CodeGen. The over-approximation is
confined to the callee's own frame, fires only for functions a caller actually
handed a stack secret, and never widens what its callers see.

## 3. A landmine this exposed, for whoever adds the next summary field

Adding `StackArgTainted` made the fixed point **fail to converge**, hitting the
100-iteration guard.

`NewSummary` in `TaintFixedPointIteration.cpp` is default-constructed each visit
and copies forward only a named subset of fields. `StackArgTainted` is set by
`propagateArgTaintToCallees` — i.e. by the *caller*, on the callee — so each visit
of the callee wiped it, the next visit of the caller re-set it, and the summary
reported "changed" forever.

**Rule: every monotone summary field set anywhere other than in the function's own
visit must be explicitly carried forward when `NewSummary` is built.** The carry
now reads `OldSummary` (freshest, includes anything a caller stored since
`CurrentSummary` was taken) OR'd with `CurrentSummary`.

## 4. What it costs

Bitcoin Core's libsecp256k1, same seeds and flags as every other measurement:

| build | before | after |
|---|---|---|
| region + loop-hoist | 660 | **661** |
| + `-taint-modset-callsite-gated` | 178 | **179** |
| functions carrying ≥1 switch | 73 / 18 | **73 / 18** (unchanged) |

**One switch, in `nonce_function_bip340_impl`, in both configurations.** No new
function is instrumented; an already-covered function got slightly wider coverage.
So the leak is real in production crypto code, not only in the synthetic repro, and
closing it is close to free here.

Because the change only ever *adds* coverage, every prior coverage result stands
(gem5 `ditSuppressed` at 103.1% of the hand oracle can only rise), and a one-switch
delta on 178 is far below the noise floor of the Bitcoin Core benchmarks, so those
were not re-run.

## 5. What is still not covered

- **Which** stack argument holds the secret. All incoming fixed objects are seeded.
- **Stack-passed pointees.** A pointer stored to the outgoing area is treated as
  Data at the call site, which is sound for the "is this call secret-passing" test
  but does not give the callee pointee-taint on that specific parameter.
- **Byval aggregates** copied into the outgoing area by a `memcpy` call rather than
  by direct stores: the copy is an external call, handled by the existing
  conservative external-call path, not by this bit.

Test: `llvm/test/CodeGen/AArch64/taint-analysis-stack-args.mir`.

---

## 6. A second entry point into the same class: a SEEDED parameter at index >= 8

**Found and fixed 2026-08-26.**

**Found 2026-08-26**, while tracing why `AlwaysEnteredWithDIT` never fires on
libsodium. Section 2's fix is caller -> summary -> callee: it recognises the
outgoing-arg store, reports it at the call, and seeds the callee's frame objects.
That covers a secret **passed** on the stack. It does not cover a secret
**declared** on the stack, because there is no caller and no store - the taint
originates from the taint-source file.

An annotation on an argument at index >= 8 was applied to the IR and then
dropped. Identical bodies, same load-multiply, only the argument position
differs:

| seeded argument | `msr DIT`, before | after |
|---|---|---|
| index 7, `tainted-pointee` (x7) | 2 | 2 |
| index 7, `tainted` (x7) | 2 | 2 |
| **index 8, `tainted-pointee`** (stack) | **0** | **2** |
| **index 8, `tainted`** (stack) | **0** | **2** |

The boundary is exactly the AAPCS64 register/stack split, and both taint kinds
were affected. Regression: `taint-analysis-stack-seeded-arg.mir`.

### The fix

`seedTaintFromArguments` walks `MRI.liveins()` and maps each incoming argument
register to an argument index, so an index that no argument register carries was
never seeded at all. It now detects exactly that -- a declared-secret index not
covered by any livein argument register did not arrive in a register -- and takes
the SAME path section 2(c) built for the caller-side case: seed every incoming
fixed frame object.

No ABI assignment is re-run, which is the constraint section 2(c) set. An unused
argument also has no livein, so this over-approximates onto the frame in that
case: safe direction, confined to a function that already carries a declared
secret, and it costs precision rather than coverage.

### What it costs on the shipped libsodium seed

**Four of the 65 CIO-parity seed lines sit at index 8, and all four are AEAD
KEYS:**

```
crypto_aead_aes256gcm_encrypt,8,pointee
crypto_aead_aes256gcm_decrypt,8,pointee
crypto_aead_chacha20poly1305_ietf_decrypt,8,pointee
crypto_aead_chacha20poly1305_ietf_encrypt,8,pointee
```

Annotating the whole library with **only** those four lines applies 4 pointee
attributes across 4 functions and produced **0 `msr DIT` across 0 functions**: a
key-only annotation of libsodium's AEAD compiled to a completely unprotected
build. It now produces **54 `msr DIT` across 9 functions**.

On the full CIO-parity seed the fix costs **398 -> 414 switches across 81 -> 83
functions**, about +4%, all of it coverage the analysis should always have had.

**This corrected a claim in the generated seed header** (`utils/taint_libsodium_eval.sh`),
which reads:

> *"The four `crypto_aead_*,8` lines are LIVE here - arg 8 is a real pointer
> param. In CIO arg_index counts SysV GPR arg regs, capped at 5, so those lines
> are DEAD there and never seed the AEAD key. We are strictly more complete."*

They were dead here too, for a different reason -- stack passing rather than
CIO's GPR cap. With the fix they are live, and the header's claim now holds.

**It was masked in the full seed, which is why nothing caught it.**
`crypto_aead_chacha20poly1305_ietf_encrypt` is also seeded on args 2 (the
plaintext, pointee) and 3 (its length), both register-passed, so the function is
instrumented via the MESSAGE and looks fine. The key's taint is dropped silently.
A key-only seed - the more conventional annotation, and the one a user would
write first - had nothing to fall back on.

### Why this blocks the ownership work

The retraction that decides `AlwaysEnteredWithDIT` asks whether a call site passes
a secret. Inside `crypto_aead_chacha20poly1305_ietf_encrypt_detached` the key is
argument 9 - also stack-passed - so the call to `crypto_stream_chacha20_ietf`
registers as passing nothing, and the chain unravels from there:

```
retract crypto_stream_chacha20_ietf (call in ..._encrypt_detached, transparent=0, instrumented=1)
retract stream_ietf_ext_ref         (call in crypto_stream_chacha20_ietf, transparent=0)
```

So the ~56 executed `MSR DIT` per AEAD call (against a hand-placed oracle's 2)
and this gap are the SAME root cause. A `-taint-dit-forwarder-ownership` fix was
prototyped on 2026-08-26 and **reverted**: it measured 35 `msr DIT` with the flag
on and 35 with it off on the internalized libsodium bitcode, because the taint
never arrives for it to act on. Do not re-attempt it until this gap is closed --
a transparent forwarder that starts carrying taint stops being transparent, so
the case it handles may not survive the fix.
`taint-analysis-dit-forwarder-ownership.mir` records the behaviour.

### Severity

Under-taint of a declared source, so the same class as section 1 - but it was
**never demonstrated as a live leak** in the shipped libsodium build, because the
message annotation covers the same code. What was demonstrated is that the
annotation was silently discarded, which is enough to make a key-only deployment
unprotected while reporting nothing.

### What it did NOT fix

The ~56 switches per AEAD call are still there: `encrypt_detached` still emits 14
and the internalized build still measures 35. `stream_ietf_ext_ref` gained the
ownership bit, `crypto_onetimeauth_poly1305_donna_init` did not. The stack gap was
*a* root of the ownership problem, not *the* root. Whatever remains is unmeasured.
