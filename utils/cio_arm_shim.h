/*
 * cio_arm_shim.h - compiled into EVERY benchmark binary, identical in every arm.
 *
 * WHY IT EXISTS: `sudo` strips DYLD_* from the environment, so the injected
 * libqospin/libditctl dylibs that supply P-core residency and the blanket-DIT
 * arm silently stop working under a rooted (kperf) run. Everything they did has
 * to be compiled in instead. It is force-included with -include, so the driver
 * SOURCES are still untouched and still byte-identical across arms -- only the
 * libsodium archive and the environment differ.
 *
 *   SHIM_DIT=1   set PSTATE.DIT before main and never clear it (blanket arm).
 *                Used only for CIO's drivers, which have no DIT support of their
 *                own. Our own drivers keep their built-in dit_enable(), which
 *                wraps only the measured region -- do not set both, or "blanket"
 *                means two different things in two rigs.
 *
 * QoS is raised unconditionally: on Apple silicon QOS_CLASS_USER_INTERACTIVE is
 * the supported P-cluster lever (kern.sched_thread_bind_cpu does not exist on a
 * stock kernel, which is why the old libcpupin.dylib never bound anything).
 *
 * The destructor reports PSTATE.DIT at exit on stderr. That is a GATE: the
 * blanket arm must read 1 and every selectively placed arm must read 0. An arm
 * that exits with DIT set leaked the mode past an unbalanced exit and is blanket
 * in disguise -- exactly the tail-call bug, which went unnoticed for months.
 */
#ifndef CIO_ARM_SHIM_H
#define CIO_ARM_SHIM_H

#include <pthread/qos.h>
#include <sys/sysctl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef CIO_SHIM_PMC
#include <setjmp.h>     /* the SIGILL-safe PMC probe */
#include <signal.h>
#include <string.h>
#endif

/*
 * CYCLE SOURCE. CIO's drivers time ONE crypto operation per iteration, and on
 * this machine CNTVCT_EL0 advances in ~41.67 ns steps -- an AES-256-GCM encrypt
 * of a 100-byte message measures 42 ticks, i.e. one or two counter increments.
 * The mean over their 1000 iterations is unbiased (the start phase is uniform
 * mod the step), but the per-sample noise is enormous on the fast primitives.
 *
 * kperf reads the real cycle counter at 1-cycle resolution and needs root, which
 * is the whole reason for the sudo run. Build with -DCIO_SHIM_KPERF and
 * -I<crypto-dit-benchmarks> to arm it; without root perf_init() fails and this
 * falls back to CNTVCT_EL0 automatically, so the same binary works either way.
 * Which one was used is printed at exit and recorded with the results.
 */
#ifdef CIO_SHIM_KPERF
#include "perf.c"
static int cio_shim_kperf_ok = 0;
#endif

/* Timed-region-only counter accumulation.
 *
 * WHY: whole-process totals are the WRONG instrument for a claim about the
 * measured call. On CIO's drivers the timed crypto is only 21-30% of process
 * cycles (the rest is the driver loop, randombytes_buf for the nonce, the
 * sanity-check decrypt, malloc). Measured 2026-09-01: whole-process cycles said
 * blanket DIT was 5-9% FASTER while the timed region said 1-4% SLOWER -- opposite
 * signs on 5 of 6 benchmarks. argon2id, where the timed work IS 80% of the
 * process, was the only one that agreed. Any counter statement about the crypto
 * has to come from inside the timer.
 *
 * These accumulate across iterations as a SIDE EFFECT of the timer macros, so
 * CIO's driver sources still do not change and stay byte-identical across arms.
 */
static unsigned long long cio_shim_reg_cyc = 0, cio_shim_reg_ins = 0;
static unsigned long long cio_shim_t0_cyc = 0, cio_shim_t0_ins = 0;
static unsigned long long cio_shim_t0_timer = 0;   /* what the DRIVER differences */
static unsigned long long cio_shim_reg_n = 0;

static inline uint64_t cio_shim_instrs(void) {
#ifdef CIO_SHIM_KPERF
    if (cio_shim_kperf_ok) {
        u64 buf[KPC_MAX_COUNTERS] = { 0 };
        if (kpc_get_thread_counters(0, KPC_MAX_COUNTERS, buf) == 0)
            return buf[perf_counter_map[1]];   /* profile_events[1] == instructions */
    }
#endif
    return 0;
}

