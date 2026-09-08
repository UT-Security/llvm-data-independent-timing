/* What one `msr DIT` costs UNDER GEM5, measured the way silicon/dit_switch_cost.c
 * measures it on the M4 (33.51 cycles per serialising write there).
 *
 * Same shape as the silicon probe: a long loop of back-to-back writes against an
 * identical loop with the writes removed, differenced, so the loop overhead
 * cancels. The ROI is delimited by m5 pseudo-ops instead of a PMC read, so the
 * simulator's own numCycles is the measurement and there is no counter offset at
 * all.
 *
 *   --kind empty   the bare loop
 *   --kind pair    msr DIT,#1 ; msr DIT,#0      (both writes CHANGE the mode)
 *   --kind same    msr DIT,#1 ; msr DIT,#1      (the second is redundant -- the
 *                                                --apple model is supposed to
 *                                                skip the flush for it)
 *   --kind sb      msr DIT,#1 ; sb ; msr DIT,#0 (Apple's bracket shape)
 *   --kind mrs     mrs x, DIT                   (the token read alone)
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline void roi_begin(void) {
    register long a0 __asm__("x0") = 0; register long a1 __asm__("x1") = 0;
    __asm__ volatile(".inst 0xff400110" :: "r"(a0), "r"(a1) : "memory");
}
static inline void roi_end(void) {
    register long a0 __asm__("x0") = 0; register long a1 __asm__("x1") = 0;
    __asm__ volatile(".inst 0xff420110" :: "r"(a0), "r"(a1) : "memory");
}
static volatile unsigned long sink;

int main(int argc, char **argv) {
    const char *kind = "empty";
    long n = 200000;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--kind") && i + 1 < argc) kind = argv[++i];
        else if (!strcmp(argv[i], "--n") && i + 1 < argc) n = atol(argv[++i]);
    }
    /* warm the loop's fetch path outside the ROI */
    for (long i = 0; i < 200; i++) __asm__ volatile("" ::: "memory");

    roi_begin();
    if (!strcmp(kind, "empty")) {
        for (long i = 0; i < n; i++) __asm__ volatile("" ::: "memory");
    } else if (!strcmp(kind, "pair")) {
        for (long i = 0; i < n; i++) {
            __asm__ volatile("msr DIT, #1" ::: "memory");
            __asm__ volatile("msr DIT, #0" ::: "memory");
        }
    } else if (!strcmp(kind, "same")) {
        __asm__ volatile("msr DIT, #1" ::: "memory");
        for (long i = 0; i < n; i++) {
            __asm__ volatile("msr DIT, #1" ::: "memory");
            __asm__ volatile("msr DIT, #1" ::: "memory");
        }
        __asm__ volatile("msr DIT, #0" ::: "memory");
    } else if (!strcmp(kind, "sb")) {
        for (long i = 0; i < n; i++) {
            __asm__ volatile("msr DIT, #1" ::: "memory");
            __asm__ volatile(".inst 0xd50330ff" ::: "memory");
            __asm__ volatile("msr DIT, #0" ::: "memory");
        }
    } else if (!strcmp(kind, "mrs")) {
        unsigned long v;
        for (long i = 0; i < n; i++) {
            __asm__ volatile("mrs %0, DIT" : "=r"(v));
            sink += v;
        }
    }
    roi_end();
    printf("dit_switch_cost kind=%s n=%ld sink=%lu\n", kind, n, sink);
    return 0;
}
