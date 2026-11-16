/*
 * dashboard.elf - PS5 payload (ps5-payload-sdk)
 *
 * Small web dashboard with "widgets" (clock, console storage, weather, state of the
 * home-screen tiles, Orbit Store state, shortcuts), styled as cards like the Welcome
 * Hub. Open  http://<IP de la PS5>:8096/  from a phone or a PC on the same network.
 *
 * app.db is only written when you flip a switch on the page (show/hide a home-screen tile,
 * pin Orbit Store); a backup is made once per run. Reboot the console to apply.
 *
 * Optional configuration files on the console:
 *   /data/dashboard_links.txt    one shortcut per line:  Label|http://...
 *   /data/dashboard_weather.txt  one line:               latitude,longitude,City
 */
#include <arpa/inet.h>
#include <ctype.h>
#include <netinet/in.h>
#include <dlfcn.h>
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/statvfs.h>
#include <stdint.h>
#include <unistd.h>

#include "appdb.h"
#include "dashboard_page.h"
#include "hub_patch_files.h"
#include "wh_inspector_files.h"
#include "orbit_latest_files.h"

#define DASH_PORT 8096
#define DASH_VERSION "1.0"
#define LINKS_FILE "/data/dashboard_links.txt"
#define WEATHER_FILE "/data/dashboard_weather.txt"
#define HUB_PATCH_DIR "/data/wh-widgets/hub-signin-patch"
/* Temporary diagnostic widget (NativeModules enumeration, see the virtual-controller
 * investigation). Independent of the toggle above; deployed/removed via /api/wh_inspector.
 * Remove WH_INSPECTOR_DIR, its deploy function, API route, source files and generator
 * once that investigation is done. */
#define WH_INSPECTOR_DIR "/data/wh-widgets/wh-inspector"
/* Experimental "Orbit Store - derniers ajouts" Welcome Hub widget, built on top of
 * the wh-inspector findings (VshNMApplicationLinkingPSModule.OpenURL works). First
 * version only tests whether a widget can render anything VISIBLE at all (so far,
 * only an invisible 8x8 box has ever been confirmed). Deployed/removed via
 * /api/orbit_latest, independently of the other widgets. */
#define ORBIT_LATEST_DIR "/data/wh-widgets/orbit-latest"

typedef struct {
    const char *title_id;
    const char *label;
} tile_t;

static const tile_t TILES[] = {
    {"NPXS40047", "PlayStation Store"},
    {"NPXS40063", "Welcome Hub"},
    {"NPXS40037", "PlayStation Plus"},
};
#define N_TILES (sizeof TILES / sizeof TILES[0])
#define ORBIT_ID "PPSA99177"

static int g_quit = 0;
static int g_backed_up = 0;
static time_t g_start = 0;
static int g_watch_orbit = 0; /* 1 = keep Orbit Store pinned+locked while running */

static int backup_once(void) {
    if (g_backed_up) return 0;
    if (appdb_backup() != 0) return -1;
    g_backed_up = 1;
    return 0;
}

/*
 * Deploys or removes the hub-signin-patch NiDUS widget under /data/wh-widgets/.
 * This makes the Welcome Hub screen clear its "Se connecter" gate (a local, cosmetic
 * patch -- see widget.js for details, it does NOT create a real PSN session). NiDUS
 * watches that directory and picks up file changes live, no reboot needed, as long as
 * NiDUS is already running on the console. Failures here are non-fatal: the tile
 * visibility change in app.db still goes through either way.
 */
/*
 * Self-extracting launcher: if the user glued an extra ELF (e.g. the NiDUS bridge
 * payload they already run manually) onto the end of THIS dashboard.elf file, pull
 * it back out to disk and run it once at startup. Format (last 16 bytes of the file):
 *   [embedded ELF bytes][8-byte magic "DASHEMB1"][8-byte little-endian length]
 * A plain dashboard.elf (no glued payload) just doesn't match the magic and this is
 * a no-op. The glued bytes are never part of this source tree / repository -- they
 * are appended locally, after the fact, to a build the user already has.
 */
#define EMBED_MAGIC "DASHEMB1"
#define EMBED_OUT_PATH "/data/.dashboard_embedded.elf"

