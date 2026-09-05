# The ExpeDITe run document: how to run the pass, every flag, every default

**This is the operating reference.** How to build the compiler, how to harden
one file or a whole library, what every flag means and what it defaults to,
which control arms to build next to a hardened build, what the reports say,
and what to expect on an M4 or M5. `CLAUDE.md` at the repo root holds the
gotchas; the design docs under `docs/design/` hold the reasoning. Defaults are
as of **2026-09-05** and are read from the code (`cl::init` in
`llvm/lib/CodeGen/TaintAnalysis.cpp`, `TaintFixedPointIteration.cpp`,
`llvm/lib/Transforms/Instrumentation/TaintSourceAnnotator.cpp`,
`llvm/lib/Target/AArch64/`); when this file and the code disagree, the code
wins and this file is the bug.

## 0. Quick start

```
ninja -C build                                                  # the compiler (see 1)
build/bin/clang -O2 -ftaint-harden=seeds.txt -c foo.c -o foo.o   # harden one TU
build/bin/llvm-objdump -d foo.o | grep -ciE '\bmsr\b.*\bdit\b'  # switch sites (-i matters)
```

`seeds.txt` names the parameters that carry secrets (section 3). Without
`-ftaint-harden` codegen is byte-for-byte unchanged; with it and an empty
seed file it is byte-identical to plain `-O2`.

## 1. What the defaults do

The one-line command above gives you all of this:

- **Region placement, intra-block** (`-taint-dit-placement=region`,
  `-taint-dit-sub-block=1`). Only the secret-dependent code runs under DIT:
  a clean preamble stays off, a loop that touches a secret is covered whole
  with one enable at its preheader (`-taint-dit-loop-hoist=1`), a short
  public corridor between two secret regions is merged when two switches
  would cost more than the dwell (`-taint-dit-switch-cyc=30`,
  `-taint-dit-dwell-per-instr=1.0`), and inside a covered block the enable
  sinks to the first secret instruction, the clear hoists past the last, and
  a public run of 8 or more instructions is cut out
  (`-taint-dit-sub-block-min-run=8`). A verifier checks every secret
  instruction is reached DIT-on and falls the function back to
  whole-function coverage if not; a second verifier runs on the final MIR.
- **Every function protects its own secrets** (`-taint-dit-contract=callee`).
  A call is never covered by the caller; a secret reaching a callee this
  build cannot see is an obligation in the info-loss report, with the seed
  line that fills it. Seeding is monotone: adding a seed never removes
  protection elsewhere.
- **A DIT-on caller calls a twin** (`-taint-dit-clone-seeded=1`). Every
  seeded function, and everything it reaches by direct call in its TU, has a
  `<name>.dit` copy that is entered with DIT already set and emits no
  switch; a call made from DIT-on code goes to the twin, so the callee stops
  toggling for itself and the caller stops re-asserting. Across TUs the twin
  is named on the strength of the seed file and the owned-symbols list.
- **Tail calls are off** TU-wide (`-taint-no-tail-calls=1`): a tail call has
  no epilogue to clear DIT in, so one tail call would turn selective
  placement into blanket coverage for the rest of the program.
- **The call-site mod-set gate is on**: a callee's memory clobber applies
  only at call sites that pass it a secret (`-taint-no-modset-gate=0`).
- **A callee outside the build is assumed to leave DIT as it found it**
  (`-taint-dit-external-preserves=1`, the default since 2026-09-05): no
  re-assert after a libc call. `=0` re-asserts after every external call,
  which is right only when an external callee calls back into hardened code;
  section 4 says when.

The pre-2026-09-05 compiler is four flags away, for an A/B:
`-mllvm -taint-dit-contract=inherit -mllvm -taint-dit-clone-seeded=0 -mllvm -taint-dit-sub-block=0 -mllvm -taint-dit-external-preserves=0`.

## 2. Build the compiler

```
cmake -S llvm -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DLLVM_TARGETS_TO_BUILD=AArch64 -DLLVM_ENABLE_PROJECTS='clang;lld'
ninja -C build            # NO target list: libLTO must not go stale
```

