/* Composite host: SQLite (PUBLIC) + libsecp256k1 signing (SECRET).
 *
 * Deployment story: a database that signs an audit record / session token every
 * N queries. The query work is the VDBE bytecode interpreter plus B-tree
 * descent, which screened at +6.09% always-on DIT; the signing is the secret.
 *
 * Deliberately UNLIKE SQLCipher: the crypto here is called once per batch of
 * queries, not once per 4 KB page, so the region is ~1.4 us rather than the
 * 300-500 cyc that put SQLCipher 3-4x below the granularity crossover.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "sqlite3.h"
#include "secret_payload.h"

#ifdef GEM5_NO_SELF_TIMING
/* gem5 SE returns SIMULATED time from clock_gettime, so self-timing makes the
 * run depend on its own cycle count and it is no longer a deterministic replay
 * (simInsts differed between machine configs for an identical binary). Under
 * gem5 we take the cycle counts from stats.txt instead and time nothing. */
static double now_s(void) { return 0.0; }
static double now_s_unused(void) {
#else
static double now_s(void) {
#endif
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
#ifdef GEM5_NO_SELF_TIMING
static double (*const now_s_keep)(void) = now_s_unused;
#endif

static double g_secret_s;
static sqlite3 *db;

static void ex(const char *sql) {
    char *err = NULL;
    if (sqlite3_exec(db, sql, NULL, NULL, &err) != SQLITE_OK) {
        fprintf(stderr, "sql error: %s\n", err);
        exit(1);
    }
}

int main(int argc, char **argv) {
    int mode = argc > 1 ? atoi(argv[1]) : DIT_OFF;
    int rows = argc > 2 ? atoi(argv[2]) : 4000;
    int rounds = argc > 3 ? atoi(argv[3]) : 6;
    int sigs = argc > 4 ? atoi(argv[4]) : 1;

    secret_init(mode);
    if (sqlite3_open(":memory:", &db) != SQLITE_OK) return 1;

    double t0 = now_s();
    ex("PRAGMA journal_mode=MEMORY; PRAGMA synchronous=OFF;");
    ex("CREATE TABLE t(a INTEGER PRIMARY KEY, b INTEGER, c TEXT);");
    ex("CREATE INDEX tb ON t(b);");
    ex("CREATE TABLE u(a INTEGER PRIMARY KEY, d INTEGER);");

    unsigned long acc = 0;
    char sql[512];

    /* ---- insert phase, signing every 2000 rows ---- */
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
        if (sigs > 0 && (i % 2000) == 1999) {
            double s0 = now_s();
            acc += secret_sign_n(sigs);
            g_secret_s += now_s() - s0;
        }
    }
    sqlite3_finalize(ins);
    ex("COMMIT;");
    ex("INSERT INTO u SELECT a, b*3 FROM t WHERE a % 3 = 0;");

    /* ---- query phase: index descent, joins, aggregation, ordering ---- */
    for (int r = 0; r < rounds; r++) {
        sqlite3_stmt *q;
        sqlite3_prepare_v2(db,
            "SELECT count(*), sum(t.b) FROM t JOIN u ON u.a=t.a "
            "WHERE t.b BETWEEN ? AND ? AND t.c LIKE 'row-%'", -1, &q, NULL);
        for (int i = 0; i < 60; i++) {
            sqlite3_bind_int(q, 1, (i * 1013) % 90000);
            sqlite3_bind_int(q, 2, (i * 1013) % 90000 + 9000);
            while (sqlite3_step(q) == SQLITE_ROW)
                acc += (unsigned long)sqlite3_column_int64(q, 0);
            sqlite3_reset(q);
            if (sigs > 0 && (i % 10) == 9) {
                double s0 = now_s();
                acc += secret_sign_n(sigs);
                g_secret_s += now_s() - s0;
            }
        }
        sqlite3_finalize(q);
        ex("SELECT a,b,c FROM t ORDER BY b LIMIT 500;");
    }

    double total = now_s() - t0;
    printf("WORK sqlite checksum %lu\n", acc % 100000000UL);
    printf("HOST sqlite mode=%d total_s=%.6f secret_s=%.6f secret_frac=%.4f%% "
           "signs=%lu toggles=%lu dit_now=%lu\n",
           mode, total, g_secret_s, 100.0 * g_secret_s / total,
           secret_count(), secret_toggles(), secret_dit_now());
    sqlite3_close(db);
    return 0;
}