static int extract_embedded(const char *out_path, size_t *out_len) {
    char self[1024];
    ssize_t n = readlink("/proc/curproc/file", self, sizeof(self) - 1);
    if (n <= 0) return -1;
    self[n] = 0;

    FILE *f = fopen(self, "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long fsize = ftell(f);
    if (fsize < 16) { fclose(f); return -1; }

    unsigned char tail[16];
    fseek(f, fsize - 16, SEEK_SET);
    if (fread(tail, 1, 16, f) != 16 || memcmp(tail, EMBED_MAGIC, 8) != 0) {
        fclose(f);
        return -1; /* no embedded payload glued on -- normal case */
    }
    uint64_t blen = 0;
    memcpy(&blen, tail + 8, 8);
    if (blen == 0 || (long)(16 + blen) > fsize) { fclose(f); return -1; }

    fseek(f, fsize - 16 - (long)blen, SEEK_SET);
    FILE *out = fopen(out_path, "wb");
    if (!out) { fclose(f); return -1; }
    unsigned char buf[65536];
    uint64_t remaining = blen;
    int ok = 1;
    while (remaining > 0) {
        size_t chunk = remaining < sizeof buf ? (size_t)remaining : sizeof buf;
        size_t r = fread(buf, 1, chunk, f);
        if (r == 0) { ok = 0; break; }
        if (fwrite(buf, 1, r, out) != r) { ok = 0; break; }
        remaining -= r;
    }
    fclose(f);
    fclose(out);
    if (!ok) { unlink(out_path); return -1; }
    chmod(out_path, 0755);
    if (out_len) *out_len = (size_t)blen;
    return 0;
}

/* Fire-and-forget: extract (if present) and launch the embedded bridge payload.
 * Non-fatal if anything fails -- the dashboard itself still runs either way. */
static void launch_embedded_bridge(void) {
    size_t blen = 0;
    if (extract_embedded(EMBED_OUT_PATH, &blen) != 0) return; /* no payload glued on */
    appdb_say("Embedded bridge payload extracted (%zu bytes), launching...", blen);
    pid_t pid = fork();
    if (pid == 0) {
        char *argv[] = { (char *)EMBED_OUT_PATH, NULL };
        execv(EMBED_OUT_PATH, argv);
        _exit(127); /* only reached if execv failed */
    } else if (pid > 0) {
        appdb_say("Embedded bridge payload launched (pid %d).", (int)pid);
    } else {
        appdb_say("Embedded bridge payload: fork failed (%s)", strerror(errno));
    }
}

static int write_file(const char *path, const char *content) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;
    size_t len = strlen(content);
    ssize_t w = write(fd, content, len);
    close(fd);
    return (w == (ssize_t)len) ? 0 : -1;
}

static void deploy_hub_patch(int on) {
    if (on) {
        if (mkdir("/data/wh-widgets", 0755) != 0 && errno != EEXIST) { /* best effort */ }
        if (mkdir(HUB_PATCH_DIR, 0755) != 0 && errno != EEXIST) {
            appdb_say("hub-signin-patch: could not create %s", HUB_PATCH_DIR);
            return;
        }
        char cfg[512], js[512];
        snprintf(cfg, sizeof cfg, "%s/config.json", HUB_PATCH_DIR);
        snprintf(js, sizeof js, "%s/widget.js", HUB_PATCH_DIR);
        if (write_file(cfg, HUB_PATCH_CONFIG_JSON) != 0 || write_file(js, HUB_PATCH_WIDGET_JS) != 0) {
            appdb_say("hub-signin-patch: failed writing widget files");
            return;
        }
        appdb_say("hub-signin-patch: deployed (NiDUS will pick it up live if running)");
    } else {
        char cfg[512], js[512];
        snprintf(cfg, sizeof cfg, "%s/config.json", HUB_PATCH_DIR);
        snprintf(js, sizeof js, "%s/widget.js", HUB_PATCH_DIR);
        unlink(cfg);
        unlink(js);
        rmdir(HUB_PATCH_DIR);
        appdb_say("hub-signin-patch: removed");
    }
}

static void deploy_wh_inspector(int on) {
    if (on) {
        if (mkdir("/data/wh-widgets", 0755) != 0 && errno != EEXIST) { /* best effort */ }
        if (mkdir(WH_INSPECTOR_DIR, 0755) != 0 && errno != EEXIST) {
            appdb_say("wh-inspector: could not create %s", WH_INSPECTOR_DIR);
            return;
        }
        char cfg[512], js[512];
        snprintf(cfg, sizeof cfg, "%s/config.json", WH_INSPECTOR_DIR);
        snprintf(js, sizeof js, "%s/widget.js", WH_INSPECTOR_DIR);
        if (write_file(cfg, WH_INSPECTOR_CONFIG_JSON) != 0 || write_file(js, WH_INSPECTOR_WIDGET_JS) != 0) {
            appdb_say("wh-inspector: failed writing widget files");
            return;
        }
        appdb_say("wh-inspector: deployed. Open the Welcome Hub, then check /data/wh-inspector.log");
    } else {
        char cfg[512], js[512];
        snprintf(cfg, sizeof cfg, "%s/config.json", WH_INSPECTOR_DIR);
        snprintf(js, sizeof js, "%s/widget.js", WH_INSPECTOR_DIR);
        unlink(cfg);
        unlink(js);
        rmdir(WH_INSPECTOR_DIR);
        appdb_say("wh-inspector: removed");
    }
}

