# The taint domain as a product lattice

Landed 2026-09-03 (Phase 1 of the three-phase plan agreed that day: Phase 0
fixes the gem5 oracle's store rule, Phase 1 refactors the analysis domain,
Phase 2 audits "unknown means tainted"). The evidence behind the design is
`docs/research/cio-taint-implementation.md`, the source-level study of CIO's
checker. This document is the design as built, the argument for the two places
it departs from the handoff's sketch, the differential verification, and the
Phase 2 audit.

**The refactor is byte-identical.** Every object of mbedTLS 3.6.2 and
libsodium 1.0.21 hardened before and after has the same sha256, and every line
of the per-function DIT precision report is identical (S6). The lit suites are
green. Nothing about placement or codegen changed; only the shape of the state
the analysis carries.

## 1. The domain

`llvm/include/llvm/CodeGen/TaintAnalysis.h`, `struct TaintState`:

| component | type | meaning | join |
|---|---|---|---|
| registers | `Reg -> TaintVal` (two `SparseBitVector`s, one per `TaintKind`) | the abstract value held | union |
| provenance | `Reg -> TaintObject` (`PointerBases`) | WHICH object a pointer targets: Frame(FI) or Arg(k) | **intersection** |
| memory | `MemCell -> TaintVal` (`Cells`) | the abstract value in a named byte range | union |
| heap | `IR Value -> TaintVal` (`UnknownMemValues`) | stores located only by IR pointer, screened by AA on load | union |
| TOP bits | `UnknownMemTainted`, `ExternalMemClobbered` | a secret went somewhere unnameable | OR |
| flags | `OutgoingArgSecret`, `NonArgSourcedTaint` | stack-argument hand-off; the mod-set source condition | OR |
| inherited | `TaintedWholeGlobals` | globals a *callee* wrote a secret into | union |

with

```
TaintVal    = (Data, Pointee)                     one bit per TaintKind
TaintObject = Frame(FI) | Global(GV) | Arg(k)     the nameable objects
MemCell     = (TaintObject, offset, size)         size 0 = unknown extent
```

**The abstract value is the same thing in a register and in a cell.** A store
deposits the stored register's whole `TaintVal` into the cell it names
(`assignCell` when the extent is known - a strong update that also clears - or
`taintCell` when it is not - a weak update under the size-0 sentinel); a load
reads the join of every overlapping cell back into its defined registers
(`readCell`). ALU results carry the join of their inputs in both dimensions.
There is one store rule and one load rule, not one per kind, which is the
factoring the handoff asked for: the "which channel does this store feed"
question that produced the oracle's store-rule bug cannot be asked of the
compiler, because a store moves a value, not a channel.

`Arg(k)` is an abstract object in the callee's memory state: the caller's
object, which the callee cannot see but can name. Its cells are whole-object
sentinels written by a callee's arg-pointee mod-set (B1) and re-exported at
exit as `WritesSecretThroughArgPointee`, one hop per call edge. That is what
`TaintedArgPointees` used to be; it is now just a cell.

`TaintKind` has two members. `TaintState::regs(K)` selects the bitvector, and
the ALU, call-result, and load-def rules iterate `{Data, Pointee}` rather than
being written out per kind.

## 2. The `Address` kind was a subset of `Data`

