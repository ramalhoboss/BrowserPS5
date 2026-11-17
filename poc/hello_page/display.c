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

#ifndef POC_NAME  // the browser builds these too, under its own name
#define POC_NAME "hello_page/display"
#endif

#include "display.h"
#include "video_out.h"

static vout_t vout;


int
display_open(void) {
  int err = vout_open(&vout);

  // Cairo ARGB32 is B G R A in memory; the video out format is R G B A.
  vout.bgra = 1;
  return err;
}


int
display_show(const uint32_t* frame, double* tile_ms, double* wait_ms) {
  return vout_show(&vout, frame, DISPLAY_WIDTH, tile_ms, wait_ms);
}


void
display_close(void) {
  vout_close(&vout);
}
