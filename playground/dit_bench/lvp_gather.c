// RESULT (M4, userspace): NEGATIVE / inconclusive. Independent-gather MLP
// overlaps the cache misses and the LVP-training warmup also warms the
// cache, so the constant-vs-random value-timing gap does not reproduce
// from userspace (needs KDK clflush + cycle counters, as FLOP used). The
// clean isolation is in lvp_dit.c (self-dependent chase) instead.
// STEP 1 — reproduce FLOP (USENIX Sec'25) §4.1 on Apple M4.
// Hypothesis: a load whose VALUE is a constant runs faster than one whose value
// is random, because the M3/M4 Load Value Predictor predicts the constant and
// breaks the RAW dependency on the (cache-missing) load. FLOP saw ~2x at 500
// iters on M3 P-cores. Pure userspace (mach_absolute_time, no KDK, no clflush) —
// we force misses with a large shuffled working set.
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <sys/qos.h>

static mach_timebase_info_data_t g_tb;
static double g_ghz;
static inline double now_ns(void){return (double)mach_absolute_time()*g_tb.numer/g_tb.denom;}

static uint64_t rng_s=0x243F6A8885A308D3ull;
static uint64_t rng(void){rng_s^=rng_s<<13;rng_s^=rng_s>>7;rng_s^=rng_s<<17;return rng_s;}

static void set_dit(int on){ if(on) __asm__ volatile("msr DIT,#1":::"memory"); else __asm__ volatile("msr DIT,#0":::"memory"); }
static int dit_on(void){uint64_t d;__asm__ volatile("mrs %0,DIT":"=r"(d));return (d>>24)&1;}

// One static byte-load instruction, executed N times, RAW-accumulated.
// x0=offsets(uint32 array, sequential), x1=mem base, x2=N -> returns acc in x0.
// The critical load is `ldrb w5,[x1,x4]`; the offset load is sequential/cheap.
static uint64_t __attribute__((noinline)) chase_b(const uint32_t*off,const uint8_t*mem,uint64_t n){
  uint64_t acc;
  __asm__ volatile(
    "mov x3, #0\n"
    "1:\n"
    "ldr w4, [%[o]], #4\n"      // next offset (sequential)
    "ldrb w5, [%[m], x4]\n"     // THE load: value is CONST or RANDOM
    "add x3, x3, x5\n"          // RAW dependency chain
    "subs %[n], %[n], #1\n"
    "b.ne 1b\n"
    "mov %[a], x3\n"
    : [a]"=r"(acc),[o]"+r"(off),[n]"+r"(n)
    : [m]"r"(mem)
    : "x3","x4","x5","cc","memory");
  return acc;
}
// 4-byte-load variant (LVP predicts arbitrary constants for <=4B loads).
static uint64_t __attribute__((noinline)) chase_w(const uint32_t*off,const uint8_t*mem,uint64_t n){
  uint64_t acc;
  __asm__ volatile(
    "mov x3, #0\n"
    "1:\n"
    "ldr w4, [%[o]], #4\n"
    "ldr w5, [%[m], x4]\n"      // 4-byte load
    "add x3, x3, x5\n"
    "subs %[n], %[n], #1\n"
    "b.ne 1b\n"
    "mov %[a], x3\n"
    : [a]"=r"(acc),[o]"+r"(off),[n]"+r"(n)
    : [m]"r"(mem)
    : "x3","x4","x5","cc","memory");
  return acc;
}

#define ARENA (256ull<<20)   // 256 MB >> LLC

int main(int argc,char**argv){
  mach_timebase_info(&g_tb);
  pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE,0);
  // calibrate GHz: 64 dependent adds
  { uint64_t i=5000000; double t0=now_ns();
    __asm__ volatile("mov x9,%[n]\n1:\n"
#define A8 "add x0,x0,#1\nadd x0,x0,#1\nadd x0,x0,#1\nadd x0,x0,#1\n"
      A8 A8 A8 A8 A8 A8 A8 A8 "subs x9,x9,#1\nb.ne 1b\n":: [n]"r"(i):"x0","x9","cc");
    g_ghz=(double)i*32.0/(now_ns()-t0); }

  uint8_t*mem=malloc(ARENA);
  const uint64_t NOFF=1u<<20;           // 1M distinct cache-line offsets
  uint32_t*off=malloc(NOFF*sizeof(uint32_t));
  if(!mem||!off){fprintf(stderr,"alloc\n");return 1;}
  for(uint64_t i=0;i<NOFF;i++) off[i]=(uint32_t)((rng()%(ARENA/128))*128);  // 128B-aligned, shuffled

  uint64_t ITERS = argc>1?strtoull(argv[1],0,10):NOFF;  // number of load executions
  if(ITERS>NOFF) ITERS=NOFF;
  int dit = argc>2?atoi(argv[2]):0;

  // warmup passes to train the LVP on the single load instruction address
  const int WARM=argc>3?atoi(argv[3]):50;

  struct { const char*name; uint64_t(*f)(const uint32_t*,const uint8_t*,uint64_t); } K[]={
    {"byte-load ldrb", chase_b},
    {"word-load ldr  ", chase_w},
  };
  printf("calib %.3f GHz | ARENA=%lluMB ITERS=%llu DIT=%d warmup=%d\n",
         g_ghz, ARENA>>20, (unsigned long long)ITERS, dit, WARM);
  printf("  %-16s %12s %12s %8s\n","kernel","CONST(ns/op)","RANDOM(ns/op)","ratio");
  volatile uint64_t sink=0;
  for(unsigned k=0;k<2;k++){
    // CONTROL: constant fill
    memset(mem, 0x5A, ARENA);
    double cbest=1e30;
    for(int r=0;r<15;r++){ set_dit(dit);
      for(int w=0;w<WARM;w++) sink+=K[k].f(off,mem,ITERS);
      double t0=now_ns(); sink+=K[k].f(off,mem,ITERS); double t=now_ns()-t0;
      if(dit&&!dit_on()){fprintf(stderr,"DIT lost\n");return 1;} set_dit(0);
      if(t<cbest)cbest=t; }
    // EXPERIMENT: random fill
    for(uint64_t i=0;i<ARENA;i+=8)*(uint64_t*)(mem+i)=rng();
    double rbest=1e30;
    for(int r=0;r<15;r++){ set_dit(dit);
      for(int w=0;w<WARM;w++) sink+=K[k].f(off,mem,ITERS);
      double t0=now_ns(); sink+=K[k].f(off,mem,ITERS); double t=now_ns()-t0;
      if(dit&&!dit_on()){fprintf(stderr,"DIT lost\n");return 1;} set_dit(0);
      if(t<rbest)rbest=t; }
    printf("  %-16s %12.3f %12.3f %8.3fx\n",K[k].name,
           cbest/ITERS, rbest/ITERS, rbest/cbest);
  }
  printf("(sink %llu)\n",(unsigned long long)sink);
  return 0;
}