The old third channel, `AddressTaintedRegs` ("the value may be used as a
secret-dependent address"), is gone, and its removal changes no output. Under
every rule the analysis had:

- it was seeded nowhere (seeds set Data or Pointee);
- an ALU result got it iff `UsesData || UsesAddress`, and got Data iff
  `UsesData`;
- a load cleared it on its defs and set Data to whatever the load read;
- a call cleared both on its result registers and set only Data;
- joins were unions.

Induction over the fixed point: if `Address ⊆ Data` holds on entry to an
instruction then `UsesAddress ⇒ UsesData`, so the ALU rule sets both to the
same value; every other rule clears Address wherever it clears Data or sets Data
without Address; and a union of two states satisfying the inclusion satisfies
it. The base case is the seed, where Address is empty. So `Address ⊆ Data` was
an invariant, and each consumer that tested it - `isTaintedInstruction`'s
`IsMemAccess && (UsesAddress || UsesData)`, `classifyDITUncovered`'s
`UsesData || UsesAddress` and its store-address check on both kinds - was
testing Data twice. What the channel was *meant* to express - a secret used as
an address, which DIT does not cover - is exactly `UsesData` on a load or a
Data-tainted address operand on a store, and that is what the uncovered report
now tests directly.

## 3. Why pointee taint is a component, not derived from provenance

The handoff's sketch had one `Kind = Scalar | Ptr(Base)` per register, with
`pointee-tainted(R) := Kind(R) = Ptr(b) ∧ Mem[b] tainted` as a derived fact and
`Kind` intersecting on join. That cannot be built without changing output, and
the reason is a polarity clash worth writing down, because it will come up
again in Phase 2.

The two uses of "which object does this register point at" want opposite
approximations:

- **Pointee taint** licenses ADDING taint: a load through the register is
  secret, a call receiving it gets a pointee-tainted parameter, the instruction
  is a Need. Over-approximating it is safe; under-approximating loses the
  secret. It must be a MAY fact and union on join. Today's `PointeeTaintedRegs`
  is exactly that: "may point into the object of some seeded pointee argument",
  propagated through every ALU op and through memory (a spilled pointer keeps
  it), unioned at joins.
- **Provenance** licenses REMOVING taint: a callee's write through argument i
  is applied to the one object this register names instead of to all caller
  memory (P1b/B1), and a libc mover's destination is named instead of TOP.
  Attributing the write to the wrong object under-taints. It must be a MUST
  fact and intersect on join; a register with a base on only one path has no
  base. Today's `PointerBases` is exactly that, and it is *not* carried through
  memory or through arithmetic other than `sp+imm` and interior pointers into
  an argument.

One `Kind` with one join cannot serve both. With intersection, a register that
is pointee-tainted on two paths from two different seeded arguments would join
to `Ptr(Unknown)` and stop being pointee-tainted: an under-taint. With union,
provenance would name two objects and precise application would have to write
both, which is sound but is a new rule with new codegen, not a refactor.

The derivation also over-taints in the default configuration. A register
holding `sp + imm` into a frame object that holds a secret would become
pointee-tainted and, at a call, would mark the callee's parameter. That is the
`-taint-frame-addr-args` behaviour, measured at +44 points against the mod-set
gate in its whole-frame form and 408 -> 628 switches on libsecp256k1 in its
per-object form (`docs/design/p1b-frame-provenance.md` S4), and left OFF for
that reason. Deriving pointee taint from frame provenance would turn it on by
default.

So the product keeps the two apart, and says so at `TaintObject`'s definition.
The honest reading of the sketch's "derived pointee" is that it needs a MAY set
of objects per register (`Ptr({b1, b2, ...})`, union on join) *alongside* the
MUST base, plus a decision about whether `Frame` objects feed it. In the
default configuration, where the only objects that could feed it are the
seeded `Arg(k)`s and `Mem[Arg(k)]` is tainted from entry and never cleared,
that may-set is isomorphic to the one bit the analysis already carries. The
bit is the collapsed form; carrying the set would cost memory and join time in
the fixed point for information nothing reads. If Phase 2 decides that `Frame`
objects should feed pointee taint, that is where to add it - as a may-set - and
it will be a measured change, not a refactor.

## 4. What moved where

| before | after |
|---|---|
| `TaintedRegs`, `PointeeTaintedRegs` | unchanged (the two `TaintKind`s) |
| `AddressTaintedRegs`, `TaintKind::Address`, `TaintFacts::UsesAddress` | removed (S2) |
| `TaintState::PointerBase {Frame, Arg}` | `TaintObject {Frame, Global, Arg}`, top-level |
| `TaintedStackCells`, `PointeeTaintedStackCells` | `Cells` with `Frame(FI)` keys, `TaintVal` values |
| `TaintedGlobalCells` | `Cells` with `Global(GV)` keys (Data only, S5) |
| `TaintedArgPointees` (bitvector by arg number) | `Cells` with `Arg(k)` keys, whole-object sentinels |
| `TaintedUnknownMemValues`, `PointeeTaintedUnknownMemValues` | `UnknownMemValues: Value -> TaintVal` |
| `TaintedWholeGlobals`, `ExternalMemClobbered`, `UnknownMemTainted`, flags | unchanged |
| `set/clear/is{Pointee}Tainted{Stack,Global}Cell`, `any…ForFI/ForGV` (14 methods) | `taintCell`, `assignCell`, `readCell`, `objectHoldsSecret` |
| `anyTaintedStackCellForFI(FI)` | `frameObjectHoldsSecret(FI)` |
| `unknownMemMayTaintLoad(MMO, Set, AA)` | `unknownMemMayTaintLoad(MMO, S, Kind, AA)` |
| per-kind store block (`storeCell` called per channel) | one `storedValueTaint(MI, S)` deposited per cell |
| per-kind load flags (`ShouldTaint`, `ShouldPointeeTaint`) | one `Loaded: TaintVal` |

Report formats: `-taint-output` loses its `address_tainted_regs` column and the
`_trace` file its `address_tainted_regs` line and `address=` count. Nothing
consumed them.

## 5. Preserved as-is, and the Phase 2 audit

The refactor preserves three behaviours that a clean-slate design would not
choose, because changing them changes output and the protocol for this phase
is byte identity. Each is a candidate for a separately measured follow-up.

1. **Globals do not carry pointer-ness.** A store to a global keeps only the
   Data fact (`propagateTaintMI`, the `CellInfo::Global` store arm). A pointer
   to a secret stored in a global and reloaded comes back as a public pointer;
   the deref through it is then protected only as a secret *address* if the
   pointer value itself was secret, or not at all. Lifting it is one line
   (deposit the whole value) and a measurement.
2. **`TaintState::empty()` gates instrumentation on the join of block exits.**
   `runTaintInterproc` calls `insertTaintDITSwitches` only when
   `TR.Merged.empty()` is false, and `Merged` is the join of every block's OUT
   state. A function whose only secret is consumed and cleared before every
   block exit reads as empty even though it executes tainted instructions, and
   is never instrumented. CONFIRMED on the identity build with
   `clang/test/CodeGen/taint-instrument-gate.c`: `leak` loads a secret global
   into x0 and calls an in-TU `consume` that returns a constant; the call
   defines x0, the analysis clears it, `Merged` is empty, and `leak` gets no
   switch and no precision line while the load and the secret-passing call
   both run with DIT clear. (An EXTERNAL callee does not show it, because the
   analysis re-taints the return register of any external call that received
   a secret.) The Scenario-B check in step 3c does not fire because it asks
   `functionHasTaintedRuns`, which is true. Fixed in the follow-up commit.
3. **Provenance is not intersected at a join against a taint-free
   predecessor.** `TaintState::join` returns early when the other state
   `isBottom()`, and `isBottom()` excludes `PointerBases` because provenance
   is not taint - so a register that names Frame(A) on one path and Frame(B)
   on the other keeps Frame(A) if the Frame(B) path carries no taint yet.
   Provenance is a MUST fact and this is the under-tainting direction: a
   callee's write through that pointer is then applied precisely to A, and a
   read of B comes back public. CONFIRMED on the identity build with
   `clang/test/CodeGen/taint-provenance-join.c` (the pointer to one of two
   locals chosen by a branch, filled by a callee that reads a secret global,
   the other local read afterwards): `caller` carries no switch at all. This
   is reachable in the DEFAULT configuration - frame provenance is always on -
   whenever the secret arrives from a global rather than from a register the
   join could see. Fixed in the follow-up commit: `join` intersects
   provenance before its bottom early-return, and the block join in
   `TaintAnalysis::run` skips predecessors not yet evaluated (an unevaluated
   backedge is unknown, not "points nowhere"), which is what kept loops as
   precise as the early return had made them. Measured: mbedTLS and
   libsodium objects identical to baseline with `.comment` and debug info
   stripped, precision reports identical - the shape is real but neither
   library exhibits it.
4. **Under `-taint-arg-provenance`, `Arg(k)` cells count as this function's
   own taint** (they live in `Cells`, so `empty()` sees them) where the old
   `TaintedArgPointees` bitvector was excluded. Observable only with that
   hidden flag on, only for a function whose sole taint is a callee's write
   through one of its pointer arguments, and only as an extra export /
   instrumentation call. Both provenance tests pass unchanged.

Phase 2 is "unknown means tainted everywhere". The following is every place in
the domain where an UNKNOWN currently reads as CLEAN. Each is a deliberate
precision choice, and each is where CIO's `make_top = Taint` would differ.

| # | where | what "unknown" is | today's reading | note |
|---|---|---|---|---|
| U1 | load, unresolved MMO (`CellInfo::Unknown`) | the object being read | clean unless the address is pointee-tainted, a located unknown store may alias (AA), or a TOP bit is set | AA's `isNoAlias` is trusted; CIO returns Taint here |
| U2 | load, no MMO | everything | clean unless pointee/located store/TOP | same |
| U3 | load through a `Global` cell | pointer-ness | never Pointee (S5.1) | |
| U4 | call to an external or indirect callee passing NO secret | the callee's memory effect | no clobber | the mod-set gate's premise: a callee cannot produce a secret from public arguments. flowprobe C1/C3/C4 (`docs/results/dit-flowprobe-undertaints.md`) are the known counterexamples |
| U5 | call result | pointer-ness of the returned value | Data only, never Pointee (`taintCallResultDefs`) | flowprobe C1: a returned pointer into a secret buffer |
| U6 | call argument that is `sp + imm` into a secret frame object | whether the callee reads the secret | not passed unless `-taint-frame-addr-args` | the measured +44-point decision (S3) |
| U7 | call argument whose provenance is `Arg(k)` with a tainted `Arg(k)` cell | same | not passed unless `-taint-arg-pointee-args` | B2 |
| U8 | `PointerBases` absent at a P1b application | which caller object a callee wrote | falls back to `ExternalMemClobbered` (TOP) | conservative, not clean |
| U9 | `INLINEASM` | a store through inline asm | no MMO, not `isCall()`: invisible | flowprobe C3 |
| U10 | NEON register tuples (`$q0_q1`) | alias propagation | `isSinglePhysReg` rejects tuples | flowprobe C4 |
| U11 | caller-saved registers across a call | whether the callee clobbered them | taint kept (only result registers are cleared) | conservative, not clean |
| U12 | `getCellFromMMO` phi/select disagreement | which of several objects | `Unknown` -> U1 | the loop-carried-pointer fix accepts only unanimous answers |
| U13 | a load from a global that is NOT in `ModuleSecretGlobals` and has no tainted cell | whether some other TU wrote it | clean | cross-TU is out of scope by design |

U1, U2, U4 and U5 are the ones that matter for Phase 2, and U4 is the one
with a measured cost attached (`-taint-no-modset-gate`: +51.20% on Bitcoin
Core's `ConnectBlockAllEcdsa` when every call site clobbers).

## 6. Verification

Protocol from the handoff, executed in this order.

1. **Baseline first.** A fresh Release/no-asserts build of the pre-refactor
   tree (worktree 5, `build/`) hardened mbedTLS 3.6.2 (108 objects, seed
   `seed_pass3.txt`, 915 `msr DIT`) and libsodium 1.0.21 (129 objects,
   `libsodium_secret.fixed.txt`, 150 `msr DIT`), recording the sha256 of every
   `.o` and the per-TU `-taint-dit-precision-report`.
2. **After the refactor, rebuild both and diff.** Every `.o` sha256 identical
   (108/108 and 129/129); every precision-report line identical (121 and 39
   lines). No per-function explanation was needed because there was no
   difference. One trap for whoever repeats this: mbedTLS is built with `-g`,
   so its objects embed the build directory in DWARF - a rebuild in a
   differently named work directory differs in every object, tainted or not,
   while the same objects stripped of `.debug_*` are identical. Rebuild at the
   SAME path (the scripts in this session's scratchpad take the work dir as an
   argument for that reason), or strip before hashing.
3. **Lit.** The taint suites and the full `llvm/test/CodeGen/AArch64` suite -
   results in the landing commit message. The `-debug-only` tests were run on
   an assertions build as well, since a Release build reports them as
   unsupported.
4. **Verifier.** Untouched; the emitter's soundness verifier still gates every
   region placement.
5. **Dynamic** (gem5 oracle, `tls_resume`, `--kex dhe`): deferred until Phase
   0's oracle store-rule fix lands, as the handoff specifies. Byte-identical
   objects make it a formality for this phase.

New tests for a byte-identical refactor cannot fail against the pre-refactor
build by construction, and none are claimed to. The existing 48 tests pin the
propagation rules through their debug output and codegen; they are the
regression suite for the new shape.
