// SPDX-License-Identifier: GPL-3.0-or-later
//
// vsh_signin_diag hook (v2, READ-ONLY): v1 tried to find a LIVE
// VshNMPsnAccountModule instance via ReactNative.PUI.PUIReactApp's
// GetAllRunningApps() (the same call ps5-syslang's find_live_language()
// uses) -- but that function is itself only an exploratory/diagnostic path
// in ps5-syslang (SYSLANG_DIAGNOSTIC==3), never its production route. Their
// actual production path (change_system_language) never looks for a live
// instance at all: it just mono_object_new()'s a fresh LanguageModule and
// calls the method directly. Confirming that twice, on two separate runs
// (including one with a settings screen reportedly open), GetAllRunningApps
// returned 0 -- so it is evidently not tracking ShellUI's own built-in
// screens the way we assumed.
//
// This version skips the live-app hunt entirely and goes straight at the
// class's metadata: it tries resolving VshNMPsnAccountModule by name in
// both DLLs the Welcome Hub's bridge code could plausibly live in
// (Sce.Vsh.ShellUI.ReactNativeShellApp.dll, where LanguageModule lives, and
// ReactNative.PUI.dll) across a handful of candidate namespaces, and for
// whichever resolves, lists every method and field name via Mono's
// reflection metadata iterators (mono_class_get_methods/get_fields) --
// still read-only, no code is invoked. The goal: confirm the class exists
// natively at all, in which assembly/namespace, and what its real
// IsSignedIn/GetState signatures and backing fields look like, before ever
// trying to call or mutate anything.
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include "hook_args.h"
#include "vshsig_log.h"

/* Compiler-generated aggregate copies must not pull in an SDK runtime. */
extern "C" void *memcpy(void *destination, const void *source, size_t size)
{
  auto out = static_cast<unsigned char *>(destination);
  auto in = static_cast<const unsigned char *>(source);
  for (size_t i = 0; i < size; ++i) out[i] = in[i];
  return destination;
}

extern "C" void *memset(void *destination, int value, size_t size)
{
  auto out = static_cast<unsigned char *>(destination);
  for (size_t i = 0; i < size; ++i) out[i] = (unsigned char)value;
  return destination;
}

struct MonoDomain;
struct MonoAssembly;
struct MonoImage;
struct MonoClass;
struct MonoMethod;
struct MonoMethodSignature;
struct MonoObject;
struct MonoThread;
struct MonoClassField;
struct MonoType;

static const vshsig_hook_args *g_hook_args;

static MonoDomain *(*mono_get_root_domain)(void);
static MonoDomain *(*mono_domain_get)(void);
static MonoThread *(*mono_thread_attach)(MonoDomain *);
static void (*mono_thread_detach)(MonoThread *);
static MonoAssembly *(*mono_domain_assembly_open)(MonoDomain *, const char *);
static MonoImage *(*mono_assembly_get_image)(MonoAssembly *);
static MonoClass *(*mono_class_from_name)(MonoImage *, const char *, const char *);
static MonoMethod *(*mono_class_get_method_from_name)(MonoClass *, const char *, int);
static MonoObject *(*mono_runtime_invoke)(MonoMethod *, void *, void **, MonoObject **);
static uint32_t (*mono_gchandle_new)(MonoObject *, int);
static void (*mono_gchandle_free)(uint32_t);
static MonoClass *(*mono_object_get_class)(MonoObject *);
static const char *(*mono_class_get_name)(MonoClass *);
static MonoClassField *(*mono_class_get_field_from_name)(MonoClass *, const char *);
static void (*mono_field_get_value)(MonoObject *, MonoClassField *, void *);
static MonoType *(*mono_class_get_type)(MonoClass *); /* resolved, unused by this diagnostic pass */
static MonoObject *(*mono_type_get_object)(MonoDomain *, MonoType *);
static void *(*mono_object_unbox)(MonoObject *);
static MonoMethod *(*mono_class_get_methods)(MonoClass *, void **);
static const char *(*mono_method_get_name)(MonoMethod *);
static MonoMethodSignature *(*mono_method_signature)(MonoMethod *);
static uint32_t (*mono_signature_get_param_count)(MonoMethodSignature *);
static MonoClassField *(*mono_class_get_fields)(MonoClass *, void **);
static const char *(*mono_field_get_name)(MonoClassField *);

#define LOG_PREFIX "[vsh-signin-diag]"

static void log_line(const char *fmt, ...)
{
  char output[512] = LOG_PREFIX " ";
  const size_t prefix = sizeof(LOG_PREFIX " ") - 1;
  va_list ap;
  auto format = reinterpret_cast<int (*)(char *, size_t, const char *, va_list)>(g_hook_args->format);
  auto debug_out = reinterpret_cast<int (*)(int, const char *)>(g_hook_args->debug_out);

  va_start(ap, fmt);
  int length = format(output + prefix, sizeof(output) - prefix - 1, fmt, ap);
  va_end(ap);
  size_t end = length < 0 ? prefix : prefix + (size_t)length;
  if (end > sizeof(output) - 2) end = sizeof(output) - 2;
  output[end++] = '\n';
  output[end] = 0;
  debug_out(0, output);
}

