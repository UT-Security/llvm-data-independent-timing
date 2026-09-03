/*
 * blanket_ctor.c - the blanket-DIT arm, and the mode-readback gate.
 *
 * CIO's drivers have no DIT support of their own, so blanket cannot be a driver
 * flag. It is a constructor that sets PSTATE.DIT before main and never clears
 * it, linked against the UNHARDENED `base` library. That way blanket and
 * baseline are the same codegen in two runtime modes -- the same relationship A
 * and C have in the native rig -- and any difference between them is the mode
 * bit alone, not a recompile.
 *
 * THE READBACK IS BEHIND -DDIT_READBACK, AND TIMING BUILDS MUST NOT DEFINE IT.
 * A `mrs DIT` that exists at all decodes to MrsDit64 under the speculative model
 * and to the SERIALISING Mrs64 under --no-speculative-dit, and that difference
 * perturbs the measured region even though the instruction itself runs in an
 * exit-time destructor, long after the last ROI dump. Measured on ed25519:
 *
 *     arms                          spec        serdit      drift
 *     base/nop, readback compiled   1562973     1557511     0.349% / 0.450%
 *     base/nop, readback removed    1560877     1560877     0.000% / 0.000%
 *
 * Both arms execute zero DIT writes inside the ROI, so the switch model has
 * nothing legitimate to act on and the drift was entirely an artifact of the
 * gate instrument. It was caught by the cross-model control gate in
 * run_cio_gem5.py, which is what that gate is for: the observer was the
 * confound. chacha20 was unaffected (0.00% either way), so this only ever
 * showed up on the long region.
 *
 * The gate is still worth having -- an arm that exits with DIT set leaked the
 * mode past an unbalanced exit and is blanket in disguise, which is the
 * tail-call bug that went unnoticed on this library for months (13 sites,
 * crypto_sign among them). So it moves to a SEPARATE build: check the mode on
 * readback-enabled binaries, take the timings from readback-free ones.
 *
 * -DBLANKET_DIT is unavoidably one `msr DIT` in the blanket arm, since that arm
 * IS the mode being set. It executes once, before any ROI, and it leaves the
 * same ~0.2% artifact on ed25519. That is inherent to the arm rather than to
 * the instrumentation, and it is recorded rather than removed.
 */
#include <stdio.h>

#ifdef BLANKET_DIT
__attribute__((constructor)) static void cio_blanket_on(void) {
    __asm__ volatile("msr DIT, #1" ::: "memory");
}
#endif

#ifdef DIT_READBACK
__attribute__((destructor)) static void cio_dit_readback(void) {
    unsigned long d = 0;
    __asm__ volatile("mrs %0, DIT" : "=r"(d));
    fprintf(stderr, "CIOGEM5 exit dit=%lu\n", (d >> 24) & 1UL);
}
#endif
