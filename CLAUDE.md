# CLAUDE.md

> **New here? Read [`docs/README.md`](docs/README.md) first** - the documentation
> index, with a reading order and an annotated map of every design doc, measured
> result, and research note. This file holds the authoritative *operating*
> instructions; `docs/` holds the reasoning behind them.

This is an **LLVM fork** (branch `dit-tainter`) implementing **interprocedural taint
analysis + PSTATE.DIT hardening** for AArch64: secret data entry points are declared
in a taint-source file, taint is propagated through registers/stack/global memory at
the MIR level across all functions of a TU, and **PSTATE.DIT (data-independent
timing) mode switches** are inserted so secret-dependent code runs with data-operand
timing side channels suppressed.

**Threat model: data-operand instruction timing (DIT), NOT speculation.** An ISB/DSB
"speculation barrier" mode used to exist as a placeholder for the DIT toggle
mechanism; it was **removed on 2026-07-14**. Speculation defense is out of scope - do
not reintroduce it. Why taint at all, rather than DIT-everywhere: DIT is not free
(some SPEC 2026 benchmarks lose ~15% with it fully on), so it should cover only
secret-dependent code. See `docs/results/dit-cost-model.md`.

## Build

Running builds is fine. They are long, so start them in the background rather than
blocking on them, and do not run benchmarks while one is in flight.

- Build dir: `build/` (Debug, all targets). **Use `ninja -C build` with NO target
  list.** The taint analysis links into `clang`, `llc` AND **`libLTO.dylib`**, and
  `ninja -C build clang llc` leaves libLTO stale - the LTO link then silently runs the
  OLD analysis with no error. That cost two 50-minute measurement builds on 2026-08-30.
  A targeted build must name `LTO` explicitly:
  `ninja -C build clang llc LTO llvm-ar llvm-ranlib llvm-objdump`
- All tools referenced below are `build/bin/...`
- Touching `llvm/include/llvm/CodeGen/TaintAnalysis.h` rebuilds every taint TU and
  relinks `clang`/`llc`, which are large; expect the link to dominate.

**macOS: create `build/bin/clang.cfg` after configuring a fresh build dir.** A
from-source clang does not infer the macOS SDK, so any `#include <stdio.h>` fails with
`'stdio.h' file not found`. Clang reads a default config file sitting next to the
binary, which fixes every invocation at once:

```
printf -- '-isysroot %s\n' "$(xcrun --show-sdk-path)" > build/bin/clang.cfg
```

`build/` is gitignored, so this must be redone per build dir. Verified not to disturb
the test suites (all 1403 `clang/test/Driver` tests pass with it present);
`--no-default-config` opts out of it for a single invocation. The alternative is
`-DDEFAULT_SYSROOT=$(xcrun --show-sdk-path)` at cmake time, or passing
`-isysroot $(xcrun --show-sdk-path)` on every command line. `utils/taint_harden_c.sh`
already auto-detects the SDK itself and needs none of this.

## How to run the pipeline

### Preferred: one-shot clang flag

```
build/bin/clang -O2 -ftaint-harden=<taint-src-file> -c file.c -o file.o
```

Flag absent means codegen is byte-for-byte unchanged. Verify:
`build/bin/llvm-objdump -d file.o | grep -iE '\bmsr\b.*\bdit\b'`
(**`-i` is required** - objdump prints the operand uppercase, `msr DIT, #0x1`, so the
case-sensitive form silently reports zero on a correctly hardened object.)

### Protection: PSTATE.DIT (the only mode)

`-taint-insert-dit` is the master switch at the `llc` level (implied by
`-ftaint-harden`; `-mllvm -taint-insert-dit=0` next to it produces an unprotected
build with otherwise identical codegen, for A/B benchmarking). Without it, the
analysis still runs and the report files are still produced, but codegen is untouched.

**Placement granularity (`-taint-dit-placement`): DEFAULT is `region` (fine-grain).**
Region placement covers only the secret-dependent regions - clean preambles and public
loop scaffolding (coordinate/index math) stay DIT-off - tuned by
`-taint-dit-switch-cyc` (**default 30** = the measured serializing switch cost),
`-taint-dit-dwell-per-instr` (default 1.0), and `-taint-dit-loop-hoist` (**default 1**:
each need-loop is coarsened On with one enable hoisted to the preheader; set `=0` for
block-minimal coverage, where DIT wraps only the blocks containing a secret op and a
need-block in a loop toggles every iteration). Both defaults were flipped on 2026-08-24
after measurement - the old `switch-cyc=0` asserted that toggles are free, which nothing
supports, and both flips widen coverage rather than narrowing it, so neither can
introduce a leak. It
carries a soundness verifier and falls back per-function to whole-function coverage if
it cannot prove coverage, so it is always safe. See `docs/design/dit-placement.md`.
Requires FEAT_DIT (Armv8.4+) at run time - Apple M-series has it
(`sysctl hw.optional.arm.FEAT_DIT`), Neoverse N1 does not, so SIGILL there; verify via
objdump/lit or `qemu-aarch64 -cpu max`.

