# The source condition: making the mod-set gate sound

> **STATUS 2026-08-24: unconditional.** The source condition and
> `-taint-return-callsite-gated` are no longer flags - both are always on whenever the
> gate is, and the gate itself is now the default. The permissive rule this document
> A/Bs against is not reachable from the command line. `-taint-no-modset-gate` disables
> the whole mechanism.

**Built and measured 2026-08-19.** `-taint-modset-callsite-gated` suppresses a
callee's memory clobber at call sites that pass no secret. That is valid only if
the callee's secret could have come from a caller at all. This is the analysis
that establishes it, plus the two amplifiers that made a correct rule look
ruinously expensive until they were found.

## 1. The rule

`FunctionMemEffects::NonArgSourced` - set when taint enters a function from
anywhere other than its own parameters:

- a load that draws taint from a **global**,
- a load that draws taint from **another TU's** unknown memory,
- a **call** that hands back taint this function did not supply: either it passed
  no secret, or the callee is itself non-argument-sourced (transitive), or this
  function already holds non-argument-sourced taint and so cannot vouch for what
  it passed.

`-taint-modset-gate-strict` (on by default with the gate) refuses to gate a callee
whose mod-set is non-argument-sourced. Test:
`llvm/test/CodeGen/AArch64/taint-analysis-modset-source-condition.mir` builds the
case the earlier "does the callee name a tainted parameter?" proxy got wrong - a
callee that takes a secret parameter **and** reads a secret global - and checks all
three behaviours: no gate covers it, the loose gate silently drops it, the source
condition keeps it.

The register summary needed the same treatment: `ReturnsTainted` was applied at
every call site unconditionally, so a shared helper poisoned by one secret-passing
caller handed phantom taint to every other caller.
`-taint-return-callsite-gated` applies the same rule there, and cut the
non-argument-sourced markings on libsecp256k1 from **258 to 40**.

## 2. It looked like soundness cost the whole win

First measurement, gem5, verification workload, no secret anywhere, serializing:

| arm | cycles vs `off` | `ditSuppressed` |
|---|---|---|
| loose gate (unsound) | +1.42% | 80 |
| **source condition** | **+13.08%** | **5,819,309** |

`secp256k1_ecdsa_verify` itself stayed at 0 in every gated build. The cost was
five *shared* variable-time group helpers - `gej_add_ge_var`,
`ge_set_all_gej_var`, `gej_double`, `ge_to_bytes`, `xonly_pubkey_serialize` - that
verification runs constantly.

## 3. What was actually routing taint into them

Traced by diffing the reports between configurations. The clobber reports were
**identical**, so it was not the mod-set at all; only the *argument summaries*
differed. One function was the sole originator:

```
secp256k1_silentpayments_recipient_scan_outputs
  └─ secp256k1_ec_seckey_tweak_add(ctx, found_outputs[k]->tweak, label_tweak)
        (main_impl.h:782 - a SEEDED entry point)
```

1. The pointers handed to `ec_seckey_tweak_add` are unresolvable, so
   `HasTaintedArg` is **false** - the frame-address under-taint.
2. `ec_seckey_tweak_add` returns tainted, correctly: it is seeded.
3. The source condition concludes the taint did not come from parameters, and
   marks `scan_outputs` non-argument-sourced. **This conclusion is right** - the
   secret originates at a seed inside the callee, not from any caller of
   `scan_outputs`.
4. Ungateable ⇒ its clobber applies ⇒ its memory is poisoned.
5. **Pointers reloaded from poisoned memory become Data-tainted**, and those
   pointers are output-buffer addresses.
6. Passed on, they mark the five helpers' *arguments* tainted, and everything the
   helpers compute becomes secret.

Confirmed by turning `-taint-frame-addr-args` on: `scan_outputs` drops out of the
non-argument-sourced set entirely. That validated the diagnosis - but the blunt
fallback is not the fix (408 switches, the five still carry 20).

The decisive control: **building without `ENABLE_MODULE_SILENTPAYMENTS`, the
sound gate and the unsound gate emit byte-identical objects** - 179 switches,
verify 0, the five 0. Soundness was never the expense. One module was, through one
call.

## 4. The fix: classify by the callee's parameter type

A Data-tainted register in a **pointer** parameter means "pointer to a secret",
not "this address is itself a secret value". Recording it as Data makes every
value the callee computes from that pointer secret, including further addresses -
which is what turns one poisoned function into a poisoned subtree.

The callee's IR parameter type settles it and is available at the propagation
site. `propagateArgTaintToCallees` now records a tainted register in a pointer
parameter as **pointee-tainted**, not data-tainted.

The residual is a genuinely secret-*valued* pointer, i.e. secret-dependent
addressing - already outside what PSTATE.DIT covers, and already reported
separately by `-taint-uncovered-report` as `secret-address`.

## 5. Result

gem5, serializing, 40 iterations:

| arm | verify cycles vs `off` | verify suppressions | signing coverage vs oracle |
|---|---|---|---|
| always-on | +0.66% | 7,085,999 | - |
| loose gate (unsound) | +1.42% | 80 | 103.1% |
| source condition, before | +13.08% | 5,819,309 | 101.8% |
| **source condition, after** | **+2.09%** | **80** | **103.1%** |

Verification executes **2 switches per call, the same as the unsound gate**
(`simInsts` identical). Signing coverage is back to 103.1% of the hand oracle.
Static count on the full TU: 187 against the loose gate's 174, and 661 ungated.

**The sound gate is now within 0.7 points of the unsound one on the workload that
decides the result.** `sha256_finalize` shedding 5 static switches cost no dynamic
coverage (4,605,220 vs 4,608,037 suppressions, 99.94%).

## 6. What this says

Two amplifiers, not one inherent cost:

1. **The frame-address under-taint** is upstream of everything. It made the gate
   antagonistic with `-taint-frame-addr-args`, and it is what put `scan_outputs`
   into the non-argument-sourced set. P1b (per-object rather than whole-frame) is
   still the real fix.
2. **The data/pointee distinction collapsing on reload** is what made a single
   poisoned function expensive rather than merely imprecise. Fixed here, cheaply,
   by consulting a type the analysis already had.

Neither was visible from switch counts; both required tracing an actual route.
