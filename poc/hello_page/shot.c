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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zlib.h>

#include "shot.h"

#define SHOT_LINE 76  // base64 characters per line


static void
put32(unsigned char* p, uint32_t v) {
  p[0] = v >> 24;
  p[1] = v >> 16;
  p[2] = v >> 8;
  p[3] = v;
}


// Appends a PNG chunk (length, type, data, CRC of type and data). The data
// may already be in place, at out + 8.
static unsigned char*
chunk(unsigned char* out, const char* type, const unsigned char* data,
      uint32_t len) {
  uLong crc;

  put32(out, len);
  memcpy(out + 4, type, 4);
  if(len && data != out + 8) {
    memcpy(out + 8, data, len);
  }
  crc = crc32(0, out + 4, len + 4);
  put32(out + 8 + len, (uint32_t)crc);
  return out + 12 + len;
}


static void
print_base64(const unsigned char* data, size_t len) {
  static const char digits[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  char line[SHOT_LINE + 1];
  size_t n = 0;

  for(size_t i = 0; i < len; i += 3) {
    uint32_t v = (uint32_t)data[i] << 16;
    size_t left = len - i;

    if(left > 1) {
      v |= (uint32_t)data[i + 1] << 8;
    }
    if(left > 2) {
      v |= data[i + 2];
    }
    line[n++] = digits[(v >> 18) & 63];
    line[n++] = digits[(v >> 12) & 63];
    line[n++] = left > 1 ? digits[(v >> 6) & 63] : '=';
    line[n++] = left > 2 ? digits[v & 63] : '=';
    if(n == SHOT_LINE || i + 3 >= len) {
      line[n] = 0;
      printf("SHOT:%s\n", line);
      n = 0;
    }
  }
  fflush(stdout);
}


unsigned long
shot_print(const uint32_t* argb, int width, int height) {
  int w = width / 2;
  int h = height / 2;
  size_t row = 1 + (size_t)w * 3;  // filter byte + RGB
  unsigned char* raw = malloc(row * h);
  uLongf zlen = compressBound(row * h);
  unsigned char* png = malloc(zlen + 64);
  unsigned char* p;
  unsigned char ihdr[13];

  if(!raw || !png) {
    free(raw);
    free(png);
    return 0;
  }

  // Each pixel is the average of a 2x2 block, so small text stays legible.
  for(int y = 0; y < h; y++) {
    unsigned char* out = raw + row * y;
    const uint32_t* a = argb + (size_t)(2 * y) * width;
    const uint32_t* b = a + width;

    *out++ = 0;
    for(int x = 0; x < w; x++) {
      uint32_t q[4] = {a[2 * x], a[2 * x + 1], b[2 * x], b[2 * x + 1]};
      for(int shift = 16; shift >= 0; shift -= 8) {
        unsigned sum = 0;
        for(int i = 0; i < 4; i++) {
          sum += (q[i] >> shift) & 255;
        }
        *out++ = (unsigned char)((sum + 2) / 4);
      }
    }
  }

  memcpy(png, "\x89PNG\r\n\x1a\n", 8);
  put32(ihdr, w);
  put32(ihdr + 4, h);
  memcpy(ihdr + 8, "\x08\x02\x00\x00\x00", 5);  // 8 bits, RGB
  p = chunk(png + 8, "IHDR", ihdr, sizeof(ihdr));

  // The IDAT chunk's data is the zlib stream, written in place.
  if(compress2(p + 8, &zlen, raw, row * h, 6) != Z_OK) {
    free(raw);
    free(png);
    return 0;
  }
  p = chunk(p, "IDAT", p + 8, (uint32_t)zlen);
  p = chunk(p, "IEND", 0, 0);

  print_base64(png, p - png);
  free(raw);
  free(png);
  return (unsigned long)(p - png);
}
