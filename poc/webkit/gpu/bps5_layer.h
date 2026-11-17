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

// A web page composited on the GPU (docs/03-video-e-gpu.md, G2): the
// WebProcess's compositor draws each frame into a framebuffer object, reads
// it back into the named shared memory /bps5layer-<page ID>
// (AcceleratedSurfacePlayStation.cpp, made by the WebProcess), and the
// UIProcess copies it to the screen (poc/browser/main.cpp). The page ID is
// WebPage::identifier() there and WKPageGetWebPageIDForBrowserPS5() here.
//
// Three buffers, as in poc/browser/title/link.h: the writer and the reader
// each own one and swap theirs with the shared middle one, so neither ever
// touches the buffer the other one is using. Pixels are Cairo's ARGB32 (BGRA
// bytes, premultiplied alpha), rows width * 4 bytes apart, top row first
// unless the frame says BPS5_LAYER_BOTTOM_UP.
//
// Version 2 (2026-11-06, docs/05-webprocess-no-titulo.md): each layer is as
// big as its view, not the whole screen. A WebProcess in the title gets its
// POSIX shared memory from the title's "flexible" memory (219 MiB left when
// it starts), and seven layers of 24.9 MiB did not fit: the bar, the menu,
// the dialog, the keyboard and the radial menus need much less. The header
// says how big a frame each buffer holds (capacity_width, capacity_height)
// and how far apart the buffers are (frame_bytes); the reader maps the size
// fstat() gives and checks that it holds them. Version 1 layers (1920x1080
// buffers, no capacity fields) are still read.

#pragma once

#include <stdint.h>
#include <stdio.h>

#define BPS5_LAYER_MAGIC   0x4c535042u  // "BPSL"
#define BPS5_LAYER_VERSION 2        // written
#define BPS5_LAYER_VERSION_FIXED 1  // read too: every buffer 1920x1080

#define BPS5_LAYER_MAX_WIDTH  1920
#define BPS5_LAYER_MAX_HEIGHT 1080
#define BPS5_LAYER_BUFFERS    3
// A version 1 layer's buffers, and the most a version 2 one has.
#define BPS5_LAYER_FRAME_BYTES \
  ((uint64_t)BPS5_LAYER_MAX_WIDTH * BPS5_LAYER_MAX_HEIGHT * 4)
#define BPS5_LAYER_HEADER_BYTES 4096
#define BPS5_LAYER_BYTES \
  (BPS5_LAYER_HEADER_BYTES + BPS5_LAYER_BUFFERS * BPS5_LAYER_FRAME_BYTES)
#define BPS5_LAYER_PAGE 16384u  // version 2 buffers: whole pages

#define BPS5_LAYER_FRESH     0x100u  // in middle: a frame not taken yet
#define BPS5_LAYER_BOTTOM_UP 0x1u    // bps5_layer_frame_t.flags

typedef struct bps5_layer_frame {
  uint32_t width, height;  // pixels, at most the MAX ones
  uint32_t flags;
  uint32_t reserved;
  uint64_t number;         // 1, 2, ... in the order they were drawn
} bps5_layer_frame_t;

// The same header in C++ on both sides, and maybe C later: plain fields,
// used with the compiler's __atomic builtins.
typedef struct bps5_layer {
  uint32_t magic;
  uint32_t version;
  uint32_t middle;        // buffer index, | BPS5_LAYER_FRESH
  int32_t pid;            // the WebProcess's
  uint64_t published;     // frames handed over
  bps5_layer_frame_t frames[BPS5_LAYER_BUFFERS];
  // Version 2: the biggest frame a buffer holds, and the bytes from one
  // buffer to the next (the first one is BPS5_LAYER_HEADER_BYTES in).
  uint32_t capacity_width, capacity_height;
  uint64_t frame_bytes;
} bps5_layer_t;

// Bytes per buffer for frames of at most width x height.
static inline uint64_t
bps5_layer_frame_bytes_for(uint32_t width, uint32_t height) {
  uint64_t bytes = (uint64_t)width * height * 4;
  return (bytes + BPS5_LAYER_PAGE - 1) & ~(uint64_t)(BPS5_LAYER_PAGE - 1);
}

// The size of a version 2 layer whose buffers hold width x height.
static inline uint64_t
bps5_layer_bytes_for(uint32_t width, uint32_t height) {
  return BPS5_LAYER_HEADER_BYTES
    + BPS5_LAYER_BUFFERS * bps5_layer_frame_bytes_for(width, height);
}

// Writer: what the buffers of a view of width x height hold. Its own size,
// at least 1x1 and at most the screen; the whole screen for one as wide as
// the screen and more than half as high (the page, whose height changes
// with the bar's favorites and in full screen, and the tabs view), so a
// change of height does not make it again.
static inline void
bps5_layer_capacity_for(int width, int height, uint32_t* capacity_width,
                        uint32_t* capacity_height) {
  uint32_t w = width < 1 ? 1 : (uint32_t)width;
  uint32_t h = height < 1 ? 1 : (uint32_t)height;
  if(w > BPS5_LAYER_MAX_WIDTH) {
    w = BPS5_LAYER_MAX_WIDTH;
  }
  if(h > BPS5_LAYER_MAX_HEIGHT) {
    h = BPS5_LAYER_MAX_HEIGHT;
  }
  if(w == BPS5_LAYER_MAX_WIDTH && h > BPS5_LAYER_MAX_HEIGHT / 2) {
    h = BPS5_LAYER_MAX_HEIGHT;
  }
  *capacity_width = w;
  *capacity_height = h;
}