static inline uint64_t cio_shim_cycles(void) {
#ifdef CIO_SHIM_KPERF
    if (cio_shim_kperf_ok) {
        u64 buf[KPC_MAX_COUNTERS] = { 0 };
        if (kpc_get_thread_counters(0, KPC_MAX_COUNTERS, buf) == 0)
            return buf[perf_counter_map[0]];   /* profile_events[0] == cycles */
    }
#endif
    uint64_t c;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(c));
    return c;
}

/* DIRECT PMC READS (PMC0 = cycles, PMC1 = instructions), when EL0 may have them.
 *
 * A kernel patched with PMCR0_USEREN_EN (bit 30) -- github.com/jprx/PacmanPatcher
 * -- lets EL0 read Apple's fixed counters straight out of the system registers.
 * That is the whole instrumentation problem solved rather than corrected:
 *
 *              per-region offset          measured on this M4
 *   kperf      ~3,400 cycles / ~17,700 instructions   (a call into the driver)
 *   PMC        1 cycle / 0 instructions               (one `mrs`)
 *
 * On a 291-cycle AES-GCM op the kperf pair is 12x the thing being measured, so
 * absolute IPC off those counters is really the instrument's IPC (it reads 5.06
 * where the truth is 4.10) and every percentage is compressed. With PMC reads
 * the numbers need no offset correction at all, and the counters are exact:
 * 33,000,004 instructions over a 3,000,000-iteration loop, reproducibly.
 *
 * It also needs NO ROOT, since the patch grants EL0 access -- so a rooted run is
 * no longer the only way to get cycles.
 *
 * THE CATCH, and why kperf stays the default. These are PER-CORE registers, not
 * per-thread: kpc_get_thread_counters() accumulates across a migration and a
 * bare `mrs` does not. If the thread moves mid-region the delta is garbage --
 * usually negative, which the guard in region_end() already drops, but a move
 * between two P-cores can also produce a plausible-looking wrong number. The
 * rig pins QOS to USER_INTERACTIVE and gates on P-cluster residency, which makes
 * this unlikely, not impossible. So it is opt-in (CIO_SHIM_PMC=1), and
 * region_end() additionally rejects a sample whose cycle delta is absurd.
 *
 * Availability is probed once, SIGILL-safe: on an unpatched kernel the `mrs`
 * traps, and the probe selects kperf instead of killing the run. */
#ifdef CIO_SHIM_PMC
static int cio_shim_pmc_ok = 0;
static sigjmp_buf cio_shim_pmc_jb;
static void cio_shim_pmc_ill(int s) { (void)s; siglongjmp(cio_shim_pmc_jb, 1); }
#define CIO_PMC48(x) ((x) & ((1ULL << 48) - 1))
/* The `isb` is REQUIRED, not defensive. A bare `mrs` of a PMC is not ordered
 * against the surrounding work: the region-end read can execute before the code
 * it is supposed to be measuring has retired, and the sample comes back short.
 * This is not theoretical -- it is how the bug was found. Measured on an
 * AES-GCM encrypt, the Apple-bracket arm read 1,301 instructions and its
 * instruction-matched NOP twin read 393, a 3x gap between two objects that
 * disassemble to the same 44 instructions. The difference was the bracket's own
 * `sb`, which happened to serialise the read that followed it; the twin, with
 * `nop` in its place, had nothing to stop the read floating up. So the arm with
 * a speculation barrier measured itself honestly and the one without did not,
 * which is the worst possible failure mode for a layout control.
 *
 * kperf never showed this because a call into the kperf driver serialises by
 * construction; going to a two-cycle `mrs` is what exposed it. */
/* CYCLES: read BARE, no isb. The ordering hazard is asymmetric and it was a
 * mistake to generalise it from the instruction counter to this one.
 *
 * An early read of the INSTRUCTION counter loses every instruction that has not
 * yet retired -- hundreds of them, measured: 5,778 against a true 6,007. An
 * early read of a free-running CYCLE counter is off by at most the reorder
 * window, tens of cycles, and the same skew appears at both boundaries so it
 * largely cancels in the delta. Measured against CNTVCT over work sizes from
 * 200 to 2,000,000 iterations, a bare PMC0 read holds a constant 4.40-4.43
 * ratio -- i.e. it IS the cycle count, at every scale.
 *
 * Reading it bare removes the drain from the cycle window entirely, and with it
 * the need to convert CNTVCT time into cycles at an assumed clock. That clock
 * was the last soft spot in the IPC number: measured per benchmark it is 4.246
 * GHz on argon2id and 4.450 on ed25519, a 4.8% spread that propagates straight
 * into any IPC computed with a single constant. Now nothing is assumed. */
