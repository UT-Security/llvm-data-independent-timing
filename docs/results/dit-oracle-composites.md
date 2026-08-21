# Oracle placement vs always-on, on four composite hosts

**Measured 2026-08-14**, Apple M5. Rig: `utils/dit_host_screening/secret/`.
Follows `docs/results/dit-host-screening.md`, which measured the size of the
prize; this measures whether the prize can actually be collected.

---

## What this is

`dit-host-screening.md` established that four real hosts have a bigger always-on
DIT cost than QuickJS. That is condition (d) only. This experiment adds a real
secret to each host and asks the question the thesis actually rests on:

> **does fine-grained placement beat always-on?**

It answers it with a **hand-placed oracle**, not the taint pass. That is
deliberate:

- The oracle is the **upper bound on any placement**, including anything the
  pass could emit. If the oracle does not beat always-on, no pass can, and the
  host is dead before any instrumentation work starts.
- It needs no taint instrumentation, so it can cover four hosts in an afternoon.
- It is the same method `docs/results/sqlcipher.md` used, so results are
  comparable to that negative.

What it does **not** show is that the *pass* can hit the oracle. On SQLCipher the
gap between oracle and shipped region placement was enormous (+0.89% oracle vs
+56.97% region). Closing that gap is the remaining work, not something measured
here.

---

## 1. Design

**One binary per host, DIT mode chosen at runtime from argv.** No codegen
differs between arms, so the MIR round-trip codegen lottery
(`dit-measurement-traps` trap 7b) cannot apply at all.

| mode | behaviour |
|---|---|
| `off` | never set DIT - the baseline |
| `always` | set once before work, never cleared - the always-on reference |
| `oracle` | set on entry to each signature, cleared on exit - perfect placement |
| `off2` | a **second run of the identical `off` arm** - the rig's own noise floor |

**Secret payload, shared by all four hosts** (`secret_payload.c`): real ECDSA
signing over libsecp256k1, ~13-16 us per signature measured. Chosen because it
is one taint seed (`secp256k1_ecdsa_sign`, arg 3), self-contained C with direct
calls and no vtable boundary, a region 40x above the
`dit-granularity-crossover` threshold, and - the point - **its declassification
is defined by the protocol**: signatures are published. That replaces the
write-to-sink trick the QuickJS result needed.

**Coverage audit (trap 8 - an under-protecting oracle looks exactly like a
win).** The secret key lives in one static, `g_seckey`, and is read only inside
`secret_sign_n` between the enable and the disable. No host touches it. The
message hash in and the signature out are both public. So oracle coverage is
100% **by construction**, not by inspection - which is the failure mode that
produced and then retracted SQLCipher's "+8.15% first positive result".

**Ordering.** Arm order is **rotated every rep**. This was added after a
zero-signature control caught a real flaw: with `SIGS=0` the oracle arm executes
**zero** `MSR DIT` instructions and does zero secret work, so it must be
identical to `off` - yet under a fixed arm order it read **+1.28%**. That was
ordering/thermal drift penalising whichever arm ran last, and it was inflating
every oracle residual. The `off2` arm now measures that floor directly on every
run.

---

## 2. The composites - what is public and what is secret

Each host pairs its screened public workload with interleaved signing. The
signing is spread through the run, not batched at the end, so the two genuinely
alternate the way a real deployment does.

| host | PUBLIC half | SECRET half | deployment story |
|---|---|---|---|
| **lua** | binary-trees (screened +14.52%) | `sign(1)` every 64 tree iterations | Redis Lua scripting / game server signing a token |
| **sqlite** | insert + indexed join/aggregate/order workload over 120k rows (VDBE + B-tree) | `sign(1)` every 2000 inserts and every 10 queries | a database signing an audit record or session token per batch |
| **cpython** | pyperformance `richards` + `go`, the two most DIT-sensitive bodies (+11.11% / +7.70%) | `_secret.sign(1)` once per "request" | **a Python web app signing a session cookie or JWT per request** - the strongest real story on the list |
| **quickjs** | Octane `richards` + `deltablue` + `splay` | `sign(1)` per benchmark run | a JS service issuing a signed token per unit of work |

Secret fractions land at 0.02%-1.7%, i.e. realistically small, which is the
premise the whole thesis rests on.

**How each was made to have a secret half.** Lua, CPython and QuickJS are all
*embeddable*, so the composite embeds the interpreter as a library and registers
a native `sign()` callable from the guest language - `lua_pushcfunction`,
`PyImport_AppendInittab`, `JS_NewCFunction` respectively. SQLite is embedded
directly and the signing is interleaved at the C level between statement
batches. This is the same shape as the existing QuickJS rig, except the secret
is real rather than `secret_mix`.

---

## 3. Results

16 reps, 2 burn-in discarded, arm order rotated, paired by rep.

