/* Copyright (C) 2026 BrowserPS5 contributors
   Video setup and tiling adapted from the PS5 port of SDL
   (ps5-payload-dev/SDL, src/video/ps5/SDL_ps5video.c and SDL_ps5tilemap.c),
   Copyright (C) 1997-2024 Sam Lantinga, zlib license.

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

// The screen output that video_out/main.c measured (60 fps inside an app
// process), as functions for the PoCs that show their result on the TV:
// two 1920x1080 tiled framebuffers in direct memory, a copy from a linear
// R G B A frame into the tiled layout, and a flip on vblank. Like
// video_out, it only works inside an app process (bigapp_gl): a plain
// payload has no direct memory.

#pragma once

#include <immintrin.h>
#include <stddef.h>
#include <stdint.h>

#include <sys/types.h>  // before sys/event.h, which uses u_short/u_int
#include <sys/event.h>
#include <sys/mman.h>

#include "poc_log.h"

#define VOUT_WIDTH  1920
#define VOUT_HEIGHT 1080
#define VOUT_MEM    0x4000000    // 64 MiB: two buffers of up to 32 MiB
#define VOUT_ALIGN  0x20000

// sceVideoOut values used by SDL's PS5 driver.
#define VOUT_USER_ID_SYSTEM     0xff
#define VOUT_BUS_TYPE_MAIN      0
#define VOUT_PIXEL_FMT_ABGR8888 0x8000000022000000UL  // bytes: R G B A
#define VOUT_TILING_MODE_TILE   0
#define VOUT_FLIP_MODE_VSYNC    1
#define VOUT_FLIP_RATE_60HZ     0
#define VOUT_MEMTYPE_WC_GARLIC  3
#define VOUT_PROT_CPU_GPU_RW    0x33

// Tiled layout of the framebuffer: 512x128 pixel tiles.
#define VOUT_TILE_WIDTH  512
#define VOUT_TILE_HEIGHT 128
#define VOUT_TILE_SIZE   (VOUT_TILE_WIDTH * VOUT_TILE_HEIGHT)
#define VOUT_TILE_BAND   8


typedef struct vout_buf {
  void* data;
  uint64_t junk[3];
} vout_buf_t;

typedef struct vout_attr {
  uint8_t junk[80];
} vout_attr_t;

typedef struct vout {
  int handle;
  struct kevent* eq;
  intptr_t paddr;  // 0 is a valid direct memory offset, -1 is none
  void* vaddr;
  vout_buf_t bufs[2];
  int frames;
  int bgra;        // set after vout_open: frames are B G R A (Cairo, pixman)
} vout_t;


int sceSystemServiceHideSplashScreen(void);

int sceKernelAllocateMainDirectMemory(size_t len, size_t align, int type,
				      intptr_t* paddr);
int sceKernelMapDirectMemory(void** vaddr, size_t len, int prot, int flags,
			     intptr_t paddr, size_t align);
int sceKernelReleaseDirectMemory(intptr_t paddr, size_t len);

int sceKernelCreateEqueue(struct kevent** eq, const char* name);
int sceKernelWaitEqueue(struct kevent* eq, struct kevent* ev, int num,
			int* out, uint32_t* timeout_us);
int sceKernelDeleteEqueue(struct kevent* eq);

int sceVideoOutOpen(int user_id, int bus_type, int index, const void* param);
void sceVideoOutClose(int handle);
int sceVideoOutAddFlipEvent(struct kevent* eq, int handle, void* udata);
int sceVideoOutDeleteFlipEvent(struct kevent* eq, int handle);
int sceVideoOutSetFlipRate(int handle, int rate);
int sceVideoOutSubmitFlip(int handle, int index, uint32_t mode, int64_t arg);
void sceVideoOutSetBufferAttribute2(vout_attr_t* attr, uint64_t format,
				    uint32_t tiling, uint32_t width,
				    uint32_t height, uint64_t option,
				    uint32_t dcc_cb, uint64_t dcc_control);
int sceVideoOutRegisterBuffers2(int handle, int set_index, int start_index,
				vout_buf_t* bufs, int count,
				vout_attr_t* attr, int category, void* option);


static uint32_t vout_col_offset[VOUT_WIDTH / 4];
static uint32_t vout_row_offset[VOUT_TILE_HEIGHT];


/**
 * Offset of pixel (x, y) inside a 512x128 tile.
 **/
