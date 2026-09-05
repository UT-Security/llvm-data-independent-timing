# DIT twins: cloning the callees of DIT-on code

**Status 2026-09-04.** Landed OPT-IN as `-mllvm -taint-dit-clone-seeded`
(default byte-identical, verified on the full libsodium archive). Phase C
item 1 of the callee contract (`dit-callee-contract.md` §5). Measured on
libsodium's signing path under the contract with the converged round-11 seed
file: executed DIT writes per two signatures **10,400 -> 41** (inherit: 6)
at identical coverage, for **+21% text**. Timing is not measured here.

## 1. The problem it removes

Under the callee contract every function protects its own secrets: a seeded
or propagated-taint callee enables DIT at entry and clears it at every exit,
and a caller whose covered span continues past the call re-asserts after it.
That is three switches per dynamic call, paid by every primitive down to the
field multiply, whether or not the caller was already DIT-on. On libsodium's
signing path the contract's fixpoint costs 10,400 DIT writes per two
signatures where the inherit contract, which holds DIT across the call from
one forwarder, pays 6 (`docs/results/dit-callee-contract-2026-09-04.md` §4).

All three switches are redundant whenever the caller is DIT-on at the call.
The callee's enable sets a bit that is already set; its clear strips the
caller's protection, which is what the re-assert then repairs. The caller
knows it is DIT-on; what it cannot do is tell the callee, because the callee
is one symbol with one body serving DIT-off callers too.

## 2. The mechanism

Give the callee a second body. `f.dit` is a copy of `f` with one property
that is not an analysis result but a fact about who may name the symbol:
**it is only ever called from code that is DIT-on at the call.** Nothing else
names `f.dit`; the only references are the ones this pass creates, and it
creates them only at call sites it has placed inside a covered span. So the
twin

- emits **no entry enable** and **no exit clear**, since DIT is set at entry
  by construction and the caller owns it;
- **owes its caller DIT set on return.** After any call of its own that may
  clear DIT (an unseeded external, an instrumented original that is not
  itself redirected) it re-asserts, and it does so even when its body holds
  nothing secret: a forwarder `f(key) { g(key); }` has no Need under the
  contract and would otherwise emit nothing, and the caller that chose `f.dit`
  precisely so as not to re-assert would run its next secret op unprotected
  if `g` cleared. This is why a twin always takes the whole-function emitter,
  which re-asserts after every clobbering call and redirects the rest
  (`insertTaintDITSwitches`, `emitFunctionGranularityDIT`);
- redirects its own calls to twins in turn, so a DIT-on subtree pays nothing
  below its root.

The original `f` is untouched. It still enables and clears for itself, which
is what a DIT-off caller, a function pointer, an implementation table or a
caller in a TU that does not know about the twin needs. Only direct calls made
from DIT-on code are redirected (`redirectCallsToDITClones`, an operand
rewrite from `f` to `f.dit` at the call). A caller loses nothing it had.

### 2.1 Who gets a twin

The annotator (`TaintSourceAnnotator.cpp`, at OptimizerLast so the body is the
optimized one and the seed attributes are already on it) clones

- every **seeded** definition, and
- every definition a seeded one **reaches by direct call inside the TU**.

The second half matters. The MIR pass instruments a callee that receives the
secret by propagation exactly as it does a seeded one, so `ge25519_cmov`,
unseeded and reached from `ge25519_cmov8_base`, toggles for itself on every
one of the eight calls per table lookup; with only seeded twins that was most
of what remained (4,784 writes of the 10,400). The reachable set is an
over-approximation of "will be instrumented" - which is only known after the
MIR analysis, too late to clone at the IR level without a two-phase build -
and the over-approximation is free of everything but code size: a twin never
emits a switch the whole-function path would not emit for its original.

The twin keeps the original's **linkage and visibility**: an external `f` has
an external `f.dit` that another TU can name; a static one has an internal
twin, kept alive in `llvm.used` until the MIR pass gives it a caller. Address
taken is no bar, since the address still names the original.

### 2.2 Across TUs, without LTO

A caller in TU A sees only a declaration of `f`. Three facts let it name
`f.dit` anyway:

1. `f` is in the seed file, so the annotator stamped the declaration
   `taint-seeded-elsewhere` (already the case since 2026-09-03);
2. this build clones seeded functions: the annotator sets the module flag
   `taint-dit-clone-seeded`, so the MIR pass knows the TU that defines `f`
   was compiled the same way;
3. `f` is a function this build defines: the `-taint-owned-symbols` list,
   when given, must contain it. Without the list every seeded declaration is
   assumed cloned, and a seed naming a function the build does not define
   breaks at link time as an undefined `f.dit` - loud, not silent.

Then `ditCloneFor` synthesizes the declaration `f.dit` in TU A with `f`'s
type, attributes and visibility, marks it `taint-dit-clone`, and the call is
redirected; the linker resolves it to the twin TU B emitted. No LTO, no
cross-module view, no annotation beyond the seed file the contract already
needs. It is the same assumption the ownership list encodes for the
obligation report: a seeded callee we define is compiled with these flags.

The reachable-but-unseeded set is **in-TU only**, because a caller cannot
see what a declaration reaches. A cross-TU callee that is instrumented by
propagation in its own TU keeps toggling for itself; the fix is to seed it,
which the obligation loop will not propose (it is covered) but which is
harmless and monotone.

## 3. Where the redirect happens, and why it is safe

