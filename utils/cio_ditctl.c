/*
 * libditctl.dylib - arm selector and counter instrument for the injected-constructor rigs.
 *
 * CIO's eval drivers know nothing about PSTATE.DIT: their mitigation is
 * instruction substitution, so there is no mode to set. We need a blanket-DIT
 * arm anyway, and the drivers must stay byte-identical across arms or the
 * comparison is confounded. So the mode is set from OUTSIDE the program, by a
 * constructor in an injected dylib, and every arm injects this same dylib and
 * differs only in the environment:
 *
 *   ENABLE_DIT=1  -> msr dit, #1 before main, never cleared  (blanket)
 *   ENABLE_DIT=0  -> nothing                                  (every other arm)
 *
 * It also raises QoS to USER_INTERACTIVE for P-cluster residency, the same as
 * libqospin.dylib -- CIO used `taskset -c 0`, which has no macOS equivalent.
 *
 * The destructor reads PSTATE.DIT back and reports it. That is a validity gate,
 * not decoration: the blanket arm must exit with dit=1, and every selectively
 * placed arm must exit with dit=0. An arm that exits with dit=1 when it should
 * not has leaked the mode past an unbalanced exit and is blanket in disguise.
 *
 * WHOLE-PROCESS CYCLES AND INSTRUCTIONS (added 2026-09-06). Experiment 11 runs
 * one php-cgi process per request, so the process IS the measured region, and
 * the metric was the parent's rusage CPU time. That instrument is the weakest
 * part of the experiment: rusage is charged in scheduler ticks, it cannot see
 * instructions, and without instructions there is no way to separate DWELL (the
 * mode slowing execution down) from SWITCHES (the mode costing extra work) --
 * which is the entire cost model the paper argues.
 *
 * A kernel patched with PMCR0_USEREN_EN (bit 30) -- github.com/jprx/PacmanPatcher,
 * boot-args enable_skstb=1 on this host -- lets EL0 read Apple's fixed counters
 * out of the system registers with no root and no driver call. Two `mrs` at each
 * end of the process bracket everything from before `main` to after the last
 * atexit handler. Same instrument as experiment 09's cio_arm_shim.h, at
 * whole-process granularity instead of per-region, and the comments there
 * explain every choice repeated below.
 *
 *   DITCTL_PMC=0   turn the counters off and keep the old rusage-only behaviour.
 *   DITCTL_PIN=0   do not attempt to bind the thread to a core.
 *   DITCTL_PIN_CPU pick the core (default: the highest index, a P-core, since
 *                  Apple silicon numbers the efficiency cluster first).
 */
#include <pthread/qos.h>
#include <sys/sysctl.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned long dit_get(void) {
    unsigned long d;
    __asm__ volatile("mrs %0, DIT" : "=r"(d));
    return (d >> 24) & 1UL;
}

/* ---------------------------------------------------------------- counters */
#define PMC48(x) ((x) & ((1ULL << 48) - 1))

/* CYCLES: read BARE. An early read of a free-running cycle counter is off by at
 * most the reorder window, tens of cycles against a 150-million-cycle process,
 * and the same skew appears at both boundaries so it cancels.
 *
 * INSTRUCTIONS: the `isb` is REQUIRED. A bare `mrs` of the instruction counter
 * is not ordered against the surrounding work, so the closing read can execute
 * before the code it is supposed to be counting has retired and the sample comes
 * back short -- hundreds of instructions, and asymmetrically across arms, since
 * an arm that ends in a speculation barrier serialises its own read and an arm
 * with `hint #0` in that place does not. That is how the bug was found in
 * experiment 09, on the bracket arm against its own NOP twin. */
static inline unsigned long long pmc0(void) {   /* cycles, bare */
    unsigned long long v;
    __asm__ volatile("mrs %0, S3_2_c15_c0_0" : "=r"(v) :: "memory");
    return PMC48(v);
}
static inline unsigned long long pmc1(void) {   /* instructions, ordered */
    unsigned long long v;
    __asm__ volatile("isb\n\tmrs %0, S3_2_c15_c1_0" : "=r"(v) :: "memory");
    return PMC48(v);
}
static inline unsigned long long cntvct(void) {
    unsigned long long v;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(v));
    return v;
}
/* Reported rather than assumed. The virtual counter runs at 1 GHz on this M4
 * and at 24 MHz on other Apple parts, and it is NOT hw.tbfrequency, which reads
 * 24000000 here and would put the implied core clock out by 41x. The consumer
 * needs the real rate to turn ns into an implied clock and gate on it. */
