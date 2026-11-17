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

#ifdef VSHSIG_INJECTION_HOST_TEST
#include "../../tests/injection_platform.h"
#else
#include <elf.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/ptrace.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/sysctl.h>

#include <sys/un.h>
#include <sys/user.h>
#include <sys/wait.h>

#include <ps5/kernel.h>

#include "elfldr.h"
#include "log.h"
#include "pt.h"
#include "thread_bootstrap.h"
#include "../../src/hook_args.h"


#ifndef IPV6_2292PKTOPTIONS
#define IPV6_2292PKTOPTIONS 25
#endif


/**
 * Convenient macros.
 **/
#define ROUND_PG(x) (((x) + (PAGE_SIZE - 1)) & ~(PAGE_SIZE - 1))
#define TRUNC_PG(x) ((x) & ~(PAGE_SIZE - 1))
#define PFLAGS(x)   ((((x) & PF_R) ? PROT_READ  : 0) | \
		     (((x) & PF_W) ? PROT_WRITE : 0) | \
		     (((x) & PF_X) ? PROT_EXEC  : 0))


/**
 * Context structure for the ELF loader.
 **/
typedef struct elfldr_ctx {
  uint8_t* elf;
  pid_t    pid;

  intptr_t base_addr;
  size_t   base_size;
  void*    base_mirror;
} elfldr_ctx_t;


/**
 * Absolute path to the SceSpZeroConf eboot.
 **/
static const char* SceSpZeroConf = "/system/vsh/app/NPXS40112/eboot.bin";


/**
* Parse a R_X86_64_RELATIVE relocatable.
**/
static int
r_relative(elfldr_ctx_t *ctx, Elf64_Rela* rela) {
  intptr_t* loc = ctx->base_mirror + rela->r_offset;
  intptr_t val = ctx->base_addr + rela->r_addend;

  *loc = val;

  return 0;
}


/**
 * Parse a PT_LOAD program header.
 **/
static int
data_load(elfldr_ctx_t *ctx, Elf64_Phdr *phdr) {
  void* data = ctx->base_mirror + phdr->p_vaddr;

  if(!phdr->p_memsz) {
    return 0;
  }

  if(!phdr->p_filesz) {
    return 0;
  }

  memcpy(data, ctx->elf+phdr->p_offset, phdr->p_filesz);

  return 0;
}


int
elfldr_sanity_check(uint8_t *elf, size_t elf_size) {
  Elf64_Ehdr *ehdr = (Elf64_Ehdr*)elf;
  Elf64_Phdr *phdr;

  if(elf_size < sizeof(Elf64_Ehdr) ||
     elf_size < sizeof(Elf64_Phdr) + ehdr->e_phoff ||
     elf_size < sizeof(Elf64_Shdr) + ehdr->e_shoff) {
    return -1;
  }

  if(ehdr->e_ident[0] != 0x7f || ehdr->e_ident[1] != 'E' ||
     ehdr->e_ident[2] != 'L'  || ehdr->e_ident[3] != 'F') {
    return -1;
  }

  phdr = (Elf64_Phdr*)(elf + ehdr->e_phoff);
  for(int i=0; i<ehdr->e_phnum; i++) {
    if(phdr[i].p_offset + phdr[i].p_filesz > elf_size) {
      return -1;
    }
  }

  return 0;
}


/**
 * Load an ELF into the address space of a process with the given pid.
 **/
