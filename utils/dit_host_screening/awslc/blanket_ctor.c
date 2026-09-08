/*
 * blanket_ctor.c - experiment 14's arm C on gem5.
 *
 * On silicon, arm C is the `rel` binary run with DYLD_INSERT_LIBRARIES pointing
 * at libditctl.dylib, whose constructor sets PSTATE.DIT before main. Every gem5
 * binary here is statically linked, so there is nothing to insert. This object
 * is linked into EVERY build instead and gated on an environment variable, so A
 * and C stay what they are on silicon: one binary, two runtime modes, byte for
 * byte the same code and the same layout. Linking it into only the blanket build
 * would have made C a different binary from A and put a layout term inside the
 * one comparison that is supposed to isolate the mode bit.
 *
 * NO READBACK HERE, deliberately. cioparity/blanket_ctor.c measured that a
 * `mrs DIT` which merely EXISTS in the binary decodes to MrsDit64 under the
 * speculative model and to the serialising Mrs64 under --no-speculative-dit,
 * and that moved a region containing no DIT writes at all by 0.35-0.45%. The
 * exit gate would therefore be the confound. The sound witnesses that the mode
 * is on are in the stats: commit.ditSetImm for the write and commit.ditCycles
 * for the dwell, both attributed to the ROI.
 *
 * The one `msr DIT, #1` below is inherent to arm C -- that arm IS the mode being
 * set. It executes once, in a constructor, before any ROI, and in every other arm
 * the branch is not taken so it is never decoded.
 */
#include <stdlib.h>

__attribute__((constructor)) static void awslc_blanket_on(void) {
    const char *v = getenv("AWSLC_BLANKET");
    if (v && v[0] == '1') {
        /* The RAW ENCODING of `msr dit, #1`, which is what AWS-LC itself uses
         * (crypto/fipsmodule/cpucap/cpu_aarch64.c) and for the same reason: the mnemonic
         * needs FEAT_DIT in the assembler's target, and clang on a host without it refuses
         * with "expected writable system register or pstate". gcc happens to accept it, so
         * this only broke when the rig's default compiler moved to clang to match the
         * silicon build's inlining. The encoding assembles anywhere. */
        __asm__ volatile(".inst 0xd503415f" ::: "memory");
    }
}
