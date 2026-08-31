/* host_sqlite_tce.c - SQLite public lane + per-column AEAD on the write path.
 *
 * The pgsodium Transparent Column Encryption shape, in a form that runs under
 * gem5 SE mode: an in-memory database, no sockets, no threads, no file locks.
 *
 * THE KNOB IS enc_cols AND IT MOVES ONLY f. The table has four BLOB columns of
 * identical size. Every one of them is stored on every row; enc_cols of them
 * are produced by an AEAD call and the rest by a same-sized fill. So the row is
 * byte-identical in SIZE at every point on the knob, the B-tree descends and
 * splits identically, the two indexes are maintained identically, and the only
 * thing that moves is how much crypto runs. A knob that also changed the public
 * work would confound the sweep and the curve would mean nothing.
 *
 * WHY THE READ PHASE TOUCHES NO CIPHERTEXT. Reading back through a decrypting
 * view is the shape that floods the application with taint (see tce_payload.c).
 * The read here is an index descent over the PUBLIC columns, which is what a
 * real application does far more often than it decrypts: look up by email,
 * filter by date, count.
 *
 *   argv: mode field_bytes enc_cols rows batch reads time_secret
 *
 * time_secret MUST be 0 under gem5: clock_gettime returns simulated time in SE
 * mode, so per-op timing calls make simInsts differ across arms and the gate
 * that catches a self-perturbing driver would fire.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "sqlite3.h"
#include "tce_payload.h"

/* Eight encryptable columns, so enc_cols reaches f ~ 10% - far enough to
 * bracket the 3.2%-wins / 16.7%-loses band measured on the libsodium composite.
 * Every column is stored at every setting; only how many are produced by an
 * AEAD call moves. */
#ifndef NCOLS
#define NCOLS 8
#endif

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static sqlite3 *db;

static void ex(const char *sql) {
    char *err = NULL;
    if (sqlite3_exec(db, sql, NULL, NULL, &err) != SQLITE_OK) {
        fprintf(stderr, "sql error: %s\n", err);
        exit(1);
    }
}

