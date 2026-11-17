/* Copyright (C) 2024 John Törnblom

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING. If not, see
<http://www.gnu.org/licenses/>.  */

#include "../../src/vshsig_log.h"

#ifdef VSHSIG_PT_HOST_TEST
#include "../../diagnostics/test_pt_platform.h"
#else
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include <sys/ptrace.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <sys/mman.h>

#include <ps5/kernel.h>
#include <ps5/klog.h>

#include "pt.h"
#endif

static int64_t
pt_now_ms(void) {
  struct timespec ts;
  if(clock_gettime(CLOCK_MONOTONIC, &ts)) {
    return -1;
  }
  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* A traced UI must not be held indefinitely by waitpid or a helper loop. */
static int
pt_wait_stop(pid_t pid, int *status) {
  int64_t start = pt_now_ms();
  if(start < 0) {
    return -1;
  }
  for(;;) {
    pid_t result = waitpid(pid, status, WNOHANG);
    if(result == pid) {
      if(WIFSTOPPED(*status)) {
        return 0;
      }
      errno = ESRCH;
      return -1;
    }
    if(result < 0 && errno != EINTR) {
      return -1;
    }
    int64_t now = pt_now_ms();
    if(now < 0 || now - start >= 500) {
      errno = ETIMEDOUT;
      return -1;
    }
    usleep(100);
  }
}


static int
sys_ptrace(int request, pid_t pid, caddr_t addr, int data) {
  pid_t mypid = getpid();
  uint64_t authid;
  int ret;

  if(!(authid=kernel_get_ucred_authid(mypid))) {
    return -1;
  }
  if(kernel_set_ucred_authid(mypid, 0x4800000000010003l)) {
    return -1;
  }

  ret = (int)syscall(SYS_ptrace, request, pid, addr, data);

  if(kernel_set_ucred_authid(mypid, authid)) {
    return -1;
  }

  return ret;
}


intptr_t
pt_resolve(pid_t pid, const char* nid) {
  intptr_t addr;

  if((addr=kernel_dynlib_resolve(pid, 0x1, nid))) {
    return addr;
  }

  return kernel_dynlib_resolve(pid, 0x2001, nid);
}


int
pt_attach(pid_t pid) {
  int status;
  if(sys_ptrace(PT_ATTACH, pid, 0, 0) == -1) {
    return -1;
  }

  if(pt_wait_stop(pid, &status)) {
    /* The attach may have taken effect even when its wait failed. */
    sys_ptrace(PT_DETACH, pid, 0, 0);
    return -1;
  }

  return 0;
}


int
pt_detach(pid_t pid, int sig) {
  if(sys_ptrace(PT_DETACH, pid, 0, sig) == -1) {
    return -1;
  }

  return 0;
}


int
pt_step(int pid) {
  int status;
  if(sys_ptrace(PT_STEP, pid, (caddr_t)1, 0)) {
    return -1;
  }

  if(pt_wait_stop(pid, &status)) {
    int saved_errno = errno;
    /* Regain a stopped target before callers restore their saved registers. */
    if(kill(pid, SIGSTOP) == 0) {
      pt_wait_stop(pid, &status);
    }
    errno = saved_errno;
    return -1;
  }
  if(WSTOPSIG(status) != SIGTRAP) {
    VSHSIG_KLOG("[vshsig-ptrace] unexpected stop: pid=%d signal=%d\n",
                pid, WSTOPSIG(status));
    errno = EFAULT;
    return -1;
  }
  return 0;
}


int
pt_continue(pid_t pid, int sig) {
  if(sys_ptrace(PT_CONTINUE, pid, (caddr_t)1, sig) == -1) {
    return -1;
  }

  return 0;
}


int
pt_getint(pid_t pid, intptr_t addr) {
  return sys_ptrace(PT_READ_D, pid, (caddr_t)addr, 0);
}


int
pt_setint(pid_t pid, intptr_t addr, int val) {
  return sys_ptrace(PT_WRITE_D, pid, (caddr_t)addr, val);
}


int
pt_getregs(pid_t pid, struct reg *r) {
  return sys_ptrace(PT_GETREGS, pid, (caddr_t)r, 0);
}


int
pt_setregs(pid_t pid, const struct reg *r) {
  return sys_ptrace(PT_SETREGS, pid, (caddr_t)r, 0);
}


int
pt_get_thread_id(pid_t pid) {
  struct ptrace_lwpinfo info = {0};
  if(sys_ptrace(PT_LWPINFO, pid, (caddr_t)&info, sizeof(info))) {
    return -1;
  }
  return info.pl_lwpid;
}


int
pt_copyin(pid_t pid, const void* buf, intptr_t addr, size_t len) {
  struct ptrace_io_desc iod = {
    .piod_op = PIOD_WRITE_D,
    .piod_offs = (void*)addr,
    .piod_addr = (void*)buf,
    .piod_len = len};
  return sys_ptrace(PT_IO, pid, (caddr_t)&iod, 0);
}


int
pt_setchar(pid_t pid, intptr_t addr, char val) {
  return pt_copyin(pid, &val, addr, sizeof(val));
}


int
pt_setshort(pid_t pid, intptr_t addr, short val) {
  return pt_copyin(pid, &val, addr, sizeof(val));
}


int
pt_setlong(pid_t pid, intptr_t addr, long val) {
  return pt_copyin(pid, &val, addr, sizeof(val));
}


int
pt_copyout(pid_t pid, intptr_t addr, void* buf, size_t len) {
  struct ptrace_io_desc iod = {
    .piod_op = PIOD_READ_D,
    .piod_offs = (void*)addr,
    .piod_addr = buf,
    .piod_len = len};
  return sys_ptrace(PT_IO, pid, (caddr_t)&iod, 0);
}

struct pt_event_wait_site {
  intptr_t syscall_return;
  intptr_t caller_return;
};

/* Use the SDK's existing read-only interface. Split at 4 KiB boundaries so
 * each request stays within a mapped page, including in older SDK builds. */
static int
pt_read_code(pid_t pid, intptr_t addr, void *buf, size_t len) {
  unsigned char *out = buf;
  while(len) {
    size_t chunk = 0x1000 - ((uintptr_t)addr & 0xfff);
    if(chunk > len) chunk = len;
    if(kernel_proc_copyout(pid, addr, out, chunk)) return -1;
    addr += chunk;
    out += chunk;
    len -= chunk;
  }
  return 0;
}

/* Recognize the libkernel stub's instructions, not its module offset.
 * Accept mov rax,imm32 or mov eax,imm32, followed by mov r10,rcx and syscall. */
static size_t
pt_wait_syscall_return(const unsigned char code[32]) {
  size_t pos;
  uint32_t number;
  if(!memcmp(code, "\x48\xc7\xc0", 3)) {
    memcpy(&number, code + 3, sizeof(number));
    pos = 7;
  } else if(code[0] == 0xb8) {
    memcpy(&number, code + 1, sizeof(number));
    pos = 5;
  } else {
    return 0;
  }
  if(number != 542) return 0; /* Event-flag wait syscall. */
  if(memcmp(code + pos, "\x49\x89\xca", 3) &&
     memcmp(code + pos, "\x4c\x8b\xd1", 3)) return 0;
  pos += 3;
  if(memcmp(code + pos, "\x0f\x05\x72\x01\xc3", 5)) return 0;
  return pos + 2;
}

/* A bounded direct-call search. A candidate must target a verified syscall
 * stub, be unique, and later match the stopped thread's actual stack caller.
 * Unsupported or ambiguous code is rejected before attaching to ShellUI. */
static int
pt_find_event_wait(pid_t pid, intptr_t kernel_base, intptr_t wait_function,
                    struct pt_event_wait_site *site) {
  unsigned char wrapper[128];
  int found = 0;
  memset(site, 0, sizeof(*site));
  if(wait_function < kernel_base || wait_function - kernel_base >= 0x1000000 ||
     pt_read_code(pid, wait_function, wrapper, sizeof(wrapper))) return -1;
  for(size_t i = 0; i + 5 <= sizeof(wrapper); ++i) {
    if(wrapper[i] != 0xe8) continue;
    int32_t displacement;
    memcpy(&displacement, wrapper + i + 1, sizeof(displacement));
    intptr_t caller = wait_function + i + 5;
    intptr_t target = caller + displacement;
    if(target < kernel_base || target - kernel_base >= 0x1000000) continue;
    unsigned char stub[32];
    if(pt_read_code(pid, target, stub, sizeof(stub))) return -1;
    size_t offset = pt_wait_syscall_return(stub);
    if(!offset) continue;
    if(found) return -1;
    site->syscall_return = target + offset;
    site->caller_return = caller;
    found = 1;
  }
  return found ? 0 : -1;
}

static int
pt_is_event_wait(pid_t pid, const struct reg *r, const struct pt_event_wait_site *site) {
  intptr_t caller;
  if(r->r_rip != site->syscall_return) return 0;
  if(pt_copyout(pid, r->r_rsp, &caller, sizeof(caller))) return -1;
  return caller == site->caller_return;
}

int
pt_attach_event_wait(pid_t pid, int timeout_ms) {
  /* Resolve sceKernelWaitEventFlag in the same module used for kernel_base. */
  intptr_t wait_function = kernel_dynlib_resolve(pid, 0x2001, "JTvBflhYazQ");
  intptr_t kernel_base = kernel_dynlib_mapbase_addr(pid, 0x2001);
  int64_t start = pt_now_ms();
  if(!kernel_base || !wait_function ||
     start < 0 || timeout_ms <= 0) {
    VSHSIG_LOG("[vshsig-ptrace] invalid wait-site setup: function=%#lx base=%#lx\n",
           wait_function, kernel_base);
    return -1;
  }
  struct pt_event_wait_site site;
  if(pt_find_event_wait(pid, kernel_base, wait_function, &site)) {
    VSHSIG_LOG("[vshsig-ptrace] cannot identify a unique event-wait instruction sequence\n");
    return -1;
  }
  VSHSIG_LOG("[vshsig-ptrace] event wait resolved: syscall=%#lx caller=%#lx offsets=%#lx/%#lx\n",
         site.syscall_return, site.caller_return,
         site.syscall_return - kernel_base, site.caller_return - wait_function);
  unsigned int attempts = 0;
  for(;;) {
    struct reg r;
    if(pt_attach(pid)) return -1;
    ++attempts;
    int result = pt_getregs(pid, &r) ? -1 :
                 pt_is_event_wait(pid, &r, &site);
    if(result < 0) {
      VSHSIG_LOG("[vshsig-ptrace] event-wait inspection failed: pid=%d errno=%d\n", pid, errno);
    }
    if(result == 1) {
      VSHSIG_LOG("[vshsig-ptrace] event wait accepted: pid=%d rip=%#lx attempts=%u\n",
             pid, r.r_rip, attempts);
      return 0;
    }
    if(result == 0 && attempts == 1) {
      VSHSIG_LOG("[vshsig-ptrace] deferring interrupted site: pid=%d rip=%#lx\n",
             pid, r.r_rip);
    }
    /* No remote helper or payload has run; release the untouched thread. */
    if(pt_detach(pid, 0)) return -1;
    if(result < 0) return -1;
    int64_t now = pt_now_ms();
    if(now < 0 || now - start >= timeout_ms || attempts >= 256) {
      errno = ETIMEDOUT;
      return -1;
    }
    usleep(2000 + (attempts % 7) * 1000);
  }
}


char
pt_getchar(pid_t pid, intptr_t addr) {
  char val = 0;

  pt_copyout(pid, addr, &val, sizeof(val));

  return val;
}


short
pt_getshort(pid_t pid, intptr_t addr) {
  short val = 0;

  pt_copyout(pid, addr, &val, sizeof(val));

  return val;
}


long
pt_getlong(pid_t pid, intptr_t addr) {
  long val = 0;

  pt_copyout(pid, addr, &val, sizeof(val));

  return val;
}


static long
pt_call(pid_t pid, intptr_t addr, uint64_t a0, uint64_t a1, uint64_t a2,
        uint64_t a3, uint64_t a4, uint64_t a5) {
  struct reg jmp_reg;
  struct reg bak_reg;

  if(pt_getregs(pid, &bak_reg)) {
    return -1;
  }

  memcpy(&jmp_reg, &bak_reg, sizeof(jmp_reg));
  jmp_reg.r_rip = addr;

  jmp_reg.r_rdi = a0;
  jmp_reg.r_rsi = a1;
  jmp_reg.r_rdx = a2;
  jmp_reg.r_rcx = a3;
  jmp_reg.r_r8  = a4;
  jmp_reg.r_r9  = a5;

  if(pt_setregs(pid, &jmp_reg)) {
    return -1;
  }

  int64_t start = pt_now_ms();
  unsigned int steps = 0;
  // Single step until return; restore the pre-call registers on every failure.
  while(jmp_reg.r_rsp <= bak_reg.r_rsp) {
    int64_t now = pt_now_ms();
    if(start < 0 || now < 0 || ++steps > 1024 || now - start >= 500 ||
       pt_step(pid) || pt_getregs(pid, &jmp_reg)) {
      VSHSIG_KLOG("[vshsig-ptrace] call aborted: pid=%d addr=%#lx rip=%#lx steps=%u\n",
                  pid, addr, jmp_reg.r_rip, steps);
      pt_setregs(pid, &bak_reg);
      return -1;
    }
  }

  // restore registers
  if(pt_setregs(pid, &bak_reg)) {
    return -1;
  }

  return jmp_reg.r_rax;
}


long
pt_syscall(pid_t pid, int sysno, uint64_t a0, uint64_t a1, uint64_t a2,
           uint64_t a3, uint64_t a4, uint64_t a5) {
  intptr_t addr = pt_resolve(pid, "HoLVWNanBBc");
  struct reg jmp_reg;
  struct reg bak_reg;

  if(!addr) {
    return -1;
  } else {
    addr += 0xa;
  }

  if(pt_getregs(pid, &bak_reg)) {
    return -1;
  }

  memcpy(&jmp_reg, &bak_reg, sizeof(jmp_reg));
  jmp_reg.r_rip = addr;
  jmp_reg.r_rax = sysno;

  jmp_reg.r_rdi = a0;
  jmp_reg.r_rsi = a1;
  jmp_reg.r_rdx = a2;
  jmp_reg.r_r10 = a3;
  jmp_reg.r_r8  = a4;
  jmp_reg.r_r9  = a5;

  if(pt_setregs(pid, &jmp_reg)) {
    return -1;
  }

  int64_t start = pt_now_ms();
  unsigned int steps = 0;
  // Single step until return; restore the pre-call registers on every failure.
  while(jmp_reg.r_rsp <= bak_reg.r_rsp) {
    int64_t now = pt_now_ms();
    if(start < 0 || now < 0 || ++steps > 1024 || now - start >= 500 ||
       pt_step(pid) || pt_getregs(pid, &jmp_reg)) {
      VSHSIG_KLOG("[vshsig-ptrace] syscall aborted: pid=%d sysno=%d rip=%#lx steps=%u\n",
                  pid, sysno, jmp_reg.r_rip, steps);
      pt_setregs(pid, &bak_reg);
      return -1;
    }
  }

  // restore registers
  if(pt_setregs(pid, &bak_reg)) {
    return -1;
  }

  return jmp_reg.r_rax;
}


intptr_t
pt_mmap(pid_t pid, intptr_t addr, size_t len, int prot, int flags,
	int fd, off_t off) {
  return pt_syscall(pid, SYS_mmap, addr, len, prot, flags, fd, off);
}


int
pt_msync(pid_t pid, intptr_t addr, size_t len, int flags) {
  return pt_syscall(pid, SYS_msync, addr, len, flags, 0, 0, 0);
}


int
pt_munmap(pid_t pid, intptr_t addr, size_t len) {
  return pt_syscall(pid, SYS_munmap, addr, len, 0, 0, 0, 0);
}


int
pt_mprotect(pid_t pid, intptr_t addr, size_t len, int prot) {
  return pt_syscall(pid, SYS_mprotect, addr, len, prot, 0, 0, 0);
}


int
pt_socket(pid_t pid, int domain, int type, int protocol) {
  return (int)pt_syscall(pid, SYS_socket, domain, type, protocol, 0, 0, 0);
}


int
pt_setsockopt(pid_t pid, int fd, int level, int optname, intptr_t optval,
	      socklen_t optlen) {
  return (int)pt_syscall(pid, SYS_setsockopt, fd, level, optname, optval,
			 optlen, 0);
}


int
pt_close(pid_t pid, int fd) {
  return (int)pt_syscall(pid, SYS_close, fd, 0, 0, 0, 0, 0);
}


int
pt_bind(pid_t pid, int sockfd, intptr_t addr, uint32_t addrlen) {
  return (int)pt_syscall(pid, SYS_bind, sockfd, addr, addrlen, 0, 0, 0);
}


ssize_t
pt_recvmsg(pid_t pid, int fd, intptr_t msg, int flags) {
  return (int)pt_syscall(pid, SYS_recvmsg, fd, msg, flags, 0, 0, 0);
}


int
pt_dup2(pid_t pid, int oldfd, int newfd) {
  return (int)pt_syscall(pid, SYS_dup2, oldfd, newfd, 0, 0, 0, 0);
}


int
pt_rdup(pid_t pid, pid_t other_pid, int fd) {
  return (int)pt_syscall(pid, 0x25b, other_pid, fd, 0, 0, 0, 0);
}


int
pt_pipe(pid_t pid, intptr_t pipefd) {
  intptr_t faddr = pt_resolve(pid, "-Jp7F+pXxNg");
  return (int)pt_call(pid, faddr, pipefd, 0, 0, 0, 0, 0);
}


int
pt_errno(pid_t pid) {
  intptr_t faddr = pt_resolve(pid, "9BcDykPmo1I");
  intptr_t addr = pt_call(pid, faddr, 0, 0, 0, 0, 0, 0);
  return pt_getint(pid, addr);
}


intptr_t
pt_sceKernelGetProcParam(pid_t pid) {
  intptr_t faddr = pt_resolve(pid, "959qrazPIrg");

  return pt_call(pid, faddr, 0, 0, 0, 0, 0, 0);
}
