# Per-function save/restore of a hardware mode bit: what already exists

**Question.** `docs/design/dit-unconditional-design.md` proposes that every instrumented
function read `PSTATE.DIT` at entry and restore it before returning, making the bit
callee-saved by convention so callers can stop re-asserting. Is that an established
compiler technique, is claiming a register for it established, and has anyone specified a
*timing* mode bit as callee-saved?

Surveyed 2026-08-27. Provenance is marked because the negatives carry weight:

- `[V]` verified by me in this checkout, including by building it.
- `[R]` reported from a primary source the survey fetched; not re-verified here.
- `[R2]` reported at second hand by the survey; treat as weakest.
- `[INF]` inference, asserted by no source.

---

## 1. Bottom line

**The code shape is not novel and does not need defending.** LLVM and GCC both already
emit it for `PSTATE.SM`, on this target, under an AAPCS64 contract whose table cell reads
*"PSTATE.SM on normal return: unchanged"*. GCC generalises the whole problem class with a
28-year-old pass whose hooks are named `TARGET_MODE_ENTRY` and `TARGET_MODE_EXIT`.

**The storage answer is a pre-RA virtual register, not a register claim.** Every mechanism
that holds mode state live across a body uses a vreg or a frame slot. In-tree PEI register
claims are for scratch only.

**Nobody has specified a timing or security mode bit as caller- or callee-saved.** That is
the novel half, and the negative behind it is hard.

---

## 2. The closest existing mechanism: AArch64 SME `PSTATE.SM`

### 2.1 The ABI already specifies the contract `[R]`

AAPCS64, "PSTATE.SM interfaces" (Beta):

| Type of interface | PSTATE.SM on entry | PSTATE.SM on normal return |
|---|---|---|
| Non-streaming | 0 | 0 |
| Streaming | 1 | 1 |
| Streaming-compatible | 0 or 1 (caller's choice) | **unchanged** |

> Every subroutine has exactly one PSTATE.SM interface. [...] The "PSTATE.SM on entry"
> column describes a requirement on callers [...] The "PSTATE.SM on normal return" column
> describes a requirement on callees: callees must ensure that PSTATE.SM has a valid value
> before returning to their caller.
>
> **All subroutines that were written before the introduction of SME are retroactively
> classified as having a non-streaming interface.**

Two things to reuse: "streaming-compatible" *is* a callee-preserves-the-mode-bit contract,
and the retroactive-classification device is the clean answer to "what about everything
compiled before this existed".

### 2.2 LLVM emits our exact sequence `[V]`

`AArch64ISelLowering.cpp:8897` creates the carrier, `:3342` lowers it to `MRS ... SVCR`.
Built with this tree's `llc` (`-mattr=+sve,+sme`, streaming-compatible body, normal
callee):

```asm
	stp	x30, x19, [sp, #64]
	mrs	x19, SVCR
	tbnz	w19, #0, .LBB0_2
	smstart	sm
.LBB0_2:
	smstop	sm
	bl	normal_callee
	smstart	sm
	tbnz	w19, #0, .LBB0_4
	smstop	sm
.LBB0_4:
	ldp	x30, x19, [sp, #64]
	ret
```

**Storage: a virtual register created during ISel.** RA assigns a callee-saved GPR because
the value is live across the call; PEI spills it as an ordinary CSR and emits the CFI. With
no intervening call, RA picks a caller-saved register instead.

**Where we deliberately diverge.** SME guards its *enable* (`tbnz` then `smstart sm`). We
may not - a mispredicted skip of a DIT enable runs secret work at DIT=0 and the gated
optimizations' footprint survives the squash (`dit-unconditional-design.md` §3.1). SME can
do it because streaming mode is not a confidentiality property. Expect a reviewer to
compare the two shapes; the divergence is principled.

### 2.3 GCC solves it with a frame slot in a late pass `[R]`

`gcc/config/aarch64/aarch64.cc`: `TARGET_USE_LATE_PROLOGUE_EPILOGUE` returns true when the
function enables PSTATE.SM, and `aarch64_old_svcr_mem` addresses a frame slot from the hard
frame pointer. Commit `r14-6164`: *"The code is emitted late using a new pass that runs
near prologue/epilogue insertion."* This is the B2 option in the design doc, and GCC is the
existence proof that a late pass can do it.

### 2.4 Tail calls: both compilers refuse the optimization `[V]`

