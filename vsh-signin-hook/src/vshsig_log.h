// SPDX-License-Identifier: GPL-3.0-or-later
// Logging switch for the vendored elfldr/pt code (modeled on ps5-syslang's
// syslang_log.h, same idea: compile-time toggle, disabled in default builds).
#pragma once

#ifndef VSHSIG_LOGGING
#define VSHSIG_LOGGING 0
#endif

#if VSHSIG_LOGGING
#define VSHSIG_LOG(...) printf(__VA_ARGS__)
#define VSHSIG_KLOG(...) klog_printf(__VA_ARGS__)
#else
#define VSHSIG_LOG(...) ((void)0)
#define VSHSIG_KLOG(...) ((void)0)
#endif