static uint32_t
vout_tile_offset(uint32_t x, uint32_t y) {
  return ((x & 1u) << 0 |
	  ((x >> 1) & 1u) << 1 |
	  ((y >> 0) & 1u) << 2 |
	  ((y >> 1) & 1u) << 3 |
	  ((y >> 2) & 1u) << 4 |
	  ((x >> 2) & 1u) << 5 |
	  (((x >> 3) ^ (y >> 3)) & 1u) << 6 |
	  (((x >> 4) ^ (y >> 4)) & 1u) << 7 |
	  (((x >> 6) ^ (y >> 5)) & 1u) << 8 |
	  (((x >> 5) ^ (y >> 6)) & 1u) << 9 |
	  ((y >> 3) & 1u) << 10 |
	  ((x >> 4) & 1u) << 11 |
	  ((y >> 6) & 1u) << 12 |
	  ((x >> 6) & 1u) << 13 |
	  ((x >> 7) & 1u) << 14 |
	  ((x >> 8) & 1u) << 15);
}


/**
 * Copy a linear frame into the tiled framebuffer. stride is the distance,
 * in pixels, from one row of src to the next, and can be negative (a GL
 * frame read with glReadPixels is bottom row first). With bgra, the source
 * pixels are B G R A in memory and are swapped to the screen's R G B A on
 * the way. Each 4x8 pixel block is contiguous in the tiled layout, so two
 * rows of 4 pixels go out in one 256-bit store.
 **/
__attribute__((target("avx2"))) static void
vout_tile_frame(const uint32_t* src, ptrdiff_t stride, uint32_t* dst,
		int bgra) {
  const __m256i swap = _mm256_setr_epi8(2, 1, 0, 3, 6, 5, 4, 7,
					10, 9, 8, 11, 14, 13, 12, 15,
					2, 1, 0, 3, 6, 5, 4, 7,
					10, 9, 8, 11, 14, 13, 12, 15);

  for(uint32_t y=0; y<VOUT_HEIGHT; y+=VOUT_TILE_BAND) {
    uint32_t* band = dst + (y / VOUT_TILE_HEIGHT) *
      (VOUT_TILE_HEIGHT * VOUT_WIDTH);
    uint32_t yo = vout_row_offset[y % VOUT_TILE_HEIGHT];
    const uint32_t* s = src + (ptrdiff_t)y * stride;

    for(uint32_t g=0; g<VOUT_WIDTH/4; g++) {
      const uint32_t* p = s + g * 4;
      uint32_t* o = band + (vout_col_offset[g] ^ yo);

      for(int k=0; k<VOUT_TILE_BAND; k+=2) {
	__m256i v = _mm256_inserti128_si256(
	  _mm256_castsi128_si256(_mm_loadu_si128((const __m128i*)p)),
	  _mm_loadu_si128((const __m128i*)(p + stride)), 1);
	if(bgra) {
	  v = _mm256_shuffle_epi8(v, swap);
	}
	_mm256_stream_si256((__m256i*)o, v);
	p += 2 * stride;
	o += 8;
      }
    }
  }
  _mm_sfence();
}


static void
vout_close(vout_t* v) {
  if(v->eq) {
    sceVideoOutDeleteFlipEvent(v->eq, v->handle);
    sceKernelDeleteEqueue(v->eq);
  }
  if(v->handle >= 0) {
    sceVideoOutClose(v->handle);
  }
  if(v->vaddr) {
    munmap(v->vaddr, VOUT_MEM);
  }
  if(v->paddr != -1) {
    sceKernelReleaseDirectMemory(v->paddr, VOUT_MEM);
  }
  memset(v, 0, sizeof(*v));
  v->handle = -1;
  v->paddr = -1;
}


/**
 * Open the main video output with two framebuffers. Returns 0, or the
 * first error code (and logs which call failed).
 **/
