/* Copyright (C) 2026 BrowserPS5 contributors

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

// Linux's sysinfo(), which WTF expects on OS(FREEBSD) too
// (Source/WTF/wtf/AvailableMemory.cpp and RAMSize.cpp), where FreeBSD gets
// it from the libsysinfo port. The PS5 has neither, so this provides the
// two fields WTF reads, from the kernel's hw.physmem.

#pragma once

#include <string.h>
#include <sys/types.h>
#include <sys/sysctl.h>

struct sysinfo {
  unsigned long totalram;
  unsigned long freeram;
  unsigned int mem_unit;
};

static inline int
sysinfo(struct sysinfo* info) {
  int mib[2] = {CTL_HW, HW_PHYSMEM};
  unsigned long physmem = 0;
  size_t len = sizeof(physmem);

  memset(info, 0, sizeof(*info));
  if(sysctl(mib, 2, &physmem, &len, 0, 0)) {
    return -1;
  }
  info->totalram = physmem;
  info->mem_unit = 1;
  return 0;
}
