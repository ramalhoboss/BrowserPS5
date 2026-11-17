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

// On the build machine (make overlay-preview): overlay.c's cursors and ring
// in a PNG, to look at them without the console. The left half is real
// size over a dark and a light page, the right half the cursors 4 times as
// big.

#include "overlay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 900
#define H 420

static uint32_t crc_table[256];

static uint32_t
crc(const unsigned char* p, size_t n, uint32_t c) {
  for(size_t i = 0; i < n; i++) {
    c = crc_table[(c ^ p[i]) & 0xff] ^ (c >> 8);
  }
  return c;
}

static void
put32(FILE* f, uint32_t v) {
  unsigned char b[4] = {v >> 24, v >> 16, v >> 8, v};
  fwrite(b, 1, 4, f);
}

static void
chunk(FILE* f, const char* type, const unsigned char* data, uint32_t n) {
  put32(f, n);
  fwrite(type, 1, 4, f);
  fwrite(data, 1, n, f);
  uint32_t c = crc((const unsigned char*)type, 4, 0xffffffffu);
  put32(f, crc(data, n, c) ^ 0xffffffffu);
}

// An RGB PNG with "stored" (uncompressed) deflate blocks.
static int
write_png(const char* path, const uint32_t* pixels, int w, int h) {
  for(uint32_t n = 0; n < 256; n++) {
    uint32_t c = n;
    for(int k = 0; k < 8; k++) {
      c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
    }
    crc_table[n] = c;
  }
  size_t raw_n = (size_t)h * (1 + 3 * w);
  unsigned char* raw = malloc(raw_n);
  size_t blocks = (raw_n + 65534) / 65535;
  size_t z_n = 2 + raw_n + blocks * 5 + 4;
  unsigned char* z = malloc(z_n);
  FILE* f = fopen(path, "wb");
  if(!raw || !z || !f) {
    return -1;
  }
  for(int y = 0; y < h; y++) {
    unsigned char* row = raw + (size_t)y * (1 + 3 * w);
    row[0] = 0;
    for(int x = 0; x < w; x++) {
      uint32_t p = pixels[(size_t)y * w + x];
      row[1 + 3 * x] = p >> 16;
      row[2 + 3 * x] = p >> 8;
      row[3 + 3 * x] = p;
    }
  }
  size_t o = 0;
  z[o++] = 0x78;
  z[o++] = 0x01;
  for(size_t done = 0; done < raw_n;) {
    size_t n = raw_n - done > 65535 ? 65535 : raw_n - done;
    z[o++] = done + n == raw_n;
    z[o++] = n & 0xff;
    z[o++] = n >> 8;
    z[o++] = ~n & 0xff;
    z[o++] = (~n >> 8) & 0xff;
    memcpy(z + o, raw + done, n);
    o += n;
    done += n;
  }
  uint32_t a = 1, b = 0;
  for(size_t i = 0; i < raw_n; i++) {
    a = (a + raw[i]) % 65521;
    b = (b + a) % 65521;
  }
  uint32_t adler = (b << 16) | a;
  z[o++] = adler >> 24;
  z[o++] = adler >> 16;
  z[o++] = adler >> 8;
  z[o++] = adler;

  static const unsigned char sig[8] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
  unsigned char ihdr[13] = {w >> 24, w >> 16, w >> 8, w, h >> 24, h >> 16,
                            h >> 8, h, 8, 2, 0, 0, 0};
  fwrite(sig, 1, 8, f);
  chunk(f, "IHDR", ihdr, 13);
  chunk(f, "IDAT", z, (uint32_t)o);
  chunk(f, "IEND", (const unsigned char*)"", 0);
  fclose(f);
  free(raw);
  free(z);
  return 0;
}

static void
fill(uint32_t* p, int x, int y, int w, int h, uint32_t c) {
  for(int r = y; r < y + h; r++) {
    for(int k = x; k < x + w; k++) {
      p[(size_t)r * W + k] = 0xff000000 | c;
    }
  }
}

int
main(int argc, char** argv) {
  static uint32_t frame[W * H];
  cursor_image_t arrow, hand;

  if(cursor_make(&arrow, CURSOR_ARROW, 0xffffff)
     || cursor_make(&hand, CURSOR_HAND, 0xffffff)) {
    return 1;
  }
  fill(frame, 0, 0, W, H, 0x1b1f24);        // a dark page
  fill(frame, 0, 210, 450, 210, 0xf2f3f5);  // a light one
  // A "button" with the ring on each, the hand over it.
  fill(frame, 60, 60, 220, 56, 0x2d333b);
  ring_draw(frame, W, H, 60, 60, 220, 56, 0, H, 0x4c8dff, 1);
  cursor_draw(frame, W, H, &hand, 170, 90);
  cursor_draw(frame, W, H, &arrow, 340, 70);
  fill(frame, 60, 270, 220, 56, 0xdfe3e8);
  ring_draw(frame, W, H, 60, 270, 220, 56, 0, H, 0x1a73e8, 1);
  cursor_draw(frame, W, H, &hand, 170, 300);
  cursor_draw(frame, W, H, &arrow, 340, 280);

  // Four times as big, nearest neighbour, over the dark page.
  uint32_t* big = calloc((size_t)arrow.width * 4 * arrow.height * 4, 4);
  const cursor_image_t* both[2] = {&arrow, &hand};
  int at_x = 480;
  for(int i = 0; i < 2; i++) {
    const cursor_image_t* c = both[i];
    cursor_image_t scaled = {c->width * 4, c->height * 4, c->hot_x * 4,
                             c->hot_y * 4, 0};
    scaled.pixels = calloc((size_t)scaled.width * scaled.height, 4);
    for(int y = 0; y < scaled.height; y++) {
      for(int x = 0; x < scaled.width; x++) {
        scaled.pixels[(size_t)y * scaled.width + x] =
          c->pixels[(size_t)(y / 4) * c->width + x / 4];
      }
    }
    cursor_draw(frame, W, H, &scaled, at_x + scaled.hot_x, 20 + scaled.hot_y);
    at_x += scaled.width + 20;
    free(scaled.pixels);
  }
  free(big);
  printf("arrow %dx%d hot %d,%d; hand %dx%d hot %d,%d\n", arrow.width,
         arrow.height, arrow.hot_x, arrow.hot_y, hand.width, hand.height,
         hand.hot_x, hand.hot_y);
  return write_png(argc > 1 ? argv[1] : "overlay-preview.png", frame, W, H);
}
