// SPDX-License-Identifier: GPL-3.0-or-later
//
// vsh_signin_diag loader: finds SceShellUI, injects the embedded read-only
// hook (hook.cpp) with elfldr_exec_preserved (same preserved-thread
// injection ps5-syslang uses for its LanguageModule hook), waits for it to
// finish, and reports success/failure as a system notification.
//
// The hook's own detailed output (every NativeModule class name found, and
// any VshNMPsnAccountModule match) goes to the kernel debug log, not to a
// file: a hook injected this way has no CRT and so cannot safely fopen() --
// see hook.cpp's comment on why it avoids the SDK runtime entirely. To read
// that output, run this with a klog listener already attached to the
// console on port 3232 (the same port vda_probe.c and wh_signin_probe.elf's
// klog_printf lines already go to) *before* sending this loader.
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <ps5/kernel.h>
#include <ps5/klog.h>

#include "elfldr.h"
#include "pt.h"
#include "hook_args.h"
#include "vshsig_log.h"

extern unsigned char shellui_hook_elf[];
extern unsigned int shellui_hook_elf_len;
extern int sceKernelSendNotificationRequest(int device, void *request, size_t size, int blocking);

#define NOTIFY_PREFIX "vsh-signin-diag:"

typedef struct NotifyRequest {
  char reserved[45];
  char message[3075];
} NotifyRequest;

static void notify(const char *fmt, ...)
{
  NotifyRequest request;
  char message[sizeof(request.message)];
  va_list ap;

  memset(&request, 0, sizeof(request));
  va_start(ap, fmt);
  vsnprintf(message, sizeof(message), fmt, ap);
  va_end(ap);

  snprintf(request.message, sizeof(request.message), NOTIFY_PREFIX " %s", message);
  sceKernelSendNotificationRequest(0, &request, sizeof(request), 0);
}

/* v3's full TypeDef scan ruled out VshNMPsnAccountModule being in either
 * Sce.Vsh.ShellUI.ReactNativeShellApp.dll or ReactNative.PUI.dll -- but it
 * did surface a real ReactNative.Modules.ShellUI.LoginMgr.* namespace
 * family (LoginMgrModule, SigninUtility, etc.) living inside
 * ReactNativeShellApp.dll. The account/sign-in module is most likely in a
 * third assembly we haven't opened yet. Rather than guess a DLL name to
 * feed Mono, just list what's actually in the directory both known DLLs
 * live in -- this needs no injection, no ptrace, nothing but a plain
 * directory read with the loader's own filesystem access (same level
 * other probes in this repo already use to fopen() their own logs). */
static void list_shellui_lib_dir(void)
{
  static const char *const dir_path = "/system_ex/common_ex/lib";
  DIR *dir = opendir(dir_path);
  if (!dir) {
    klog_printf("[vsh-signin-diag-loader] opendir(%s) failed\n", dir_path);
    return;
  }
  int count = 0;
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (entry->d_name[0] == '.') continue;
    klog_printf("[vsh-signin-diag-loader] lib entry: %s\n", entry->d_name);
    count++;
  }
  closedir(dir);
  klog_printf("[vsh-signin-diag-loader] %s: %d entries\n", dir_path, count);
}

int main(void)
{
  setvbuf(stdout, NULL, _IONBF, 0);

  list_shellui_lib_dir();

  if (elfldr_sanity_check(shellui_hook_elf, shellui_hook_elf_len)) {
    notify("embedded hook ELF is invalid");
    return 1;
  }

  pid_t pid = elfldr_find_pid("SceShellUI");
  if (pid < 0) {
    notify("SceShellUI not found");
    return 1;
  }

  struct vshsig_hook_args hook_args = {0};
  if (elfldr_resolve_mono_args(pid, &hook_args)) {
    notify("could not resolve ShellUI Mono exports");
    return 1;
  }
  VSHSIG_LOG("[vsh-signin-diag-loader] attaching pid=%d\n", pid);
  if (pt_attach_event_wait(pid, 5000)) {
    notify("could not acquire a verified ShellUI wait site");
    return 1;
  }
  VSHSIG_LOG("[vsh-signin-diag-loader] attached pid=%d\n", pid);

  struct elfldr_injection injection = {0};
  if (elfldr_exec_preserved(pid, shellui_hook_elf, &hook_args, &injection)) {
    notify("preserved injection failed");
    return 1;
  }

  int status = -1;
  for (int i = 0; i < 100 && status == -1; ++i) {
    if (kernel_proc_copyout(pid, injection.status_addr, &status, sizeof(status))) {
      notify("failed to read hook completion status");
      return 1;
    }
    if (status == -1) {
      usleep(50000);
    }
  }
  VSHSIG_LOG("[vsh-signin-diag-loader] pid=%d hook_result=%#x\n", pid, status);
  if (status != -1 && elfldr_cleanup_preserved(pid, &injection)) {
    notify("hook returned, but resource cleanup failed");
    return 1;
  }

  if (status == -1) {
    notify("hook timed out");
    return 1;
  } else if (status == 0) {
    notify("done -- VshNMPsnAccountModule found, see klog port 3232");
  } else if (status == 2) {
    notify("done -- ran, but no VshNMPsnAccountModule match, see klog port 3232");
  } else {
    notify("hook failed (%#x), see klog port 3232", status);
  }
  return 0;
}
