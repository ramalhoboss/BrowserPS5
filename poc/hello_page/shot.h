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

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Prints an ARGB32 picture (Cairo's format, as WKPagePaint fills it) at half
 * size as a PNG on stdout, in base64 lines that start with "SHOT:", so the
 * page can be seen on the PC without the TV: poc/shot.ps1 turns a payload
 * log into the PNG. Returns the PNG's size in bytes, 0 on failure.
 **/
unsigned long shot_print(const uint32_t* argb, int width, int height);

#ifdef __cplusplus
}
#endif
