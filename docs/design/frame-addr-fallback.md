# The frame-address fallback (`-taint-frame-addr-args`)

> **STATUS 2026-08-24: THE FLAG IS GONE.** `-taint-frame-addr-args` was deleted. It
> reasoned about whole frames rather than objects, so once the call-site mod-set gate
> existed nearly every call site looked secret-passing and the gate stopped firing:
> `ConnectBlockAllEcdsa` measured +45.32% with both against +0.66% with the gate alone,
> 15/15 reps. P1b (`p1b-frame-provenance.md`) is the per-object replacement and does NOT
> rescue this fallback (both got worse). The under-taint it targeted — passing
> `&local_secret` into a callee — is REAL and now OPEN; see the KNOWN GAP comment in
> `TaintFixedPointIteration.cpp`. This document is kept as the record of what the
> whole-frame approach cost.

> Numbers below are the **re-measurement of 2026-07-27, after** the two soundness bugs
> in `docs/design/spill-soundness-bugs.md` were fixed. (The pre-fix run gave 49% -> 84% recall
> at 112 -> 287 functions — qualitatively identical, so those bugs did not drive this
> result. The only table still holding pre-fix numbers is the alloca one, labelled.)

**Status:** prototype, **default OFF**. Measured 2026-07-27 on libsodium 1.0.21 (M4).
Closes a soundness gap; the question the measurement answers is what it costs.

## The gap

The analysis runs **post-prologepilog**, so a local buffer's address is just
`$sp + imm`: no FrameIndex, no memory operand, nothing tying the register to the
stack cell it points at. Register taint and memory (cell) taint are therefore two
separate universes joined by exactly one bridge — pointee taint seeded on a pointer
*argument*, which survives pointer arithmetic. **Taking the address of a local is not
on that bridge.**

Consequence: `f(&local_secret)` transfers nothing, and the callee is analyzed as
**clean**. Traced instance (`crypto_sign/ed25519/ref10/sign.c:70-76`):

```
$x0 = ADDXri $sp, 360        clean   (&hs)
$x1 = ADDXri $sp, 232        clean   (&nonce)   <-- $x1 WAS pointee-tainted;
BL @crypto_hash_sha512_final clean               the ADDXri killed it
...
$x0 = ADDXri $sp, 8          clean   (&R)
$x1 = ADDXri $sp, 232        clean   (&nonce)
BL @ge25519_scalarmult_base  clean   <-- SECRET nonce, no tainted argument
```

`ge25519_scalarmult_base` multiplies the secret nonce with **DIT off**. The taint is
correctly recorded — loads after `sha512_final` do come back TAINTED via
`ExternalMemClobbered` — but no register carries "points at it".

This violates the project's own invariant (`CLAUDE.md`): *any "can't classify" path
must over-approximate*. The store-payload hook returns `std::nullopt` when it cannot
classify; this path instead returned a confident **clean** for a case it had no
information about.

## The fix

`TaintKind::FrameAddr` — **provenance, not taint**, tracked in the same
subreg/superreg-aware register machinery:

- **Seed:** a def of an instruction that reads SP/FP (`anyFrameBaseUse`, via generic
  `TRI->getFrameRegister` / `getStackPointerRegisterToSaveRestore`, not a hard-coded
  register number).
- **Propagate:** through address arithmetic and copies; cleared by any other
  computation, by loads, and on call result defs.
- **Consume — at call boundaries ONLY:** an argument register that is a frame address
  is treated as pointee-tainted iff `frameMayHoldSecret()` (any tainted stack cell, or
  `ExternalMemClobbered`). Feeds `taintedCallArguments`, `propagateArgTaintToCallees`,
  and `TaintFacts.UsesPointee` so the call becomes a DIT Need.

**Why consume only at calls:** setting pointee taint at the `ADDXri` itself would make
every subsequent load through that pointer secret, destroying cell-level stack
precision — for a problem that only exists at the caller→callee transfer.

Excluded from `empty()` / `countRegs()`: a function holding only frame addresses is
not tainted and must not become instrumented on that basis.

## Measured cost (libsodium 1.0.21, CIO's 21 seed functions, pointee-typed)