static void deploy_orbit_latest(int on) {
    if (on) {
        if (mkdir("/data/wh-widgets", 0755) != 0 && errno != EEXIST) { /* best effort */ }
        if (mkdir(ORBIT_LATEST_DIR, 0755) != 0 && errno != EEXIST) {
            appdb_say("orbit-latest: could not create %s", ORBIT_LATEST_DIR);
            return;
        }
        char cfg[512], js[512];
        snprintf(cfg, sizeof cfg, "%s/config.json", ORBIT_LATEST_DIR);
        snprintf(js, sizeof js, "%s/widget.js", ORBIT_LATEST_DIR);
        if (write_file(cfg, ORBIT_LATEST_CONFIG_JSON) != 0 || write_file(js, ORBIT_LATEST_WIDGET_JS) != 0) {
            appdb_say("orbit-latest: failed writing widget files");
            return;
        }
        appdb_say("orbit-latest: deployed. Open the Welcome Hub, then check /data/orbit-latest.log");
    } else {
        char cfg[512], js[512];
        snprintf(cfg, sizeof cfg, "%s/config.json", ORBIT_LATEST_DIR);
        snprintf(js, sizeof js, "%s/widget.js", ORBIT_LATEST_DIR);
        unlink(cfg);
        unlink(js);
        rmdir(ORBIT_LATEST_DIR);
        appdb_say("orbit-latest: removed");
    }
}

static int set_tile(const char *title_id, int value) {
    sqlite3 *db = appdb_open();
    if (!db) return -1;
    int total = 0, vis = 0;
    if (appdb_count(db, title_id, &total, &vis) != 0 || total == 0 || backup_once() != 0) {
        sqlite3_close(db);
        return -1;
    }
    int changed = appdb_set_visible(db, title_id, value);
    sqlite3_close(db);
    if (changed >= 0) {
        appdb_say("%s: %s (%d row(s)). Reboot to apply.", title_id, value ? "SHOWN" : "HIDDEN", changed);
        if (!strcmp(title_id, "NPXS40063")) deploy_hub_patch(value);
    }
    return changed;
}

static int set_orbit(int on) {
    sqlite3 *db = appdb_open();
    if (!db) return -1;
    int pinned = 0;
    if (!appdb_system_state(db, ORBIT_ID, &pinned) || backup_once() != 0) { sqlite3_close(db); return -1; }
    int changed = appdb_set_system_pin(db, ORBIT_ID, on);
    if (changed >= 0 && appdb_set_lock(db, ORBIT_ID, on) != 0) changed = -1;
    sqlite3_close(db);
    if (changed >= 0) {
        g_watch_orbit = on;
        appdb_say("Orbit Store: %s. Reboot to apply.", on ? "PINNED + LOCKED" : "UNPINNED");
    }
    return changed;
}

/* The console rewrites a tile's lastAccessIndex when that app is launched, which drops
 * the lock. While the dashboard runs (and Orbit Store was pinned via the switch, or was
 * already pinned at startup), re-apply the lock as soon as it is lost. */
static void watch_tick(void) {
    if (!g_watch_orbit) return;
    sqlite3 *db = appdb_open();
    if (!db) return;
    int pinned = 0;
    if (appdb_system_state(db, ORBIT_ID, &pinned) && !pinned) {
        if (backup_once() == 0 && appdb_set_system_pin(db, ORBIT_ID, 1) >= 0
            && appdb_set_lock(db, ORBIT_ID, 1) == 0)
            appdb_say("Orbit Store: lock re-applied.");
    }
    sqlite3_close(db);
}

static void query_get(const char *query, const char *key, char *out, size_t n) {
    size_t kl = strlen(key);
    out[0] = 0;
    for (const char *p = query; p && *p;) {
        if (!strncmp(p, key, kl) && p[kl] == '=') {
            p += kl + 1;
            size_t i = 0;
            while (*p && *p != '&' && i + 1 < n) out[i++] = *p++;
            out[i] = 0;
            return;
        }
        p = strchr(p, '&');
        if (p) p++;
    }
}

static size_t json_str_nl(char *out, size_t cap, size_t off, const char *s);

