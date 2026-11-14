/*
 * appdb.h - helpers shared by the payloads: backup + edit of
 * /system_data/priv/mms/app.db (tables tbl_iconinfo*).
 */
#ifndef APPDB_H
#define APPDB_H

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

#ifndef APPDB_PATH
#define APPDB_PATH "/system_data/priv/mms/app.db"
#endif
#ifndef APPDB_BACKUP_DIR
#define APPDB_BACKUP_DIR "/data"
#endif

#define APPDB_FN static __attribute__((unused))

/* Optional on-screen notification: only used if the symbol is resolved. */
typedef struct {
    char unused[45];
    char message[3075];
} appdb_notify_t;
extern int sceKernelSendNotificationRequest(int, appdb_notify_t *, size_t, int)
    __attribute__((weak));

APPDB_FN void appdb_say(const char *fmt, ...) {
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    printf("[appdb] %s\n", msg);
    fflush(stdout);
    if (sceKernelSendNotificationRequest) {
        appdb_notify_t req;
        memset(&req, 0, sizeof req);
        snprintf(req.message, sizeof req.message, "%s", msg);
        sceKernelSendNotificationRequest(0, &req, sizeof req, 0);
    }
}

APPDB_FN int appdb_copy_file(const char *src, const char *dst) {
    int in = open(src, O_RDONLY);
    if (in < 0) return -1;
    int out = open(dst, O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (out < 0) { int e = errno; close(in); errno = e; return -1; }

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

APPDB_FN int appdb_same_size(const char *a, const char *b) {
    struct stat sa, sb;
    return stat(a, &sa) == 0 && stat(b, &sb) == 0 && sa.st_size == sb.st_size;
}

/* Raw copy of app.db (+ -wal / -journal) to /data/app.db.<time>.bak.
 * Never overwrites an existing backup. Returns 0 on success. */
APPDB_FN int appdb_backup(void) {
    char dst[256], src[256], dsx[256];
    long t = (long)time(NULL);
    int ok = 0;

    for (int i = 0; i < 100 && !ok; i++) {
        if (i == 0)
            snprintf(dst, sizeof dst, APPDB_BACKUP_DIR "/app.db.%ld.bak", t);
        else
            snprintf(dst, sizeof dst, APPDB_BACKUP_DIR "/app.db.%ld-%d.bak", t, i);
        if (appdb_copy_file(APPDB_PATH, dst) == 0) { ok = 1; break; }
        if (errno != EEXIST) break;
    }
    if (!ok || !appdb_same_size(APPDB_PATH, dst)) {
        appdb_say("Backup failed: %s", strerror(errno));
        return -1;
    }
    static const char *const sfx[] = {"-wal", "-journal"};
    for (int i = 0; i < 2; i++) {
        snprintf(src, sizeof src, APPDB_PATH "%s", sfx[i]);
        if (access(src, F_OK) != 0) continue;
        snprintf(dsx, sizeof dsx, "%s%s", dst, sfx[i]);
        if (appdb_copy_file(src, dsx) != 0) {
            appdb_say("Backup of %s failed: %s", src, strerror(errno));
            return -1;
        }
    }
    appdb_say("Backup OK: %s", dst);
    return 0;
}

APPDB_FN sqlite3 *appdb_open(void) {
    sqlite3 *db = NULL;
    if (sqlite3_open_v2(APPDB_PATH, &db, SQLITE_OPEN_READWRITE, NULL) != SQLITE_OK) {
        appdb_say("Cannot open %s: %s", APPDB_PATH, db ? sqlite3_errmsg(db) : "?");
        if (db) sqlite3_close(db);
        return NULL;
    }
    sqlite3_busy_timeout(db, 5000);
    return db;
}

/* Returns 1 if the table has both titleId and visible columns. */
APPDB_FN int appdb_has_columns(sqlite3 *db, const char *table) {
    char *sql = sqlite3_mprintf("PRAGMA table_info(\"%w\")", table);
    sqlite3_stmt *st = NULL;
    int has_id = 0, has_vis = 0;
    if (!sql) return 0;
    int rc = sqlite3_prepare_v2(db, sql, -1, &st, NULL);
    sqlite3_free(sql);
    if (rc != SQLITE_OK) return 0;
    while (sqlite3_step(st) == SQLITE_ROW) {
        const char *name = (const char *)sqlite3_column_text(st, 1);
        if (!name) continue;
        if (!strcasecmp(name, "titleId")) has_id = 1;
        if (!strcasecmp(name, "visible")) has_vis = 1;
    }
    sqlite3_finalize(st);
    return has_id && has_vis;
}

/* Names of the usable tbl_iconinfo* tables (caller frees each). */
APPDB_FN int appdb_tables(sqlite3 *db, char **names, int max) {
    sqlite3_stmt *st = NULL;
    int n = 0;
    if (sqlite3_prepare_v2(db,
            "SELECT name FROM sqlite_master "
            "WHERE type='table' AND name LIKE 'tbl_iconinfo%'",
            -1, &st, NULL) != SQLITE_OK) {
        appdb_say("Query failed: %s", sqlite3_errmsg(db));
        return -1;
    }
    while (n < max && sqlite3_step(st) == SQLITE_ROW) {
        const char *s = (const char *)sqlite3_column_text(st, 0);
        if (s) names[n++] = strdup(s);
    }
    sqlite3_finalize(st);
    return n;
}

/* Counts rows for title_id over all usable tables: how many rows exist
 * and how many of them are currently visible. Returns 0, or -1 on error. */
APPDB_FN int appdb_count(sqlite3 *db, const char *title_id, int *total, int *visible) {
    char *names[64];
    int n = appdb_tables(db, names, 64);
    if (n < 0) return -1;
    *total = 0;
    *visible = 0;
    for (int i = 0; i < n; i++) {
        if (appdb_has_columns(db, names[i])) {
            char *sql = sqlite3_mprintf(
                "SELECT COUNT(*), COALESCE(SUM(IFNULL(visible,0)<>0),0) "
                "FROM \"%w\" WHERE titleId=%Q", names[i], title_id);
            sqlite3_stmt *st = NULL;
            if (sql && sqlite3_prepare_v2(db, sql, -1, &st, NULL) == SQLITE_OK) {
                if (sqlite3_step(st) == SQLITE_ROW) {
                    *total += sqlite3_column_int(st, 0);
                    *visible += sqlite3_column_int(st, 1);
                }
                sqlite3_finalize(st);
            }
            sqlite3_free(sql);
        }
        free(names[i]);
    }
    return 0;
}

/* Sets visible=value for title_id in every usable table.
 * Returns the number of rows changed, or -1 on error. */
APPDB_FN int appdb_set_visible(sqlite3 *db, const char *title_id, int value) {
    char *names[64];
    int n = appdb_tables(db, names, 64);
    int changed = 0, err = 0;
    if (n < 0) return -1;
    for (int i = 0; i < n; i++) {
        if (appdb_has_columns(db, names[i])) {
            char *e = NULL;
            char *sql = sqlite3_mprintf(
                "UPDATE \"%w\" SET visible=%d WHERE titleId=%Q "
                "AND IFNULL(visible,-1)<>%d", names[i], value, title_id, value);
            if (!sql) { err = 1; }
            else if (sqlite3_exec(db, "BEGIN IMMEDIATE", NULL, NULL, &e) != SQLITE_OK) {
                appdb_say("%s: %s", names[i], e ? e : "begin failed");
                err = 1;
            } else if (sqlite3_exec(db, sql, NULL, NULL, &e) != SQLITE_OK) {
                appdb_say("%s: %s", names[i], e ? e : "update failed");
                sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
                err = 1;
            } else {
                int c = sqlite3_changes(db);
                if (sqlite3_exec(db, "COMMIT", NULL, NULL, &e) != SQLITE_OK) {
                    appdb_say("%s: %s", names[i], e ? e : "commit failed");
                    err = 1;
                } else {
                    changed += c;
                }
            }
            sqlite3_free(e);
            sqlite3_free(sql);
        }
        free(names[i]);
    }
    return err ? -1 : changed;
}

/* "System" group of the home screen (PlayStation Store, Game Library, ...). */
#define APPDB_DISP_SYSTEM 146
#define APPDB_DISP_GAMES  138

/* 1 if title_id exists (first table that has it); *pinned = in the system group. */
APPDB_FN int appdb_system_state(sqlite3 *db, const char *title_id, int *pinned) {
    char *names[64];
    int n = appdb_tables(db, names, 64);
    int found = 0;
    for (int i = 0; i < n; i++) {
        if (!found) {
            char *sql = sqlite3_mprintf(
                "SELECT dispLocation FROM \"%w\" WHERE titleId=%Q LIMIT 1", names[i], title_id);
            sqlite3_stmt *st = NULL;
            if (sql && sqlite3_prepare_v2(db, sql, -1, &st, NULL) == SQLITE_OK) {
                if (sqlite3_step(st) == SQLITE_ROW) {
                    *pinned = sqlite3_column_int(st, 0) == APPDB_DISP_SYSTEM;
                    found = 1;
                }
                sqlite3_finalize(st);
            }
            sqlite3_free(sql);
        }
        free(names[i]);
    }
    return found;
}

/* on=1: move title_id into the system group (dispLocation=146, pinnedStatus=2,
 *       lastAccessIndex just below the lowest one in that group) -> leftmost.
 * on=0: back to the games row (dispLocation=138, pinnedStatus=NULL, most recent).
 * Applied in every table where the title exists. Returns rows changed, or -1. */
APPDB_FN int appdb_set_system_pin(sqlite3 *db, const char *title_id, int on) {
    char *names[64];
    int n = appdb_tables(db, names, 64);
    int changed = 0, err = 0;
    if (n < 0) return -1;
    for (int i = 0; i < n; i++) {
        char *e = NULL;
        char *sql;
        if (on)
            sql = sqlite3_mprintf(
                "UPDATE \"%w\" SET dispLocation=%d, pinnedStatus=2, visible=1, lastAccessIndex="
                " (SELECT IFNULL(MIN(lastAccessIndex),2)-1 FROM \"%w\""
                "  WHERE dispLocation=%d AND titleId<>%Q) WHERE titleId=%Q",
                names[i], APPDB_DISP_SYSTEM, names[i], APPDB_DISP_SYSTEM, title_id, title_id);
        else
            sql = sqlite3_mprintf(
                "UPDATE \"%w\" SET dispLocation=%d, pinnedStatus=NULL, lastAccessIndex="
                " (SELECT IFNULL(MAX(lastAccessIndex),0)+1 FROM \"%w\" WHERE titleId<>%Q) "
                "WHERE titleId=%Q",
                names[i], APPDB_DISP_GAMES, names[i], title_id, title_id);
        if (!sql) { err = 1; }
        else if (sqlite3_exec(db, "BEGIN IMMEDIATE", NULL, NULL, &e) != SQLITE_OK) {
            appdb_say("%s: %s", names[i], e ? e : "begin failed"); err = 1;
        } else if (sqlite3_exec(db, sql, NULL, NULL, &e) != SQLITE_OK) {
            appdb_say("%s: %s", names[i], e ? e : "update failed");
            sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL); err = 1;
        } else {
            int c = sqlite3_changes(db);
            if (sqlite3_exec(db, "COMMIT", NULL, NULL, &e) != SQLITE_OK) {
                appdb_say("%s: %s", names[i], e ? e : "commit failed"); err = 1;
            } else changed += c;
        }
        sqlite3_free(e);
        sqlite3_free(sql);
        free(names[i]);
    }
    return err ? -1 : changed;
}

#endif /* APPDB_H */