static inline unsigned long long cio_shim_pmc0(void) {   /* cycles, bare */
    unsigned long long v;
    __asm__ volatile("mrs %0, S3_2_c15_c0_0" : "=r"(v) :: "memory");
    return CIO_PMC48(v);
}
static inline unsigned long long cio_shim_pmc1(void) {   /* instructions */
    unsigned long long v;
    __asm__ volatile("isb\n\tmrs %0, S3_2_c15_c1_0" : "=r"(v) :: "memory");
    return CIO_PMC48(v);
}
static void cio_shim_pmc_probe(void) {
    struct sigaction sa, old;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = cio_shim_pmc_ill;
    sigaction(SIGILL, &sa, &old);
    if (sigsetjmp(cio_shim_pmc_jb, 1) == 0) {
        unsigned long long a = cio_shim_pmc0(), b = cio_shim_pmc1();
        /* A patched-but-disabled PMCR0 reads a frozen zero; require movement. */
        cio_shim_pmc_ok = (a != 0 || b != 0);
    } else {
        cio_shim_pmc_ok = 0;
    }
    sigaction(SIGILL, &old, NULL);
}
#endif

/* SAMPLED PMC ACCUMULATION.
 *
 * The isb that makes a PMC read correct is a pipeline drain, and per-op it is
 * not cheap: measured 73 cycles on a synthetic op and 150-250 on the AEADs,
 * against the ~21 cycles CNTVCT costs. Two separate problems come out of that,
 * and only one of them is about the numbers:
 *
 *   the RUN is perturbed. Every operation pays the drain, which on a 264-cycle
 *   AES-GCM encrypt is 40% more work, and it lands between the driver's
 *   iterations where it disturbs exactly the predictor and cache state the
 *   experiment is trying to measure. Worse, it need not disturb every arm
 *   equally.
 *
 *   the SAMPLE is biased. Cycles counted between two isb-ordered reads include
 *   the drain; instructions do not (measured: -0.0 instructions, the drain
 *   retires the op's own tail and nothing new). So IPC off per-op PMC reads is
 *   understated by ~22%.
 *
 * Sampling fixes the first, which is the one that can silently corrupt an arm
 * comparison: 1 region in CIO_PMC_SAMPLE (default 64) is instrumented, and the
 * other 63 run untouched. The second is handled by measuring rather than
 * guessing -- cio_shim_pmc_offset() times a null region at startup and the
 * value is reported as pmc_off, so a consumer can subtract a number that was
 * measured in this process rather than inferred from another machine.
 *
 * Instruction counts need neither correction and are exact either way. */
static unsigned long long cio_shim_samp_cyc = 0, cio_shim_samp_ins = 0;
static unsigned long long cio_shim_samp_n = 0, cio_shim_samp_seq = 0;
static unsigned long long cio_shim_samp_every = 64;
/* Skip the first regions outright. Sampling 1 in 64 gives only ~16 samples over
 * a 1025-iteration driver, so ONE cold sample dominates the mean -- and region 0
 * is the coldest call there is: lazy initialisation, first-call resolution, an
 * empty cache. Measured with no skip, aes256gcm-encrypt reported 2,584
 * instructions per op against a true 1,275, because sample 0 carried ~20,000
 * one-time instructions spread over 16 samples. CIO's drivers warm up 25
 * iterations; a full sampling period covers that with room to spare. */
static unsigned long long cio_shim_samp_skip = 64;
static unsigned long long cio_shim_pmc_off = 0;
static int cio_shim_samp_live = 0;   /* is THIS region a sampled one? */
static unsigned long long cio_shim_s0_cyc = 0, cio_shim_s0_ins = 0;

