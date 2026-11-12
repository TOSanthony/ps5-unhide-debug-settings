/*
 * unhide_debug.elf - PS5 payload (ps5-payload-sdk)
 *
 *  1. Backs up /system_data/priv/mms/app.db to /data/app.db.<timestamp>.bak
 *     (raw byte copy, plus -wal / -journal if present).
 *  2. In every table named tbl_iconinfo*, sets visible=1 for titleId NPXS40063
 *     (only where it is currently 0).
 *
 * Nothing is changed if the backup fails. Reboot the console afterwards.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <sqlite3.h>

#define DB_PATH   "/system_data/priv/mms/app.db"
#define BACKUP_DIR "/data"
#define TITLE_ID  "NPXS40063"

/* Optional on-screen notification: only used if the symbol is resolved. */
typedef struct {
    char unused[45];
    char message[3075];
} notify_request_t;
extern int sceKernelSendNotificationRequest(int, notify_request_t *, size_t, int)
    __attribute__((weak));

static void say(const char *fmt, ...) {
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    printf("[unhide] %s\n", msg);
    fflush(stdout);
    if (sceKernelSendNotificationRequest) {
        notify_request_t req;
        memset(&req, 0, sizeof req);
        snprintf(req.message, sizeof req.message, "%s", msg);
        sceKernelSendNotificationRequest(0, &req, sizeof req, 0);
    }
}

static int copy_file(const char *src, const char *dst) {
    int in = open(src, O_RDONLY);
    if (in < 0) return -1;
    int out = open(dst, O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (out < 0) { close(in); return -1; }

    char buf[65536];
    ssize_t n;
    int rc = 0;
    while ((n = read(in, buf, sizeof buf)) > 0) {
        char *p = buf;
        while (n > 0) {
            ssize_t w = write(out, p, (size_t)n);
            if (w < 0) { rc = -1; goto done; }
            p += w; n -= w;
        }
    }
    if (n < 0) rc = -1;
    if (rc == 0 && fsync(out) != 0) rc = -1;
done:
    close(in);
    if (close(out) != 0) rc = -1;
    if (rc != 0) unlink(dst);
    return rc;
}

static int same_size(const char *a, const char *b) {
    struct stat sa, sb;
    return stat(a, &sa) == 0 && stat(b, &sb) == 0 && sa.st_size == sb.st_size;
}

static int backup_db(void) {
    char dst[256], src[256], dsx[256];
    time_t t = time(NULL);

    snprintf(dst, sizeof dst, BACKUP_DIR "/app.db.%ld.bak", (long)t);
    if (copy_file(DB_PATH, dst) != 0 || !same_size(DB_PATH, dst)) {
        say("Backup failed (%s): %s", dst, strerror(errno));
        return -1;
    }
    static const char *const sfx[] = {"-wal", "-journal"};
    for (int i = 0; i < 2; i++) {
        snprintf(src, sizeof src, DB_PATH "%s", sfx[i]);
        if (access(src, F_OK) != 0) continue;
        snprintf(dsx, sizeof dsx, "%s%s", dst, sfx[i]);
        if (copy_file(src, dsx) != 0) {
            say("Backup of %s failed: %s", src, strerror(errno));
            return -1;
        }
    }
    say("Backup OK: %s", dst);
    return 0;
}

/* Returns 1 if the table has both columns, 0 if not, -1 on error. */
static int has_columns(sqlite3 *db, const char *table) {
    char *sql = sqlite3_mprintf("PRAGMA table_info(\"%w\")", table);
    sqlite3_stmt *st = NULL;
    int has_id = 0, has_vis = 0;
    if (!sql) return -1;
    int rc = sqlite3_prepare_v2(db, sql, -1, &st, NULL);
    sqlite3_free(sql);
    if (rc != SQLITE_OK) return -1;
    while (sqlite3_step(st) == SQLITE_ROW) {
        const char *name = (const char *)sqlite3_column_text(st, 1);
        if (!name) continue;
        if (!strcasecmp(name, "titleId")) has_id = 1;
        if (!strcasecmp(name, "visible")) has_vis = 1;
    }
    sqlite3_finalize(st);
    return has_id && has_vis;
}

static int patch_table(sqlite3 *db, const char *table) {
    char *err = NULL;
    char *sql = sqlite3_mprintf(
        "UPDATE \"%w\" SET visible=1 WHERE titleId='" TITLE_ID "' AND visible=0",
        table);
    if (!sql) return -1;

    if (sqlite3_exec(db, "BEGIN IMMEDIATE", NULL, NULL, &err) != SQLITE_OK) goto fail;
    if (sqlite3_exec(db, sql, NULL, NULL, &err) != SQLITE_OK) {
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
        goto fail;
    }
    int changed = sqlite3_changes(db);
    if (sqlite3_exec(db, "COMMIT", NULL, NULL, &err) != SQLITE_OK) goto fail;
    sqlite3_free(sql);
    return changed;
fail:
    say("%s: %s", table, err ? err : "sqlite error");
    sqlite3_free(err);
    sqlite3_free(sql);
    return -1;
}

int main(void) {
    if (backup_db() != 0) return 1;

    sqlite3 *db = NULL;
    if (sqlite3_open_v2(DB_PATH, &db, SQLITE_OPEN_READWRITE, NULL) != SQLITE_OK) {
        say("Cannot open %s: %s", DB_PATH, db ? sqlite3_errmsg(db) : "?");
        if (db) sqlite3_close(db);
        return 1;
    }
    sqlite3_busy_timeout(db, 5000);

    /* Collect table names first, so we are not modifying while stepping. */
    char *names[64];
    int n = 0;
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db,
            "SELECT name FROM sqlite_master "
            "WHERE type='table' AND name LIKE 'tbl_iconinfo%'",
            -1, &st, NULL) != SQLITE_OK) {
        say("Query failed: %s", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }
    while (n < 64 && sqlite3_step(st) == SQLITE_ROW) {
        const char *s = (const char *)sqlite3_column_text(st, 0);
        if (s) names[n++] = strdup(s);
    }
    sqlite3_finalize(st);

    if (n == 0) {
        say("No tbl_iconinfo* table found. Nothing changed.");
        sqlite3_close(db);
        return 1;
    }

    int total = 0, patched_tables = 0;
    for (int i = 0; i < n; i++) {
        if (has_columns(db, names[i]) == 1) {
            int c = patch_table(db, names[i]);
            if (c > 0) { total += c; patched_tables++; }
        }
        free(names[i]);
    }
    sqlite3_close(db);

    if (total > 0)
        say("%s now visible (%d row(s) in %d table(s)). Reboot the console.",
            TITLE_ID, total, patched_tables);
    else
        say("%s: nothing to change (already visible or not found).", TITLE_ID);
    return 0;
}
