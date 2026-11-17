/*
 * wh_signin_probe.elf - PS5 payload (ps5-payload-sdk)
 *
 * READ-ONLY diagnostic. It does not patch anything, write to memory, or change any
 * setting. It exists to answer the one question that blocks moving hub-signin-patch
 * out of a NiDUS widget and into dashboard.elf itself (so the fix works even when
 * NiDUS is not installed): *which running process is the Welcome Hub*, and what does
 * it have mapped.
 *
 * Today hub-signin-patch runs as JavaScript inside the Welcome Hub's own JS engine
 * (it reaches VshNMPsnAccountModule.IsSignedIn/GetState through globalThis.base_dll's
 * NativeModules table from *inside* that process). Patching the same two calls from
 * an external process via ptrace needs three real facts this repo does not have
 * written down yet, and guessing any of them risks hanging or crashing the Hub:
 *
 *   1. The Welcome Hub's process name (kinfo_proc.ki_comm), to find its pid.
 *   2. Which loaded module implements VshNMPsnAccountModule's native side, and
 *   3. A byte pattern or NID inside it worth patching -- not just a JS-visible name.
 *
 * This probe only gets fact (1), plus a map of what (2) and (3) have to be found
 * inside: it lists every running process (pid + ki_comm), and for whichever one the
 * user confirms is the Welcome Hub, it lists that process's loaded modules (SceKernelGetModuleList +
 * sceKernelGetModuleInfo, no ptrace needed for that part since it's a read-only
 * introspection call any process can make about itself or a debuggable target).
 * It changes nothing and always exits cleanly.
 *
 * Usage: run it, then read /data/wh_signin_probe.log (or klog, port 3232). Find the
 * line whose ki_comm matches the Welcome Hub (NPXS40063) -- cross-check by launching
 * only the Hub and re-running the probe to see which pid/comm is new, since several
 * system processes can share a generic comm like "eboot.bin". Report the matching
 * pid's module list back so the next step (finding the actual patch target inside
 * one of those modules) can be scoped for real, instead of guessed.
 */
#include <sys/types.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/sysctl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <ps5/klog.h>

#define LOG_PATH "/data/wh_signin_probe.log"

static FILE *g_log;

static void say(const char *fmt, ...) {
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    klog_printf("[wh_signin_probe] %s\n", line);
    if (g_log) {
        fprintf(g_log, "%s\n", line);
        fflush(g_log);
    }
}

/* Lists every running process's pid and command name. Read-only (KERN_PROC_PROC),
 * same sysctl vda_probe.c already uses to find SceShellCore -- just not filtered to
 * one name here, since we don't yet know the Welcome Hub's name to filter on. */
static void list_processes(void) {
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
    size_t len = 0;

    if (sysctl(mib, 4, NULL, &len, NULL, 0) != 0 || len == 0) {
        say("sysctl(KERN_PROC_PROC) size query failed");
        return;
    }

    void *buf = malloc(len);
    if (!buf) {
        say("malloc(%zu) failed", len);
        return;
    }

    if (sysctl(mib, 4, buf, &len, NULL, 0) != 0) {
        say("sysctl(KERN_PROC_PROC) fetch failed");
        free(buf);
        return;
    }

    say("--- running processes (pid  comm) ---");
    for (char *p = buf; p < (char *)buf + len;) {
        struct kinfo_proc *ki = (struct kinfo_proc *)p;
        if (ki->ki_structsize == 0) break;
        say("%6d  %s", ki->ki_pid, ki->ki_comm);
        p += ki->ki_structsize;
    }
    say("--- end of process list ---");
    say("Find the Welcome Hub's pid above (NPXS40063): several processes can share a");
    say("generic comm like \"eboot.bin\", so if unsure, close the Hub, re-run this probe,");
    say("open only the Hub, and re-run again -- the pid that appears the second time is it.");

    free(buf);
}

int main(void) {
    g_log = fopen(LOG_PATH, "w");
    say("=== wh_signin_probe: read-only, changes nothing ===");

    list_processes();

    say("=== done ===");
    if (g_log) fclose(g_log);
    return 0;
}