static inline void
bps5_layer_name(char* name, size_t size, uint64_t page_id) {
  snprintf(name, size, "/bps5layer-%llu", (unsigned long long)page_id);
}

// What a layer's buffers hold (version 1: the whole screen).
static inline void
bps5_layer_capacity(const bps5_layer_t* layer, uint32_t* width, uint32_t* height) {
  if(layer->version == BPS5_LAYER_VERSION_FIXED) {
    *width = BPS5_LAYER_MAX_WIDTH;
    *height = BPS5_LAYER_MAX_HEIGHT;
  } else {
    *width = layer->capacity_width;
    *height = layer->capacity_height;
  }
}

static inline uint64_t
bps5_layer_frame_bytes(const bps5_layer_t* layer) {
  return layer->version == BPS5_LAYER_VERSION_FIXED ? BPS5_LAYER_FRAME_BYTES
                                                    : layer->frame_bytes;
}

static inline uint32_t*
bps5_layer_pixels(bps5_layer_t* layer, unsigned index) {
  return (uint32_t*)(void*)((char*)layer + BPS5_LAYER_HEADER_BYTES
                     + index * bps5_layer_frame_bytes(layer));
}

// Writer: hand over the buffer it wrote (its frames[] entry filled in), get
// back the one to write next.
static inline unsigned
bps5_layer_publish(bps5_layer_t* layer, unsigned back) {
  unsigned next = __atomic_exchange_n(&layer->middle, back | BPS5_LAYER_FRESH,
                                      __ATOMIC_ACQ_REL);
  __atomic_fetch_add(&layer->published, 1, __ATOMIC_RELAXED);
  return next & ~BPS5_LAYER_FRESH;
}

// Reader: the newest frame if there is one (its own buffer goes to the
// middle), else the buffer it already had.
static inline unsigned
bps5_layer_take(bps5_layer_t* layer, unsigned front, int* fresh) {
  *fresh = 0;
  if(__atomic_load_n(&layer->middle, __ATOMIC_ACQUIRE) & BPS5_LAYER_FRESH) {
    *fresh = 1;
    return __atomic_exchange_n(&layer->middle, front, __ATOMIC_ACQ_REL)
      & ~BPS5_LAYER_FRESH;
  }
  return front;
}

// The writer owns buffer 0 and the reader buffer 2 at the start. Fields are
// set, then magic last, with a release store. The layer is
// bps5_layer_bytes_for(capacity_width, capacity_height) long.
static inline void
bps5_layer_init(bps5_layer_t* layer, int32_t pid, uint32_t capacity_width,
                uint32_t capacity_height) {
  layer->version = BPS5_LAYER_VERSION;
  layer->pid = pid;
  layer->capacity_width = capacity_width;
  layer->capacity_height = capacity_height;
  layer->frame_bytes = bps5_layer_frame_bytes_for(capacity_width, capacity_height);
  for(unsigned i = 0; i < BPS5_LAYER_BUFFERS; i++) {
    layer->frames[i].width = 0;
    layer->frames[i].height = 0;
    layer->frames[i].flags = 0;
    layer->frames[i].number = 0;
  }
  __atomic_store_n(&layer->middle, 1, __ATOMIC_RELAXED);
  __atomic_store_n(&layer->published, 0, __ATOMIC_RELAXED);
  __atomic_store_n(&layer->magic, BPS5_LAYER_MAGIC, __ATOMIC_RELEASE);
}

static inline int
bps5_layer_ready(bps5_layer_t* layer) {
  return __atomic_load_n(&layer->magic, __ATOMIC_ACQUIRE) == BPS5_LAYER_MAGIC
    && (layer->version == BPS5_LAYER_VERSION
        || layer->version == BPS5_LAYER_VERSION_FIXED);
}

// Reader, once it is ready: its buffers lie inside the bytes mapped (the
// size fstat() gave), so no frame is read beyond the end.
static inline int
bps5_layer_fits(const bps5_layer_t* layer, uint64_t mapped) {
  uint32_t width, height;
  bps5_layer_capacity(layer, &width, &height);
  uint64_t frame = bps5_layer_frame_bytes(layer);
  return width && height && width <= BPS5_LAYER_MAX_WIDTH
    && height <= BPS5_LAYER_MAX_HEIGHT && frame >= (uint64_t)width * height * 4
    && frame <= BPS5_LAYER_FRAME_BYTES + BPS5_LAYER_PAGE
    && BPS5_LAYER_HEADER_BYTES + BPS5_LAYER_BUFFERS * frame <= mapped;
}

// Reader: a frame it may copy (one the writer says is bigger than the
// buffers is not).
static inline int
bps5_layer_frame_fits(const bps5_layer_t* layer, const bps5_layer_frame_t* frame) {
  uint32_t width, height;
  bps5_layer_capacity(layer, &width, &height);
  return frame->width <= width && frame->height <= height;
}
