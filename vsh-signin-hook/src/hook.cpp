// SPDX-License-Identifier: GPL-3.0-or-later
//
// vsh_signin_diag hook (v3, READ-ONLY): v2 guessed 7 namespaces for
// VshNMPsnAccountModule in the two DLLs we already know exist
// (Sce.Vsh.ShellUI.ReactNativeShellApp.dll, ReactNative.PUI.dll) and found
// none of them -- confirmed on three separate runs, so the namespace-guess
// strategy is exhausted, not just unlucky. The klog itself gives a concrete
// clue the guesses missed: "Type:ReactNative.Vsh.Common.AppScene" and
// "ReactNative.Views.UI3.View.ReactModalScene" show the real top-level
// namespace tree is "ReactNative.Vsh.*" / "ReactNative.Views.*", not
// "ReactNative.Modules.ShellUI.*" for everything.
//
// Rather than keep guessing namespace strings, this version reads the
// TypeDef metadata table directly (mono_image_get_table_info +
// mono_table_info_get_rows + mono_class_get, all plain ECMA-335/Mono
// metadata APIs -- MONO_TABLE_TYPEDEF=2 and the 0x02000000 TypeDef token
// tag are fixed values from the CLI spec, not version-fragile) and walks
// EVERY class actually defined in each assembly, logging the ones whose
// name or namespace contains a short account/sign-in-related substring.
// This finds VshNMPsnAccountModule's real namespace regardless of what it
// is, and as a side effect maps out the real "ReactNative.Vsh.*" tree for
// next time. Still nothing is invoked or modified -- this only reads
// metadata.
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

/* No <string.h> from the SDK's libc inside this freestanding hook. */
static bool contains_ci(const char *haystack, const char *needle)
{
  if (!haystack || !needle || !*needle) return false;
  for (const char *h = haystack; *h; ++h) {
    const char *a = h;
    const char *b = needle;
    while (*a && *b) {
      char ca = *a >= 'A' && *a <= 'Z' ? *a + 32 : *a;
      char cb = *b >= 'A' && *b <= 'Z' ? *b + 32 : *b;
      if (ca != cb) break;
      ++a; ++b;
    }
    if (!*b) return true;
  }
  return false;
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
struct MonoTableInfo;

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
static const char *(*mono_class_get_namespace)(MonoClass *);
static const MonoTableInfo *(*mono_image_get_table_info)(MonoImage *, int);
static int (*mono_table_info_get_rows)(const MonoTableInfo *);
static MonoClass *(*mono_class_get)(MonoImage *, uint32_t);

#define LOG_PREFIX "[vsh-signin-diag]"
#define MONO_TABLE_TYPEDEF 2
#define MONO_TOKEN_TYPE_DEF 0x02000000u

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
         mono_class_get_fields && mono_field_get_name &&
         mono_class_get_namespace && mono_image_get_table_info &&
         mono_table_info_get_rows && mono_class_get;
}

/* Logs every method and field name of klass (pure reflection metadata --
 * nothing is invoked, nothing is read from any object). */
