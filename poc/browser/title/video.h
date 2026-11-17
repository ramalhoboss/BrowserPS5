/* Copyright (C) 2026 BrowserPS5 contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 3, or (at your option) any
 * later version.
 *
 * The title's video (docs/08-4k.md, D3): decodes what ps5videosink sends
 * through ../../common/video_ring.h and puts it under the page, where
 * WebKit left the page transparent.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Makes VIDEO_RING_SHM and starts the decoder threads. config: a file of
// NAME=value lines (threads=8, slice=1, max=3840x2160@60), or 0. Returns 0
// on success.
int video_start(const char *config);

// The screen with the videos under it: screen itself when no video shows,
// else a buffer of the same size. width x height pixels, stride pixels
// from a row to the next. On the display thread, once a flip.
const uint32_t *video_compose(const uint32_t *screen, int width, int height, int stride);

// A line for the title's report every few seconds (0 when nothing played).
int video_status(char *line, int size);

// The running time of the picture slot shows (1), or 0 when it shows none.
// On the display thread.
int video_picture_ns(int slot, int64_t *running_ns);

// How many packets or pictures slot's decoder refused since the title
// started (a stream that does not decode: e.g. frames missing before a
// seek's place).
unsigned video_errors(int slot);

void video_stop(void);

// 2026-11-09: the system is over the title (the PS button), or no more: the
// PS5's video decoder and its GPU queues are let go while away.
void video_away(int away);

// A WebProcess ended: the videos it wrote end now.
void video_writer_ended(int32_t pid);

#ifdef __cplusplus
}
#endif