static int
vout_open(vout_t* v) {
  vout_attr_t attr = {0};
  int err;

  memset(v, 0, sizeof(*v));
  v->paddr = -1;
  sceSystemServiceHideSplashScreen();

  if((v->handle=sceVideoOutOpen(VOUT_USER_ID_SYSTEM, VOUT_BUS_TYPE_MAIN, 0,
				0)) < 0) {
    POC_LOG("sceVideoOutOpen = 0x%x", v->handle);
    return v->handle;
  }
  if((err=sceKernelAllocateMainDirectMemory(VOUT_MEM, VOUT_ALIGN,
					    VOUT_MEMTYPE_WC_GARLIC,
					    &v->paddr))) {
    POC_LOG("sceKernelAllocateMainDirectMemory = 0x%x", err);
    v->paddr = -1;
    goto fail;
  }
  if((err=sceKernelMapDirectMemory(&v->vaddr, VOUT_MEM, VOUT_PROT_CPU_GPU_RW,
				   0, v->paddr, VOUT_ALIGN))) {
    POC_LOG("sceKernelMapDirectMemory = 0x%x", err);
    v->vaddr = 0;
    goto fail;
  }
  v->bufs[0].data = v->vaddr;
  v->bufs[1].data = (uint8_t*)v->vaddr + VOUT_MEM / 2;
  memset(v->vaddr, 0, VOUT_MEM);

  if((err=sceKernelCreateEqueue(&v->eq, "vout flip"))) {
    POC_LOG("sceKernelCreateEqueue = 0x%x", err);
    v->eq = 0;
    goto fail;
  }
  if((err=sceVideoOutAddFlipEvent(v->eq, v->handle, 0))) {
    POC_LOG("sceVideoOutAddFlipEvent = 0x%x", err);
    goto fail;
  }
  sceVideoOutSetFlipRate(v->handle, VOUT_FLIP_RATE_60HZ);
  sceVideoOutSetBufferAttribute2(&attr, VOUT_PIXEL_FMT_ABGR8888,
				 VOUT_TILING_MODE_TILE, VOUT_WIDTH,
				 VOUT_HEIGHT, 0, 0, 0);
  if((err=sceVideoOutRegisterBuffers2(v->handle, 0, 0, v->bufs, 2, &attr, 0,
				      0))) {
    POC_LOG("sceVideoOutRegisterBuffers2 = 0x%x", err);
    goto fail;
  }

  for(uint32_t x=0; x<VOUT_WIDTH; x+=4) {
    vout_col_offset[x / 4] = (x / VOUT_TILE_WIDTH) * VOUT_TILE_SIZE +
      vout_tile_offset(x % VOUT_TILE_WIDTH, 0);
  }
  for(uint32_t y=0; y<VOUT_TILE_HEIGHT; y++) {
    vout_row_offset[y] = vout_tile_offset(0, y);
  }
  return 0;

 fail:
  vout_close(v);
  return err;
}


/**
 * Show a linear VOUT_WIDTH x VOUT_HEIGHT frame (see vout_tile_frame for
 * stride) on the next vblank. Adds the time spent copying and waiting for
 * the flip to *tile_ms and *wait_ms. Returns 0, 1 if no flip came within
 * a second, or an error code.
 **/
static int
vout_show(vout_t* v, const uint32_t* src, ptrdiff_t stride, double* tile_ms,
	  double* wait_ms) {
  int idx = v->frames % 2;
  uint32_t timeout_us = 1000000;
  struct kevent evt;
  double t0, t1;
  int err, n;

  t0 = poc_now_ms();
  vout_tile_frame(src, stride, v->bufs[idx].data, v->bgra);
  t1 = poc_now_ms();
  *tile_ms += t1 - t0;

  if((err=sceVideoOutSubmitFlip(v->handle, idx, VOUT_FLIP_MODE_VSYNC,
				v->frames))) {
    return err;
  }
  v->frames++;
  err = sceKernelWaitEqueue(v->eq, &evt, 1, &n, &timeout_us) ? 1 : 0;
  *wait_ms += poc_now_ms() - t1;
  return err;
}
