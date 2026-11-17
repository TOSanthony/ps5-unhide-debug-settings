// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#define BOOT_ENTRY 136
#define BOOT_ARGS 144
#define BOOT_STATUS 168
#define BOOT_XMASK 176
#define BOOT_XSTATE 192

#ifndef __ASSEMBLER__
#include <stddef.h>
#include <stdint.h>

struct thread_bootstrap {
  uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
  uint64_t rdi, rsi, rbp, rbx, rdx, rcx, rax, rsp, rflags;
  uint64_t entry, args, reserved_words[2];
  int32_t status;
  uint32_t reserved;
  uint64_t xmask;
  uint64_t padding;
  unsigned char xstate[1024];
};
_Static_assert(offsetof(struct thread_bootstrap, entry) == BOOT_ENTRY, "entry offset");
_Static_assert(offsetof(struct thread_bootstrap, status) == BOOT_STATUS, "status offset");
_Static_assert(offsetof(struct thread_bootstrap, xstate) == BOOT_XSTATE, "xstate offset");
extern const unsigned char vshsig_bootstrap_start[];
extern const unsigned char vshsig_bootstrap_end[];
extern const unsigned char vshsig_bootstrap_rip[];
extern const unsigned char vshsig_bootstrap_state[];
#endif
