/*
 * icon_menu.elf - PS5 payload (ps5-payload-sdk)
 *
 * Small web menu with switches to show/hide home-screen icons:
 *   - PlayStation Store (NPXS40047)
 *   - Welcome Hub       (NPXS40063)
 *   - PlayStation Plus  (NPXS40037)
 * and a switch to pin Orbit Store (PPSA99177) like the PlayStation Store
 * (system group, dispLocation=146, leftmost).
 *
 * Launch the ELF, then open  http://<IP de la PS5>:8095/  from a phone or PC
 * on the same network (the IP is shown in a notification and on stdout).
 * app.db is backed up once, before the first change of each run.
 * Reboot the console after changing switches.
 */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/socket.h>

#include "appdb.h"

#define MENU_PORT 8095

typedef struct {
    const char *id;
    const char *title_id;
    const char *label;
} item_t;

static const item_t ITEMS[] = {
    {"store", "NPXS40047", "PlayStation Store"},
    {"hub",   "NPXS40063", "Welcome Hub"},
    {"plus",  "NPXS40037", "PlayStation Plus"},
};
#define N_ITEMS (sizeof ITEMS / sizeof ITEMS[0])

/* Apps that can be pinned like the PlayStation Store (system group, leftmost). */
static const item_t PINS[] = {
    {"orbit", "PPSA99177", "Orbit Store"},
};
#define N_PINS (sizeof PINS / sizeof PINS[0])


static int g_backed_up = 0;
static int g_quit = 0;
static int g_watch[N_PINS];   /* 1 = keep this pin locked while the menu runs */

static const char PAGE[] =
"<!doctype html><html lang=fr><meta charset=utf-8>"
"<meta name=viewport content='width=device-width,initial-scale=1'>"
"<title>PS5 - Icônes</title><style>"
"body{font-family:system-ui,sans-serif;background:#0b0f1a;color:#e8ecf5;margin:0;padding:24px}"
"main{max-width:460px;margin:auto}h1{font-size:22px}"
".row{display:flex;align-items:center;justify-content:space-between;background:#141a2b;"
"border-radius:12px;padding:16px;margin:12px 0}.row small{display:block;color:#8a93ad;margin-top:4px}"
".sw{position:relative;width:52px;height:30px;flex:none}.sw input{opacity:0;width:0;height:0}"
".sl{position:absolute;inset:0;background:#3a435c;border-radius:30px;transition:.2s;cursor:pointer}"
".row button{margin:0}button:disabled{opacity:.4}"
".sl:before{content:'';position:absolute;width:24px;height:24px;left:3px;top:3px;background:#fff;"
"border-radius:50%;transition:.2s}input:checked+.sl{background:#2f7cf6}"
"input:checked+.sl:before{transform:translateX(22px)}input:disabled+.sl{opacity:.4;cursor:not-allowed}"
"#msg{min-height:1.4em;color:#8fd19e}button{background:#252d45;color:#e8ecf5;border:0;"
"border-radius:8px;padding:10px 14px;margin-top:16px}</style>"
"<main><h1>Icônes de l'écran d'accueil</h1><div id=list>Chargement…</div>"
"<p id=msg></p><button onclick=quit()>Arrêter le menu</button></main><script>"
"const H={'X-Menu':'1'};"
"async function load(){const r=await fetch('/api/state');const s=await r.json();"
"const l=document.getElementById('list');l.innerHTML='';"
"for(const i of s.items){const d=document.createElement('div');d.className='row';"
"d.innerHTML='<div>'+i.label+'<small>'+(i.found?i.title_id:'introuvable dans app.db')+'</small></div>"
"<label class=sw><input type=checkbox '+(i.visible?'checked ':'')+(i.found?'':'disabled ')+"
"'><span class=sl></span></label>';"
"d.querySelector('input').onchange=e=>set(i.id,e.target.checked);l.appendChild(d)}"
"for(const p of s.pins){const d=document.createElement('div');d.className='row';"
"d.innerHTML='<div>'+p.label+'<small>'+(p.found?'Épinglé comme le PlayStation Store':'introuvable dans app.db')+'</small></div>"
"<label class=sw><input type=checkbox '+(p.pinned?'checked ':'')+(p.found?'':'disabled ')+"
"'><span class=sl></span></label>';"
"d.querySelector('input').onchange=e=>pin(p.id,e.target.checked);l.appendChild(d)}}"
"async function pin(id,v){const m=document.getElementById('msg');"
"const r=await fetch('/api/pin?id='+id+'&v='+(v?1:0),{method:'POST',headers:H});"
"m.textContent=r.ok?'Enregistré. Redémarre la PS5 pour appliquer.':'Erreur ('+r.status+')';"
"m.style.color=r.ok?'#8fd19e':'#ff8a8a';load()}"
"async function set(id,v){const m=document.getElementById('msg');"
"const r=await fetch('/api/set?id='+id+'&v='+(v?1:0),{method:'POST',headers:H});"
"m.textContent=r.ok?'Enregistré. Redémarre la PS5 pour appliquer.':'Erreur ('+r.status+')';"
"m.style.color=r.ok?'#8fd19e':'#ff8a8a';load()}"
"async function quit(){await fetch('/api/quit',{method:'POST',headers:H});"
"document.body.innerHTML='<main><p>Menu arrêté.</p></main>'}"
"load()</script></html>";

