// DIT microbenchmark — measures the two numbers the placement cost model needs:
//   (a) cost of an `MSR DIT` toggle (pipeline-flush / serialization cost)
//   (b) steady-state slowdown of identical code executed with PSTATE.DIT = 1
//
// Requires FEAT_DIT (Apple M-series). Build:
//   cc -O2 dit_bench.c -o dit_bench
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <sys/qos.h>

static mach_timebase_info_data_t g_tb;

static inline double now_ns(void) {
  return (double)mach_absolute_time() * (double)g_tb.numer / (double)g_tb.denom;
}

// x9 = loop counter, x0..x5 = scratch. Body is repeated REPS times per iteration.
#define KERNEL(name, body)                                                     \
  static double name(uint64_t iters, uint64_t seed) {                          \
    uint64_t x0 = seed, x1 = seed | 1, i = iters;                              \
    double t0 = now_ns();                                                      \
    __asm__ volatile("mov x0, %[a]\n"                                          \
                     "mov x1, %[b]\n"                                          \
                     "mov x9, %[n]\n"                                          \
                     "1:\n" body "subs x9, x9, #1\n"                           \
                     "b.ne 1b\n"                                               \
                     :                                                         \
                     : [a] "r"(x0), [b] "r"(x1), [n] "r"(i)                    \
                     : "x0", "x1", "x2", "x3", "x9", "cc", "memory");          \
    return now_ns() - t0;                                                      \
  }

#define R2(s) s s
#define R4(s) R2(s) R2(s)
#define R8(s) R4(s) R4(s)
#define R16(s) R8(s) R8(s)
#define R32(s) R16(s) R16(s)
#define R64(s) R32(s) R32(s)

// ---- calibration: 64 dependent 1-cycle adds per iteration ----
KERNEL(k_addchain, R64("add x0, x0, #1\n"))

// ---- (a) toggle cost ----
// 32 alternating toggles per iteration (a real region entry+exit pair x16).
KERNEL(k_toggle_alt, R16("msr DIT, #1\nmsr DIT, #0\n"))
// 32 same-value writes: does the core elide a write that doesn't change PSTATE?
KERNEL(k_toggle_same, R32("msr DIT, #1\n"))
// 16 toggle-pairs interleaved with independent ALU work: is the cost a full
// serializing flush, or can surrounding work hide under it?
KERNEL(k_toggle_ilp,
       R16("msr DIT, #1\nadd x2, x2, #1\nadd x3, x3, #1\nmsr DIT, #0\nadd x2, "
           "x2, #1\nadd x3, x3, #1\n"))
// Baseline for k_toggle_ilp with the toggles removed (same ALU work).
KERNEL(k_toggle_ilp_base,
       R16("add x2, x2, #1\nadd x3, x3, #1\nadd x2, x2, #1\nadd x3, x3, #1\n"))

// ---- (b) steady-state cost with DIT on vs off ----
// Dependent 64x64 multiply chain — the classic data-dependent-latency suspect.
KERNEL(k_mul, R64("mul x0, x0, x1\n"))
// Dependent add chain (should be data-independent already; control group).
KERNEL(k_add, R64("add x0, x0, x1\n"))
// High-multiplier-unit pressure: umulh, where early-out is most plausible.
KERNEL(k_umulh, R64("umulh x0, x0, x1\n"))
// Independent multiplies — throughput rather than latency.
KERNEL(k_mul_tp, R64("mul x2, x0, x1\nmul x3, x1, x0\n"))

static void set_dit(int on) {
  if (on)
    __asm__ volatile("msr DIT, #1" ::: "memory");
  else
    __asm__ volatile("msr DIT, #0" ::: "memory");
}

static int dit_is_on(void) {
  uint64_t d;
  __asm__ volatile("mrs %0, DIT" : "=r"(d));
  return (int)((d >> 24) & 1);
}

typedef double (*kfn)(uint64_t, uint64_t);

// The (a) kernels toggle DIT themselves, so only the steady-state kernels are
// expected to leave PSTATE.DIT as the caller set it.
static int g_check_dit_preserved = 0;

// Best-of-N to strip scheduler noise.
static double best(kfn f, uint64_t iters, uint64_t seed, int dit) {
  double b = 1e30;
  for (int r = 0; r < 9; r++) {
    set_dit(dit);
    double t = f(iters, seed);
    if (g_check_dit_preserved && dit_is_on() != dit) {
      fprintf(stderr, "FATAL: DIT state not preserved across kernel\n");
      exit(1);
    }
    set_dit(0);
    if (t < b)
      b = t;
  }
  return b;
}

