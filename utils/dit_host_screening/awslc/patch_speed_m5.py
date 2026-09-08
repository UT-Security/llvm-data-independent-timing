#!/usr/bin/env python3
"""Experiment 14 under gem5: make `bssl speed` run a FIXED number of calls inside an m5 ROI.

The silicon rig reads Apple's PMCs around TimeFunction's existing loop
(patch_speed_pmc.py). Neither half of that survives a move to gem5:

  * S3_2_c15_c0_0 is an Apple system register. Cycles come from stats.txt instead
    (core.numCycles / commitStats0.numInsts), delimited by m5_reset_stats and
    m5_dump_reset_stats.

  * THE TOOL MUST NOT TIME ITSELF. gem5 SE returns SIMULATED time from
    clock_gettime, so `-timeout_ms 50` would make the iteration count a function
    of the very thing being measured: a slower arm would execute fewer calls, and
    the whole-run instruction count would differ across switch models, which is
    the one cross-model gate that catches a self-timing driver. The loop
    therefore runs M5_CALLS calls, always, whatever the simulated clock says.

ONE DUMP PER ROW, AGGREGATED. The ROI covers M5_CALLS calls, not one: an
m5-delimited region carries a fixed 0-or-+400-instruction marker slop (CLAUDE.md),
and a 16-byte AES-GCM seal is ~190 cycles on silicon, so a per-call ROI would be
mostly marker. M5_WARM calls run before the reset and land in no dump.

Each TimeFunction call emits exactly one Begin/End block and stamps its index into
the JSON row as "roi", so the driver maps dumps to rows by number rather than by
counting and hoping.

PER-ROW CALL COUNTS, and why they are not optional. The suite's rows span eight orders of
magnitude of cost: an AES-128 block is 10 ns natively, RSA 4096 key generation is 1.41 s,
which is about 23 HOURS of simulation for a single call. One global count therefore either
starves the cheap rows or never finishes the expensive ones. M5_CALLS_LIST gives each row
its own count, in emission order, and a count of 0 means the row is NOT RUN AT ALL --
`func()` is never called and no ROI is emitted. That is what makes both the cost cap and
the partitioning of the suite across processes free rather than merely unmeasured. A
skipped row still prints its JSON, with numCalls 0 and roi = 2^64-1, so the driver can tell
"deliberately not run" from "failed".

  M5_CALLS      (default 1000)  measured calls per row, when no list is given
  M5_WARM       (default 50)    calls before the reset, capped at the row's own count
  M5_CALLS_LIST (unset)         comma-separated per-row counts in emission order; a row
                                past the end of the list falls back to M5_CALLS
usage: patch_speed_m5.py <aws-lc tree>
"""
import sys, os
p = os.path.join(sys.argv[1], 'tool', 'speed.cc')
s = open(p).read()
if 'bm_m5_calls' in s:
    print('speed.cc: already patched'); sys.exit(0)

def sub(a, b, count=1):
    global s
    n = s.count(a)
    assert n == count, f'anchor found {n}x, expected {count}: {a[:60]!r}'
    s = s.replace(a, b)

