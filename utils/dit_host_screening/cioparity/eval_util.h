#ifndef EVAL_UTIL_H
#define EVAL_UTIL_H
/*
 * eval_util.h - gem5/AArch64 stand-in for CIO's x86 eval_util.h.
 *
 * WHY THIS FILE IS THE WHOLE PORT. CIO's six eval_*.c drivers are unmodified,
 * byte for byte, from counter-optimization/cio. Every one of them already
 * brackets exactly one crypto call with START_CYCLE_TIMER / STOP_CYCLE_TIMER:
 *
 *     start_time = START_CYCLE_TIMER;
 *     crypto_sign(...);                 <- the measured region
 *     end_time   = STOP_CYCLE_TIMER;
 *
 * Their versions of those macros are rdtsc/rdtscp with cpuid fences, which do
 * not exist on AArch64. Replacing only the header turns the same two points
 * into gem5 ROI markers, so the simulated region is *precisely* the call the
 * Apple M5 run timed. No driver source changes, no force-included shim.
 *
 * THE DRIVERS MUST NOT TIME THEMSELVES. gem5 SE mode returns SIMULATED time
 * from clock_gettime and the ARM virtual counter, so any in-binary timer reads
 * back a number that looks plausible and means nothing. This project has
 * already burned that trap once (utils/dit_host_screening/xover/run_gem5.py,
 * module docstring). Both macros therefore return 0 and the driver's cycle
 * counts file is written full of zeros on purpose: cycles come from stats.txt.
 *
 * ONE DUMP PER MEASURED CALL. m5_reset_stats zeroes the counters at the top of
 * the region; m5_dump_reset_stats emits a Begin/End block for it and zeroes
 * again. Driver work between regions (keypair generation, the sanity-check
 * verify, malloc) accumulates and is then discarded by the next reset, so it
 * never lands in a dump. The blocks arrive in loop order, which means the first
 * <num_warmup> of them are warmup and are dropped by the analysis rather than
 * averaged in -- the native rig could not do that, and its reg_n counted 1025
 * regions against 1000 timing samples for exactly this reason.
 *
 * Every stats block also carries commit.ditSetImm / ditClearImm / ditWriteReg /
 * ditRead / ditWrites, so executed DIT switches are attributed to the measured
 * region only, with no whole-process contamination and none of the constant
 * instrumentation offset that produced the retracted IPC 12-14 claim.
 */
#include <stdlib.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>

#include <gem5/m5ops.h>

#define EVAL_UTIL_H_SEED 172812

#define START_CYCLE_TIMER ({ m5_reset_stats(0, 0);      (uint64_t)0; })
#define STOP_CYCLE_TIMER  ({ m5_dump_reset_stats(0, 0); (uint64_t)0; })

/* Kept so the drivers link; never called on this path. */
static inline uint64_t ciocc_eval_rdtsc(void)  { return 0; }
static inline uint64_t ciocc_eval_rdtscp(void) { return 0; }

void
ciocc_eval_rand_fill_buf(unsigned char* buf, int buf_len)
{
	for (int ii = 0; ii < buf_len; ++ii) {
		buf[ii] = rand();
	}
}

/*
 * CIO's dynamic hit counts instrument THEIR transformed libsodium, which counts
 * x86 opcodes into llvm_stats via updateStats. Ours is a different mitigation on
 * a different ISA and emits no such calls, so these are stubs. The drivers only
 * reach print_dynamic_hitcounts when argc > 5, and this rig never passes a
 * sixth argument.
 */
volatile int llvm_stats[300] = {0};

void updateStats(const register int64_t idx) { (void)idx; }

void
print_dynamic_hitcounts(const char* outfilename)
{
	FILE* ff = fopen(outfilename, "w");
	if (ff) { fprintf(ff, "# no dynamic hit counts on the AArch64/DIT path\n"); fclose(ff); }
}
#endif /* EVAL_UTIL_H */
