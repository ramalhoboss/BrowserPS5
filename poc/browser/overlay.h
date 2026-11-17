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

// What the display thread draws over the frame (docs/07-navegacao-e-
// definicoes.md): the cursor, an arrow or a pointing hand, smooth-edged with
// an outline and a soft shadow, and the ring around the element the D-pad
// chose. Pixels are 0xAARRGGBB, as the frame's. Made from distance fields
// once at start, so there is no picture file to ship.

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cursor_image {
  int width, height;
  int hot_x, hot_y;  // the pixel that points
  uint32_t* pixels;  // width * height, alpha not premultiplied
} cursor_image_t;

typedef enum cursor_kind {
  CURSOR_ARROW,
  CURSOR_HAND,
} cursor_kind_t;

// fill: the colour inside the outline (0xRRGGBB). 0 on success. Used when
// the build machine has no Windows cursors (cursors_win.h).
int cursor_make(cursor_image_t* image, cursor_kind_t kind, uint32_t fill);

// A copy of a ready cursor picture (pixels, alpha not premultiplied) with a
// soft shadow under it, a little down and right. 0 on success.
int cursor_with_shadow(cursor_image_t* image, int width, int height,
                       int hot_x, int hot_y, const uint32_t* pixels);

// Its hot pixel at (x, y) of a frame of width x height.
void cursor_draw(uint32_t* frame, int width, int height,
                 const cursor_image_t* image, int x, int y);

// A light of d x d pixels (each its alpha and its colour, the frame's order)
// with its top left at (x0, y0), strength/256 of it (BrowserPS5 RN's neon
// ball behind the cursor).
void light_draw(uint32_t* frame, int width, int height, const uint8_t* alpha,
                const uint32_t* rgb, int d, int x0, int y0, unsigned strength);

// A rounded neon ring just outside the rectangle (x, y, w, h): a light core,
// a wide glow of color outside and a faint one inside, drawn only between
// rows clip_top and clip_bottom (the pane it is in). strength (0..1) fades
// it in.
void ring_draw(uint32_t* frame, int width, int height, float x, float y, float w,
               float h, int clip_top, int clip_bottom, uint32_t color, float strength);

// A panel over the frame (the menu, a dialog): its own pixels with round
// corners and a soft shadow under it. Made once for its size.
typedef struct panel {
  int w, h, margin;
  uint8_t* coverage;   // w * h, 255 inside
  uint8_t* shadow;     // (w + 2 margin) * (h + 2 margin)
  int* full_left;      // the columns of a row that are all panel:
  int* full_right;     // [full_left, full_right)
} panel_t;

// drop: how far down the shadow goes; strength: its darkest, 0 to 1.
int panel_make(panel_t* panel, int w, int h, int radius, int margin, int drop,
               float strength);
// The panel's top left at (x, y); pixels its rows, stride pixels apart.
void panel_draw(uint32_t* frame, int width, int height, const panel_t* panel,
                int x, int y, const uint32_t* pixels, int stride);
// The same, alpha (0-255) of it over what is under it: the radial menus'
// glass, and their fade.
void panel_draw_glass(uint32_t* frame, int width, int height, const panel_t* panel,
                      int x, int y, const uint32_t* pixels, int stride,
                      unsigned alpha);
// Half as bright: what is under a dialog.
void frame_dim(uint32_t* frame, size_t count);

// A ready picture (ARGB, premultiplied, w x h) over the frame, its top left at
// (left, top) (BrowserPS5 RN: the window of a page's alert, ask_box.h).
void bitmap_draw(uint32_t* frame, int width, int height, const uint32_t* pixels, int w, int h,
                 int left, int top);
// a (0-255) darker: what is under a radial menu, as it fades in.
void frame_darken(uint32_t* frame, size_t count, unsigned a);
// src over the frame, a (0-255) of it: the tabs' switcher as it fades in.
void frame_blend(uint32_t* frame, const uint32_t* src, size_t count, unsigned a);

#ifdef __cplusplus
}
#endif
