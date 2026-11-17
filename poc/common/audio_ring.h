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

// audio_ring.h: the sound of WebKit's media, from ps5audiosink in the
// WebProcess to the browser's title (browser/title/title.cpp), which plays
// it. While an app is in front, the system silences the sound of every
// other process, and the WebProcess is a payload; the title is the app in
// front (found by ear on 2026-11-04, docs/03-video-e-gpu.md, "Teste de som
// de ouvido").
//
// Named shared memory AUDIO_RING_SHM, made by the title when it starts:
// AUDIO_RING_SLOTS rings of 48 kHz stereo S16, one for each sink that
// plays. A ring has one writer (a sink) and one reader (the title's sound
// thread): the writer moves only `written` and `flush_to`, the reader only
// `read`. The title mixes the rings into its one port and says it is alive
// in `consumer_ns`; a sink whose title went away (closed from the system)
// drops its sound at the pace of real time instead of waiting forever.
//
// So the WebProcess opens no sound port of its own: one that crashes or is
// killed leaves none open (V2, V3 and the TV test of 2026-11-04), and the
// title's port goes with the title, as any game's does.

#pragma once

#include <stdint.h>
#include <time.h>

#define AUDIO_RING_SHM      "/bps5audio"
#define AUDIO_RING_MAGIC    0x41535042u  // "BPSA"
#define AUDIO_RING_VERSION  1
#define AUDIO_RING_SLOTS    8
#define AUDIO_RING_FRAMES   4096u  // 85 ms a ring; a power of two
#define AUDIO_RING_RATE     48000
#define AUDIO_RING_BLOCK    256u   // frames the title plays at a time (5.3 ms)
// The title's sound thread is gone if it has played nothing for this long.
#define AUDIO_RING_ALIVE_NS 500000000ull

enum {
  AUDIO_SLOT_FREE,
  AUDIO_SLOT_TAKING,   // a writer is setting it up; the reader skips it
  AUDIO_SLOT_PLAYING,
};

typedef struct audio_slot {
  uint32_t state;
  int32_t pid;          // the writer's, for the logs
  uint64_t written;     // frames ever written (the writer's)
  uint64_t read;        // frames ever read (the reader's)
  uint64_t flush_to;    // the writer asks the reader to skip to here
  uint64_t reserved[4];
  int16_t samples[AUDIO_RING_FRAMES * 2];
} audio_slot_t;

typedef struct audio_ring {
  uint32_t magic;
  uint32_t version;
  uint64_t consumer_ns;  // CLOCK_MONOTONIC when the title last played a block
  uint64_t blocks;       // blocks the title has played
  uint64_t reserved[5];
  audio_slot_t slots[AUDIO_RING_SLOTS];
} audio_ring_t;


static inline uint64_t
audio_ring_now_ns(void) {
  struct timespec t;

  clock_gettime(CLOCK_MONOTONIC, &t);
  return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}


static inline int
audio_ring_ready(const audio_ring_t* ring) {
  return __atomic_load_n(&ring->magic, __ATOMIC_ACQUIRE) == AUDIO_RING_MAGIC
    && ring->version == AUDIO_RING_VERSION;
}


// The title's sound thread played a block not long ago.
static inline int
audio_ring_alive(const audio_ring_t* ring) {
  uint64_t then = __atomic_load_n(&ring->consumer_ns, __ATOMIC_ACQUIRE);
  uint64_t now = audio_ring_now_ns();

  return then && now >= then && now - then < AUDIO_RING_ALIVE_NS;
}
