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
 *   3. If most of libScePad.sprx came back unreadable in (1): whether the bytes can
 *      instead be read by making SceShellCore itself call memcpy() into a scratch
 *      buffer we allocate (see probe_indirect_read()). This still never writes to any
 *      file or system memory; it uses a bounded single-step loop (call_bounded()) so
 *      that if the call hits a genuinely protected page, we abort and restore
 *      SceShellCore's exact prior register state instead of letting anything crash.
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
#include <sys/mman.h>
#include <machine/reg.h>
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
 * malloc'd buffer. A failed pt_copyout page is zero-filled (zeros can't match the
 * signature, whose first byte is 0xe8) and never crashes the target -- it just skips
 * that page. Unlike the previous version, this one does NOT bail out after a run of
 * unreadable pages: it always scans the full SCAN_MAX window, because a readable
 * segment (.rodata/.data) can follow a long unreadable one (.text) further in, and
 * stopping early would miss it. It logs each contiguous readable/unreadable run so the
 * memory layout is visible (e.g. "ok 0x0-0x18000, fail 0x18000-0x73000, ok 0x73000-..."). */
static uint8_t *read_module(pid_t pid, intptr_t base, size_t *out_len) {
    uint8_t *buf = calloc(1, SCAN_MAX);
    *out_len = SCAN_MAX;
    if (!buf) return NULL;

    int run_ok = -1;    /* -1 = no run started yet; else 0/1 for the kind of current run */
    size_t run_start = 0;
    size_t total_ok = 0, total_fail = 0;

    for (size_t off = 0; off + SCAN_CHUNK <= SCAN_MAX; off += SCAN_CHUNK) {
        int ok = (pt_copyout(pid, base + (intptr_t)off, buf + off, SCAN_CHUNK) == 0);
        if (ok) total_ok++; else total_fail++;
        if (run_ok == -1) { run_ok = ok; run_start = off; }
        else if (ok != run_ok) {
            say("  %s 0x%zx-0x%zx", run_ok ? "ok  " : "fail", run_start, off);
            run_ok = ok; run_start = off;
        }
    }
    if (run_ok != -1)
        say("  %s 0x%zx-0x%zx", run_ok ? "ok  " : "fail", run_start, (size_t)SCAN_MAX);
    say("  totals: %zu page(s) ok, %zu page(s) unreadable, out of %zu scanned",
        total_ok, total_fail, total_ok + total_fail);
    return buf;
}

/* Returns the module's base address (so the caller can reuse it), or 0 if it
 * couldn't be found/mapped. */
static intptr_t scan_pad_module(pid_t pid) {
    uint32_t h_pad = 0;
    if (kernel_dynlib_handle(pid, PAD_MODULE, &h_pad) != 0 || !h_pad) {
        say("%s: not loaded in %s (normal if the shell hasn't touched a controller yet)",
            PAD_MODULE, TARGET_PROC);
        return 0;
    }
    intptr_t base = kernel_dynlib_mapbase_addr(pid, h_pad);
    if (!base) {
        say("%s: handle=0x%x but could not get its mapped base address", PAD_MODULE, h_pad);
        return 0;
    }
    say("%s: handle=0x%x base=%p -- scanning up to %d MB for the VDA signature...",
        PAD_MODULE, h_pad, (void *)base, SCAN_MAX / (1024 * 1024));

    size_t len = 0;
    uint8_t *buf = read_module(pid, base, &len);
    if (!buf) {
        say("  malloc failed, skipping scan");
        return base;
    }

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
    return base;
}

/* --- indirect-read test ---------------------------------------------------
 * Large parts of libScePad.sprx's mapping could not be read with a plain
 * pt_copyout (see the scan above). This checks whether the same bytes can be
 * read by instead making the *target process itself* call memcpy() to copy
 * them into memory we control -- the technique Ghostpad's "hidDumper" tool
 * used after finding that direct copyin/copyout into SceShellCore fails.
 *
 * Safety: the call is made through call_bounded() below, never the library's
 * own pt_call(). If the injected call doesn't return within CALL_MAX_STEPS
 * single-steps -- which is what happens if it faults trying to read a
 * genuinely protected page, since pt_step() always passes signal 0 and so
 * never lets that fault actually reach the process -- we give up, restore
 * the exact register state SceShellCore had before we touched it, and carry
 * on. Nothing is ever patched or written back into the target; the only
 * memory we write to is a scratch page we ourselves just allocated.
 */
#define CALL_MAX_STEPS 200000

/* Like pt_call() in pt.c, but bounded. Returns 0 if the call returned
 * normally (*out_rax holds its return value), 1 if it did not return within
 * CALL_MAX_STEPS steps (treated as "aborted, assume it's stuck/faulting"),
 * or -1 if a ptrace operation itself failed. Always restores the pre-call
 * registers before returning, in every case. */
