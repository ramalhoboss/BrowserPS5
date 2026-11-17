/* Copyright (C) 2026 BrowserPS5 contributors. GPL-3.0-or-later.

overlay.c's tests, on the build machine (make test): the panels' colours.
The menu's shadow came out as a blotchy blue ring on the TV (2026-11-05):
red and blue were divided by 255 as one number. Here the menu's panel goes
over a flat page and each pixel must be the page darkened (the shadow) or
between the page and the panel (the round corners), colour by colour; a
fade (frame_blend) must be within 1 of the exact mix. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "overlay.h"

#define W 1920
#define H 1080

static int failures;

#define CHECK(cond, ...)                                                \
  do {                                                                  \
    if(!(cond)) {                                                       \
      if(failures++ < 10) {                                             \
        printf("FAIL %s:%d: ", __FILE__, __LINE__);                     \
        printf(__VA_ARGS__);                                            \
        printf("\n");                                                   \
      }                                                                 \
    }                                                                   \
  } while(0)

static unsigned
channel(uint32_t p, int i) {
  return (p >> (8 * i)) & 0xff;
}


int
main(void) {
  static uint32_t frame[W * H];
  static const uint32_t pages[] = {0xff0b0d14, 0xffe8ecf4, 0xff3a1f7c};
  static const uint32_t panels[] = {0xff1b2030, 0xfffafafa, 0xff00e5ff};
  enum { PW = 560, PH = 836, PX = W - PW - 18, PY = 92 };
  panel_t panel;
  uint32_t* pixels = (uint32_t*)malloc(sizeof(uint32_t) * PW * PH);

  if(!pixels || panel_make(&panel, PW, PH, 26, 40, 14, 0.5f)) {
    printf("FAIL: no memory\n");
    return 1;
  }
  for(size_t k = 0; k < sizeof(pages) / sizeof(pages[0]); k++) {
    uint32_t page = pages[k], color = panels[k];
    for(size_t i = 0; i < (size_t)W * H; i++) {
      frame[i] = page;
    }
    for(size_t i = 0; i < (size_t)PW * PH; i++) {
      pixels[i] = color;
    }
    panel_draw(frame, W, H, &panel, PX, PY, pixels, PW);
    for(int y = 0; y < H; y++) {
      for(int x = 0; x < W; x++) {
        uint32_t p = frame[(size_t)y * W + x];
        int in = x >= PX && x < PX + PW && y >= PY && y < PY + PH;
        CHECK(p >> 24 == 0xff, "(%d, %d) alpha %u", x, y, p >> 24);
        for(int c = 0; c < 3; c++) {
          unsigned v = channel(p, c), a = channel(page, c), b = channel(color, c);
          unsigned hi = a > b ? a : b;
          if(in) {  // the panel, or a corner: between the two (or the shadow under a corner)
            CHECK(v <= hi, "(%d, %d) page %08x panel %08x: %08x", x, y, page, color, p);
          } else {  // the shadow: the page, darker
            CHECK(v <= a && v + 1 >= a / 2, "(%d, %d) page %08x: shadow %08x", x, y, page, p);
          }
        }
      }
    }
    // The shadow keeps the dark page's colour: blue no further from red.
    if(k == 0) {
      for(int y = PY - 40; y < PY + PH + 40; y += 3) {
        uint32_t p = frame[(size_t)y * W + PX - 20];
        int spread = (int)channel(p, 0) - (int)channel(p, 2);
        int was = (int)channel(page, 0) - (int)channel(page, 2);
        CHECK(abs(spread - was) <= 9, "(%d, %d) page %08x: shadow %08x", PX - 20, y, page, p);
      }
    }
  }

  // A fade: exact within 1, every alpha.
  for(unsigned a = 0; a < 256; a++) {
    for(unsigned s = 0; s < 256; s += 17) {
      for(unsigned d = 0; d < 256; d += 15) {
        uint32_t dst = 0xff000000u | d << 16 | (255 - d) << 8 | d;
        uint32_t src = 0xff000000u | (255 - s) << 16 | s << 8 | s;
        frame_blend(&dst, &src, 1, a);
        for(int c = 0; c < 3; c++) {
          unsigned sv = channel(src, c);
          unsigned dv = c == 1 ? 255 - d : d;
          unsigned want = (sv * a + dv * (255 - a) + 127) / 255;
          int got = (int)channel(dst, c);
          CHECK(abs(got - (int)want) <= 1, "a %u: %u over %u = %d, not %u", a, sv, dv, got, want);
        }
      }
    }
  }
  free(pixels);
  printf("%s\n", failures ? "FAILED" : "all passed");
  return failures != 0;
}
