// BrowserPS5 RN (2026-11-11): the window of a page's alert(), confirm() or
// prompt(), drawn by the app itself. While one is open the page's script
// waits, and with it the whole WebProcess, the one that also draws the
// browser's own pages (the dialog, the menu): none of them can show the
// question. So this one is a picture made here, with PlutoVG and the title's
// own font, laid over the frame (main.cpp ask_native, compose()).
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ask_box {
  uint32_t* pixels;  // width x height, ARGB, premultiplied; free() it
  int width, height;
} ask_box_t;

// The window: who asks (title, e.g. "example.com says"), the page's text
// (wrapped; more than nine lines end in "..."), for prompt() the value that
// OK gives (value, or null), and what the two buttons do (cancel null: only
// OK, an alert). fonts: the folder with DejaVuSans.ttf and
// DejaVuSans-Bold.ttf. accent: the theme's colour, 0xRRGGBB. 0 when drawn.
int ask_box_draw(ask_box_t* box, const char* fonts, const char* title, const char* text,
                 const char* value, const char* ok, const char* cancel, uint32_t accent);

#ifdef __cplusplus
}
#endif
