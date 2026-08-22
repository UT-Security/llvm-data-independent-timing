/* flowprobe.c - complex-flow probes for the taint pass.
 *
 * Each case is a self-contained channel from a seeded secret to a consumer that
 * computes on it. The GROUND TRUTH is stated per case: which functions must be
 * instrumented for the analysis to be sound. A consumer that computes on the
 * secret and carries ZERO `msr DIT` is an UNDER-TAINT - a secret running with
 * DIT off, which is a leak, not a cost.
 *
 * The four negative cases were predicted by reading the implementation, before
 * running anything:
 *   C1  return taint is Data-only; there is no ReturnsPointeeTainted field, so a
 *       function returning a POINTER to secret memory transfers nothing.
 *   C2  global taint travels only along call edges - it lives in the per-function
 *       TaintState and the only cross-function carrier is a callee's mod-set
 *       applied AT ITS CALL SITE. A sibling reader is analysed with its own entry
 *       state and never learns the global is secret.
 *   C3  INLINEASM is not isCall() and normally carries no MMO, so neither the
 *       store handler nor the clobber path runs. An asm that writes a secret
 *       through a pointer is invisible.
 *   C4  taint does not cross a register-tuple boundary in either direction
 *       (isSinglePhysReg rejects $q0_q1), so a secret moved through a NEON
 *       ld2/st2 pair may be dropped.
 *
 * P1/P2 are positive controls: if THEY come out clean the harness is broken, not
 * the analysis - the same logic as keeping lvp_chase in-band.
 *
 * Everything is `noinline`; at -O2 the inliner otherwise deletes the call edges
 * before the MIR pass ever runs and the probe silently tests nothing.
 */
#include <stdio.h>
#include <string.h>
#include <arm_neon.h>

#define NOINL __attribute__((noinline))

static unsigned long sink;

/* ---------------- P1: direct argument (positive control) ---------------- */
/* GROUND TRUTH: p1_consume must be instrumented. */
NOINL unsigned long p1_consume(const unsigned char *sk) {
    unsigned long a = 0;
    for (int i = 0; i < 32; i++) a = a * 131 + sk[i];
    return a;
}

/* ---------------- P2: four-level chain (positive control) --------------- */
/* GROUND TRUTH: all of p2_a..p2_d instrumented. */
NOINL unsigned long p2_d(const unsigned char *s) {
    unsigned long a = 0;
    for (int i = 0; i < 32; i++) a = a * 131 + s[i];
    return a;
}
NOINL unsigned long p2_c(const unsigned char *s) { return p2_d(s) ^ 0x5a; }
NOINL unsigned long p2_b(const unsigned char *s) { return p2_c(s) + 1; }
NOINL unsigned long p2_a(const unsigned char *s) { return p2_b(s); }

/* ---------------- C1: function returns a POINTER to secret -------------- */
/* GROUND TRUTH: c1_consume computes on the secret -> must be instrumented. */
static unsigned char c1_buf[32];
NOINL unsigned char *c1_produce(const unsigned char *sk) {
    for (int i = 0; i < 32; i++) c1_buf[i] = sk[i];   /* manual copy, not memcpy:
                                                        memcpy is an external call
                                                        and would poison memory
                                                        wholesale, masking the gap */
    return c1_buf;
}
NOINL unsigned long c1_consume(const unsigned char *p) {
    unsigned long a = 0;
    for (int i = 0; i < 32; i++) a = a * 131 + p[i];
    return a;
}

/* ---------------- C2: global as a channel, siblings --------------------- */
/* GROUND TRUTH: c2_reader computes on the secret -> must be instrumented.
 * There is NO call edge from c2_writer to c2_reader; both are called by main. */
static unsigned char c2_chan[32];
NOINL void c2_writer(const unsigned char *sk) {
    for (int i = 0; i < 32; i++) c2_chan[i] = sk[i];
}
NOINL unsigned long c2_reader(void) {
    unsigned long a = 0;
    for (int i = 0; i < 32; i++) a = a * 131 + c2_chan[i];
    return a;
}

/* ---------------- C3: inline asm stores the secret ---------------------- */
/* GROUND TRUTH: c3_consume computes on the secret -> must be instrumented. */
NOINL void c3_asm_store(const unsigned char *sk, unsigned char *dst) {
    const unsigned long *s = (const unsigned long *)sk;
    for (int i = 0; i < 4; i++) {
        unsigned long v = s[i];
        __asm__ volatile("str %x0, [%1]" :: "r"(v), "r"(dst + i * 8) : "memory");
    }
}
NOINL unsigned long c3_consume(const unsigned char *p) {
    unsigned long a = 0;
    for (int i = 0; i < 32; i++) a = a * 131 + p[i];
    return a;
}

/* ---------------- C4: secret through a NEON register tuple -------------- */
/* GROUND TRUTH: c4_consume computes on the secret -> must be instrumented. */
NOINL void c4_tuple(const unsigned char *sk, unsigned char *dst) {
    uint8x16x2_t v = vld2q_u8(sk);      /* loads into a $q0_q1 register tuple */
    vst2q_u8(dst, v);
}
NOINL unsigned long c4_consume(const unsigned char *p) {
    unsigned long a = 0;
    for (int i = 0; i < 32; i++) a = a * 131 + p[i];
    return a;
}

int main(void) {
    unsigned char sk[32];
    for (int i = 0; i < 32; i++) sk[i] = (unsigned char)(0x40 + i * 7);

    sink += p1_consume(sk);
    sink += p2_a(sk);

    sink += c1_consume(c1_produce(sk));

    c2_writer(sk);
    sink += c2_reader();

    static unsigned char c3_dst[32];
    c3_asm_store(sk, c3_dst);
    sink += c3_consume(c3_dst);

    static unsigned char c4_dst[32];
    c4_tuple(sk, c4_dst);
    sink += c4_consume(c4_dst);

    printf("flowprobe sink=%lu\n", sink);
    return 0;
}