/* PIN THE THREAD TO ONE CORE.
 *
 * The PMCs are per-core registers. kperf's kpc_get_thread_counters() is
 * per-THREAD and the kernel carries it across a migration; a bare `mrs` is not,
 * so a thread that moves between a region's two snapshots differences two
 * different cores' counters. region_end() drops the deltas that come out
 * obviously broken, but a move between two cores whose counters happen to sit
 * close produces a plausible wrong number and is not detectable at all. The
 * only real fix is to stop migrating.
 *
 * This kernel can do that: `kern.sched_thread_bind_cpu` exists and boot-args
 * carry enable_skstb=1, which is what makes it functional. It is a development
 * kernel facility, so writing it is root-only (EPERM otherwise) -- an unrooted
 * run simply stays unpinned and says so, which is the pre-existing behaviour.
 *
 * QoS still goes to USER_INTERACTIVE either way: that is the P-cluster lever
 * that works on a stock kernel, and it is what keeps an unpinned run honest.
 *
 * CIO_PIN_CPU picks the core. The default is the highest-numbered one, because
 * Apple silicon numbers the efficiency cluster first and the performance
 * cluster last, so the last index is a P-core on every part this runs on. The
 * residency gate checks the achieved clock afterwards, so a wrong guess shows
 * up as a gate failure rather than as quietly slow numbers. */
static int cio_shim_pinned = -1;

static void cio_shim_pin(void) {
    int want = -1;
    const char *e = getenv("CIO_PIN_CPU");
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
        cio_shim_pinned = want;
    /* EPERM (not root) is the normal case and not an error: leave it unpinned. */
}

static unsigned long cio_shim_dit_get(void) {
    unsigned long d;
    __asm__ volatile("mrs %0, DIT" : "=r"(d));
    return (d >> 24) & 1UL;
}

__attribute__((constructor)) static void cio_shim_start(void) {
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#ifdef CIO_SHIM_PMC
    cio_shim_pmc_probe();   /* before kperf: a patched kernel makes kperf redundant */
    if (cio_shim_pmc_ok) {
        const char *sv = getenv("CIO_PMC_SAMPLE");
        if (sv) { unsigned long long v = strtoull(sv, NULL, 10); if (v) cio_shim_samp_every = v; }
        const char *kv = getenv("CIO_PMC_SKIP");
        if (kv) cio_shim_samp_skip = strtoull(kv, NULL, 10);
        /* What a null region costs, measured HERE rather than carried over from
         * another machine: the floor of an empty begin/end pair. It is a floor,
         * not the whole story -- the drain lengthens with what is in flight --
         * so it under-corrects a busy region and is reported, never applied. */
        unsigned long long best = ~0ULL;
        for (int k = 0; k < 200; k++) {
            unsigned long long a = cio_shim_pmc0();
            unsigned long long b = cio_shim_pmc0();
            if (b - a < best) best = b - a;
        }
        cio_shim_pmc_off = best;
    }
#endif
#ifdef CIO_SHIM_KPERF
    /* needs root; failure is not an error, it selects the CNTVCT fallback */
    cio_shim_kperf_ok = (perf_init("cioparity") == 0) && (perf_start() == 0);
#endif
#ifdef CIO_SHIM_PMC
    /* after the PMC probe: pinning only matters when the per-core counters are
     * the ones being read. */
    if (cio_shim_pmc_ok) cio_shim_pin();
#endif
    const char *e = getenv("SHIM_DIT");
    if (e && e[0] == '1')
        __asm__ volatile("msr dit, #1\n\tisb" ::: "memory");
}

/* called by the timer macros in eval_util.h */
/* ONE counter read per boundary, not two.
 *
 * The first version read instructions and cycles in separate
 * kpc_get_thread_counters() calls, so the instruction window bracketed two extra
 * calls that the cycle window did not. Cycles stayed correct (they matched the
 * timed mean) but instruction counts carried a large constant, and the resulting
 * "IPC" read 12.0 and 14.2 on an 8-wide core -- impossible, and caught only
 * because the number was absurd. Differences across arms still cancelled it, so
 * the switch-count and cycles-per-switch results were unaffected; absolute IPC
 * was not. One read per boundary puts both counters on the same window. */

static unsigned long long cio_shim_reg_drop = 0;


/* NOTE: no PMC path here on purpose. This runs on EVERY region boundary, and an
 * isb-ordered PMC read costs a pipeline drain (73-250 cycles); paying it per op
 * is exactly what the sampled accumulator exists to avoid. PMC counters come
 * from samp_cyc/samp_ins instead, on 1 region in CIO_PMC_SAMPLE. reg_* stays
 * the cheap per-op series: kperf when rooted, CNTVCT otherwise. */
static inline void cio_shim_read2(unsigned long long *cyc, unsigned long long *ins) {
#ifdef CIO_SHIM_KPERF
    if (cio_shim_kperf_ok) {
        u64 buf[KPC_MAX_COUNTERS] = { 0 };
        if (kpc_get_thread_counters(0, KPC_MAX_COUNTERS, buf) == 0) {
            *cyc = buf[perf_counter_map[0]];
            *ins = buf[perf_counter_map[1]];
            return;
        }
    }
#endif
    unsigned long long c;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(c));
    *cyc = c; *ins = 0;
}

