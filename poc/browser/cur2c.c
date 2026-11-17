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

// On the build machine: a Windows cursor (.cur) as a C array, for the
// browser's cursor (docs/07-navegacao-e-definicoes.md). The cursors are the
// user's own Windows ones (C:\Windows\Cursors), read when the browser is
// built: they are not part of the project and the header made from them
// (cursors_win.h) must not be published.
//
//   cur2c --list FILE.cur
//   cur2c NAME SIZE FILE.cur    (the image nearest SIZE pixels, to stdout)
//
// A .cur is an ICO directory: per image, its size, its hot spot (where an
// icon has its planes and bit count) and a BMP without the file header,
// 32 bits a pixel here (BGRA, bottom row first, then a 1-bit AND mask).

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned
u16(const unsigned char* p) {
  return p[0] | p[1] << 8;
}

static uint32_t
u32(const unsigned char* p) {
  return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

int
main(int argc, char** argv) {
  const char* path = argv[argc - 1];
  int list = argc == 3 && !strcmp(argv[1], "--list");
  if(!list && argc != 4) {
    fprintf(stderr, "usage: cur2c --list FILE.cur | cur2c NAME SIZE FILE.cur\n");
    return 2;
  }
  FILE* f = fopen(path, "rb");
  if(!f) {
    perror(path);
    return 1;
  }
  static unsigned char data[4 << 20];
  size_t n = fread(data, 1, sizeof(data), f);
  fclose(f);
  if(n < 6 || u16(data + 2) != 2) {
    fprintf(stderr, "%s: not a cursor\n", path);
    return 1;
  }
  unsigned count = u16(data + 4);
  int want = list ? 0 : atoi(argv[2]);
  int best = -1, best_diff = 1 << 30;
  for(unsigned i = 0; i < count && 6 + 16 * (i + 1) <= n; i++) {
    const unsigned char* e = data + 6 + 16 * i;
    int w = e[0] ? e[0] : 256, h = e[1] ? e[1] : 256;
    uint32_t off = u32(e + 12);
    int png = off + 8 <= n && !memcmp(data + off, "\x89PNG", 4);
    int bits = off + 16 <= n && !png ? (int)u16(data + off + 14) : 0;
    if(list) {
      printf("%ux%u hot %u,%u %s %d bits, %u bytes\n", w, h, u16(e + 4),
             u16(e + 6), png ? "PNG" : "BMP", bits, u32(e + 8));
    }
    int diff = abs(w - want);
    if(!png && bits == 32 && diff < best_diff) {
      best = (int)i;
      best_diff = diff;
    }
  }
  if(list) {
    return 0;
  }
  if(best < 0) {
    fprintf(stderr, "%s: no 32-bit BMP image\n", path);
    return 1;
  }
  const unsigned char* e = data + 6 + 16 * best;
  int w = e[0] ? e[0] : 256, h = e[1] ? e[1] : 256;
  const unsigned char* bmp = data + u32(e + 12);
  const unsigned char* px = bmp + u32(bmp);  // after the BITMAPINFOHEADER
  if((size_t)(px - data) + (size_t)w * h * 4 > n) {
    fprintf(stderr, "%s: short image\n", path);
    return 1;
  }
  const char* name = argv[1];
  printf("// %dx%d from %s (the build machine's Windows cursors; not part of\n"
         "// the project, do not publish).\n", w, h, path);
  printf("static const int %s_width = %d, %s_height = %d;\n", name, w, name, h);
  printf("static const int %s_hot_x = %u, %s_hot_y = %u;\n", name, u16(e + 4),
         name, u16(e + 6));
  printf("static const uint32_t %s_pixels[%d] = {\n", name, w * h);
  for(int y = 0; y < h; y++) {
    const unsigned char* row = px + (size_t)(h - 1 - y) * w * 4;  // bottom up
    printf("  ");
    for(int x = 0; x < w; x++) {
      const unsigned char* p = row + 4 * x;  // B G R A
      uint32_t argb = (uint32_t)p[3] << 24 | p[2] << 16 | p[1] << 8 | p[0];
      printf("0x%08x,%s", argb, x % 8 == 7 && x + 1 < w ? "\n  " : "");
    }
    printf("\n");
  }
  printf("};\n");
  return 0;
}