`AArch64ISelLowering.cpp:9401`, and GCC independently in
`aarch64_function_ok_for_sibcall` `[R]`. Covered in `docs/research/tail-call-precedent.md`.

### 2.5 Non-local exit: force a known value, re-establish at the handler `[R]`

AAPCS64 §10.4 requires PSTATE.SM = 0 on entry to an exception handler.
`MachineSMEABIPass.cpp:486` seeds EH pads with a fixed entry state, and
`sme-streaming-mode-landingpads.ll` states the rule: *"The unwinder will always re-enter
functions with streaming-mode disabled, so we must ensure streaming-mode is enabled on
entry to exception handlers."* libunwind was taught to call `__arm_za_disable` in
`jumpto()`.

**abi-aa #394, "Streaming State recoverability by the Unwinder", is still open** - Arm has
no DWARF CFI mechanism for recording streaming mode across frames. So this problem is
unsolved upstream too, not just for us.

---

## 3. GCC has had a generic framework since 1998 `[R]`

`gcc/mode-switching.cc`. `gcc/doc/tm.texi`:

> `TARGET_MODE_ENTRY (int entity)` - It should return the mode that *entity* is guaranteed
> to be in on entry to the function [...] If `TARGET_MODE_ENTRY` is defined then
> `TARGET_MODE_EXIT` must be defined.
>
> `TARGET_MODE_BACKPROP (...)` - suppose there is a "one-shot" entity that [...] either
> stays off or makes exactly one transition from off to on. It is safe to make the
> transition at any time, but it is better not to do so unnecessarily. **This hook allows
> the function to manage such an entity without having to track its state at runtime.**
>
> `TARGET_MODE_EH_HANDLER (int entity)` - it should return the mode that *entity* is
> guaranteed to be in on entry to an exception handler.

Two of those are things this project built by hand or has not built at all:
`TARGET_MODE_BACKPROP` is `-taint-dit-loop-hoist=1`, and **`TARGET_MODE_EH_HANDLER` is the
lattice element our soundness verifier lacks for EH edges.** GCC's abnormal-edge rule is
also worth copying verbatim: *"We don't control mode changes across abnormal edges"* /
*"Pretend the mode is clobbered across abnormal edges"*, i.e. TOP on an abnormal edge,
forced to a target-declared constant at the EH pad.

Targets using it: x86 (`X86_DIRFLAG`, `AVX_U128`, four `I387_CW_*`), SH4 (FPSCR PR/SZ),
RISC-V (`VXRM`, `FRM`), AArch64 (SME), Epiphany. **LLVM has no equivalent.**

---

## 4. Storage techniques, with instances

| technique | instances | holds state across a body? |
|---|---|---|
| **Global register reservation** | AArch64 ShadowCallStack (x18), `-ffixed-xN`, `-mstack-protector-guard-reg` `[V/R]` | yes, but TU-wide cost; SCS is `report_fatal_error` on Darwin because x18 is the platform register there |
| **PEI claim of an unspilled CSR** | AArch64 big-stack scavenging, Win64-on-non-Windows x18, ARM interrupt FPSCR/FPEXC via R4/R5, RISC-V `cm.push`, Mips16 `[R]` | **no - scratch or transient only** |
| **Frame slot created pre/during PEI** | StackProtector canary, SME TPIDR2 block, GCC `old_svcr`, AArch64 SwiftAsyncContext, x87 control word around `FP80_ADD` `[R]` | yes |
| **Pre-RA virtual register** | LLVM SME `PStateSMReg`, LLVM RISC-V `frm`, GCC SH4 FPSCR `[V/R]` | yes - **and this is what to copy** |

The nearest analogue to our sequence is ARM's `interrupt_save_fp` `[R]`:
`VMRS R4, FPSCR` in the prologue and `VMSR` in the epilogue. Two differences: the carrier
is a hard-coded R4, and the value is pushed immediately rather than held. Its CFI story is
a warning - it landed, was reverted, and re-landed with the annotation dropped:
*"Since these are status registers, there really is no viable way of annotating this."*

**Why a PEI claim is the wrong tool**, three documented reasons, reproduced in
`dit-unconditional-design.md` §5.1: the `isPhysRegModified` seed misses read-only uses
(bug `d78597ec08b9`), it competes with the register scavenger where MTE already needs two
registers, and on Darwin an unpairable claim is silently reverted.

---

## 5. Has anyone specified a security mode bit as callee-saved? No.