/* Diagnostic: checks (in a child process, so a crash cannot kill the dashboard) which
 * system libraries/functions needed for controller battery and trophies can be reached.
 * It only resolves symbols; it calls none of them. */
typedef int (*load_fn)(const char *, size_t, const void *, unsigned, const void *, int *);
typedef int (*dlsym_fn)(int, const char *, void **);

static void probe_run(FILE *o) {
    static const char *mods[] = {"libSceUserService", "libScePad", "libSceNpTrophy",
                                 "libSceNpTrophy2", "libSceSystemService"};
    static const char *syms[] = {"sceUserServiceInitialize", "sceUserServiceGetLoginUserIdList",
                                 "sceUserServiceGetInitialUser", "scePadInit", "scePadOpen",
                                 "scePadGetHandle", "scePadGetControllerInformation",
                                 "scePadReadState", "sceNpTrophyCreateContext",
                                 "sceNpTrophyCreateHandle", "sceNpTrophyGetGameProgress",
                                 "sceSystemServiceGetStatus"};
    load_fn load = (load_fn)dlsym(RTLD_DEFAULT, "sceKernelLoadStartModule");
    dlsym_fn kdlsym = (dlsym_fn)dlsym(RTLD_DEFAULT, "sceKernelDlsym");
    fprintf(o, "sceKernelLoadStartModule: %s\nsceKernelDlsym: %s\n", load ? "OK" : "-", kdlsym ? "OK" : "-");
    fflush(o);
    if (!load) { fprintf(o, "end\n"); return; }
    for (size_t i = 0; i < sizeof mods / sizeof *mods; i++) {
        char path[160];
        snprintf(path, sizeof path, "/system/common/lib/%s.sprx", mods[i]);
        int res = 0;
        int h = load(path, 0, NULL, 0, NULL, &res);
        fprintf(o, "%s: handle=%d (0x%x) res=%d\n", mods[i], h, (unsigned)h, res);
        fflush(o);
        if (h <= 0 || !kdlsym) continue;
        for (size_t j = 0; j < sizeof syms / sizeof *syms; j++) {
            void *addr = NULL;
            int r = kdlsym(h, syms[j], &addr);
            if (r == 0 && addr) fprintf(o, "  sym %s: OK\n", syms[j]);
        }
        fflush(o);
    }
    fprintf(o, "end\n");
}

static void probe_json(char *out, size_t cap) {
    int pfd[2];
    size_t off = 0;
    char raw[3072];
    size_t n = 0;
    raw[0] = 0;
    if (pipe(pfd) != 0) { snprintf(out, cap, "{\"log\":\"pipe failed\"}"); return; }
    pid_t pid = fork();
    if (pid == 0) {
        close(pfd[0]);
        FILE *o = fdopen(pfd[1], "w");
        probe_run(o);
        fclose(o);
        _exit(0);
    }
    close(pfd[1]);
    if (pid > 0) {
        time_t t0 = time(NULL);
        fcntl(pfd[0], F_SETFL, O_NONBLOCK);
        int st = 0, done = 0;
        while (time(NULL) - t0 < 4 && !done) {
            ssize_t r = read(pfd[0], raw + n, sizeof raw - 1 - n);
            if (r > 0) n += (size_t)r;
            if (waitpid(pid, &st, WNOHANG) == pid) done = 1;
            else usleep(50000);
        }
        ssize_t r;
        while ((r = read(pfd[0], raw + n, sizeof raw - 1 - n)) > 0) n += (size_t)r;
        if (!done) { kill(pid, SIGKILL); waitpid(pid, &st, 0); }
        raw[n] = 0;
        if (!done) n += (size_t)snprintf(raw + n, sizeof raw - n, "TIMEOUT (blocked)\n");
        else if (WIFSIGNALED(st)) n += (size_t)snprintf(raw + n, sizeof raw - n, "CRASH signal %d\n", WTERMSIG(st));
    }
    close(pfd[0]);
    off += (size_t)snprintf(out + off, cap - off, "{\"log\":\"");
    off = json_str_nl(out, cap, off, raw);
    snprintf(out + off, cap - off, "\"}");
    FILE *f = fopen("/data/dashboard_probe.log", "w");
    if (f) { fputs(raw, f); fclose(f); }
}

static void send_all(int fd, const char *p, size_t n) {
    while (n) {
        ssize_t w = send(fd, p, n, 0);
        if (w < 0) {
            if (errno == EINTR) continue;
            return;
        }
        p += w;
        n -= (size_t)w;
    }
}

