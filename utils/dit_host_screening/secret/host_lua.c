/* Composite host: embedded Lua (PUBLIC) + libsecp256k1 signing (SECRET).
 *
 * One binary, DIT mode chosen at runtime, so no codegen differs between arms.
 * The Lua script drives: it does a chunk of public work, then calls sign(k),
 * repeatedly - the interleaving a real deployment has, not all the secret work
 * batched at the end where it could not perturb anything.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "secret_payload.h"

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static double g_secret_s;

static int l_sign(lua_State *L) {
    int n = (int)luaL_checkinteger(L, 1);
    double t0 = now_s();
    unsigned long r = secret_sign_n(n);
    g_secret_s += now_s() - t0;
    lua_pushinteger(L, (lua_Integer)(r & 0x7fffffff));
    return 1;
}

int main(int argc, char **argv) {
    const char *script = argc > 1 ? argv[1] : "work.lua";
    int mode = argc > 2 ? atoi(argv[2]) : DIT_OFF;
    const char *a1 = argc > 3 ? argv[3] : "17";
    const char *a2 = argc > 4 ? argv[4] : "1";

    secret_init(mode);

    lua_State *L = luaL_newstate();
    luaL_openlibs(L);
    lua_pushcfunction(L, l_sign);
    lua_setglobal(L, "sign");

    /* pass script args as globals; simpler than building an `arg` table */
    lua_pushstring(L, a1); lua_setglobal(L, "ARG1");
    lua_pushstring(L, a2); lua_setglobal(L, "ARG2");

    double t0 = now_s();
    if (luaL_dofile(L, script) != LUA_OK) {
        fprintf(stderr, "lua error: %s\n", lua_tostring(L, -1));
        return 1;
    }
    double total = now_s() - t0;

    printf("HOST lua mode=%d total_s=%.6f secret_s=%.6f secret_frac=%.4f%% "
           "signs=%lu toggles=%lu dit_now=%lu\n",
           mode, total, g_secret_s, 100.0 * g_secret_s / total,
           secret_count(), secret_toggles(), secret_dit_now());
    lua_close(L);
    return 0;
}
