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

// C wrapper around ../common/video_out.h for the C++ app.

#pragma once

#include <stdint.h>

#define DISPLAY_WIDTH  1920
#define DISPLAY_HEIGHT 1080

#ifdef __cplusplus
extern "C" {
#endif

int display_open(void);

// Show a DISPLAY_WIDTH x DISPLAY_HEIGHT Cairo ARGB32 frame (B G R A in
// memory). Waits for the flip. Returns 0, 1 when no flip came within 1 s,
// or an error code.
int display_show(const uint32_t* frame, double* tile_ms, double* wait_ms);

void display_close(void);

#ifdef __cplusplus
}
#endif
