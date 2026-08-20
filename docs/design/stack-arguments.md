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