static intptr_t
elfldr_load(pid_t pid, uint8_t *elf, struct elfldr_injection *injection) {
  Elf64_Ehdr *ehdr = (Elf64_Ehdr*)elf;
  Elf64_Phdr *phdr = (Elf64_Phdr*)(elf + ehdr->e_phoff);
  Elf64_Shdr *shdr = (Elf64_Shdr*)(elf + ehdr->e_shoff);

  elfldr_ctx_t ctx = {.elf = elf, .pid=pid};

  size_t min_vaddr = -1;
  size_t max_vaddr = 0;

  int error = 0;

  // Compute size of virtual memory region.
  for(int i=0; i<ehdr->e_phnum; i++) {
    if(phdr[i].p_vaddr < min_vaddr) {
      min_vaddr = phdr[i].p_vaddr;
    }

    if(max_vaddr < phdr[i].p_vaddr + phdr[i].p_memsz) {
      max_vaddr = phdr[i].p_vaddr + phdr[i].p_memsz;
    }
  }

  min_vaddr = TRUNC_PG(min_vaddr);
  max_vaddr = ROUND_PG(max_vaddr);
  ctx.base_size = max_vaddr - min_vaddr;

  int flags = MAP_PRIVATE | MAP_ANONYMOUS;
  int prot = PROT_READ | PROT_WRITE;
  if(ehdr->e_type == ET_DYN) {
    ctx.base_addr = 0;
  } else if(ehdr->e_type == ET_EXEC) {
    ctx.base_addr = min_vaddr;
    flags |= MAP_FIXED;
  } else {
    LOG_PUTS("elfldr_load: ELF type not supported");
    return 0;
  }

  if(!(ctx.base_mirror=calloc(1, ctx.base_size))) {
    LOG_PERROR("calloc");
    return 0;
  }

  // Reserve an address space of sufficient size.
  if((ctx.base_addr=pt_mmap(pid, ctx.base_addr, ctx.base_size, prot,
			    flags, -1, 0)) == -1) {
    LOG_PT_PERROR(pid, "pt_mmap");
    free(ctx.base_mirror);
    return 0;
  }
  if(injection) {
    injection->image = ctx.base_addr;
    injection->image_size = ctx.base_size;
  }

  // Parse program headers.
  for(int i=0; i<ehdr->e_phnum && !error; i++) {
    switch(phdr[i].p_type) {
    case PT_LOAD:
      error = data_load(&ctx, &phdr[i]);
      break;
    }
  }

  // Apply relocations.
  for(int i=0; i<ehdr->e_shnum && !error; i++) {
    if(shdr[i].sh_type != SHT_RELA) {
      continue;
    }

    Elf64_Rela* rela = (Elf64_Rela*)(elf + shdr[i].sh_offset);
    for(int j=0; j<shdr[i].sh_size/sizeof(Elf64_Rela); j++) {
      switch(rela[j].r_info & 0xffffffffl) {
      case R_X86_64_RELATIVE:
	error = r_relative(&ctx, &rela[j]);
	break;
      }
    }
  }

  if(pt_copyin(ctx.pid, ctx.base_mirror, ctx.base_addr, ctx.base_size)) {
    LOG_PERROR("pt_copyin");
    error = 1;
  }

  // Set protection bits on mapped segments.
  for(int i=0; i<ehdr->e_phnum && !error; i++) {
    if(phdr[i].p_type != PT_LOAD || phdr[i].p_memsz == 0) {
      continue;
    }

    if(phdr[i].p_flags & PF_X) {
      if(kernel_mprotect(pid, ctx.base_addr + phdr[i].p_vaddr,
                         ROUND_PG(phdr[i].p_memsz),
                         PFLAGS(phdr[i].p_flags))) {
	LOG_PERROR("kernel_mprotect");
	error = 1;
      }
    } else {
      if(pt_mprotect(pid, ctx.base_addr + phdr[i].p_vaddr,
		     ROUND_PG(phdr[i].p_memsz),
		     PFLAGS(phdr[i].p_flags))) {
	LOG_PT_PERROR(pid, "pt_mprotect");
	error = 1;
      }
    }
  }

  if(pt_msync(pid, ctx.base_addr, ctx.base_size, MS_SYNC)) {
    LOG_PT_PERROR(pid, "pt_msync");
    error = 1;
  }

  free(ctx.base_mirror);

  if(error) {
    if(!pt_munmap(pid, ctx.base_addr, ctx.base_size) && injection) {
      injection->image = 0;
    }
    return 0;
  }

  return ctx.base_addr + ehdr->e_entry;
}


/**
 * Create payload args in the address space of the process with the given pid.
 **/
