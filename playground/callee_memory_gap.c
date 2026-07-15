#include <stdint.h>
extern void ext_copy(int *dst, int secret);   // external: may write secret into dst
extern int  ext_id(int secret);               // external: may return the secret
typedef int (*fp_t)(int);

int via_external_memory(int secret) {         // secret is arg 0
  int buf[4] = {0,0,0,0};
  ext_copy(buf, secret);                      // secret ESCAPES to an external callee
  return buf[0] * 3;                          // does the reload come back tainted?
}

int via_external_return(int secret) {
  int r = ext_id(secret);                     // external return value
  return r * 3;
}

int via_indirect(int secret, fp_t f) {
  int r = f(secret);                          // indirect call
  return r * 3;
}

/* -------------------------------------------------------------------------
 * FIXED 2026-07-15 (blunt-TOP P0 memory-effects summary). Build:
 *   build/bin/clang -O1 -ftaint-harden=playground/callee_memory_gap_secret.txt \
 *       -c playground/callee_memory_gap.c -o /tmp/g.o
 *   build/bin/llvm-objdump -d --no-show-raw-insn /tmp/g.o
 *
 * via_external_return / via_indirect: always were SOUND -- an external/indirect
 * callee's return value is conservatively tainted when any argument is tainted.
 *
 * via_external_memory: WAS the missing-barrier leak. ext_copy() writes the
 * secret into buf; the summary had no memory component, so the reload was left
 * outside DIT coverage. Now ext_copy is an external decl receiving a secret, so
 * the caller applies TOP (ExternalMemClobbered) and the reload is tainted. The
 * hardened disasm now protects it (msr DIT, #1 re-asserted after the bl, and the
 * reload + secret-dependent arithmetic run before msr DIT, #0):
 *
 *      msr  DIT, #0x1        <- entry enable
 *      bl   ext_copy
 *      msr  DIT, #0x1        <- re-assert after call (G1)
 *      ldr  w8, [sp, #0x8]   <- reload of the secret: NOW PROTECTED
 *      add  w0, w8, w8, lsl #1
 *      msr  DIT, #0x0        <- disable before ret
 *
 * The same fix covers the plain in-TU direct-call case (a callee that stores a
 * secret through a pointer arg gets a WritesSecretToUnknown mod-set) -- see the
 * intu_copy case in llvm/test/CodeGen/AArch64/taint-analysis-callee-memory.mir.
 * P0 is blunt (whole-object, no arg-i precision); design + P1 refinements:
 * utils/taint_memory_summary_research.md
 * ------------------------------------------------------------------------- */
