/*
 * vda_probe.elf - PS5 payload (ps5-payload-sdk)
 *
 * READ-ONLY diagnostic. It does not patch anything, write to memory, or change any
 * setting. It exists to check, on this console's firmware, whether two pieces of
 * information needed to adapt the "Ghostpad" virtual-controller technique (a GPL-3.0
 * project whose source is currently unreachable) still hold:
 *
 *   1. Does libScePad.sprx, as loaded inside SceShellCore, still contain the byte
 *      pattern documented for the VDA (Virtual Device Adapter) dispatch-call code cave:
 *          e8 ?? ?? ?? ?? 48 8b 0b 48 3b 4d f0 75 ??
 *      ('??' = wildcard byte). This scans SceShellCore's mapped copy of libScePad.sprx
 *      and reports every offset (relative to the module's base) where it finds a match,
 *      plus the bytes around each match for manual inspection. It changes nothing.
 *
 *   2. Can two libSceMbus.sprx functions be resolved by plain name inside SceShellCore:
 *      sceMbusDisconnectDevice and sceMbusBindDeviceWithUserId. This only resolves their
 *      addresses (via the SDK's kernel_dynlib_dlsym helper, which hashes the name to a
 *      NID internally) and prints them; it never calls them.
 *
 * It attaches briefly to SceShellCore via ptrace (ptrace helpers are a modified copy of
 * John Tornblom's pt.c/pt.h, GPL-3.0-or-later, already used elsewhere in this repo's
 * pad_rpc tooling) and always detaches again before exiting, even on error.
 *
 * Result: printed to klog (port 3232) and written to /data/vda_probe.log.
 */
#include <sys/types.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/sysctl.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <ps5/klog.h>
#include <ps5/kernel.h>

#include "pt/pt.h"

#define LOG_PATH     "/data/vda_probe.log"
#define TARGET_PROC  "SceShellCore"
#define PAD_MODULE   "libScePad.sprx"
#define MBUS_MODULE  "libSceMbus.sprx"
#define SCAN_MAX     0x400000   /* stop after this many bytes even if pages keep mapping */
#define SCAN_CHUNK   0x1000     /* read one page at a time; stop at the first unmapped one */

static FILE *g_log;

static void say(const char *fmt, ...) {
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    klog_printf("[vda_probe] %s\n", line);
    if (g_log) {
        fprintf(g_log, "%s\n", line);
        fflush(g_log);
    }
}

/* e8 ?? ?? ?? ?? 48 8b 0b 48 3b 4d f0 75 ?? , '?' entries are wildcards. */
static const int PATTERN_LEN = 14;
static const int PATTERN_BYTES[14] = {
    0xe8, -1, -1, -1, -1, 0x48, 0x8b, 0x0b, 0x48, 0x3b, 0x4d, 0xf0, 0x75, -1
};

static int match_at(const uint8_t *buf, size_t len, size_t off) {
    if (off + (size_t)PATTERN_LEN > len) return 0;
    for (int i = 0; i < PATTERN_LEN; i++) {
        if (PATTERN_BYTES[i] >= 0 && buf[off + i] != (uint8_t)PATTERN_BYTES[i])
            return 0;
    }
    return 1;
}

static void hexdump_line(char *out, size_t cap, const uint8_t *p, int n) {
    size_t off = 0;
    for (int i = 0; i < n && off + 3 < cap; i++)
        off += (size_t)snprintf(out + off, cap - off, "%02x ", p[i]);
}

/* Reads up to SCAN_MAX bytes starting at `base` from `pid`'s address space into a
 * malloc'd buffer, stopping at the first page it can't read (unmapped / end of module).
 * Returns the buffer (caller frees) and sets *out_len. Never crashes the target: a
 * failed pt_copyout just ends the scan early. */
