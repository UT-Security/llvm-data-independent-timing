// STEP 1/2 (userspace-clean probe) — isolate the Apple M4 LVP via a self-
// dependent load chain, and test whether DIT disables it.
//
// The independent-gather of FLOP Listing 1 can't be isolated from userspace:
// memory-level parallelism overlaps the cache misses, and the warmup needed to
// TRAIN the LVP also warms the CACHE (killing the misses). FLOP used the Kernel
// Debug Kit to clflush while keeping the LVP trained; we can't.
//
// Instead: a LOAD-TO-ADDRESS chase `x = arr[x]`, so the load is on the critical
// path (the next address depends on the loaded value — MLP cannot hide it).
//   CONST arr (all == C): x settles to C, the load ALWAYS returns C, address
//     fixed -> L1 hit AND LVP-predictable. If the LVP is on, it predicts C and
//     breaks the load->address dependency -> throughput ~1 op/cyc.
//   PERM arr (random permutation cycle): each hop loads a different value,
//     L1-resident but the ~3-4cyc L1 latency is exposed on the chase, LVP can't
//     predict -> ~L1-latency/hop.
// The decisive project test is DIT on the CONST chase: same code+data, DIT
// flipped. If DIT disables the LVP, the CONST chase slows toward the PERM chase.
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <sys/qos.h>

static mach_timebase_info_data_t g_tb;
static double g_ghz;
static inline double now_ns(void){return (double)mach_absolute_time()*g_tb.numer/g_tb.denom;}
static uint64_t rs=0x9E3779B97F4A7C15ull;
static uint64_t rng(void){rs^=rs<<13;rs^=rs>>7;rs^=rs<<17;return rs;}
static void set_dit(int on){ if(on)__asm__ volatile("msr DIT,#1":::"memory"); else __asm__ volatile("msr DIT,#0":::"memory"); }
static int dit_on(void){uint64_t d;__asm__ volatile("mrs %0,DIT":"=r"(d));return (d>>24)&1;}

// x = arr[x] chase, N hops. 4-byte loads. Single static load instruction.
static uint32_t __attribute__((noinline)) chase(const uint32_t*arr,uint32_t x,uint64_t n){
  __asm__ volatile(
    "1:\n"
    "ldr %w[x], [%[a], %w[x], uxtw #2]\n"  // x = arr[x] : load-to-address dep
    "subs %[n], %[n], #1\n"
    "b.ne 1b\n"
    : [x]"+r"(x),[n]"+r"(n) : [a]"r"(arr) : "cc","memory");
  return x;
}

#define NENT 1024                 // 4 KB -> L1-resident
#define HOPS 20000000ull

static double best(const uint32_t*arr,uint32_t x0,int dit){
  double b=1e30;
  for(int r=0;r<11;r++){ set_dit(dit);
    volatile uint32_t s=chase(arr,x0,2000);            // warm/train LVP
    (void)s;
    double t0=now_ns(); volatile uint32_t r2=chase(arr,x0,HOPS); (void)r2;
    double t=now_ns()-t0;
    if(dit&&!dit_on()){fprintf(stderr,"DIT lost\n");exit(1);} set_dit(0);
    if(t<b)b=t; }
  return b;
}

int main(void){
  mach_timebase_info(&g_tb);
  pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE,0);
  { uint64_t i=5000000; double t0=now_ns();
    __asm__ volatile("mov x9,%[n]\n1:\n"
#define A8 "add x0,x0,#1\nadd x0,x0,#1\nadd x0,x0,#1\nadd x0,x0,#1\n"
      A8 A8 A8 A8 A8 A8 A8 A8 "subs x9,x9,#1\nb.ne 1b\n":: [n]"r"(i):"x0","x9","cc");
    g_ghz=(double)i*32.0/(now_ns()-t0); }

  uint32_t*cst=malloc(NENT*4),*prm=malloc(NENT*4);
  const uint32_t C=7;
  for(int i=0;i<NENT;i++) cst[i]=C;                    // constant -> LVP-predictable
  for(int i=0;i<NENT;i++) prm[i]=i;                    // random permutation cycle
  for(int i=NENT-1;i>0;i--){int j=rng()%(i+1);uint32_t t=prm[i];prm[i]=prm[j];prm[j]=t;}

  printf("calib %.3f GHz | %d entries (L1-resident), %llu hops\n\n",g_ghz,NENT,HOPS);
  printf("  %-26s %10s %10s\n","chase","cyc/hop","ns/hop");

  double c_off=best(cst,C,0), c_on=best(cst,C,1);
  double p_off=best(prm,0,0), p_on=best(prm,0,1);
#define cyc(t) ((t)*g_ghz/HOPS)
  printf("  %-26s %10.3f %10.4f\n","CONST value, DIT off",cyc(c_off),c_off/HOPS);
  printf("  %-26s %10.3f %10.4f  <-- DIT disables LVP?\n","CONST value, DIT on ",cyc(c_on),c_on/HOPS);
  printf("  %-26s %10.3f %10.4f\n","PERM  value, DIT off",cyc(p_off),p_off/HOPS);
  printf("  %-26s %10.3f %10.4f\n","PERM  value, DIT on ",cyc(p_on),p_on/HOPS);
  printf("\n  CONST speedup from LVP (DIT off): PERM/CONST = %.2fx\n",p_off/c_off);
  printf("  DIT-on penalty on CONST (LVP off): on/off    = %.2fx\n",c_on/c_off);
  return 0;
}
