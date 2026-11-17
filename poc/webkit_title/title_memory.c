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

// A title's anonymous memory from direct memory.
//
// In a title, anonymous mmap() takes "flexible" memory, 448 MiB in all,
// while 12 GiB of direct memory are free (poc/title_probe, docs/05). WebKit
// maps its heaps with mmap() (libpas, JavaScriptCore through memory_extra's
// vss), so here every anonymous mapping gets direct memory instead:
//
//   - PROT_NONE: a virtual range (sceKernelReserveVirtualRange), which costs
//     no memory;
//   - readable or writable, anywhere: direct memory allocated and mapped;
//   - readable or writable at a fixed place (a commit inside a reserved
//     range, or libpas's zero-fill over its own pages): direct memory mapped
//     there, or, where it is already, the bytes zeroed and the protection
//     set;
//   - munmap(), and a virtual range reserved over it (vss's decommit), give
//     the direct memory under it back.
//
// What the probe showed (2026-11-04 23:18, title_probe-20261104-231823.log):
// direct memory is zero when mapped, runs code once made read+write+execute
// (the JIT's pool), can be given back in 16 KiB parts, and can be mapped at
// a fixed place inside a reserved range; madvise() on it returns 0 and keeps
// the bytes (libpas's decommit then keeps the memory, which is fine with
// GiBs free).
//
// Mappings of files, and anything that fails here, go to the kernel's
// mmap() as before. The table of what is mapped is a sorted array under one
// mutex; nothing here allocates, so malloc may use mmap() freely.

#include "title_memory.h"

#include <errno.h>
#include <stdarg.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

int sceKernelDebugOutText(int channel, const char* text);
int sceKernelAllocateDirectMemory(off_t start, off_t end, size_t len, size_t alignment, int type, off_t* phys);
int sceKernelMapDirectMemory(void** addr, size_t len, int prot, int flags, off_t phys, size_t alignment);
int sceKernelReleaseDirectMemory(off_t phys, size_t len);
size_t sceKernelGetDirectMemorySize(void);
int sceKernelMprotect(const void* addr, size_t len, int prot);

// Wrapped (build-title.sh: --wrap=munmap --wrap=sceKernelReserveVirtualRange).
void* __real_mmap(void* address, size_t length, int protection, int flags, int fd, off_t offset);
int __real_munmap(void* address, size_t length);
int __real_sceKernelReserveVirtualRange(void** addr, size_t len, int flags, size_t alignment);

// 2026-11-14 (../webkit/malloc/ps5malloc.c, a title built with TITLE_MALLOC=1): the title's malloc on
// libpas tells its own blocks from the console's libc's by the pages mapped here. Under -DTITLE_MALLOC:
// the title's converter takes a weak reference that nothing defines for an import, and refuses it.
#ifdef TITLE_MALLOC
void ps5malloc_note_map(void* address, size_t length, int flags);
void ps5malloc_note_unmap(void* address, size_t length);
#endif

#define PAGE ((size_t)16384)
#define SCE_KERNEL_MAP_FIXED 0x10
// 2026-11-10: 65536 (32768 before), and neighbours joined (insert()): WhatsApp
// Web's service worker took the title down (a SIGSEGV writing a 4.9 MB
// script's string, 19:55) after the pages' heaps were committed page by page.
#define MAX_RANGES 65536

typedef struct range {
  uintptr_t start;
  size_t length;
  off_t phys;  // direct memory under start, contiguous
} range_t;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static range_t ranges[MAX_RANGES];
static size_t count;
static size_t direct_bytes;
static size_t peak_bytes;
static size_t fallbacks;
static size_t alloc_failures, map_failures, table_full, joined, stale;


