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

// Reservations are libkernel virtual ranges: address space with no memory
// behind it. Inside an app process (the UIProcess) every anonymous mmap
// counts, PROT_NONE too: at most 256 MiB per call and 384 MiB in all, out
// of a 448 MiB flexible memory budget, while JSC and libpas reserve address
// space by the GiB. A virtual range of up to 16 GiB costs nothing; memory
// mapped into it with MAP_FIXED costs what it maps, and a virtual range
// mapped back over that memory gives it back (poc/app_probe, 2026-11-03).
// Where virtual ranges fail, mmap PROT_NONE does the same, as before.

#include <memory-extra.h>
#include <memory-extra/vss.h>

#include <stdint.h>
#include <sys/mman.h>

extern "C" int sceKernelReserveVirtualRange(void** addr, size_t len, int flags,
                                            size_t alignment);

#define SCE_KERNEL_MAP_FIXED 0x10

namespace memory_extra {
namespace vss {

static int
toProt(bool readable, bool writable) {
  return (readable ? PROT_READ : 0) | (writable ? PROT_WRITE : 0);
}


// Its alignment argument refuses large alignments (4 GiB), so callers
// over-reserve and align by hand.
static void*
reserveRange(size_t bytes) {
  void* p = nullptr;

  if(!sceKernelReserveVirtualRange(&p, bytes, 0, 0)) {
    return p;
  }
  p = mmap(0, bytes, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
  return p == MAP_FAILED ? nullptr : p;
}


void*
reserve(size_t bytes, size_t alignment) {
  size_t padded = alignment ? bytes + alignment : bytes;
  void* p = reserveRange(padded);

  if(!p) {
    return nullptr;
  }
  if(!alignment) {
    return p;
  }

  // Over-reserve, then give back the unaligned head and the extra tail.
  uintptr_t start = reinterpret_cast<uintptr_t>(p);
  uintptr_t aligned = (start + alignment - 1) & ~(uintptr_t)(alignment - 1);
  size_t head = aligned - start;
  size_t tail = padded - head - bytes;

  if(head) {
    munmap(p, head);
  }
  if(tail) {
    munmap(reinterpret_cast<void*>(aligned + bytes), tail);
  }
  return reinterpret_cast<void*>(aligned);
}


// A virtual range cannot be mprotect()ed into memory: map fresh anonymous
// pages over it. Callers only commit what is not committed (libpas keeps
// watermarks, OSAllocator commits decommitted memory), so nothing is lost.
bool
commit(void* addr, size_t bytes, bool writable, int) {
  return mmap(addr, bytes, toProt(true, writable),
              MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0) != MAP_FAILED;
}


// Mapping a virtual range (or fresh PROT_NONE pages) over the range frees
// the old pages and keeps the reservation.
bool
decommit(void* addr, size_t bytes) {
  void* p = addr;

  if(!sceKernelReserveVirtualRange(&p, bytes, SCE_KERNEL_MAP_FIXED, 0) &&
     p == addr) {
    return true;
  }
  return mmap(addr, bytes, PROT_NONE, MAP_PRIVATE | MAP_ANON | MAP_FIXED,
              -1, 0) != MAP_FAILED;
}


bool
release(void* addr, size_t bytes) {
  return !munmap(addr, bytes);
}


bool
protect(void* addr, size_t bytes, bool readable, bool writable, int) {
  return !mprotect(addr, bytes, toProt(readable, writable));
}

} // namespace vss
} // namespace memory_extra


extern "C" {

void*
memory_extra_vss_reserve(size_t bytes, size_t alignment) {
  return memory_extra::vss::reserve(bytes, alignment);
}


bool
memory_extra_vss_commit(void* addr, size_t bytes, bool writable, int usage) {
  return memory_extra::vss::commit(addr, bytes, writable, usage);
}


bool
memory_extra_vss_decommit(void* addr, size_t bytes) {
  return memory_extra::vss::decommit(addr, bytes);
}


bool
memory_extra_vss_release(void* addr, size_t bytes) {
  return memory_extra::vss::release(addr, bytes);
}


bool
memory_extra_vss_protect(void* addr, size_t bytes, bool readable,
                         bool writable, int usage) {
  return memory_extra::vss::protect(addr, bytes, readable, writable, usage);
}

} // extern "C"
