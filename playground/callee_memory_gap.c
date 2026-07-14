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
 * KNOWN GAP (2026-07-14, verified on commit cb64535). Build:
 *   build/bin/clang -O1 -ftaint-harden=playground/callee_memory_gap_secret.txt \
 *       -c playground/callee_memory_gap.c -o /tmp/g.o
 *   build/bin/llvm-objdump -d --no-show-raw-insn /tmp/g.o
 *
 * via_external_return / via_indirect: SOUND. An external or indirect callee's
 * return value is conservatively tainted when any argument is tainted, so the
 * secret-dependent arithmetic after the call sits inside a barrier region.
 *
 * via_external_memory: UNSOUND -- the missing barrier. ext_copy() writes the
 * secret into buf, but the summary has no memory component, so the region
 * CLOSES with a dsb right after the bl and the reload is left unprotected:
 *
 *      isb
 *      bl   ext_copy
 *      dsb  sy          <- region closes here
 *      ldr  w8, [sp]    <- reload of the secret: UNPROTECTED
 *      add  w0, w8, w8, lsl #1
 *
 * The same hole exists for a plain in-TU direct call (see the intu_copy case
 * in the research doc) -- it is NOT specific to external callees.
 * Design + literature: utils/taint_memory_summary_research.md
 * ------------------------------------------------------------------------- */
