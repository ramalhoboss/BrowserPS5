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

// The loader interface that libwpe's static build (src/loader-static.c)
// expects the backend to define. On the PlayStation, Sony's WPE backend
// provides it; BrowserPS5 builds WebKit without that backend
// (USE_WPE_BACKEND_PLAYSTATION is off), so the only libwpe object WebKit
// asks for is the pasteboard, and libwpe falls back to its generic one
// when load_object returns NULL. The view and renderer backends are only
// used with Sony's backend.

#include <stddef.h>

#include <wpe/wpe.h>

static void*
load_object(const char* object_name) {
  (void)object_name;
  return NULL;
}

struct wpe_loader_interface _wpe_loader_interface = {
  load_object,
  NULL, NULL, NULL, NULL
};
