/*
 * pad_probe.elf - PS5 payload (ps5-payload-sdk)
 *
 * Diagnostic only: finds out whether a payload can read the DualSense controllers through
 * libScePad / libSceUserService (the SDK ships stubs for both), and dumps the raw data so the
 * battery level can be located by comparing with the controller's real charge.
 * Everything runs in a child process: a crash cannot take the console payload loader down.
 * Result: printed and written to /data/pad_probe.log
 *
 * Usage: launch it while at least one controller is connected, note the battery % shown on
 * the console (PS button menu), send /data/pad_probe.log.
 */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int sceUserServiceInitialize(const void *params);
int sceUserServiceGetInitialUser(int *user_id);
int sceUserServiceGetLoginUserIdList(int ids[4]);
int scePadInit(void);
int scePadGetHandle(int user_id, int type, int index);
int scePadOpen(int user_id, int type, int index, const void *param);
int scePadReadState(int handle, void *data);
int scePadGetControllerInformation(int handle, void *info);

static FILE *g_log;

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#include <stdarg.h>
static void say(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    printf("[pad] %s\n", buf);
    fflush(stdout);
    if (g_log) { fprintf(g_log, "%s\n", buf); fflush(g_log); }
}

static void hexdump(const char *tag, const unsigned char *p, size_t n) {
    char line[128];
    for (size_t i = 0; i < n; i += 16) {
        size_t o = (size_t)snprintf(line, sizeof line, "%s +%03zx:", tag, i);
        for (size_t j = i; j < i + 16 && j < n; j++)
            o += (size_t)snprintf(line + o, sizeof line - o, " %02x", p[j]);
        say("%s", line);
    }
}

static void probe(void) {
    int r = sceUserServiceInitialize(NULL);
    say("sceUserServiceInitialize: 0x%x", (unsigned)r);
    int ids[4] = {-1, -1, -1, -1};
    r = sceUserServiceGetLoginUserIdList(ids);
    say("GetLoginUserIdList: 0x%x ids=%d,%d,%d,%d", (unsigned)r, ids[0], ids[1], ids[2], ids[3]);
    int init = -1;
    r = sceUserServiceGetInitialUser(&init);
    say("GetInitialUser: 0x%x user=%d", (unsigned)r, init);
    r = scePadInit();
    say("scePadInit: 0x%x", (unsigned)r);

    int users[5] = {ids[0], ids[1], ids[2], ids[3], init};
    for (int u = 0; u < 5; u++) {
        if (users[u] <= 0) continue;
        for (int idx = 0; idx < 2; idx++) {
            int h = scePadGetHandle(users[u], 0, idx);
            say("user %d index %d: scePadGetHandle=0x%x", users[u], idx, (unsigned)h);
            if (h < 0) {
                h = scePadOpen(users[u], 0, idx, NULL);
                say("user %d index %d: scePadOpen=0x%x", users[u], idx, (unsigned)h);
            }
            if (h < 0) continue;
            static unsigned char info[256], data[512];
            memset(info, 0, sizeof info);
            memset(data, 0, sizeof data);
            r = scePadGetControllerInformation(h, info);
            say("GetControllerInformation: 0x%x", (unsigned)r);
            hexdump("info", info, 64);
            r = scePadReadState(h, data);
            say("ReadState: 0x%x", (unsigned)r);
            hexdump("data", data, 160);
        }
    }
    say("end");
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    g_log = fopen("/data/pad_probe.log", "w");
    pid_t pid = fork();
    if (pid == 0) { probe(); _exit(0); }
    int st = 0;
    for (int i = 0; i < 100; i++) {          /* 10 s max */
        if (waitpid(pid, &st, WNOHANG) == pid) {
            if (WIFSIGNALED(st)) say("CRASH signal %d", WTERMSIG(st));
            if (g_log) fclose(g_log);
            return 0;
        }
        usleep(100000);
    }
    kill(pid, SIGKILL);
    waitpid(pid, &st, 0);
    say("TIMEOUT (blocked)");
    if (g_log) fclose(g_log);
    return 0;
}
