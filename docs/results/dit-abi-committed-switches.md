# Committed PSTATE.DIT switches: does the callee-saved ABI actually execute fewer?

**Measured 2026-08-31, gem5 (Neoverse V2 FDP config), libsodium 1.0.21 and
libsecp256k1.**

Every ABI number before this was a **static** count - `MSR DIT` instructions in
the binary - plus wall-clock. Static counts do not say how often a switch
*executes*, and the corrected predictor in `dit-abi-measured.md` §3 is explicitly
dynamic ("re-asserts executed per unit of work"). This closes that gap by counting
committed DIT accesses inside the simulator.

The question it answers: *the ABI moves a mode write from the caller's call site
to the callee's prologue/epilogue - dynamically, does anything actually go away?*

**Yes, 2.4x on the one workload with real switch traffic - and it still costs time
unless the switch is serializing.**

---

## 1. The instrument

Four counters in the O3 commit stage (`src/cpu/o3/commit.cc`,
`Commit::countDitAccess`), exported as gem5 stats:

| stat | instruction |
|---|---|
| `commit.ditSetImm` | `msr DIT, #1` |
| `commit.ditClearImm` | `msr DIT, #0` |
| `commit.ditWriteReg` | `msr DIT, Xt` - the ABI's exact restore |
| `commit.ditRead` | `mrs Xt, DIT` - the ABI's carrier save |
| `commit.ditWrites` | formula: every mode-changing write |

Counted **at commit**, so these are executed switches on the real path, not
speculative ones and not the static count.

**Matched on the raw encoding, not the StaticInst class.** MSR DIT is built by
four different opcodes depending on `--speculative-dit`
(`MsrImmDitSet64`/`MsrImmDitClr64`/`MsrRegDit64` vs the serializing
`MsrImm64`/`Msr64`). A count keyed on the class would change meaning with the
switch model, which is the exact axis this study sweeps.

```
msr DIT, #imm    d503405f | (imm << 8)     CRm is the only variable field
msr DIT, Xt      d51b42a0 | Rt             S3_3_C4_C2_5, write
mrs Xt, DIT      d53b42a0 | Rt             S3_3_C4_C2_5, read
```

---

## 2. libsodium AEAD - the only primitive with switch traffic

ROI = the driver's iteration loop (`m5_reset_stats` .. `m5_dump_reset_stats`),
228,001 committed instructions. `taint` is the shipped default (region placement,
`switch-cyc=30`); `abi` is the same plus `-ftaint-dit-abi`.

| switch model | arm | committed writes | reads | cycles | vs base |
|---|---|---|---|---|---|
| renamed | base | 0 | 0 | 113,173 | - |
| renamed | `taint` | **460** | 0 | 112,903 | -0.24% |
| renamed | `abi` | **190** | 130 | 114,933 | **+1.56%** |
| renamed | blanket | 0 | 0 | 112,653 | -0.46% |
| serializing | `taint` | **460** | 0 | 123,694 | **+9.30%** |
| serializing | `abi` | **190** | 130 | 119,918 | **+5.96%** |

**The trade is real and now directly observed:** committed writes fall
460 -> 190 (2.4x) and 130 carrier reads appear where there were none. That is
precisely the caller-saved-to-callee-saved exchange, executing.

**Whether it pays is decided entirely by what a write costs:**

- **Serializing** - the ABI recovers **3.34 percentage points** (9.30% -> 5.96%).
  270 removed serializing writes are worth more than 130 reads plus the carrier's
  extra instructions.
- **Renamed** - the ABI **loses 1.80 points** (-0.24% -> +1.56%). With a cheap
  write there is nothing to recover, and the carrier is pure added cost.

So the ABI is not a free win that the shipped default is leaving on the table. On
renamed hardware it is a net negative on this workload, which is an independent
reason for the default staying off beyond the LTO argument in
`dit-abi-measured.md` §4.

### 2.1 The ABI arm is not leaking DIT

A cheaper arm would be worthless if it were cheap because it stopped clearing -
which is exactly how the earlier `abi30` libsodium arm was invalidated
(`-mllvm -taint-dit-abi` without the tail-call disable, so it degenerated to
blanket). This arm uses the driver flag, and three things say it is clean:

- **No `DITLEAK return` warnings** in the build log, for either arm. That case
  warns on stderr, so silence is evidence.
- **The committed counts balance at the top level.** The AEAD driver runs
  `iter=10`, and the ABI arm commits exactly **10 clears** - one per call.
  A leaking arm shows zero. The other ~170 enables are inner re-enables inside an
  already-on region, whose guarded clears correctly branch over.
- **The static shape is right**: 26 carrier reads, 25 guarded exits, 1 exact
  register restore - i.e. 96% of exits take the cheap form, as designed.

