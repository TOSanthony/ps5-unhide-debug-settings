/*
 * dashboard.elf - PS5 payload (ps5-payload-sdk)
 *
 * Small web dashboard with "widgets" (clock, console storage, weather, state of the
 * home-screen tiles, Orbit Store state, shortcuts), styled as cards like the Welcome
 * Hub. Open  http://<IP de la PS5>:8096/  from a phone or a PC on the same network.
 *
 * Read-only: it never writes app.db.
 *
 * Optional configuration files on the console:
 *   /data/dashboard_links.txt    one shortcut per line:  Label|http://...
 *   /data/dashboard_weather.txt  one line:               latitude,longitude,City
 */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/statvfs.h>

#include "appdb.h"
#include "dashboard_page.h"

#define DASH_PORT 8096
#define DASH_VERSION "1.0"
#define LINKS_FILE "/data/dashboard_links.txt"
#define WEATHER_FILE "/data/dashboard_weather.txt"

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
static time_t g_start = 0;

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
        off += (size_t)snprintf(out + off, cap - off, "%s{\"label\":\"%s\",\"visible\":%s}",
                                i ? "," : "", TILES[i].label, v > 0 ? "true" : "false");
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

static void handle(int fd) {
    char req[2048];
    ssize_t n = recv(fd, req, sizeof req - 1, 0);
    if (n <= 0) return;
    req[n] = 0;
    char method[8], path[256];
    if (sscanf(req, "%7s %255s", method, path) != 2) return;

    if (!strcmp(method, "GET") && !strcmp(path, "/")) {
        reply(fd, 200, "text/html; charset=utf-8", PAGE);
    } else if (!strcmp(method, "GET") && !strcmp(path, "/api/info")) {
        static char buf[8192];
        char ip[64];
        local_ip(ip, sizeof ip);
        info_json(buf, sizeof buf, ip);
        reply(fd, 200, "application/json", buf);
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

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { appdb_say("socket: %s", strerror(errno)); return 1; }
    int one = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(DASH_PORT);
    if (bind(srv, (struct sockaddr *)&addr, sizeof addr) != 0 || listen(srv, 4) != 0) {
        appdb_say("Port %d unavailable (dashboard already running?): %s", DASH_PORT, strerror(errno));
        close(srv);
        return 1;
    }

    char ip[64];
    local_ip(ip, sizeof ip);
    appdb_say("Dashboard: http://%s:%d/", ip, DASH_PORT);

    while (!g_quit) {
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
