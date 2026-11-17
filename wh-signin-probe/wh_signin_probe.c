/*
 * wh_signin_probe.elf - PS5 payload (ps5-payload-sdk)
 *
 * READ-ONLY diagnostic. It does not patch anything, write to memory, or change any
 * setting. It exists to answer what blocks moving hub-signin-patch out of a NiDUS
 * widget and into dashboard.elf itself (so the fix works even when NiDUS is not
 * installed): which running process the Welcome Hub's JS actually executes in, and
 * which loaded module implements VshNMPsnAccountModule's native side.
 *
 * v1 of this probe only listed process names (pid + ki_comm). That run found no
 * separate per-app process for NPXS40063 -- instead, three processes were present
 * whether the Hub was open or closed:
 *
 *     SceNKUIProcess, SceNKNetworkProcess, SceNKWebProcess
 *
 * which looks like a Chromium-style split (UI / network / renderer) for the system's
 * own web engine -- a strong candidate for where the Hub's (and NiDUS's) JS runs,
 * since hub-signin-patch's widget.js reaches VshNMPsnAccountModule through
 * globalThis.base_dll, a JS-engine-level object, not anything app-process-specific.
 *
 * v2 (this one) checks that directly instead of guessing further: for each of those
 * three processes (plus SceShellUI/SceShellCore, the vda_probe.c precedent, for
 * comparison), it lists every DISTINCT file mapped into that process's address space
 * (via sysctl KERN_PROC_VMMAP -- same read-only process-introspection family as
 * KERN_PROC_PROC, just per-process instead of system-wide; no memory is read, only
 * each mapping's file path). Whichever process has a module with "Psn", "Account",
 * "Vsh" or similar in its path is the one worth scanning for the actual patch target
 * next -- instead of assuming it's SceNKWebProcess just because the name sounds right.
 *
 * Briefly ptrace-attaches to each candidate (pt_attach/pt_detach, vendored from John
 * Törnblom's pt.c/pt.h, GPL-3.0-or-later, same copy vda_probe.c already uses) only
 * because VMMAP access has needed that in practice on this SDK's other probes; it
 * always detaches again, even on error, and never reads or writes any memory.
 */
#include <sys/types.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/sysctl.h>
#include <sys/wait.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <ps5/klog.h>

#include "pt/pt.h"

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

/* Read-only (KERN_PROC_PROC), same sysctl vda_probe.c uses to find SceShellCore. */
static pid_t find_pid(const char *want_name) {
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
    size_t len = 0;
    pid_t found = -1;

    if (sysctl(mib, 4, NULL, &len, NULL, 0) != 0 || len == 0) return -1;
    void *buf = malloc(len);
    if (!buf) return -1;
    if (sysctl(mib, 4, buf, &len, NULL, 0) != 0) { free(buf); return -1; }

    for (char *p = buf; p < (char *)buf + len;) {
        struct kinfo_proc *ki = (struct kinfo_proc *)p;
        if (ki->ki_structsize == 0) break;
        if (!strcmp(ki->ki_comm, want_name)) { found = ki->ki_pid; break; }
        p += ki->ki_structsize;
    }
    free(buf);
    return found;
}

/* Lists every DISTINCT file path mapped into `pid`'s address space. Read-only:
 * only the mapping list is fetched (KERN_PROC_VMMAP), no bytes of the target's
 * memory are ever read. Briefly ptrace-attaches first since that has been needed
 * in practice for this SDK's other per-process introspection; always detaches,
 * even if the sysctl itself fails. */
static void list_mapped_files(const char *name) {
    pid_t pid = find_pid(name);
    if (pid < 0) {
        say("%-20s not running", name);
        return;
    }

    if (pt_attach(pid) != 0)
        say("%-20s pid=%-6d could not ptrace-attach -- listing anyway", name, pid);

    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_VMMAP, pid};
    size_t len = 0;
    if (sysctl(mib, 4, NULL, &len, NULL, 0) != 0 || len == 0) {
        say("%-20s pid=%-6d sysctl(KERN_PROC_VMMAP) size query failed", name, pid);
        pt_detach(pid, 0);
        return;
    }
    len += len / 4 + 4096; /* the map can grow between the size query and the fetch */

    void *buf = malloc(len);
    if (!buf) {
        say("%-20s pid=%-6d malloc(%zu) failed", name, pid, len);
        pt_detach(pid, 0);
        return;
    }

    if (sysctl(mib, 4, buf, &len, NULL, 0) != 0) {
        say("%-20s pid=%-6d sysctl(KERN_PROC_VMMAP) fetch failed", name, pid);
        free(buf);
        pt_detach(pid, 0);
        return;
    }

    say("%-20s pid=%-6d -- distinct mapped files:", name, pid);
    char last[PATH_MAX] = {0};
    int shown = 0;
    for (char *p = buf; p < (char *)buf + len;) {
        struct kinfo_vmentry *kv = (struct kinfo_vmentry *)p;
        if (kv->kve_structsize == 0) break;
        if (kv->kve_path[0] && strcmp(kv->kve_path, last)) {
            say("    %s", kv->kve_path);
            snprintf(last, sizeof last, "%s", kv->kve_path);
            shown++;
        }
        p += kv->kve_structsize;
    }
    if (!shown) say("    (no named/file-backed mappings found)");

    free(buf);
    pt_detach(pid, 0);
}

int main(void) {
    g_log = fopen(LOG_PATH, "w");
    say("=== wh_signin_probe v2: read-only, changes nothing ===");
    say("Looking for a module with \"Psn\", \"Account\", \"Vsh\" or similar in its path --");
    say("that's the one implementing VshNMPsnAccountModule's native side.");

    static const char *const candidates[] = {
        "SceNKWebProcess", "SceNKUIProcess", "SceNKNetworkProcess",
        "SceShellUI", "SceShellCore",
    };
    for (size_t i = 0; i < sizeof candidates / sizeof *candidates; i++)
        list_mapped_files(candidates[i]);

    say("=== done ===");
    if (g_log) fclose(g_log);
    return 0;
}
