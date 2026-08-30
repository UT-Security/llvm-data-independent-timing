// Silicon cost of the three PSTATE.DIT exit forms for the callee-saved ABI
// (docs/design/dit-abi.md OPEN item 1), as a function of guard predictability.
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <sys/qos.h>

#define DITBIT (1ULL << 24)
#define NPAT   65536
#define PMASK  (NPAT - 1)

extern uint64_t exit_noop(uint64_t), exit_imm(uint64_t), exit_reg(uint64_t), exit_grd(uint64_t);
extern uint64_t b_noop(uint64_t, uint64_t), b_reg(uint64_t, uint64_t), b_grd(uint64_t, uint64_t), b_brctl(uint64_t, uint64_t);

static mach_timebase_info_data_t g_tb;
static double g_ghz;
static inline double now_ns(void){ return (double)mach_absolute_time()*g_tb.numer/g_tb.denom; }

static uint64_t pat[NPAT];
static void fill(int kind){                       // 0 all-off 1 all-on 2 alt 3 random
  uint64_t s = 0x9E3779B97F4A7C15ULL;
  for (int i=0;i<NPAT;i++){
    int b;
    if(kind==0) b=0; else if(kind==1) b=1; else if(kind==2) b=i&1;
    else { s^=s<<13; s^=s>>7; s^=s<<17; b=(int)(s&1); }
    pat[i] = b ? DITBIT : 0;
  }
}

// Experiment A: caller establishes a REAL entry DIT state, then calls.
static double runA(uint64_t (*fn)(uint64_t), uint64_t n){
  double t0=now_ns(); uint64_t acc=0;
  for(uint64_t i=0;i<n;i++){
    uint64_t v = pat[i & PMASK];
    __asm__ volatile("msr DIT, %0" :: "r"(v) : "memory");
    acc += fn(acc);
  }
  double d=now_ns()-t0; __asm__ volatile("" :: "r"(acc)); return d;
}
// Experiment B: guard value passed in x1, nothing serializing before the branch.
static double runB(uint64_t (*fn)(uint64_t,uint64_t), uint64_t n){
  double t0=now_ns(); uint64_t acc=0;
  for(uint64_t i=0;i<n;i++) acc += fn(acc, pat[i & PMASK]);
  double d=now_ns()-t0; __asm__ volatile("" :: "r"(acc)); return d;
}
static double bestA(uint64_t (*f)(uint64_t), uint64_t n){
  double b=1e30; for(int r=0;r<11;r++){ double t=runA(f,n); if(t<b)b=t; } return b; }
static double bestB(uint64_t (*f)(uint64_t,uint64_t), uint64_t n){
  double b=1e30; for(int r=0;r<11;r++){ double t=runB(f,n); if(t<b)b=t; } return b; }

int main(void){
  mach_timebase_info(&g_tb);
  pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
  const uint64_t N = 2000000;
  const char *names[4] = {"entered DIT=0 (always clear)","entered DIT=1 (always skip)",
                          "alternating","random"};

  fill(0);
  double cal = bestA(exit_noop, N);              // includes caller msr + call
  g_ghz = 0.0;
  { // calibrate GHz off an empty dependent-add loop
    double t0=now_ns(); volatile uint64_t x=0;
    for(uint64_t i=0;i<50000000ULL;i++) x+=1;
    double d=now_ns()-t0; g_ghz = 50000000.0/d;  // 1 add/iter ~ 1 cyc
  }
  printf("Apple M5, calibration %.2f GHz  (rough; ratios below are the result)\n", g_ghz);
  printf("N=%llu per rep, best of 11, QOS user-interactive\n\n", (unsigned long long)N);

  printf("=== A. faithful: caller sets real DIT per call (cycles per call, minus noop) ===\n");
  printf("  %-30s %10s %10s %10s\n","pattern","imm(today)","reg","guarded");
  for(int k=0;k<4;k++){
    fill(k);
    double base = bestA(exit_noop,N);
    double a = (bestA(exit_imm,N)-base)*g_ghz/N;
    double b = (bestA(exit_reg,N)-base)*g_ghz/N;
    double c = (bestA(exit_grd,N)-base)*g_ghz/N;
    printf("  %-30s %10.2f %10.2f %10.2f\n", names[k], a, b, c);
  }

  printf("\n=== B. predictor isolation: guard value in x1, no serializing write ===\n");
  printf("  %-30s %10s %10s %10s\n","pattern","reg","guarded","BRANCH ONLY");
  for(int k=0;k<4;k++){
    fill(k);
    double base = bestB(b_noop,N);
    double b = (bestB(b_reg,N)-base)*g_ghz/N;
    double c = (bestB(b_grd,N)-base)*g_ghz/N;
    double d = (bestB(b_brctl,N)-base)*g_ghz/N;
    printf("  %-30s %10.2f %10.2f %10.2f\n", names[k], b, c, d);
  }
  printf("\n  BRANCH ONLY is the control: same tbnz, trivial body. If it reads ~0 on\n"
         "  'random' then the predictor learned the pattern and NO mispredict was measured.\n");
  __asm__ volatile("msr DIT, #0" ::: "memory");
  return 0;
}
