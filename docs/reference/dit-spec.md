# Arm PSTATE.DIT - what it actually guarantees (reference)

**Source of truth:** Arm A-profile Architecture Registers, `DIT, Data Independent
Timing` - https://developer.arm.com/documentation/ddi0601/2026-06/AArch64-Registers/DIT--Data-Independent-Timing
(the developer.arm.com page is JS-rendered; the same content in static form:
https://arm.jonpalmisc.com/latest_sysreg/AArch64-dit and the older
ddi0595 renderings). Captured 2026-07-16. This file is the authority the taint
pass's `TargetInstrInfo::isDITProtected` hook is transcribed from - keep them in
sync.

## The guarantee (PSTATE.DIT == 1)

**The one-line rule (the load-bearing principle):** for the covered instructions,
**the timing cannot depend on non-address register data values.** Exactly:

- **Loads and stores:** *"The timing of every load and store instruction is
  insensitive to the value of the data being loaded or stored."* - so a secret
  **stored/loaded value** is covered (this is what disables silent-store elision
  and load-value prediction on the value). The **address** is a separate matter
  (next bullet).
- **Data-processing:** for the covered set, *"the instruction takes a time which
  is independent of: the values of the data supplied in any of its registers;
  the values of the NZCV flags."* Exception-response timing is likewise
  value-independent.
- **NOT covered - the address/memory-system side.** DIT says nothing about
  timing that depends on the *address* accessed (cache/TLB). A secret-**dependent
  address** still leaks. This is why the taint pass's `secret-address` diagnostic
  is a real residual even under DIT.
- **Outside the set there is NO guarantee:** *"The architecture makes no
  statement about the timing properties when the PSTATE.DIT bit is not set"* - and
  equally, instructions **not in the covered list** get no DIT guarantee even when
  DIT=1. This is why the hook is a **membership list (default: uncovered)**, not a
  short exclusion list: an instruction we cannot place in the covered set is
  flagged for audit rather than silently assumed safe.

## Covered instruction groups (transcribed from the DIT register description)

Loads/stores are covered **as a class** for their data value (blanket statement
above), so the hook returns "covered" for any `mayLoad()/mayStore()` instruction
on the data-value question and lets the `secret-address` path handle the address.
The **data-processing** covered set is enumerated:

**Branches / system:** `CFINV`, `NOP`.

**DP - immediate:** add/sub `ADD ADDS SUB SUBS`; bitfield `BFM SBFM UBFM`; extract
`EXTR`; logical `AND ANDS EOR ORR`; min/max `SMAX SMIN UMAX UMIN`; move-wide
`MOVK MOVN MOVZ`.

**DP - register:** add/sub (extended/shifted/carry) `ADD ADDS SUB SUBS ADC ADCS
SBC SBCS`; conditional compare `CCMN CCMP`; conditional select `CSEL CSINC CSINV
CSNEG`; 1-source `ABS CLS CLZ CNT CTZ RBIT REV16 REV32 REV`; 2-source `ASRV
LSLV LSRV RORV CRC32B CRC32CB CRC32CH CRC32CW CRC32CX CRC32H CRC32W CRC32X SMAX
SMIN UMAX UMIN`; 3-source `MADD MSUB SMADDL SMSUBL SMULH UMADDL UMSUBL UMULH`;
flag ops `SETF8 SETF16 RMIF`; logical (shifted) `AND ANDS BIC BICS EON EOR ORN
ORR`.

**DP - scalar FP & Advanced SIMD:** the SIMD/FP data-processing set, incl.
conditional select `FCSEL`, and the crypto extensions - AES `AESD AESE AESIMC
AESMC`; SHA `SHA1C SHA1M SHA1P SHA1H SHA256H SHA256H2 SHA256SU0 SHA256SU1 SHA512H
SHA512H2 SHA512SU0 SHA512SU1`; SM3/SM4.

**SVE / SME:** the corresponding vector/predicate data-processing and memory ops
(not emitted by the current pipeline; treated conservatively - see below).

## The exclusions that matter to this project

Notably **absent** from the covered data-processing set - therefore **NOT DIT
protected**, and flagged by the pass:

- **Integer divide: `SDIV`, `UDIV`.** (Absent from DP 2-source, which lists
  `ASRV/LSLV/LSRV/RORV/CRC32*/SMAX/SMIN/UMAX/UMIN` but no divide.)
- **FP divide / square root: `FDIV`, `FSQRT`** (scalar and vector), likewise
  absent from the covered FP set.

These are the documented data-value-timing instructions a compiler actually
emits. A tainted one is silent false assurance: DIT is enabled around it but its
operand-timing channel remains. → diagnostic reason `not-dit-covered` (the
printed opcode identifies the specific instruction, e.g. `SDIVXr`).

## How the hook maps this (membership list, default uncovered)

`TargetInstrInfo::isDITProtected(MI)` (AArch64 override):
1. `mayLoad() || mayStore()` → **covered** (data value; the blanket load/store
   guarantee). The address side is handled separately by the `secret-address`
   diagnostic, not here.
2. Opcode in the enumerated covered DP/SIMD/crypto set → **covered**.
3. Otherwise → **uncovered** (default). Divide/sqrt land here (not in the set), as
   does anything the transcription does not recognize - flagged for audit, the
   safe direction. Over-flagging an exotic covered instruction costs an audit
   line; under-flagging an uncovered one is the silent-leak we are preventing.

**Maintenance:** when the covered opcode switch in `AArch64InstrInfo.cpp` and this
file disagree, this file (the Arm spec) wins - update the switch to match.