The taint analysis links into `clang`, `llc` and `libLTO`; a targeted build
that names only `clang llc` leaves `libLTO` stale and an LTO link then runs
the old analysis with no error. On macOS, once per build directory, so
`#include <stdio.h>` resolves:

```
printf -- '-isysroot %s\n' "$(xcrun --show-sdk-path)" > build/bin/clang.cfg
```

Tests: `build/bin/llvm-lit -sv llvm/test/CodeGen/AArch64/taint-analysis-*.mir llvm/test/Transforms/TaintAnnotate clang/test/CodeGen/taint-*.c` (58 tests).

## 3. How to run it

### The clang flag (the way to build anything you will measure)

```
build/bin/clang -O2 -ftaint-harden=<seed file> [-mllvm -taint-...] -c file.c -o file.o
```

`-ftaint-harden` runs the `taint-annotate` IR pass at the end of the
optimiser (it stamps the seeded parameters), then adds `TaintInterprocPass`
as a module pass right after PrologEpilogInserter, so every MachineFunction
of the TU is analysed at once and the switches are inserted before the
post-RA passes. Every backend flag below is passed as `-mllvm -<flag>`.
Debug info (`-g`) is supported. **Not compatible with LTO for that TU**: the
pass lowers to an object eagerly.

### The taint-source (seed) file

One line per secret parameter, 0-based, `#` comments, C++ needs mangled names:

```
function_name,arg_index            # the argument VALUE is secret
function_name,arg_index,pointee    # the pointer is public, the memory behind it is secret
```

Start with the API entry points that receive the key:

```
crypto_sign_ed25519_detached,4,pointee
crypto_aead_chacha20poly1305_ietf_encrypt,8,pointee
```

An argument index past the function's arity is a fatal error since
2026-09-03 (`-mllvm -taint-seed-report=F` plus `utils/taint_seed_check.py`
finds seeds that applied nowhere). Seeds are TU-scoped parameter attributes;
a function that is only DECLARED in a TU is stamped as seeded elsewhere, and
nothing else crosses a TU boundary, which is why the seed loop in section 4
exists.

### The wrapper and llc (debugging, the report files the clang flag does not write)

```
utils/taint_harden_c.sh --opt-level -O2 file.c        # seed file: file_secret.txt next to it
```

It runs `clang -emit-llvm`, `opt -passes=taint-annotate -taint-src=...`,
`llc -stop-after=prologepilog`, a perl strip of `<mcsymbol >` (MIR CFI
serialisation bug), `llc -enable-new-pm -run-taint-interproc -taint-insert-dit`
with the report flags, then `llc -start-after=prologepilog -filetype=obj`.
It still round-trips through MIR text, so its codegen differs from the clang
path by a per-binary layout lottery (up to +2.65% measured); never time a
wrapper build against a clang build. `-run-taint-interproc` is the `llc`
entry point for the pass on a `.mir` file, which is how the lit tests run it.

## 4. Hardening a library

### Build it twice: once to learn what it defines, once with that list

Every TU of the library must see the same flags; put them in `CFLAGS`. The
info-loss report APPENDS, so remove it before each build.

```
rm -f loss.txt
CC=build/bin/clang CFLAGS="-O2 -ftaint-harden=$PWD/seeds.txt \
   -mllvm -taint-info-loss-report=$PWD/loss.txt" ./configure ... && make
```

Then the owned-symbols list, from the objects this build just made:

```
utils/taint_owned_symbols.sh src/.libs/libfoo.a > owned.txt
```

and build again with it:

```
rm -f loss.txt
CC=build/bin/clang CFLAGS="-O2 -ftaint-harden=$PWD/seeds.txt \
   -mllvm -taint-owned-symbols=$PWD/owned.txt \
   -mllvm -taint-info-loss-report=$PWD/loss.txt" ./configure ... && make
```

The list does two things: the report files a callee outside it as external
(libc, another library: out of scope, no seed proposed), and **a cross-TU
call is only ever redirected to a twin when the callee is in the list**.
Without the list the twins work inside each TU and a cross-TU call keeps the
original, which protects itself. Nothing is lost but the optimisation.

