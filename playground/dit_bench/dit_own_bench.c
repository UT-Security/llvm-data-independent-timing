// Does the DIT ownership rule actually pay? Compare, per call:
//
//   today      callee sets DIT, callee clears DIT, caller re-asserts  = 3 writes
//   ownership  callee saves DIT at entry, tests it at exit, and when it was
//              already set (the hot path in a loop like cbc_encrypt) skips the
//              clear entirely -- so ZERO writes execute
//
// The measurement trap in the first attempt: the guarded `msr DIT, #0` only
// disappears when DIT is genuinely ON, so the loop must run with DIT set. With
// DIT off the branch falls through and the 30-cycle write executes, which reads
// as the ownership path being expensive when it is really the cold path.
//
//   clang -O2 own_bench.c -o own_bench
#include <stdio.h>
#include <stdint.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <sys/qos.h>

static mach_timebase_info_data_t g_tb;
static double g_ghz;
static inline double now_ns(void) {
  return (double)mach_absolute_time() * (double)g_tb.numer / (double)g_tb.denom;
}

#define KERNEL(name, body)                                                     \
  static double name(uint64_t iters) {                                         \
    double t0 = now_ns();                                                      \
    __asm__ volatile("mov x9, %[n]\n"                                          \
                     "1:\n" body "subs x9, x9, #1\n"                           \
                     "b.ne 1b\n"                                               \
                     :                                                         \
                     : [n] "r"(iters)                                          \
                     : "x0", "x1", "x2", "x3", "x9", "x30", "cc", "memory");   \
    return now_ns() - t0;                                                      \
  }

#define R2(s) s s
#define R4(s) R2(s) R2(s)
#define R8(s) R4(s) R4(s)
#define R16(s) R8(s) R8(s)
#define R32(s) R16(s) R16(s)

KERNEL(k_nop, R32("add x0, x0, #1\n"))
KERNEL(k_today, R32("msr DIT, #1\nmsr DIT, #0\nmsr DIT, #1\n"))
KERNEL(k_mrs, R32("mrs x0, DIT\n"))
// The callee's whole per-call cost under the rule: save the entry state, and at
// exit test it and skip the clear. Correct only when DIT is ON (set by main).
KERNEL(k_own, R32("mrs x0, DIT\nstr x0, [sp, #-16]!\nldr x0, [sp], #16\n"
                  "tbnz x0, #24, 4f\nmsr DIT, #0\n4:\n"))
// Same, minus the stack round trip, in case a future design keeps the entry
// state in a reserved register instead of a frame slot.
KERNEL(k_own_reg, R32("mrs x0, DIT\ntbnz x0, #24, 5f\nmsr DIT, #0\n5:\n"))

static double best(double (*f)(uint64_t), uint64_t n) {
  double b = 1e30;
  for (int r = 0; r < 11; r++) {
    double t = f(n);
    if (t < b)
      b = t;
  }
  return b;
}

int main(void) {
  mach_timebase_info(&g_tb);
  pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
  const uint64_t N = 1000000;

  double cal = best(k_nop, N);
  g_ghz = (double)N * 32.0 / cal;

  uint64_t dit;
  __asm__ volatile("mrs %0, DIT" : "=r"(dit));
  printf("calibration: %.3f GHz   (DIT at start = %llu)\n\n", g_ghz,
         (unsigned long long)((dit >> 24) & 1));

  printf("  %-40s %12s\n", "per call (32 groups per iteration)", "cycles");
  printf("  %-40s %12.2f\n", "TODAY: 3x msr DIT",
         best(k_today, N) * g_ghz / (double)N / 32.0);
  printf("  %-40s %12.2f\n", "mrs DIT alone",
         best(k_mrs, N) * g_ghz / (double)N / 32.0);

  // Everything below must run with DIT SET, or the guarded clear executes and
  // the hot path is not what gets measured.
  __asm__ volatile("msr DIT, #1" ::: "memory");
  double own = best(k_own, N) * g_ghz / (double)N / 32.0;
  double own_reg = best(k_own_reg, N) * g_ghz / (double)N / 32.0;
  __asm__ volatile("mrs %0, DIT" : "=r"(dit));
  __asm__ volatile("msr DIT, #0" ::: "memory");

  printf("  %-40s %12.2f\n", "OWNERSHIP (frame slot), DIT on", own);
  printf("  %-40s %12.2f\n", "OWNERSHIP (register), DIT on", own_reg);
  printf("\n  DIT during the ownership runs = %llu (must be 1)\n",
         (unsigned long long)((dit >> 24) & 1));

  double today = best(k_today, N) * g_ghz / (double)N / 32.0;
  printf("  saving per call: %.1f cycles (%.1fx)\n", today - own,
         own > 0 ? today / own : 0.0);
  return 0;
}