static void reply(int fd, int code, const char *ctype, const char *body) {
    char head[256];
    const char *msg = code == 200 ? "OK" : code == 403 ? "Forbidden"
                    : code == 404 ? "Not Found" : "Internal Server Error";
    size_t len = strlen(body);
    int n = snprintf(head, sizeof head,
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
        "Cache-Control: no-store\r\nConnection: close\r\n\r\n", code, msg, ctype, len);
    send_all(fd, head, (size_t)n);
    send_all(fd, body, len);
}

/* Appends s to out as a JSON string body (escaped); returns the new offset. */
static size_t json_str(char *out, size_t cap, size_t off, const char *s) {
    for (; *s && off + 8 < cap; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { out[off++] = '\\'; out[off++] = (char)c; }
        else if (c < 0x20) { out[off++] = ' '; }
        else out[off++] = (char)c;
    }
    return off;
}

static size_t json_str_nl(char *out, size_t cap, size_t off, const char *s) {
    for (; *s && off + 8 < cap; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { out[off++] = '\\'; out[off++] = (char)c; }
        else if (c == '\n') { out[off++] = '\\'; out[off++] = 'n'; }
        else if (c < 0x20) out[off++] = ' ';
        else out[off++] = (char)c;
    }
    return off;
}

static void local_ip(char *out, size_t n) {
    snprintf(out, n, "IP_DE_LA_PS5");
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return;
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(53);
    inet_pton(AF_INET, "192.0.2.1", &a.sin_addr);
    socklen_t l = sizeof a;
    if (connect(s, (struct sockaddr *)&a, sizeof a) == 0 &&
        getsockname(s, (struct sockaddr *)&a, &l) == 0)
        inet_ntop(AF_INET, &a.sin_addr, out, (socklen_t)n);
    close(s);
}

/* Only http(s) links are accepted (avoids javascript: URLs in the page). */
static int url_ok(const char *u) {
    return !strncmp(u, "http://", 7) || !strncmp(u, "https://", 8);
}

static size_t links_json(char *out, size_t cap, size_t off) {
    off += (size_t)snprintf(out + off, cap - off, "\"links\":[");
    FILE *f = fopen(LINKS_FILE, "r");
    int n = 0;
    if (f) {
        char line[512];
        while (fgets(line, sizeof line, f) && n < 12 && off + 600 < cap) {
            line[strcspn(line, "\r\n")] = 0;
            char *bar = strchr(line, '|');
            if (!bar || line[0] == '#') continue;
            *bar = 0;
            const char *label = line, *url = bar + 1;
            if (!*label || !url_ok(url)) continue;
            off += (size_t)snprintf(out + off, cap - off, "%s{\"label\":\"", n ? "," : "");
            off = json_str(out, cap, off, label);
            off += (size_t)snprintf(out + off, cap - off, "\",\"url\":\"");
            off = json_str(out, cap, off, url);
            off += (size_t)snprintf(out + off, cap - off, "\"}");
            n++;
        }
        fclose(f);
    }
    off += (size_t)snprintf(out + off, cap - off, "]");
    return off;
}

