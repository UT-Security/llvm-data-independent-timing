// DIT steady-state cost, memory-side. The ALU kernels show 1.00x, but on M3+
// setting PSTATE.DIT is documented to also disable the data memory-dependent
// prefetcher (DMP, the GoFetch mechanism). Any real DIT-on cost should show up
// here, not in an add chain.
//
//   cc -O2 dit_mem_bench.c -o dit_mem_bench
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <sys/qos.h>

static mach_timebase_info_data_t g_tb;
static double g_ghz = 0;

static inline double now_ns(void) {
  return (double)mach_absolute_time() * (double)g_tb.numer / (double)g_tb.denom;
}
static void set_dit(int on) {
  if (on)
    __asm__ volatile("msr DIT, #1" ::: "memory");
  else
    __asm__ volatile("msr DIT, #0" ::: "memory");
}

static uint64_t rng_s = 0x243F6A8885A308D3ull;
static uint64_t rng(void) {
  rng_s ^= rng_s << 13;
  rng_s ^= rng_s >> 7;
  rng_s ^= rng_s << 17;
  return rng_s;
}

#define NPTR (1u << 20)      // 1M pointers = 8 MB of pointer array
#define ARENA (256u << 20)   // 256 MB arena, far past LLC

static void **g_parr; // array of pointers into arena  (DMP-visible pattern)
static uint8_t *g_arena;
static uint64_t *g_chase; // pointer-chase ring
static uint64_t *g_flat;  // flat array for streaming

// (1) DMP-sensitive: sequentially walk an array of pointers, dereferencing each.
// The pointer array itself is trivially prefetchable; the *targets* are random.
// The DMP is exactly what prefetches those targets. DIT off -> DMP on -> fast.
static uint64_t k_deref(uint64_t n) {
  uint64_t s = 0;
  void **p = g_parr;
  for (uint64_t i = 0; i < n; i++)
    s += *(uint64_t *)p[i & (NPTR - 1)];
  return s;
}

// (2) Classic dependent pointer chase — latency bound, DMP cannot help (the
// next address is not known until the load returns). Control group.
static uint64_t k_chase(uint64_t n) {
  uint64_t idx = 0;
  for (uint64_t i = 0; i < n; i++)
    idx = g_chase[idx];
  return idx;
}

// (3) Streaming loads — the ordinary stride prefetcher handles this; DIT is not
// documented to touch it. Control group.
static uint64_t k_stream(uint64_t n) {
  uint64_t s = 0;
  for (uint64_t i = 0; i < n; i++)
    s += g_flat[i & (NPTR - 1)];
  return s;
}

// (4) Silent stores: write the value already present. If the core elides those,
// DIT (which must not let stored data affect timing) should stop it.
static uint64_t k_silent_store(uint64_t n) {
  for (uint64_t i = 0; i < n; i++)
    g_flat[i & (NPTR - 1)] = 0;
  return g_flat[0];
}

// (5) Same loop, values that always differ -> no silent-store opportunity.
static uint64_t k_dirty_store(uint64_t n) {
  for (uint64_t i = 0; i < n; i++)
    g_flat[i & (NPTR - 1)] = i;
  return g_flat[0];
}

typedef uint64_t (*kfn)(uint64_t);

static double best(kfn f, uint64_t n, int dit, uint64_t *sink) {
  double b = 1e30;
  for (int r = 0; r < 7; r++) {
    set_dit(dit);
    double t0 = now_ns();
    *sink += f(n);
    double t = now_ns() - t0;
    set_dit(0);
    if (t < b)
      b = t;
  }
  return b;
}

int main(void) {
  mach_timebase_info(&g_tb);
  pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);

  // calibrate GHz with a dependent add chain (64 deps/iter)
  {
    uint64_t i = 5000000;
    double t0 = now_ns();
    __asm__ volatile("mov x9, %[n]\n1:\n"
#define R8A "add x0,x0,#1\nadd x0,x0,#1\nadd x0,x0,#1\nadd x0,x0,#1\n"
                     R8A R8A R8A R8A R8A R8A R8A R8A
                     "subs x9, x9, #1\nb.ne 1b\n"
                     : : [n] "r"(i) : "x0", "x9", "cc");
    double t = now_ns() - t0;
    g_ghz = (double)i * 32.0 / t;
  }
  printf("calibration: %.3f GHz effective\n", g_ghz);

  g_arena = malloc(ARENA);
  g_parr = malloc(NPTR * sizeof(void *));
  g_chase = malloc(NPTR * sizeof(uint64_t));
  g_flat = malloc(NPTR * sizeof(uint64_t));
  if (!g_arena || !g_parr || !g_chase || !g_flat)
    return fprintf(stderr, "alloc failed\n"), 1;
  memset(g_arena, 1, ARENA);
  memset(g_flat, 0, NPTR * sizeof(uint64_t));

  // pointer array -> random 64B-aligned targets in the arena
  for (uint32_t i = 0; i < NPTR; i++)
    g_parr[i] = g_arena + ((rng() % (ARENA / 64)) * 64);
  // pointer chase: random permutation cycle
  for (uint32_t i = 0; i < NPTR; i++)
    g_chase[i] = i;
  for (uint32_t i = NPTR - 1; i > 0; i--) {
    uint32_t j = (uint32_t)(rng() % (i + 1));
    uint64_t t = g_chase[i];
    g_chase[i] = g_chase[j];
    g_chase[j] = t;
  }

  const uint64_t N = 20000000;
  uint64_t sink = 0;

  struct {
    const char *name;
    kfn f;
  } K[] = {
      {"deref ptr-array  (DMP-sensitive)", k_deref},
      {"pointer chase    (dep, control)", k_chase},
      {"streaming loads  (control)", k_stream},
      {"silent stores    (store same val)", k_silent_store},
      {"dirty  stores    (store new val)", k_dirty_store},
  };

  printf("\n=== steady-state memory kernels: DIT off vs DIT on ===\n");
  printf("  %-34s %11s %11s %8s\n", "kernel", "off(ns/op)", "on(ns/op)",
         "ratio");
  for (unsigned i = 0; i < sizeof(K) / sizeof(K[0]); i++) {
    double off = best(K[i].f, N, 0, &sink) / (double)N;
    double on = best(K[i].f, N, 1, &sink) / (double)N;
    printf("  %-34s %11.3f %11.3f %8.3fx\n", K[i].name, off, on, on / off);
  }
  printf("\n(sink %llu)\n", (unsigned long long)sink);
  return 0;
}
