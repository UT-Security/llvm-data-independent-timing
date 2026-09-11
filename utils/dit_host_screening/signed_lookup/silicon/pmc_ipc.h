/* Apple-silicon cycles and instructions for the signed-lookup crossover, read
 * straight out of the PMC system registers from EL0.
 *
 * WHY NOT kperf, which is what benchmarks/signed_lookup/kperf_ipc.h does.
 * kperf needs root (kpc_force_all_ctrs_set) and each read is a call into the
 * kperf driver: measured on this M4 at ~3,400 cycles and ~17,700 instructions
 * per region boundary (utils/cio_offset_probe.c, and the note in
 * utils/cio_arm_shim.h). The ROI here is the whole request loop, so that offset
 * is amortised and kperf would in fact have been adequate -- but a kernel
 * patched with PMCR0_USEREN_EN (github.com/jprx/PacmanPatcher, boot-args
 * enable_skstb=1 on this host) makes the same counters readable with one `mrs`,
 * no root, and no driver in the measured process at all. Measured here:
 * 21,000,012 instructions over a 3,000,000-iteration loop, i.e. exact.
 *
 * DROP-IN. The gem5 driver includes "kperf_ipc.h" with a quoted include, so the
 * copy of that name next to the staged driver wins and includes this file. The
 * driver source is therefore byte-identical to the one the gem5 arms run
 * (build_silicon.sh records its sha256), and only the instrument differs.
 *
 * ORDERING, and it is not defensive.
 *   instructions: the `isb` is REQUIRED. A bare `mrs` of the instruction
 *     counter is not ordered against the surrounding work, so the closing read
 *     can execute before the code it is counting has retired. Experiment 09
 *     found this the hard way: an arm ending in a speculation barrier
 *     serialised its own read and its NOP twin did not, and the two objects
 *     disassembling to the same 44 instructions read 1,301 and 393.
 *   cycles: read BARE. An early read of a free-running cycle counter is off by
 *     at most the reorder window, tens of cycles against the millions in this
 *     ROI, and the same skew appears at both boundaries so it cancels. Putting
 *     an `isb` there would instead charge the drain to the measured region.
 *   So: begin = cntvct, isb+PMC1, PMC0.  end = PMC0, isb+PMC1, cntvct.
 *     PMCs innermost, the drains outside the cycle window.
 *
 * THE MIGRATION HAZARD, and how a rootless run stays honest. These are PER-CORE
 * registers. kperf's kpc_get_thread_counters() is per-thread and the kernel
 * carries it across a migration; a bare `mrs` does not, so a thread that moves
 * mid-ROI differences two different cores' counters. `kern.sched_thread_bind_cpu`
 * fixes that and this file calls it, but writing it is root-only (EPERM
 * otherwise) and an unrooted run stays unpinned.
 *
 * What makes an unpinned run usable is that a cross-core delta is not subtly
 * wrong, it is absurd: two cores' counters sit seconds of core time apart. So
 * the ROI is also bracketed by CNTVCT_EL0 (1 GHz on this M4 -- read CNTFRQ, do
 * not assume, and do not use hw.tbfrequency, which reads 24 MHz here) and the
 * IMPLIED CLOCK, cycles / seconds, is reported next to every measurement. A
 * P-core on this part runs 4.30-4.46 GHz (measured over 60 regions, zero
 * excursions); an E-core runs ~2.6; a migration reads hundreds of GHz or
 * negative. The runner gates on it and COUNTS what it rejects -- nothing is
 * dropped silently.
 *
 * The exit line is a diagnostic channel that needs no change to the driver:
 *   PMC exit dit=.. pmc=.. pinned=.. cyc=.. ins=.. ncyc=.. frq=.. ghz=.. bad=..
 */
#ifndef KPERF_IPC_H
#define KPERF_IPC_H
#include <pthread/qos.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>

#define PMC_MASK48(x) ((x) & ((1ULL << 48) - 1))

static inline uint64_t pmc_cyc_raw(void) {   /* cycles, bare */
    uint64_t v;
    __asm__ volatile("mrs %0, S3_2_c15_c0_0" : "=r"(v) :: "memory");
    return PMC_MASK48(v);
}
static inline uint64_t pmc_ins_raw(void) {   /* instructions, ordered */
    uint64_t v;
    __asm__ volatile("isb\n\tmrs %0, S3_2_c15_c1_0" : "=r"(v) :: "memory");
    return PMC_MASK48(v);
}
static inline uint64_t pmc_cntvct(void) {
    uint64_t v;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(v));
    return v;
}
static inline uint64_t pmc_cntfrq(void) {
    uint64_t v;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v));
    return v;
}
static inline unsigned long pmc_dit_get(void) {
    unsigned long d;
    __asm__ volatile("mrs %0, DIT" : "=r"(d));
    return (d >> 24) & 1UL;
}