static void dump_class(MonoClass *klass, const char *label)
{
  log_line("%s: full name=%s.%s", label, mono_class_get_namespace(klass), mono_class_get_name(klass));

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

/* v6 pivot: v4+v5 exhaustively scanned 25 assemblies (thousands of classes)
 * for an exact "VshNMPsnAccountModule" match and found none -- that name is
 * almost certainly NiDUS's own internal alias for the widget, not the real
 * PS5 C# class name. v5's substring scan did surface real, concrete
 * sign-in-related classes though: a dedicated "Bridge" namespace in
 * AppSystem (Bridge.LoginMgrUtil / Bridge.ILoginMgrUtil -- exactly the
 * shape of a native<->React-Native bridge), plus LoginMgrUtil/
 * LoginMgrUtilInternal/CheckSigninStateJob in Legacy and
 * BigAppCompanion.WaitActiveSigninScreen in ReactNativeApps. v6 fully dumps
 * (every method + every field, pure reflection, nothing invoked) all of
 * those plus every class under any "*Bridge*" namespace, instead of
 * chasing the literal name further. */
static bool str_eq(const char *a, const char *b)
{
  if (!a) a = "";
  if (!b) b = "";
  while (*a && *b && *a == *b) { ++a; ++b; }
  return *a == 0 && *b == 0;
}

struct deep_dump_target { const char *ns; const char *name; };
static const deep_dump_target deep_dump_targets[] = {
    {"Sce.Vsh.ShellUI", "LoginMgrUtil"},
    {"Sce.Vsh.ShellUI", "LoginMgrUtilInternal"},
    {"", "CheckSigninStateJob"},
    {"Sce.Vsh.ShellUI.BigAppCompanion", "WaitActiveSigninScreen"},
    {"Sce.Vsh.ShellUI.LoginMgrProxy", "PsnAccountInfo"},
};

/* Walks every TypeDef row in image (the ground-truth class list -- no
 * namespace guessing), logging any class whose namespace or name contains
 * one of needles. Dumps the full method/field list for an exact name match
 * on "VshNMPsnAccountModule", for any class in deep_dump_targets, and for
 * any class under a namespace containing "Bridge". Returns the exact-match
 * count for "VshNMPsnAccountModule" specifically (deep-dump hits are
 * logged but counted separately and don't affect this return value). */
static int scan_image(MonoImage *image, const char *dll_label)
{
  const MonoTableInfo *table = mono_image_get_table_info(image, MONO_TABLE_TYPEDEF);
  if (!table) {
    log_line("%s: no TypeDef table", dll_label);
    return 0;
  }
  int rows = mono_table_info_get_rows(table);
  log_line("%s: %d TypeDef row(s)", dll_label, rows);

  /* Narrower than v3/v4's filter (dropped "Vsh"/"Login"/"Auth", too common
   * in a Vsh-everything DLL and just flooded klog with 200 near-misses
   * last run) -- the exact-name check below runs over every row regardless
   * of this list, so narrowing it only quiets the candidate listing. */
  static const char *const needles[] = {
      "Account", "Psn", "SignIn", "Signin", "Nm",
  };

  int exact_matches = 0;
  int deep_dumps = 0;
  int logged = 0;
  for (int row = 0; row < rows; ++row) {
    uint32_t token = MONO_TOKEN_TYPE_DEF | (uint32_t)(row + 1);
    MonoClass *klass = mono_class_get(image, token);
    if (!klass) continue;
    const char *name = mono_class_get_name(klass);
    const char *ns = mono_class_get_namespace(klass);
    if (!name) continue;

    bool interesting = false;
    for (const char *needle : needles) {
      if (contains_ci(name, needle) || contains_ci(ns, needle)) { interesting = true; break; }
    }
    if (!interesting) continue;

    if (logged < 200) {
      log_line("%s: %s.%s", dll_label, ns ? ns : "", name);
      logged++;
    }

    if (str_eq(name, "VshNMPsnAccountModule")) {
      exact_matches++;
      log_line("%s: EXACT MATCH for VshNMPsnAccountModule at TypeDef row %d (token=%#x)", dll_label, row, token);
      dump_class(klass, dll_label);
      continue;
    }

    bool deep_dump = contains_ci(ns, "Bridge");
    if (!deep_dump) {
      for (const auto &t : deep_dump_targets) {
        if (str_eq(ns, t.ns) && str_eq(name, t.name)) { deep_dump = true; break; }
      }
    }
    if (deep_dump) {
      deep_dumps++;
      log_line("%s: DEEP DUMP target at TypeDef row %d (token=%#x)", dll_label, row, token);
      dump_class(klass, dll_label);
    }
  }
  log_line("%s: scan complete, %d candidate(s) logged, %d deep dump(s), %d exact match(es)", dll_label, logged, deep_dumps, exact_matches);
  return exact_matches;
}

static int probe_assembly(MonoDomain *domain, const char *dll_path, const char *dll_label)
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
  return scan_image(image, dll_label);
}

static int run_hook(void)
{
  log_line("hook v6 starting (read-only diagnostic, deep-dump Bridge.* + known signin leads, 6 new candidate DLLs)");

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

  /* v5 exhaustively scanned 25 assemblies total (v4's 10 + v5's 13 + the
   * original 2) -- thousands of classes -- for an exact
   * "VshNMPsnAccountModule" match and found zero. That name is almost
   * certainly NiDUS's own internal alias for the widget, not a real PS5
   * class name, so v6 stops chasing it as a literal string. Instead it
   * re-opens the three assemblies where v5's substring scan already
   * surfaced concrete sign-in-related classes -- AppSystem (has a
   * dedicated "Bridge" namespace: Bridge.LoginMgrUtil / Bridge.ILoginMgrUtil,
   * exactly the native<->React-Native bridge shape), Legacy (LoginMgrUtil,
   * LoginMgrUtilInternal, CheckSigninStateJob), ReactNativeApps
   * (BigAppCompanion.WaitActiveSigninScreen) -- and fully dumps those
   * classes' methods/fields via deep_dump_targets[] and the "*Bridge*"
   * namespace check in scan_image(). Also trying a handful of still-untried
   * ShellUI screen DLLs from the directory listing in case the real class
   * is there instead. */
  static const struct { const char *path; const char *label; } candidates[] = {
      {"/system_ex/common_ex/lib/Sce.Vsh.ShellUI.AppSystem.dll", "Sce.Vsh.ShellUI.AppSystem"},
      {"/system_ex/common_ex/lib/Sce.Vsh.ShellUI.Legacy.dll", "Sce.Vsh.ShellUI.Legacy"},
      {"/system_ex/common_ex/lib/Sce.Vsh.ShellUI.ReactNativeApps.dll", "Sce.Vsh.ShellUI.ReactNativeApps"},
      {"/system_ex/common_ex/lib/Sce.Vsh.ShellUI.BGLayer.dll", "Sce.Vsh.ShellUI.BGLayer"},
      {"/system_ex/common_ex/lib/Sce.Vsh.ShellUI.SystemModalDialog.dll", "Sce.Vsh.ShellUI.SystemModalDialog"},
      {"/system_ex/common_ex/lib/Sce.Vsh.ShellUI.Broadcast.dll", "Sce.Vsh.ShellUI.Broadcast"},
      {"/system_ex/common_ex/lib/Sce.Vsh.ShellUI.Theme.dll", "Sce.Vsh.ShellUI.Theme"},
      {"/system_ex/common_ex/lib/Sce.Vsh.ShellUI.Ugc.dll", "Sce.Vsh.ShellUI.Ugc"},
      {"/system_ex/common_ex/lib/Sce.Vsh.ShellUI.VoiceAndAgent.dll", "Sce.Vsh.ShellUI.VoiceAndAgent"},
  };

  int total_found = 0;
  for (const auto &c : candidates) {
    total_found += probe_assembly(domain, c.path, c.label);
  }

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
