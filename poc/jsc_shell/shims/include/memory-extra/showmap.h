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

// Open replacement for the "showmap" part of Sony's private memory-extra
// API, which Source/WebCore/page/playstation/ResourceUsageThreadPlayStation.cpp
// uses for the Web Inspector's memory overlay:
//
//   memory_extra::showmap::Result<4> result;
//   auto entry = result.reserve("SceNKFastMalloc");
//   result.collect();
//   ... result.rss, entry->rss
//
// Sony's version sums the resident size of the named mappings. Plain POSIX
// has no per-mapping numbers, so the total is the process's peak resident
// size (getrusage) and every named entry reads 0.

#pragma once

#include <stddef.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

namespace memory_extra {
namespace showmap {

struct Entry {
  const char* name = nullptr;
  size_t rss = 0;
};

template<size_t N>
struct Result {
  size_t rss = 0;

  // Track the mappings named `name`; nullptr when all N slots are taken.
  Entry* reserve(const char* name) {
    if(m_count == N) {
      return nullptr;
    }
    m_entries[m_count].name = name;
    return &m_entries[m_count++];
  }

  void collect() {
    struct rusage usage;
    memset(&usage, 0, sizeof usage);
    getrusage(RUSAGE_SELF, &usage);

    // ru_maxrss is in KiB. The caller asserts the total is larger than any
    // entry, so never report 0.
    rss = static_cast<size_t>(usage.ru_maxrss) * 1024;
    if(!rss) {
      rss = static_cast<size_t>(getpagesize());
    }
    for(size_t i = 0; i < m_count; i++) {
      m_entries[i].rss = 0;
    }
  }

private:
  Entry m_entries[N];
  size_t m_count = 0;
};

} // namespace showmap
} // namespace memory_extra
