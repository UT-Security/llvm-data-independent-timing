#include <stdio.h>
__attribute__((constructor)) static void dit_on(void) {
  __asm__ volatile("msr DIT, #1" ::: "memory");
  unsigned long d; __asm__ volatile("mrs %0, DIT" : "=r"(d));
  fprintf(stderr, "[ctor] PSTATE.DIT = %lu\n", (d >> 24) & 1);
}
