# Context-insensitive summaries are the dominant false-positive source

**Measured 2026-07-28** on libsodium 1.0.21 with CIO's 21 seed functions, by auditing
*which* functions we instrument that CIO does not, and asking whether a secret can
actually reach them.

## The finding

A `FunctionMemEffects` mod-set is **per function, not per call site**. Once any caller
passes a secret into `crypto_hash_sha512_update`, that function's mod-set becomes TOP
(`WritesSecretToUnknown`), and `propagateTaintMI` then applies TOP at **every** call site
of it — including callers that passed nothing secret. `ExternalMemClobbered` is set in
those callers, poisoning all their subsequent loads.

Concretely, from `-taint-clobber-report`:

```
CLOBBER modset-top callee=crypto_hash_sha512_update caller=crypto_auth_hmacsha512
CLOBBER modset-top callee=crypto_hash_sha512_final  caller=crypto_auth_hmacsha512
CLOBBER modset-top callee=argon2_hash               caller=argon2i_hash_raw
```

`crypto_auth_hmacsha512` authenticates a public message under a key that **is not in the
seed set**. No secret reaches it by any path. It is instrumented anyway, because ed25519
signing uses the same SHA-512 helpers and polluted their summaries.

## How big it is

Call-graph closure from the 23 seed symbols: 96 functions downward (callees), 50 upward
(callers), 123 in the union. Functions we instrument that CIO does not, classified
against that closure:

| Config | ours-only | via downward | via upward only | **outside both closures** |
|---|---|---|---|---|
| fallback OFF | 63 | 13 | 2 | **48** |
| fallback ON | 199 | 18 | 12 | **169** |

169 of 199 (and 48 of 63 with the fallback off) are functions that no secret can reach by
argument or return propagation. They are instrumented purely through context-insensitive
mod-set application. **This is a larger false-positive source than the frame-address
fallback, the `implicit-def` bug, and the alloca case combined** — and unlike those it is
a design property, not an oversight.

## Are we *missing* anything? (the other direction)

Of the 19 CIO-reference functions we do not instrument even with the fallback on, **15
are not reachable from any seed at all** — no secret can arrive, so they are artifacts of
CIO's blunt domain (every unresolvable load returns TOP, and TOP = Taint, so any function
their worklist *visits* generates alerts). The remaining 4 are reachable:

- `crypto_hash_sha512_init`, `randombytes_init_if_needed`, `sodium_misuse` — write
  constants / abort; they process no secret. Correct not to taint.
- `crypto_stream_chacha20_ietf` — a thin wrapper that forwards its own arguments to the
  seeded `stream_ietf_ext_ref`. A genuine **modeling difference**: seeding an internal
  function does not retroactively mark the corresponding argument of its *callers* as
  secret. Not a leak (the secret is protected where it is seeded), but if you care about
  the wrapper, seed the wrapper.

So the miss direction is clean; the false-positive direction is where the work is.

## Fix direction

Context-sensitivity is the real answer, but the cheap 80% is **P1b**: apply
`WritesSecretThroughArgPointee{i}` to the *object the caller actually passed for argument
i*, instead of collapsing it to a whole-function `ExternalMemClobbered`. A caller that
passed only public buffers then absorbs nothing. That also makes the frame-address
fallback per-object rather than whole-frame (`taint_frame_addr_fallback.md`), which is
what its 9.1× instruction-volume cost is paying for.

A cheaper stopgap worth measuring first: gate mod-set application on whether **this** call
site passes a secret (`taintedCallArguments(...).any()`), matching how the
external/indirect path already gates on `HasTaintedArg`. That is still context-insensitive
in the summary but context-*sensitive* in the application, and it would have suppressed
every example above. Needs care: a callee can write a secret it obtained from a global or
a previous call rather than from this caller's arguments, so this is NOT sound in general
— measure the delta, then decide whether it belongs behind a flag next to
`-taint-annotation-driven`.