static intptr_t
elfldr_payload_args(pid_t pid) {
  int victim_sock;
  int master_sock;
  intptr_t buf;
  int pipe0;
  int pipe1;

  if((buf=pt_mmap(pid, 0, PAGE_SIZE, PROT_READ | PROT_WRITE,
		  MAP_ANONYMOUS | MAP_PRIVATE, -1, 0)) == -1) {
    LOG_PT_PERROR(pid, "pt_mmap");
    return 0;
  }

  if((master_sock=pt_socket(pid, AF_INET6, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
    LOG_PT_PERROR(pid, "pt_socket");
    return 0;
  }

  pt_setint(pid, buf+0x00, 20);
  pt_setint(pid, buf+0x04, IPPROTO_IPV6);
  pt_setint(pid, buf+0x08, IPV6_TCLASS);
  pt_setint(pid, buf+0x0c, 0);
  pt_setint(pid, buf+0x10, 0);
  pt_setint(pid, buf+0x14, 0);
  if(pt_setsockopt(pid, master_sock, IPPROTO_IPV6, IPV6_2292PKTOPTIONS, buf, 24)) {
    LOG_PT_PERROR(pid, "pt_setsockopt");
    return 0;
  }

  if((victim_sock=pt_socket(pid, AF_INET6, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
    LOG_PT_PERROR(pid, "pt_socket");
    return 0;
  }

  pt_setint(pid, buf+0x00, 0);
  pt_setint(pid, buf+0x04, 0);
  pt_setint(pid, buf+0x08, 0);
  pt_setint(pid, buf+0x0c, 0);
  pt_setint(pid, buf+0x10, 0);
  if(pt_setsockopt(pid, victim_sock, IPPROTO_IPV6, IPV6_PKTINFO, buf, 20)) {
    LOG_PT_PERROR(pid, "pt_setsockopt");
    return 0;
  }

  if(kernel_overlap_sockets(pid, master_sock, victim_sock)) {
    LOG_PUTS("kernel_overlap_sockets failed");
    return 0;
  }

  if(pt_pipe(pid, buf)) {
    LOG_PT_PERROR(pid, "pt_pipe");
    return 0;
  }
  pipe0 = pt_getint(pid, buf);
  pipe1 = pt_getint(pid, buf+4);

  intptr_t args       = buf;
  intptr_t rwpipe     = buf + 0x100;
  intptr_t rwpair     = buf + 0x200;
  intptr_t kpipe_addr = kernel_get_proc_file(pid, pipe0);
  intptr_t payloadout = buf + 0x300;
  intptr_t getpid      = pt_resolve(pid, "HoLVWNanBBc");

  pt_setlong(pid, args + 0x00, getpid);
  pt_setlong(pid, args + 0x08, rwpipe);
  pt_setlong(pid, args + 0x10, rwpair);
  pt_setlong(pid, args + 0x18, kpipe_addr);
  pt_setlong(pid, args + 0x20, KERNEL_ADDRESS_DATA_BASE);
  pt_setlong(pid, args + 0x28, payloadout);
  pt_setint(pid, rwpipe + 0, pipe0);
  pt_setint(pid, rwpipe + 4, pipe1);
  pt_setint(pid, rwpair + 0, master_sock);
  pt_setint(pid, rwpair + 4, victim_sock);
  pt_setint(pid, payloadout, 0);

  return args;
}


/**
 * Prepare registers of a process for execution of an ELF.
 **/
static int
elfldr_prepare_exec(pid_t pid, uint8_t *elf) {
  intptr_t entry;
  intptr_t args;
  struct reg r;

  if(pt_getregs(pid, &r)) {
    LOG_PERROR("pt_getregs");
    return -1;
  }

  if(!(entry=elfldr_load(pid, elf, NULL))) {
    LOG_PUTS("elfldr_load failed");
    return -1;
  }

  if(!(args=elfldr_payload_args(pid))) {
    LOG_PUTS("elfldr_payload_args failed");
    return -1;
  }

  pt_setlong(pid, r.r_rsp-8, r.r_rip);
  r.r_rsp -= 8;
  r.r_rip = entry;
  r.r_rdi = args;

  if(pt_setregs(pid, &r)) {
    LOG_PERROR("pt_setregs");
    return -1;
  }

  return 0;
}

#endif /* !VSHSIG_INJECTION_HOST_TEST */

int
elfldr_resolve_mono_args(pid_t pid, struct vshsig_hook_args *args) {
  unsigned int mono_handle;
  if(kernel_dynlib_handle(pid, "libmonosgen-2.0.sprx", &mono_handle)) {
    return -1;
  }
  /* Always resolved (not gated behind VSHSIG_LOGGING like upstream): this
   * diagnostic hook's whole purpose is its log output. 0x2 (libkernel) and
   * 0x2001 (libSceSysmodule-provided debug text) are the same handle
   * numbers ps5-syslang already resolves these two names from. */
  args->format = kernel_dynlib_dlsym(pid, 0x2, "vsnprintf");
  args->debug_out = kernel_dynlib_dlsym(pid, 0x2001, "sceKernelDebugOutText");
  if(!args->format || !args->debug_out) return -1;
#define RESOLVE_MONO(name) \
  if(!(args->name = kernel_dynlib_dlsym(pid, mono_handle, #name))) return -1;
  VSHSIG_MONO_EXPORTS(RESOLVE_MONO)
#undef RESOLVE_MONO
  return 0;
}


static intptr_t
elfldr_pack_hook_args(pid_t pid, const struct vshsig_hook_args *args,
                     struct elfldr_injection *injection) {
  intptr_t addr = pt_mmap(pid, 0, PAGE_SIZE, PROT_READ | PROT_WRITE,
                          MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  if(addr == -1) return 0;
  injection->args = addr;
  if(pt_copyin(pid, args, addr, sizeof(*args))) return 0;
  return addr;
}


#ifdef VSHSIG_VERIFY_UNMAP
/* Test instrumentation: observe before a resumed UI can reuse the addresses. */
static void
elfldr_verify_unmap(pid_t pid, intptr_t addr, size_t size, intptr_t reference) {
  unsigned int pages = 0, unmapped = 0;
  for(size_t offset = 0; offset < size; offset += PAGE_SIZE) {
    ++pages;
    if(kernel_get_vmem_protection(pid, addr + offset, 1) == -1) ++unmapped;
  }
  int reference_prot = kernel_get_vmem_protection(pid, reference, 1);
  printf("[vshsig-inject] unmap_check addr=%#lx bytes=%zu pages=%u unmapped=%u reference_prot=%#x\n",
         addr, size, pages, unmapped, reference_prot);
}
#define VERIFY_UNMAP(pid, addr, size, reference) elfldr_verify_unmap(pid, addr, size, reference)
#else
#define VERIFY_UNMAP(pid, addr, size, reference) ((void)0)
#endif

/* Caller owns the stopped thread and has selected the scratch stack. */
static int
elfldr_release_preserved(pid_t pid, struct elfldr_injection *injection,
                         const struct reg *resume) {
  int error = 0;
  unsigned char red_zone[128];
  if(pt_copyout(pid, resume->r_rsp - sizeof(red_zone), red_zone, sizeof(red_zone))) {
    pt_setregs(pid, resume);
    return -1;
  }
#define RELEASE(field, size) \
  if(injection->field) { \
    if(pt_munmap(pid, injection->field, (size))) error = -1; \
    else { \
      VERIFY_UNMAP(pid, injection->field, (size), resume->r_rip); \
      injection->field = 0; \
    } \
  }
  RELEASE(image, injection->image_size);
  RELEASE(args, PAGE_SIZE);
  RELEASE(bootstrap, 2 * PAGE_SIZE);
  /* The last unmap must not execute a return on the freed scratch stack.
   * Also preserve the original red zone if a syscall error wrapper uses it. */
  if(pt_setregs(pid, resume)) return -1;
  RELEASE(stack, 0x10000);
#undef RELEASE
  if(pt_copyin(pid, red_zone, resume->r_rsp - sizeof(red_zone), sizeof(red_zone))) {
    error = -1;
  }
  return error;
}
#undef VERIFY_UNMAP


static int
elfldr_prepare_preserved(pid_t pid, uint8_t *elf, const struct vshsig_hook_args *hook_args,
                         struct elfldr_injection *injection) {
  const size_t stack_size = 0x10000;
  struct reg original, launch;
  struct thread_bootstrap state = {0};
  intptr_t stack = 0, mapping = 0, entry, args;
  size_t code_size = vshsig_bootstrap_end - vshsig_bootstrap_start;
  unsigned char *code = NULL;

  memset(injection, 0, sizeof(*injection));
  if(pt_getregs(pid, &original)) {
    return -1;
  }
  injection->thread_id = pt_get_thread_id(pid);
  injection->original_rsp = original.r_rsp;
  if(injection->thread_id <= 0) return -1;
  if(code_size > PAGE_SIZE || !(code = malloc(code_size))) {
    return -1;
  }
  /* Remote helper calls must not overwrite the interrupted thread's red zone. */
  stack = pt_mmap(pid, 0, stack_size, PROT_READ | PROT_WRITE,
                  MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  if(stack == -1) {
    free(code);
    return -1;
  }
  injection->stack = stack;
  launch = original;
  VSHSIG_LOG("[vshsig-inject] original rip=%#lx rsp=%#lx scratch=%#lx\n",
         original.r_rip, original.r_rsp, stack);
  launch.r_rsp = stack + stack_size - 0x108;
  if(pt_setregs(pid, &launch)) {
    goto fail;
  }
  if(!(entry = elfldr_load(pid, elf, injection)) ||
     !(args = elfldr_pack_hook_args(pid, hook_args, injection))) {
    goto fail;
  }
  VSHSIG_LOG("[vshsig-inject] entry=%#lx args=%#lx\n", entry, args);
  mapping = pt_mmap(pid, 0, 2 * PAGE_SIZE, PROT_READ | PROT_WRITE,
                    MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  if(mapping == -1) {
    mapping = 0;
    goto fail;
  }
  injection->bootstrap = mapping;
  state.r15 = original.r_r15;
  state.r14 = original.r_r14;
  state.r13 = original.r_r13;
  state.r12 = original.r_r12;
  state.r11 = original.r_r11;
  state.r10 = original.r_r10;
  state.r9 = original.r_r9;
  state.r8 = original.r_r8;
  state.rdi = original.r_rdi;
  state.rsi = original.r_rsi;
  state.rbp = original.r_rbp;
  state.rbx = original.r_rbx;
  state.rdx = original.r_rdx;
  state.rcx = original.r_rcx;
  state.rax = original.r_rax;
  state.rsp = original.r_rsp;
  state.rflags = original.r_rflags;
  state.entry = entry;
  state.args = args;
  state.status = -1;
  memcpy(code, vshsig_bootstrap_start, code_size);
  memcpy(code + (vshsig_bootstrap_rip - vshsig_bootstrap_start),
         &original.r_rip, sizeof(original.r_rip));
  intptr_t state_addr = mapping + PAGE_SIZE;
  memcpy(code + (vshsig_bootstrap_state - vshsig_bootstrap_start),
         &state_addr, sizeof(state_addr));
  if(pt_copyin(pid, code, mapping, code_size) ||
     pt_copyin(pid, &state, mapping + PAGE_SIZE, sizeof(state)) ||
     /* kernel_mprotect changes a whole VM entry without splitting it.
      * First split code from writable state through the real syscall. */
     pt_mprotect(pid, mapping, PAGE_SIZE, PROT_READ) ||
     kernel_mprotect(pid, mapping, PAGE_SIZE, PROT_READ | PROT_EXEC)) {
    goto fail;
  }
  int code_prot = kernel_get_vmem_protection(pid, mapping, PAGE_SIZE);
  int state_prot = kernel_get_vmem_protection(pid, state_addr, PAGE_SIZE);
  VSHSIG_LOG("[vshsig-inject] code_prot=%#x state_prot=%#x\n", code_prot, state_prot);
  if(code_prot != (PROT_READ | PROT_EXEC) ||
     state_prot != (PROT_READ | PROT_WRITE)) {
    LOG_PUTS("bootstrap code/state protections are not isolated");
    goto fail;
  }
  launch.r_rip = mapping;
  launch.r_rsp = stack + stack_size - 0x100;
  launch.r_rdi = mapping + PAGE_SIZE;
  if(pt_setregs(pid, &launch)) {
    goto fail;
  }
  injection->status_addr = mapping + PAGE_SIZE + BOOT_STATUS;
  VSHSIG_LOG("[vshsig-inject] bootstrap=%#lx state=%#lx restore=%#lx\n",
         mapping, mapping + PAGE_SIZE, original.r_rip);
  free(code);
  return 0;

fail:
  launch = original;
  launch.r_rsp = stack + stack_size - 0x108;
  int cleanup_error = pt_setregs(pid, &launch) ? -1 :
                      elfldr_release_preserved(pid, injection, &original);
  (void)cleanup_error;
  pt_setregs(pid, &original);
  VSHSIG_LOG("[vshsig-inject] setup_cleanup_result=%d image=%#lx args=%#lx stack=%#lx bootstrap=%#lx\n",
         cleanup_error, injection->image, injection->args, injection->stack, injection->bootstrap);
  free(code);
  return -1;
}


int
elfldr_cleanup_preserved(pid_t pid, struct elfldr_injection *injection) {
  struct reg resume, scratch;
  int status = -1;
  if(!injection->status_addr || !injection->stack ||
     kernel_proc_copyout(pid, injection->status_addr, &status, sizeof(status)) ||
     status == -1) return -1;
  /* A completion word alone is insufficient: the restore trampoline still
   * needs its state page. Reacquire the same original wait frame first. */
  if(pt_attach_event_wait(pid, 5000)) return -1;
  int error = -1;
  if(pt_get_thread_id(pid) != injection->thread_id ||
     pt_getregs(pid, &resume) || resume.r_rsp != injection->original_rsp) {
    LOG_PUTS("cleanup deferred: original thread has not returned to its wait frame");
    goto detach;
  }
  scratch = resume;
  scratch.r_rsp = injection->stack + 0x10000 - 0x108;
  if(pt_setregs(pid, &scratch)) goto detach;
  error = elfldr_release_preserved(pid, injection, &resume);
  if(error) pt_setregs(pid, &resume);
  if(!error) injection->status_addr = 0;
detach:
  if(pt_detach(pid, 0)) error = -1;
  VSHSIG_LOG("[vshsig-inject] cleanup_result=%d image=%#lx args=%#lx stack=%#lx bootstrap=%#lx\n",
         error, injection->image, injection->args, injection->stack, injection->bootstrap);
  return error;
}


#ifndef VSHSIG_INJECTION_HOST_TEST
/**
 * Set the name of a process.
 **/
static int
elfldr_set_procname(pid_t pid, const char* name) {
  intptr_t buf;

  if((buf=pt_mmap(pid, 0, PAGE_SIZE, PROT_READ | PROT_WRITE,
		  MAP_ANONYMOUS | MAP_PRIVATE, -1, 0)) == -1) {
    LOG_PT_PERROR(pid, "pt_mmap");
    return -1;
  }

  pt_copyin(pid, name, buf, strlen(name)+1);
  pt_syscall(pid, SYS_thr_set_name, -1, buf, 0, 0, 0, 0);
  pt_msync(pid, buf, PAGE_SIZE, MS_SYNC);
  pt_munmap(pid, buf, PAGE_SIZE);

  return 0;
}


/**
 * Escape jail and raise privileges.
 **/
int
elfldr_raise_privileges(pid_t pid) {
  static const uint8_t caps[16] = {0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
				   0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff};
  intptr_t vnode;

  if(!(vnode=kernel_get_root_vnode())) {
    return -1;
  }
  if(kernel_set_proc_rootdir(pid, vnode)) {
    return -1;
  }
  if(kernel_set_proc_jaildir(pid, 0)) {
    return -1;
  }
  if(kernel_set_ucred_uid(pid, 0)) {
    return -1;
  }
  if(kernel_set_ucred_caps(pid, caps)) {
    return -1;
  }

  return 0;
}


/**
 * Execute an ELF inside the process with the given pid.
 **/
static int
elfldr_exec_impl(pid_t pid, int stdio, uint8_t* elf, struct elfldr_injection *injection,
                 const struct vshsig_hook_args *hook_args) {
  uint8_t caps[16];
  intptr_t jaildir;
  intptr_t rootdir;
  uint64_t authid;
  int error = 0;

  // backup privileges
  jaildir = kernel_get_proc_jaildir(pid);
  if(!(rootdir=kernel_get_proc_rootdir(pid))) {
    LOG_PUTS("kernel_get_proc_rootdir failed");
    pt_detach(pid, 0);
    return -1;
  }
  if(kernel_get_ucred_caps(pid, caps)) {
    LOG_PUTS("kernel_get_ucred_caps failed");
    pt_detach(pid, 0);
    return -1;
  }
  if(!(authid=kernel_get_ucred_authid(pid))) {
    LOG_PUTS("kernel_get_ucred_authid failed");
    pt_detach(pid, 0);
    return -1;
  }

  if(elfldr_raise_privileges(pid)) {
    LOG_PUTS("Unable to raise privileges");
    pt_detach(pid, 0);
    return -1;
  }

  if(stdio > 0) {
    stdio = pt_rdup(pid, getpid(), stdio);

    pt_close(pid, STDERR_FILENO);
    pt_close(pid, STDOUT_FILENO);
    pt_close(pid, STDIN_FILENO);

    pt_dup2(pid, stdio, STDIN_FILENO);
    pt_dup2(pid, stdio, STDOUT_FILENO);
    pt_dup2(pid, stdio, STDERR_FILENO);

    pt_close(pid, stdio);
  }

  if(injection ? elfldr_prepare_preserved(pid, elf, hook_args, injection)
               : elfldr_prepare_exec(pid, elf)) {
    error = -1;
  }

  // restore privileges
  if(kernel_set_proc_jaildir(pid, jaildir)) {
    LOG_PUTS("kernel_set_proc_jaildir failed");
    error = -1;
  }
  if(kernel_set_proc_rootdir(pid, rootdir)) {
    LOG_PUTS("kernel_set_proc_rootdir failed");
    error = -1;
  }

  if(kernel_set_ucred_caps(pid, caps)) {
    LOG_PUTS("kernel_set_ucred_caps failed");
    error = -1;
  }
  if(kernel_set_ucred_authid(pid, authid)) {
    LOG_PUTS("kernel_set_ucred_authid failed");
    error = -1;
  }

  if(pt_detach(pid, 0)) {
    LOG_PERROR("pt_detach");
    error = -1;
  }

  return error;
}

int
elfldr_exec(pid_t pid, int stdio, uint8_t *elf) {
  return elfldr_exec_impl(pid, stdio, elf, NULL, NULL);
}

int
elfldr_exec_preserved(pid_t pid, uint8_t *elf, const struct vshsig_hook_args *args,
                      struct elfldr_injection *injection) {
  return elfldr_exec_impl(pid, -1, elf, injection, args);
}


/**
 * Set the heap size for libc.
 **/
static int
elfldr_set_heap_size(pid_t pid, ssize_t size) {
  intptr_t sceLibcHeapSize;
  intptr_t sceLibcParam;
  intptr_t sceProcParam;
  intptr_t Need_sceLibc;

  if(!(sceProcParam=pt_sceKernelGetProcParam(pid))) {
    LOG_PT_PERROR(pid, "pt_sceKernelGetProcParam");
    return -1;
  }

  if(pt_copyout(pid, sceProcParam+56, &sceLibcParam,
		sizeof(sceLibcParam))) {
    LOG_PERROR("pt_copyout");
    return -1;
  }

  if(pt_copyout(pid, sceLibcParam+16, &sceLibcHeapSize,
		sizeof(sceLibcHeapSize))) {
    LOG_PERROR("pt_copyout");
    return -1;
  }

  if(pt_setlong(pid, sceLibcHeapSize, size)) {
    LOG_PERROR("pt_setlong");
    return -1;
  }

  if(size != -1) {
    return 0;
  }

  if(pt_copyout(pid, sceLibcParam+72, &Need_sceLibc,
		sizeof(Need_sceLibc))) {
    LOG_PERROR("pt_copyout");
    return -1;
  }

  return pt_setlong(pid, sceLibcParam+32, Need_sceLibc);
}


static int
sys_budget_set(long budget) {
  return __syscall(0x23b, budget);
}


static int
elfldr_rfork_entry(void* argv) {
  if(sys_budget_set(0)) {
    klog_perror("sys_budget_set");
    return -1;
  }
  if(open("/dev/deci_stdin", O_RDONLY) < 0) {
    klog_perror("open");
    return -1;
  }
  if(open("/dev/deci_stdout", O_WRONLY) < 0) {
    klog_perror("open");
    return -1;
  }
  if(open("/dev/deci_stderr", O_WRONLY) < 0) {
    klog_perror("open");
    return -1;
  }

  if(ptrace(PT_TRACE_ME, 0, 0, 0)) {
    klog_perror("ptrace");
    return -1;
  }

  execve(SceSpZeroConf, argv, 0);
  klog_perror("execve");
  return -1;
}


/**
 * Execute an ELF inside a new process.
 **/
pid_t
elfldr_spawn(int stdio, char* const argv[], uint8_t* elf, size_t payload_size) {
  uint8_t int3instr = 0xcc;
  struct kevent evt;
  intptr_t brkpoint;
  uint8_t orginstr;
  void *stack;
  pid_t pid;
  int kq;

  if((kq=kqueue()) < 0) {
    LOG_PERROR("kqueue");
    return -1;
  }

  if(!(stack=malloc(PAGE_SIZE))) {
    LOG_PERROR("malloc");
    close(kq);
    return -1;
  }

  if((pid=rfork_thread(RFPROC | RFCFDG | RFMEM, stack+PAGE_SIZE-8,
		       elfldr_rfork_entry, (void*)argv)) < 0) {
    LOG_PERROR("rfork_thread");
    free(stack);
    close(kq);
    return -1;
  }

  EV_SET(&evt, pid, EVFILT_PROC, EV_ADD, NOTE_EXEC, 0, 0);
  if(kevent(kq, &evt, 1, &evt, 1, 0) < 0) {
    LOG_PERROR("kevent");
    free(stack);
    close(kq);
    return -1;
  }

  if(waitpid(pid, 0, 0) < 0) {
    LOG_PERROR("waitpid");
    free(stack);
    close(kq);
    return -1;
  }

  free(stack);
  close(kq);

  // The proc is now in the STOP state, with the instruction pointer pointing
  // at the libkernel entry. Let the kernel assign process parameters accessed
  // via sceKernelGetProcParam()
  if(pt_syscall(pid, 599, 0, 0, 0, 0, 0, 0)) {
    LOG_PT_PERROR(pid, "sys_dynlib_process_needed_and_relocate");
    pt_detach(pid, SIGKILL);
    return -1;
  }

  // Allow libc to allocate arbitrary amount of memory.
  elfldr_set_heap_size(pid, -1);

  //Insert a breakpoint at the eboot entry.
  if(!(brkpoint=kernel_dynlib_entry_addr(pid, 0))) {
    LOG_PUTS("kernel_dynlib_entry_addr failed");
    pt_detach(pid, SIGKILL);
    return -1;
  }
  brkpoint += 58;// offset to invocation of main()

  if(kernel_mprotect(pid, brkpoint, PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC)) {
    LOG_PUTS("kernel_mprotect failed");
    pt_detach(pid, SIGKILL);
    return -1;
  }

  if(pt_copyout(pid, brkpoint, &orginstr, sizeof(orginstr))) {
    LOG_PERROR("pt_copyout");
    pt_detach(pid, SIGKILL);
    return -1;
  }
  if(pt_copyin(pid, &int3instr, brkpoint, sizeof(int3instr))) {
    LOG_PERROR("pt_copyin");
    pt_detach(pid, SIGKILL);
    return -1;
  }

  // Continue execution until we hit the breakpoint, then remove it.
  if(pt_continue(pid, SIGCONT)) {
    LOG_PERROR("pt_continue");
    pt_detach(pid, SIGKILL);
    return -1;
  }
  if(waitpid(pid, 0, 0) == -1) {
    LOG_PERROR("waitpid");
    pt_detach(pid, SIGKILL);
    return -1;
  }
  if(pt_copyin(pid, &orginstr, brkpoint, sizeof(orginstr))) {
    LOG_PERROR("pt_copyin");
    pt_detach(pid, SIGKILL);
    return -1;
  }

  // Execute the ELF
  elfldr_set_procname(pid, argv[0]);
  if(elfldr_exec(pid, stdio, elf)) {
    kill(pid, SIGKILL);
    return -1;
  }

  return pid;
}


/**
 * Fint the pid of a process with the given name.
 **/
pid_t
elfldr_find_pid(const char* name) {
  int mib[4] = {1, 14, 8, 0};
  pid_t mypid = getpid();
  pid_t pid = -1;
  size_t buf_size;
  uint8_t *buf;

  if(sysctl(mib, 4, 0, &buf_size, 0, 0)) {
    LOG_PERROR("sysctl");
    return -1;
  }

  if(!(buf=malloc(buf_size))) {
    LOG_PERROR("malloc");
    return -1;
  }

  if(sysctl(mib, 4, buf, &buf_size, 0, 0)) {
    LOG_PERROR("sysctl");
    free(buf);
    return -1;
  }

  for(uint8_t *ptr=buf; ptr<(buf+buf_size);) {
    int ki_structsize = *(int*)ptr;
    pid_t ki_pid = *(pid_t*)&ptr[72];
    char *ki_tdname = (char*)&ptr[447];

    ptr += ki_structsize;
    if(!strcmp(name, ki_tdname) && ki_pid != mypid) {
      pid = ki_pid;
    }
  }

  free(buf);

  return pid;
}


/**
 * Read an ELF from a given socket.
 **/
int
elfldr_read(int fd, uint8_t** elf, size_t* elf_size) {
  Elf64_Shdr *shdr;
  Elf64_Ehdr ehdr;
  uint8_t* buf;
  uint8_t* bak;
  size_t size;
  off_t shend;
  size_t rem;

  if(recv(fd, &ehdr, sizeof(ehdr), MSG_WAITALL) != sizeof(ehdr)) {
    return -1;
  }

  if(ehdr.e_ident[0] != 0x7f || ehdr.e_ident[1] != 'E' ||
     ehdr.e_ident[2] != 'L'  || ehdr.e_ident[3] != 'F') {
    errno = ENOEXEC;
    return -1;
  }

  size = ehdr.e_shoff + ehdr.e_shnum * sizeof(Elf64_Ehdr);
  if(!(buf=malloc(size))) {
    return -1;
  }

  memcpy(buf, &ehdr, sizeof(ehdr));
  rem = size - sizeof(ehdr);
  if(recv(fd, buf + sizeof(ehdr), rem, MSG_WAITALL) != rem) {
    free(buf);
    return -1;
  }

  shend = 0;
  shdr = (Elf64_Shdr*)(buf + ehdr.e_shoff);
  for(int i=0; i<ehdr.e_shnum; i++) {
    if(shdr[i].sh_type == SHT_NOBITS) {
      continue;
    }
    size_t end = shdr[i].sh_offset + shdr[i].sh_size;
    if(end > shend) {
      shend = end;
    }
  }

  // sections appear before section headers
  if(shend <= size) {
    *elf = buf;
    *elf_size = size;
    return 0;
  }

  bak = buf;
  if(!(buf=realloc(buf, shend))) {
    free(bak);
    return -1;
  }

  rem = shend - size;
  if(recv(fd, buf + size, rem, MSG_WAITALL) != rem) {
    free(buf);
    return -1;
  }

  *elf = buf;
  *elf_size = shend;

  return 0;
}
#endif /* !VSHSIG_INJECTION_HOST_TEST */
