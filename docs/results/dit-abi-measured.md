# The callee-saved DIT ABI, measured

> **State on 2026-08-31.** The ABI is implemented, measured, and CLOSED as a piece
> of work. `-ftaint-dit-abi` is opt-in and stays that way (§4). Nothing here changes
> shipped codegen. Full AArch64 suite: 3907 tests, 0 failures.
>
> **Open, in priority order.** (1) Nobody has measured the ABI on a workload with a
> high switch-per-function ratio that is NOT LTO - §3 predicts that is where it
> would pay off in a shippable configuration, and no such workload has been tried.
> (2) The four `DIT left set at a plain return` findings on the LTO baseline are a
> precision bug, over-protection not exposure, uninvestigated; they are the same
> four double-returning functions the verifier flagged. (3) `unwind` detection in
> the NONLOCAL report is written but untested.


**Measured 2026-08-31 on Apple M5, Bitcoin Core `bench_bitcoin`.** Compiler pinned
at `bc6404a4a773` via a toolchain snapshot (§5). Region placement throughout - the
shipped default - not `-taint-dit-placement=function`.

**Conclusion: the ABI is a large win under LTO and neutral without it, and the
default stays OFF.** Reasoning in §4.

---

## 1. Switch counts

| arm | switches | enables | clears | reg-form restores | carrier reads |
|---|---|---|---|---|---|
| non-LTO baseline | 95 | 76 | 19 | 0 | 0 |
| non-LTO + ABI | **57** | 28 | 21 | 8 | 16 |
| full-LTO baseline | 127,744 | 116,611 | 11,133 | 0 | 0 |
| full-LTO + ABI | **15,462** | 6,374 | 8,940 | 148 | 2,498 |

Non-LTO **-40%**. Full LTO **-87.9%, an 8.26x reduction.**

The sound build lands within 1.2% of the UNSOUND upper bound measured on
2026-08-30 (15,272), which deleted every re-assert and paid no callee-side cost at
all. Under LTO the carrier is nearly free in switch terms: the entry read is `MRS`,
not a mode switch, and the guarded clear replaces the exit clear one for one.

## 2. Timing

25 reps (CoinSelection) / 30 reps (SignTransactionECDSA), interleaved with rotating
arm order, exclusive machine. Raw data in `data/abi-{coinsel,sign}-timing.csv`.

| comparison | CoinSelection | SignTransactionECDSA |
|---|---|---|
| non-LTO: ABI vs baseline | +0.08% (11/25) | -0.05% (15/30) |
| **LTO: ABI vs baseline** | **-5.40% (25/25)** | **-8.52% (27/30)** |
| LTO baseline vs non-LTO baseline | +26.32% (0/25) | +19.84% (0/30) |
| LTO+ABI vs non-LTO baseline | +19.50% (0/25) | +9.08% (2/30) |

Noise: 0.3-0.4% on CoinSelection, 4.1-4.9% on signing. The LTO wins are unanimous
or near-unanimous and far outside it; the non-LTO rows are coin flips.

## 3. Why LTO wins and non-LTO does not

**The ABI trades a per-CALL-SITE cost for a per-FUNCTION cost.** It deletes the
after-call re-asserts, which scale with call sites, and adds a carrier read, a
frame slot, and an exit restore, which scale with functions. So it wins exactly
when switches-per-instrumented-function is high:

| | switches | instrumented functions | ratio |
|---|---|---|---|
| non-LTO | 95 | 16 | **5.9** |
| full LTO | 127,744 | 2,498 | **51.1** |

At 5.9 the carrier costs back what the deleted re-asserts save, which is exactly
what the non-LTO timing shows. At 51.1 the deletion dominates by an order of
magnitude. **That ratio, not the workload, is the predictor.**

## 4. The decision: default stays OFF

The criterion was set before the numbers were seen: flipping the default requires
a large switch reduction AND a timing win outside noise on a benchmark that
actually executes instrumented code. LTO satisfies both. Non-LTO satisfies only
the first.

The default nevertheless stays off, for a reason the per-arm deltas hide:

**LTO with the ABI is still slower than non-LTO without it** - +19.50% on
CoinSelection and +9.08% on signing. The ABI recovers roughly a quarter to a half
of LTO's penalty and never closes it. So the configuration where the ABI helps is
one nobody should be choosing on performance grounds, and the configuration people
do choose gets no measurable benefit.

Turning it on by default would also impose real costs on non-LTO builds for
nothing measurable: shrink wrapping disabled, tail calls disabled TU-wide (which
also disables tail-recursion elimination, so a tail-recursive function gets O(n)
frames), and a frame slot in every function of a hardened module.

**Recommendation: use `-ftaint-dit-abi` when you are building with LTO anyway.**
Do not enable it otherwise, and do not adopt LTO to get it.

This is consistent with the other evidence against LTO on record: libsodium tells
users not to build with it, the hardened link is single-threaded and doubles an
already-slow build (29 min compile, 20 min link), and `dit-secp-tier2.md` finds
blanket DIT beating our placement on libsecp256k1 signing regardless.

## 5. Method notes

**A pinned toolchain snapshot was necessary.** Three separate LTO runs were
invalidated by rebuilding `build/bin/clang` or `build/lib/libLTO.dylib` while a
measurement was in flight. The compiler is now copied to
`~/Documents/bitcoin/.toolchain-snapshot` (binaries, `libLTO.dylib`, and clang's
resource directory) and cmake points at the copy, so tree rebuilds cannot reach a
run in progress. The run logs the pinned commit.

**Three build traps, each of which cost a ~50 minute run:**

- `ninja -C build clang llc` leaves `libLTO.dylib` stale. The LTO link then runs
  the OLD analysis with no error. Build everything, or name `LTO` explicitly.
- The LTO build needs `llvm-ar`/`llvm-ranlib`; `/usr/bin/ar` does not index
  bitcode members and the link dies in undefined symbols after the full compile.
  The NON-LTO build wants the system archiver - do not copy its settings.
- `-ftaint-dit-abi` reaches LTO codegen as a module flag, because codegen runs
  inside libLTO where clang's CodeGenOptions do not exist. A hardened LTO binary
  with zero `mrs DIT` means that channel is broken; the build still pays the
  tail-call disable and gets no carrier.

**Both baselines reproduce their historical figures** - 95 and 127,740/127,744 -
which is what makes the deltas trustworthy. The +4 on the LTO baseline is from the
merge of `origin/dit-tainter` (stack-argument seeding), not from the ABI.

See `docs/reference/dit-abi-runbook.md` to reproduce.