static double g_ghz;

// Cycles per iteration.
static double cyc(double ns, uint64_t iters) {
  return ns * g_ghz / (double)iters;
}

int main(void) {
  mach_timebase_info(&g_tb);
  pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0); // prefer P-core

  const uint64_t ITERS = 2000000;
  const uint64_t SEED = 0x9E3779B97F4A7C15ull;

  // Calibrate frequency: 64 dependent adds = 64 cycles/iter exactly.
  double cal = best(k_addchain, ITERS, SEED, 0);
  g_ghz = (double)ITERS * 64.0 / cal;
  printf("calibration: dependent add chain -> %.3f GHz effective\n\n", g_ghz);

  printf("=== (a) MSR DIT toggle cost ===\n");
  double alt = best(k_toggle_alt, ITERS, SEED, 0);
  double same = best(k_toggle_same, ITERS, SEED, 0);
  double ilp = best(k_toggle_ilp, ITERS, SEED, 0);
  double ilpb = best(k_toggle_ilp_base, ITERS, SEED, 0);
  printf("  alternating msr DIT #1/#0 : %7.2f cyc/toggle\n",
         cyc(alt, ITERS) / 32.0);
  printf("  repeated   msr DIT #1     : %7.2f cyc/write   (same value; elided?)\n",
         cyc(same, ITERS) / 32.0);
  printf("  toggle + independent ALU  : %7.2f cyc/iter (%.2f baseline, "
         "delta %.2f cyc over 32 toggles = %.2f cyc/toggle)\n",
         cyc(ilp, ITERS), cyc(ilpb, ITERS),
         cyc(ilp, ITERS) - cyc(ilpb, ITERS),
         (cyc(ilp, ITERS) - cyc(ilpb, ITERS)) / 32.0);

  g_check_dit_preserved = 1; // kernels below must not disturb PSTATE.DIT
  printf("\n=== (b) steady-state: DIT off vs DIT on ===\n");
  struct {
    const char *name;
    kfn f;
    int per_iter;
  } K[] = {
      {"add chain   (dep, 64/iter)", k_add, 64},
      {"mul chain   (dep, 64/iter)", k_mul, 64},
      {"umulh chain (dep, 64/iter)", k_umulh, 64},
      {"mul indep   (tp,128/iter)", k_mul_tp, 128},
  };
  printf("  %-28s %10s %10s %8s\n", "kernel", "off(cyc/op)", "on(cyc/op)",
         "ratio");
  for (unsigned i = 0; i < sizeof(K) / sizeof(K[0]); i++) {
    double off = cyc(best(K[i].f, ITERS, SEED, 0), ITERS) / K[i].per_iter;
    double on = cyc(best(K[i].f, ITERS, SEED, 1), ITERS) / K[i].per_iter;
    printf("  %-28s %10.3f %10.3f %8.3fx\n", K[i].name, off, on, on / off);
  }

  // Data-operand sensitivity: if the core simplifies computation on "easy"
  // operands (e.g. zero), DIT=0 shows a zero-vs-random gap and DIT=1 closes it.
  printf("\n=== (c) data-operand dependence (zero vs random operands) ===\n");
  printf("  %-28s %10s %10s %8s\n", "kernel/operands", "off(cyc/op)",
         "on(cyc/op)", "ratio");
  struct {
    const char *name;
    kfn f;
    uint64_t seed;
    int per_iter;
  } D[] = {
      {"mul  operands=0", k_mul, 0, 64},
      {"mul  operands=random", k_mul, SEED, 64},
      {"umulh operands=0", k_umulh, 0, 64},
      {"umulh operands=random", k_umulh, SEED, 64},
  };
  for (unsigned i = 0; i < sizeof(D) / sizeof(D[0]); i++) {
    double off = cyc(best(D[i].f, ITERS, D[i].seed, 0), ITERS) / D[i].per_iter;
    double on = cyc(best(D[i].f, ITERS, D[i].seed, 1), ITERS) / D[i].per_iter;
    printf("  %-28s %10.3f %10.3f %8.3fx\n", D[i].name, off, on, on / off);
  }
  return 0;
}