static bool resolve_mono(void)
{
#define RESOLVE_MONO(name) name = reinterpret_cast<decltype(name)>(g_hook_args->name);
  VSHSIG_MONO_EXPORTS(RESOLVE_MONO)
#undef RESOLVE_MONO

  return mono_get_root_domain && mono_domain_get && mono_thread_attach && mono_domain_assembly_open &&
         mono_assembly_get_image && mono_class_from_name &&
         mono_class_get_method_from_name && mono_runtime_invoke && mono_gchandle_new &&
         mono_gchandle_free && mono_thread_detach &&
         mono_object_get_class && mono_class_get_name &&
         mono_class_get_field_from_name && mono_field_get_value &&
         mono_type_get_object && mono_object_unbox &&
         mono_class_get_methods && mono_method_get_name &&
         mono_method_signature && mono_signature_get_param_count &&
         mono_class_get_fields && mono_field_get_name;
}

/* Logs every method and field name of klass (pure reflection metadata --
 * nothing is invoked, nothing is read from any object). */
static void dump_class(MonoClass *klass, const char *label)
{
  log_line("%s: class=%s", label, mono_class_get_name(klass));

  void *method_iter = nullptr;
  int method_count = 0;
  MonoMethod *method;
  while ((method = mono_class_get_methods(klass, &method_iter)) != nullptr) {
    MonoMethodSignature *sig = mono_method_signature(method);
    uint32_t argc = sig ? mono_signature_get_param_count(sig) : 0xffffffffu;
    log_line("%s: method #%d: %s (argc=%u)", label, method_count++, mono_method_get_name(method), argc);
  }
  log_line("%s: %d method(s) total", label, method_count);

  void *field_iter = nullptr;
  int field_count = 0;
  MonoClassField *field;
  while ((field = mono_class_get_fields(klass, &field_iter)) != nullptr) {
    log_line("%s: field #%d: %s", label, field_count++, mono_field_get_name(field));
  }
  log_line("%s: %d field(s) total", label, field_count);
}

/* Tries every (assembly, namespace) combination we can plausibly guess for
 * VshNMPsnAccountModule and dumps whichever one resolves. Returns how many
 * resolved (ideally exactly one -- more would mean the name is ambiguous
 * across assemblies, which is itself useful to know). */
static int probe_class_in(MonoDomain *domain, const char *dll_path, const char *dll_label)
{
  MonoAssembly *assembly = mono_domain_assembly_open(domain, dll_path);
  if (!assembly) {
    log_line("%s: assembly not found", dll_label);
    return 0;
  }
  MonoImage *image = mono_assembly_get_image(assembly);
  if (!image) {
    log_line("%s: no image", dll_label);
    return 0;
  }

  static const char *const candidate_namespaces[] = {
      "ReactNative.Modules.ShellUI.Settings",
      "ReactNative.Modules.ShellUI.Account",
      "ReactNative.Modules.ShellUI.Psn",
      "ReactNative.Modules.ShellUI.Np",
      "ReactNative.Modules.ShellUI",
      "ReactNative.PUI",
      "",
  };

  int found = 0;
  for (const char *ns : candidate_namespaces) {
    MonoClass *klass = mono_class_from_name(image, ns, "VshNMPsnAccountModule");
    if (!klass) continue;
    found++;
    log_line("%s: VshNMPsnAccountModule FOUND in namespace \"%s\"", dll_label, *ns ? ns : "(global)");
    dump_class(klass, dll_label);
  }
  if (!found) {
    log_line("%s: VshNMPsnAccountModule not found in any candidate namespace", dll_label);
  }
  return found;
}

static int run_hook(void)
{
  log_line("hook v2 starting (read-only diagnostic, metadata-only)");

  if (!resolve_mono()) {
    log_line("not running in SceShellUI, or a required Mono export is missing");
    return 1;
  }

  MonoDomain *current_domain = mono_domain_get();
  MonoDomain *root_domain = mono_get_root_domain();
  log_line("Mono domains: current=%p root=%p", current_domain, root_domain);
  MonoDomain *domain = current_domain ? current_domain : root_domain;
  if (!domain) {
    log_line("Mono domain not found");
    return 1;
  }
  if (!current_domain && !mono_thread_attach(domain)) {
    log_line("Mono attach failed");
    return 1;
  }

  int total_found = 0;
  total_found += probe_class_in(domain, "/system_ex/common_ex/lib/Sce.Vsh.ShellUI.ReactNativeShellApp.dll",
                                "ReactNativeShellApp");
  total_found += probe_class_in(domain, "/system_ex/common_ex/lib/ReactNative.PUI.dll", "ReactNative.PUI");

  log_line("done: VshNMPsnAccountModule resolved in %d place(s)", total_found);
  return total_found > 0 ? 0 : 2;
}

/* Ordinary C ABI: no SDK CRT, import loader, heap, or kernel descriptors. */
extern "C" int vshsig_hook_entry(const vshsig_hook_args *args)
{
  if (!args) return 1;
  if (!args->format || !args->debug_out) return 1;
  g_hook_args = args;
  return run_hook();
}
