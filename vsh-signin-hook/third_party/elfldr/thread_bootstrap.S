// SPDX-License-Identifier: GPL-3.0-or-later
#include "thread_bootstrap.h"
.intel_syntax noprefix
.text
.global vshsig_bootstrap_start
.global vshsig_bootstrap_end
.global vshsig_bootstrap_rip
.global vshsig_bootstrap_state
vshsig_bootstrap_start:
  mov rbx, rdi
  xor ecx, ecx
  xgetbv
  and eax, 7
  xor edx, edx
  mov [rbx + BOOT_XMASK], rax
  xsave64 [rbx + BOOT_XSTATE]
  cld

  // Managed calls must stay within Mono's registered original thread stack.
  // Reserve the interrupted frame's complete 128-byte red zone first.
  mov rsp, [rbx + 120]
  sub rsp, 128
  and rsp, -16
  mov rdi, [rbx + BOOT_ARGS]
  call [rbx + BOOT_ENTRY]
  // Reload the state pointer before restoring the interrupted context.
  mov rbx, [rip + vshsig_bootstrap_state]
  // The freestanding hook returns its result through the normal C ABI.
  mov [rbx + BOOT_STATUS], eax

  mov eax, [rbx + BOOT_XMASK]
  xor edx, edx
  xrstor64 [rbx + BOOT_XSTATE]
  push qword ptr [rbx + 128]
  popfq
  mov r15, [rbx + 0]
  mov r14, [rbx + 8]
  mov r13, [rbx + 16]
  mov r12, [rbx + 24]
  mov r11, [rbx + 32]
  mov r10, [rbx + 40]
  mov r9, [rbx + 48]
  mov r8, [rbx + 56]
  mov rdi, [rbx + 64]
  mov rsi, [rbx + 72]
  mov rbp, [rbx + 80]
  mov rdx, [rbx + 96]
  mov rcx, [rbx + 104]
  mov rax, [rbx + 112]
  mov rsp, [rbx + 120]
  mov rbx, [rbx + 88]
  jmp qword ptr [rip + vshsig_bootstrap_rip]
.balign 8
vshsig_bootstrap_rip:
  .quad 0
vshsig_bootstrap_state:
  .quad 0
vshsig_bootstrap_end:
.section .note.GNU-stack,"",@progbits
