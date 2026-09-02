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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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

static unsigned long cio_shim_dit_get(void) {
    unsigned long d;
    __asm__ volatile("mrs %0, DIT" : "=r"(d));
    return (d >> 24) & 1UL;
}

__attribute__((constructor)) static void cio_shim_start(void) {
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#ifdef CIO_SHIM_KPERF
    /* needs root; failure is not an error, it selects the CNTVCT fallback */
    cio_shim_kperf_ok = (perf_init("cioparity") == 0) && (perf_start() == 0);
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
    if (cio_shim_t0_cyc && c > cio_shim_t0_cyc) {
        cio_shim_reg_cyc += c - cio_shim_t0_cyc;
        cio_shim_reg_ins += i - cio_shim_t0_ins;
        cio_shim_reg_n++;
    }
    return t1;
}

__attribute__((destructor)) static void cio_shim_end(void) {
    const char *src = "cntvct";
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
                    "map_stall=%llu flush=%llu reg_cyc=%llu reg_ins=%llu reg_n=%llu\n",
            cio_shim_dit_get(), src, timer, cyc, ins, stall, flush,
            cio_shim_reg_cyc, cio_shim_reg_ins, cio_shim_reg_n);
}

#endif /* CIO_ARM_SHIM_H */
