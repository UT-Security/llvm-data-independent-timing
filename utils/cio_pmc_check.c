/*
 * cio_pmc_check.c - can this machine read Apple's PMCs from userspace?
 *
 *   clang -O1 -o /tmp/pmc_check utils/cio_pmc_check.c && /tmp/pmc_check
 *
 * Run this BEFORE using PMC=1 with utils/taint_libsodium_sudo_run.sh. It exits 0
 * when the counters are usable and non-zero when they are not, so it also works
 * as a gate in a script.
 *
 * WHAT IT IS FOR. The rooted rig reads counters through kperf, which costs about
 * 3,400 cycles and 17,700 instructions per region boundary. That is fine on a
 * 35,000-cycle ed25519 signature and hopeless on a 443-cycle AES-GCM encrypt,
 * where it is several times the thing being measured: absolute IPC off those
 * counters comes back as the INSTRUMENT's IPC, not the library's.
 *
 * A kernel patched with PMCR0_USEREN_EN (bit 30 of PMCR0_EL1) lets EL0 read
 * Apple's two fixed counters directly -- PMC0 (S3_2_c15_c0_0, cycles) and PMC1
 * (S3_2_c15_c1_0, instructions retired) -- for the price of one `mrs`. See
 * github.com/jprx/PacmanPatcher, which needs SIP off, a matching KDK, a patched
 * kernel collection and a boot into 1TR. It is not the default state of a Mac,
 * which is exactly why this check exists: PMC=1 on an unpatched machine falls
 * back to kperf silently and by design, so nothing breaks -- and nothing tells
 * you that you did not get what you asked for either.
 *
 * TWO THINGS IT VERIFIES BEYOND "the register reads":
 *
 *   The reads must be ORDERED. A bare `mrs` of a PMC is not ordered against
 *   surrounding work, so a region-end read can execute before the code it is
 *   measuring has retired. This is not theoretical: measured on an AES-GCM
 *   encrypt, the Apple-bracket arm read 1,301 instructions and its
 *   instruction-matched NOP twin read 393 -- a 3x gap between two objects that
 *   disassemble to the same 44 instructions. The bracket's own `sb` serialised
 *   the read after it; the twin, with `nop` there instead, had nothing. The arm
 *   with a speculation barrier measured itself honestly and its control did not.
 *   An `isb` before each read fixes it, and this program reports its cost.
 *
 *   NOPs must RETIRE. Every layout control in this rig is an arm with `nop`
 *   where the real one has a mode switch, and the control is only sound if the
 *   two retire the same count. Measured here: they do, exactly, while 16 nops
 *   cost one cycle -- retired but free, which is the property the control needs.
 */
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define M48(x) ((x) & ((1ULL << 48) - 1))
#define PMCR0_USEREN_EN (1u << 30)

static sigjmp_buf jb;
static void on_ill(int s) { (void)s; siglongjmp(jb, 1); }

static inline uint64_t pmc0(void) {   /* cycles */
    uint64_t v; __asm__ volatile("isb\n\tmrs %0, S3_2_c15_c0_0" : "=r"(v) :: "memory"); return M48(v);
}
static inline uint64_t pmc1(void) {   /* instructions retired */
    uint64_t v; __asm__ volatile("isb\n\tmrs %0, S3_2_c15_c1_0" : "=r"(v) :: "memory"); return M48(v);
}
static inline uint64_t pmc0_raw(void) {
    uint64_t v; __asm__ volatile("mrs %0, S3_2_c15_c0_0" : "=r"(v) :: "memory"); return M48(v);
}
static inline uint64_t cntvct(void) {
    uint64_t v; __asm__ volatile("mrs %0, cntvct_el0" : "=r"(v) :: "memory"); return v;
}

static volatile int sink;
#define NOP1  "nop\n\t"
#define NOP16 NOP1 NOP1 NOP1 NOP1 NOP1 NOP1 NOP1 NOP1 NOP1 NOP1 NOP1 NOP1 NOP1 NOP1 NOP1 NOP1