static inline unsigned long long cntfrq(void) {
    unsigned long long v;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v));
    return v;
}

static int ditctl_pmc_ok = 0;
static sigjmp_buf ditctl_jb;
static void ditctl_ill(int s) { (void)s; siglongjmp(ditctl_jb, 1); }

/* SIGILL-safe: on an unpatched kernel the `mrs` traps, and the probe selects
 * "no counters" rather than killing every request in the run. */
static void ditctl_pmc_probe(void) {
    struct sigaction sa, old;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = ditctl_ill;
    sigaction(SIGILL, &sa, &old);
    if (sigsetjmp(ditctl_jb, 1) == 0) {
        unsigned long long a = pmc0(), b = pmc1();
        ditctl_pmc_ok = (a != 0 || b != 0);   /* patched-but-disabled reads a frozen zero */
    } else {
        ditctl_pmc_ok = 0;
    }
    sigaction(SIGILL, &old, NULL);
}

/* ------------------------------------------------------------------ pinning */
/* The PMCs are PER-CORE registers, not per-thread. A process that migrates
 * between its two snapshots differences two different cores' counters, and on
 * this host those counters sit seconds of core time apart, so the delta comes
 * back absurd or negative. Measured on a 35 ms process before this was written:
 * one run in eight migrated.
 *
 * `kern.sched_thread_bind_cpu` binds the calling thread and makes the hazard go
 * away, but it is a development-kernel facility and writing it is root-only
 * (EPERM otherwise). An unrooted run stays unpinned and says so; the consumer
 * gates on the implied clock instead and drops what migrated. QoS goes to
 * USER_INTERACTIVE either way -- that is the P-cluster lever that works on a
 * stock kernel, and it is what keeps an unpinned run honest. */
static int ditctl_pinned = -1;

static void ditctl_pin(void) {
    int want = -1;
    const char *e = getenv("DITCTL_PIN_CPU");
    if (e) {
        want = (int)strtol(e, NULL, 10);
    } else {
        int n = 0; size_t sz = sizeof n;
        if (sysctlbyname("hw.ncpu", &n, &sz, NULL, 0) == 0 && n > 0)
            want = n - 1;              /* last index: performance cluster */
    }
    if (want < 0)
        return;
    if (sysctlbyname("kern.sched_thread_bind_cpu", NULL, NULL, &want, sizeof want) == 0)
        ditctl_pinned = want;
}

/* --------------------------------------------------------------- the window */
static unsigned long long t0_cyc, t0_ins, t0_ns;
static int off_env(const char *k) { const char *v = getenv(k); return v && v[0] == '0'; }

__attribute__((constructor)) static void ditctl_start(void) {
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    if (!off_env("DITCTL_PMC")) {
        ditctl_pmc_probe();
        if (ditctl_pmc_ok && !off_env("DITCTL_PIN"))
            ditctl_pin();              /* pinning only matters once the per-core counters are read */
    }
    const char *e = getenv("ENABLE_DIT");
    if (e && e[0] == '1')
        __asm__ volatile("msr dit, #1\n\tisb" ::: "memory");
    /* LAST, so the mode is already set and nothing above lands in the window.
     * Instructions first (its isb drains), cycles last, so the drain precedes
     * the cycle snapshot instead of sitting inside the window. */
    if (ditctl_pmc_ok) {
        t0_ns  = cntvct();
        t0_ins = pmc1();
        t0_cyc = pmc0();
    }
}

__attribute__((destructor)) static void ditctl_end(void) {
    unsigned long long cyc = 0, ins = 0, ns = 0;
    if (ditctl_pmc_ok) {
        /* Cycles first (bare, cheap), instructions after: the closing drain then
         * lands outside the cycle window and inside the instruction one, which is
         * where it belongs. */
        unsigned long long c1 = pmc0(), i1 = pmc1(), n1 = cntvct();
        if (c1 > t0_cyc && i1 >= t0_ins) { cyc = c1 - t0_cyc; ins = i1 - t0_ins; }
        if (n1 > t0_ns) ns = n1 - t0_ns;
    }
    fprintf(stderr, "DITCTL exit dit=%lu pmc=%d pinned=%d cyc=%llu ins=%llu ns=%llu frq=%llu\n",
            dit_get(), ditctl_pmc_ok, ditctl_pinned, cyc, ins, ns,
            ditctl_pmc_ok ? cntfrq() : 0ULL);
}
