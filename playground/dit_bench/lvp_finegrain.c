// STEP 3 — fine-grain DIT placement demo on M4.
// A region mixes PUBLIC LVP-friendly work (a constant-value chase, 4x faster
// with the LVP per lvp2) and SECRET arithmetic (dependent muls, standing in for
// secret-dependent computation that must run with DIT on). Three placements:
//   (i)   DIT off whole loop  : LVP on, public fast, SECRET UNPROTECTED (floor)
//   (ii)  DIT on  whole loop  : LVP off, public 4x slower, secret protected
//                               (the coarse / whole-process mitigation, e.g.
//                                FLOP's Safari-wide DIT)
//   (iii) FINE-GRAIN          : DIT off for the public chase, msr#1 around just
//                               the secret block, msr#0 after -> secret protected
//                               AND the LVP preserved for the public part.
// Thesis: (iii) beats (ii) iff the LVP saving on the public part per region
// exceeds the toggle cost (~30 cyc each, ~60/pair). Sweep P (public hops per
// region) to find the crossover.
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <sys/qos.h>

static mach_timebase_info_data_t g_tb; static double g_ghz;
static inline double now_ns(void){return (double)mach_absolute_time()*g_tb.numer/g_tb.denom;}
static void set_dit(int on){ if(on)__asm__ volatile("msr DIT,#1":::"memory"); else __asm__ volatile("msr DIT,#0":::"memory"); }
static int dit_on(void){uint64_t d;__asm__ volatile("mrs %0,DIT":"=r"(d));return (d>>24)&1;}

// T regions; each = P-hop constant chase (public) + S dependent muls (secret).
// fine=1 wraps ONLY the secret muls in msr#1/#0 (leaving the chase DIT-off).
static uint64_t __attribute__((noinline))
run(const uint32_t*arr,uint64_t T,uint32_t P,uint32_t S,int fine){
  uint32_t x=7; uint64_t sec=0x1234567;
  for(uint64_t t=0;t<T;t++){
    // public LVP-friendly chase
    __asm__ volatile("mov w4,%w[P]\n2:\n"
      "ldr %w[x],[%[a],%w[x],uxtw #2]\n"
      "subs w4,w4,#1\nb.ne 2b\n"
      :[x]"+r"(x):[a]"r"(arr),[P]"r"(P):"w4","cc","memory");
    if(fine) __asm__ volatile("msr DIT,#1":::"memory");
    // secret arithmetic (dependent muls)
    __asm__ volatile("mov w5,%w[S]\n3:\n"
      "mul %[s],%[s],%[s]\nadd %[s],%[s],#7\n"
      "subs w5,w5,#1\nb.ne 3b\n"
      :[s]"+r"(sec):[S]"r"(S):"w5","cc");
    if(fine) __asm__ volatile("msr DIT,#0":::"memory");
  }
  return x+sec;
}

static double best(const uint32_t*arr,uint64_t T,uint32_t P,uint32_t S,int dit,int fine){
  double b=1e30; volatile uint64_t sink=0;
  for(int r=0;r<9;r++){ set_dit(dit);
    sink+=run(arr,64,P,S,fine);                       // warm/train
    double t0=now_ns(); sink+=run(arr,T,P,S,fine); double t=now_ns()-t0;
    set_dit(0); if(t<b)b=t; }
  return b;
}

int main(int argc,char**argv){
  mach_timebase_info(&g_tb);
  pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE,0);
  { uint64_t i=5000000; double t0=now_ns();
    __asm__ volatile("mov x9,%[n]\n1:\n"
#define A8 "add x0,x0,#1\nadd x0,x0,#1\nadd x0,x0,#1\nadd x0,x0,#1\n"
      A8 A8 A8 A8 A8 A8 A8 A8 "subs x9,x9,#1\nb.ne 1b\n":: [n]"r"(i):"x0","x9","cc");
    g_ghz=(double)i*32.0/(now_ns()-t0); }
  const int NENT=1024; uint32_t*arr=malloc(NENT*4); for(int i=0;i<NENT;i++)arr[i]=7;

  const uint64_t T=2000000; const uint32_t S=8;   // 8 secret muls per region
  printf("calib %.3f GHz | %llu regions, S=%u secret muls/region\n",g_ghz,(unsigned long long)T,S);
  printf("verify: DIT restored to %d after runs\n\n",dit_on());
  printf("  %-4s %12s %12s %12s   %-22s\n","P","(i)off/UNPROT","(ii)whole-DIT","(iii)fine","winner vs (ii)");
  uint32_t Ps[]={4,8,16,32,64,128,256};
  for(unsigned k=0;k<sizeof(Ps)/sizeof(Ps[0]);k++){
    uint32_t P=Ps[k];
    double i_off=best(arr,T,P,S,0,0);     // whole DIT off (secret unprotected) - floor
    double ii   =best(arr,T,P,S,1,0);     // whole DIT on  (coarse mitigation)
    double iii  =best(arr,T,P,S,0,1);     // fine-grain (toggle around secret only)
    const char*w = iii<ii ? "FINE wins" : "whole-DIT wins";
    printf("  %-4u %12.1f %12.1f %12.1f   %-22s (fine %.2fx of coarse)\n",
           P, i_off/T*1000, ii/T*1000, iii/T*1000, w, iii/ii);
  }
  printf("\n(ns/region *1000 = ps/region; lower is faster)\n");
  return 0;
}
