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

// Declarations for the POSIX extras that the PS5 firmware exports from
// libScePosixForWebKit.sprx (the ps5-payload-sdk ships a stub for it) and
// that WebKit's PlayStation code calls. Sony's own SDK declares them in its
// libc headers; the open SDK does not, so this file is force-included
// (-include) into every WebKit translation unit.
//
// Signatures are inferred from the WebKit call sites:
//   getenv_np          Source/WTF/wtf/playstation/LanguagePlayStation.cpp
//   pthread_setname_np Source/WTF/wtf/posix/ThreadingPOSIX.cpp, libpas
//   clock_gettime_np   Source/bmalloc/libpas/src/libpas/pas_monotonic_time.c

#pragma once

#include <stddef.h>
// WTF's FreeBSD code includes <sys/sysctl.h> before <sys/types.h>, but
// FreeBSD 11's sysctl.h needs u_int from it.
#include <sys/types.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

// Copies the variable into value; returns 0 on success.
int getenv_np(const char* name, char* value, size_t size);

// Names the calling thread (Darwin-style, one argument).
int pthread_setname_np(const char* name);

int clock_gettime_np(clockid_t clock_id, struct timespec* tp);

#ifdef __cplusplus
}
#endif