static void info_json(char *out, size_t cap, const char *ip) {
    size_t off = 0;
    off += (size_t)snprintf(out + off, cap - off, "{\"ip\":\"%s\",\"version\":\"%s\",\"uptime\":%ld,",
                            ip, DASH_VERSION, (long)(time(NULL) - g_start));

    /* storage: first path that answers */
    unsigned long long total = 0, freeb = 0;
    static const char *paths[] = {"/user", "/data", "/"};
    for (size_t i = 0; i < 3 && !total; i++) {
        struct statvfs sv;
        if (statvfs(paths[i], &sv) == 0 && sv.f_blocks) {
            total = (unsigned long long)sv.f_blocks * sv.f_frsize;
            freeb = (unsigned long long)sv.f_bavail * sv.f_frsize;
        }
    }
    off += (size_t)snprintf(out + off, cap - off, "\"storage\":{\"total\":%llu,\"free\":%llu},", total, freeb);

    /* tiles + Orbit (read-only) */
    sqlite3 *db = appdb_open();
    off += (size_t)snprintf(out + off, cap - off, "\"tiles\":[");
    for (size_t i = 0; i < N_TILES; i++) {
        int t = 0, v = 0;
        if (db) appdb_count(db, TILES[i].title_id, &t, &v);
        off += (size_t)snprintf(out + off, cap - off, "%s{\"id\":\"%s\",\"label\":\"%s\",\"found\":%s,\"visible\":%s}",
                                i ? "," : "", TILES[i].title_id, TILES[i].label, t > 0 ? "true" : "false", v > 0 ? "true" : "false");
    }
    int pinned = 0, found = db ? appdb_system_state(db, ORBIT_ID, &pinned) : 0;
    off += (size_t)snprintf(out + off, cap - off, "],\"orbit\":{\"found\":%s,\"pinned\":%s},",
                            found ? "true" : "false", pinned ? "true" : "false");
    if (db) sqlite3_close(db);

    /* weather location */
    char lat[32] = "48.8566", lon[32] = "2.3522", city[96] = "Paris";
    FILE *f = fopen(WEATHER_FILE, "r");
    if (f) {
        char line[200];
        if (fgets(line, sizeof line, f)) {
            line[strcspn(line, "\r\n")] = 0;
            char a[32], b[32], c[96];
            if (sscanf(line, "%31[^,],%31[^,],%95[^\n]", a, b, c) == 3) {
                int ok = 1;
                for (const char *p = a; *p; p++) if (!strchr("0123456789.-", *p)) ok = 0;
                for (const char *p = b; *p; p++) if (!strchr("0123456789.-", *p)) ok = 0;
                if (ok) { snprintf(lat, sizeof lat, "%s", a); snprintf(lon, sizeof lon, "%s", b); snprintf(city, sizeof city, "%s", c); }
            }
        }
        fclose(f);
    }
    off += (size_t)snprintf(out + off, cap - off, "\"weather\":{\"lat\":%s,\"lon\":%s,\"city\":\"", lat, lon);
    off = json_str(out, cap, off, city);
    off += (size_t)snprintf(out + off, cap - off, "\"},");

    off = links_json(out, cap, off);

    struct stat whst;
    int wh_deployed = stat(WH_INSPECTOR_DIR, &whst) == 0;
    off += (size_t)snprintf(out + off, cap - off, ",\"wh_inspector\":%s", wh_deployed ? "true" : "false");

    struct stat olst;
    int ol_deployed = stat(ORBIT_LATEST_DIR, &olst) == 0;
    off += (size_t)snprintf(out + off, cap - off, ",\"orbit_latest\":%s", ol_deployed ? "true" : "false");

    off += (size_t)snprintf(out + off, cap - off, "}");
}

static int has_header(const char *req) {
    for (const char *p = req; (p = strchr(p, '\n')); ) {
        p++;
        if (!strncasecmp(p, "X-Menu:", 7)) return 1;
        if (*p == '\r' || *p == '\n') break;
    }
    return 0;
}

/* Decodes a hex string (as produced by a NiDUS widget's hexOf()) into out,
 * truncating safely; returns the decoded length. */
static size_t hex_decode(const char *hex, char *out, size_t cap) {
    size_t n = 0;
    while (hex[0] && hex[1] && n + 1 < cap) {
        int hi = isxdigit((unsigned char)hex[0]) ? (hex[0] <= '9' ? hex[0] - '0' : (hex[0] | 0x20) - 'a' + 10) : -1;
        int lo = isxdigit((unsigned char)hex[1]) ? (hex[1] <= '9' ? hex[1] - '0' : (hex[1] | 0x20) - 'a' + 10) : -1;
        if (hi < 0 || lo < 0) break;
        out[n++] = (char)((hi << 4) | lo);
        hex += 2;
    }
    out[n] = 0;
    return n;
}

/* Receives diagnostic log lines from a NiDUS widget running inside the Welcome Hub
 * (POST /__log?w=<hex widget id>, raw message as the request body). Appends them to
 * /data/<widget>.log. The widget id stays hex-encoded in the query string (it's tiny,
 * well under any line-length limit); the message itself goes in the body instead of
 * the URL because NiDUS's internal HTTP client truncates the request LINE (method +
 * path + version) at around 256 bytes -- a GET with a long hex-encoded message was
 * silently cut mid-word every time, which a body is not subject to.
 * No auth header required: NiDUS's embedded webview cannot set the X-Menu header this
 * server otherwise requires, and this endpoint only ever receives text, never acts on it. */
static void handle_widget_log(int fd, const char *query, const char *body) {
    char w[64] = {0}, widget[32] = {0};
    query_get(query ? query : "", "w", w, sizeof w);
    hex_decode(w, widget, sizeof widget);
    if (!widget[0]) snprintf(widget, sizeof widget, "widget");

    (void)mkdir("/data", 0755);
    char path[96];
    snprintf(path, sizeof path, "/data/%.40s.log", widget);
    static char msg[4001];
    size_t i = 0;
    for (const char *p = body ? body : ""; *p && *p != '\r' && *p != '\n' && i + 1 < sizeof msg; p++)
        msg[i++] = *p;
    msg[i] = 0;

    FILE *f = fopen(path, "a");
    if (f) {
        time_t now = time(NULL);
        fprintf(f, "%lld %s\n", (long long)now, msg);
        fclose(f);
    }
    reply(fd, 200, "text/plain", "ok");
}

