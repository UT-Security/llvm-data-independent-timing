/* Minimal DIT control callable from Python via ctypes, so the ORACLE arm can
 * wrap a REAL application's crypto call without patching the application. */
void dit_on(void)  { __asm__ volatile("msr DIT, #1" ::: "memory"); }
void dit_off(void) { __asm__ volatile("msr DIT, #0" ::: "memory"); }
unsigned long dit_get(void) {
    unsigned long d; __asm__ volatile("mrs %0, DIT" : "=r"(d)); return (d >> 24) & 1UL;
}
