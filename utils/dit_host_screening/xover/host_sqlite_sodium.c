/* host_sqlite_sodium.c - SQLite public lane + libsodium secret lane.
 *
 * The public lane is byte-identical to the one used for the libsecp256k1
 * composite and is characterised at c_P = 12.66% always-on DIT on M5, so any
 * difference in the verdict between the two experiments is attributable to the
 * CRYPTO, not to the host.
 *
 * Deployment story: a database serving rows, some of which hold an encrypted
 * column. That is a real pattern (PHP's sodium extension, Paragon CipherSweet),
 * and it makes both dials native application parameters rather than knobs -
 * `period` is how many rows are encrypted, `msgsize` is how big the field is.
 *
 *   argv: mode prim msgsize period nper rows rounds [ops mem]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "sqlite3.h"
#include "sodium_payload.h"

#define QUERIES_PER_ROUND 60

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
static sqlite3 *db;

static void ex(const char *sql) {
    char *err = NULL;
    if (sqlite3_exec(db, sql, NULL, NULL, &err) != SQLITE_OK) {
        fprintf(stderr, "sql error: %s\n", err);
        exit(1);
    }
}

int main(int argc, char **argv) {
    int    mode    = argc > 1 ? atoi(argv[1]) : DIT_OFF;
    int    prim    = argc > 2 ? atoi(argv[2]) : PRIM_AEAD;
    size_t msgsize = argc > 3 ? (size_t)atol(argv[3]) : 1024;
    int    period  = argc > 4 ? atoi(argv[4]) : 10;
    int    nper    = argc > 5 ? atoi(argv[5]) : 1;
    int    rows    = argc > 6 ? atoi(argv[6]) : 4000;
    int    rounds  = argc > 7 ? atoi(argv[7]) : 100;
    unsigned long ops = argc > 8 ? strtoul(argv[8], NULL, 10) : 2;
    size_t mem     = argc > 9 ? (size_t)atol(argv[9]) : (64UL << 20);

    if (period < 1) period = 1;
    if (secret_init(mode, prim, msgsize, ops, mem) != 0) {
        fprintf(stderr, "secret_init failed\n");
        return 1;
    }
    if (sqlite3_open(":memory:", &db) != SQLITE_OK) return 1;

    ex("PRAGMA journal_mode=MEMORY; PRAGMA synchronous=OFF;");
    ex("CREATE TABLE t(a INTEGER PRIMARY KEY, b INTEGER, c TEXT);");
    ex("CREATE INDEX tb ON t(b);");
    ex("CREATE TABLE u(a INTEGER PRIMARY KEY, d INTEGER);");

    unsigned long acc = 0;
    char sql[512];

    /* setup: no secret work, outside the measured region */
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

    /* ================= measured region ================= */
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

            if (nper > 0 && (qidx % period) == (long)period - 1) {
                double s0 = now_s();
                acc += secret_work_n(nper);
                g_secret_s += now_s() - s0;
            }
            qidx++;
        }
        sqlite3_finalize(q);
        ex("SELECT a,b,c FROM t ORDER BY b LIMIT 500;");
    }
    double total = now_s() - t0;
    /* ================= end region ================= */

    printf("WORK sqlite checksum %lu\n", acc % 100000000UL);
    printf("HOST xsod lane=sqlite mode=%d prim=%s msgsize=%zu period=%d nper=%d "
           "rows=%d rounds=%d total_s=%.6f secret_s=%.6f secret_frac=%.4f%% "
           "ops=%lu R_us=%.3f toggles=%lu dit_now=%lu\n",
           mode, secret_prim_name(), msgsize, period, nper, rows, rounds,
           total, g_secret_s, total > 0 ? 100.0 * g_secret_s / total : 0.0,
           secret_count(), secret_last_op_us(), secret_toggles(), secret_dit_now());
    sqlite3_close(db);
    return 0;
}
