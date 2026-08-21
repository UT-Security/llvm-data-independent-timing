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
fallback per-object rather than whole-frame (`docs/design/frame-addr-fallback.md`), which is
what its 9.1× instruction-volume cost is paying for.

**Correction: P1b is a much smaller lever than that makes it sound.** Measured on the
same libsodium run, only **17 of 583** secret-writing call sites resolve provenance to an
argument at all; the other 566 are TOP. So precise application of
`WritesSecretThroughArgPointee{i}` has almost nothing to act on until provenance itself
improves, which likely means analyzing at IR and carrying the facts down to MIR
(`getUnderlyingObject` usually cannot reach the `Argument` through optimized post-PEI
code). Recover provenance first, then do P1b. (Figure recorded 2026-07-29 in commit
`49c4d74`; this doc is the citation target for it in `docs/README.md` and
`docs/overview.md`.)

A cheaper stopgap worth measuring first: gate mod-set application on whether **this** call
site passes a secret (`taintedCallArguments(...).any()`), matching how the
external/indirect path already gates on `HasTaintedArg`. That is still context-insensitive
in the summary but context-*sensitive* in the application, and it would have suppressed
every example above. Needs care: a callee can write a secret it obtained from a global or
a previous call rather than from this caller's arguments, so this is NOT sound in general
— measure the delta, then decide whether it belongs behind a flag next to
`-taint-annotation-driven`.

**IMPLEMENTED AND MEASURED 2026-08-19** as `-mllvm -taint-modset-callsite-gated`
(default off). Full write-up: `docs/results/dit-modset-callsite-gated.md`.

On Bitcoin Core's libsecp256k1 it cuts 660 switches to **178** and takes signature
verification from **+51.20% to +0.67%** on M5, with **no coverage loss**: gem5
`ditSuppressed` on the signing path holds at **103.1% of the hand oracle** (98.8%
of the ungated pass), and cycles land **+0.13%** from the oracle against the
ungated **+6.80%**. Against blanket always-on DIT the pass goes from 4 wins /
5 catastrophic losses to **5 wins / 4 small losses**.

Two refinements the measurement produced:

- **`WritesSecretToGlobal` should not be gated.** It is already per-global rather
  than a flood, and it is precisely the "callee got the secret from a global"
  case the gate is otherwise unsound for. Leaving it ungated costs nothing
  measurable and removes the most likely unsound shape.
- **The gate must NOT be paired with `-taint-frame-addr-args`** — measured
  2026-08-19, correcting the opposite conclusion drawn from static switch counts.
  The gate asks whether an argument *register* is tainted; the fallback taints
  frame addresses on a **whole-frame** approximation, so nearly every call site
  looks secret-passing and the gate stops firing. `ConnectBlockAllEcdsa`: gate
  alone **+0.66%**, fallback+gate **+45.32%**, i.e. the fallback costs +44.43 points
  (15/15 reps). gem5 agrees: verification suppression 80 ops with the gate,
  6,042,126 with fallback+gate. Static counts do fall (404 vs 975) — they are the
  wrong metric. `+frame-addr +gate` is also the only configuration whose signing
  coverage lands *below* the hand oracle (99.86%).

  The `&secret_local` under-taint is real, but the fallback costs more than the
  flood it replaces. **P1b is the way to close it**: per-object precision instead
  of whole-frame taint, which would not trip the gate's predicate.

**What it does not reach.** The same context-insensitivity exists in the
*register/argument* summary, not just the mod-set: `secp256k1_scalar_set_b32`
carries `PointeeTaintedArgIndices` from signing's secret nonce and replays it when
`ecdsa_verify` passes it a public message hash. That residue is 2 switches per
verification and is now the largest remaining false-positive source. P1b or real
context-sensitivity is what reaches it.

---

## SQLCipher, measured 2026-08-12: the key does NOT spread

The libsodium numbers above are the false-positive case. SQLCipher is the
opposite, and worth recording so the two are not conflated.

Built `sqlite3.c` (262,970 lines) with the repo's own seed set
(`gem5-DIT/benchmarks/sqlcipher/sqlcipher_secret.txt`), region placement:

| | |
|---|---|
| functions instrumented in `sqlite3.c` | **2** (`sqlcipher_ltc_kdf`, `sqlcipher_ltc_cipher`) |
| `MSR DIT` in the whole TU | **11** |
| clobber sites | 2, both `external-arg` into libtomcrypt (`pkcs_5_alg2`, `cbc_start`) |
| escapes | the same 2, both "covered by inherited DIT" |
| compile overhead | 2.3x (4m45s vs 2m06s) |

**No context-insensitive explosion.** Unlike libsodium, nothing in SQLite's core
gets instrumented. The reason is structural: the key crosses into libtomcrypt
almost immediately, and the only in-TU functions touching it are the two provider
shims.

**The decrypted plaintext is not tracked**, and that is deliberate rather than a
gap. `sqlcipher_ltc_cipher` passes SQLite's pager buffer as `out` to
`cbc_decrypt`, which fills it with plaintext - but the seed set declares the
*key* secret (`cbc_decrypt,3` is the CBC state holding the key schedule), not the
data. DIT is defending against key extraction; the plaintext is what the
authenticated user is entitled to read. State this explicitly when reporting, or
it looks like an under-taint.

**One real gap the report surfaces:** `UNCOVERED secret-branch
func=sqlcipher_ltc_cipher bb=2 : CBZW` - a secret-dependent branch, which DIT
does not cover (control-flow timing is outside its guarantee).

### Where SQLCipher actually fails: granularity, not precision

libtomcrypt carries 53 switches, concentrated in `cbc_encrypt` (17),
`cbc_decrypt` (12), `rijndael_setup` (10), `cbc_start` (10). `cbc_encrypt`'s loop
dispatches `cipher_descriptor[...].ecb_encrypt` through a function pointer **once
per 16-byte block**, so a 4 KB page is ~256 DIT regions wrapping ~300-500 cycles
of AES each.

The granularity crossover measured independently on QuickJS
(`docs/results/quickjs.md`) is **~1300 cycles of work per region**. SQLCipher sits
3-4x below it, so fine-grained placement is predicted to lose - which matches
every SQLCipher result the project has. At the measured 62-74 ns/region, 256
regions/page is ~18 us of toggling against ~25 us of AES, i.e. a ~70% tax on the
crypto, consistent with the +46%..+94% in `docs/results/dit-cost-model.md`.

**This is the first time a threshold measured on one workload has predicted
another workload's outcome.**