| Metric | OFF | ON | Δ |
|---|---|---|---|
| Functions instrumented | 109 / 932 | 286 / 932 | 2.6× |
| Tainted instructions | 2,958 | 26,912 | **9.1×** |
| `msr DIT` switches | 711 | 2,447 | 3.4× |
| DIT regions | 524 | 2,111 | 4.0× |
| ESCAPE / UNCOVERED / CLOBBER | 35 / 203 / 618 | 107 / 835 / 1,361 | ~3× |
| `__text` (baseline 257,040) | 259,980 (+1.14%) | 267,152 (**+3.94%**) | — |
| Analysis wall-clock | ~7 min | ~30 min | ~4× |

**Recall against the CIO artifact's own alert set** (116 of their subroutines exist in
our module; `cio_vs_ours.txt`):

| | matched | recall | instrumented but NOT in CIO's set |
|---|---|---|---|
| OFF | 56 / 116 | 48% | 63 |
| **ON** | **97 / 116** | **84%** | 199 |

Note the taint-volume ratio got *worse* after the bug fixes (6.1× -> 9.1×) even though
both absolute numbers are cleaner. The fallback's own total barely moved
(26,964 -> 26,912) while the OFF baseline dropped 33%: under the fallback the taint is
**saturated** — dominated by whole-frame poisoning, not by the implicit-def artifact.
That is the clearest single argument that the fallback needs per-object precision (P1b)
rather than tuning.

All previously-missing gap targets are now covered: `ge25519_scalarmult_base`,
`sc25519_reduce`, `fe25519_invert`, `ge25519_madd`, `crypto_hash_sha512_final`,
`blake2b_compress_ref`, `blake2b_update`, `argon2_fill_segment_ref`,
`crypto_verify_32`, `SHA256_Transform`.

### Is the extra coverage real or noise?

177 functions are newly instrumented. By area: **67 ed25519/curve25519, 36 hash,
35 aead/stream cipher, 13 pwhash/scrypt**, 10 kdf/kx/box/sign API, 10 other, and only
**6 utils/alloc/random** (`sodium_init`, `_sodium_alloc_init`,
`randombytes_internal_random*`, `sodium_add`, `sodium_is_zero`). The growth is
overwhelmingly in real crypto internals — the shape expected from correctly following a
secret buffer into a primitive — not in unrelated support code. So the 2.6× function
count is far better targeted than the raw ratio suggests; it is the 9.1× *instruction*
volume, driven by whole-frame poisoning, that is the real cost.

The **19 CIO reference functions still unmatched** are almost entirely *their* false
positives, which is the reassuring direction: all six `*_pick_best_implementation`,
`sodium_init`, `sodium_misuse`, `sodium_mlock`, `sodium_crit_enter`, `randombytes_stir`,
`randombytes_init_if_needed`, `sodium_set_misuse_handler`, `sodium_base642bin`,
`crypto_aead_aes256gcm_is_available`, `stream_avx2` (x86-only), plus the constant-setting
`crypto_hash_sha{256,512}_init` / `blake2b_init_salt_personal`. Their blunt memory
domain (every unresolvable load returns TOP, and **TOP = Taint**) taints these; we do
not, and should not. The one genuine miss worth a look is `crypto_stream_chacha20_ietf`.

## Known residuals (this does NOT make the analysis sound)

1. **A reloaded spilled pointer clears the frame-address fact**, so the fallback
   under-fires there. Smaller instance of the same gap.
2. Frame-address provenance is **not** tracked through memory (store `&local`, reload
   it, pass it).
3. `frameMayHoldSecret()` is whole-frame: it cannot distinguish *which* local the
   address points at.

## CORRECTION (2026-07-27): the frame is far more trackable than the above implies

An earlier version of this doc said per-object precision would mean "reconstructing what
prologepilog erased." **That is wrong.** PEI *computes* the frame layout and
`MachineFrameInfo` retains it — the MIR still carries the objects with their source
names:

```
- { id: 2, name: az,    offset: -360, size: 64  }
- { id: 3, name: nonce, offset: -424, size: 64  }
- { id: 5, name: R,     offset: -648, size: 160 }
```

Only the *instruction operand* lost the FrameIndex (`ADDXri $sp, 232` rather than
`ADDXri %stack.3, 0`). Both directions of the map already exist in-tree:
`MachineFrameInfo::getObjectAllocation(FI)` → `AllocaInst`, and
`TargetFrameLowering::getFrameIndexReference(MF, FI, &FrameReg)` → (base reg, offset).
Resolving `$sp+232` to `nonce` is a table lookup.

