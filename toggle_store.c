/*
 * toggle_store.elf - PS5 payload (ps5-payload-sdk)
 *
 * Toggles the PlayStation Store icon (NPXS40047):
 *   - icon visible  -> hides it  (visible=0)
 *   - icon hidden   -> shows it  (visible=1)
 * Backs up app.db before each change. Reboot the console afterwards.
 */
#include "appdb.h"

#define TITLE_ID "NPXS40047"

int main(void) {
    sqlite3 *db = appdb_open();
    if (!db) return 1;

    int total = 0, visible = 0;
    if (appdb_count(db, TITLE_ID, &total, &visible) != 0) {
        sqlite3_close(db);
        return 1;
    }
    if (total == 0) {
        appdb_say("%s (PlayStation Store) not found in tbl_iconinfo*. Nothing changed.",
                  TITLE_ID);
        sqlite3_close(db);
        return 1;
    }

    /* Any visible row -> hide; everything hidden -> show. */
    int target = visible > 0 ? 0 : 1;

    if (appdb_backup() != 0) {
        sqlite3_close(db);
        return 1;
    }
    int changed = appdb_set_visible(db, TITLE_ID, target);
    sqlite3_close(db);
    if (changed < 0) return 1;

    appdb_say("PlayStation Store %s (%d row(s)). Reboot the console.",
              target ? "SHOWN" : "HIDDEN", changed);
    return 0;
}