int main(int argc, char **argv) {
    int    mode     = argc > 1 ? atoi(argv[1]) : DIT_OFF;
    size_t fbytes   = argc > 2 ? (size_t)atol(argv[2]) : 128;
    int    enc_cols = argc > 3 ? atoi(argv[3]) : 1;
    int    rows     = argc > 4 ? atoi(argv[4]) : 20000;
    int    batch    = argc > 5 ? atoi(argv[5]) : 500;
    int    reads    = argc > 6 ? atoi(argv[6]) : 2000;
    int    time_secret = argc > 7 ? atoi(argv[7]) : 1;

    if (enc_cols < 0) enc_cols = 0;
    if (enc_cols > NCOLS) enc_cols = NCOLS;
    if (batch < 1) batch = 1;

    if (tce_init(mode, fbytes, time_secret) != 0) { fprintf(stderr, "tce_init failed\n"); return 1; }
    if (sqlite3_open(":memory:", &db) != SQLITE_OK) return 1;

    ex("PRAGMA journal_mode=MEMORY; PRAGMA synchronous=OFF;");
    {   /* CREATE and INSERT are generated so NCOLS is the single source of truth */
        char ddl[1024], *p = ddl;
        p += snprintf(p, sizeof ddl,
                      "CREATE TABLE accounts("
                      "  id INTEGER PRIMARY KEY, email TEXT, created INTEGER");
        for (int j = 0; j < NCOLS; j++)
            p += snprintf(p, sizeof ddl - (size_t)(p - ddl), ", c%d BLOB", j);
        snprintf(p, sizeof ddl - (size_t)(p - ddl), ");");
        ex(ddl);
    }
    ex("CREATE INDEX acc_email ON accounts(email);");
    ex("CREATE INDEX acc_created ON accounts(created);");

    size_t clen_max = fbytes + 16;
    unsigned char *field[NCOLS];
    for (int j = 0; j < NCOLS; j++) {
        field[j] = (unsigned char *)malloc(clen_max + 64);
        if (!field[j]) return 1;
    }

    sqlite3_stmt *ins, *sel;
    {
        char dml[512], *p = dml;
        p += snprintf(p, sizeof dml, "INSERT INTO accounts VALUES(?,?,?");
        for (int j = 0; j < NCOLS; j++)
            p += snprintf(p, sizeof dml - (size_t)(p - dml), ",?");
        snprintf(p, sizeof dml - (size_t)(p - dml), ")");
        sqlite3_prepare_v2(db, dml, -1, &ins, NULL);
    }
    sqlite3_prepare_v2(db,
        "SELECT count(*), sum(created) FROM accounts "
        "WHERE email > ? AND created BETWEEN ? AND ?", -1, &sel, NULL);

    unsigned long acc = 0;
    double secret_s = 0.0;
    char email[64];

    /* ================= measured region ================= */
    double t0 = time_secret ? now_s() : 0.0;

    for (int i = 0; i < rows; i++) {
        if (i % batch == 0) ex("BEGIN;");

        /* public: build the row's public columns */
        snprintf(email, sizeof email, "user%08d@example.com", (i * 2654435761u) % 100000000u);
        long created = 1700000000L + (i * 7919) % 31536000L;

        /* secret: enc_cols of the four columns go through the AEAD */
        double s0 = time_secret ? now_s() : 0.0;
        tce_row_begin();
        for (int j = 0; j < NCOLS; j++) {
            if (j < enc_cols) tce_encrypt_field(field[j], (unsigned long)i, j);
            else              tce_plain_field(field[j],   (unsigned long)i, j);
        }
        tce_row_end();
        if (time_secret) secret_s += now_s() - s0;

        /* public: bind, descend the B-tree, maintain two indexes */
        sqlite3_bind_int64(ins, 1, i);
        sqlite3_bind_text(ins, 2, email, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(ins, 3, created);
        for (int j = 0; j < NCOLS; j++)
            sqlite3_bind_blob(ins, 4 + j, field[j], (int)clen_max, SQLITE_TRANSIENT);
        if (sqlite3_step(ins) != SQLITE_DONE) { fprintf(stderr, "insert failed\n"); return 1; }
        sqlite3_reset(ins);
        /* Fold every column into the checksum, not just the first: a checksum
         * that only witnesses column 0 cannot tell an arm that skipped the
         * crypto on columns 1-3 from one that did it. */
        for (int j = 0; j < NCOLS; j++)
            acc += (unsigned long)field[j][0] + (unsigned long)field[j][clen_max - 1];
        acc += (unsigned long)created % 97;

        if (i % batch == batch - 1 || i == rows - 1) ex("COMMIT;");
    }

    /* public: index descent over public columns only, no ciphertext touched */
    for (int i = 0; i < reads; i++) {
        snprintf(email, sizeof email, "user%08d@example.com", (i * 1013904223u) % 100000000u);
        sqlite3_bind_text(sel, 1, email, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(sel, 2, 1700000000L + (i * 4099) % 31536000L);
        sqlite3_bind_int64(sel, 3, 1700000000L + (i * 4099) % 31536000L + 2592000L);
        while (sqlite3_step(sel) == SQLITE_ROW)
            acc += (unsigned long)sqlite3_column_int64(sel, 0);
        sqlite3_reset(sel);
    }

    double total = time_secret ? now_s() - t0 : 0.0;
    /* ================= end region ================= */

    printf("WORK tce checksum %lu\n", acc % 100000000UL);
    printf("HOST xtce lane=sqlite mode=%d field_bytes=%zu enc_cols=%d/%d rows=%d "
           "batch=%d reads=%d total_s=%.6f secret_s=%.6f secret_frac=%.4f%% "
           "ops=%lu R_us=%.3f toggles=%lu dit_now=%lu\n",
           mode, tce_field_bytes(), enc_cols, NCOLS, rows, batch, reads,
           total, secret_s, total > 0 ? 100.0 * secret_s / total : 0.0,
           tce_count(), tce_last_op_us(), tce_toggles(), tce_dit_now());

    sqlite3_finalize(ins);
    sqlite3_finalize(sel);
    sqlite3_close(db);
    return 0;
}