sub('#include <sstream>\n', '''#include <sstream>

// experiment 14 on gem5: fixed call count inside an m5 ROI. See patch_speed_m5.py.
//
// BM_M5_NO_OPS builds the same patched tool WITHOUT the m5 pseudo-instructions, which is
// how a call-count list is validated. A list that skips a row another row depends on (the
// ECDSA verify that checks the signature the skipped signing row was meant to produce)
// makes the tool exit mid-chain, and finding that out costs simulator hours. The no-ops
// build runs natively in seconds and answers the same question, because the skipping logic
// is identical and only the markers are gone.
#ifdef BM_M5_NO_OPS
static inline void m5_reset_stats(unsigned long a, unsigned long b) { (void)a; (void)b; }
static inline void m5_dump_reset_stats(unsigned long a, unsigned long b) { (void)a; (void)b; }
#else
#include <gem5/m5ops.h>
#endif
static uint64_t bm_m5_env(const char *name, uint64_t dflt) {
  const char *v = getenv(name);
  if (!v || !*v) return dflt;
  char *end = NULL;
  unsigned long long n = strtoull(v, &end, 10);
  return (end && *end == 0 && n > 0) ? (uint64_t)n : dflt;
}
static uint64_t bm_m5_calls(void) { static uint64_t v = bm_m5_env("M5_CALLS", 1000); return v; }
static uint64_t bm_m5_warm(void)  { static uint64_t v = bm_m5_env("M5_WARM", 50);   return v; }
// Two counters, deliberately distinct: every TimeFunction call consumes a ROW index, but
// only a measured row consumes an ROI index, so a skipped row does not shift the mapping
// from stats dumps to rows.
// M5_FILTER: the -filter value, taken from the environment. gem5 splits --arguments on
// whitespace, so a filter naming a benchmark with a space in it ("ECDSA P-256") cannot be
// passed on the command line at all -- it arrives as two arguments and the tool exits.
// Reading it from the environment sidesteps that entirely, which is what makes segmenting
// the suite by filter possible. Filtering is also the ONLY clean way to exclude a
// benchmark: a filtered-out Speed* function returns before doing anything, where a row
// skipped by M5_CALLS_LIST still runs its setup and can leave state a later benchmark
// depends on (skipping RSA key-gen makes ML-KEM-512 encaps fail with
// OPERATION_NOT_SUPPORTED_FOR_THIS_KEYTYPE; measured, not theorised). Note the tool already
// gates RSAKeyGen, Jitter, CRYPTO_refcount_inc, HRSS, SPAKE2, base64, 25519 and SHAKE256-x4
// behind the filter NAMING them, so any non-empty filter excludes those by construction.
static uint64_t bm_m5_row = 0;
static uint64_t bm_m5_roi = 0;
#define BM_M5_SKIPPED ((uint64_t)-1)
static uint64_t bm_m5_calls_for_row(uint64_t row) {
  static const char *list = NULL;
  static int looked_up = 0;
  if (!looked_up) { list = getenv("M5_CALLS_LIST"); looked_up = 1; }
  if (!list || !*list) return bm_m5_calls();
  uint64_t i = 0;
  const char *p = list;
  for (;;) {
    char *end = NULL;
    unsigned long long n = strtoull(p, &end, 10);
    if (end == p) break;
    if (i == row) return (uint64_t)n;
    i++;
    if (*end != ',') break;
    p = end + 1;
  }
  return bm_m5_calls();   // past the end of the list: the global default
}
''')

sub('''  // us is the number of microseconds that elapsed in the time period.
  uint64_t us;
''', '''  // us is the number of microseconds that elapsed in the time period.
  // SIMULATED under gem5 and therefore meaningless: kept only so the tool's own
  // text output still prints. Cycles come from stats.txt.
  uint64_t us;
  // experiment 14 on gem5: index of this row's stats dump, 0-based, in emission
  // order. The driver joins dumps to rows on it.
  uint64_t roi;
''')

# JSON output, both overloads
sub('''    printf("}");
    first_json_printed = true;''', '''    printf(", \\"roi\\": %" PRIu64, roi);
    printf("}");
    first_json_printed = true;''', count=2)

# a row whose count is 0 is not run at all: the early return is BEFORE the self-check call
sub("""static bool TimeFunction(TimeResults *results, std::function<bool()> func) {
  // The first time |func| is called an expensive self check might run that
  // will skew the iterations between checks calculation
  if (!func()) {
    return false;
  }""", """static bool TimeFunction(TimeResults *results, std::function<bool()> func) {
  // experiment 14 on gem5: a row whose count is 0 is not run at all. The early return is
  // BEFORE the self-check call on purpose -- one call of a capped row can be hours.
  const uint64_t bm_row = bm_m5_row++;
  const uint64_t bm_calls = bm_m5_calls_for_row(bm_row);
  if (bm_calls == 0) {
    results->us = 1;
    results->num_calls = 0;
    results->roi = BM_M5_SKIPPED;
    return true;
  }
  // warm-up never exceeds the row's own count: an expensive row runs one measured call,
  // and 50 warm-up calls there would be fifty times the cost of the measurement
  const uint64_t bm_warm = bm_m5_warm() < bm_calls ? bm_m5_warm() : bm_calls;

  // The first time |func| is called an expensive self check might run that
  // will skew the iterations between checks calculation
  if (!func()) {
    return false;
  }""")