static void send_all(int fd, const char *p, size_t n) {
    while (n > 0) {
        ssize_t w = send(fd, p, n, 0);
        if (w <= 0) return;
        p += w;
        n -= (size_t)w;
    }
}

static void reply(int fd, int code, const char *ctype, const char *body) {
    char head[256];
    const char *msg = code == 200 ? "OK" : code == 400 ? "Bad Request"
                    : code == 403 ? "Forbidden" : code == 404 ? "Not Found"
                    : "Internal Server Error";
    size_t len = strlen(body);
    int n = snprintf(head, sizeof head,
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
        "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
        code, msg, ctype, len);
    send_all(fd, head, (size_t)n);
    send_all(fd, body, len);
}

static const item_t *find_item(const char *id) {
    for (size_t i = 0; i < N_ITEMS; i++)
        if (!strcmp(ITEMS[i].id, id)) return &ITEMS[i];
    return NULL;
}

static void state_json(char *out, size_t cap) {
    sqlite3 *db = appdb_open();
    size_t off = 0;
    off += (size_t)snprintf(out + off, cap - off, "{\"items\":[");
    for (size_t i = 0; i < N_ITEMS; i++) {
        int total = 0, vis = 0;
        if (db) appdb_count(db, ITEMS[i].title_id, &total, &vis);
        off += (size_t)snprintf(out + off, cap - off,
            "%s{\"id\":\"%s\",\"title_id\":\"%s\",\"label\":\"%s\",\"found\":%s,\"visible\":%s}",
            i ? "," : "", ITEMS[i].id, ITEMS[i].title_id, ITEMS[i].label,
            total > 0 ? "true" : "false", vis > 0 ? "true" : "false");
    }
    off += (size_t)snprintf(out + off, cap - off, "],\"pins\":[");
    for (size_t i = 0; i < N_PINS; i++) {
        int pinned = 0;
        int found = db ? appdb_system_state(db, PINS[i].title_id, &pinned) : 0;
        off += (size_t)snprintf(out + off, cap - off,
            "%s{\"id\":\"%s\",\"label\":\"%s\",\"found\":%s,\"pinned\":%s}",
            i ? "," : "", PINS[i].id, PINS[i].label,
            found ? "true" : "false", pinned ? "true" : "false");
    }
    off += (size_t)snprintf(out + off, cap - off, "]");
    snprintf(out + off, cap - off, "}");
    if (db) sqlite3_close(db);
}

static int set_item(const item_t *it, int value) {
    sqlite3 *db = appdb_open();
    if (!db) return -1;
    int total = 0, vis = 0;
    if (appdb_count(db, it->title_id, &total, &vis) != 0 || total == 0) {
        sqlite3_close(db);
        return -1;
    }
    if (!g_backed_up) {
        if (appdb_backup() != 0) { sqlite3_close(db); return -1; }
        g_backed_up = 1;
    }
    int changed = appdb_set_visible(db, it->title_id, value);
    sqlite3_close(db);
    if (changed >= 0)
        appdb_say("%s: %s (%d row(s)). Reboot to apply.", it->label,
                  value ? "SHOWN" : "HIDDEN", changed);
    return changed;
}

static const item_t *find_pin(const char *id) {
    for (size_t i = 0; i < N_PINS; i++)
        if (!strcmp(PINS[i].id, id)) return &PINS[i];
    return NULL;
}

static int pin_item(const item_t *it, int on) {
    sqlite3 *db = appdb_open();
    if (!db) return -1;
    int pinned = 0;
    if (!appdb_system_state(db, it->title_id, &pinned)) { sqlite3_close(db); return -1; }
    if (!g_backed_up) {
        if (appdb_backup() != 0) { sqlite3_close(db); return -1; }
        g_backed_up = 1;
    }
    g_watch[it - PINS] = on;
    int changed = appdb_set_system_pin(db, it->title_id, on);
    if (changed >= 0 && appdb_set_lock(db, it->title_id, on) != 0) changed = -1;
    sqlite3_close(db);
    if (changed >= 0)
        appdb_say("%s: %s (%d row(s)). Reboot to apply.", it->label,
                  on ? "PINNED + LOCKED (system group, index 1e9)" : "UNPINNED (games row)", changed);
    return changed;
}