static inline uint64_t cio_shim_cntvct(void) {
    uint64_t c;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(c));
    return c;
}

/* CHEAP TIMER (-DCIO_SHIM_CHEAP_TIMER), and why it exists.
 *
 * kpc_get_thread_counters() is a call into the kperf driver, not a register
 * read, and the counter is sampled part-way through it. So the tail of the
 * opening call and the head of the closing one land BETWEEN the two timestamps
 * and are charged to the crypto. Measured on M4 2026-09-02 with
 * utils/cio_offset_probe.c: 3234 cycles per region, additive (slope 0.9989
 * against a known payload), stable to +/-0.3%.
 *
 * That offset cancels in arm-vs-arm DIFFERENCES but not in RATIOS, because it
 * sits in the denominator -- and on the fast primitives it IS the denominator:
 * 72% of the measured chacha20-poly1305 baseline and 87% of aes256-gcm's. It
 * therefore deflates every percentage in the headline table, by 3.6-3.9x on
 * chacha and 6-14x on AES. (ed25519 is 8.5% and argon2id 0.002%, so those two
 * rows were never materially affected.)
 *
 * The fix is to notice that the percentage columns are RATIOS and therefore
 * unit-free: they never needed cycles. CNTVCT_EL0 is two instructions and
 * carries a 21-cycle offset instead of 3234 -- 154x better, and 0.14 cycles/op
 * once averaged over CIO's 1000 iterations.
 *
 * ORDERING IS THE WHOLE TRICK. The expensive kperf read is moved OUTSIDE the
 * window the driver differences: last thing before the region opens, first
 * thing after it closes.
 *
 *     region_begin:  [kperf read] [cntvct read -> t0]
 *                    ... the measured call ...
 *     region_end:    [cntvct read -> t1] [kperf read]
 *
 * so t1 - t0 holds the call plus two register reads, while reg_cyc/reg_ins
 * still bracket everything and keep their own offset -- harmless, because they
 * are only ever consumed as differences between arms.
 *
 * OPT-IN, DELIBERATELY. Default keeps the kperf timing that produced
 * paper_experiments/09, so those numbers stay reproducible byte-for-byte. Turn
 * this on and the driver's samples change UNITS, from cycles to CNTVCT ticks
 * (1 ns on M4; ~41.67 ns on the 24 MHz hosts -- check CNTFRQ_EL0, do not
 * assume). Ratios are unaffected; any absolute "cycles/op" column is not, and
 * must either be converted at the measured core clock or relabelled. The exit
 * line reports timer= so the choice is recorded with the results. */
