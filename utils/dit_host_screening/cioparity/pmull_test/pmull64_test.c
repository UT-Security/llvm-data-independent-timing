/*
 * pmull64_test.c - known-answer and property test for PMULL/PMULL2 at size=3,
 * the 64x64 -> 128 carry-less multiply (FEAT_PMULL).
 *
 * WHY IT IS BUILT THIS WAY. gem5's implementation of this instruction is a
 * shift-and-xor loop over Element/BigElement. Checking it against a
 * shift-and-xor loop written here would compare an algorithm with itself and
 * prove nothing, so this test does not do that. Instead:
 *
 *   1. ALGEBRAIC known answers, derived from the definition of multiplication
 *      in GF(2)[x] rather than computed by any loop. clmul(3,3) = 5 because
 *      (1+x)^2 = 1 + x^2 over GF(2), the cross term 2x vanishing. Likewise
 *      clmul(2^i, 2^j) = 2^(i+j), and clmul(all-ones, all-ones) = 0x5555...
 *      because (sum_{i<64} x^i)^2 = sum_{i<64} x^{2i}, every cross term
 *      appearing an even number of times.
 *
 *   2. STRUCTURAL properties that a wrong implementation is unlikely to satisfy
 *      by accident: commutativity, and bilinearity over GF(2), i.e.
 *      clmul(a ^ a2, b) == clmul(a,b) ^ clmul(a2,b). Bilinearity is the strong
 *      one -- it constrains the whole map, not individual points.
 *
 *   3. OPERAND SELECTION, which is the part most likely to be wrong and which
 *      no arithmetic check would catch: PMULL must read the LOW 64-bit element
 *      of each source and PMULL2 the HIGH one. Tested with vectors whose two
 *      halves differ.
 *
 * And the real ground truth: this same binary runs natively on the Neoverse-N1
 * host, which implements FEAT_PMULL in hardware. Native output is the reference;
 * the simulator must reproduce it byte for byte. That is an independent oracle,
 * not a restatement of the model.
 */
#include <stdio.h>
#include <stdint.h>

typedef unsigned __int128 u128;

/* PMULL Vd.1Q, Vn.1D, Vm.1D -- low 64-bit element of each source. */
static inline u128 pmull_lo(uint64_t a, uint64_t b) {
    u128 r;
    __asm__ volatile(
        "fmov d0, %1\n\t"
        "fmov d1, %2\n\t"
        "pmull v2.1q, v0.1d, v1.1d\n\t"
        "str q2, %0\n\t"
        : "=m"(r) : "r"(a), "r"(b) : "v0", "v1", "v2");
    return r;
}

/* PMULL2 Vd.1Q, Vn.2D, Vm.2D -- HIGH 64-bit element of each source. */
static inline u128 pmull2_hi(uint64_t a_lo, uint64_t a_hi,
                             uint64_t b_lo, uint64_t b_hi) {
    u128 r;
    __asm__ volatile(
        "fmov d0, %1\n\t"
        "mov v0.d[1], %2\n\t"
        "fmov d1, %3\n\t"
        "mov v1.d[1], %4\n\t"
        "pmull2 v2.1q, v0.2d, v1.2d\n\t"
        "str q2, %0\n\t"
        : "=m"(r) : "r"(a_lo), "r"(a_hi), "r"(b_lo), "r"(b_hi)
        : "v0", "v1", "v2");
    return r;
}

static int fails = 0;
static void ck(const char *what, u128 got, u128 want) {
    if (got != want) {
        fails++;
        printf("  FAIL %-46s got %016llx%016llx want %016llx%016llx\n", what,
               (unsigned long long)(uint64_t)(got >> 64),
               (unsigned long long)(uint64_t)got,
               (unsigned long long)(uint64_t)(want >> 64),
               (unsigned long long)(uint64_t)want);
    } else {
        printf("  ok   %-46s %016llx%016llx\n", what,
               (unsigned long long)(uint64_t)(got >> 64),
               (unsigned long long)(uint64_t)got);
    }
}

int main(void) {
    printf("== PMULL/PMULL2 size=3 (64x64 -> 128) ==\n");

    /* --- 1. algebraic known answers ------------------------------------ */
    ck("clmul(0,0) = 0", pmull_lo(0, 0), (u128)0);
    ck("clmul(1,x) = x, x=0xdeadbeefcafef00d",
       pmull_lo(1, 0xdeadbeefcafef00dULL), (u128)0xdeadbeefcafef00dULL);
    ck("clmul(2,2) = 4  [x*x = x^2]", pmull_lo(2, 2), (u128)4);
    ck("clmul(3,3) = 5  [(1+x)^2 = 1+x^2]", pmull_lo(3, 3), (u128)5);
    ck("clmul(7,7) = 0x15  [(1+x+x^2)^2]", pmull_lo(7, 7), (u128)0x15);
    /* 2^63 * 2^63 = x^126 */
    ck("clmul(1<<63,1<<63) = 1<<126",
       pmull_lo(1ULL << 63, 1ULL << 63), ((u128)1) << 126);
    /* 2^40 * 2^23 = x^63 -- crosses the 64-bit boundary of the result */
    ck("clmul(1<<40,1<<23) = 1<<63",
       pmull_lo(1ULL << 40, 1ULL << 23), ((u128)1) << 63);
    /* 2^40 * 2^24 = x^64 -- first bit of the HIGH half */
    ck("clmul(1<<40,1<<24) = 1<<64",
       pmull_lo(1ULL << 40, 1ULL << 24), ((u128)1) << 64);
    /* (sum_{i<64} x^i)^2 = sum_{i<64} x^{2i} = bits 0,2,...,126 */
    {
        u128 want = 0;
        for (int i = 0; i < 64; i++) want |= ((u128)1) << (2 * i);
        ck("clmul(~0,~0) = 0x5555...5555", pmull_lo(~0ULL, ~0ULL), want);
    }

    /* --- 2. structural properties -------------------------------------- */
    {
        const uint64_t a = 0x0123456789abcdefULL, a2 = 0xfedcba9876543210ULL,
                       b = 0x00ff00ff00ff00ffULL;
        ck("commutative: clmul(a,b) == clmul(b,a)",
           pmull_lo(a, b), pmull_lo(b, a));
        /* bilinearity over GF(2) */
        ck("bilinear: clmul(a^a2,b) == clmul(a,b)^clmul(a2,b)",
           pmull_lo(a ^ a2, b), pmull_lo(a, b) ^ pmull_lo(a2, b));
        ck("bilinear in 2nd arg",
           pmull_lo(b, a ^ a2), pmull_lo(b, a) ^ pmull_lo(b, a2));
    }

    /* --- 3. operand selection ------------------------------------------ */
    {
        /* halves deliberately different, so reading the wrong one is visible */
        const uint64_t alo = 3, ahi = 7, blo = 3, bhi = 7;
        ck("PMULL  reads LOW  halves  -> clmul(3,3) = 5",
           pmull_lo(alo, blo), (u128)5);
        ck("PMULL2 reads HIGH halves  -> clmul(7,7) = 0x15",
           pmull2_hi(alo, ahi, blo, bhi), (u128)0x15);
        /* and PMULL2 must NOT pick up the low half */
        ck("PMULL2 ignores LOW halves",
           pmull2_hi(0xffffffffffffffffULL, 2, 0xffffffffffffffffULL, 2),
           (u128)4);
    }

    printf(fails ? "\nFAIL: %d check(s)\n" : "\nPASS: all checks\n", fails);
    return fails ? 1 : 0;
}
