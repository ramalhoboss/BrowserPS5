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

// A title's anonymous memory from direct memory (title_memory.c).

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

void* title_memory_mmap(void* address, size_t length, int protection, int flags, int fd, off_t offset);

typedef struct title_memory_stats {
  size_t direct_bytes;   // direct memory mapped now
  size_t ranges;         // mapped ranges in the table
  size_t peak_bytes;     // the most direct memory mapped at once
  size_t fallbacks;      // anonymous mappings left to the kernel's mmap
  size_t alloc_failures; // sceKernelAllocateDirectMemory refused
  size_t map_failures;   // sceKernelMapDirectMemory refused or moved
  size_t table_full;     // no room in the table
  size_t joined;         // mappings joined to a neighbour's range
} title_memory_stats_t;

void title_memory_stats(title_memory_stats_t* stats);

#ifdef __cplusplus
}
#endif