/* the names the driver calls */
static int      kperf_ok = 0;
static int      pmc_pinned = -1;
static uint64_t pmc_a_cyc, pmc_a_ins, pmc_a_n;
static uint64_t pmc_b_cyc, pmc_b_ins, pmc_b_n;

static sigjmp_buf pmc_jb;
static void pmc_ill(int s) { (void)s; siglongjmp(pmc_jb, 1); }

/* Bind the calling thread to one core. Root-only: this is a development-kernel
 * facility (boot-args enable_skstb=1 is what makes it functional at all), and
 * EPERM is the normal unrooted case, not an error. PMC_PIN_CPU picks the core;
 * the default is the highest index, because Apple silicon numbers the
 * efficiency cluster first and the performance cluster last. QoS goes to
 * USER_INTERACTIVE either way -- that is the P-cluster lever that works on a
 * stock kernel, and the implied-clock gate is what keeps an unpinned run
 * honest. */
static void pmc_pin(void) {
    int want = -1;
    const char *e = getenv("PMC_PIN_CPU");
    if (e) {
        want = (int)strtol(e, NULL, 10);
    } else {
        int n = 0; size_t sz = sizeof n;
        if (sysctlbyname("hw.ncpu", &n, &sz, NULL, 0) == 0 && n > 0)
            want = n - 1;
    }
    if (want < 0) return;
    if (sysctlbyname("kern.sched_thread_bind_cpu", NULL, NULL, &want, sizeof want) == 0)
        pmc_pinned = want;
}

/* SIGILL-safe: on an unpatched kernel the `mrs` traps, and the probe reports
 * no counters rather than killing the run. A patched-but-disabled PMCR0 reads a
 * frozen zero, so movement is required, not just the absence of a trap. */
static int kperf_init(void) {
    struct sigaction sa, old;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = pmc_ill;
    sigaction(SIGILL, &sa, &old);
    if (sigsetjmp(pmc_jb, 1) == 0) {
        uint64_t a = pmc_cyc_raw(), b = pmc_ins_raw();
        kperf_ok = (a != 0 || b != 0);
    } else {
        kperf_ok = 0;
    }
    sigaction(SIGILL, &old, NULL);
    /* QoS unconditionally; pinning only matters once per-core counters are read. */
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    if (kperf_ok) pmc_pin();
    return kperf_ok;
}

/* cntvct outermost, PMCs innermost; instructions before cycles at the start so
 * the isb drain precedes the cycle snapshot, and after cycles at the end so it
 * lands outside the cycle window. */
static inline void kperf_begin(void) {
    if (!kperf_ok) return;
    pmc_a_n   = pmc_cntvct();
    pmc_a_ins = pmc_ins_raw();
    pmc_a_cyc = pmc_cyc_raw();
}
static inline void kperf_end(void) {
    if (!kperf_ok) return;
    pmc_b_cyc = pmc_cyc_raw();
    pmc_b_ins = pmc_ins_raw();
    pmc_b_n   = pmc_cntvct();
}
static inline uint64_t kperf_cycles(void) { return kperf_ok ? pmc_b_cyc - pmc_a_cyc : 0; }
static inline uint64_t kperf_insts(void)  { return kperf_ok ? pmc_b_ins - pmc_a_ins : 0; }

/* The runner reads this line. `bad` is this process's own opinion of its
 * sample: a cross-core delta shows up as an implied clock nowhere near a P-core's.
 * It is REPORTED, never acted on here -- the runner counts rejects next to the
 * numbers rather than quietly dropping them. */
__attribute__((destructor)) static void pmc_report(void) {
    double ghz = 0.0;
    int bad = 1;
    uint64_t cyc = 0, ins = 0, ncyc = 0, frq = kperf_ok ? pmc_cntfrq() : 0;
    if (kperf_ok && pmc_b_cyc > pmc_a_cyc && pmc_b_ins >= pmc_a_ins && pmc_b_n > pmc_a_n && frq) {
        cyc = pmc_b_cyc - pmc_a_cyc;
        ins = pmc_b_ins - pmc_a_ins;
        ncyc = pmc_b_n - pmc_a_n;
        ghz = (double)cyc / ((double)ncyc / (double)frq) / 1e9;
        bad = !(ghz > 3.4 && ghz < 5.0);
    }
    fprintf(stderr, "PMC exit dit=%lu pmc=%d pinned=%d cyc=%llu ins=%llu ncyc=%llu "
                    "frq=%llu ghz=%.4f bad=%d\n",
            pmc_dit_get(), kperf_ok, pmc_pinned,
            (unsigned long long)cyc, (unsigned long long)ins,
            (unsigned long long)ncyc, (unsigned long long)frq, ghz, bad);
}

#endif /* KPERF_IPC_H */