Not fully checked: the per-TU **tail-call** sites. `-taint-callsite-report` takes
one path and each TU truncates it, so a whole-library enumeration needs a
per-TU path scheme that does not exist yet.

### 2.2 Dynamic reduction exceeds static reduction

Static `msr DIT` in the library: 120 (`taint`) -> 69 (`abi`), **1.74x**.
Committed in the ROI: 460 -> 190, **2.4x**. The switches the ABI removes are
hotter than average, so the static count *understates* what it does - the
opposite of the usual direction, and a reminder that neither number substitutes
for the other.

---

## 3. Everything else has no switch traffic to remove

| workload | ROI insts | writes, `taint` | writes, `abi` | `taint` vs base | `abi` vs base |
|---|---|---|---|---|---|
| ed25519 sign | 211,063 | **1** | 2 | +4.61% | +4.53% |
| x25519 | 439,910 | 0 | 0 | +0.11% | +0.42% |
| sha512 | 256,746 | 0 | 0 | -0.87% | -1.15% |
| salsa20 | 126,690 | 0 | 0 | -1.06% | -0.58% |
| hmac_sha512 | 349,166 | 0 | 0 | -0.08% | -0.06% |

**ed25519 is the sharpest result in the table.** It pays **+4.61%** while
committing **one** DIT write in the whole region, and the figure is identical
under both switch models (+4.61% renamed, +4.61% serializing). Whatever that
4.6% is, it is not switches - it is the codegen difference of going through the
taint flow, which corroborates the NOP-control finding in `CLAUDE.md` from a
completely different direction. No ABI, no placement policy and no cheaper switch
can touch it.

The four zero-write rows are primitives the seed file does not reach; they are a
negative control showing the counter reports zero when nothing is instrumented,
and that the arms are otherwise within +/-1% noise of each other.

### 3.1 Blanket DIT is free on five of six, and the pass loses to it

`blanket` is the unhardened library plus `msr DIT, #1` in a constructor, never
cleared. `compSimplifier.ditSuppressed` confirms the mode really is on in that arm
(base suppresses 0); `taint`/`abi`/`blanket` cover the same work.

| primitive | blanket vs base | `ditSuppressed`, blanket | `ditSuppressed`, `taint` |
|---|---|---|---|
| aead | **-0.46%** | 20,252 | 20,267 |
| ed25519 | **+6.88%** | 64,277 | 64,581 |
| x25519 | -1.58% | 222,016 | **0** |
| sha512 | -1.74% | 4 | 0 |
| salsa20 | +0.09% | 0 | 0 |
| hmac_sha512 | -1.27% | 0 | 0 |

**Blanket costs nothing on five of six primitives and is faster than base on four.**
The gated optimizations were not paying on this code in the first place - on AEAD,
`compSimplifier.simplified` is **0** in the base arm out of 20,009 candidates - so
suppressing them is free. Only ed25519 has anything to lose, at +6.88%.

Put that next to the AEAD costs at matched coverage (within 0.7%):

| arm | `ditSuppressed` | renamed | serializing |
|---|---|---|---|
| blanket | 20,252 | **-0.46%** | **-0.46%** |
| `taint` (shipped) | 20,267 | -0.24% | **+9.30%** |
| `taint` + ABI | 20,131 | +1.56% | +5.96% |

**On serializing hardware blanket beats the shipped placement by 9.76 points while
protecting the same instructions.** The ABI's 3.34-point recovery is therefore not
profit - it claws back a third of a self-inflicted deficit and still lands 6.4
points behind doing nothing clever.

On four of six primitives the pass suppresses **zero** - the CIO-parity seed set
never reaches x25519, sha512, salsa20 or hmac_sha512 - while blanket covers them at
zero or negative cost. That is a seed-coverage gap, not a pass bug, but it means the
pass delivers no protection there where blanket delivers full protection for free.

### 3.2 ed25519 is the one real win, and the round-trip control confirms it

ed25519 is the only primitive where selective placement beats blanket: **+4.61% vs
+6.88%**, at equal-or-better coverage (64,581 vs 64,277 suppressed). 2.27 points.

That gap was initially suspect, because `blanket` links the *unhardened* library
while `taint` links the hardened one, so it could have been the MIR round-trip
codegen lottery (`CLAUDE.md`: +0.58% / +0.06% / +2.65%). An `rt` arm was added to
settle it - `-ftaint-harden=<empty seed file>`, full flow, zero switches:

| prim | base | rt | delta |
|---|---|---|---|
| aead | 113,173 | 113,173 | **0** |
| ed25519 | 88,344 | 88,344 | **0** |

