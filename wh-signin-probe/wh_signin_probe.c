/*
 * wh_signin_probe.elf - PS5 payload (ps5-payload-sdk)
 *
 * READ-ONLY diagnostic. It does not patch anything, write to memory, or change any
 * setting. It exists to answer what blocks moving hub-signin-patch out of a NiDUS
 * widget and into dashboard.elf itself (so the fix works even when NiDUS is not
 * installed): which loaded module implements VshNMPsnAccountModule's native side.
 *
 * v1 found no separate per-app process for the Welcome Hub (NPXS40063). v2's VMMAP
 * scan found no .sprx in any candidate process (KERN_PROC_VMMAP only shows
 * file-backed mappings -- fonts, databases -- not the kernel's own module loader).
 *
 * In between, cross-checking wh.log/wh-inspector.log from an older NiDUS install
 * (where "[installer] pid=95"/"pid=58" logged NiDUS deploying its widgets, including
 * the "[hub]"-tagged NativeModules enumeration that lists VshNMPsnAccountModule)
 * against a process list taken at the same time pinned that pid to SceShellUI, in
 * two separate boots. So this is no longer a guess: SceShellUI is where the Hub's
 * (and NiDUS's) JS -- and the NativeModules bridge it calls into -- actually runs.
 *
 * v3 (this one) tries to list EVERY module loaded in SceShellUI, via
 * kernel_dynlib_getlist() (ps5-payload-sdk's <ps5/kernel.h>, same header vda_probe.c
 * already uses successfully for kernel_dynlib_handle/kernel_dynlib_mapbase_addr/
 * kernel_dynlib_dlsym) -- so instead of guessing a module name to query, it should
 * return every handle already loaded and let kernel_dynlib_mapbase_addr + a name
 * lookup identify each one. The exact function name/signature is not yet proven to
 * compile against this SDK version (unlike the other three dynlib_* calls, which
 * vda_probe.c already exercises) -- if `make` reports it missing or a different
 * signature, that is a cheap, safe failure (a build error, same as the earlier
 * missing stdarg.h, not anything that touches the console) to fix from the real
 * compiler message, not a guess to avoid making.
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
#include <ps5/kernel.h>

#include "pt/pt.h"

#define LOG_PATH     "/data/wh_signin_probe.log"
#define TARGET_PROC  "SceShellUI"
#define MAX_HANDLES  256

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

int main(void) {
    g_log = fopen(LOG_PATH, "w");
    say("=== wh_signin_probe v3: read-only, changes nothing ===");
    say("Target: every module loaded in %s (confirmed by wh.log/wh-inspector.log", TARGET_PROC);
    say("cross-reference as where the Hub's NativeModules bridge, incl.");
    say("VshNMPsnAccountModule, actually runs).");

    pid_t pid = find_pid(TARGET_PROC);
    if (pid < 0) {
        say("could not find process '%s' -- aborting", TARGET_PROC);
        if (g_log) fclose(g_log);
        return 1;
    }
    say("target: %s pid=%d", TARGET_PROC, pid);

    if (pt_attach(pid)) {
        say("pt_attach failed (errno=%d) -- aborting, nothing was touched", errno);
        if (g_log) fclose(g_log);
        return 1;
    }

    uint32_t handles[MAX_HANDLES];
    size_t n = 0;
    int rc = kernel_dynlib_getlist(pid, handles, MAX_HANDLES, &n);
    if (rc != 0) {
        say("kernel_dynlib_getlist failed (rc=%d) -- this call may not exist or have a", rc);
        say("different signature on this SDK version; needs fixing from the build error,");
        say("not guessed further here.");
    } else {
        say("%zu module(s) loaded in %s:", n, TARGET_PROC);
        for (size_t i = 0; i < n; i++) {
            char name[256] = {0};
            int nrc = kernel_dynlib_name(pid, handles[i], name, sizeof name);
            intptr_t base = kernel_dynlib_mapbase_addr(pid, handles[i]);
            if (nrc == 0 && name[0])
                say("  handle=0x%-6x base=%p  %s", handles[i], (void *)base, name);
            else
                say("  handle=0x%-6x base=%p  (name lookup failed, rc=%d)", handles[i], (void *)base, nrc);
        }
    }

    pt_detach(pid, 0);
    say("=== done, detached from %s cleanly ===", TARGET_PROC);
    if (g_log) fclose(g_log);
    return 0;
}
