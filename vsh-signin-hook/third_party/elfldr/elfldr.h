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

#pragma once

#include <stdint.h>
#include <unistd.h>


/**
 * Find the id of a process with the given name.
 **/
pid_t elfldr_find_pid(const char* name);


/**
 * Spawn a new process that executes the given ELF file.
 **/
pid_t elfldr_spawn(int stdio, char* const argv[], uint8_t *elf, size_t elf_size);


/**
 * Execute an ELF file in a given process.
 **/
int elfldr_exec(pid_t pid, int stdio, uint8_t* elf);

/* Protect the interrupted frame's red zone and restore its CPU state. */
struct elfldr_injection {
  intptr_t image, args, stack, bootstrap, status_addr;
  size_t image_size;
  uint64_t original_rsp;
  int thread_id;
};
struct vshsig_hook_args;
/* Resolve exports before stopping the UI. */
int elfldr_resolve_mono_args(pid_t pid, struct vshsig_hook_args *args);
int elfldr_exec_preserved(pid_t pid, uint8_t *elf, const struct vshsig_hook_args *args,
                          struct elfldr_injection *injection);
/* Reclaim only after the same thread has returned to its original wait frame. */
int elfldr_cleanup_preserved(pid_t pid, struct elfldr_injection *injection);


/**
 * Read an ELF from the given socket.
 **/
int elfldr_read(int fd, uint8_t** elf, size_t* elf_size);


int elfldr_sanity_check(uint8_t *elf, size_t elf_size);


int elfldr_raise_privileges(pid_t pid);
