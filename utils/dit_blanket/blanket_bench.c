/*
 * blanket_bench.c -- what does PSTATE.DIT cost when you just turn it on?
 *
 * A deliberately small driver for ONE question: the ratio between a libsodium
 * primitive run with DIT set for the whole process and the same primitive in
 * the same binary with DIT clear. No compiler pass, no seeds, no placement, no
 * arms table. Blanket DIT is `msr dit, #1` before any work and never cleared,
 * so measuring it needs no toolchain at all -- any clang and the stock library.
 *
 * WHY THIS EXISTS SEPARATELY FROM cio_arm_shim.h. That shim instruments every
 * region so it can attribute cost to individual switches, and pays a counter
 * read at each boundary. Rooted, that read is kperf: ~3,400 cycles and ~17,700
 * instructions, against a ~275-cycle AES-GCM operation. It is the right design
 * for a selective-placement experiment and the wrong one here, because it
 * compresses every percentage by an offset it cannot subtract. Blanket needs no
 * per-region attribution -- the mode is set once and never changes -- so this
 * driver reads the counters ONCE around a long loop and lets N amortise them.
 * At the default N the pair costs under 0.01% of the measured total.
 *
 *   ./blanket_bench <primitive> <iters> <warmup>
 *   BLANKET_DIT=1 -> msr dit, #1 before any setup, never cleared
 *
 * Emits one CSV line on stdout, everything else on stderr.
 *
 * WHAT THE ARMS ARE. Exactly one binary. The two arms are two invocations of
 * this same file with BLANKET_DIT=0 and BLANKET_DIT=1. There is no second
 * build, so there is no layout difference to control for, which is why this
 * driver ships no NOP twin: the twin exists to separate switch cost from code
 * placement, and identical code at identical addresses has no placement delta.
 */
#include <sodium.h>
#include <pthread/qos.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "perf.c"   /* kpc_get_thread_counters, perf_init, perf_start */

static int kperf_ok = 0;

static inline uint64_t now_ticks(void) {   /* CNTVCT_EL0: 1 ns/tick here, TIME */
    uint64_t t; __asm__ volatile("isb\n\tmrs %0, cntvct_el0" : "=r"(t));
    return t;
}
static inline uint64_t dit_bit(void) {
    uint64_t d; __asm__ volatile("mrs %0, dit" : "=r"(d)); return (d >> 24) & 1;
}
/* Both counters from ONE call, so they cover the same window. Reading them in
 * two calls is what once produced an IPC of 12 on an 8-wide core. */
static void counters(uint64_t *cyc, uint64_t *ins) {
    *cyc = 0; *ins = 0;
    if (!kperf_ok) return;
    uint64_t buf[KPC_MAX_COUNTERS] = {0};
    if (kpc_get_thread_counters(0, KPC_MAX_COUNTERS, buf) == 0) { *cyc = buf[0]; *ins = buf[1]; }
}

/* ---------------------------------------------------------------- workloads */
#define MLEN 1024
static unsigned char msg[MLEN], out[MLEN + 64], sk[64], pk[32], key[32], npub[12], sig[64];
static unsigned long long outlen;

/* The positive control. A pointer chase whose stride depends on the value it
 * just loaded is exactly what DIT is specified to make constant-time, so this
 * MUST get slower when DIT is on. Without it a flat result is ambiguous: it
 * could mean DIT is free, or it could mean DIT never got set. */
#define CHASE_N (1 << 14)
static uint32_t chase[CHASE_N];
static volatile uint32_t chase_sink;

static void setup(const char *what) {
    /* DETERMINISTIC, and it has to be. Each arm is a separate process, so a
     * randomised setup gives the two arms different data to work on. Most of
     * these primitives are data-independent in instruction count and hide it;
     * ed25519 verification is not, and read 0.450% apart on 2026-09-06 -- which
     * gate 2 caught. Same bytes in both arms, or the comparison is not one. */
    for (int i = 0; i < MLEN; i++) msg[i] = (unsigned char)(i * 31 + 7);
    for (int i = 0; i < 32; i++)   key[i]  = (unsigned char)(i * 17 + 3);
    for (int i = 0; i < 12; i++)   npub[i] = (unsigned char)(i * 13 + 5);
    unsigned char seed[crypto_sign_SEEDBYTES];
    for (unsigned i = 0; i < sizeof seed; i++) seed[i] = (unsigned char)(i * 11 + 1);
    crypto_sign_seed_keypair(pk, sk, seed);
    for (int i = 0; i < CHASE_N; i++) chase[i] = (uint32_t)((i * 2654435761u) % CHASE_N);
    if (!strcmp(what, "chacha_dec"))
        crypto_aead_chacha20poly1305_ietf_encrypt(out, &outlen, msg, MLEN, NULL, 0, NULL, npub, key);
    else if (!strcmp(what, "aes_dec"))
        crypto_aead_aes256gcm_encrypt(out, &outlen, msg, MLEN, NULL, 0, NULL, npub, key);
    else if (!strcmp(what, "ed25519_open"))
        crypto_sign_detached(sig, NULL, msg, MLEN, sk);
}