static inline void cio_shim_region_begin(void) {
#ifdef CIO_SHIM_PMC
    /* Decide first, then read: the read must not happen on an unsampled region,
     * or the drain is back on every iteration and sampling bought nothing. */
    if (cio_shim_pmc_ok) {
        unsigned long long q = cio_shim_samp_seq++;
        cio_shim_samp_live = (q >= cio_shim_samp_skip)
                          && ((q - cio_shim_samp_skip) % cio_shim_samp_every) == 0;
        if (cio_shim_samp_live) {
            /* instructions first (its isb drains), cycles last: the drain then
             * precedes the cycle snapshot and is not inside the window. */
            cio_shim_s0_ins = cio_shim_pmc1();
            cio_shim_s0_cyc = cio_shim_pmc0();
        }
    }
#endif
#ifdef CIO_SHIM_CHEAP_TIMER
    cio_shim_read2(&cio_shim_t0_cyc, &cio_shim_t0_ins);   /* expensive: first */
    cio_shim_t0_timer = cio_shim_cntvct();                /* cheap: last */
#else
    cio_shim_read2(&cio_shim_t0_cyc, &cio_shim_t0_ins);
    cio_shim_t0_timer = cio_shim_t0_cyc;
#endif
}
/* returns the END timer value, so the driver's (end - start) is unchanged */
static inline uint64_t cio_shim_region_end(void) {
    unsigned long long c = 0, i = 0;
#ifdef CIO_SHIM_CHEAP_TIMER
    uint64_t t1 = cio_shim_cntvct();                      /* cheap: first */
    cio_shim_read2(&c, &i);                               /* expensive: after */
#endif
#ifndef CIO_SHIM_CHEAP_TIMER
    cio_shim_read2(&c, &i);
    uint64_t t1 = c;
#endif
#ifdef CIO_SHIM_PMC
    if (cio_shim_pmc_ok && cio_shim_samp_live) {
        /* cycles first (bare, cheap), instructions after: the end drain lands
         * outside the cycle window and inside the instruction one, which is
         * where it belongs -- instructions need it, cycles must not pay it. */
        unsigned long long s1c = cio_shim_pmc0();
        unsigned long long s1i = cio_shim_pmc1();
        if (s1c > cio_shim_s0_cyc && s1i >= cio_shim_s0_ins) {
            unsigned long long dc = s1c - cio_shim_s0_cyc;
            if (dc < 1000000000ULL) {          /* migration guard, as below */
                cio_shim_samp_cyc += dc;
                cio_shim_samp_ins += s1i - cio_shim_s0_ins;
                cio_shim_samp_n++;
            } else {
                cio_shim_reg_drop++;
            }
        }
        cio_shim_samp_live = 0;
    }
#endif
    if (cio_shim_t0_cyc && c > cio_shim_t0_cyc && i >= cio_shim_t0_ins) {
        unsigned long long dc = c - cio_shim_t0_cyc;
        /* Per-core PMCs read across a thread migration give a delta belonging to
         * two different cores. Negative is caught above; the other direction is
         * not, so drop anything absurd. 1e9 cycles is ~0.2s: longer than any
         * region here including an argon2id hash, and far shorter than the
         * counter's 48-bit range, so this rejects migrations without ever
         * rejecting a real sample. Counted, not silently dropped. */
        if (dc < 1000000000ULL) {
            cio_shim_reg_cyc += dc;
            cio_shim_reg_ins += i - cio_shim_t0_ins;
            cio_shim_reg_n++;
        } else {
            cio_shim_reg_drop++;
        }
    }
    return t1;
}

__attribute__((destructor)) static void cio_shim_end(void) {
    const char *src = "cntvct";
#ifdef CIO_SHIM_PMC
    if (cio_shim_pmc_ok) src = "pmc";
#endif
    unsigned long long cyc = 0, ins = 0, stall = 0, flush = 0;
#ifdef CIO_SHIM_KPERF
    if (cio_shim_kperf_ok) {
        src = "kperf";
        /* Whole-process totals for the four events perf.c configures:
         * cycles, instructions, map_stall, flush-restart-non-spec.
         *
         * Per-ITERATION instruction counts would mean adding a second array to
         * CIO's drivers, and their sources have to stay byte-identical across
         * arms. Whole-process totals need no source change and still answer the
         * questions that matter, because the run is 1000 identical iterations
         * plus fixed setup:
         *   instructions  A vs C  -- must be ~equal (blanket adds one MSR).
         *                            If it is not, the arms are not running the
         *                            same work and no cycle ratio means anything.
         *   instructions  A vs P  -- rises by the executed switch count, which
         *                            is what selective placement actually adds.
         *   IPC = cycles/instructions -- separates DWELL (same instructions,
         *                            more cycles: the mode slowing execution
         *                            down) from SWITCHES (more instructions).
         *                            That distinction is the whole cost model
         *                            and cntvct cannot see it. */
        if (perf_stop() == 0) {
            cyc = perf_results[0]; ins = perf_results[1];
            stall = perf_results[2]; flush = perf_results[3];
        }
    }
#endif
#ifdef CIO_SHIM_CHEAP_TIMER
    const char *timer = "cntvct";
#else
    const char *timer = src;   /* the driver differenced the counter reads */
#endif
    fprintf(stderr, "SHIM exit dit=%lu cycles=%s timer=%s tot_cyc=%llu tot_ins=%llu "
                    "map_stall=%llu flush=%llu reg_cyc=%llu reg_ins=%llu reg_n=%llu "
                    "samp_cyc=%llu samp_ins=%llu samp_n=%llu samp_every=%llu "
                    "pmc_off=%llu reg_drop=%llu pinned=%d\n",
            cio_shim_dit_get(), src, timer, cyc, ins, stall, flush,
            cio_shim_reg_cyc, cio_shim_reg_ins, cio_shim_reg_n,
            cio_shim_samp_cyc, cio_shim_samp_ins, cio_shim_samp_n,
            cio_shim_samp_every, cio_shim_pmc_off, cio_shim_reg_drop,
            cio_shim_pinned);
}

#endif /* CIO_ARM_SHIM_H */