The external-callee assumption is on by default (since 2026-09-05): a
callee the build does not define is assumed never to write PSTATE.DIT, so no
re-assert follows it; a symbol the owned list names is still yours and keeps
it. Turn it off only when an external function calls back into hardened code
(`qsort` with a hardened comparator, `pthread_once`), which returns with the
mode wherever the callback left it:

```
   -mllvm -taint-dit-external-preserves=0 \
```

With it off every callee the build does not define is assumed to clear DIT
and DIT-on code re-asserts after each one: on libsodium that was three
`msr DIT` per argon2 block after glibc's `memcpy`, 393,216 per hash.

### Close the seed loop

```
utils/taint_obligations.py loss.txt --owned owned.txt \
    --next-round seeds2.txt --seeds seeds.txt
```

It prints three lists. `OWNED` is the seed lines to add: callees this build
defines that received a secret it cannot see (another TU, or by address from
the caller's frame). `INDIRECT` is call sites through a pointer: seed the
targets by name. `EXTERNAL` is libc and friends, counted, never proposed. It
writes `seeds2.txt` = your seeds plus the owned lines. Rebuild with it (step
3, second build), repeat until `OWNED` is empty. libsodium's signing path
took eleven rounds from the shipped 65 seeds to 188; the converged files for
libsodium are `benchmarks/crypto/libsodium_secret_contract.txt` and
`libsodium_owned.txt` in gem5-DIT.

**The pre-contract seed files are not enough.** libsodium's shipped seed
file protects nothing under the contract (its seeds sit on forwarders; the
old contract covered everything below by inheritance). Run the loop.

Two obligations no seed can fill, and the report says so: a libc mover
(`memcpy`, `memset`) handed a secret, whose repair is a hardened mover
linked ahead of libc (`gem5-DIT benchmarks/taint_oracle/dit_movers/`), and
an allocator, whose repair is upstream.


### Check that it worked

```
build/bin/llvm-objdump -d src/.libs/libfoo.a | grep -ciE '\bmsr\b.*\bdit\b'   # switch sites
build/bin/llvm-nm src/.libs/libfoo.a | grep -c ' [TtWw] .*\.dit$'             # twins
```

`-i` is required: objdump prints `msr DIT, #0x1` in capitals. Expect twins for
every seeded function and its in-TU callees, and expect the switch-site
count to be LOWER than the same build with `-mllvm -taint-dit-clone-seeded=0`:
the sites that vanish are re-asserts after calls now made to twins.

On stderr, expect one summary line per TU with obligations and nothing
else. `falling back to whole-function coverage`, `DITLEAK` and `cannot carry`
are not expected; each names a function and a reason.

`-mllvm -taint-dit-precision-report=prec.txt` gives per-function
need/underdit/collateral/switch counts, twins included as `<name>.dit`.


## 5. Every flag

Grouped by what it controls. "Default" is the code's `cl::init`. A flag
marked **A/B** exists to build a control arm and should not be on in a build
you ship; one marked **retired** still parses but should not be used.

### Driver

| flag | values | default | meaning |
|---|---|---|---|
| `-ftaint-harden=<file>` | seed file | off | Turns the whole pipeline on for this TU: `taint-annotate` at the end of the optimiser, `TaintInterprocPass` after PEI, `-taint-insert-dit`, and the TU-wide tail-call disable (stamped at codegen so tail-recursion elimination still runs). An empty file gives a byte-identical `-O2` object. |
| `-ftaint-dit-abi` | | off, **not shipping** | The callee-saved PSTATE.DIT ABI (`docs/design/dit-abi.md`): `MRS` at entry, restore at every return, nothing at call sites. Measured and shelved (`docs/results/dit-abi-measured.md`); refuses `-taint-no-tail-calls=0`. |
| `-mllvm -<flag>` | | | Passes any backend flag below through clang. |

### Placement

| flag | values | default | meaning |
|---|---|---|---|
| `-taint-insert-dit` | 0/1 | 0, implied 1 by `-ftaint-harden` | The master switch. With it off the analysis still runs and the report files are written, but no `MSR DIT` is emitted: the unprotected A/B twin of a build. `-mllvm -taint-insert-dit=0` next to `-ftaint-harden` wins. |
| `-taint-dit-placement` | `region`, `function` | `region` | `region`: only secret-dependent code is covered (below). `function`: `MSR DIT, #1` at entry of any function with a secret, `#0` before each return, re-assert after each call that may clear. |
| `-taint-dit-sub-block` | 0/1 | 1 (since 2026-09-05) | Intra-block placement: the entry enable sinks to the block's first secret instruction, a pre-return clear hoists past its last, a DIT-off hole is cut across a public run of at least `-taint-dit-sub-block-min-run`. Block entry/exit states are unchanged. `0` covers a block with any secret instruction whole. |
| `-taint-dit-sub-block-min-run` | n | 8 | Shortest public run worth cutting a hole across. Independent of the switch cost on purpose: two switches cost two instructions of I-cache even when the mode write is free. |
| `-taint-dit-loop-hoist` | 0/1 | 1 | A loop that contains a secret instruction is covered whole and its one enable is hoisted to the preheader. `0` covers only the blocks that contain one, toggling every iteration. |
| `-taint-dit-switch-cyc` | cycles | 30 | What one `MSR DIT` costs, for the corridor admission test: a public corridor between two covered regions is merged unless its dwell (`instructions x dwell-per-instr`, block-frequency weighted) exceeds two switches. 30 is the measured serialising cost on the M5; at 30 against dwell 1.0 the static crossover is ~60 instructions. `0` asserts switches are free and was the default until 2026-08-24; nothing supports it. |
| `-taint-dit-dwell-per-instr` | cycles | 1.0 | The other side of the admission test. |
| `-taint-no-tail-calls` | 0/1 | 1 | Tail calls off TU-wide under `-ftaint-harden`. `0` restores them (**A/B**, worth +8.89 points at 9.4% secret on serialising hardware); refused under `-ftaint-dit-abi`. |

### The contract and the twins

| flag | values | default | meaning |
|---|---|---|---|
| `-taint-dit-contract` | `callee`, `inherit` | `callee` (since 2026-09-05) | `callee`: every function protects its own secrets; a call is never a Need; a secret reaching a callee the build cannot see is an obligation in the info-loss report; a callee-saved restore is a Need. `inherit`: a secret-passing call is a Need and the caller holds DIT across it; the `AlwaysEnteredWithDIT` ownership rule and the Scenario-B assertion exist only here. |
| `-taint-dit-clone-seeded` | 0/1 | 1 (since 2026-09-05) | The DIT twins: every seeded function and everything it reaches by direct call in its TU gets a `<name>.dit` copy entered DIT-on that emits no enable or clear (it still re-asserts after a call of its own that may clear); a call made from DIT-on code is redirected to it. Every TU must be built with the same value. `0` for an **A/B**. |
| `-taint-owned-symbols=<file>` | file, one symbol per line | none | The functions this build defines (`utils/taint_owned_symbols.sh` over the build's objects). Two effects: the obligation report files a callee outside it as `external-call` (out of scope, no seed proposed), and a cross-TU call is redirected to a twin ONLY when the callee is in the list. Without it: in-TU twins only, originals across TUs, nothing lost but the optimisation. |
| `-taint-dit-external-preserves` | 0/1 | 1 (since 2026-09-05) | Assume a direct callee this module does not define, and the owned list does not name, never writes PSTATE.DIT: no re-assert after it, and a function whose only calls are external keeps `PreservesDIT`. Right for libc (removes every re-assert after `memcpy`: argon2id 395,758 switches per hash -> 3, +7.58% -> +1.08% serialising); wrong for an external that calls back into hardened code. Indirect calls unchanged. `docs/results/dit-external-preserves-2026-09-05.md`. |
| `-taint-dit-twin-narrow` | 0/1 | 0 | Region placement inside a twin: a clear at its top over a public preamble, corridors, an enable before every return. Measured not to pay on libsodium (`docs/results/dit-twin-narrowing-2026-09-05.md`). |
| `-taint-dit-twin-switch-cyc` | cycles, -1 | -1 | The switch cost inside a narrowing twin; -1 uses `-taint-dit-switch-cyc`, 0 is maximal narrowing. |
| `-taint-dit-clone-list=<file>` | file | none | The older, explicit twin list (local linkage only); superseded by `-taint-dit-clone-seeded`. |
| `-taint-dit-abi` | 0/1 | 0, **not shipping** | The backend half of `-ftaint-dit-abi`, for `llc` runs. |

### Analysis precision

| flag | values | default | meaning |
|---|---|---|---|
| `-taint-no-modset-gate` | 0/1 | 0 | Turns off the call-site mod-set gate: a callee's memory clobber then applies at every call site, not only where a secret is passed. Maximally conservative, much slower; the escape hatch for taint that does not travel by arguments. The strict source condition and return-call-site gating are unconditional with the gate. |
| `-taint-arg-provenance` | 0/1 | 0 | B1: name the object an incoming pointer argument points at, so a callee's arg-pointee mod-set applies to that object rather than clobbering all caller memory. Measured to close no leak alone (`docs/design/frame-address-gap.md`). |
| `-taint-arg-pointee-args` | 0/1 | 0 | B2: a call argument that points at a tainted arg pointee passes a secret. Needs B1. |
| `-taint-libc-model` | 0/1 | 0 | Model `memcpy`/`memmove`'s memory effect instead of treating them as opaque clobbers of all caller memory. |
| `-taint-unknown-load-tainted` | 0/1 | 0 | U1: a load whose object cannot be resolved is secret whenever any memory-resident secret exists. Byte-neutral on fully seeded mbedTLS (`docs/results/phase2-unknown-tainted-2026-09-04.md`). |
| `-taint-no-mmo-load-tainted` | 0/1 | 0 | U2: a load with no memory operand at all is secret under the same condition. Byte-neutral likewise. |
| `-taint-frame-addr-args` | 0/1 | 0, **retired** | Whole-frame version of B2; superseded by P1b's per-object rule, measured +44 points against the gate. Should have been deleted on 2026-08-24; still parses. Do not enable. |

Not flags, always on: the product taint domain (a value is (Data, Pointee)),
PHI look-through for loop-carried pointers, a global written with a secret
being secret module-wide, `ReturnsPointeeTainted` and the seeded-return
gate, register-tuple parts, a twin inheriting its original's argument taint,
stack-passed arguments seeded as both kinds, the mod-set gate's strict
source condition.

### Reports (all `-mllvm`; a path argument each)

| flag | writes | notes |
|---|---|---|
| `-taint-info-loss-report=F` | one numbered record per site where the analysis lost the secret: severity, what the pass did instead, what it cost, and the seed line that repairs it; obligations under the callee contract | **Appends** across clang invocations, so delete it before a build. Severe losses also warn on stderr with no flag. The seed loop's input (`utils/taint_obligations.py`). |
| `-taint-seed-report=F` | one APPLIED/DECLARED/ABSENT record per seed line per TU | Appends. `utils/taint_seed_check.py` finds seeds that applied nowhere. |
| `-taint-dit-precision-report=F` | per function: need, underdit, collateral, switches, precision, coverage, loop-weighted variants; twins as `<name>.dit` | The number a placement should be judged by; read the loop-weighted columns (`docs/design/dit-precision.md`). |
| `-taint-callsite-report=F` | `ESCAPE` lines (secrets passed to callees the pass cannot instrument), `DITLEAK` lines (functions that exit with DIT set: `tailcall`, `return`, `tailcall-ungated`) | `DITLEAK return` is a placement bug and also warns on stderr. |
| `-taint-uncovered-report=F` | tainted instructions PSTATE.DIT does not protect: divide/sqrt, secret-dependent addresses, secret branches | Gap G2: without it these are silent false assurance. |
| `-taint-clobber-report=F` | call sites that make the caller treat memory as secret | Where a taint explosion originates. |
| `-taint-dit-reassert-report=F` | every call site followed by a re-assert, with the reason (`indirect`, `external`, `clears-on-exit`, `propagates-unresolvable`) | The per-call switch cost, itemised. |
| `-taint-dit-join-report=F` | Off-to-On boundaries, split by whether the join is mixed | The cost edge bundling would remove; measured small (`dit-placement.md` §7). |
| `-taint-nonlocal-report=F` | sites where the mode is simply left set: `setjmp`, `musttail`, unwind, no scratch register | Dwell, never exposure. |
| `-taint-frameref-report=F` | whether each SP/FP-relative address resolves to a frame object | The go/no-go measurement for P1b. |
| `-taint-output=F`, `-taint-regions-output=F`, `-taint-source-regions-output=F` | the per-instruction taint dump (TSV), the coalesced regions, the same by source line | The regions feed these reports only; they do not drive placement (the merge gap is a constant 2). |
| `-debug-only=taint-interproc` | the fixed-point trace on stderr | Needs an assertions build. |

### Instrumentation and measurement controls

| flag | default | meaning |
|---|---|---|
| `-taint-dit-nop-switches` | 0 | **A/B.** Every inserted `MSR DIT` is emitted as `HINT #0` at the asm-printer, so layout and instruction count match the real build exactly with no mode switch executing. The instruction-matched baseline every measurement uses. Not neutral: a NOP costs ~0.25% more than a renamed `MSR DIT`. |
| `-taint-dit-oracle-hooks` | 0 | Staples a call to the gem5 oracle's re-arm trampoline to every switch. Instrumentation only; never time such a build. |
| `-taint-dit-verify-warn-only` | 0 | The final-MIR DIT verifier (`AArch64DITVerifier`, runs last, fails the build on a secret instruction reached DIT-off) reports instead of failing. The object is unsound; for enumerating sites only. |

### Tools

| tool | what it does |
|---|---|
| `utils/taint_owned_symbols.sh <objects>` | the owned-symbols list from a build's objects |
| `utils/taint_obligations.py <loss report> --owned F --next-round out --seeds in` | splits the info-loss report into OWNED / INDIRECT / EXTERNAL and writes the next round's seed file |
| `utils/taint_seed_check.py` | seeds that applied nowhere, from `-taint-seed-report` |
| `utils/taint_dit_precision.py`, `utils/taint_region_distance.py` | precision-report and region-spacing readers |
| `utils/taint_harden_c.sh` | the wrapper flow (section 3) |

### What stderr says

One summary line per TU with obligations under the callee contract
(`N secret-passing call site(s) reach M callee(s) this build does not cover`)
and one for external callees when an owned list is given. A severe
information loss (`DIT stays SET past this call`) warns with no flag.
`falling back to whole-function coverage`, `DITLEAK` and `cannot carry` name
a function and a reason and are not expected in a clean build.

## 6. The control arms

Every measurement in this repo is against these; build them from the same
source with the same seeds so only the thing under test differs.

| arm | flags | what it controls for |
|---|---|---|
| unhardened | `-O2` | the baseline; byte-identical to `-ftaint-harden=<empty seed file>` |
| **NOP** | `... -mllvm -taint-dit-nop-switches` | same placement, same layout, every switch a `HINT #0`: the instruction-matched baseline. Not neutral: a NOP costs more than a renamed `MSR DIT` |
| blanket | unhardened or NOP library + a constructor `msr DIT, #1` linked into the program (`gem5-DIT benchmarks/taint_convolve/dit_blanket.c`) | DIT everywhere, no analysis |
| **Apple bracket** | unhardened library + `utils/dit_host_screening/cioparity/api_bracket.c` around each public entry point: read the previous state, `msr DIT, #1`, `sb` (`isb sy` on gem5, which lacks `sb`), the call, clear only if it was clear. gem5 rig: the linker's `--wrap` (`build_arms.sh` arms `api`, `apiisb`); silicon rig: a compile-time rename of the driver's prototypes, `taint_libsodium_sudo_run.sh` arm `B:baseline:api` | what a careful library author does by hand, and what Apple's `timingsafe_enable_if_supported` does; no analysis |
| twins off | `... -mllvm -taint-dit-clone-seeded=0` | every callee toggles for itself |
| old contract | `... -mllvm -taint-dit-contract=inherit -mllvm -taint-dit-clone-seeded=0` | the pre-2026-09-05 compiler |
| whole-function | `... -mllvm -taint-dit-placement=function` | coarse placement |

The driver that calls the library is compiled without `-ftaint-harden`
unless it handles the secret itself.

## 7. On the M4 / M5

FEAT_DIT is present on every Apple M-series core; check once:

```
sysctl hw.optional.arm.FEAT_DIT      # 1
```

A Neoverse N1 (the Linux hosts here) has no FEAT_DIT and SIGILLs on the
first `MSR DIT`, so hardened binaries run there only under gem5 or
`qemu-aarch64 -cpu max`.

Nothing else is needed: the binary sets and clears PSTATE.DIT itself. What
to know when reading the numbers:

- **Apple's `MSR DIT` serialises; it is the gem5 SERIALISING column that
  has a silicon counterpart, not the renamed one.** Measured on the M5: a
  `MSR DIT` that changes the bit costs ~30 cycles and `MRS DIT` 1 cycle
  (`docs/results/dit-cost-model.md`); an executed clear 34.45 cycles against
  -0.01 for a skipped one, immediate and register forms alike
  (`docs/design/dit-abi.md`, the `dit_exitform` harness); 40 to 45 cycles per
  executed switch across three libsodium benchmarks and ~24 on the signed
  lookup, by division (experiments 09 and 02). gem5's serialising model
  charges 19 to 37. The renamed model is a counterfactual for hardware that
  does not exist yet; nothing in this repository shows Apple renaming the
  write. (An earlier version of this bullet said the opposite, without
  evidence; do not cite it.) So on an M4 or M5 expect the switch count to
  matter as the serialising column says it does, plus the dwell the
  predictors cost while DIT is set.
- **Layout matters at the percent level.** Measure several `argv[0]` lengths
  or binary paths and report the spread, as the experiments do
  (`paper_experiments/*/README.md`, "five stack offsets").
- **Root for the counters.** `kperf` (used by `run_crossover.py` and the
  cioparity rig) needs root; wall-clock does not.
- **Cycle counters, not `msr` counts.** There is no committed-switch counter
  on silicon; the static site count from step 5 and gem5's `commit.ditWrites`
  are the only switch counts. Report both instruments' numbers as that
  instrument's.

The silicon rigs that already exist and take the new binaries unchanged:
`utils/dit_host_screening/cioparity/` (experiment 09),
`gem5-DIT/benchmarks/signed_lookup/run_crossover.py` (experiment 02),
`paper_experiments/01-bitcoin-core-wallet/reproduce.sh` (experiment 01).

## 8. What the twins do not reach

- **Indirect calls.** A function reached through a table or pointer
  (libsodium's Poly1305 and ChaCha20 implementations) keeps toggling for
  itself: a table entry could point at a twin only if every user of the
  table were DIT-on. Seed the targets so they protect themselves; the
  switches stay.
- **Cross-TU callees instrumented by propagation, not seed.** The caller
  cannot know; seed them (harmless, monotone).
- **Hand-written assembly and prebuilt libraries** are as unreachable as
  before (`docs/results/dit-openssl-asm-limit.md`).
- **Code size**: +21% text on libsodium.

## 9. The gem5 rigs behind the numbers

| what | where |
|---|---|
| twins vs no twins vs blanket, both switch models, ed25519 and AEAD | `gem5-DIT benchmarks/crypto/run_clone_timing.sh` (`build_libsodium.sh contract contractnop clone clonenop` first) |
| experiment 02, the secret-fraction crossover | `paper_experiments/02-libsodium-signed-lookup/reproduce.sh` |
| the shadow-taint oracle (what is protected, what is not) | `gem5-DIT benchmarks/taint_oracle/`, `docs/results/dit-callee-contract-2026-09-04.md` §1 for the movers caveat |