static int call_bounded(pid_t pid, intptr_t addr, uint64_t a0, uint64_t a1,
                         uint64_t a2, long *out_rax) {
    struct reg bak_reg, cur_reg;

    if (pt_getregs(pid, &bak_reg)) return -1;

    cur_reg = bak_reg;
    cur_reg.r_rip = addr;
    cur_reg.r_rdi = a0;
    cur_reg.r_rsi = a1;
    cur_reg.r_rdx = a2;

    if (pt_setregs(pid, &cur_reg)) return -1;

    int ret = 1; /* assume stuck/faulting unless proven otherwise */
    for (long i = 0; i < CALL_MAX_STEPS; i++) {
        if (pt_step(pid)) { ret = -1; break; }
        if (pt_getregs(pid, &cur_reg)) { ret = -1; break; }
        if ((uint64_t)cur_reg.r_rsp > (uint64_t)bak_reg.r_rsp) {
            if (out_rax) *out_rax = cur_reg.r_rax;
            ret = 0;
            break;
        }
    }

    /* Whatever happened, put the thread back exactly how we found it. */
    pt_setregs(pid, &bak_reg);
    return ret;
}

static intptr_t find_memcpy(pid_t pid, char *found_module, size_t cap) {
    static const char *const candidates[] = {
        "libkernel.sprx", "libSceLibcInternal.sprx", "libc.sprx",
    };
    for (size_t i = 0; i < sizeof candidates / sizeof *candidates; i++) {
        uint32_t h = 0;
        if (kernel_dynlib_handle(pid, candidates[i], &h) == 0 && h) {
            intptr_t addr = kernel_dynlib_dlsym(pid, h, "memcpy");
            if (addr) {
                snprintf(found_module, cap, "%s", candidates[i]);
                return addr;
            }
        }
    }
    return 0;
}

static void probe_indirect_read(pid_t pid, intptr_t pad_base) {
    say("--- indirect-read test (memcpy executed inside %s via pt_call) ---", TARGET_PROC);

    char mod[64] = {0};
    intptr_t memcpy_addr = find_memcpy(pid, mod, sizeof mod);
    if (!memcpy_addr) {
        say("  could not resolve memcpy in any known module -- skipping");
        return;
    }
    say("  memcpy found in %s at %p", mod, (void *)memcpy_addr);

    intptr_t scratch = pt_mmap(pid, 0, 0x1000, PROT_READ | PROT_WRITE,
                                MAP_ANON | MAP_PRIVATE, -1, 0);
    if (!scratch || scratch == (intptr_t)-1) {
        say("  pt_mmap failed -- skipping");
        return;
    }
    say("  scratch buffer mapped at %p", (void *)scratch);

    uint8_t local[256];
    long rax = 0;

    /* Control test first: copy from the window the earlier scan already
     * proved readable (pad_base+0x1c000). This validates the whole
     * mechanism (mmap, injected call, bounded single-step loop, copyout)
     * with zero new risk before touching anything unproven. */
    intptr_t good_src = pad_base + 0x1c000;
    int r = call_bounded(pid, memcpy_addr, (uint64_t)scratch, (uint64_t)good_src, sizeof local, &rax);
    if (r == 0 && pt_copyout(pid, scratch, local, sizeof local) == 0) {
        uint8_t direct[256];
        int direct_ok = (pt_copyout(pid, good_src, direct, sizeof direct) == 0);
        int same = direct_ok && memcmp(local, direct, sizeof local) == 0;
        say("  control test (known-readable source): memcpy returned, rax=0x%lx, bytes %s direct read",
            rax, same ? "MATCH" : "differ from");
        if (!same) {
            say("  mechanism not validated -- stopping here rather than trust the next test");
            pt_munmap(pid, scratch, 0x1000);
            return;
        }
    } else {
        say("  control test (known-readable source): %s -- mechanism itself not confirmed, stopping here",
            r == 1 ? "timed out (unexpected on memory we already know is readable)" : "ptrace error");
        pt_munmap(pid, scratch, 0x1000);
        return;
    }

    /* Real test: same call, reading from the very start of the module --
     * the region plain pt_copyout could not read at all in the scan above. */
    intptr_t suspect_src = pad_base;
    r = call_bounded(pid, memcpy_addr, (uint64_t)scratch, (uint64_t)suspect_src, sizeof local, &rax);
    if (r == 1) {
        say("  protected-region test: memcpy did not return within %d single-steps -- "
            "consistent with a read protection on that page that holds even for code "
            "executing inside the process (no crash: the fault was never delivered, "
            "the call was aborted and SceShellCore's registers were restored)",
            CALL_MAX_STEPS);
    } else if (r == -1) {
        say("  protected-region test: a ptrace operation failed mid-call -- inconclusive");
    } else if (pt_copyout(pid, scratch, local, sizeof local) == 0) {
        char ctx[3 * 32 + 1];
        hexdump_line(ctx, sizeof ctx, local, 32);
        say("  protected-region test: memcpy RETURNED (rax=0x%lx) -- first 32 bytes: %s", rax, ctx);
        say("  (non-zero/structured bytes here would mean in-process execution CAN read this "
            "region even though a direct ptrace peek cannot)");
    } else {
        say("  protected-region test: memcpy returned but the scratch buffer couldn't be read back -- inconclusive");
    }

    pt_munmap(pid, scratch, 0x1000);
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

    intptr_t pad_base = scan_pad_module(pid);
    resolve_mbus(pid);
    if (pad_base) probe_indirect_read(pid, pad_base);

    pt_detach(pid, 0);
    say("=== done, detached from %s cleanly ===", TARGET_PROC);
    if (g_log) fclose(g_log);
    return 0;
}
