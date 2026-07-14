// Pin down the toggle cost precisely and put it in context: MSR DIT vs the
// ISB/DSB barriers the other mode emits, vs a call/ret, vs plain ALU.
//   cc -O2 dit_toggle_bench.c -o dit_toggle_bench
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

KERNEL(k_nop, R32("add x0, x0, #1\n"))            // 32 dep adds = 32 cyc, calib
KERNEL(k_dit_alt, R16("msr DIT, #1\nmsr DIT, #0\n"))  // 32 writes, value flips
KERNEL(k_dit_same1, R32("msr DIT, #1\n"))             // 32 writes, no change
KERNEL(k_dit_same0, R32("msr DIT, #0\n"))             // 32 writes, no change
KERNEL(k_isb, R32("isb\n"))
KERNEL(k_dsb_sy, R32("dsb sy\n"))
KERNEL(k_dsb_ish, R32("dsb ish\n"))
KERNEL(k_isb_dsb, R16("dsb sy\nisb\n"))               // what -taint-insert-isb emits
KERNEL(k_callret, R32("bl 2f\nb 3f\n2:\nret\n3:\n"))  // call+ret pair, for scale

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
  printf("calibration: %.3f GHz\n\n", g_ghz);
  printf("  %-34s %12s\n", "instruction (32 per iteration)", "cycles each");

  struct {
    const char *n;
    double (*f)(uint64_t);
  } K[] = {
      {"add (dependent, reference)", k_nop},   {"msr DIT (alternating 1/0)", k_dit_alt},
      {"msr DIT, #1 (already 1)", k_dit_same1}, {"msr DIT, #0 (already 0)", k_dit_same0},
      {"isb", k_isb},                          {"dsb ish", k_dsb_ish},
      {"dsb sy", k_dsb_sy},                    {"dsb sy + isb (pair)", k_isb_dsb},
      {"bl + ret (pair)", k_callret},
  };
  for (unsigned i = 0; i < sizeof(K) / sizeof(K[0]); i++)
    printf("  %-34s %12.2f\n", K[i].n,
           best(K[i].f, N) * g_ghz / (double)N / 32.0);

  printf("\nregion overhead (2 toggles: enter+exit) = %.1f cycles\n",
         2.0 * (best(k_dit_alt, N) * g_ghz / (double)N / 32.0));
  return 0;
}
