/* host_lua_typeb.c - the INSTRUCTION-LEVEL interleaving regime.
 *
 * Every other composite in this directory has the two lanes ALTERNATING at call
 * granularity: public work runs, then a crypto call runs, then public work again.
 * There is always a call boundary to put a mode switch at.
 *
 * This one has no such boundary. The secret is a STRING handed to the Lua
 * interpreter, and the public code - the VM's dispatch loop, its arithmetic, its
 * table and string handling - computes ON it. Public and secret instructions
 * interleave inside the interpreter, and `K` sets how tightly: at K = 1 the loop
 * touches the secret every iteration.
 *
 * THE ORACLE HERE IS DIFFERENT, AND THAT IS THE POINT. With no call boundary,
 * the best a human can do is wrap the whole secret-processing phase in one
 * region - which is exactly what Apple's timingsafe_enable/restore scope guards
 * and AWS-LC's caller-level hoisting do in practice. So DIT_ORACLE brackets the
 * entire script execution rather than any inner operation, and the question the
 * experiment asks is whether ANY finer placement can beat it.
 *
 *   argv: script mode K N seclen
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#define DIT_OFF    0
#define DIT_ALWAYS 1
#define DIT_ORACLE 2    /* one region around the whole secret-processing phase */

static inline void dit_on(void)  { __asm__ volatile("msr DIT, #1" ::: "memory"); }
static inline void dit_off(void) { __asm__ volatile("msr DIT, #0" ::: "memory"); }
static inline unsigned long dit_read(void) {
    unsigned long d; __asm__ volatile("mrs %0, DIT" : "=r"(d)); return (d >> 24) & 1UL;
}

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

int main(int argc, char **argv) {
    const char *script = argc > 1 ? argv[1] : "work_typeb.lua";
    int mode   = argc > 2 ? atoi(argv[2]) : DIT_OFF;
    int K      = argc > 3 ? atoi(argv[3]) : 1;
    long N     = argc > 4 ? atol(argv[4]) : 2000000;
    int seclen = argc > 5 ? atoi(argv[5]) : 4096;

    /* The secret. In a deployment this is a decrypted buffer; what matters for
     * the measurement is only that the pass believes it is secret, which the
     * `lua_pushlstring,1,pointee` seed establishes. */
    char *sec = (char *)malloc(seclen);
    for (int i = 0; i < seclen; i++) sec[i] = (char)(0x40 + (i * 7) % 60);

    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    lua_pushlstring(L, sec, (size_t)seclen);   /* <-- THE SEEDED ENTRY POINT */
    lua_setglobal(L, "SECRET");
    lua_pushinteger(L, K); lua_setglobal(L, "ARG_K");
    lua_pushinteger(L, (lua_Integer)N); lua_setglobal(L, "ARG_N");

    if (mode == DIT_ALWAYS) dit_on();

    double t0 = now_s();
    if (mode == DIT_ORACLE) dit_on();
    int rc = luaL_dofile(L, script);
    if (mode == DIT_ORACLE) dit_off();
    double total = now_s() - t0;

    if (rc != LUA_OK) { fprintf(stderr, "lua error: %s\n", lua_tostring(L, -1)); return 1; }

    printf("HOST typeb mode=%d K=%d N=%ld seclen=%d total_s=%.6f dit_now=%lu\n",
           mode, K, N, seclen, total, dit_read());
    lua_close(L);
    return 0;
}
