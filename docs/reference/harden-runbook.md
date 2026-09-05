# Hardening a library with the shipped defaults, and running it on Apple silicon

**As of 2026-09-05 the defaults are the callee contract and the DIT twins**
(`docs/design/dit-callee-contract.md`, `docs/design/dit-cloning.md`). This is
the end-to-end recipe: what to build, what to seed, how to know it worked,
which control arms to build next to it, and what to expect on an M4 or M5.
The gem5 rigs that produced the numbers in the design notes are named at the
end; on silicon the same binaries run as they are.

## 0. What the defaults do

- **Every function protects its own secrets** (`-taint-dit-contract=callee`).
  A call is never covered by the caller; a secret reaching a callee this
  build cannot see is an obligation in the info-loss report, with the seed
  line that fills it. Seeding is monotone: adding a seed never removes
  protection elsewhere.
- **A DIT-on caller calls a twin** (`-taint-dit-clone-seeded`). Every seeded
  function, and everything it reaches by direct call in its TU, has a
  `<name>.dit` copy that is entered with DIT already set and emits no switch;
  a call made from DIT-on code goes to the twin, so the callee stops
  toggling for itself and the caller stops re-asserting. Across TUs the twin
  is named on the strength of the seed file and the owned-symbols list.
- **Tail calls are off** TU-wide under `-ftaint-harden` (a tail call has no
  epilogue to clear DIT in). No extra flag is needed for that any more;
  `-fno-optimize-sibling-calls` is harmless but redundant.
- Region placement, `switch-cyc=30`, loop hoist, the call-site mod-set gate:
  unchanged.

The old behaviour is one flag away for an A/B: `-mllvm
-taint-dit-contract=inherit -mllvm -taint-dit-clone-seeded=0`.

## 1. Build the compiler

```
cmake -S llvm -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DLLVM_TARGETS_TO_BUILD=AArch64 -DLLVM_ENABLE_PROJECTS='clang;lld'
ninja -C build            # NO target list: libLTO must not go stale
```

On macOS, once per build directory, so `#include <stdio.h>` resolves:

```
printf -- '-isysroot %s\n' "$(xcrun --show-sdk-path)" > build/bin/clang.cfg
```

## 2. Seed the entry points

One file, one line per secret parameter, 0-based, `pointee` when the pointer
is public and the memory behind it is secret; `#` comments; C++ needs mangled
names. Start with the API entry points that receive the key:

```
crypto_sign_ed25519_detached,4,pointee
crypto_aead_chacha20poly1305_ietf_encrypt,8,pointee
```

Seeds are TU-scoped parameter attributes. A function that is only DECLARED
in a TU gets a stamp saying it is seeded elsewhere; nothing else crosses a TU
boundary, which is why step 4 exists.

## 3. Build the library, twice

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

Add the libc model on the second build too:

```
   -mllvm -taint-dit-preserving-symbols=$LLVM/utils/dit_preserving_libc.txt \
```

Every callee the build does not define is otherwise assumed to clear DIT, and
DIT-on code re-asserts after each one: on libsodium that is three `msr DIT`
per argon2 block after glibc's `memcpy`, 393,216 per hash. The file names the
external leaves that never write PSTATE.DIT (movers, string functions,
allocators, syscall wrappers) and the re-assert after them goes. It is
trusted only for symbols the owned list does not name, so a hardened `memcpy`
of your own is still handled as yours. Do not add anything that takes a
callback: the callback may be hardened code, which clears at its exit.

## 4. Close the seed loop

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

## 5. Check that it worked

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

## 6. The control arms

Every measurement in this repo is against these; build them from the same
source with the same seeds so only the thing under test differs.

| arm | flags | what it controls for |
|---|---|---|
| unhardened | `-O2` | the baseline; byte-identical to `-ftaint-harden=<empty seed file>` |
| **NOP** | `... -mllvm -taint-dit-nop-switches` | same placement, same layout, every switch a `HINT #0`: the instruction-matched baseline. Not neutral: a NOP costs more than a renamed `MSR DIT` |
| blanket | unhardened or NOP library + a constructor `msr DIT, #1` linked into the program (`gem5-DIT benchmarks/taint_convolve/dit_blanket.c`) | DIT everywhere, no analysis |
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

- **Apple's `MSR DIT` is the renamed kind.** The switches are close to free;
  what the hardened arm pays is DIT dwell, the predictors it turns off while
  set (the load value predictor above all). Expect the twins arm to land
  near blanket where the secret lane dominates and well under it where a
  public lane does, and expect the NOP arm to sit ABOVE the real arm. The
  serialising column of the gem5 tables has no silicon counterpart on an M4
  or M5.
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
