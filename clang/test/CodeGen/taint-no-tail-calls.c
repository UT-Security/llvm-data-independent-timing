// Tail calls are off TU-wide for any -ftaint-harden build, and TailRecursionElimination
// still runs. docs/design/dit-tailcall-gap.md
//
// WHY THE DISABLE EXISTS. A tail call is an exit with no epilogue, so an instrumented
// function that takes one can never clear PSTATE.DIT again. libsodium's
// `randombytes_buf` exits through an indirect tail call, so a program that only calls
// `sodium_init()` runs with DIT set for its whole life and pays the entire always-on
// penalty at zero secret fraction (+14.77% renamed / +14.64% serializing, against
// -0.10% / -0.00% with tail calls suppressed - dit-tailcall-gap.md §7).
//
// WHY IT IS CHECKED IN ASM AND NOT IN IR. The `disable-tail-calls` attribute is
// deliberately NOT in -emit-llvm output: it is stamped at codegen, after the IR
// pipeline, so that TailRecursionElimination - which reads the same attribute - is not
// collateral damage. Only the module flag is visible in IR.
//
// REQUIRES: aarch64-registered-target

typedef unsigned long u64;
extern u64 sink(u64);

// The function under test is UNSEEDED. The disable is TU-wide, so it must lose its tail
// call anyway; a test that only inspected instrumented functions would pass with a
// per-function implementation that leaves the rest of the module leaking.
u64 fwd(u64 s) { return sink(s); }

// Self tail-recursive, and opaque enough that the optimizer cannot close-form it. TRE
// turns this into a loop; without TRE it stays a real recursive call, which is the
// O(n)-stack-frames hazard that made this disable unshippable until it moved
// after the optimizer.
u64 walk(const u64 *p, u64 acc) {
  if (!*p)
    return acc;
  return walk(p + 1, acc + *p);
}

u64 mix(u64 secret) { return secret * 0x9e3779b97f4a7c15UL; }

// 1. The default. -ftaint-harden alone, nothing else.
// RUN: %clang_cc1 -triple aarch64-apple-macosx -O2 -S \
// RUN:     -ftaint-harden=%S/Inputs/taint-no-tail-calls-secret.txt %s -o - \
// RUN:   | FileCheck %s --check-prefixes=DEFAULT,TRE
//
// DEFAULT-LABEL: _fwd:
// DEFAULT:       bl {{_?}}sink
// DEFAULT:       ret
// DEFAULT-NOT:   b {{_?}}sink

// 2. TRE SURVIVES. This is the invariant the late stamp exists to protect: if anyone
// moves the stamp back before the optimizer, `walk` becomes a real recursion and this
// fails.
// TRE-LABEL: _walk:
// TRE-NOT:   bl {{_?}}walk
// TRE:       ret

// 3. An EMPTY seed file still disables tail calls. This is the baseline arm every A/B
// rig builds; keying the disable on the seeds instead of the flag would leave it
// codegen-mismatched against the arm it is the control for.
// RUN: %clang_cc1 -triple aarch64-apple-macosx -O2 -S \
// RUN:     -ftaint-harden=%S/Inputs/taint-no-tail-calls-empty.txt %s -o - \
// RUN:   | FileCheck %s --check-prefix=EMPTYSEED
//
// EMPTYSEED-LABEL: _fwd:
// EMPTYSEED:       bl {{_?}}sink
// EMPTYSEED-NOT:   b {{_?}}sink
// No seeds means no hardening, so the baseline really is switch-free.
// EMPTYSEED-NOT:   msr

// 4. What is, and is not, in the IR. The module carries the REQUEST so the LTO backend
// can act on it after its own optimizer; it must NOT carry the attribute, or the LTO
// pipeline's TRE would see it.
// RUN: %clang_cc1 -triple aarch64-apple-macosx -O2 -emit-llvm \
// RUN:     -ftaint-harden=%S/Inputs/taint-no-tail-calls-secret.txt %s -o - \
// RUN:   | FileCheck %s --check-prefix=IR
//
// IR-NOT: "disable-tail-calls"
// IR:     !{i32 4, !"taint-no-tail-calls", i32 1}

// 5. Input that is ALREADY IR. CGCall's per-function stamping never ran for `-x ir`,
// which is how the libsodium sweep builds every arm - five tail calls through
// crypto_onetimeauth survived that way. Emit the bitcode WITHOUT hardening, so the tail
// call is fully formed in it, then harden the bitcode.
// RUN: %clang_cc1 -triple aarch64-apple-macosx -O2 -emit-llvm-bc %s -o %t.bc
// RUN: %clang_cc1 -triple aarch64-apple-macosx -O2 -S -x ir %t.bc \
// RUN:     -ftaint-harden=%S/Inputs/taint-no-tail-calls-secret.txt -o - \
// RUN:   | FileCheck %s --check-prefix=FROMBC
//
// FROMBC-LABEL: _fwd:
// FROMBC:       bl {{_?}}sink
// FROMBC-NOT:   b {{_?}}sink

// 6. The A/B hatch. On serializing-`MSR DIT` hardware the clears this restores are real
// switches, worth +8.89 points at f = 9.4%, so the trade has to stay measurable.
// RUN: %clang_cc1 -triple aarch64-apple-macosx -O2 -S \
// RUN:     -ftaint-harden=%S/Inputs/taint-no-tail-calls-secret.txt \
// RUN:     -mllvm -taint-no-tail-calls=0 %s -o - \
// RUN:   | FileCheck %s --check-prefix=OPTOUT
//
// OPTOUT-LABEL: _fwd:
// OPTOUT:       b {{_?}}sink

// 7. The hatch is REFUSED under the callee-saved ABI, where a surviving tail call is not
// a cost but a violation of the contract: the callee would leak its enable into its
// caller's caller with no epilogue to restore it.
// RUN: %clang_cc1 -triple aarch64-apple-macosx -O2 -S -ftaint-dit-abi \
// RUN:     -ftaint-harden=%S/Inputs/taint-no-tail-calls-secret.txt \
// RUN:     -mllvm -taint-no-tail-calls=0 %s -o /dev/null 2>&1 \
// RUN:   | FileCheck %s --check-prefix=ABIWARN
//
// ABIWARN: warning: taint: -taint-no-tail-calls=0 ignored under -taint-dit-abi
//
// RUN: %clang_cc1 -triple aarch64-apple-macosx -O2 -S -ftaint-dit-abi \
// RUN:     -ftaint-harden=%S/Inputs/taint-no-tail-calls-secret.txt \
// RUN:     -mllvm -taint-no-tail-calls=0 %s -o - 2>/dev/null \
// RUN:   | FileCheck %s --check-prefix=ABIREFUSED
//
// ABIREFUSED-LABEL: _fwd:
// ABIREFUSED:       bl {{_?}}sink

// 8. No flag, no change. Codegen is byte-for-byte what it was.
// RUN: %clang_cc1 -triple aarch64-apple-macosx -O2 -S %s -o - \
// RUN:   | FileCheck %s --check-prefix=NOFLAG
//
// NOFLAG-LABEL: _fwd:
// NOFLAG:       b {{_?}}sink
