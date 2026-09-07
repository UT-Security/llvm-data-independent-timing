/* clock_probe.c - what does a hard-pinned, busy P-core's clock do over time?
 *
 * Spins on one thread and prints PMC0 cycles over mach_absolute_time every interval:
 * the implied core clock in MHz. Run it the way the benchmark runs, pinned by the
 * injected constructor (root for the bind, PMC reads need the patched kernel):
 *
 *   clang -O2 -o clock_probe clock_probe.c
 *   sudo PIN_CPU=9 DYLD_INSERT_LIBRARIES=<W>/libditctl.dylib ./clock_probe 100 300
 *
 * args: interval_ms (default 100), samples (default 200). If the printed clock dips
 * below 4000 MHz while the loop is running, that is DVFS on a pinned core, not a
 * migration, and cycles-per-op measurements taken then are still valid.
 */
#include <mach/mach_time.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
static inline uint64_t pmc0(void) { uint64_t v; __asm__ volatile("isb\n\tmrs %0, S3_2_c15_c0_0" : "=r"(v) :: "memory"); return v & ((1ULL << 48) - 1); }
int main(int argc, char **argv) {
    int interval_ms = argc > 1 ? atoi(argv[1]) : 100, n = argc > 2 ? atoi(argv[2]) : 200;
    mach_timebase_info_data_t tb; mach_timebase_info(&tb);
    volatile uint64_t sink = 0;
    uint64_t c0 = pmc0(), t0 = mach_absolute_time();
    printf("t_ms  MHz\n");
    for (int i = 1; i <= n; i++) {
        uint64_t target = t0 + (uint64_t)i * interval_ms * 1000000ULL * tb.denom / tb.numer;
        while (mach_absolute_time() < target) sink += sink * 7 + 1;      /* stay busy */
        uint64_t c1 = pmc0(), t1 = mach_absolute_time();
        double ns = (double)(t1 - t0) * tb.numer / tb.denom;
        printf("%5.0f %5.0f\n", ns / 1e6, ((c1 - c0) & ((1ULL << 48) - 1)) / (ns / 1e3));
        c0 = c1; t0 = t1;
    }
    return 0;
}