/* Returns the content of a widget's /data/<name>.log as a JSON string, for the
 * dashboard's "Diagnostic" panel. Requires the same X-Menu header as the other
 * admin actions. */
static void handle_widget_log_view(int fd, const char *log_path) {
    static char buf[16384];
    size_t off = (size_t)snprintf(buf, sizeof buf, "{\"log\":\"");
    FILE *f = fopen(log_path, "r");
    if (f) {
        char line[1024];
        while (fgets(line, sizeof line, f) && off + 64 < sizeof buf)
            off = json_str_nl(buf, sizeof buf, off, line);
        fclose(f);
    }
    off += (size_t)snprintf(buf + off, sizeof buf - off, "\"}");
    reply(fd, 200, "application/json", buf);
}

/* Reads a full HTTP request (headers + body, if any) into req/cap, looping recv()
 * until the header terminator is seen and, if Content-Length is present, until that
 * many body bytes have arrived too. Needed because a POST body (used by /__log) can
 * easily span more than one TCP read; a single recv() silently truncated it before.
 * Returns the total bytes read (always NUL-terminated within cap), or <= 0 on error.
 * *body is set to point at the start of the body within req (or the end of the
 * buffer, as an empty string, if there is none). */
static ssize_t read_request(int fd, char *req, size_t cap, char **body) {
    size_t have = 0;
    char *hdr_end = NULL;
    for (;;) {
        if (have + 1 >= cap) break;
        ssize_t n = recv(fd, req + have, cap - 1 - have, 0);
        if (n <= 0) { if (have == 0) return n; break; }
        have += (size_t)n;
        req[have] = 0;
        hdr_end = strstr(req, "\r\n\r\n");
        if (hdr_end) break;
    }
    if (!hdr_end) { *body = req + have; return (ssize_t)have; }
    *body = hdr_end + 4;

    long want = -1;
    for (const char *p = req; (p = strchr(p, '\n')); ) {
        p++;
        if (!strncasecmp(p, "Content-Length:", 15)) { want = strtol(p + 15, NULL, 10); break; }
        if (*p == '\r' || *p == '\n') break;
    }
    if (want > 0) {
        size_t body_off = (size_t)(*body - req);
        long got = (long)have - (long)body_off;
        while (got < want && have + 1 < cap) {
            ssize_t n = recv(fd, req + have, cap - 1 - have, 0);
            if (n <= 0) break;
            have += (size_t)n;
            req[have] = 0;
            got += n;
        }
        *body = req + body_off; /* req may not have moved, but be explicit */
    }
    return (ssize_t)have;
}

