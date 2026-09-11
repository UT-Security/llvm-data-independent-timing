/* Does gem5's SB cost grow with what is in flight? The gem5 twin of the M4
 * probe: W independent long-latency loads issued, then the sequence.
 *
 * On the M4, sb costs 22 cycles with nothing in flight and 58 with 32
 * independent DRAM loads -- it waits for something that scales. IsSerializeAfter
 * ("stall rename until the ROB empties") should produce exactly that shape. If
 * it does under --expedite, where the MSR does not squash, then the flag is the
 * right mechanism and the zero under --apple is the MSR's squash removing the
 * work sb would otherwise have waited for. If it does NOT grow, the flag is not
 * enough on its own.
 *
 *   --kind work | msr | sb   (work alone / +2 writes / +2 writes and sb)
 *   --w N                    independent loads in flight before the sequence
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
enum { BIG = 1u << 21 };          /* 16 MB, past L2 */
static uint64_t big[BIG];
static volatile uint64_t sink;

int main(int argc, char **argv) {
    const char *kind = "work";
    long n = 20000, W = 8;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--kind") && i + 1 < argc) kind = argv[++i];
        else if (!strcmp(argv[i], "--n") && i + 1 < argc) n = atol(argv[++i]);
        else if (!strcmp(argv[i], "--w") && i + 1 < argc) W = atol(argv[++i]);
    }
    for (size_t i = 0; i < BIG; i++) big[i] = i * 2654435761u;
    /* kind=isb swaps sb for isb, which already carries IsSquashAfter -- so it
     * measures what Sb64 would cost if it were given that flag instead of
     * IsSerializeAfter, with no rebuild. */
    int k = !strcmp(kind, "work") ? 0 : !strcmp(kind, "msr") ? 1
          : !strcmp(kind, "isb") ? 3 : 2;
    roi_begin();
    for (long i = 0; i < n; i++) {
        uint64_t a = 0;
        for (long w = 0; w < W; w++)
            a += big[(uint64_t)(i * 7919 + w * 65537) & (BIG - 1)];
        sink += a;
        if (k >= 1) __asm__ volatile("msr DIT, #1" ::: "memory");
        if (k == 2) __asm__ volatile(".inst 0xd50330ff" ::: "memory");
        if (k == 3) __asm__ volatile("isb sy" ::: "memory");
        if (k >= 1) __asm__ volatile("msr DIT, #0" ::: "memory");
    }
    roi_end();
    printf("sbdrain kind=%s w=%ld n=%ld sink=%lu\n", kind, W, n, sink);
    return 0;
}