/* Value of query parameter `key` in `query` into out (empty if absent). */
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

static int has_menu_header(const char *req) {
    for (const char *p = req; (p = strchr(p, '\n')); ) {
        p++;
        if (!strncasecmp(p, "X-Menu:", 7)) return 1;
        if (*p == '\r' || *p == '\n') break;
    }
    return 0;
}

static void handle(int fd) {
    char req[4096];
    size_t len = 0;
    while (len < sizeof req - 1) {
        ssize_t n = recv(fd, req + len, sizeof req - 1 - len, 0);
        if (n <= 0) break;
        len += (size_t)n;
        req[len] = 0;
        if (strstr(req, "\r\n\r\n")) break;
    }
    req[len] = 0;

    char method[8] = {0}, target[256] = {0};
    if (sscanf(req, "%7s %255s", method, target) != 2) {
        reply(fd, 400, "text/plain", "bad request");
        return;
    }
    char *query = strchr(target, '?');
    if (query) *query++ = 0;

    if (!strcmp(method, "GET") && !strcmp(target, "/")) {
        reply(fd, 200, "text/html; charset=utf-8", PAGE);
    } else if (!strcmp(method, "GET") && !strcmp(target, "/api/state")) {
        char json[4096];
        state_json(json, sizeof json);
        reply(fd, 200, "application/json", json);
    } else if (!strcmp(method, "POST")) {
        /* The custom header blocks cross-site form posts from other web pages. */
        if (!has_menu_header(req)) {
            reply(fd, 403, "text/plain", "forbidden");
        } else if (!strcmp(target, "/api/set")) {
            char id[16], v[4];
            query_get(query ? query : "", "id", id, sizeof id);
            query_get(query ? query : "", "v", v, sizeof v);
            const item_t *it = find_item(id);
            if (!it || (strcmp(v, "0") && strcmp(v, "1")))
                reply(fd, 400, "text/plain", "bad params");
            else if (set_item(it, v[0] == '1') < 0)
                reply(fd, 500, "text/plain", "failed");
            else
                reply(fd, 200, "application/json", "{\"ok\":true}");
        } else if (!strcmp(target, "/api/pin")) {
            char id[16], v[4];
            query_get(query ? query : "", "id", id, sizeof id);
            query_get(query ? query : "", "v", v, sizeof v);
            const item_t *it = find_pin(id);
            if (!it || (strcmp(v, "0") && strcmp(v, "1")))
                reply(fd, 400, "text/plain", "bad params");
            else if (pin_item(it, v[0] == '1') < 0) reply(fd, 500, "text/plain", "failed");
            else reply(fd, 200, "application/json", "{\"ok\":true}");
        } else if (!strcmp(target, "/api/quit")) {
            reply(fd, 200, "application/json", "{\"ok\":true}");
            g_quit = 1;
        } else {
            reply(fd, 404, "text/plain", "not found");
        }
    } else {
        reply(fd, 404, "text/plain", "not found");
    }
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

/* The console rewrites a tile's lastAccessIndex when that app is launched, which drops
 * the lock. While the menu runs, re-apply it as soon as it is lost. */
static void watch_tick(void) {
    for (size_t i = 0; i < N_PINS; i++) {
        if (!g_watch[i]) continue;
        sqlite3 *db = appdb_open();
        if (!db) continue;
        int pinned = 0;
        if (appdb_system_state(db, PINS[i].title_id, &pinned) && !pinned) {
            if (!g_backed_up && appdb_backup() == 0) g_backed_up = 1;
            if (g_backed_up && appdb_set_system_pin(db, PINS[i].title_id, 1) >= 0
                && appdb_set_lock(db, PINS[i].title_id, 1) == 0)
                appdb_say("%s: lock re-applied.", PINS[i].label);
        }
        sqlite3_close(db);
    }
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    {   /* pins already active at start are kept locked */
        sqlite3 *db = appdb_open();
        if (db) {
            for (size_t i = 0; i < N_PINS; i++) {
                int pinned = 0;
                if (appdb_system_state(db, PINS[i].title_id, &pinned) && pinned) g_watch[i] = 1;
            }
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
    addr.sin_port = htons(MENU_PORT);
    if (bind(srv, (struct sockaddr *)&addr, sizeof addr) != 0 || listen(srv, 4) != 0) {
        appdb_say("Port %d unavailable (menu already running?): %s", MENU_PORT, strerror(errno));
        close(srv);
        return 1;
    }

    char ip[64];
    local_ip(ip, sizeof ip);
    appdb_say("Menu: http://%s:%d/", ip, MENU_PORT);

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
    appdb_say("Menu stopped.");
    return 0;
}
