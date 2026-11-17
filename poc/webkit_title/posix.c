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

// What the PlayStation port takes from the system's libScePosixForWebKit,
// for a title. That module does not load in a title (2026-11-04 23:12: the
// first mmap went through a null __wrap_mmap, in libpas), so a title that
// links WebKit has its own. The declarations are poc/jsc_shell's
// ps5-posix-webkit.h.

#include <errno.h>
#include <pthread.h>
#include <pthread_np.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>

#include "title_memory.h"


// The port links with --wrap=mmap: every mmap() of WebKit and its
// libraries comes here (title_memory.c: anonymous memory from direct
// memory, the rest to the kernel's mmap).
void*
__wrap_mmap(void* address, size_t length, int protection, int flags, int fd, off_t offset) {
  return title_memory_mmap(address, length, protection, flags, fd, offset);
}


// The value of an environment variable, copied: 0 when it is set.
int
getenv_np(const char* name, char* value, size_t size) {
  const char* found = getenv(name);
  if(!found) {
    return ENOENT;
  }
  if(strlen(found) >= size) {
    return ERANGE;
  }
  strcpy(value, found);
  return 0;
}


// Names the calling thread.
int
pthread_setname_np(const char* name) {
  pthread_set_name_np(pthread_self(), name);
  return 0;
}


int
clock_gettime_np(clockid_t clock, struct timespec* time) {
  return clock_gettime(clock, time);
}


int
isatty(int fd) {
  (void)fd;
  errno = ENOTTY;
  return 0;
}


// fnmatch(3) for the patterns GStreamer's GST_DEBUG and the others use:
// *, ?, [...] with ranges and !/^, backslash escapes; FNM_NOESCAPE (1),
// FNM_PATHNAME (2) and FNM_PERIOD (4).
#define FNM_NOMATCH 1
#define FNM_NOESCAPE 0x01
#define FNM_PATHNAME 0x02
#define FNM_PERIOD 0x04

static int
match(const char* pattern, const char* string, const char* start, int flags) {
  for(;;) {
    char p = *pattern++;
    switch(p) {
    case 0:
      return *string ? FNM_NOMATCH : 0;
    case '?':
      if(!*string || (*string == '/' && (flags & FNM_PATHNAME)) ||
         (*string == '.' && (flags & FNM_PERIOD) &&
          (string == start || ((flags & FNM_PATHNAME) && string[-1] == '/')))) {
        return FNM_NOMATCH;
      }
      string++;
      break;
    case '*':
      while(*pattern == '*') {
        pattern++;
      }
      if(*string == '.' && (flags & FNM_PERIOD) &&
         (string == start || ((flags & FNM_PATHNAME) && string[-1] == '/'))) {
        return FNM_NOMATCH;
      }
      if(!*pattern) {
        return (flags & FNM_PATHNAME) && strchr(string, '/') ? FNM_NOMATCH : 0;
      }
      for(; *string; string++) {
        if(!match(pattern, string, start, flags)) {
          return 0;
        }
        if(*string == '/' && (flags & FNM_PATHNAME)) {
          return FNM_NOMATCH;
        }
      }
      return match(pattern, string, start, flags);
    case '[': {
      if(!*string || (*string == '/' && (flags & FNM_PATHNAME))) {
        return FNM_NOMATCH;
      }
      int negate = *pattern == '!' || *pattern == '^';
      if(negate) {
        pattern++;
      }
      int found = 0;
      const char* class_start = pattern;
      while(*pattern && (*pattern != ']' || pattern == class_start)) {
        char low = *pattern++;
        if(low == '\\' && !(flags & FNM_NOESCAPE) && *pattern) {
          low = *pattern++;
        }
        char high = low;
        if(*pattern == '-' && pattern[1] && pattern[1] != ']') {
          high = pattern[1];
          pattern += 2;
          if(high == '\\' && !(flags & FNM_NOESCAPE) && *pattern) {
            high = *pattern++;
          }
        }
        if(*string >= low && *string <= high) {
          found = 1;
        }
      }
      if(*pattern != ']') {
        return FNM_NOMATCH;  // no closing bracket
      }
      pattern++;
      if(found == negate) {
        return FNM_NOMATCH;
      }
      string++;
      break;
    }
    case '\\':
      if(!(flags & FNM_NOESCAPE) && *pattern) {
        p = *pattern++;
      }
      // fall through
    default:
      if(*string++ != p) {
        return FNM_NOMATCH;
      }
      break;
    }
  }
}


int
fnmatch(const char* pattern, const char* string, int flags) {
  return match(pattern, string, string, flags);
}
