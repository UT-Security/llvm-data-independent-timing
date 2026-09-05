# DIT twins: cloning the callees of DIT-on code

**Status 2026-09-05: DEFAULT ON** (`-taint-dit-clone-seeded`, `=0` for an A/B
arm), together with the callee contract; the recipe is
`docs/reference/harden-runbook.md`. Landed 2026-09-04 as opt-in, where the
no-flag build was verified byte-identical on the full libsodium archive.
One rule changed at the flip: a cross-TU call is redirected to a twin ONLY
when the owned-symbols list names the callee (§2.2), so a build without
the list keeps the original across TUs instead of risking an undefined
`<name>.dit` at link time. Phase C
item 1 of the callee contract (`dit-callee-contract.md` §5). Measured on
libsodium's signing path under the contract with the converged round-11 seed
file: executed DIT writes per two signatures **10,400 -> 41** (inherit: 6)
at identical coverage, for **+21% text**. Timed on gem5 (§5.1): on the
serialising switch model signing goes from **+76% to +2.2%** over the
instruction-matched NOP baseline, and the twins beat blanket by 0.4 points;
on the renamed model the switches were never the cost and the twins tie
blanket. AEAD keeps 38 of its 58 switches per call behind implementation
tables (indirect calls are never redirected) and stays +6.1% serialising.

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
3. `f` is a function this build defines: the `-taint-owned-symbols` list
   contains it. The list is REQUIRED for a cross-TU redirect (since the
   default flip; the opt-in version assumed every seeded declaration cloned
   and would have failed at link time on a seed naming a function the build
   does not define, such as a hardened `memcpy` seed in a build linking
   libc's). Without the list the cross-TU call keeps the original, which
   protects itself: the optimisation is lost, never the coverage.

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

### 5.1 Timing, gem5 NeoverseV2 FDP, both switch models

Five arms, all callee contract, round-11 seeds, one binary per arm and
workload (`benchmarks/crypto/run_clone_timing.sh` in gem5-DIT). The baseline
the user asked for and the one used throughout: the same build with every
switch emitted as `HINT #0`, so the instruction stream is matched; blanket is
that NOP library plus the constructor that sets DIT before `main` and never
clears it, so it is matched too. The renamed model runs every arm; the
serialising model (`--no-speculative-dit`) runs the two arms whose switches
execute, since a NOP or blanket arm executes no switch inside the ROI.
(gem5's `simInsts` skips `isNop()` at commit, so a NOP arm's instruction
count reads lower than its real arm's by exactly the executed switches; the
binaries are matched, the stat is not.)

**ed25519, 50 signatures of 1 KiB** (`crypto_sign`):

| arm | switch model | cycles | DIT writes | vs own NOP baseline | vs blanket |
|---|---|---|---|---|---|
| no twins, NOP | | 4,579,009 | 0 | | |
| no twins | renamed | 4,465,099 | 176,500 | -2.49% | -3.58% |
| no twins | serialising | 8,069,259 | 176,500 | **+76.22%** | +74.24% |
| twins, NOP | | 4,550,491 | 0 | | |
| **twins** | renamed | 4,615,182 | 800 | +1.42% | -0.34% |
| **twins** | serialising | 4,650,441 | 800 | **+2.20%** | +0.42% |
| blanket (twins' NOP library + constructor) | | 4,631,047 | 0 | +1.77% | |

**AEAD ChaCha20-Poly1305, 200 encryptions of 1400 B:**

| arm | switch model | cycles | DIT writes | vs own NOP baseline | vs blanket |
|---|---|---|---|---|---|
| no twins, NOP | | 3,051,306 | 0 | | |
| no twins | renamed | 3,072,053 | 11,600 | +0.68% | -0.19% |
| no twins | serialising | 3,301,260 | 11,600 | +8.19% | +7.26% |
| twins, NOP | | 3,053,529 | 0 | | |
| **twins** | renamed | 3,073,056 | 7,600 | +0.64% | -0.16% |
| **twins** | serialising | 3,239,461 | 7,600 | **+6.09%** | +5.25% |
| blanket | | 3,077,912 | 0 | +0.80% | |

Three readings.

- **On serialising hardware the twins are the whole result.** Signing pays
  3,530 switches per signature without them and 16 with, and the cost goes
  from +76% to +2.2%, 0.4 points above blanket. That is the regime the
  contract was uneconomic in, and it is closed.
- **On renamed hardware the switches were never the cost.** The no-twin
  arm with 176,500 executed switches is the fastest binary in the table,
  faster than its own NOP baseline by 2.5%: a renamed `MSR DIT` is cheaper
  than the `HINT #0` standing in for it, which is the NOP-not-neutral
  effect already recorded for mbedTLS, at a size that makes the NOP-relative
  number for that arm meaningless. What the twins cost there is dwell: a
  twin is covered whole, the no-twin arm's callees clear between calls, and
  the twin arm lands where blanket does (+1.4% against +1.8%). The renamed
  cost of the contract with twins is the cost of DIT being on, not of
  turning it on.
- **AEAD shows the limit.** Poly1305 and ChaCha20 are reached through
  implementation tables (`blr`), and an indirect call is never redirected,
  so `poly1305_blocks`, `chacha20_encrypt_bytes` and the donna entry points
  toggle for themselves on every block: 38 of the 58 switches per encryption
  survive, and serialising stays +6.1%, five points worse than blanket. The
  repair is not in the compiler: a table entry can point at the twin only
  if every caller of the table is DIT-on, which is a property of the
  library, not of a call site. Seeding the table's targets already makes
  them protect themselves; making them free needs the dispatch to be direct.

## 6. Not done

- **Silicon.** gem5 charges ~21 cycles of rename stall per serialising
  switch by model; the ordering is the result, the magnitudes are the
  model's. The M5 has the renamed behaviour and cannot show the serialising
  column.
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