**The coverage contract at a call boundary (`-taint-dit-contract`, default `inherit`;
`docs/design/dit-callee-contract.md`).** Under `inherit` a secret-passing call is a Need,
the caller holds DIT across it and an unseen callee inherits protection; that is what
makes the seed loop non-monotone (`ecp_mod_p256` lost 1.16M ops of inherited coverage
when a callee inside it was seeded) and what the callee-saved ABI existed to repair.
**`=callee`** is the opt-in alternative (2026-09-04): every function protects its own
secrets, a call is never a Need for its arguments, `AlwaysEnteredWithDIT` and the
Scenario-B assertion are off, and a secret reaching a callee this build cannot see is an
`UNCOVERED` obligation in `-taint-info-loss-report` with the seed line as the repair,
summarised once per TU on stderr. A libc mover handed a secret IS an obligation (its loads
and stores of the secret are the data-value channel DIT covers; the repair is a hardened
`memcpy` linked ahead of libc), an allocator's is unfillable (the repair is upstream). Two
placement facts ride on the flag: spill slots are exempt from blunt-TOP poisoning, and a
callee-saved restore is a placement Need in an instrumented function (it reloads the
CALLER's value, which may be a secret; under inherit the poisoned reload covered it by
accident - 224k ops per run in `mbedtls_mpi_sub_abs` alone). Seeding is monotone under it, no
function needs its entry state, and a callee that leaves DIT set breaks nobody - which is
what makes the cross-boundary cost model (sticky exits, automatic cloning) safe to build
next. **Measured** (`docs/results/dit-callee-contract-2026-09-04.md`): mbedTLS 727 seeds
+6.17% renamed / +251.53% serialising vs +3.50% / +252.62% inherit at 1.6% fewer executed
switches and 99.88% vs 99.93% coverage; libsodium's shipped seeds protect NOTHING under it
until the report's 21 lines are pasted (then identical coverage); and two findings that
outrank it: glibc's `memcpy` blinds the oracle (libsecp256k1's nonce derivation was never
protected; link `gem5-DIT benchmarks/taint_oracle/dit_movers/dit_movers.o` ahead of libc
or every oracle number is an undercount), and callee-saved restores reload the caller's
secrets. Test `clang/test/CodeGen/taint-dit-contract.c`. **Ownership:** the obligation list is
only a to-do list for callees the build defines; `utils/taint_owned_symbols.sh` over the
build's objects writes that set, `-taint-owned-symbols=<file>` makes the report file every
other callee as `external-call` (out of scope, no repair), and `utils/taint_obligations.py`
splits a report offline and writes the next round's seed file (test
`clang/test/CodeGen/taint-dit-owned.c`).

**`-taint-dit-placement=function`** is the opt-in coarse policy: `MSR DIT, #1` at entry
of any function containing taint, `MSR DIT, #0` before each return. Whole-function
coverage avoids per-region toggles clearing an enclosing region's DIT across calls;
`MSR DIT, #1` is also re-asserted after every non-tail call site (a callee may clear
DIT on its exit - gap G1, fixed), except when the callee's `PreservesDIT` summary bit
proves the re-assert redundant (in-TU, uninstrumented, only preserving calls). Secrets
passed to external/indirect callees cannot be protected by placement - audit them with
`-taint-callsite-report=<file>` (`ESCAPE` lines). The same file carries `DITLEAK` lines:
functions that enable DIT and exit without clearing it, which is every tail call in an
instrumented function (a tail call is an exit with no epilogue, so there is nowhere to
put the clear - `docs/design/dit-tailcall-gap.md`). `DITLEAK return` is a placement bug and also
warns on stderr.

**Tail calls are OFF TU-wide for any hardened build** (`-taint-no-tail-calls`, default
1). Not because of the callee-saved ABI - that is not shipping - but because a tail call
has no epilogue in which to clear DIT, so taking one turns selective placement into
blanket coverage for the rest of the program, silently: the switches are all still
emitted and the verifier still passes. libsodium's `randombytes_buf` exits through an
indirect tail call, which is why a program that merely calls `sodium_init()` used to pay
the entire always-on penalty at zero secret fraction (+14.77%, against -0.10% with the
disable; `docs/design/dit-tailcall-gap.md` §7). `DITLEAK tailcall` should therefore
normally be absent; a surviving one means `musttail` or the MachineOutliner.
`DITLEAK tailcall-ungated` means something else - the flag did not reach that TU, or the
build set `-mllvm -taint-no-tail-calls=0`.

Two properties of the implementation are load-bearing. It is stamped **at codegen**, not
in `CodeGenOptions`, because `disable-tail-calls` is read by TailRecursionElimination as
well as by ISel and stamping it early would cost every function in the TU its
tail-recursion elimination; so it is **not** visible in `-emit-llvm` output, only the
`taint-no-tail-calls` module flag is. And it keys on `-ftaint-harden` being **present**,
not on any seed matching, so an `-ftaint-harden=<EMPTY>` baseline arm stays
codegen-matched to the arm it controls for. `=0` is the A/B hatch (worth +8.89 points at
f = 9.4% on serializing hardware, so the trade is real); it is refused under
`-ftaint-dit-abi`.

The coalesced "regions" in the reports **do not drive placement** - they feed the report
files only. The gap that merges them was `-taint-region-merge-gap` until 2026-08-24 and
is now a fixed constant (2), because placement partitions BLOCKS and prices its own
merges with the frequency-weighted admission test.

**The call-site mod-set gate is ON by default** (since 2026-08-24), together with the
strict source condition and return-call-site gating, which are unconditional. It applies
a callee's memory clobber only at call sites that actually pass a secret: without it
`secp256k1_ecdsa_verify` carries 17 `MSR DIT` for public data and Bitcoin Core's
`ConnectBlockAllEcdsa` costs +51.20%; with it, +0.67%. The soundness claim is scoped -
*preserves coverage for argument-carried taint* - and `flowprobe` confirmed four channels
that escape it (returned pointer into a secret buffer, global read by a sibling with no
call edge, inline asm, NEON register tuple). `-taint-no-modset-gate` is the escape hatch.

**The sibling-global channel is FIXED (2026-08-31).** A global written with a secret
anywhere in the module is now secret module-wide
(`TaintSummaryInfo::addSecretGlobal`, folded into the fixed point and consulted on the
global load path), instead of travelling only along call edges. flowprobe under-taint
went **265 ops / 203 sites -> 5 / 5**, and libsecp256k1 and libsodium switch counts are
**unchanged** (16/4 and 120/69), so it is not a flood - a global is a named object, and
only loads of that object are affected. Test:
`taint-analysis-global-sibling.mir`, verified to fail pre-fix.