**Worse, we do not track user locals as stack cells at all.** `getCellFromMMO` builds a
Stack cell only from a `FixedStackPseudoSourceValue` — i.e. **spill slots**. In
`_crypto_sign_ed25519_detached` the 12 `%stack.N` MMOs are all spills; the real buffers
are accessed via `%ir.az` / `%ir.add.ptr6` / `%ir.arrayidx2.i`, whose underlying object
is an `AllocaInst`, for which there is no case — so they fall through to **Unknown**.
The "cell-level stack precision" the design claims covers spills, not the buffers
secrets actually live in.

So option 2 is cheaper than stated, and splits into two pieces that must land together:

- **(a) Alloca-keyed cells + offset resolution.** Add a `CellInfo::Alloca` case, key by
  FI via `getObjectAllocation`, and resolve `$sp/$fp + imm` against the layout so a frame
  address names an object.
- **(b) P1b: precise mod-set application.** Map a callee's
  `WritesSecretThroughArgPointee{i}` back to the object the caller actually passed,
  instead of `setExternalMemClobbered()`. Without this the `nonce` taint stays a
  whole-frame poison and (a) has nothing per-object to consult.

### Result of (a), measured 2026-07-27: correct, but nearly inert at -O2

The alloca case was implemented (`getCellFromMMO` now resolves an `AllocaInst` underlying
object to its frame object via `findFrameIndexForAlloca`, with a constant-offset check
that drops to whole-object under a variable index rather than risk a half-matching key).
It fixed the limitation `taint-analysis-memory.mir` documented in its own comment — that
test's local now tracks as `stack cell FI=0 off=0 sz=4` instead of falling into the
unknown-memory set.

**On libsodium it changed almost nothing**, contradicting the prediction that it would cut
the TOP flood (numbers below are PRE-bug-fix; the comparison is still valid because both
columns were measured on the same build):

| | before | after |
|---|---|---|
| Instrumented functions | 112 | 112 |
| Tainted instructions | 4,397 | 4,354 |
| `modset-top` CLOBBER lines | 571 | **571** |
| `__text` | 259,996 | 259,992 |

**Why:** at -O2, after inlining, the surviving functions operate on buffers owned by their
*callers*. The MMO underlying objects are overwhelmingly pointer **Arguments**
(`%ir.state`, `%ir.out`, `%ir.k`, `%ir.rkeys` — already handled by `CellInfo::Arg`) or
CodeGenPrepare-sunk addresses (`%ir.sunkaddr`, provenance destroyed). Own-frame allocas
are mostly promoted to registers. In the ed25519 case the caller never stores to `nonce`
at all — it passes `&nonce`, and `crypto_hash_sha512_final` writes it through its `out`
parameter.

**So the lever is (b), not (a).** The `nonce` taint arrives as that callee's
`WritesSecretThroughArgPointee{1}`, which P1a applies bluntly as a whole-frame
`ExternalMemClobbered`. P1b — mapping callee argument *i* back to the object the caller
actually passed — is both the real precision win and the precondition for making the
frame-address fallback per-object. Keep (a): it is correct, costs nothing measurable, and
matters on less-inlined code. Do not expect it to move the numbers on optimized crypto.

Residual "not always" cases that still degrade to whole-object or whole-frame: dynamic
indices / VLAs / dynamic alloca; pointers that escape into a callee and are reloaded
(needs real points-to); and stack coloring merging disjoint-lifetime locals into one
slot (safe for taint, but "which local" is then ill-defined).

**Expected payoff, honestly:** in `_crypto_sign_ed25519_detached` the entire frame
(`hs`, `az`, `nonce`, `hram`, `R`) is secret-derived, so whole-frame is already about
right and per-object recording buys little *there*. The win is in functions mixing
secret and public locals, and on the **address** side — firing the fallback only for
addresses that point at a secret object rather than for any frame address in a dirty
frame.

## Open question the numbers cannot answer

The static cost is modest (+3.88% code size, still far under CIO's +62%/+208%/+266%).
The **dwell** cost is not readable from these numbers: 2,411 toggles × ~30 cyc plus
substantially more time under DIT. That needs the runtime harness
(`docs/results/dit-cost-model.md`), and it is the number that should decide whether this
ships on by default, ships with option-2 precision, or stays opt-in.
