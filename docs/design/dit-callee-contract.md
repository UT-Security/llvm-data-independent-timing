# The callee contract: every function protects its own secrets

**Status 2026-09-04.** Landed OPT-IN as `-taint-dit-contract=callee`
(default `inherit`, byte-identical to before). Phases A and B of the plan:
the contract in placement, the obligation report, tests, and the four
measurements. Phase C, the cross-boundary cost model (sticky exits chosen by
frequency, automatic cloning, tail calls to in-TU callees), is a follow-up
and is only safe once this contract holds; its first item, cloning, landed
the same day (`dit-cloning.md`: libsodium's 10,400 DIT writes -> 41 at
identical coverage). Measurements:
`docs/results/dit-callee-contract-2026-09-04.md`.

## 1. The two contracts

**Inherit (shipped).** A secret-passing call is a Need (`needsDIT`'s
`|| MI.isCall()` term, `isTaintedInstruction`'s `PassesPointeeSecretToCall`),
so the caller holds DIT across it and the callee inherits protection. Step 3c
asserts the "Scenario-B invariant": every secret-passing call executes with
DIT set. A callee this pass cannot see - another TU, an indirect target, a
library - is covered by that inheritance, and the info-loss report says so at
moderate severity: "DIT is enabled so the callee inherits protection".
`AlwaysEnteredWithDIT` (docs/design/dit-callee-ownership.md) is derived from
the same guarantee: a function whose every call site passes a secret is
entered with DIT on, so it never clears.

**Callee.** Every function protects its own secrets, and no function relies on
the state it was entered with:

- A call is never a Need for its arguments. A `bl` has no data-dependent
  timing; the instructions that marshalled the secret arguments are Needs in
  their own right. Whether the caller's region spans a call is a cost decision
  the corridor pricing already makes, not a coverage decision.
- A callee that receives taint through the call graph instruments itself, as
  before. Nothing changes in propagation.
- A secret reaching a callee this build cannot see is an **obligation**: an
  `UNCOVERED` record in the info-loss report with the repair, and one summary
  line per TU on stderr. Three repairs, by callee: a seed line for a callee
  another TU defines; for a libc mover (`memcpy`, `memmove`, `mempcpy`,
  `memset`), a hardened mover linked ahead of libc, because its loads and
  stores of the secret bytes are the data-value channel DIT covers
  (`docs/reference/dit-spec.md` lists loads and stores, and the gem5 oracle
  counts them - a first draft of this note exempted movers and libsecp256k1's
  oracle showed why not); for an allocator (`malloc`, `calloc`, `realloc`,
  `free`), nothing a seed can fill: the secret is a size or a pointer, the
  allocator's control flow depends on it, and the repair is upstream.
- Two placement facts the contract needs, gated on it so the default stays
  byte-identical. A **spill slot is exempt from the blunt-TOP poisoning** of
  stack loads after an external secret-passing call: it is compiler-private,
  no callee can have written it, and without the exemption the epilogue's
  link-register reload was the only Need left in a thin wrapper around such
  a call. And **a callee-saved register restore is a placement Need** in any
  instrumented function (`isCalleeSavedRestore`): it reloads the caller's
  value, which may be a secret no summary can know about (a summary knows the
  callee's parameters, not its caller's live registers), and the load's
  data-value timing is the channel DIT covers. Under inherit the poisoned
  reload kept the exit clear below the restores only by accident; the oracle
  priced the accident at one `ldp x22, x21` per signature on libsecp256k1 and
  224k ops per run in `mbedtls_mpi_sub_abs` alone, whose callers keep limb
  pointers and carries in x19-x26 across every call. Only the placement's
  NeedSet, verifier and pin consult it; `functionHasTaintedRuns` and the
  precision report do not, so a clean function stays clean. A first version
  stopped the exit-clear hoist at a restore instead, which reaches only a
  clear placed inside the epilogue block, not one at the block boundary
  before it.
- The Scenario-B assertion and `AlwaysEnteredWithDIT` are off. `PreservesDIT`
  and the `.dit` clones remain: a clone is entered DIT-on by construction of
  who may call it, which is a placement fact, not a taint fact.

## 1.1 Ownership: what is ours to seed

An obligation is only a to-do item if the callee is ours. The pass cannot
tell from inside one TU whether another TU of the same build defines a
callee, so ownership is a build-wide fact supplied to it:

- `utils/taint_owned_symbols.sh <objects|archives>` writes the functions the
  build defines, one per line (every defined text symbol, statics included,
  since a seed matches a static by name inside its TU).
- `-taint-owned-symbols=<file>` makes the report classify at the source. An
  unseen callee in the set stays an `uncovered-callee` obligation with its
  seed line; one outside it becomes an `external-call` record, Info severity,
  "out of scope for the seed loop", with its class named (mover, allocator,
  other) and no repair, because a hardened mover or a hardened libc is the
  developer's decision, not the compiler's demand. Indirect sites stay
  obligations: their targets are found by name. The per-TU stderr line
  splits the same way. Codegen is identical; taint still propagates through
  an external call exactly as before.
- `utils/taint_obligations.py <report> --owned <file> [--next-round out
  --seeds in]` is the same split offline, and writes the next round's seed
  file: the previous seeds plus the owned lines. Build, run it, rebuild,
  oracle; the loop ends when OWNED is empty.

On libsodium's round-two report (the shipped 65 seeds plus round one's 21):
10 owned lines (`crypto_hash_sha512`, `_update`, `ge25519_p3_tobytes`,
`sc25519_muladd`, `randombytes_buf`, `sodium_memzero`,
`argon2_encode_string`), 7 indirect sites (the Poly1305 and ChaCha20
implementation tables), and 12 external sites on 4 callees (`memset` x7,
`memcpy` x3, `memmove`, `__explicit_bzero_chk`) that are no longer proposed.