**C1 and C5 are FIXED (2026-09-04, `docs/results/returns-pointee-2026-09-04.md`).**
Five pieces: a `ReturnsPointeeTainted` summary bit (x0 pointee-tainted at a return,
applied like `ReturnsTainted`, and set for every external callee handed a secret); the
address of a module-secret global is pointee-tainted when materialised; a secret stored
through a pointer makes that pointer pointee-tainted; globals carry pointer-ness
module-wide (`TaintState::PointeeGlobals` -> `WritesPointeeToGlobal` ->
`TaintSummaryInfo::ModulePointeeGlobals`); and **the return gate honours seeds** - a
seeded callee's return applies at EVERY call site, not only where the caller believes it
passed a secret. That last one is what closed C5: flowprobe's `produce_all` has no
parameters, so from its own view it passed nothing, and the summary bit was set and
correct while the returned pointer still came back public. The same hole is deliberately
KEPT in the mod-set gate (that is the U4 decision). Measured: flowprobe 389 -> 256
under-taint ops; libsodium byte-identical (129/129 objects); libsecp256k1 +9 sites
(`secp256k1_scalar_mul` now toggles for itself instead of inheriting) for identical
oracle coverage; mbedTLS 727 seeds +15.8% sites but only +184 executed switches per
resumption, 12 functions' under-taint closed to zero (the record layer's
secret-dependent error codes, `mbedtls_ssl_read_record` 110 -> 0), per-resumption
uncovered 8,610 -> 8,222, **+0.58% renamed / -0.08% serialising**. Test
`clang/test/CodeGen/taint-returns-pointee.c`. The `-taint-call-result-pointee` U5 flag
is gone; the bit subsumes it.

**Two remain open,** and they are storage-independent: flowprobe gained heap twins
C5/C6/C7 on 2026-08-31 (gem5-DIT `a3ca11dc68`) and each fails identically to its global
twin.

