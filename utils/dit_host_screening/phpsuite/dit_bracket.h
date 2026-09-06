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
 */
#ifndef DIT_BRACKET_H
#define DIT_BRACKET_H
#if defined(DIT_BRACKET) && DIT_BRACKET
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
