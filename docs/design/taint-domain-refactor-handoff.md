# Phase 1 handoff: refactor the taint domain to a product lattice

Written 2026-09-03 so that Phase 1 can start in a separate worktree while
Phase 0 finishes. Everything a fresh agent needs is here or linked from here.
Read `docs/research/cio-taint-implementation.md` first (1009 lines, source-level
study of CIO's checker) - it is the evidence behind the design below.

## Where this sits

Three phases, agreed with the project lead on 2026-09-03:

| phase | what | status |
|---|---|---|
| 0 | fix the gem5 oracle's store rule; add seed validation | in flight, worktree 4 (see below) |
| **1** | **refactor the taint domain (this document)** | **not started** |
| 2 | "unknown means tainted" everywhere | folds into 1's audit |

Phase 1 is independent of Phase 0's code. Its PRIMARY verification is
differential (same compiler, placement unchanged) and needs no oracle. Only
the final dynamic validation wants Phase 0's fixed gem5, and that can come
last.

## The premise, stated honestly

**Every time the compiler's analysis was suspected on 2026-09-02/03, the
compiler was right.** Store coverage, register-move coverage, the
data-vs-address classification of pointers in `mbedtls_mpi_mul_mod`, the
soundness verifier under sub-block placement: all correct. The two real bugs
were a seeding gap (no ECDHE seed) and an oracle propagation rule (gem5, not
LLVM). So this refactor is for simplicity and maintainability. **It must not
regress code that currently passes.** That is why the verification protocol
below is differential and strict.

## The domain today

`llvm/include/llvm/CodeGen/TaintAnalysis.h`, `struct TaintState`:

| field | meaning | kind |
|---|---|---|
| `TaintedRegs` | register holds a secret value | taint channel 1 |
| `PointeeTaintedRegs` | register points at secret memory | taint channel 2 |
| `AddressTaintedRegs` | register is a secret-dependent address | taint channel 3 |
| `PointerBases` (`Reg -> Frame(FI) \| Arg(k)`) | what object a register points at | provenance, INTERSECTS on join |
| `TaintedArgPointees` | incoming arg k's pointee is secret | taint, unions on join |
| `TaintedStackCells`, `PointeeTaintedStackCells` | memory, keyed (FI, off, size) | memory taint, two channels |
| `TaintedGlobalCells`, `TaintedWholeGlobals` | memory | memory taint |
| `TaintedUnknownMemValues`, `PointeeTaintedUnknownMemValues`, `UnknownMemTainted` | heap/unknown | memory taint, two channels |
| `ExternalMemClobbered` | a call may have written a secret anywhere | TOP bit |
| `OutgoingArgSecret`, `NonArgSourcedTaint` | side flags | flags |

Three separately propagated register channels plus a provenance map, each with
its own rule per instruction class inside `propagateTaintMI`
(`llvm/lib/CodeGen/TaintAnalysis.cpp:1217`, ~500 lines), and the memory side
duplicated per channel. `TaintKind {Data, Pointee, Address}` and
`TaintState::regs(K)` are the existing seam - the channels are already
parameterized, which is the hinge for this refactor.

Blast radius (references across TaintAnalysis.{h,cpp},
TaintFixedPointIteration.cpp, TaintSummaryInfo.h): `TaintedRegs` 26,
`PointeeTaintedRegs` 10, `AddressTaintedRegs` 10, `PointerBases` 16,
`regs(` 11, `TaintFacts::{UsesData 12, UsesPointee 7, UsesAddress 8,
DefsData 3}`. About 100 sites. `TaintAnalysis.cpp` is 4,770 lines.

## The target domain (CIO's factoring, our precision)

CIO's checker (`checker_taint.ml`, 133 lines) has ONE taint bit,
`Notaint | Taint`, and keeps pointer-ness and region as SEPARATE domains in a
reduced product: `((WrappingInterval x Taint) x Type) x Bases`. "Secret
address" is not a second taint channel; it is `Taint AND Ptr` read from two
independent dimensions. We built the same information the other way - two
kinds of taint on one value - and every instruction class then needs a rule
for how the kinds interact. Target:

```
Reg  ->  Taint   (Notaint | Taint)
      x  Kind    (Scalar | Ptr(Base))          Base = Frame(FI) | Arg(k) | Global(GV) | Unknown
Mem  ->  Taint   per cell, ONE channel
```

Then two of today's channels become DERIVED facts, not propagated state:

- **pointee-tainted(R)** = `Kind(R) = Ptr(b)` and `Mem[b]` is tainted. Read
  from memory state through the base. For `Arg(k)` the callee cannot see the
  caller's memory, so `Arg(k)`'s pointee is an ABSTRACT memory object in the
  callee's memory state, seeded from the `pointee` attribute. That is what
  `TaintedArgPointees` already is; it becomes `Mem[Arg(k)]`.
- **address-tainted(R)** = `Taint(R) AND Kind(R) = Ptr(_)`. A secret VALUE
  used as a pointer. No separate bitvector.
- `PointerBases` becomes the `Base` inside `Kind`. It is not a side map that
  must agree with three bitvectors; it is one component of one join.

What this buys: the frame-provenance rules, the pointer-base rules, and the
per-channel memory sets stop being three mechanisms that must agree and become
one join in a product. The "which channel does this store feed" question that
produced today's oracle bug cannot arise in the compiler, because there is one
channel.

Join: `Taint` unions. `Kind` intersects to `Ptr(Unknown)` on disagreement
(today's `PointerBases` intersection, kept). `Mem` unions per cell.

## What NOT to do

Do not adopt CIO's coarseness. Their loads return `Taint` unconditionally,
every call havocs all memory to tainted, `make_top = Taint`, and they never
kill taint. That is why their overhead reaches 27.84x on argon2id. Our 1.5%
depends on the precision we have. **Keep the precision, fix the factoring.**

Do not touch placement (`insertTaintDITSwitches`, the region emitter, the
sub-block code guarded by `-taint-dit-sub-block`, default OFF). Phase 1 is
the analysis domain only.

Do not touch gem5-DIT. Phase 0 has uncommitted changes in
`src/cpu/o3/commit.cc` and `src/arch/arm/insts/mem64.hh` there.

## Verification protocol (the whole point)

1. **Baseline first, before any edit.** In your build, harden mbedTLS 3.6.2
   (`~/Documents/mbedtls-3.6.2`, config: set `MBEDTLS_USE_PSA_CRYPTO`,
   `MBEDTLS_PSA_CRYPTO_EXTERNAL_RNG`; unset `MBEDTLS_HAVE_ASM`,
   `MBEDTLS_HAVE_TIME_DATE`, `MBEDTLS_HAVE_TIME`, `MBEDTLS_DEBUG_C`;
   `CFLAGS=-O2 -g -march=armv8.4-a`) with
   `/home/rgangar/Documents/gem5-DIT/benchmarks/tls_resume/seed_pass3.txt`, and
   libsodium 1.0.21 (`~/Documents/libsodium-1.0.21`, `--disable-shared
   --enable-static --disable-asm`, `CFLAGS=-O2 -ftaint-harden=<seed>
   -fno-optimize-sibling-calls`) with
   `/home/rgangar/Documents/gem5-DIT/benchmarks/crypto/libsodium_secret.fixed.txt`
   (the corrected seed set; the shipped one has 12 dead lines). Record per TU:
   sha256 of every `.o`, and `-mllvm -taint-dit-precision-report=<f>` output
   (per-function `need`/`underdit`/`switches`). A worked build+report script
   is `gem5-DIT/benchmarks/tls_resume/sbsweep2/run.sh`.
2. **After the refactor, rebuild both and diff.** Target: every `.o` byte
   identical and every precision-report line identical. Any difference must
   be explained per function and must be in the safe direction (more `need`,
   never less). Lower `need` anywhere is a regression until proven otherwise.
3. **Lit.** `build/bin/llvm-lit -sv llvm/test/CodeGen/AArch64/taint-analysis-*.mir
   llvm/test/Transforms/TaintAnnotate clang/test/CodeGen/taint-*.c` must be
   green (34 pass, 13 unsupported, 1 xfail as of 2026-09-03), and the full
   `llvm/test/CodeGen/AArch64` suite must have zero failures. New tests for
   the new domain should be verified to FAIL against the pre-refactor build.
4. **Verifier.** The soundness verifier in the emitter is a hard gate; a
   refactor that breaks coverage fails the build. Keep it that way.
5. **Last, dynamic.** Once Phase 0's gem5 lands, run the oracle
   (`gem5-DIT/benchmarks/tls_resume/`, `--kex dhe`, arms null/pass3/blkt at
   `--resumptions 0` and `2`) and compare against
   `paper_experiments/10-mbedtls-session-ticket/data/oracle_store_rule_fix.csv`
   ("fixed" rows). Coverage must not drop.

## Build

Do NOT use `~/Documents/llvm-data-independent-timing/build` - it is the shared
checkout's build and other sessions depend on it. Make an isolated one in your
worktree:

```
cmake -G Ninja llvm -B build -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_ENABLE_PROJECTS='clang;lld' -DLLVM_TARGETS_TO_BUILD=AArch64 \
  -DLLVM_ENABLE_ASSERTIONS=OFF -DLLVM_USE_LINKER=lld -DLLVM_CCACHE_BUILD=ON \
  -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_CXX_COMPILER=/usr/bin/clang++
ninja -C build -j 96 clang llc opt FileCheck not count split-file llvm-ar \
  llvm-ranlib llvm-objdump llvm-nm llvm-config llvm-readobj llvm-readelf \
  llvm-dwarfdump llvm-mc llvm-cgdata llvm-lto2 llvm-profdata llvm-symbolizer
```

ccache is warm from worktree 4's build (same flags), so this is ~10 min, not
40. The tool list is exactly what the lit suites need; `llvm-lit` itself is a
cmake-generated script in `build/bin/`, not a ninja target.

Machine: `beckham`, 160-core Neoverse-N1, 497 GB. **No FEAT_DIT**, so nothing
DIT can run natively; correctness is lit + verifier + object diff, timing is
gem5. Check `uptime` / `pgrep gem5.opt` before a big build - Phase 0 may have
oracle runs going.

## Landmines (each cost real time on 2026-09-03)

- `-debug-only=` needs an assertions build. Both the shared build and worktree
  4's are Release/no-asserts. Tests using it carry `REQUIRES: asserts`.
- `pkill -f <pattern>` kills your own shell if the pattern is in your command
  line. Use `pkill -x <procname>`.
- gem5 does not accept `--version`; probe with `--help`.
- The precision/info-loss/seed reports APPEND; delete them before a build.
- In `local a=$1 b="...$a"` bash expands `$a` before assigning it.
- A seed with an argument index the function lacks used to be silently
  dropped; Phase 0 made it fatal (`TaintSourceAnnotator.cpp`). If a build
  suddenly fails with "taint seed argument index out of range", the seed was
  always dead.

## Phase 0's in-flight changes (worktree 4, uncommitted)

`llvm/lib/CodeGen/TaintAnalysis.cpp` - the sub-block emitter
(`cutSubBlockHoles`, `sinkEntryEnableTo`, `hoistExitDisableTo`, flags
`-taint-dit-sub-block` default OFF and `-taint-dit-sub-block-min-run`), all in
the EMISSION section around line 3800-4300. Phase 1 edits the STATE and
PROPAGATION sections (header struct, `propagateTaintMI` ~1217-1720,
`replayTaint`, `TaintFixedPointIteration.cpp`). Different hunks of the same
file; git will merge them. `TaintSourceAnnotator.cpp` (seed validation),
`utils/taint_seed_check.py`, three clang tests (`REQUIRES: asserts`). None of
it overlaps Phase 1.

Worktree 4 path, for reading any of the above by absolute path:
`/home/rgangar/.treehouse/llvm-data-independent-timing-7b712d/4/llvm-data-independent-timing`

## Addendum (2026-09-03, later): Phase 0 outcome the Phase 1 agent should know

`docs/results/oracle-pointer-taint-2026-09-03.md`. The oracle's denominator
shrank ~40% per resumption; the "fixed" rows in `oracle_store_rule_fix.csv` are
the baseline for step 5 above. The residual uncovered count has a floor of
secret-derived POINTER traffic from mbedTLS's leading-zero-limb trim feeding
`calloc` - genuine, address-channel, not DIT-coverable, and correctly excluded
by the compiler's address-class rule. Do not let the refactor "fix" that: a
domain change that starts protecting those pointer moves has regressed toward
the oracle's over-count, not improved.
