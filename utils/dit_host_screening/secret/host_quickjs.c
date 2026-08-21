/* Composite host: embedded QuickJS (PUBLIC) + libsecp256k1 signing (SECRET).
 *
 * The project's existing QuickJS result used a SYNTHETIC secret (secret_mix /
 * js_secret_consume in quickjs.c) and a write-to-sink trick to stop the taint
 * flowing back through the return value. This replaces both: a real ECDSA
 * signature, returned normally to JS, whose declassification is the protocol's
 * (signatures are published) rather than the harness's.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "quickjs.h"
#include "secret_payload.h"

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static double g_secret_s;

static JSValue js_sign(JSContext *ctx, JSValueConst this_val,
                       int argc, JSValueConst *argv) {
    int32_t n = 1;
    if (argc > 0) JS_ToInt32(ctx, &n, argv[0]);
    double t0 = now_s();
    unsigned long r = secret_sign_n((int)n);
    g_secret_s += now_s() - t0;
    return JS_NewInt64(ctx, (int64_t)(r & 0x7fffffffUL));
}

static char *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc(n + 1);
    if (fread(b, 1, n, f) != (size_t)n) { fclose(f); free(b); return NULL; }
    b[n] = 0;
    fclose(f);
    *len = (size_t)n;
    return b;
}

int main(int argc, char **argv) {
    const char *script = argc > 1 ? argv[1] : "work.js";
    int mode = argc > 2 ? atoi(argv[2]) : DIT_OFF;
    int sigs = argc > 3 ? atoi(argv[3]) : 1;

    secret_init(mode);

    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx = JS_NewContext(rt);
    JSValue g = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, g, "sign", JS_NewCFunction(ctx, js_sign, "sign", 1));
    /* Octane wants print() */
    char initbuf[128];
    int ilen = snprintf(initbuf, sizeof initbuf,
        "globalThis.print=function(){};globalThis.SIGS=%d;", sigs);
    JS_FreeValue(ctx, JS_Eval(ctx, initbuf, ilen, "<init>", JS_EVAL_TYPE_GLOBAL));
    JS_FreeValue(ctx, g);

    size_t len;
    char *src = slurp(script, &len);
    if (!src) { fprintf(stderr, "cannot read %s\n", script); return 1; }

    double t0 = now_s();
    JSValue v = JS_Eval(ctx, src, len, script, JS_EVAL_TYPE_GLOBAL);
    double total = now_s() - t0;
    int bad = JS_IsException(v);
    if (bad) {
        JSValue e = JS_GetException(ctx);
        const char *s = JS_ToCString(ctx, e);
        fprintf(stderr, "js error: %s\n", s ? s : "?");
        JS_FreeCString(ctx, s);
        JS_FreeValue(ctx, e);
    }
    JS_FreeValue(ctx, v);

    /* correctness gate: the composite's checksum must match across all modes */
    {
        JSValue gg = JS_GetGlobalObject(ctx);
        JSValue res = JS_GetPropertyStr(ctx, gg, "__result");
        int64_t r64 = 0;
        JS_ToInt64(ctx, &r64, res);
        printf("WORK quickjs checksum %lld\n", (long long)r64);
        JS_FreeValue(ctx, res);
        JS_FreeValue(ctx, gg);
    }

    printf("HOST quickjs mode=%d total_s=%.6f secret_s=%.6f secret_frac=%.4f%% "
           "signs=%lu toggles=%lu dit_now=%lu\n",
           mode, total, g_secret_s, 100.0 * g_secret_s / total,
           secret_count(), secret_toggles(), secret_dit_now());
    free(src);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    return bad;
}