- **Region placement**: before emission, every call in an On block
  (`insertTaintDITRegions`). Under the callee contract the entry enable may
  sink past a call, so a call to a twin **pins** it (`sinkEntryEnableTo`,
  `hoistExitDisableTo` treat `isDITClone(callee)` as a pin); a sub-block hole
  never spans a call. After redirect the emitter's clobber scan sees a callee
  that leaves DIT set (`calleeLeavesDITSet`) and emits no re-assert.
- **Whole-function placement**, a twin, or a function entered with DIT set:
  every call, since DIT is on from the entry enable (or entry) to the clear
  immediately before each return.
- **The verifier** (`computeDITOnEntry` + the replay in
  `insertTaintDITRegions`) treats a call to a twin reached DIT-off as
  unsound and falls the function back to whole-function coverage. It should
  never fire; it is there so that if it does, no twin is entered DIT-clear.
- The info-loss report skips twins: every record a twin would file is
  already filed under its original.

Both contracts get it. Under inherit an in-TU, address-not-taken callee that
is always entered DIT-on already emits no switches (`AlwaysEnteredWithDIT`),
so the twin adds nothing there; what it adds under inherit is the cross-TU
case and address-taken callees. Under the callee contract, where ownership
is never relaxed, it is the whole difference.

## 4. What it costs

| | round 11, no twins | seeded twins | seeded + reached twins |
|---|---|---|---|
| `msr DIT` sites in the archive | 489 | 382 | 358 |
| twins in the archive | 0 | 68 | 83 |
| `.text` bytes | 316,119 | 360,219 | 383,255 (+21%) |
| redirected call sites (linked driver) | 0 | 325 to 40 twins | 400 to 49 twins |

Two static residuals are worth knowing about. Calls from a twin to a
`noreturn` function (`__stack_chk_fail_local`, `sodium_misuse`,
`__assert_fail`: 23 of the 33 un-redirected calls) get a re-assert that can
never execute; it is dead code, not a dynamic cost, and skipping it is a
one-line `hasFnAttribute(NoReturn)` test not done here. And a twin is
covered whole, where its original may have been region-placed, which is the
+5.6% wasted coverage below.

## 5. Measured: libsodium signing, gem5 oracle, round-11 seeds

Two signatures, `--eves --dmp --comp-simp`, the protocol of
`taint_oracle/run_sodium_oracle.sh`; every arm the same driver, linked
against the arm's `libsodium.a`.

| arm | protected | uncovered | wasted | DIT writes | set / clear | instructions |
|---|---|---|---|---|---|---|
| inherit (shipped seeds) | 294,164 | 0 | 53,996 | 6 | 4 / 2 | 630,664 |
| callee, round 11 | 294,164 | 0 | 51,134 | 10,400 | 6,947 / 3,453 | 641,189 |
| callee, round 11, seeded twins | 294,164 | 0 | 53,972 | 4,784 | 3,198 / 1,586 | 640,678 |
| **callee, round 11, seeded + reached twins** | **294,164** | **0** | **54,010** | **41** | **36 / 5** | **636,727** |

Coverage is identical in all four arms: cloning moves switches, it does not
move protection. The 41 that remain are the entries into the hardened
library from unhardened code - `crypto_sign_ed25519_detached` is a forwarder
with no Need and so is Off, and `_crypto_sign_ed25519_detached` below it
enables for itself and then calls nothing but twins (SHA-512, the scalar
arithmetic, `ge25519_scalarmult_base`, `sodium_memzero`); the keypair setup
takes the originals of `crypto_hash_sha512` and `ge25519_scalarmult_base`
for the same reason. Inherit's 6 is one forwarder holding DIT across
everything. The contract with twins is within a few entry toggles of that,
with every dependency still explicit and every callee still protecting
itself for any caller the twin does not serve.

The 4,511 fewer instructions are the removed switches net of the twins'
whole-function coverage; nothing else in the binary changed, which the
identical coverage counts confirm.

## 6. Not done

- **Timing.** This is a switch-count and coverage result on gem5; the
  serialising-model cost of 10,400 writes was never measured on this
  workload either (experiment 9's rig is the place).
- **"Clone what is instrumented"** instead of what a seed reaches would need
  the MIR analysis's verdict at IR time: a two-phase build, which is what the
  older `-taint-dit-clone-list` (`docs/design/dit-callee-ownership.md`) was.
  It still exists and still requires local linkage.
- **Cross-TU propagation.** A callee in another TU that is instrumented by
  propagation, not seed, is not twinned from the caller's side.
- **`noreturn`** re-asserts inside twins (§4).
- A twin of a function that is never called from DIT-on code is dead weight
  the linker keeps (no `--gc-sections` in these builds).

## 7. Code

| piece | where |
|---|---|
| `-taint-dit-clone-seeded`, the reachable set, the twin, the module flag | `TaintSourceAnnotator.cpp` |
| `isDITClone` (public), `ditCloneFor` incl. the cross-TU declaration, `redirectCallsToDITClones` | `TaintAnalysis.cpp` |
| `taintOwnedSymbols()`: the owned list, loaded once, shared with the report | `TaintAnalysis.cpp`, declared in `TaintAnalysis.h` |
| a clean twin still emitted; the verifier's twin-call check | `insertTaintDITSwitches`, `insertTaintDITRegions`; the emission loop in `TaintFixedPointIteration.cpp` |
| twins skipped by the report | step 3c, `TaintFixedPointIteration.cpp` |
| tests | `clang/test/CodeGen/taint-dit-clone-seeded.c` (two TUs, both contracts' shapes), `llvm/test/Transforms/TaintAnnotate/taint-dit-clone-seeded.ll` |