| host | off s | always s | oracle s | **always-on** | **oracle residual** | recovered | o<a reps | rig noise |
|---|---|---|---|---|---|---|---|---|
| **lua** | 2.082 | 2.290 | 2.096 | **+10.47%** | +0.63% | 94.0% | 15/16 | +0.33% |
| **sqlite** | 3.006 | 3.116 | 3.018 | **+3.48%** | +0.46% | 86.9% | 16/16 | +0.47% |
| **cpython** | 3.896 | 4.288 | 3.899 | **+9.87%** | +0.07% | 99.3% | 16/16 | +0.09% |
| **quickjs** | 1.367 | 1.451 | 1.355 | **+6.78%** | -0.62% | 109.2% | 16/16 | +0.01% |

Checksums identical across all modes on all four hosts.

**The result: oracle placement recovers essentially the entire always-on cost on
every host tested.** The precise recovery percentages should NOT be quoted -
every oracle residual sits at or below the rig's own noise floor (sqlite +0.46%
against a +0.47% floor; cpython +0.07% against +0.09%; quickjs -0.62% against
+0.01%, and a negative is an artifact since DIT can only remove optimizations).
The defensible statement is **"residual indistinguishable from zero"**, not
"94.0%" or "109.2%".

**Always-on costs differ from `dit-host-screening.md`** (lua 10.47 vs 14.52,
sqlite 3.48 vs 6.09, cpython 9.87 vs 7.00, quickjs 6.78 vs 1.08) because the
composites run different workload mixes than the standalone benchmarks - e.g.
the QuickJS composite drops navier-stokes and crypto, which are the least
DIT-sensitive parts of the Octane subset, so its cost rises. Compare within this
table, not across the two.

### The arm-ordering artifact, and why the `off2` arm exists

A first pass at this experiment used a **fixed** arm order and reported QuickJS
recovering only **67.6%**, with a +3.25% oracle residual - 36x larger than
toggle cost plus payload tax could explain. Rather than write a story for it,
the zero-signature control was run: with `SIGS=0` the oracle arm executes **zero
`MSR DIT` instructions and does zero secret work**, so it is instruction-for-
instruction identical to `off` - and it still read **+1.28%**.

That is ordering/thermal drift penalising whichever arm ran last. Rotating arm
order moved QuickJS from 67.6% to 109.2%. **Every number in the fixed-order run
was biased against the oracle**, and the `off2` arm now measures that floor on
every run so it cannot recur silently. This is the same class of error as
`dit-measurement-traps` trap 3, in a new disguise.

---

## 4. Hosts that could NOT be given a secret half, and what it would take

| host | why not | what would be needed |
|---|---|---|
| **git** | not embeddable as a library; commit signing shells out to `gpg`/`ssh-keygen`, a **separate process**, so there is no taint path from git's address space into the signer and nothing for the pass to instrument | link a signing library into git itself (e.g. replace the `gpg` invocation with an in-process libsecp256k1/Ed25519 call). Invasive and unrepresentative of how git actually works. **Recommend dropping git as a candidate** - it screened at only +2.48% anyway, the lowest non-QuickJS prize |
| **Bitcoin Core** | not attempted here; heavy C++ build | already vendors libsecp256k1 as source, so the payload needs no grafting at all - wallet signing is genuinely present. The open risk is condition (d): much of validation time is signature *verification*, which is crypto and therefore DIT-insensitive (libsodium: +0.1%). **Screen it first** with the always-on rig before building a composite |
| **PHP / Ruby** | not built; long dependency-heavy builds | both are AOT bytecode interpreters with native session-signing in their standard libraries, so the secret half already exists in-tree. Worth screening if a third interpreter data point is wanted, but the interpreter finding is already covered by lua/cpython/quickjs |
| **Redis / LevelDB** | no natural secret | would need the same grafting treatment as SQLite. LevelDB is worth screening as a **compiled** tree-descent control, to test whether the interpreter dispatch loop really is the discriminator |

---

## 4b. Superseded by a real-application measurement (same day)

`docs/research/real-world-instances.md` §6 measures **Django 6.1 + PyJWT +
cryptography, unmodified from PyPI** - a real instance of the pattern the
`cpython` composite models. It costs **+1.21% always-on**, against this
composite's **+9.87%**. Same interpreter family, same machine, same rig; the
gap is workload selection (`richards`/`go` are dispatch-bound, a real Django
request is largely C-layer work), and it is **not** a build artifact - PGO vs
non-PGO was tested and both read 7-9%.

At a realistic signing rate the oracle recovers **~0.64 points of 1.18%** there,
consistent in direction (11/12 reps) but close to the noise floor.

**Treat the composite numbers in §3 as an upper bound obtained on favourable
benchmarks, not as application figures.**

## 5. Caveats

1. **Oracle, not pass.** This is the ceiling. The gap between oracle and what
   `-ftaint-harden` actually emits is unmeasured here and was catastrophic on
   SQLCipher.
2. **Grafted secrets.** Only Bitcoin Core would have a native one. The
   declassification boundary is real, but the *pairing* of workload and secret
   is constructed - a reviewer will say so, and the honest answer is that
   CPython + per-request signing is a faithful model of a real deployment even
   though this particular binary is not a real application.
3. **Single machine, single core type.** M5 P-cores. The LVP is a P-core
   feature; expect materially smaller effects on E-cores.
