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

// C flavour of the memory-extra API, used by libpas
// (Source/bmalloc/libpas/src/libpas/pas_compact_heap_reservation.c).
// Same semantics as memory-extra/vss.h; implemented in src/vss.cpp.

#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void* memory_extra_vss_reserve(size_t bytes, size_t alignment);
bool  memory_extra_vss_commit(void* addr, size_t bytes, bool writable,
                              int usage);
bool  memory_extra_vss_decommit(void* addr, size_t bytes);
bool  memory_extra_vss_release(void* addr, size_t bytes);
bool  memory_extra_vss_protect(void* addr, size_t bytes, bool readable,
                               bool writable, int usage);

#ifdef __cplusplus
}
#endif