## 2. Why

Three properties, none of which the inherit contract has:

1. **Seeding is monotone.** Adding a seed can only add coverage. Under inherit,
   seeding `mbedtls_mpi_grow` stripped `ecp_mod_p256` of the protection it was
   inheriting, from 0 to 1,157,430 uncovered ops, because the newly
   instrumented callee cleared DIT on exit inside a function that had no
   instrumentation of its own (`seed-loop-not-monotone`). Under the contract
   nothing depended on that state, so nothing can be stripped; `ecp_mod_p256`
   shows up instead as what it is, an unseeded function-pointer target, at the
   indirect call site's obligation record.
2. **No function needs its entry state.** The callee-saved ABI existed to learn
   one bit at run time, was I entered with DIT on, at the price of an `MRS`
   that the gem5 model must serialise, a reserved frame slot, and a per-exit
   restore form. Under the contract that bit is irrelevant: enables are the
   callee's job and a correctness matter; clears are a performance matter,
   decided per exit, and a callee that leaves DIT set breaks nobody.
3. **The obligation list is complete and honest.** Every place a secret leaves
   the analysis's sight is a record with a repair, and a build with an empty
   list plus a clean oracle is the verification. Under inherit the same sites
   read as reassurance, and the one that destroyed a measurement (a tail call
   into another TU turning selective placement into blanket) looked like
   success.

## 3. What it costs

- **Inheritance is no longer a guarantee.** A function on the secret path that
  the analysis does not instrument is unprotected. That is the seed set's
  defect, made visible; the info-loss report names the site. Complete seeding,
  verified by the oracle, was already the requirement after the ABI's
  retirement; the contract makes it explicit.
- **Obligations that cannot be filled.** `memcmp` and any computational libc
  routine handed a secret must be replaced by a hardened one linked into the
  build. The byte-loop movers in `benchmarks/taint_oracle/dit_movers/` fill
  the `memcpy`/`memset` obligations and fix the oracle (section 1 of the
  results note), but they are a naive `memcpy` and cost mbedTLS +10% on their
  own; a hardened build of a real `memcpy` is the honest version and is not
  done. Hand-written assembly and prebuilt libraries are as uncoverable as
  before; the contract lists them instead of hiding them behind an enable in
  the caller.
- **Switches.** Every hot callee toggles for itself, which on the serialising
  switch model is the 12.1M-writes regime. Phase C is where that falls: with
  the contract holding, an exit clear can be skipped wherever the
  frequency-weighted cost model says the dwell it saves is cheaper than the
  toggle, and a callee reached only from On regions can be cloned without a
  switch. Neither is safe under inherit.

## 4. What changes in the code

| where | inherit | callee |
|---|---|---|
| `isTaintedInstruction`, `needsDIT` (`TaintAnalysis.cpp`) | a call with secret arguments is a Need | a call is never a Need |
| step 3c invariant (`TaintFixedPointIteration.cpp`) | asserts the enclosing function is instrumented | off |
| step 3b-2 `AlwaysEnteredWithDIT` | computed | off |
| info-loss record at an unseen callee | `cross-tu` / `indirect`, moderate, "inherits protection" | `uncovered-callee` / `uncovered-indirect`, `UNCOVERED`; repair = seed line, or a hardened mover, or "upstream" for an allocator |
| `ESCAPE` line suffix | "(covered by inherited DIT)" | "(UNCOVERED: callee contract)" or "(UNCOVERED: libc mover, link a hardened one)" |
| stack load after an external secret-passing call (`propagateTaintMI`) | every stack load poisoned | spill slots exempt |
| callee-saved restores (`isCalleeSavedRestore`) | not Needs | placement Needs in an instrumented function, so the exit clear sits below them |
| stderr | per severe record | one summary per TU: sites, callees, indirect targets |

Test: `clang/test/CodeGen/taint-dit-contract.c`, which runs both contracts on
the same five shapes so every check is a difference between them.

## 5. Not in this phase

- Post-call clears in Off regions when a callee may leave DIT set, sticky
  exits by frequency, tail calls to in-TU instrumented callees: Phase C.
  Cloning is done (`dit-cloning.md`, `-taint-dit-clone-seeded`, opt-in).
- `sinkEntryEnableTo` / `hoistExitDisableTo` still stop at a call because the
  callee "may inherit DIT". Under the contract that is merely conservative
  (an enable a little earlier), not wrong; tightening it is a Phase C
  measurement.
- Flipping the default.
