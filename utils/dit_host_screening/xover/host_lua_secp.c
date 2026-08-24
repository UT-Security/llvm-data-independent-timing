/* host_lua_secp.c - Lua 5.4 public lane + libsecp256k1 ECDSA secret lane.
 *
 * WHY THIS FILE EXISTS. The crossover result rests on one gem5 composite whose
 * public lane is SQLite (host_sqlite.c). That is a single point for c_P, and the
 * serializing-vs-renamed switch axis - which only gem5 can measure - had never
 * been run on a second public lane. Lua's bytecode dispatch screened at +14.52%
 * always-on DIT against SQLite's +12.66% (docs/results/dit-host-screening.md):
 * both high, by DIFFERENT mechanisms (interpreter dispatch vs B-tree descent),
 * which is exactly what makes the pair worth having.
 *
 * Everything except the public lane is shared with host_sqlite.c - the same
 * secret_payload.c, the same seed, the same arms, the same runtime DIT modes -
 * so a difference between the two composites is attributable to the public lane
 * alone.
 *
 * PLAIN Lua 5.4.7, deliberately NOT LuaJIT: an MIR pass cannot instrument
 * JIT-generated code, and LuaJIT's compiled output has nothing like the
 * interpreter's dispatch profile anyway.
 *
 * THE SCRIPT IS EMBEDDED, not loaded from disk. gem5 SE mode services file I/O
 * through syscall emulation; a missing or differently-buffered script file would
 * change the instruction stream between runs, and simInsts identity across
 * machine configurations is a hard gate here. A string literal cannot vary.
 *
 *   argv: mode depth rounds sigs period verifies
 *
 * Argument ORDER matches host_sqlite.c so run_gem5.py drives both unchanged:
 * `rows` is read as the tree depth, everything else means what it does there.
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "secret_payload.h"

#ifdef USE_M5
#include <gem5/m5ops.h>
#define ROI_BEGIN() m5_reset_stats(0, 0)
#define ROI_END()   m5_dump_reset_stats(0, 0)
#else
#define ROI_BEGIN() do {} while (0)
#define ROI_END()   do {} while (0)
#endif

#ifdef GEM5_NO_SELF_TIMING
/* gem5 SE returns SIMULATED time from clock_gettime, so self-timing makes the
 * run depend on its own cycle count and it stops being a deterministic replay
 * (simInsts differed between machine configs for an identical binary - see
 * dit-gem5-composite.md sec 3). Under gem5 cycles come from stats.txt. */
static double now_s(void) { return 0.0; }
#else
static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
#endif

static double g_secret_s, g_verify_s;

/* SECRET lane, called from the guest at the script's own cadence, so the work
 * between crypto calls is genuine interpreter execution and not a C loop. */
static int l_secret(lua_State *L) {
    int n = (int)luaL_checkinteger(L, 1);
    double t0 = now_s();
    unsigned long r = secret_sign_n(n);
    g_secret_s += now_s() - t0;
    lua_pushinteger(L, (lua_Integer)(r & 0x7fffffff));
    return 1;
}

/* PUBLIC lane that shares helper code with signing: the phi (false-positive)
 * dial. It holds no secret, so every switch the pass leaves here is waste. */
static int l_verify(lua_State *L) {
    int n = (int)luaL_checkinteger(L, 1);
    double t0 = now_s();
    unsigned long r = public_verify_n(n);
    g_verify_s += now_s() - t0;
    lua_pushinteger(L, (lua_Integer)(r & 0x7fffffff));
    return 1;
}

/* binary-trees: the body that screened at +14.52%. Allocation- and
 * pointer-chase-heavy, so the cost lands in the interpreter's own dispatch loop
 * rather than in guest arithmetic. */
static const char *WORK =
    "local N      = ARG_DEPTH\n"
    "local ROUNDS = ARG_ROUNDS\n"
    "local PERIOD = ARG_PERIOD\n"
    "local NPER   = ARG_SIGS\n"
    "local NVER   = ARG_VERIFIES\n"
    "local function bottomup(d)\n"
    "  if d > 0 then return { bottomup(d - 1), bottomup(d - 1) } end\n"
    "  return {}\n"
    "end\n"
    "local function check(t)\n"
    "  if t[1] then return 1 + check(t[1]) + check(t[2]) end\n"
    "  return 1\n"
    "end\n"
    "local total, sacc, k = 0, 0, 0\n"
    "local long = bottomup(N)\n"
    "for r = 1, ROUNDS do\n"
    "  local d = 4\n"
    "  while d <= N do\n"
    "    local iters = 1 << (N - d + 4)\n"
    "    for i = 1, iters do\n"
    "      total = total + check(bottomup(d))\n"
    "      k = k + 1\n"
    "      if (k % PERIOD) == 0 then\n"
    "        if NPER > 0 then sacc = sacc + secret(NPER) end\n"
    "        if NVER > 0 then sacc = sacc + verify(NVER) end\n"
    "      end\n"
    "    end\n"
    "    d = d + 2\n"
    "  end\n"
    "end\n"
    "total = total + check(long)\n"
    "CHECKSUM = (total + sacc) % 100000000\n";

int main(int argc, char **argv) {
    int mode     = argc > 1 ? atoi(argv[1]) : DIT_OFF;
    int depth    = argc > 2 ? atoi(argv[2]) : 12;
    int rounds   = argc > 3 ? atoi(argv[3]) : 1;
    int sigs     = argc > 4 ? atoi(argv[4]) : 1;
    int period   = argc > 5 ? atoi(argv[5]) : 64;
    int verifies = argc > 6 ? atoi(argv[6]) : 0;

    if (period < 1) period = 1;

    secret_init(mode);

    lua_State *L = luaL_newstate();
    luaL_openlibs(L);
    lua_pushcfunction(L, l_secret); lua_setglobal(L, "secret");
    lua_pushcfunction(L, l_verify); lua_setglobal(L, "verify");
    lua_pushinteger(L, depth);    lua_setglobal(L, "ARG_DEPTH");
    lua_pushinteger(L, rounds);   lua_setglobal(L, "ARG_ROUNDS");
    lua_pushinteger(L, period);   lua_setglobal(L, "ARG_PERIOD");
    lua_pushinteger(L, sigs);     lua_setglobal(L, "ARG_SIGS");
    lua_pushinteger(L, verifies); lua_setglobal(L, "ARG_VERIFIES");

    /* ================= ROI ================= */
    ROI_BEGIN();
    double t0 = now_s();
    if (luaL_dostring(L, WORK) != LUA_OK) {
        fprintf(stderr, "lua error: %s\n", lua_tostring(L, -1));
        return 1;
    }
    double total = now_s() - t0;
    ROI_END();
    /* ================= end ROI ================= */

    lua_getglobal(L, "CHECKSUM");
    unsigned long acc = (unsigned long)lua_tointeger(L, -1);

    printf("WORK lua checksum %lu\n", acc);
    printf("HOST xover mode=%d depth=%d rounds=%d sigs=%d period=%d verifies=%d "
           "total_s=%.6f secret_s=%.6f verify_s=%.6f secret_frac=%.4f%% "
           "signs=%lu verifs=%lu toggles=%lu dit_now=%lu\n",
           mode, depth, rounds, sigs, period, verifies,
           total, g_secret_s, g_verify_s,
           total > 0.0 ? 100.0 * g_secret_s / total : 0.0,
           secret_count(), verify_count(), secret_toggles(), secret_dit_now());
    lua_close(L);
    return 0;
}
