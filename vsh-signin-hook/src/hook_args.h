// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>

/* Resolve SceShellUI's existing Mono (libmonosgen-2.0.sprx) exports in the
 * loader, without starting a second SDK runtime or installing kernel-access
 * descriptors in ShellUI. Same export set ps5-syslang resolves for the same
 * purpose (it already proved these names/signatures work against this
 * Mono build). */
#define VSHSIG_MONO_EXPORTS(X) \
  X(mono_get_root_domain) \
  X(mono_domain_get) \
  X(mono_thread_attach) \
  X(mono_thread_detach) \
  X(mono_domain_assembly_open) \
  X(mono_assembly_get_image) \
  X(mono_class_from_name) \
  X(mono_class_get_method_from_name) \
  X(mono_runtime_invoke) \
  X(mono_gchandle_new) \
  X(mono_gchandle_free) \
  X(mono_object_get_class) \
  X(mono_class_get_name) \
  X(mono_class_get_field_from_name) \
  X(mono_field_get_value) \
  X(mono_class_get_type) \
  X(mono_type_get_object) \
  X(mono_object_unbox)

struct vshsig_hook_args {
  uintptr_t format;
  uintptr_t debug_out;
#define VSHSIG_EXPORT_FIELD(name) uintptr_t name;
  VSHSIG_MONO_EXPORTS(VSHSIG_EXPORT_FIELD)
#undef VSHSIG_EXPORT_FIELD
};
