# Vendored elfldr notice

This directory contains code from
[ps5-payload-dev/elfldr](https://github.com/ps5-payload-dev/elfldr)
by John Törnblom.

Copyright (C) 2024 John Törnblom.
Licensed under the GNU General Public License, version 3 or (at your option)
any later version. See [LICENSE](../../LICENSE). The original copyright,
license and warranty notices are retained in the source files.

## ps5-syslang modifications — 2026-10-18

The vendored copy has been modified for ps5-syslang:

- elfldr.c and elfldr.h: preserved-thread hook execution and resource cleanup.
- pt.c and pt.h: bounded remote operations and verified event-wait selection.
- log.h: compile-time logging switch, disabled in default builds.
- thread_bootstrap.S and thread_bootstrap.h: project additions for hook entry
  and interrupted-thread state preservation, also GPL-3.0-or-later.

All modified upstream files retain their original copyright and license notices.
