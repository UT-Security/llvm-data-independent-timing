// DIT-sensitivity gate for int8 MAC (quantized inference kernel).
//
// Motivation (deep-research 2026-07-22): THOR (IEEE LCA'25) and FeatureBleed
// demonstrate value-dependent timing in on-CPU int8 matmul via hardware ZERO-SKIP
// (Intel AMX); FLOP (USENIX'25) shows the Apple M-series LVP is a value-timing
// channel that DIT disables. The open question this benchmark answers on THIS M4:
// does a general-CPU int8 multiply-accumulate loop actually leak activation values
// via the LVP (or any value-dependent timing), i.e. is DIT-on measurably slower
// than DIT-off on zero-heavy (LVP-friendly) activations? If yes -> DIT-sensitive
// (unlike firefox_convolve, 0.968x). If no -> the general ARM CPU MAC is not
// LVP-exploitable the way the AMX zero-skip accelerator is, an important negative.
//
// The kernel is a small int8 GEMM: W[M][K] (public weights) x a[K] (SECRET
// activations) -> out[M], accumulating int32. We vary the SECRET activation data:
//   ZERO   : all activations 0        (maximally LVP-predictable / zero-skippable)
//   CONST  : all activations 7        (LVP-predictable, nonzero)
//   SPARSE : ~90% zeros (ReLU-like)   (the realistic quantized pattern)
//   RAND   : random int8              (LVP-unpredictable baseline)
// Weights are fixed random (public) in all cases. Two loop shapes are timed:
//   PAR : the natural GEMM (loads MLP-parallel, LVP benefit may be hidden)
//   DEP : a serial-accumulator variant where each MAC's activation load feeds the
//         critical path (LVP benefit exposed) — tests whether the mechanism EXISTS
//         even if the parallel loop hides it.
// For each (data x shape) we time DIT-off vs DIT-on and report on/off. A ratio
// > 1 means DIT (LVP disabled) costs performance => DIT-sensitive on that pattern.
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
static uint64_t rs=0x9E3779B97F4A7C15ull;
static uint64_t rng(void){rs^=rs<<13;rs^=rs>>7;rs^=rs<<17;return rs;}
static void set_dit(int on){ if(on)__asm__ volatile("msr DIT,#1":::"memory"); else __asm__ volatile("msr DIT,#0":::"memory"); }
static int dit_on(void){uint64_t d;__asm__ volatile("mrs %0,DIT":"=r"(d));return (d>>24)&1;}

#define M 64            // output rows (weights M x K)
#define K 512           // reduction length (secret activation vector)
#define REPS 4000ull    // GEMM invocations per timed run

static int8_t W[M*K];
static int8_t A[K];
static int32_t OUT[M];

// PAR: natural GEMM inner loop — loads are index-addressed, MLP can prefetch.
static void __attribute__((noinline)) gemm_par(const int8_t* w, const int8_t* a, int32_t* out){
  for(int m=0;m<M;m++){
    int32_t acc=0;
    const int8_t* wr=w+m*K;
    for(int k=0;k<K;k++) acc += (int32_t)wr[k]*(int32_t)a[k];
    out[m]=acc;
  }
}

// DEP: serial variant — the loaded activation is folded into the index of the next
// activation load (clamped to [0,K)), so the activation load sits on the critical
// path and an LVP prediction of its value shortcuts the chain. Same MAC arithmetic.
static void __attribute__((noinline)) gemm_dep(const int8_t* w, const int8_t* a, int32_t* out){
  for(int m=0;m<M;m++){
    int32_t acc=0; uint32_t idx=(uint32_t)m & (K-1);
    const int8_t* wr=w+m*K;
    for(int k=0;k<K;k++){
      int32_t av=(int32_t)a[idx];
      acc += (int32_t)wr[k]*av;
      idx = (idx + 1u + (uint32_t)(av & 3)) & (K-1);   // next load addr depends on loaded value
    }
    out[m]=acc;
  }
}

static void fill(int mode){
  for(int k=0;k<K;k++){
    switch(mode){
      case 0: A[k]=0; break;                                   // ZERO
      case 1: A[k]=7; break;                                   // CONST
      case 2: A[k]=(rng()%10==0)?(int8_t)(rng()&0x7f):0; break;// SPARSE ~90% zero
      default:A[k]=(int8_t)(rng()&0xff); break;                // RAND
    }
  }
}

typedef void (*kern_t)(const int8_t*,const int8_t*,int32_t*);
static double best(kern_t f,int dit){
  double b=1e30;
  for(int r=0;r<11;r++){ set_dit(dit);
    for(int w=0;w<50;w++) f(W,A,OUT);                    // warm/train LVP + caches
    double t0=now_ns();
    for(uint64_t i=0;i<REPS;i++) f(W,A,OUT);
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

  for(int i=0;i<M*K;i++) W[i]=(int8_t)(rng()&0xff);     // fixed random public weights

  printf("calib %.3f GHz | GEMM %dx%d, %llu reps | %llu MACs/timed-run\n\n",
         g_ghz,M,K,REPS,(uint64_t)M*K*REPS);
  const char* names[4]={"ZERO   (all 0)","CONST  (all 7)","SPARSE (~90%% 0)","RAND   (uniform)"};
  struct{const char*n;kern_t f;} shapes[2]={{"PAR (MLP-parallel)",gemm_par},{"DEP (load on crit path)",gemm_dep}};

  for(int s=0;s<2;s++){
    printf("== %s ==\n",shapes[s].n);
    printf("  %-18s %12s %12s %10s\n","activations","off ns/run","on ns/run","on/off");
    for(int mode=0;mode<4;mode++){
      fill(mode);
      double off=best(shapes[s].f,0), on=best(shapes[s].f,1);
      printf("  %-18s %12.1f %12.1f %9.3fx%s\n",names[mode],off,on,on/off,
             (on/off>1.03)?"  <-- DIT-sensitive":"");
    }
    printf("\n");
  }
  return 0;
}
