/* host_sqlite.c - the crossover composite.
 *
 * PUBLIC lane : SQLite VDBE dispatch + B-tree/index descent. Screened at
 *               +6.09% always-on DIT, so c_P is real and large.
 * VERIFY lane : secp256k1_ecdsa_verify over public data. Holds no secret;
 *               every switch the pass places here is a false positive.
 * SECRET lane : secp256k1_ecdsa_sign over a static secret key.
 *
 * THREE ORTHOGONAL DIALS, and the public work is IDENTICAL at every setting of
 * all three (rows and rounds are fixed; only what happens at the trigger points
 * changes):
 *
 *   period   queries between trigger points        -> f   (secret fraction)
 *   sigs     signatures per trigger  + batch mode  -> R   (work per DIT region)
 *   verifies verifications per trigger             -> phi (false-positive cost)
 *
 * This is the benchmark(S, C) generator shape of SpectreGuard (DAC'19 Fig. 3),
 * reused by ProSpeCT (USENIX Sec'23 Tbl. 1) and SpecControl (Fig. 13), with two
 * axes they do not have: work-per-region, and a false-positive dial made of real
 * library code rather than synthetic annotation.
 *
 * f IS MEASURED, NEVER INFERRED FROM THE KNOB. Natively by an in-run region
 * timer; under gem5 by differencing cycles against a sigs=0 run of the same
 * binary, which is exact because gem5 is deterministic.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "sqlite3.h"
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

#define QUERIES_PER_ROUND 60

static double g_secret_s;
static double g_verify_s;
static sqlite3 *db;

static void ex(const char *sql) {
    char *err = NULL;
    if (sqlite3_exec(db, sql, NULL, NULL, &err) != SQLITE_OK) {
        fprintf(stderr, "sql error: %s\n", err);
        exit(1);
    }
}

int main(int argc, char **argv) {
    int mode     = argc > 1 ? atoi(argv[1]) : DIT_OFF;
    int rows     = argc > 2 ? atoi(argv[2]) : 4000;
    int rounds   = argc > 3 ? atoi(argv[3]) : 6;
    int sigs     = argc > 4 ? atoi(argv[4]) : 1;
    int period   = argc > 5 ? atoi(argv[5]) : 10;
    int verifies = argc > 6 ? atoi(argv[6]) : 0;

    if (period < 1) period = 1;

    secret_init(mode);
    if (sqlite3_open(":memory:", &db) != SQLITE_OK) return 1;

    ex("PRAGMA journal_mode=MEMORY; PRAGMA synchronous=OFF;");
    ex("CREATE TABLE t(a INTEGER PRIMARY KEY, b INTEGER, c TEXT);");
    ex("CREATE INDEX tb ON t(b);");
    ex("CREATE TABLE u(a INTEGER PRIMARY KEY, d INTEGER);");

    unsigned long acc = 0;
    char sql[512];

    /* ---- insert phase: setup only, NO secret work, outside the ROI ---- */
    ex("BEGIN;");
    sqlite3_stmt *ins;
    sqlite3_prepare_v2(db, "INSERT INTO t VALUES(?,?,?)", -1, &ins, NULL);
    for (int i = 0; i < rows; i++) {
        sqlite3_bind_int(ins, 1, i);
        sqlite3_bind_int(ins, 2, (i * 2654435761u) % 100000);
        snprintf(sql, sizeof sql, "row-%d-%d", i, i * 7 % 991);
        sqlite3_bind_text(ins, 3, sql, -1, SQLITE_TRANSIENT);
        sqlite3_step(ins);
        sqlite3_reset(ins);
    }
    sqlite3_finalize(ins);
    ex("COMMIT;");
    ex("INSERT INTO u SELECT a, b*3 FROM t WHERE a % 3 = 0;");

    /* ================= ROI: query phase ================= */
    ROI_BEGIN();
    double t0 = now_s();
    long qidx = 0;

    for (int r = 0; r < rounds; r++) {
        sqlite3_stmt *q;
        sqlite3_prepare_v2(db,
            "SELECT count(*), sum(t.b) FROM t JOIN u ON u.a=t.a "
            "WHERE t.b BETWEEN ? AND ? AND t.c LIKE 'row-%'", -1, &q, NULL);
        for (int i = 0; i < QUERIES_PER_ROUND; i++) {
            sqlite3_bind_int(q, 1, (i * 1013) % 90000);
            sqlite3_bind_int(q, 2, (i * 1013) % 90000 + 9000);
            while (sqlite3_step(q) == SQLITE_ROW)
                acc += (unsigned long)sqlite3_column_int64(q, 0);
            sqlite3_reset(q);

            /* The trigger point. Public work above is identical regardless of
             * what happens here, which is what makes the dials orthogonal. */
            if ((qidx % period) == (long)period - 1) {
                if (verifies > 0) {
                    double v0 = now_s();
                    acc += public_verify_n(verifies);
                    g_verify_s += now_s() - v0;
                }
                if (sigs > 0) {
                    double s0 = now_s();
                    acc += secret_sign_n(sigs);
                    g_secret_s += now_s() - s0;
                }
            }
            qidx++;
        }
        sqlite3_finalize(q);
        ex("SELECT a,b,c FROM t ORDER BY b LIMIT 500;");
    }

    double total = now_s() - t0;
    ROI_END();
    /* ================= end ROI ================= */

    printf("WORK sqlite checksum %lu\n", acc % 100000000UL);
    printf("HOST xover mode=%d rows=%d rounds=%d sigs=%d period=%d verifies=%d "
           "total_s=%.6f secret_s=%.6f verify_s=%.6f secret_frac=%.4f%% "
           "signs=%lu verifs=%lu toggles=%lu dit_now=%lu\n",
           mode, rows, rounds, sigs, period, verifies,
           total, g_secret_s, g_verify_s,
           total > 0.0 ? 100.0 * g_secret_s / total : 0.0,
           secret_count(), verify_count(), secret_toggles(), secret_dit_now());
    sqlite3_close(db);
    return 0;
}
