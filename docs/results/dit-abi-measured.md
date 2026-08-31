# The callee-saved DIT ABI, measured

> **State on 2026-08-31.** The ABI is implemented, measured, and CLOSED as a piece
> of work. `-ftaint-dit-abi` is opt-in and stays that way (§4). Nothing here changes
> shipped codegen. Full AArch64 suite: 3907 tests, 0 failures.
>
> **Soundness is a wash, measured** - the shadow-taint oracle reports identical
> protection with and without the ABI (§4.1). It is not a security improvement.
>
> **Open, in priority order.** (1) ~~No non-LTO workload with a high ratio~~ -
> ANSWERED by the libsodium f-sweep (§3.1), which also falsified the ratio theory
> itself; §3 is corrected. The open question is now narrower: real applications sit
> near 1-2% secret fraction, where the ABI measures neutral, so is there a
> *deployed* workload at a high enough call rate for it to pay?
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

## 3. What predicts the win: executed re-asserts, not the static ratio

**An earlier version of this section was wrong and is corrected here.** It claimed
the predictor is switches per instrumented function - 5.9 non-LTO against 51.1
under LTO - and predicted, in advance and in writing, that libsodium at 5.5 would
be neutral. It is not. The libsodium f-sweep (§3.1) shows the ABI worth **5.3
points** at high secret fraction, at a ratio essentially identical to Bitcoin's.

The static ratio was a proxy that happened to correlate. The mechanism is
**re-asserts EXECUTED per unit of work**, because a re-assert is paid per executed
call site, not per call site in the binary:

| workload | static ratio | dynamic character | ABI |
|---|---|---|---|
| Bitcoin signing, non-LTO | 5.9 | one call into secp256k1, much internal work | neutral |
| libsodium, f = 0.001-2% | 5.5 | few AEAD calls per period | neutral |
| libsodium, f = 25.8% | 5.5 | 16 AEAD calls per period, boundaries crossed constantly | **-5.30 pts** |
| Bitcoin, full LTO | 51.1 | whole-program merge multiplies executed call sites | **-5.4 / -8.5%** |

LTO scored high on the static ratio for the same underlying reason - merging
multiplies executed call sites per function - which is why the proxy held there
and broke on libsodium.

**To predict a new workload, ask how often control crosses an instrumented call
boundary, not how many switches the binary contains.**

### 3.1 The libsodium f-sweep

SQLite lane, ChaCha20-Poly1305 AEAD, 10 points x 9 arms x 20 reps + 3 burn-in,
rotating arm order, Apple M5. Raw data in `data/abi-libsodium-fsweep.{log,jsonl}`.
Percentages are against the `nodit` baseline.

|      f% | blanket | def30 | **abi30** | nop30 | ABI vs def30 |
|---|---|---|---|---|---|
|   0.001 |  +11.92 | -0.07 |     +0.08 | +0.17 |        +0.15 |
|   0.023 |  +11.94 | +0.33 |     +0.00 | +0.43 |        -0.33 |
|   0.729 |  +11.61 | +0.62 |     +0.05 | +0.07 |        -0.57 |
|   2.142 |  +11.49 | +0.87 |     -0.05 | -0.18 |        -0.92 |
|   8.026 |  +10.85 | +2.67 |     +0.95 | -0.32 |        -1.72 |
|  25.763 |   +8.78 | +8.64 | **+3.34** | +0.03 |    **-5.30** |

**The ABI is what makes selective placement beat blanket here.** At f = 25.8%,
`def30` costs +8.64 against blanket's +8.78 - a tie inside noise, so the shipped
placement buys nothing. `abi30` costs +3.34 and beats blanket outright.

The `nop30` control is +0.03 at that point, so essentially all of `def30`'s +8.64
is the switches themselves rather than layout. Switches fall 482 -> 278 (-42%)
while time falls 8.64 -> 3.34 (-61%), more than proportional - consistent with the
deleted re-asserts sitting on the hottest paths.

**Validity gate.** An arm that cannot establish a carrier reverts to
whole-function coverage, which is blanket for that function, and an earlier abi30
arm was withdrawn for exactly that. This one reports **zero** non-local exits.
Check `<arm>.nonlocal.txt` is empty before believing any number from an ABI arm.

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

**Recommendation, revised after the libsodium sweep: enable `-ftaint-dit-abi`
when control crosses instrumented call boundaries often** - a high secret
fraction, a high call rate into hardened code, or LTO. Leave it off otherwise.
Do not adopt LTO to get it.

The default stays off because real applications sit near 1-2% secret fraction
(`dit-real-app-vs-benchmark.md`), where this measures neutral. That is a judgement
about typical workloads, not a claim that the ABI does not help.

This is consistent with the other evidence against LTO on record: libsodium tells
users not to build with it, the hardened link is single-threaded and doubles an
already-slow build (29 min compile, 20 min link), and `dit-secp-tier2.md` finds
blanket DIT beating our placement on libsecp256k1 signing regardless.

## 4.1 Soundness: measured, and it is a WASH

The static verifier cannot answer whether the ABI is safer. It is intraprocedural
and treats calls as **transparent** by design - exactly the cross-frame property
the ABI is about - so "the verifier passes" is evidence it cannot see the question.

The gem5 shadow-taint oracle can. Run 2026-08-31 on libsecp256k1 ECDSA signing,
two arms identical except for `-ftaint-dit-abi`
(`gem5-DIT/benchmarks/taint_oracle/run_secp_abi.sh`, compiler `5c1be960d0d0`):

| arm | secret ops protected | secret ops with DIT clear | cycles |
|---|---|---|---|
| ABI off | 464,796 | 4 | 283,325 |
| ABI on | **464,796** | **4** | 284,983 |

**Protection is bit-identical.** The same four under-taint sites appear in both,
all in the driver's `main`, none inside libsecp256k1 - the same residue the
existing tier-2 run reports.

So the ABI is **exactly as sound here, neither more nor less**. It relocates
responsibility for the mode from caller to callee; it does not change which
instructions execute protected. The one genuine security argument for it is
narrower than "more secure": it stops soundness depending on the `PreservesDIT`
and `AlwaysEnteredWithDIT` summary bits being correct, since the callee now
guarantees the contract regardless. That removes two analyses from the trusted
base, and buys nothing observable on this workload.

Note the cycles: +0.585% with the ABI on. This is a non-LTO build at a
switch-per-function ratio near 6, so §3 predicts neutral-to-slightly-negative, and
that is what a third instrument shows.

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
