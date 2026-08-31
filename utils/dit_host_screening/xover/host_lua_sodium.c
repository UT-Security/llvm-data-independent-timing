/* host_lua_sodium.c - Lua 5.4 public lane + libsodium secret lane.
 *
 * The SECOND public lane, so c_P is not a single point. Lua's bytecode dispatch
 * screened at +14.52% always-on DIT against SQLite's +12.66% on the query lane -
 * both high, by different mechanisms (interpreter dispatch vs B-tree descent),
 * which is what makes the pair worth having.
 *
 * PLAIN Lua 5.4.7, deliberately NOT LuaJIT: an MIR pass cannot instrument
 * JIT-generated code, and LuaJIT's compiled output has nothing like the
 * interpreter's dispatch profile anyway.
 *
 *   argv: script mode prim msgsize period nper depth [ops mem]
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "sodium_payload.h"

#ifdef GEM5_NO_SELF_TIMING
/* gem5 SE returns SIMULATED time from clock_gettime, so self-timing makes the
 * run depend on its own cycle count and it stops being a deterministic replay:
 * simInsts then differs between machine configs for an identical binary, because
 * a timing-derived value printed with %%.3f/%%.4f emits different digits and
 * therefore different work. Measured residual before this guard: 1.4e-6 relative
 * (dit-gem5-composite.md sec 3 is the same defect on the secp composite).
 *
 * With the guard on, f and R come from DIFFERENCING against an nper=0 run of the
 * same binary, which is exact because gem5 is deterministic:
 *     f = (cyc - cyc_nocrypto) / cyc
 *     R = (cyc - cyc_nocrypto) / ops / freq
 * so nothing is lost -- the in-run timer was only ever a cross-check. */
static double now_s(void) { return 0.0; }
#else
static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
#endif

static double g_secret_s;

/* Exposed to Lua as secret(n): the script calls it at its own cadence, so the
 * public work between calls is genuine interpreter execution, not a C loop. */
static int l_secret(lua_State *L) {
    int n = (int)luaL_checkinteger(L, 1);
    double t0 = now_s();
    unsigned long r = secret_work_n(n);
    g_secret_s += now_s() - t0;
    lua_pushinteger(L, (lua_Integer)(r & 0x7fffffff));
    return 1;
}

int main(int argc, char **argv) {
    const char *script = argc > 1 ? argv[1] : "work_sodium.lua";
    int    mode    = argc > 2 ? atoi(argv[2]) : DIT_OFF;
    int    prim    = argc > 3 ? atoi(argv[3]) : PRIM_AEAD;
    size_t msgsize = argc > 4 ? (size_t)atol(argv[4]) : 1024;
    int    period  = argc > 5 ? atoi(argv[5]) : 64;
    int    nper    = argc > 6 ? atoi(argv[6]) : 1;
    const char *depth = argc > 7 ? argv[7] : "16";
    unsigned long ops = argc > 8 ? strtoul(argv[8], NULL, 10) : 2;
    size_t mem     = argc > 9 ? (size_t)atol(argv[9]) : (64UL << 20);

    if (secret_init(mode, prim, msgsize, ops, mem) != 0) {
        fprintf(stderr, "secret_init failed\n");
        return 1;
    }

    lua_State *L = luaL_newstate();
    luaL_openlibs(L);
    lua_pushcfunction(L, l_secret);
    lua_setglobal(L, "secret");
    lua_pushstring(L, depth);            lua_setglobal(L, "ARG_DEPTH");
    lua_pushinteger(L, period);          lua_setglobal(L, "ARG_PERIOD");
    lua_pushinteger(L, nper);            lua_setglobal(L, "ARG_NPER");

    double t0 = now_s();
    if (luaL_dofile(L, script) != LUA_OK) {
        fprintf(stderr, "lua error: %s\n", lua_tostring(L, -1));
        return 1;
    }
    double total = now_s() - t0;

    printf("HOST xsod lane=lua mode=%d prim=%s msgsize=%zu period=%d nper=%d "
           "depth=%s total_s=%.6f secret_s=%.6f secret_frac=%.4f%% "
           "ops=%lu R_us=%.3f toggles=%lu dit_now=%lu\n",
           mode, secret_prim_name(), msgsize, period, nper, depth,
           total, g_secret_s, total > 0 ? 100.0 * g_secret_s / total : 0.0,
           secret_count(), secret_last_op_us(), secret_toggles(), secret_dit_now());
    lua_close(L);
    return 0;
}