static uint8_t *read_module(pid_t pid, intptr_t base, size_t *out_len) {
    uint8_t *buf = malloc(SCAN_MAX);
    *out_len = 0;
    if (!buf) return NULL;
    size_t off = 0;
    while (off + SCAN_CHUNK <= SCAN_MAX) {
        if (pt_copyout(pid, base + (intptr_t)off, buf + off, SCAN_CHUNK) != 0)
            break; /* unmapped page: normal end of the module's mapping */
        off += SCAN_CHUNK;
    }
    *out_len = off;
    return buf;
}

static void scan_pad_module(pid_t pid) {
    uint32_t h_pad = 0;
    if (kernel_dynlib_handle(pid, PAD_MODULE, &h_pad) != 0 || !h_pad) {
        say("%s: not loaded in %s (normal if the shell hasn't touched a controller yet)",
            PAD_MODULE, TARGET_PROC);
        return;
    }
    intptr_t base = kernel_dynlib_mapbase_addr(pid, h_pad);
    if (!base) {
        say("%s: handle=0x%x but could not get its mapped base address", PAD_MODULE, h_pad);
        return;
    }
    say("%s: handle=0x%x base=%p -- scanning up to %d MB for the VDA signature...",
        PAD_MODULE, h_pad, (void *)base, SCAN_MAX / (1024 * 1024));

    size_t len = 0;
    uint8_t *buf = read_module(pid, base, &len);
    if (!buf) {
        say("  malloc failed, skipping scan");
        return;
    }
    say("  read %zu bytes (0x%zx) before hitting an unmapped page", len, len);

    int hits = 0;
    for (size_t off = 0; off + (size_t)PATTERN_LEN <= len; off++) {
        if (match_at(buf, len, off)) {
            hits++;
            char ctx[64];
            size_t show_from = off >= 4 ? off - 4 : 0;
            hexdump_line(ctx, sizeof ctx, buf + show_from, PATTERN_LEN + 8);
            say("  MATCH #%d at module offset 0x%zx (addr %p): %s",
                hits, off, (void *)(base + (intptr_t)off), ctx);
        }
    }
    if (!hits)
        say("  no match found in the %zu bytes scanned -- signature likely needs updating for this firmware", len);
    else
        say("  %d match(es) found -- signature still applies to this firmware's %s", hits, PAD_MODULE);

    free(buf);
}

static void resolve_mbus(pid_t pid) {
    uint32_t h_mbus = 0;
    if (kernel_dynlib_handle(pid, MBUS_MODULE, &h_mbus) != 0 || !h_mbus) {
        say("%s: not loaded in %s", MBUS_MODULE, TARGET_PROC);
        return;
    }
    say("%s: handle=0x%x", MBUS_MODULE, h_mbus);

    static const char *const names[] = {
        "sceMbusDisconnectDevice",
        "sceMbusBindDeviceWithUserId",
    };
    for (size_t i = 0; i < sizeof names / sizeof *names; i++) {
        intptr_t addr = kernel_dynlib_dlsym(pid, h_mbus, names[i]);
        say("  %-32s -> %s", names[i], addr ? "resolved" : "NOT FOUND");
        if (addr) say("    address = %p (not called, just resolved)", (void *)addr);
    }
}

static pid_t find_pid(const char *want_name) {
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
    size_t sz = 0;
    if (sysctl(mib, 4, NULL, &sz, NULL, 0)) return -1;
    void *buf = malloc(sz);
    if (!buf || sysctl(mib, 4, buf, &sz, NULL, 0)) { free(buf); return -1; }

    pid_t found = -1;
    for (void *p = buf; p < buf + sz;) {
        struct kinfo_proc *ki = (struct kinfo_proc *)p;
        p += ki->ki_structsize;
        if (!strcmp(ki->ki_comm, want_name)) { found = ki->ki_pid; break; }
    }
    free(buf);
    return found;
}

int main(void) {
    g_log = fopen(LOG_PATH, "w");
    say("=== vda_probe v1: read-only check (no patch, no write) ===");

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

    scan_pad_module(pid);
    resolve_mbus(pid);

    pt_detach(pid, 0);
    say("=== done, detached from %s cleanly ===", TARGET_PROC);
    if (g_log) fclose(g_log);
    return 0;
}
