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

// Open replacement for Sony's private "memory-extra" virtual memory API,
// which WebKit's PlayStation port calls from
// Source/WTF/wtf/playstation/OSAllocatorPlayStation.cpp. The signatures are
// inferred from those call sites; the implementation is plain POSIX mmap.

#pragma once

#include <stddef.h>

namespace memory_extra {
namespace vss {

// Reserve address space without backing memory (PROT_NONE).
void* reserve(size_t bytes, size_t alignment = 0);

// Back [addr, addr+bytes) with memory, readable and optionally writable.
// `usage` is a WebKit OSAllocator::Usage tag; it is only a hint.
bool commit(void* addr, size_t bytes, bool writable, int usage);

// Drop the backing memory but keep the address space reserved.
bool decommit(void* addr, size_t bytes);

// Give the address space back.
bool release(void* addr, size_t bytes);

bool protect(void* addr, size_t bytes, bool readable, bool writable,
             int usage);

} // namespace vss
} // namespace memory_extra