Zero - and not by luck. **The clang path no longer round-trips MIR at all**
(`4fb7600db532`, 2026-08-30): `RunTaintHardenCodegen` now runs the taint pass as a
module pass after PEI instead of serializing to MIR text and reparsing. The `rt`
binaries are **byte-identical** to base, archive included, and so are the secp
rig's own `secp_gem5_rt` and `secp_gem5_nodit`.

So there is no round-trip term to subtract from any clang-built arm, and ed25519's
2.27 points is real DIT cost. `utils/taint_harden_c.sh` and hand-driven `llc` flows
still round-trip, so the lottery still applies there.

What the counters do *not* explain is the mechanism: both arms gate nearly the same
work (`ditTaggedSet` 442,272 vs 445,858), so the dwell should be near-identical. The
3,586-instruction difference is code *between* the crypto calls, which `taint` leaves
un-gated - and public scaffolding is exactly where the gated optimizations pay
(`dit-finegrain-win-condition`). Consistent with the counters, not established by
them.

---

## 4. libsecp256k1 ECDSA sign

Measured as a **slope** (rounds 1 -> 9, differenced) because this driver has no
ROI markers, so process startup would otherwise be included. ~265,000 committed
instructions per signature.

| build | arm | writes/sig | reads/sig | cycles/sig |
|---|---|---|---|---|
| per-TU | baseline | 24 | 0 | 105,256 |
| per-TU | ABI | 22 | 7 | 106,414 |
| per-TU | no-DIT control | 0 | 0 | 102,816 |
| whole-program | baseline | 22 | 0 | 104,742 |
| whole-program | ABI | 18 | 6 | 105,326 |

**20 static write sites produce 24 executed writes per signature.** Essentially
1:1 - the switches are not inside loops, so there is no dynamic amplification and
nothing for the ABI to recover. It removes 2 writes and adds 7 reads, for +1.1%.

The "whole-program" rows merge all four TUs with `llvm-link` before a single
backend invocation. It is **not** an LTO build: the cross driver links with
Homebrew's `ld.lld`, so `-flto` would run codegen inside a different LLVM with no
taint pass - the build succeeds and emits **zero** DIT instructions. Our tree does
not build `lld`. The merge reproduces the property under test (one module, taint
crosses TU boundaries) but not LTO's cross-module inlining or internalization.

---

## 5. What is still not measured

**The regime where the ABI actually won is not reproducible on this rig.** The
8.26x static reduction and the -5.40%/-8.52% in `dit-abi-measured.md` are full
Bitcoin Core under LTO on M5: 116,611 static sets over 2,498 instrumented
functions, ~46.7 sets per function. Nothing here reaches that density - the
highest is AEAD at 460 committed writes per 228,001 instructions, one per 496.

So the claim that 46.7-vs-2.6 sets per function is a faithful *dynamic* ratio
remains **inferred**, not measured. Getting it would need a static aarch64-linux
LTO cross-build of Bitcoin Core, which the gem5 bitcoin rig (a reduced harness
over `btc_sign_gem5.c` / `btc_coinsel_gem5.cpp` plus stubs) does not provide.

What this study does establish is the mechanism and its price:

> The ABI trades **one write per executed call site** for **one read plus a
> usually-branched-over write per invocation**. Measured, that is 460 -> 190
> writes and 0 -> 130 reads. It buys time only when a write is expensive enough
> to outweigh the carrier, which on gem5 means the serializing switch model.

The predictor in `dit-abi-measured.md` §3 should therefore read: **re-asserts
executed per unit of work, times the cost of a switch, net of the carrier's added
instructions** - not the executed count alone.

---

## 6. Method notes

- `--eves --dmp --comp-simp` on `configs/example/arm/fdp_neoverse_v2_binary.py`,
  matching the other DIT studies. `--no-speculative-dit` selects the serializing
  switch model.
- **Remove the gem5 output directory between runs.** gem5 appends to an existing
  `stats.txt`, so a leftover directory from a failed run turns dump #1 into
  someone else's data. This produced a spurious "abi runs 5x the instructions"
  result before it was caught by the identical checksums and the dump count.
- All arms verified to print the same workload checksum, and every reported row
  verified to have exactly 2 stats dumps (ROI + teardown).
- All libsodium arms rebuilt on the current toolchain. `taintfn` is **excluded**:
  its library tree predates the toolchain snapshot and was not rebuilt.
- `argon2id` is excluded - gem5 aborts on that binary, unrelated to DIT.
- Binaries are copied to a fixed path (`/tmp/dit_arm_bench`) before every run:
  `argv[0]` length shifts the initial stack and fakes sub-1% effects
  (`dit-gem5-rig-traps` #5).