# the measured loop in TimeFunction: fixed count, ROI markers, no clock feedback
sub('''  // Don't include the time taken to run |func| to calculate
  // |iterations_between_time_checks|
  start = time_now();
  uint64_t done = 0;
  for (;;) {
    for (unsigned i = 0; i < iterations_between_time_checks; i++) {
      if (!func()) {
        return false;
      }
      done++;
    }

    now = time_now();
    if (now - start > total_us) {
      break;
    }
  }

  results->us = now - start;
  results->num_calls = done;
  return true;
''', '''  // experiment 14 on gem5: the loop runs a FIXED number of calls. The simulated
  // clock is never consulted, so every arm executes the same instruction stream
  // and the cross-switch-model instruction-count gate means something.
  (void)iterations_between_time_checks;
  (void)total_us;
  for (uint64_t i = 0; i < bm_warm; i++) {
    if (!func()) {
      return false;
    }
  }
  start = time_now();
  uint64_t done = 0;
  m5_reset_stats(0, 0);
  for (uint64_t i = 0; i < bm_calls; i++) {
    if (!func()) {
      return false;
    }
    done++;
  }
  m5_dump_reset_stats(0, 0);
  now = time_now();

  results->us = now - start;
  results->num_calls = done;
  results->roi = bm_m5_roi++;
  return true;
''')

# the tool's one aggregate initialiser of TimeResults must name the new field:
# it is compiled with -Wmissing-field-initializers -Werror
sub('    const TimeResults results = {num_calls, us};', '    const TimeResults results = {num_calls, us, 0};')

# CRYPTO_refcount_inc spawns std::thread even for a single thread, and gem5 SE on a
# one-CPU configuration cannot create one: every arm of that group died in under a minute
# with std::system_error "Resource temporarily unavailable". Running the single-thread case
# INLINE makes the benchmark measurable there. It does change what the row means -- no
# thread create/join in the measured window -- which for one thread is arguably the honest
# reading anyway, and the row carries no DIT bracket in any case. Combine with `-threads 1`;
# 2 threads and up still spawn and would still fail.
sub("""  if (!TimeFunction(&results, [&num_threads, &thread_func]() -> bool {
        std::vector<std::thread> threads;
        for (size_t i = 0; i < num_threads; i++) {
          threads.emplace_back(thread_func);
        }
        for (auto &t : threads) {
          t.join();
        }
        return true;
      })) {""", """  if (!TimeFunction(&results, [&num_threads, &thread_func]() -> bool {
        if (num_threads == 1) {
          return thread_func();   // gem5 SE cannot create a thread; see patch_speed_m5.py
        }
        std::vector<std::thread> threads;
        for (size_t i = 0; i < num_threads; i++) {
          threads.emplace_back(thread_func);
        }
        for (auto &t : threads) {
          t.join();
        }
        return true;
      })) {""")

# M5_FILTER overrides -filter, so a filter containing a space survives gem5's argument
# splitting. Applied after the tool's own parsing so it wins.
sub("""  if (args_map.count("-filter") != 0) {""", """  if (const char *bm_f = getenv("M5_FILTER")) {
    if (*bm_f) {
      g_filters.clear();
      std::string bm_s(bm_f), bm_item;
      std::stringstream bm_ss(bm_s);
      while (std::getline(bm_ss, bm_item, ',')) {
        if (!bm_item.empty()) g_filters.push_back(bm_item);
      }
    }
  }
  if (args_map.count("-filter") != 0) {""")
open(p, 'w').write(s)
print('speed.cc: fixed call count + m5 ROI markers + "roi" in the JSON rows')
