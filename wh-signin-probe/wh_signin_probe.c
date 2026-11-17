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
 * v3 tried kernel_dynlib_getlist() to enumerate every loaded module by handle. It
 * doesn't exist on this SDK: the real <ps5/kernel.h> (checked against
 * ps5-payload-dev/sdk's own source after the build error) only offers
 * kernel_dynlib_handle(pid, name, &handle) -- resolve-by-name, nothing that lists
 * modules without already knowing one's filename. So there is no way to enumerate
 * SceShellUI's modules through this SDK; guessing .sprx filenames to feed it would
 * be exactly the kind of blind guess this probe exists to avoid.
 *
 * v4 (this one) sidesteps needing a module name at all: it scans every readable
 * mapped region of SceShellUI (found via sysctl KERN_PROC_VMMAP, same as v2) for
 * the literal ASCII strings "VshNMPsnAccountModule" and "IsSignedIn" using
 * pt_copyout, exactly like vda_probe.c's read_module()/scan_pad_module() already
 * scan a named module for a byte pattern -- just across every mapped region
 * instead of one known base address, and a string instead of opcodes. A failed
 * pt_copyout page is skipped (never crashes anything, same tolerance vda_probe.c
 * already relies on); huge regions (heaps, framebuffers) are capped per-region so
 * this finishes in reasonable time. A hit's containing region and address is
 * exactly the fact needed to scope the real patch target next.
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

#define LOG_PATH      "/data/wh_signin_probe.log"
#define TARGET_PROC   "SceShellUI"
#define PER_REGION_CAP (4 * 1024 * 1024)  /* don't scan more than 4MB of any one mapping */
#define SCAN_CHUNK     0x1000

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

/* Finds every occurrence of `needle` in buf[0..len), reporting offsets. */
static void scan_for(const uint8_t *buf, size_t len, const char *needle,
                      intptr_t region_base, const char *region_path, int *hits) {
    size_t nlen = strlen(needle);
    if (nlen == 0 || len < nlen) return;
    for (size_t off = 0; off + nlen <= len; off++) {
        if (memcmp(buf + off, needle, nlen) == 0) {
            (*hits)++;
            say("  MATCH \"%s\" at %p (region %s, +0x%zx)",
                needle, (void *)(region_base + (intptr_t)off),
                region_path[0] ? region_path : "(anonymous)", off);
        }
    }
}

int main(void) {
    g_log = fopen(LOG_PATH, "w");
    say("=== wh_signin_probe v4: read-only, changes nothing ===");
    say("Scanning %s's mapped memory for the literal strings", TARGET_PROC);
    say("\"VshNMPsnAccountModule\" and \"IsSignedIn\" -- no module name needed.");

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

    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_VMMAP, pid};
    size_t len = 0;
    if (sysctl(mib, 4, NULL, &len, NULL, 0) != 0 || len == 0) {
        say("sysctl(KERN_PROC_VMMAP) size query failed");
        pt_detach(pid, 0);
        if (g_log) fclose(g_log);
        return 1;
    }
    len += len / 4 + 4096;

    void *mapbuf = malloc(len);
    if (!mapbuf || sysctl(mib, 4, mapbuf, &len, NULL, 0) != 0) {
        say("sysctl(KERN_PROC_VMMAP) fetch failed");
        free(mapbuf);
        pt_detach(pid, 0);
        if (g_log) fclose(g_log);
        return 1;
    }

    uint8_t *scratch = malloc(PER_REGION_CAP);
    int hits = 0, regions = 0, regions_scanned = 0;
    if (!scratch) {
        say("malloc(scratch) failed -- aborting");
    } else {
        for (char *p = mapbuf; p < (char *)mapbuf + len;) {
            struct kinfo_vmentry *kv = (struct kinfo_vmentry *)p;
            if (kv->kve_structsize == 0) break;
            regions++;

            size_t rlen = (size_t)(kv->kve_end - kv->kve_start);
            size_t want = rlen < PER_REGION_CAP ? rlen : PER_REGION_CAP;
            if (want >= SCAN_CHUNK) {
                size_t got = 0;
                for (size_t off = 0; off + SCAN_CHUNK <= want; off += SCAN_CHUNK) {
                    if (pt_copyout(pid, (intptr_t)kv->kve_start + (intptr_t)off,
                                   scratch + off, SCAN_CHUNK) == 0)
                        got += SCAN_CHUNK;
                    else
                        memset(scratch + off, 0, SCAN_CHUNK); /* unreadable page, skip safely */
                }
                if (got > 0) {
                    regions_scanned++;
                    scan_for(scratch, want, "VshNMPsnAccountModule",
                             (intptr_t)kv->kve_start, kv->kve_path, &hits);
                    scan_for(scratch, want, "IsSignedIn",
                             (intptr_t)kv->kve_start, kv->kve_path, &hits);
                }
            }
            p += kv->kve_structsize;
        }
        free(scratch);
    }
    say("scanned %d/%d mapped region(s) (up to %d MB each), %d match(es)",
        regions_scanned, regions, PER_REGION_CAP / (1024 * 1024), hits);
    if (!hits) {
        say("no match -- the identifier may live in a region larger than the per-region cap,");
        say("or the JS engine may not keep it as a flat ASCII string in memory at all.");
    }

    free(mapbuf);
    pt_detach(pid, 0);
    say("=== done, detached from %s cleanly ===", TARGET_PROC);
    if (g_log) fclose(g_log);
    return 0;
}