static void handle(int fd) {
    static char req[8192];
    char *body;
    ssize_t n = read_request(fd, req, sizeof req, &body);
    if (n <= 0) return;
    char method[8], path[256];
    if (sscanf(req, "%7s %255s", method, path) != 2) return;
    char *query = strchr(path, '?');
    if (query) *query++ = 0;

    if (!strcmp(method, "GET") && !strcmp(path, "/")) {
        reply(fd, 200, "text/html; charset=utf-8", PAGE);
    } else if (!strcmp(method, "POST") && !strcmp(path, "/__log")) {
        handle_widget_log(fd, query, body);
    } else if (!strcmp(method, "GET") && !strcmp(path, "/api/info")) {
        static char buf[8192];
        char ip[64];
        local_ip(ip, sizeof ip);
        info_json(buf, sizeof buf, ip);
        reply(fd, 200, "application/json", buf);
    } else if (!strcmp(method, "POST") && (!strcmp(path, "/api/set") || !strcmp(path, "/api/orbit"))) {
        if (!has_header(req)) { reply(fd, 403, "text/plain", "forbidden"); return; }
        char id[16], v[4];
        query_get(query ? query : "", "id", id, sizeof id);
        query_get(query ? query : "", "v", v, sizeof v);
        if (strcmp(v, "0") && strcmp(v, "1")) { reply(fd, 400, "text/plain", "bad params"); return; }
        int r = -1;
        if (!strcmp(path, "/api/orbit")) r = set_orbit(v[0] == '1');
        else for (size_t i = 0; i < N_TILES; i++)
            if (!strcmp(id, TILES[i].title_id)) r = set_tile(id, v[0] == '1');
        if (r < 0) reply(fd, 500, "text/plain", "failed");
        else reply(fd, 200, "application/json", "{\"ok\":true}");
    } else if (!strcmp(method, "POST") && !strcmp(path, "/api/wh_inspector")) {
        if (!has_header(req)) { reply(fd, 403, "text/plain", "forbidden"); return; }
        char v[4];
        query_get(query ? query : "", "v", v, sizeof v);
        if (strcmp(v, "0") && strcmp(v, "1")) { reply(fd, 400, "text/plain", "bad params"); return; }
        deploy_wh_inspector(v[0] == '1');
        reply(fd, 200, "application/json", "{\"ok\":true}");
    } else if (!strcmp(method, "GET") && !strcmp(path, "/api/wh_inspector_log")) {
        if (!has_header(req)) { reply(fd, 403, "text/plain", "forbidden"); return; }
        handle_widget_log_view(fd, "/data/wh-inspector.log");
    } else if (!strcmp(method, "POST") && !strcmp(path, "/api/orbit_latest")) {
        if (!has_header(req)) { reply(fd, 403, "text/plain", "forbidden"); return; }
        char v[4];
        query_get(query ? query : "", "v", v, sizeof v);
        if (strcmp(v, "0") && strcmp(v, "1")) { reply(fd, 400, "text/plain", "bad params"); return; }
        deploy_orbit_latest(v[0] == '1');
        reply(fd, 200, "application/json", "{\"ok\":true}");
    } else if (!strcmp(method, "GET") && !strcmp(path, "/api/orbit_latest_log")) {
        if (!has_header(req)) { reply(fd, 403, "text/plain", "forbidden"); return; }
        handle_widget_log_view(fd, "/data/orbit-latest.log");
    } else if (!strcmp(method, "POST") && !strcmp(path, "/api/probe")) {
        if (!has_header(req)) { reply(fd, 403, "text/plain", "forbidden"); return; }
        static char pb[4096];
        probe_json(pb, sizeof pb);
        reply(fd, 200, "application/json", pb);
    } else if (!strcmp(method, "POST") && !strcmp(path, "/api/quit")) {
        if (!has_header(req)) { reply(fd, 403, "text/plain", "forbidden"); return; }
        g_quit = 1;
        reply(fd, 200, "application/json", "{\"ok\":true}");
    } else {
        reply(fd, 404, "text/plain", "not found");
    }
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    g_start = time(NULL);

    {   /* if Orbit Store is already pinned at startup, keep watching its lock */
        sqlite3 *db = appdb_open();
        if (db) {
            int pinned = 0;
            if (appdb_system_state(db, ORBIT_ID, &pinned) && pinned) g_watch_orbit = 1;
            sqlite3_close(db);
        }
    }

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { appdb_say("socket: %s", strerror(errno)); return 1; }
    int one = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(DASH_PORT);
    int bound = bind(srv, (struct sockaddr *)&addr, sizeof addr) == 0;
    if (!bound) {
        /* A previous dashboard is still running: ask it to quit, then take over. */
        int c = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in lo = addr;
        lo.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (c >= 0 && connect(c, (struct sockaddr *)&lo, sizeof lo) == 0) {
            static const char q[] = "POST /api/quit HTTP/1.1\r\nHost: localhost\r\nX-Menu: 1\r\nContent-Length: 0\r\n\r\n";
            send_all(c, q, sizeof q - 1);
            char tmp[128];
            recv(c, tmp, sizeof tmp, 0);
            appdb_say("Previous dashboard asked to quit.");
        }
        if (c >= 0) close(c);
        for (int i = 0; i < 20 && !bound; i++) {
            usleep(250000);
            bound = bind(srv, (struct sockaddr *)&addr, sizeof addr) == 0;
        }
    }
    if (!bound || listen(srv, 4) != 0) {
        appdb_say("Port %d unavailable (dashboard already running?): %s", DASH_PORT, strerror(errno));
        close(srv);
        return 1;
    }

    char ip[64];
    local_ip(ip, sizeof ip);
    appdb_say("Dashboard: http://%s:%d/", ip, DASH_PORT);

    launch_embedded_bridge();

    while (!g_quit) {
        fd_set rs;
        FD_ZERO(&rs);
        FD_SET(srv, &rs);
        struct timeval wt = {2, 0};
        int sr = select(srv + 1, &rs, NULL, NULL, &wt);
        if (sr == 0) { watch_tick(); continue; }
        if (sr < 0) { if (errno == EINTR) continue; break; }
        int fd = accept(srv, NULL, NULL);
        if (fd < 0) {
            if (errno == EINTR) continue;
            break;
        }
        struct timeval tv = {3, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        handle(fd);
        close(fd);
    }
    close(srv);
    appdb_say("Dashboard stopped.");
    return 0;
}