static void
log_line(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

static void
log_line(const char* fmt, ...) {
  char line[256];
  va_list args;
  va_start(args, fmt);
  int n = snprintf(line, sizeof(line), "[title_memory] ");
  vsnprintf(line + n, sizeof(line) - n - 1, fmt, args);
  va_end(args);
  strcat(line, "\n");
  sceKernelDebugOutText(0, line);
}


static size_t
round_up(size_t n) {
  return (n + PAGE - 1) & ~(PAGE - 1);
}


// The first range that ends after address.
static size_t
first_after(uintptr_t address) {
  size_t low = 0;
  size_t high = count;
  while(low < high) {
    size_t mid = (low + high) / 2;
    if(ranges[mid].start + ranges[mid].length <= address) {
      low = mid + 1;
    } else {
      high = mid;
    }
  }
  return low;
}


static int
insert(uintptr_t start, size_t length, off_t phys) {
  size_t at = first_after(start);
  // A neighbour whose pages and direct memory both run on into these takes
  // them (libpas commits page by page, and the direct memory found first is
  // often the next one): fewer ranges. forget() cuts ranges as before.
  if(at > 0) {
    range_t* p = &ranges[at - 1];
    if(p->start + p->length == start && p->phys + (off_t)p->length == phys) {
      p->length += length;
      if(at < count && p->start + p->length == ranges[at].start
         && p->phys + (off_t)p->length == ranges[at].phys) {
        p->length += ranges[at].length;
        memmove(&ranges[at], &ranges[at + 1], (count - at - 1) * sizeof(range_t));
        count--;
      }
      goto joined_in;
    }
  }
  if(at < count && start + length == ranges[at].start && phys + (off_t)length == ranges[at].phys) {
    ranges[at].start = start;
    ranges[at].phys = phys;
    ranges[at].length += length;
    goto joined_in;
  }
  if(count == MAX_RANGES) {
    return 0;
  }
  memmove(&ranges[at + 1], &ranges[at], (count - at) * sizeof(range_t));
  ranges[at] = (range_t){start, length, phys};
  count++;
  goto counted;
joined_in:
  joined++;
counted:
  direct_bytes += length;
  if(direct_bytes > peak_bytes) {
    peak_bytes = direct_bytes;
  }
  return 1;
}


// Forgets [start, start+length) and gives its direct memory back. The
// virtual pages must already be unmapped or reserved over.
static void
forget(uintptr_t start, size_t length) {
  uintptr_t end = start + length;
  size_t i = first_after(start);

  while(i < count && ranges[i].start < end) {
    range_t* r = &ranges[i];
    uintptr_t r_end = r->start + r->length;
    uintptr_t cut_start = r->start > start ? r->start : start;
    uintptr_t cut_end = r_end < end ? r_end : end;
    off_t cut_phys = r->phys + (off_t)(cut_start - r->start);

    sceKernelReleaseDirectMemory(cut_phys, cut_end - cut_start);
    direct_bytes -= cut_end - cut_start;
    if(cut_start == r->start && cut_end == r_end) {
      memmove(&ranges[i], &ranges[i + 1], (count - i - 1) * sizeof(range_t));
      count--;
      continue;
    }
    if(cut_start == r->start) {
      r->phys += (off_t)(cut_end - r->start);
      r->length = r_end - cut_end;
      r->start = cut_end;
      i++;
      continue;
    }
    if(cut_end == r_end) {
      r->length = cut_start - r->start;
      i++;
      continue;
    }
    // The cut is in the middle: two ranges are left.
    range_t tail = {cut_end, r_end - cut_end, r->phys + (off_t)(cut_end - r->start)};
    r->length = cut_start - r->start;
    if(count == MAX_RANGES) {
      // No room for the tail: give it back too (its pages stay mapped and
      // readable until unmapped; this does not happen with 32768 ranges).
      sceKernelReleaseDirectMemory(tail.phys, tail.length);
      direct_bytes -= tail.length;
      return;
    }
    memmove(&ranges[i + 2], &ranges[i + 1], (count - i - 1) * sizeof(range_t));
    ranges[i + 1] = tail;
    count++;
    return;
  }
}


// Direct memory mapped at address (fixed when fixed), or 0.
static void*
map_direct(void* address, size_t length, int protection, int fixed) {
  static size_t total;
  off_t phys = 0;

  if(!total) {
    total = sceKernelGetDirectMemorySize();
  }
  int err = sceKernelAllocateDirectMemory(0, (off_t)total, length, PAGE, 0, &phys);
  if(err) {
    if(alloc_failures++ < 5) {
      log_line("allocating %zu KiB of direct memory: 0x%x (%zu MiB mapped)", length >> 10, err,
               direct_bytes >> 20);
    }
    return 0;
  }
  void* at = fixed ? address : 0;
  err = sceKernelMapDirectMemory(&at, length, protection, fixed ? SCE_KERNEL_MAP_FIXED : 0, phys, PAGE);
  if(err || (fixed && at != address)) {
    if(map_failures++ < 5) {
      log_line("mapping %zu KiB of direct memory at %p: 0x%x", length >> 10, address, err);
    }
    if(!err) {
      __real_munmap(at, length);
    }
    sceKernelReleaseDirectMemory(phys, length);
    return 0;
  }
  // The kernel gave addresses the table still holds: those entries are stale
  // (their pages were unmapped some way not seen here), so they go, with
  // their direct memory, before the new one is put in.
  size_t i = first_after((uintptr_t)at);
  if(i < count && ranges[i].start < (uintptr_t)at + length) {
    if(stale++ < 5) {
      log_line("stale entries under %p (%zu KiB) dropped", at, length >> 10);
    }
    forget((uintptr_t)at, length);
  }
  if(!insert((uintptr_t)at, length, phys)) {
    if(table_full++ < 5) {
      log_line("the table is full (%d ranges)", MAX_RANGES);
    }
    __real_munmap(at, length);
    sceKernelReleaseDirectMemory(phys, length);
    return 0;
  }
  return at;
}


// [start, start+length) at a fixed place: zeroed where direct memory is,
// direct memory mapped in the gaps. 0 when a gap could not be filled.
static int
commit_fixed(uintptr_t start, size_t length, int protection) {
  uintptr_t end = start + length;
  uintptr_t at = start;

  while(at < end) {
    size_t i = first_after(at);
    if(i < count && ranges[i].start <= at) {
      uintptr_t r_end = ranges[i].start + ranges[i].length;
      uintptr_t stop = r_end < end ? r_end : end;
      sceKernelMprotect((void*)at, stop - at, protection | PROT_WRITE);
      memset((void*)at, 0, stop - at);
      if(!(protection & PROT_WRITE)) {
        sceKernelMprotect((void*)at, stop - at, protection);
      }
      at = stop;
      continue;
    }
    uintptr_t stop = i < count && ranges[i].start < end ? ranges[i].start : end;
    if(!map_direct((void*)at, stop - at, protection, 1)) {
      return 0;
    }
    at = stop;
  }
  return 1;
}


void*
title_memory_mmap(void* address, size_t length, int protection, int flags, int fd, off_t offset) {
  if(!(flags & MAP_ANON) || !length) {
    return __real_mmap(address, length, protection, flags, fd, offset);
  }
  size_t size = round_up(length);
  int fixed = (flags & MAP_FIXED) != 0;
  void* result = MAP_FAILED;

  pthread_mutex_lock(&lock);
  if(protection == PROT_NONE) {
    void* at = fixed ? address : 0;
    if(!__real_sceKernelReserveVirtualRange(&at, size, fixed ? SCE_KERNEL_MAP_FIXED : 0, 0)) {
      if(fixed) {
        forget((uintptr_t)address, size);
      }
      result = at;
    }
  } else if(fixed) {
    if(commit_fixed((uintptr_t)address, size, protection)) {
      result = address;
    }
  } else {
    void* at = map_direct(0, size, protection, 0);
    if(at) {
      result = at;
    }
  }
  if(result == MAP_FAILED) {
    fallbacks++;
  }
  pthread_mutex_unlock(&lock);

  if(result == MAP_FAILED) {
    // As before: the kernel's anonymous memory (flexible).
    result = __real_mmap(address, length, protection, flags, fd, offset);
  }
#ifdef TITLE_MALLOC
  if(result != MAP_FAILED) {
    ps5malloc_note_map(result, size, flags);
  }
#endif
  return result;
}


// 2026-11-10 20:30: the unmap and the forget() under one lock. Before, the
// pages were unmapped first and forgotten after, without the lock: in
// between, another thread's mmap() could be given the same addresses (new
// direct memory, inserted next to the old entries), and the forget() then
// gave back the NEW mapping's direct memory too, which unmaps it: a write
// into a live buffer faulted (WhatsApp's service worker, signal 11 code 1
// at 0x260024000, 19:59) and the title went down with the console.
int
__wrap_munmap(void* address, size_t length) {
  pthread_mutex_lock(&lock);
  int result = __real_munmap(address, length);
  if(!result) {
    forget((uintptr_t)address, round_up(length));
#ifdef TITLE_MALLOC
    ps5malloc_note_unmap(address, round_up(length));
#endif
  }
  pthread_mutex_unlock(&lock);
  return result;
}


// A virtual range reserved over mapped pages unmaps them (vss's decommit):
// the direct memory under them goes back. Under the lock, as munmap().
int
__wrap_sceKernelReserveVirtualRange(void** addr, size_t len, int flags, size_t alignment) {
  void* wanted = *addr;
  pthread_mutex_lock(&lock);
  int result = __real_sceKernelReserveVirtualRange(addr, len, flags, alignment);
  if(!result && (flags & SCE_KERNEL_MAP_FIXED)) {
    forget((uintptr_t)wanted, round_up(len));
  }
  pthread_mutex_unlock(&lock);
  return result;
}


void
title_memory_stats(title_memory_stats_t* stats) {
  pthread_mutex_lock(&lock);
  stats->direct_bytes = direct_bytes;
  stats->ranges = count;
  stats->peak_bytes = peak_bytes;
  stats->fallbacks = fallbacks;
  stats->alloc_failures = alloc_failures;
  stats->map_failures = map_failures;
  stats->table_full = table_full;
  stats->joined = joined;
  pthread_mutex_unlock(&lock);
}