/* Dispatch is resolved ONCE, before the timed loop. It used to be a strcmp
 * chain evaluated per iteration, which is a constant inside the measurement
 * window -- and a constant does not cancel in a RATIO. Measured overhead is
 * (T_C + D)/(T_A + D), which is pulled toward zero by exactly the D this rig
 * exists to avoid paying. An indirect call through a function pointer costs a
 * couple of cycles instead of five to seven string compares. */
typedef void (*op_fn)(void);

static unsigned char tmp[MLEN + 64];
static unsigned long long tmplen;

static void op_ed25519_sign(void) { crypto_sign_detached(sig, NULL, msg, MLEN, sk); }
static void op_ed25519_open(void) { crypto_sign_verify_detached(sig, msg, MLEN, pk); }
static void op_chacha_enc(void)   { crypto_aead_chacha20poly1305_ietf_encrypt(out, &outlen, msg, MLEN, NULL, 0, NULL, npub, key); }
static void op_chacha_dec(void)   { crypto_aead_chacha20poly1305_ietf_decrypt(tmp, &tmplen, NULL, out, outlen, NULL, 0, npub, key); }
static void op_aes_enc(void)      { crypto_aead_aes256gcm_encrypt(out, &outlen, msg, MLEN, NULL, 0, NULL, npub, key); }
static void op_aes_dec(void)      { crypto_aead_aes256gcm_decrypt(tmp, &tmplen, NULL, out, outlen, NULL, 0, npub, key); }
static void op_control(void)      { uint32_t i = 0; for (int k = 0; k < 256; k++) i = chase[i]; chase_sink = i; }
/* The floor. Everything the loop costs with no crypto in it: the indirect call
 * and the loop itself. Subtract nothing automatically -- report it, so a row
 * whose op is close to this floor is visibly not resolvable. */
static void op_noop(void)         { __asm__ volatile("" ::: "memory"); }

static op_fn pick(const char *what) {
    if (!strcmp(what, "ed25519_sign")) return op_ed25519_sign;
    if (!strcmp(what, "ed25519_open")) return op_ed25519_open;
    if (!strcmp(what, "chacha_enc"))   return op_chacha_enc;
    if (!strcmp(what, "chacha_dec"))   return op_chacha_dec;
    if (!strcmp(what, "aes_enc"))      return op_aes_enc;
    if (!strcmp(what, "aes_dec"))      return op_aes_dec;
    if (!strcmp(what, "control"))      return op_control;
    if (!strcmp(what, "noop"))         return op_noop;
    return NULL;
}

int main(int argc, char **argv) {
    /* BEFORE anything else, including sodium_init(): blanket means the whole
     * process, and setup running under the mode is part of what blanket costs
     * a real deployment. Setup is not timed either way. */
    const char *e = getenv("BLANKET_DIT");
    if (e && e[0] == '1') __asm__ volatile("msr dit, #1\n\tisb" ::: "memory");

    const char *what = argc > 1 ? argv[1] : "aes_enc";
    long iters  = argc > 2 ? atol(argv[2]) : 200000;
    long warm   = argc > 3 ? atol(argv[3]) : 20000;

    /* P-cluster: QOS_CLASS_USER_INTERACTIVE is the only supported lever on a
     * stock kernel (kern.sched_thread_bind_cpu does not exist). */
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    if (sodium_init() < 0) { fprintf(stderr, "sodium_init failed\n"); return 1; }
    if (!strncmp(what, "aes", 3) && !crypto_aead_aes256gcm_is_available()) {
        fprintf(stderr, "aes256gcm not available on this CPU\n"); return 3;
    }
    kperf_ok = (perf_init("ditblanket") == 0) && (perf_start() == 0);
    setup(what);
    uint64_t cntfrq; __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(cntfrq));

    /* Warmup is outside the window: caches, branch predictors and the DVFS ramp
     * all settle here. A cold first iteration inside a median would survive it. */
    op_fn op = pick(what);
    if (!op) { fprintf(stderr, "unknown primitive: %s\n", what); return 2; }
    for (long i = 0; i < warm; i++) op();

    uint64_t c0, i0, c1, i1, t0, t1;
    counters(&c0, &i0); t0 = now_ticks();
    for (long i = 0; i < iters; i++) op();
    t1 = now_ticks(); counters(&c1, &i1);

    printf("%s,%s,%ld,%llu,%llu,%llu,%llu\n", what, (e && e[0] == '1') ? "C" : "A",
           iters, (unsigned long long)(c1 - c0), (unsigned long long)(i1 - i0),
           (unsigned long long)(t1 - t0), (unsigned long long)dit_bit());
    /* CNTFRQ_EL0 is the ONLY thing that sets the units of the tick column. Do not
     * substitute hw.tbfrequency: that is the 24 MHz Mach timebase and disagrees. */
    fprintf(stderr, "dit_exit=%llu cycles=%s cntfrq=%llu\n", (unsigned long long)dit_bit(),
            kperf_ok ? "kperf" : "none", (unsigned long long)cntfrq);
    return 0;
}
