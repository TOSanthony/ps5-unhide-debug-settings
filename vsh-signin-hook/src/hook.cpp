// SPDX-License-Identifier: GPL-3.0-or-later
//
// vsh_signin_diag hook (v1, READ-ONLY): runs inside SceShellUI's Mono domain
// (injected the same way ps5-syslang injects its LanguageModule hook -- see
// README.md) and finds the live VshNMPsnAccountModule instance that backs
// the Welcome Hub's IsSignedIn/GetState calls, by walking the same object
// graph hub-signin-patch's widget.js reaches from the JS side, but from the
// native/Mono side instead: GetAllRunningApps() -> each app's
// _reactContext -> NMRegistry -> _moduleInstances (a Dictionary<Type,object>
// of every NativeModule live in that app), which this enumerates entry by
// entry logging each value's class name.
//
// It does NOT call IsSignedIn/GetState/any module method, and does NOT
// write to any field: those methods' real argument count (a Promise
// parameter, as ChangeSystemLanguage(int,Promise) takes, or something else
// entirely) is not yet confirmed, and mono_runtime_invoke does not check
// arity -- calling it with a guessed wrong arg count is an out-of-bounds
// read, a real crash risk, not merely a "might fail safely" guess. This
// pass only uses methods whose 0-argument signature is fixed by the .NET
// BCL itself (IEnumerator.MoveNext/Current, KeyValuePair.Key/Value,
// List<T>.get_Count/get_Item used the same way by ps5-syslang's own
// find_live_language) -- so finding the module's live address is proven
// safe; calling its sign-in methods is the next step, once this confirms
// where it lives and (from a follow-up read of its fields) what it expects.
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

/* Same reasoning: no <string.h> from the SDK's libc inside this hook. */
static bool name_is(const char *a, const char *b)
{
  if (!a || !b) return false;
  size_t i = 0;
  for (; a[i] && b[i]; ++i)
    if (a[i] != b[i]) return false;
  return a[i] == b[i];
}

struct MonoDomain;
struct MonoAssembly;
struct MonoImage;
struct MonoClass;
struct MonoMethod;
struct MonoObject;
struct MonoThread;
struct MonoClassField;
struct MonoType;

static MonoDomain *g_root_domain;
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
         mono_type_get_object && mono_object_unbox;
}

static MonoObject *read_reference(MonoObject *object, const char *name)
{
  if (!object) return nullptr;
  MonoClassField *field = mono_class_get_field_from_name(mono_object_get_class(object), name);
  MonoObject *value = nullptr;
  if (field) mono_field_get_value(object, field, &value);
  return value;
}

static MonoObject *call_object(MonoClass *klass, MonoObject *object,
                               const char *name, int argc, void **args = nullptr)
{
  MonoMethod *method = mono_class_get_method_from_name(klass, name, argc);
  if (!method) {
    log_line("method missing: %s::%s", mono_class_get_name(klass), name);
    return nullptr;
  }
  MonoObject *exception = nullptr;
  MonoObject *result = mono_runtime_invoke(method, object, args, &exception);
  if (exception) {
    log_line("%s exception: %s", name, mono_class_get_name(mono_object_get_class(exception)));
    return nullptr;
  }
  return result;
}

/* Walks one app's NMRegistry._moduleInstances (Dictionary<Type,object>) and
 * logs every live module's class name, flagging VshNMPsnAccountModule. All
 * calls below (GetEnumerator/MoveNext/get_Current/get_Key/get_Value) are
 * fixed 0-argument BCL signatures, same safety class as the get_Count/
 * get_Item calls ps5-syslang's own find_live_language already relies on. */
