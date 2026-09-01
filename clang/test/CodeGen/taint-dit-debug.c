// -g -O2 -ftaint-harden on VECTOR code must not crash the compiler.
//
// REQUIRES: aarch64-registered-target
//
// isDITProtected() ends in a class fallback: anything carrying an FP/SIMD
// register operand is treated as DIT-covered. A DBG_VALUE describing a variable
// that lives in a NEON register carries one, so it was classified as a Need and
// handed to pinToTimingMode(), which marks a Need by ADDING an implicit $dit
// operand.
//
// A DBG_VALUE must have exactly four operands. The fifth aborted the compiler in
// a later pass, nowhere near the taint code:
//
//   Assertion failed: ((MI.isDebugValueList() || MI.getNumOperands() == 4)
//                      && "malformed DBG_VALUE"), VarLocBasedImpl.cpp:430
//   Running pass 'Live DEBUG_VALUE analysis' on '@chacha20_encrypt_bytes_ref'
//
// Found building libsodium natively with -g; chacha20_ref.c is the smallest
// real reproducer. Needs FULL debug info - -gline-tables-only emits no
// DBG_VALUE and does not trigger it. Meta instructions never execute, so they
// can never need the mode; needsDIT() now says so, and pinToTimingMode()
// refuses to mutate one regardless of caller.
//
// The bug is a CRASH, so the test is that this compiles at all. The DIT check
// keeps it honest: hardening must still be doing its job on this file.
//
// RUN: %clang_cc1 -triple aarch64-apple-macosx -O2 -debug-info-kind=limited -dwarf-version=5 -S \
// RUN:     -ftaint-harden=%S/Inputs/dit-debug-secret.txt %s -o - \
// RUN:   | FileCheck %s
//
// Same file without debug info, as a control: if this ever fails the test is
// no longer isolating the debug-info interaction.
// RUN: %clang_cc1 -triple aarch64-apple-macosx -O2 -S \
// RUN:     -ftaint-harden=%S/Inputs/dit-debug-secret.txt %s -o - \
// RUN:   | FileCheck %s

typedef unsigned char u8x16 __attribute__((vector_size(16)));

// Vector locals live in NEON registers and get DBG_VALUEs naming them under -g.
unsigned mix_vec(const unsigned char *secret, unsigned n) {
  u8x16 acc = (u8x16){0};
  for (unsigned i = 0; i < n; i++) {
    u8x16 v;
    __builtin_memcpy(&v, secret + (i & 15), sizeof v);
    acc ^= v;
    acc += v;
  }
  unsigned r = 0;
  for (int i = 0; i < 16; i++) r = r * 131 + acc[i];
  return r;
}

// 26 is the PSTATE.DIT pstatefield encoding (op1=0b011, op2=0b010). Match on
// the number, not the name: the Mach-O asm printer emits `msr #26, #1` where the
// ELF one prints `msr DIT, #0x1`, so a name-based CHECK silently passes nothing
// on this triple.
// CHECK: msr{{[[:space:]]+}}#26, #1
