/* The developer's bracket (experiment 11, arm B).
 *
 * What Apple's corecrypto does at the boundary of a routine that touches a
 * secret, written by hand at the entry of a PHP crypto builtin: set PSTATE.DIT,
 * then a speculation barrier so nothing after the write runs under the old
 * mode; clear the bit again on the way out. No analysis, no knowledge of where
 * the secret goes inside.
 *
 *   -DDIT_BRACKET=1                 emit the bracket
 *   -DDIT_BRACKET=1 -DDIT_BRACKET_NOP=1   emit `hint #0` in each place instead:
 *                                   the layout control (arm Bn), same
 *                                   instruction count and addresses, no switch.
 *   -DDIT_BRACKET=1 -DDIT_BRACKET_COUNT=1 count calls instead of switching, and
 *                                   dump them at exit. NOT a measurement arm --
 *                                   it perturbs the code it counts. It answers
 *                                   the x-axis question the crossover argument
 *                                   rests on: how many crypto calls does a given
 *                                   request actually make? The bracket wraps
 *                                   exactly the seven builtins, so counting its
 *                                   entries counts the crypto boundary crossings
 *                                   by construction.
 */
#ifndef DIT_BRACKET_H
#define DIT_BRACKET_H
#if defined(DIT_BRACKET) && DIT_BRACKET && defined(DIT_BRACKET_COUNT) && DIT_BRACKET_COUNT
#  include <stdio.h>
/* Per translation unit, so four tables and four dumps; the builtin names keep
 * them apart. __func__ is a distinct static array per function, so identity
 * comparison is enough and no strcmp is needed on the hot path. */
#  define DIT_COUNT_MAX 8
static const char *dit_cnt_name[DIT_COUNT_MAX];
static unsigned long dit_cnt_val[DIT_COUNT_MAX];
static int dit_cnt_n;
static inline void dit_count(const char *f) {
	for (int i = 0; i < dit_cnt_n; i++)
		if (dit_cnt_name[i] == f) { dit_cnt_val[i]++; return; }
	if (dit_cnt_n < DIT_COUNT_MAX) { dit_cnt_name[dit_cnt_n] = f; dit_cnt_val[dit_cnt_n++] = 1; }
}
__attribute__((destructor)) static void dit_count_dump(void) {
	for (int i = 0; i < dit_cnt_n; i++)
		fprintf(stderr, "DITCOUNT %s %lu\n", dit_cnt_name[i], dit_cnt_val[i]);
}
#  define DIT_BRACKET_ENTER() dit_count(__func__)
#  define DIT_BRACKET_LEAVE() ((void)0)
#elif defined(DIT_BRACKET) && DIT_BRACKET
#  if defined(DIT_BRACKET_NOP) && DIT_BRACKET_NOP
#    define DIT_BRACKET_ENTER() __asm__ volatile("hint #0\n\thint #0" ::: "memory")
#    define DIT_BRACKET_LEAVE() __asm__ volatile("hint #0" ::: "memory")
#  else
#    define DIT_BRACKET_ENTER() __asm__ volatile("msr DIT, #1\n\tsb" ::: "memory")
#    define DIT_BRACKET_LEAVE() __asm__ volatile("msr DIT, #0" ::: "memory")
#  endif
#else
#  define DIT_BRACKET_ENTER() ((void)0)
#  define DIT_BRACKET_LEAVE() ((void)0)
#endif
#endif