| channel | mechanism | status |
|---|---|---|
| C1 / C5 | returned pointer into a secret buffer | **fixed** 2026-09-04 |
| C2 | global read by a sibling | **fixed** 2026-08-31 |
| C3 / C6 | inline asm store (`INLINEASM` isn't `isCall()`, carries no MMO) | 63 under-taint ops each |
| C4 / C7 | NEON register tuple (`isSinglePhysReg` rejects `$q0_q1`) | 63 each |

**Two traps when reading that probe.** `consume_all` used to accumulate every consumer's
secret-derived return into one local, so once ONE channel started working the first call
made it tainted and region placement blanketed all seven calls - the probe reported
everything clean. Note the direction: **the C2 fix is what broke the harness**, because
while every channel was broken no return was recognised as secret and it discriminated by
accident. Now one clean wrapper with its own sink per channel. And `ditseen[]` is a hint,
not the verdict - it samples DIT at *entry*, so it cannot separate "unprotected" from
"self-instrumented, enable comes later", and it reads 1 whenever the caller is DIT-on
across the call. **Read the oracle's per-PC under-taint list.**

### The DIT ABI (settled 2026-08-30) - `docs/design/dit-abi.md`

**To RUN it, read `docs/reference/dit-abi-runbook.md`** - build steps (including the
libLTO trap), the exact flags for LTO and non-LTO, how to read the counts, and what a
gem5 run sees.

**PSTATE.DIT is callee-saved.** An instrumented callee returns DIT exactly as it found
it at every exit it controls; a caller may rely on DIT never coming back lower than it
went in, so **call sites emit nothing**. That removes all four after-call re-assert
classes by construction, with no LTO and no annotation.

**The TU-wide tail-call disable moved OUT of this ABI on 2026-09-01** and now rides on
`-ftaint-harden` (see `-taint-no-tail-calls` above). It was the one piece of the ABI
worth keeping, and it was only ever reachable through the ABI flag. The
TailRecursionElimination hazard that had gated it here is gone: the attribute is stamped
at codegen instead of in `CodeGenOptions`, so TRE runs first and self-recursion still
becomes a loop. What remains ABI-specific is that `-mllvm -taint-no-tail-calls=0` is
refused under `-ftaint-dit-abi`, where a surviving tail call breaks the contract rather
than costing cycles. `musttail` and `MachineOutlinerTailCall` survive regardless and show
up as `DITLEAK tailcall`.

Landed and OPT-IN: **`-ftaint-dit-abi`** (the `-mllvm -taint-dit-abi` cl::opt still
exists for llc/A-B runs; both spellings now get the tail-call disable, since it rides on
`-ftaint-harden`, but only the driver flag refuses the opt-out), the callee half - entry `MRS` into a
pre-PEI-reserved frame slot, a restore at each return, and **nothing at any call site**.
**Both placements are supported.** The restore form is chosen **per exit**, not per
placement: a guarded clear (`tbnz w, #24` over `msr DIT, #0`) where DIT is provably set
at that return, which is free when the function was entered with DIT already on; the
unconditional `msr DIT, Xt` otherwise, because a clear-only restore would return DIT
lower than entry and guarding an ENABLE is forbidden. Whole-function coverage always
takes the cheap form; so does a region-placed return inside an On block, which is the
common case.

**MEASURED 2026-08-31, region placement, M5** (`docs/results/dit-abi-measured.md`):
non-LTO 95 -> 57 switches for **no measurable time change**; full LTO
127,744 -> **15,462** (8.26x) for **-5.40% CoinSelection (25/25)** and **-8.52%
SignTransactionECDSA (27/30)**.

**Default stays OFF, and the reason is not the per-arm delta.** LTO with the ABI is
still slower than non-LTO WITHOUT it (+19.50% / +9.08%), so the ABI only helps in a
configuration nobody should pick on performance grounds, while the default
configuration gains nothing measurable and would pay shrink wrapping and a frame slot
per function. (The TU-wide tail-call disable used to be listed here as a cost of the
ABI; it is now the hardening default and is paid either way.) **Superseded 2026-09-01:
this ABI is not shipping at all - do not prescribe `-ftaint-dit-abi`.**

**libsodium f-sweep, 2026-08-31**: at f=25.8% the ABI costs **+3.34%** against
`def30`'s +8.64% and blanket's +8.78% - so **the ABI is what makes selective
placement beat blanket** on that workload, where the shipped default merely ties it.

The predictor is **re-asserts EXECUTED per unit of work**, not any static count. An
earlier "switches per instrumented function" rule was a proxy that correlated with
LTO and was falsified by libsodium at the same ratio. Ask how often control crosses
an instrumented call boundary.

**Two traps if you touch this.** `TBNZX` hard-codes b5=1, so `TBNZX ..., 24` tests bit
**56**, the guard never fires, and the function strips its caller - use `TBNZW` on the
32-bit subreg, and pin the register WIDTH in tests, because the asm printer shows the
raw operand either way. And any dataflow over the mode must use
`TII->getTimingModeStateAfter`, not `getTimingModeSwitch`: the latter reports
`std::nullopt` for the register-form restore, which reads as "no change" and models DIT
as still set.

New report: **`-taint-nonlocal-report=<file>`** lists the sites where the obligation
degrades to the guarantee and DIT is simply left set - `setjmp`, `musttail`, `unwind`,
and `noscratch`. All are dwell, never exposure.

### When the analysis loses the secret, it SAYS so

**A severe information loss warns on stderr by default - no flag.** Over-approximating
is safe for the secret but it is not free, and the expensive case is invisible:
`crypto_sign` is a two-instruction forwarder that enables DIT and tail-calls
`crypto_sign_ed25519` in another TU. A tail call has no epilogue, so DIT is never
cleared and **every instruction afterwards runs protected** - measured at 100% of the
public lane on the signed-lookup workload, i.e. selective placement had silently
become blanket, with the binary looking hardened and the counters looking right.

```
taint: crypto_sign: DIT stays SET past this call: all later code runs protected,
       so selective placement degenerates to blanket coverage from here on
```

**`-taint-info-loss-report=<file>`** is the detail: one record per site, with two
fields the older reports do not carry - what the loss COST, and the annotation that
repairs it. Severity is judged by consequence, not cause (the same "cannot see the
callee" fact is a footnote at an ordinary call and a disaster at a tail call).

```
taint-stop cross-tu  in=crypto_sign callee=crypto_sign_ed25519
  severity  moderate
  action    the secret is passed to a callee this pass cannot see; DIT is
            enabled so the callee inherits protection
  cost      no placement happens inside the callee - it runs entirely protected
  repair    seed the TU that defines it:
              crypto_sign_ed25519,4,pointee
```

The repair line is pasteable and the loop closes: adding it took `ref10/sign.c` from
**0 to 24** `msr DIT`, and the report then named the next wall
(`crypto_sign_ed25519_detached` -> `crypto_hash_sha512`). That is the intended
workflow - the compiler reports where it stopped, you annotate, it goes deeper.
On libsodium's signing path the loop reaches a **fixpoint in one round**: five seed
lines take SHA-512 from 0 to 14 switches and the curve arithmetic from 0 to 4.

**A callee the seed file already covers is SUPPRESSED, matched per argument.**
Seeds are parameter attributes, so they live with the body - a TU that only
*declares* a seeded callee could not tell "never annotated" from "annotated in the
TU that defines it", and kept re-suggesting a repair the user had already applied
(9 of 41 records on libsodium). `TaintSourceAnnotator` now stamps a seeded
declaration with `"taint-seeded-elsewhere"="<dataMask>,<pointeeMask>"`, read ONLY
by the report - the hardened library is byte-identical with and without it.
Matching is **per argument, not by name**, so a partially seeded callee (key
seeded, message not) is still reported and lists only the MISSING arguments.
Report went 41 -> 32 records, all actionable.

**The report APPENDS and never truncates, and records are numbered.** Truncating
per clang invocation left only the last TU's lines - on libsodium an empty file
while seven functions had warned on stderr - so it accumulates instead, and each
record carries `src=`. The consequence is that **two builds double the file** (32
-> 64 measured), so remove it first; the build scripts here do. The `[N]` counter
is per INVOCATION, shared by both writers, so a run of numbers restarting partway
down the file is how you spot a stale file. Verified safe under `make -j9`: nine
concurrent clang processes appending produced zero interleaved records.

Noise is low because the criterion is consequence: a whole libsodium build produces
**7 warnings on 7 functions**, all of them genuine thin forwarders (`crypto_sign`,
the `crypto_onetimeauth*` family, `poly1305_finish`). A file with no losses produces
no records.

The three older reports (`ESCAPE`, `DITLEAK`, `NONLOCAL`) still exist and each knows
a piece of this; they are opt-in, split across three flags, and phrased as audit
records rather than consequences - `ESCAPE`'s "(covered by inherited DIT)" reads as
reassurance for the exact site that destroyed the measurement.

### Reach limits: what the pass CANNOT instrument

Three, and they compound. The third has no workaround inside the compiler.

1. **Cross-TU** - taint is module-scoped; a secret entering another TU needs its own
   seed line.
2. **Prebuilt libraries** - SQLCipher's OpenSSL provider got 25 `MSR DIT` sites and
   **zero on any cipher instruction**, costing +2.27% for no protection.
3. **Hand-written assembly** - and on aarch64 that is where every serious crypto
   library puts its hot loops. OpenSSL 3.5.4 has 19 perlasm generators covering AES,
   AES-GCM, ChaCha20-Poly1305, P-256, bignum and SHA. **Building from source does not
   help** (unlike limit 2), and `no-asm` is a strawman because the C AES is the
   T-table version whose real leak is cache timing, which DIT does not cover.
   `docs/results/dit-openssl-asm-limit.md`.

**libsodium works because its primitives are C.** When picking a workload, check for
assembly first: `find crypto -name '*armv8*'`.

### Taint-source file format (one per line)

```
function_name,arg_index            # arg value is secret
function_name,arg_index,pointee    # pointer public, memory loaded through it secret
```
0-based indices; `#` comments; C++ needs **mangled** names.

### Wrapper / manual multi-tool flow (debugging, report files)

```
utils/taint_harden_c.sh --opt-level -O2 playground/firefox_convolve_int.c
```
Taint source auto-detected as `<basename>_secret.txt`. Steps it performs: clang
`-emit-llvm`, `opt -passes=taint-annotate -taint-src=...`,
`llc -stop-after=prologepilog`, perl strip of `<mcsymbol >` (MIR CFI serialization
bug), `llc -enable-new-pm -run-taint-interproc -taint-insert-dit`, then
`llc -start-after=prologepilog -filetype=obj`. Report
files: `-taint-output`, `-taint-regions-output`, `-taint-source-regions-output`,
`-taint-nonlocal-report` (DIT obligation degrades to the guarantee),
`-taint-callsite-report` (secret-escape call sites; the clang flag does not emit
these), **`-taint-info-loss-report`** (see below), `-taint-dit-precision-report` (DIT accounting - need/underdit/collateral/
switches per function; reachable from clang as `-mllvm
-taint-dit-precision-report=`). Region spacing:
`utils/taint_region_distance.py OUT.hardened.mir`.

## Code map

| Piece | Where |
|---|---|
| `taint-annotate` IR pass (marks `tainted`/`tainted-pointee` arg attrs from taint-src file) | `llvm/lib/Transforms/Instrumentation/TaintSourceAnnotator.cpp` |
| Interproc MIR taint analysis + DIT mode-switch insertion (`TaintInterprocPass`, new-PM module pass, post-prologepilog, cell-based memory taint, fixed point over call graph, region merging; `insertTaintDITSwitches`) | `llvm/lib/CodeGen/TaintAnalysis.cpp`, `llvm/lib/CodeGen/TaintFixedPointIteration.cpp` |
| `TaintSummaryInfo` - plain per-function summary map, owned and populated by `TaintInterprocPass` (there is no `taint-summary` *analysis*; one existed, always returned an empty summary, and was removed 2026-07-13) | `llvm/include/llvm/CodeGen/TaintSummaryInfo.h` (header-only) |
| `FunctionMemEffects` mod-set summary (callee-to-caller taint through memory) | `TaintSummaryInfo.h`; `computeFunctionMemEffects` in `TaintAnalysis.cpp` |
| Store payload classification (`getNumStoredValueRegs`) | `llvm/include/llvm/CodeGen/TargetInstrInfo.h`, `llvm/lib/Target/AArch64/AArch64InstrInfo.cpp` |
| `TargetInstrInfo::insertTimingModeSwitch` hook (emits `MSR DIT`; the ISB/DSB `insertInstructionBarrier`/`insertDataBarrier` hooks were removed 2026-07-14) | `TargetInstrInfo.h`, `AArch64InstrInfo.cpp` |
| `isDITProtected` membership list - **keep in sync with `docs/reference/dit-spec.md`** | `AArch64InstrInfo.cpp` |
| Taint cl::opts (`-taint-insert-dit` etc. are `extern cl::opt` globals) | `llvm/include/llvm/CodeGen/TaintAnalysis.h` |
| `-ftaint-harden` flag + codegen takeover (`RunTaintHardenCodegen`; runs the taint pass in-pipeline after PEI - the 3-phase MIR round-trip was removed 2026-08-30) | `clang/lib/CodeGen/BackendUtil.cpp`; flag in `clang/include/clang/Options/Options.td`, `clang/include/clang/Basic/CodeGenOptions.h`, forwarding in `clang/lib/Driver/ToolChains/Clang.cpp` |
| **SQLCipher, MEASURED 2026-08-12 (100 paired reps, M5) - a DEFINITIVE NEGATIVE: there is no headroom to recover.** With the oracle correctly wrapping all 3 provider entry points (cipher+kdf+**hmac**): libtomcrypt headroom **+0.89% +/-0.19**, OpenSSL (the DEFAULT shipping provider, hardware AES) headroom **-0.08% +/-0.38 = ZERO (48/100)**. Protecting the secret costs what protecting everything costs (oracle +7.85% vs blanket +8.81% on ltc; +1.87% vs +1.76% on OpenSSL) - almost all of always-on's cost is DIT **on the crypto**, which any correct placement must also pay. **A +8.15% 'first positive result' was reported earlier the same day and is RETRACTED** - the oracle had missed the per-page HMAC, so it was protecting less, not costing less. Also: on the OpenSSL build the pass **cannot instrument the AES at all** (it lives in prebuilt `libcrypto.dylib`) - 25 `MSR DIT` sites, zero on any cipher instruction, costing +2.27% for no protection. Software AES is DIT-expensive only because of its **T-table data-dependent loads**, whose real leak (cache timing) DIT does not even cover; hardware `AESE` is already constant-time. **AES is a bad motivating workload for this project** | `docs/results/sqlcipher.md` **gem5 corroboration 2026-08-13** (`gem5-DIT/docs/dit/studies/sqlcipher-dit-placement-2026-08-13.md`): running the identical binary under serializing vs renamed `MSR DIT` isolates **toggle cost with dwell held constant** - **+0.08% / +12.8% / +19.1%** for 6 / 54 / 63 switch sites, reproducing the M5 ordering and region:hoist ratio (1.49x vs 1.52x) at ~1/3 magnitude. The **prize is ~1.4%** (all of it EVES; DMP/SIP/comp-simp inert or negative here), so the shipped placement spends 19% to protect 1.4%. **Microbenchmarks overstate the prize ~200x** (`lvp_chase` 4.0x vs 1-2% real). **The MIR round-trip is a per-binary codegen LOTTERY** (+0.58% QuickJS, +0.06% native, **+2.65%** gem5, where the zero-DIT `nodit` control is the slowest binary in the table) - baselining against it is necessary but NOT sufficient. **Applies to the `utils/taint_harden_c.sh` / `llc` path only since 2026-08-30**: the clang `-ftaint-harden` path no longer round-trips MIR and its empty-seed control is byte-identical to plain `-O2` |
| Tests | `llvm/test/CodeGen/AArch64/taint-analysis-*.mir`, `llvm/test/Transforms/TaintAnnotate/taint-annotate.ll` |
| Scratch experiments (not shipping code) | `playground/` |

### The MIR round-trip is GONE from the clang path (2026-08-30, `4fb7600db532`)

`-ftaint-harden` used to run codegen three times - legacy PM `stop-after=prologepilog`
to in-memory MIR text (+ `<mcsymbol >` strip), reparse so every MachineFunction is
resident at once for the interprocedural pass, then `start-after=prologepilog` to
object. That was only ever a way to materialize all MFs simultaneously, and it cost
real correctness: MIR does not serialize exception-handling state, so LandingPadInfo /
TypeInfos / FilterIds were dropped on reparse and any C++ TU with a landing pad lost
its exception tables.

**`RunTaintHardenCodegen` now adds `TaintInterprocPass` as a module pass immediately
after PrologEpilogInserter**, in the ordinary pipeline. Same all-resident view, same
point in the pipeline, no serialization - so EH, debug info and CFI stay as codegen
built them.

**Consequence for measurement: the codegen lottery is gone on the clang path.** A
round-trip control (`-ftaint-harden=<empty seed file>`, zero switches) is now
**byte-identical** to a plain `-O2` build. Verified 2026-08-31 on libsodium
(`libsodium.a` and both driver binaries identical) and on libsecp256k1
(`secp_gem5_rt` == `secp_gem5_nodit`). So a `taint`-vs-`base` delta from a
clang-built binary is DIT cost, full stop - no round-trip term to subtract.

**The wrapper still round-trips.** `utils/taint_harden_c.sh` drives `llc` with
`-stop-after`/`-start-after` and the perl `<mcsymbol >` strip, so the lottery still
applies to anything built that way, and to hand-driven `llc` flows. Scope the warning
below accordingly.

Still true: **AArch64 has no new-PM codegen pipeline** (`buildCodeGenPipeline` is
X86/AMDGPU only), so lowering and emission remain on the legacy PM.

## Constraints and gotchas

- **AArch64 only.** Analysis runs post-prologepilog (sees real stack offsets); that is
  why re-lowering uses `-start-after=prologepilog`.
- Interproc scope = one TU/module; cross-TU taint is not tracked - annotate the entry
  function in each TU receiving the secret.
- `-ftaint-harden` is incompatible with LTO for that TU (lowers to object eagerly).
- MIR round-trip landmines, all handled and verified: CFI `<mcsymbol >` (textual
  strip); call-site debug info `callSites:` block-number-vs-layout-position mismatch
  (fixed at root in `llvm/lib/CodeGen/MIRParser/MIRParser.cpp` `parseMachineInst`,
  resolves via `PFS.MBBSlots`); jump tables are safe (compression/relaxation rerun in
  phase 3). `-g -O2 -ftaint-harden` is supported: barriers identical to the
  non-debug build, `llvm-dwarfdump --verify` clean, `DW_TAG_call_site` preserved.
- **Anything that MARKS instructions must skip meta instructions.** `isDITProtected`
  ends in a class fallback - any FP/SIMD register operand counts as covered - and a
  `DBG_VALUE` for a variable living in a NEON register carries one. It was therefore
  a Need, and `pinToTimingMode` marks a Need by *adding an implicit `$dit` operand*.
  A `DBG_VALUE` must have exactly four operands, so the fifth aborted the compiler
  in an unrelated later pass (`malformed DBG_VALUE`, `VarLocBasedImpl.cpp:430`,
  during `Live DEBUG_VALUE analysis`). Fixed 2026-08-31; `needsDIT` rejects
  `isMetaInstruction()` and `pinToTimingMode` refuses to mutate one regardless of
  caller. Test `clang/test/CodeGen/taint-dit-debug.c`. Two things to know: it needs
  FULL debug info (`-gline-tables-only` emits no `DBG_VALUE` and hides it), and the
  **non-debug build was byte-identical before and after the fix**, so no measurement
  taken without `-g` is affected. Same failure shape as the plain-return carve-out
  in `needsDIT`, which the FP fallback had swept in via `RET ... implicit $d0`.
- **A loop-carried pointer is a PHI, and `getUnderlyingObject` stops at one**, so
  before 2026-09-02 every `buf[i]` inside a loop was classified unknown/heap
  rather than as a cell of the object the loop walks. Measured in one libhydrogen
  function: **753 of 777** unresolved accesses. `getCellFromMMO` now falls back to
  `getUnderlyingObjects` (plural) and accepts the answer only when every path
  agrees on one object. Free (libsodium 134 -> 134 switches) and worth **8x** the
  protected operations on libhydrogen signing; the gem5 oracle went 97.61% ->
  80.85% unprotected, and the info-loss report's own suggested repair line went
  from doing nothing to reaching 0.03%. See `docs/design/frame-address-gap.md`.
- `taint-annotate` runs at the **OptimizerLast** extension point so attributes survive
  the -O2 middle-end.
- **Never classify instructions by mnemonic string.** A store's payload-register count
  used to come from `TII->getName(...).starts_with("STP")`, which silently missed
  `STNP*`/`STGP*`/`STXP*`/`STLXP*` (all store-pairs) and so never examined their
  *second* value register - an under-taint, i.e. a missing barrier. It is now the
  `TargetInstrInfo::getNumStoredValueRegs` hook (opcode switch in `AArch64InstrInfo`),
  which returns `std::nullopt` for shapes it cannot classify so callers
  over-approximate. Beware `STGPostIndex`: that is `STG` + `PostIndex`, a *single* tag
  store, not a pair - exactly the trap a prefix test falls into.
- Taint over-approximation is always the safe direction: a spurious barrier costs
  performance, a missing one costs the secret. Any "cannot classify" path must fall
  back to treating every register use as secret.
- **`MachineInstr::uses()` spans implicit DEFS.** It starts after the *explicit* defs,
  so `implicit-def $xN` (which every 32-bit AArch64 result carries) and a call's `$lr`
  clobber appear in it. Any "does this instruction read a secret?" walk MUST guard with
  `MO.isDef()` - check `isReg()` first, `isDef()` asserts on non-register operands.
  Without the guard an instruction that merely *overwrites* a tainted register looks
  like it *read* one and re-taints its own defs, so taint can never leave a register
  (fixed 2026-07-27, was inflating tainted-instruction counts by ~33%; test
  `taint-analysis-implicit-def.mir`).
- **A tail call is BOTH `isReturn()` and `isCall()`.** On AArch64 `TCRETURN*` satisfies
  both, so any "before every return" walk that tests `isReturn()` first will also fire
  on a tail call, which is where the caller hands its secret arguments to the callee.
  That is exactly how whole-function placement came to clear `MSR DIT, #0` immediately
  before `b crypto_sign_ed25519`, running the whole signing operation unprotected
  (fixed 2026-08-05, test `taint-analysis-tailcall.mir`). A tail call has no
  instruction after it at which state could be restored, so it needs its own case, not
  the return case. See `docs/design/dit-tailcall-gap.md`.
- **Memory cells are keyed `(FI/GV, offset, size)`; the READ path must test OVERLAP,
  not equality.** Spilling 8 secret bytes and reloading the low 4 from the same slot
  used to miss the cell entirely and return the secret as public, an under-taint. The
  CLEAR path deliberately stays exact-match (widening a clear would drop taint a
  partial public store never overwrote). Fixed 2026-07-27; test
  `taint-analysis-stack-partial-reload.mir`. See
  `docs/design/spill-soundness-bugs.md`.
- **Callee-to-caller taint through memory is FIXED (blunt-TOP P0, 2026-07-15).**
  `FunctionTaintSummary` carries a `FunctionMemEffects` mod-set (`WritesSecretToGlobal`
  set + `WritesSecretToUnknown` TOP bit). At a call the caller applies it: a direct
  in-TU callee contributes its precise mod-set; an **external decl or indirect call
  receiving a secret is blunt TOP**, which sets `ExternalMemClobbered` and poisons every
  subsequent stack/global/heap load. Own-frame (non-fixed FrameIndex) callee writes are
  ignored (caller-invisible). **P0 is deliberately blunt: no arg-i or per-offset
  precision, weak updates only, every truncation goes to TOP.** P1 (libc model table,
  `memory(argmem: write)` narrowing, arg-i provenance) is deferred. Design and
  literature: `docs/research/memory-summaries.md`; repro
  `playground/callee_memory_gap.c`.

- **A seed must name an entry point in EVERY TU the secret reaches, and an
  indirect dispatch table breaks propagation outright.** SQLCipher's arms carried
  taint on the cipher entry points only, so `sqlcipher_ltc_hmac` got zero switches
  and the per-page HMAC ran with DIT off - coverage topped out at 94.4-95.4% of a
  hand-placed oracle. The tell is exact: `hmac_init.o` and `sha512.o` were
  **byte-identical between the plain and instrumented builds**, i.e. the pass had
  nothing to do in them. Compare object hashes per TU when coverage looks short.
  Note the HMAC could not have been reached by propagation anyway: libtomcrypt
  calls the hash through `hash_descriptor[].process`, a function-pointer table, so
  `hmac_*` -> `sha512_*` needs its own seed line. Fix and measurements:
  `benchmarks/sqlcipher/ltc_hmac_seed.txt` in the gem5 tree, coverage 94.4% ->
  98.4%.
- **`region` is the right policy even on a TU that is secret work end to end - the
  old "use `-taint-dit-placement=function` there" advice was measured at
  `switch-cyc=0` and is OBSOLETE.** Re-derived 2026-08-27 from the existing
  `gem5-sqlc3` placement sweep (`~/Documents/dit-browser-bench/gem5-sqlc3/`,
  arms defined in `utils/dit_host_screening/sqlc_gem5.py`), overhead vs the
  `nodit` baseline at matched config and cache:

  | cache | `hmacfix` region, sw=0 | **`hmacsw30` region, sw=30 (shipped)** | `hmacfn` function |
  |---|---|---|---|
  | 16 serializing | 38.11% | **16.10%** | 17.25% |
  | 1024 serializing | 37.21% | **15.74%** | 16.89% |
  | 1792 serializing | 31.85% | **13.23%** | 14.75% |
  | 1920 serializing | 23.76% | **10.30%** | 10.42% |
  | 16 renamed | 4.55% | **1.17%** | 1.19% |
  | 1024 renamed | 3.17% | 1.45% | **-0.10%** |
  | 1792 renamed | 2.24% | 1.41% | **1.01%** |
  | 1920 renamed | 1.01% | 0.97% | **-0.02%** |

  **Region with the shipped `switch-cyc=30` beats function placement at all four
  cache points on serializing hardware**, ties at cache 16 renamed, and loses by
  0.41-1.55 pp at the other three renamed points. Coverage is a wash: `ditSuppressed`
  is within 0.2-0.8% of function placement's, both ~97-99% of the oracle. **The
  +38.11% that motivated the old advice is `switch-cyc=0`**, i.e. the pre-2026-08-24
  default asserting toggles are free. Do not quote it as region's cost.

  **Why region needs no compression-round special case.** On a loop whose body is
  secret end to end, every block is a need-block, so `admitOffCorridors` early-returns
  at its `all_of(On)` check and region placement *degenerates to whole-function
  coverage by construction*. Verified 2026-08-27: on a synthetic compression-round MIR
  (hot loop, secret `MADD`, non-preserving call in the body), `region` and
  `-taint-dit-placement=function` emit **byte-identical** code - entry enable,
  per-iteration post-call re-assert, exit clear - and `switch-cyc` from 0 to 100,000 is
  inert because there is no corridor to merge. The old 888,967-vs-224,289 switch gap
  was `switch-cyc=0` splitting blocks that 30 now merges, not a policy difference.
  Judge a placement by how often it **leaves** a region, not by switch count.

  **What the admission test still cannot reach**, and it is not corridors:
  `TaintAnalysis.cpp:2826` refuses any one-sided corridor (`!HasOnPred || !HasOnSucc`)
  at any switch cost, correctly, because a leading preamble or trailing epilogue has no
  toggle pair to save. So the residual switches are entry enables, exit clears, and
  post-call re-asserts, reachable only by callee ownership (cloning, or Mode 2) - which
  is what [[dit-switch-cyc-revision]] already identified as the next highest-value
  target.
- **"Coverage >= 100% of oracle" is not reachable and should not be gated on.** A
  wrapper oracle holds DIT across untainted code in the same call - `find_hash`,
  `sha512_init`, loop bookkeeping - which a precise analysis correctly leaves
  alone. Adding full genuine HMAC coverage moved the ratio only ~2 points of the
  ~5 that were "missing", so the rest is oracle slack. Treat a shortfall as a
  pointer to a suspect TU, not as a number to drive to 100.
- **The NOP control is not neutral; substitute a real op as well.**
  `-taint-dit-nop-switches` emits `HINT #0`, which controls for layout but not for
  the issue slot. Against `mul xzr, xzr, xzr` at the same addresses with the same
  instruction count, the NOP build is consistently **slower by ~0.25%** (both
  gem5 switch models, two cache points) - so a NOP baseline overstates itself and
  therefore *understates* DIT cost. There is no `mul` mode in the pass yet; the
  measurements were done by rewriting the switch sites in the assembly.
- **Most of what looks like switch cost can be codegen - CONTESTED, and scoped to
  the round-tripping `llc` path.** With all 121 SQLCipher HMAC/SHA switches turned
  into NOPs and no DIT executing at all, the instrumented build still cost
  **+17.10/+16.57 pp serializing and +4.05/+2.52 pp renamed** - under a renamed
  switch, the majority of the total. Region placement splits blocks inside a
  compression loop and the restructuring is expensive by itself.

  **Two later measurements say the opposite on other workloads, and both postdate
  this one.** The Result 2 re-run (`docs/results/dit-switch-cyc-confirmation.md`
  §8, 2026-08-31) finds the layout share **~0%, not ~100%** on the libsodium
  composite, with the def-minus-NOP term positive at 19 of 20 points and scaling
  2-3x with the switch model. And
  `docs/results/dit-abi-committed-switches.md` resolves 9.54 points of pure switch
  cost on libsodium AEAD from the two switch models over identical binaries, while
  its round-trip control comes back **byte-identical** to base - a layout term of
  exactly zero.

  The likely reconciler: both the SQLCipher NOP arms and Result 2 as originally
  run went through the **MIR-round-tripping `llc` path**, whose codegen lottery
  this file prices at up to +2.65%. The clang `-ftaint-harden` path stopped
  round-tripping in `4fb7600db532`. That cannot be all of +17.10 pp, so do not
  treat it as settled - **rebuild the SQLCipher NOP arms on the current path
  before quoting either side.**

  **Measured on the clang path, mbedTLS 727 seeds, 2026-09-04**
  (`docs/results/dit-callee-contract-2026-09-04.md`): every switch a `HINT #0` costs
  **+4.84% (inherit) / +7.14% (callee) on BOTH switch models**, MORE than the real arms
  on the renamed model (+3.50% / +6.17%) - executing DIT in place of the NOPs recovers
  about a point, blanket's -1.40% direction. On renamed hardware the entire cost of
  selective placement on this workload is the inserted instructions and their layout;
  on serialising hardware the mode adds +244 to +248 points on top of that same share.
  The NOP-not-neutral caveat (~0.25%) applies.
- **A gem5 ROI delimited by `m5_reset_stats` does NOT give exactly equal
  instruction counts across machine configs.** The marker lands as a scheduled
  event, so a ROB-scale number of in-flight instructions commit on either side.
  Measured across a 40-run sweep the discrepancy is always exactly 0 or +400 and
  never negative - a fixed offset that does not scale with the region (400 out of
  85M and out of 887k alike). The identity gate therefore takes a 0.01%-of-ROI
  tolerance; a real divergence scales with the workload. Do not read the constant
  as contamination.

### Internal structure (post-2026-07-13 cleanup)

- **The domain is a product (2026-09-03, `docs/design/taint-domain.md`).** A value
  is a `TaintVal` = (Data, Pointee), the same two may-facts whether it sits in a
  register or a memory cell; a store deposits the whole value in a cell and a load
  reads it back, so the memory side is ONE map (`TaintState::Cells`, keyed by
  `MemCell` = (`TaintObject` Frame/Global/Arg, offset, size)) rather than one set per
  kind. Register provenance (`PointerBases`, MUST, intersects on join) stays a
  separate component from pointee taint (MAY, unions) on purpose - deriving the
  latter from the former would either under-taint or cost the measured +44-point
  frame-address rule. The old `Address` kind was provably a subset of `Data` and is
  gone. The two kinds are parameterized by `TaintKind`, not written out longhand -
  `TaintState::regs(K)` selects the bitvector, and one `updateWithAliases` holds the
  subreg/superreg walk.
- Every consumer of a converged `TaintResult` goes through the single
  `replayTaint(MF, TR, TSI, AA, Post, Pre)` visitor, which hands out `TaintFacts` (the
  four booleans consumers actually need) rather than a per-instruction `TaintState`
  copy. Do not hand-roll another `TR.IN.find(&MBB)` replay loop - that is how the
  replay drifts out of step with `propagateTaintMI`.
- Report files go through `deriveReportPath` / `openTaintReport` (`TaintAnalysis.h`);
  `openTaintReport` returns null both for an unrequested report (empty path) and a
  failed open.

## Testing

```
build/bin/llvm-lit -sv llvm/test/CodeGen/AArch64/taint-analysis-*.mir llvm/test/Transforms/TaintAnnotate
```

All 33 tests pass as of 2026-08-27. The whole `llvm/test/CodeGen/AArch64` suite was
last run clean on 2026-08-27 (3898 discovered, 3894 pass, 4 pre-existing XFAIL, 0
failures).

**End-to-end reference:** harden `playground/firefox_convolve_int.c` and compare
per-symbol DIT placement between the clang flag and the wrapper - they must match
exactly. Under the default `region` placement a tainted function's clean preamble is
DIT-off and the `msr DIT, #0x1` sits at the loop preheader, not the entry (add
`-mllvm -taint-dit-placement=function` for the old whole-function reference: one
`msr DIT, #0x1` at entry, one `msr DIT, #0x0` before each return).

**Exception since 2026-08-08 - a function that is `AlwaysEnteredWithDIT` gets
whole-function coverage even under `region`, and that is correct, not a regression.**
It was entered with DIT already on, so it does not own DIT and may not clear it; region
placement narrows *by clearing*, so narrowing there would strip the caller's
protection. Nothing is actually lost: the caller's region already covered the callee.
In `firefox_convolve_int.c` this applies to `convolve_pixel_int` (internal,
address-never-taken, sole call site passes the pointee-tainted `source`), so expect
**one entry enable and no preheader enable** there; `run_kernel_int` is unaffected and
still shows the preheader form. `-debug-only=taint-interproc` prints which functions
took this path. See `docs/design/dit-callee-ownership.md`.

**No `isb`/`dsb` anywhere** in either mode (the ISB/DSB mode was removed 2026-07-14).
Count mnemonics, not `grep` hits on the objdump output - file paths themselves often
contain "isb"/"dit" and inflate naive counts.

`playground/firefox_convolve_int.c` is a good *correctness* reference but a **bad
performance benchmark for placement**: it is DIT-insensitive (whole-program DIT =
0.968x), so it cannot show a placement win. See `docs/results/dit-cost-model.md`.

**Writing MIR tests:** a register-class mismatch fails as an llc abort in the
MachineVerifier with an empty FileCheck stdin, which looks nothing like a CHECK
mismatch. `ADDWri` takes `GPR32sp` and `STRXui`/`LDRXui` take `GPR64`; declaring vregs
`gpr32common`/`gpr64common` satisfies both the plain and `sp` variants. Tests that pin
a fix should be verified to **FAIL against the pre-fix build**, not just to pass after
it, and should carry positive controls where the obvious wrong implementation would
otherwise pass.