| document | searched for | result |
|---|---|---|
| AAPCS64, 3635 lines | `DIT`, `data.independent` | **0** `[R]` |
| abi-aa, all 412 issue + PR records | `DIT`, "data independent timing", "side channel", "constant time", "DOIT" | **0 each** `[R]` |
| aadwarf64 | `DIT`, `timing` | **0**; no PSTATE DWARF register `[R]` |
| ACLE | `DIT` | 1 hit, the FMV feature-name table only `[R]` |
| x86-64 psABI v1.0 | `DOITM`, "data operand", "independent timing" | **0, 0, 0** `[R]` |
| Linux `msr-index.h` | `UARCH`, `DOITM` | no matches; the enable-DOITM patch never merged `[R]` |
| LLVM at merge-base | `insertTimingModeSwitch` | absent - **all DIT codegen in this tree is the fork's** `[R]` |
| BoringSSL, libsodium | `msr dit`, `FEAT_DIT` | **0** in both `[R2]` |

Both 2025 LLVM constant-time RFCs mention DIT/DOIT and decline to emit them. Trail of
Bits: *"Adding support for DIT and DOIT to the ARM and x86 LLVM backends would require
further significant changes and discussion."*

### What does exist, and is quotable

**x86-64 psABI §3.2.1 `[R]` - the 30-year precedent for exactly our split:**

> The control bits of the `MXCSR` register are callee-saved (preserved across calls), while
> the status bits are caller-saved [...] the x87 control word is callee-saved.
> [...] The direction flag DF in the `%rFLAGS` register must be clear on function entry and
> return.

Control/mode bits callee-saved, cumulative status bits caller-saved, plus a
mandated-known-value-at-boundaries rule. Never extended to DOITM.

**AAPCS64 has three categories, and mode registers get the third `[R]`.** FPCR is a
*"global register"* - "neither saved nor destroyed by a subroutine" - with one exception:
*"The NEP bit (bit 2) must be zero on entry to and return from a public interface."* So we
depart from how AAPCS64 treats mode bits *generally* and adopt how it treats SME state
*specifically*. Two shapes are available: **fixed value at the boundary** (FPCR.NEP, x86
DF) versus **restore the entry value** (PSTATE.SM streaming-compatible, MXCSR control
bits). Ours is the second, the rarer one.

**ACLE already supplies the vocabulary `[R]`:** `__arm_in`, `__arm_out`, `__arm_inout`,
and **`__arm_preserves`** - *"the callee does not read the incoming state and returns with
the state unchanged"* - with the default being caller-restores. **The cleanest statement of
this proposal is: make `__arm_preserves("dit")` the universal default.**

**A private convention is legitimate, but not across a public edge `[R]`.** abi-aa #405,
Richard Earnshaw (Arm): *"the AAPCS defines behavior at public **conforming** interfaces.
There's no requirement for all interfaces to conform to the conventions it specifies, if
there is agreement between all related parties."* Peter Smith, same thread: a PLT stub,
lazy binding, or a `--wrap` interposed wrapper can run code between caller and callee. So
we can ship without amending AAPCS64, and correspondingly must keep the whole-function
fallback and the `ESCAPE` audit for edges we do not control.

**Four codebases hand-write the callee half.** Apple `timingsafe_enable_if_supported` /
`_restore_if_supported` (shipping API, macOS 15.2+) `[R]`; corecrypto, whose comment *"in
that case that function is responsible for disabling DIT when returning"* is this
convention in prose `[R]`; Go `crypto/subtle.WithDataIndependentTiming`, which is
**bidirectional at the cgo boundary** - it re-asserts DIT after returning from C and
preserves the caller's DIT when C calls into Go `[R]`; and OpenSSL PR #28764, still open,
which reached our call-site precision insight by hand (`/* OSSL_ENABLE_DIT_FOR_SCOPE
explicitly omitted on verify */`) `[R]`.

**DIT is already preserved everywhere except across a procedure call `[R]`.** It is PSTATE
bit 24, saved and restored by hardware across exception entry/return and by the kernel
across context switches and signals. **The procedure call is the one boundary at which its
value is unspecified - that is the hole this convention fills.** Good framing for the
paper.

---

## 6. Hazards

- **`region` + `longjmp` is the leak case `[R]`.** glibc and musl AArch64
  `setjmp`/`longjmp` touch no PSTATE, so a CSR-held carrier is restored to its
  `setjmp`-time value and the setjmp'ing frame still restores correctly. What is lost is
  every *intervening* frame's restore. Under whole-function placement that is
  over-protection; under `region`, which actively clears, the innermost frame can leave DIT
  cleared. Note glibc's `getcontext.S`/`setcontext.S` *do* save and restore FPCR but never
  read the pstate slot, so `swapcontext`-based coroutines silently drop DIT.