static int scan_module_dictionary(MonoObject *modules, int app_index)
{
  if (!modules) return 0;
  MonoClass *dict_class = mono_object_get_class(modules);
  MonoObject *enumerator = call_object(dict_class, modules, "GetEnumerator", 0);
  if (!enumerator) {
    log_line("app[%d]: _moduleInstances has no GetEnumerator -- unexpected shape", app_index);
    return 0;
  }
  uint32_t enum_handle = mono_gchandle_new(enumerator, 1);
  MonoClass *enum_class = mono_object_get_class(enumerator);

  int count = 0;
  int found = 0;
  for (int guard = 0; guard < 4096; ++guard) {
    MonoObject *more = call_object(enum_class, enumerator, "MoveNext", 0);
    if (!more || !*(uint8_t *)mono_object_unbox(more)) break;

    MonoObject *entry = call_object(enum_class, enumerator, "get_Current", 0);
    if (!entry) continue;
    MonoClass *entry_class = mono_object_get_class(entry);
    MonoObject *value = call_object(entry_class, entry, "get_Value", 0);
    if (!value) continue;

    const char *class_name = mono_class_get_name(mono_object_get_class(value));
    count++;
    if (name_is(class_name, "VshNMPsnAccountModule")) {
      log_line("app[%d] module #%d: %s  <-- MATCH, live instance=%p", app_index, count, class_name, (void *)value);
      found++;
    } else {
      log_line("app[%d] module #%d: %s", app_index, count, class_name ? class_name : "(unnamed)");
    }
  }
  mono_gchandle_free(enum_handle);
  log_line("app[%d]: %d module(s) enumerated, %d match(es) for VshNMPsnAccountModule", app_index, count, found);
  return found;
}

static int run_hook(void)
{
  log_line("hook starting (read-only diagnostic)");

  if (!resolve_mono()) {
    log_line("not running in SceShellUI");
    return 1;
  }

  MonoDomain *current_domain = mono_domain_get();
  MonoDomain *root_domain = mono_get_root_domain();
  log_line("Mono domains: current=%p root=%p", current_domain, root_domain);
  g_root_domain = current_domain ? current_domain : root_domain;
  if (!g_root_domain) {
    log_line("Mono domain not found");
    return 1;
  }
  if (!current_domain && !mono_thread_attach(g_root_domain)) {
    log_line("Mono attach failed");
    return 1;
  }

  MonoAssembly *assembly = mono_domain_assembly_open(
      g_root_domain, "/system_ex/common_ex/lib/ReactNative.PUI.dll");
  MonoImage *image = assembly ? mono_assembly_get_image(assembly) : nullptr;
  MonoClass *app_class = image ? mono_class_from_name(image, "ReactNative.PUI", "PUIReactApp") : nullptr;
  if (!app_class) {
    log_line("PUIReactApp not found");
    return 1;
  }

  MonoObject *apps = call_object(app_class, nullptr, "GetAllRunningApps", 0);
  if (!apps) {
    log_line("GetAllRunningApps failed");
    return 1;
  }
  uint32_t apps_handle = mono_gchandle_new(apps, 1);
  MonoClass *list_class = mono_object_get_class(apps);
  MonoObject *count_box = call_object(list_class, apps, "get_Count", 0);
  int app_count = count_box ? *(int *)mono_object_unbox(count_box) : 0;
  log_line("running React apps=%d", app_count);

  int total_found = 0;
  for (int index = 0; index < app_count && index < 64; ++index) {
    void *item_args[] = {&index};
    MonoObject *app = call_object(list_class, apps, "get_Item", 1, item_args);
    MonoObject *context = read_reference(app, "_reactContext");
    MonoObject *registry = context ? call_object(mono_object_get_class(context), context,
                                                "get_NMRegistry", 0) : nullptr;
    MonoObject *modules = read_reference(registry, "_moduleInstances");
    log_line("app[%d]: context=%p registry=%p moduleInstances=%p", index, context, registry, modules);
    total_found += scan_module_dictionary(modules, index);
  }
  mono_gchandle_free(apps_handle);

  log_line("done: %d total match(es) for VshNMPsnAccountModule across %d app(s)", total_found, app_count);
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