int main(void) {
    struct sigaction sa, old;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_ill;
    sigaction(SIGILL, &sa, &old);

    puts("Apple PMC userspace access check\n");

    /* 1. Do the registers read at all? */
    if (sigsetjmp(jb, 1) != 0) {
        sigaction(SIGILL, &old, NULL);
        puts("  PMC0/PMC1        SIGILL - not readable from EL0");
        puts("\nVERDICT: NOT available. PMC=1 would silently fall back to kperf.");
        puts("  To enable: github.com/jprx/PacmanPatcher (SIP off, matching KDK,");
        puts("  patched kernel collection, boot into 1TR). Until then use the");
        puts("  rooted kperf path and remember its ~3,400-cycle region offset.");
        return 1;
    }
    uint64_t c_a = pmc0(), i_a = pmc1();

    /* PMCR0 itself is EL1; readable only on a patched kernel, so treat a trap
     * here as informational rather than fatal -- the counters are what matter. */
    unsigned long long pmcr0 = 0; int have_pmcr0 = 1;
    if (sigsetjmp(jb, 1) == 0)
        __asm__ volatile("mrs %0, S3_1_c15_c0_0" : "=r"(pmcr0));
    else
        have_pmcr0 = 0;
    sigaction(SIGILL, &old, NULL);

    printf("  PMC0 (cycles)    readable, %llu\n", (unsigned long long)c_a);
    printf("  PMC1 (instrs)    readable, %llu\n", (unsigned long long)i_a);
    if (have_pmcr0)
        printf("  PMCR0_EL1        0x%llx, USEREN_EN %s\n", pmcr0,
               (pmcr0 & PMCR0_USEREN_EN) ? "SET" : "CLEAR");
    else
        puts("  PMCR0_EL1        not readable (fine: the counters are what matter)");

    /* 2. Do they COUNT, and at a sane rate? A patched-but-unconfigured PMCR0
     *    reads a frozen value, which is worse than a trap: it looks like data. */
    uint64_t c0 = pmc0(), i0 = pmc1(), t0 = cntvct();
    for (volatile int k = 0; k < 3000000; k++) sink += k;
    uint64_t c1 = pmc0(), i1 = pmc1(), t1 = cntvct();
    double cyc = (double)(c1 - c0), ins = (double)(i1 - i0), ns = (double)(t1 - t0);
    if (cyc <= 0 || ins <= 0) {
        puts("\nVERDICT: registers read but DO NOT COUNT (frozen).");
        puts("  The patch is present but the counters are not configured. Do not");
        puts("  use PMC=1: a frozen counter produces plausible-looking zeros.");
        return 2;
    }
    printf("\n  workload         %.0f cycles, %.0f instrs, %.0f ns"
           " -> %.2f GHz, IPC %.2f\n", cyc, ins, ns, cyc / ns, ins / cyc);

    /* 3. The cost of a read, ordered and not. The isb is required for
     *    correctness; this says what it costs so the offset is known, not
     *    guessed. Minimum over 1000, because we want the floor, not the tail. */
    uint64_t bare = ~0ULL, ordered = ~0ULL;
    for (int k = 0; k < 1000; k++) { uint64_t a = pmc0_raw(), b = pmc0_raw(); if (b - a < bare) bare = b - a; }
    for (int k = 0; k < 1000; k++) { uint64_t a = pmc0(),     b = pmc0();     if (b - a < ordered) ordered = b - a; }
    printf("  read cost        %llu cycles bare, %llu cycles with isb"
           " (kperf is ~3,400)\n", (unsigned long long)bare, (unsigned long long)ordered);

    /* 4. Do nops retire? Every NOP control in this rig depends on it. */
    uint64_t n0 = pmc1();
    for (int k = 0; k < 100000; k++) { __asm__ volatile("" ::: "memory"); sink = k; }
    uint64_t n1 = pmc1();
    uint64_t m0 = pmc1();
    for (int k = 0; k < 100000; k++) { __asm__ volatile(NOP16 ::: "memory"); sink = k; }
    uint64_t m1 = pmc1();
    double per = (double)((m1 - m0) - (n1 - n0)) / 100000.0;
    printf("  nops retire      %.2f extra instrs for 16 nops/iter (want ~16)\n", per);
    int nop_ok = (per > 15.0 && per < 17.0);

    puts("");
    if (!nop_ok) {
        puts("VERDICT: counters work, but nops did not retire as expected.");
        puts("  The NOP layout controls may not be instruction-matched on this");
        puts("  core. Cycle numbers are still fine; treat instruction counts and");
        puts("  IPC from the NOP arms with suspicion.");
        return 3;
    }
    puts("VERDICT: PMC access is AVAILABLE and sane. PMC=1 will use it.");
    puts("  Reads must stay isb-ordered (the shim does this). Note the counters");
    puts("  are PER-CORE, not per-thread: a thread migration mid-region gives a");
    puts("  delta spanning two cores, so check reg_drop is 0 on a PMC run.");
    return 0;
}