- **EH unwinding skips the epilogue, and CFI cannot describe DIT `[R]`.** The Itanium EH
  ABI restores callee-saved registers and says nothing about mode bits. There is no DWARF
  register number for DIT. If one is ever added, aadwarf64's own guidance is to encode
  absolute state: `DW_CFA_AARCH64_negate_ra_state` was deprecated because it *"is unable to
  correctly represent the state when the code flow is not linear between the
  signing/authenticating PAC instructions"* - which describes region placement exactly.
- **Signals are benign `[R]`.** `setup_return()` does not clear `PSR_DIT_BIT`, and pstate
  round-trips through the sigframe. Note the kernel sets DIT *on* when entering from EL0
  (`entry.S`, `SET_PSTATE_DIT(1)` under `ARM64_HAS_DIT`), after SPSR is latched.
- **objtool is the verifier to copy `[R]`.** It carries `bool df` and `bool uaccess` in
  `insn_state`, keys the visited-set on the mode bit (`visited = VISITED_BRANCH <<
  statep->uaccess`), warns at every call and every return, and models `pushf`/`popf` as a
  shift register with an exhaustion check. **It warns in both directions**, including
  *"return with UACCESS disabled from a UACCESS-safe function"* - the mirror of our
  under-taint direction, which `reportUnbalancedDITExits` does not currently check.
- **`-fzero-call-used-regs` does not conflict `[R]`** - it excludes callee-saved registers
  - but it inserts at `MBB.getFirstTerminator()`, the same iterator the epilogue uses.
- **PAC/BTI ordering `[R]`.** `PACI[AB]SP` is implicitly `BTI c`; if our `mrs` becomes the
  first executable instruction that fast path stops matching and an explicit `bti c` is
  emitted. Insert after the frame-setup `PAUTH_PROLOGUE`.

---

## 7. Closest related work for the paper

**CryptoMPK, IEEE S&P 2022 `[R]`** - this project's architecture on x86 MPK:
compiler-inserted hardware mode-register toggles driven by static taint analysis. Reports
*"the WRPKRU instruction takes about 20 to 30 CPU cycles"*, bracketing our 9.7 renamed /
22.6 serializing and the shipped `switch-cyc=30`. It independently reached our placement
findings - *"if we insert too many privilege switches, especially in hotspots, the
execution will often suffer from performance issue"*, a frequency-weighted admission test
in which *"instructions in loops are additionally weighted"*, and function-level isolation
for hotspots. Its alternative to a convention is **cloning**, which is already partly in
this tree. It has no calling-convention content at all.

**Häuser and Schneider, *Secret Types Require OS-Backed Secrecy Code Sections*, MoVe4SPS
'25 `[R]`** - the closest published *proposal*, with a nesting-counted contract rather than
a callee-saved one, and an explicit argument against compiler ownership: *"the OS must be
responsible for environmental mitigations."* Prepare a rebuttal.

**Let's DOIT, TCHES 2025(3) `[R]`** - surveying its own field: *"there is very little work
directly related to Intel's DOIT or Arm's DIT execution modes [...] we are not aware of any
prior works."* Assumes the mode is enabled before its code runs, and leaves DIT to future
work.

---

## 8. Two action items unrelated to this design

1. **`isDITProtected` may be stale in the UNSAFE direction `[R]`.** Arm has removed
   instructions from the DIT-covered set at least six times since 2021: `RET` (2021-12),
   `SQCVTN`/`SQRSHRN` (2023-03), all `WHILE*` (2023-09), `PEXT` (2023-12),
   `CNTP`/`PUNPK*` (2024-06), all `RCW*` (2024-12). An instruction we believe is covered
   but Arm has since removed is a **missing barrier**. Let's DOIT ships a scraper and a
   change log.
2. **Revisit the post-`MSR DIT` barrier `[R]`.** Apple's documentation says to add a
   speculation barrier after enabling DIT *"to ensure that subsequent instruction timing
   reflects the updated DIT processor state"* - a correctness reason, not speculation
   defense - and its shipping `timingsafe_enable_if_supported` emits one. The ISB/DSB hooks
   were removed 2026-07-14 under a rationale that does not cover this. OpenSSL, notably,
   does not emit one.
