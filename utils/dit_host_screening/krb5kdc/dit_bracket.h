/* The developer's bracket (experiment 13, arm B), save/restore form.
 *
 * Apple's recipe at the entry of a routine that touches a secret: remember the
 * mode, set PSTATE.DIT, drain speculation with sb; restore the mode on the way
 * out. Save/restore rather than a plain clear because these API functions nest
 * (krb5_c_decrypt calls krb5_k_decrypt), and a plain clear in the inner one would
 * strip the outer one's protection.
 *
 *   -DDIT_BRACKET=1                      emit the bracket
 *   -DDIT_BRACKET=1 -DDIT_BRACKET_NOP=1  hint #0 in each place: the layout control
 *                                        (arm Bn), same instruction count, no switch
 *   ... -DDIT_BRACKET_COUNT=1             diagnostic: count executed entries, print at exit
 */
#ifndef DIT_BRACKET_H
#define DIT_BRACKET_H
#if defined(DIT_BRACKET) && DIT_BRACKET
#  if defined(DIT_BRACKET_COUNT) && DIT_BRACKET_COUNT
     /* diagnostic: count executed bracket entries, printed once at exit */
#    include <stdio.h>
#    include <string.h>
     __attribute__((weak)) unsigned long __dit_bracket_entries;
     __attribute__((weak)) int __dit_bracket_reported;
     __attribute__((weak)) const char *__dit_bracket_names[64];
     __attribute__((weak)) unsigned long __dit_bracket_counts[64];
     static void __dit_bracket_hit(const char *f) {
         int i; __dit_bracket_entries++;
         for (i = 0; i < 64; i++) {
             if (__dit_bracket_names[i] == f || (__dit_bracket_names[i] && !strcmp(__dit_bracket_names[i], f))) { __dit_bracket_counts[i]++; return; }
             if (!__dit_bracket_names[i]) { __dit_bracket_names[i] = f; __dit_bracket_counts[i] = 1; return; } } }
     __attribute__((destructor)) static void __dit_bracket_report(void) {
         int i; if (__dit_bracket_reported++) return;
         fprintf(stderr, "DITBRACKET entries=%lu\n", __dit_bracket_entries);
         for (i = 0; i < 64 && __dit_bracket_names[i]; i++) fprintf(stderr, "DITBRACKET %s %lu\n", __dit_bracket_names[i], __dit_bracket_counts[i]); }
#    define DIT_BRACKET_COUNT_HOOK() __dit_bracket_hit(__func__)
#  else
#    define DIT_BRACKET_COUNT_HOOK() ((void)0)
#  endif
#  if defined(DIT_BRACKET_NOP) && DIT_BRACKET_NOP
#    define DIT_BRACKET_ENTER() unsigned long __dit_saved; DIT_BRACKET_COUNT_HOOK(); \
        __asm__ volatile("hint #0\n\thint #0\n\thint #0" : "=r"(__dit_saved) : : "memory")
#    define DIT_BRACKET_LEAVE() __asm__ volatile("hint #0" : : "r"(__dit_saved) : "memory")
#  else
#    define DIT_BRACKET_ENTER() unsigned long __dit_saved; DIT_BRACKET_COUNT_HOOK(); \
        __asm__ volatile("mrs %0, DIT\n\tmsr DIT, #1\n\tsb" : "=r"(__dit_saved) : : "memory")
#    define DIT_BRACKET_LEAVE() __asm__ volatile("msr DIT, %0" : : "r"(__dit_saved) : "memory")
#  endif
#else
#  define DIT_BRACKET_ENTER() ((void)0)
#  define DIT_BRACKET_LEAVE() ((void)0)
#endif
#endif
