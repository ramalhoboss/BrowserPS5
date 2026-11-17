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

// Source/WTF/wtf/unix/MemoryPressureHandlerUnix.cpp includes <malloc.h>,
// which in the SDK's FreeBSD 11 headers is only an #error pointing to
// <stdlib.h>. WTF needs nothing else from it there (malloc_trim is behind
// HAVE(MALLOC_TRIM)), so this stands in for it.

#pragma once

#include <stdlib.h>
