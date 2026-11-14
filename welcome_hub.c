/*
 * welcome_hub.elf - PS5 payload (ps5-payload-sdk)
 *
 * Backs up app.db, then sets visible=1 for NPXS40063 in tbl_iconinfo*.
 * Reboot the console afterwards.
 */
#include "appdb.h"

#define TITLE_ID "NPXS40063"

int main(void) {
    sqlite3 *db = appdb_open();
    if (!db) return 1;

    int total = 0, visible = 0;
    if (appdb_count(db, TITLE_ID, &total, &visible) != 0) {
        sqlite3_close(db);
        return 1;
    }
    if (total == 0) {
        appdb_say("%s not found in tbl_iconinfo*. Nothing changed.", TITLE_ID);
        sqlite3_close(db);
        return 1;
    }
    if (visible == total) {
        appdb_say("%s is already visible. Nothing changed.", TITLE_ID);
        sqlite3_close(db);
        return 0;
    }

    if (appdb_backup() != 0) {
        sqlite3_close(db);
        return 1;
    }
    int changed = appdb_set_visible(db, TITLE_ID, 1);
    sqlite3_close(db);
    if (changed < 0) return 1;

    appdb_say("%s now visible (%d row(s)). Reboot the console.", TITLE_ID, changed);
    return 0;
}
