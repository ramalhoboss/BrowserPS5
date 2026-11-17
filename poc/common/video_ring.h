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

// video_ring.h: WebKit's video, still compressed, from ps5videosink in the
// WebProcess to the browser's title (browser/title/video.c), which decodes
// it and puts it under the page (docs/08-4k.md, D2 and D3). The WebProcess
// is a payload, and while a title is in front the payloads share about 2
// cores; the title has 13 (docs/05). WebKit's "hole punch" (its Westeros
// quirk, switched on in the WebProcess's environment) leaves the video's
// place in the page transparent and gives the sink its rectangle.
//
// Named shared memory VIDEO_RING_SHM, made by the title when it starts, as
// AUDIO_RING_SHM (audio_ring.h). VIDEO_RING_SLOTS slots, one for each sink
// (one <video>). A slot has one writer (its sink) and one reader (the
// title's decoder thread for that slot):
//   - the writer moves data_written, frames_written, generation and the
//     clock and rectangle fields; the reader moves data_read, frames_read
//     and its counts;
//   - each frame's bytes are in one piece in data[]: one that would not fit
//     before the end starts again at the beginning, and the bytes in between
//     are skipped;
//   - a VIDEO_FRAME_FORMAT frame carries a video_format_t and the codec data
//     after it (H.264's avcC, HEVC's hvcC) instead of a picture: the codec
//     or the size changed;
//   - a VIDEO_FRAME_DRAIN frame (no bytes) is the end of the stream: the
//     reader takes the pictures its decoder still holds;
//   - generation counts the sink's flushes (a seek): the reader drops what
//     it holds from an older one.
// The clock: the writer says that at CLOCK_MONOTONIC clock_mono_ns the
// pipeline's running time was clock_running_ns, and that it goes on from
// there while playing (the pipeline's clock is the sound's). Each frame
// carries the running time it is for. The rectangle is in the page's own
// pixels; the UIProcess says where the page is on the screen (page[]).

#pragma once

#include <stdint.h>
#include <string.h>
#include <sched.h>
#include <time.h>

#define VIDEO_RING_SHM      "/bps5video"
#define VIDEO_RING_MAGIC    0x56535042u  // "BPSV"
#define VIDEO_RING_VERSION  1
#define VIDEO_RING_SLOTS    4
#define VIDEO_RING_DATA     (8u << 20)   // compressed bytes a slot holds; a power of two
#define VIDEO_RING_FRAMES   128u         // frames a slot holds; a power of two
#define VIDEO_RING_MAX_FRAME (VIDEO_RING_DATA / 2)
#define VIDEO_RING_MAX_CONFIG 4096u      // codec data
// The title's video thread is gone if it has not looked for this long.
#define VIDEO_RING_ALIVE_NS 500000000ull
// A picture shown this long after its time counts as dropped (a slot's
// "behind"): more than two frames at 60/s, under what lips show.
#define VIDEO_BEHIND_NS 40000000ll

enum {
  VIDEO_SLOT_FREE,
  VIDEO_SLOT_TAKING,  // a writer is setting it up; the reader skips it
  VIDEO_SLOT_ACTIVE,
};

enum {
  VIDEO_CODEC_NONE,
  VIDEO_CODEC_VP9,
  VIDEO_CODEC_H264,   // Annex B, or AVC with the avcC in the format
  VIDEO_CODEC_HEVC,   // the same, with hvcC
  VIDEO_CODEC_VP8,
};

#define VIDEO_FRAME_KEY    1u  // decodes alone
#define VIDEO_FRAME_FORMAT 2u  // a video_format_t and codec data, no picture
#define VIDEO_FRAME_DRAIN  4u  // the end of the stream, no bytes

typedef struct video_format {
  uint32_t codec;
  uint32_t width, height;  // from the caps; 0 if they did not say
  uint32_t config_size;    // bytes of codec data after this
} video_format_t;

typedef struct video_frame {
  uint64_t offset;       // where its bytes start: a count of bytes, mod VIDEO_RING_DATA
  uint32_t size;
  uint32_t flags;
  int64_t running_ns;    // when to show it, in the pipeline's running time
  uint32_t generation;   // the slot's when written
  uint32_t reserved;
} video_frame_t;

typedef struct video_slot {
  uint32_t state;        // VIDEO_SLOT_*
  int32_t pid;           // the writer's
  uint32_t generation;   // flushes so far (the writer's)
  uint32_t serial;       // a new one each time a writer takes the slot

  // The clock (the writer's), read with clock_seq: odd while it changes.
  uint32_t clock_seq;
  uint32_t playing;
  int64_t clock_running_ns;
  uint64_t clock_mono_ns;

  // WebKit's hole for this video, in the page's pixels (the writer's), read
  // with rect_seq. Width 0: not placed yet.
  uint32_t rect_seq;
  int32_t rect[4];       // x, y, width, height

  uint64_t data_written;    // the writer's
  uint64_t frames_written;  // the writer's
  uint64_t data_read;       // the reader's
  uint64_t frames_read;     // the reader's

  // The reader's counts, for the logs.
  uint64_t decoded;
  uint64_t shown;
  uint64_t late;            // decoded too late to be shown
  uint32_t width, height;   // of the last picture decoded
  // Shown while playing, but more than VIDEO_BEHIND_NS after their time:
  // the decoder is behind (in "shown" too). With "late", what the sink
  // tells WebKit was dropped (the page's getVideoPlaybackQuality(), which
  // sites read to choose a lower quality). Was reserved[0]: same layout.
  uint64_t behind;
  uint64_t reserved[7];

  video_frame_t frames[VIDEO_RING_FRAMES];
  uint8_t data[VIDEO_RING_DATA];
} video_slot_t;

typedef struct video_ring {
  uint32_t magic;
  uint32_t version;
  uint64_t consumer_ns;     // CLOCK_MONOTONIC when the title last looked
  // The largest video the title decodes in time: the WebProcess turns it
  // into WEBKIT_GST_VIDEO_DECODING_LIMIT.
  uint32_t max_width, max_height, max_fps;
  uint32_t reserved0;
  // Where the page is on the screen (the UIProcess's), read with page_seq:
  // the page's pixel (0, 0) at (page[0], page[1]); page[2] x page[3] of it
  // show.
  uint32_t page_seq;
  int32_t page[4];
  uint64_t reserved[8];
  video_slot_t slots[VIDEO_RING_SLOTS];
} video_ring_t;


static inline uint64_t
video_ring_now_ns(void) {
  struct timespec t;

  clock_gettime(CLOCK_MONOTONIC, &t);
  return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}


static inline int
video_ring_ready(const video_ring_t* ring) {
  return __atomic_load_n(&ring->magic, __ATOMIC_ACQUIRE) == VIDEO_RING_MAGIC
    && ring->version == VIDEO_RING_VERSION;
}


static inline int
video_ring_alive(const video_ring_t* ring) {
  uint64_t then = __atomic_load_n(&ring->consumer_ns, __ATOMIC_ACQUIRE);
  uint64_t now = video_ring_now_ns();

  return then && now >= then && now - then < VIDEO_RING_ALIVE_NS;
}


// Four ints under a sequence count (the rectangle, the page's place): one
// writer, any readers.
static inline void
video_ring_put4(uint32_t* seq, int32_t* to, const int32_t* from) {
  uint32_t s = __atomic_load_n(seq, __ATOMIC_RELAXED);

  __atomic_store_n(seq, s + 1, __ATOMIC_RELAXED);
  __atomic_thread_fence(__ATOMIC_RELEASE);
  for(int i = 0; i < 4; i++) {
    __atomic_store_n(&to[i], from[i], __ATOMIC_RELAXED);
  }
  __atomic_store_n(seq, s + 2, __ATOMIC_RELEASE);
}

static inline void
video_ring_get4(const uint32_t* seq, const int32_t* from, int32_t* to) {
  for(;;) {
    uint32_t s = __atomic_load_n(seq, __ATOMIC_ACQUIRE);
    if(s & 1) {
      sched_yield();  // the writer is between two stores: a few instructions
      continue;
    }
    for(int i = 0; i < 4; i++) {
      to[i] = __atomic_load_n(&from[i], __ATOMIC_RELAXED);
    }
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if(__atomic_load_n(seq, __ATOMIC_RELAXED) == s) {
      return;
    }
  }
}


// The clock (writer): running_ns of the pipeline at mono_ns.
static inline void
video_slot_set_clock(video_slot_t* slot, int64_t running_ns, uint64_t mono_ns,
                     int playing) {
  uint32_t s = __atomic_load_n(&slot->clock_seq, __ATOMIC_RELAXED);

  __atomic_store_n(&slot->clock_seq, s + 1, __ATOMIC_RELAXED);
  __atomic_thread_fence(__ATOMIC_RELEASE);
  __atomic_store_n(&slot->clock_running_ns, running_ns, __ATOMIC_RELAXED);
  __atomic_store_n(&slot->clock_mono_ns, mono_ns, __ATOMIC_RELAXED);
  __atomic_store_n(&slot->playing, (uint32_t)(playing != 0), __ATOMIC_RELAXED);
  __atomic_store_n(&slot->clock_seq, s + 2, __ATOMIC_RELEASE);
}

// The clock (reader): the running time at mono_ns, and in *playing whether
// it moves.
static inline int64_t
video_slot_running(const video_slot_t* slot, uint64_t mono_ns, int* playing) {
  for(;;) {
    uint32_t s = __atomic_load_n(&slot->clock_seq, __ATOMIC_ACQUIRE);
    if(s & 1) {
      sched_yield();
      continue;
    }
    int64_t running = __atomic_load_n(&slot->clock_running_ns, __ATOMIC_RELAXED);
    uint64_t then = __atomic_load_n(&slot->clock_mono_ns, __ATOMIC_RELAXED);
    uint32_t on = __atomic_load_n(&slot->playing, __ATOMIC_RELAXED);
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if(__atomic_load_n(&slot->clock_seq, __ATOMIC_RELAXED) != s) {
      continue;
    }
    if(playing) {
      *playing = (int)on;
    }
    if(on && mono_ns > then) {
      running += (int64_t)(mono_ns - then);
    }
    return running;
  }
}


// Writer: room for a frame of size bytes? Then where its bytes go (in
// *offset, a count; the pointer is data + offset % VIDEO_RING_DATA).
static inline int
video_slot_room(const video_slot_t* slot, uint32_t size, uint64_t* offset) {
  uint64_t at = slot->data_written;
  uint64_t in_ring = at % VIDEO_RING_DATA;

  if(size > VIDEO_RING_MAX_FRAME) {
    return 0;
  }
  if(in_ring + size > VIDEO_RING_DATA) {  // not past the end: from the start
    at += VIDEO_RING_DATA - in_ring;
  }
  uint64_t read = __atomic_load_n(&slot->data_read, __ATOMIC_ACQUIRE);
  uint64_t frames_read = __atomic_load_n(&slot->frames_read, __ATOMIC_ACQUIRE);
  if(at + size - read > VIDEO_RING_DATA
     || slot->frames_written - frames_read >= VIDEO_RING_FRAMES) {
    return 0;
  }
  *offset = at;
  return 1;
}

// Writer: the frame whose bytes are at offset (from video_slot_room), now
// in place, goes to the reader.
static inline void
video_slot_push(video_slot_t* slot, uint64_t offset, uint32_t size,
                uint32_t flags, int64_t running_ns) {
  video_frame_t* f = &slot->frames[slot->frames_written % VIDEO_RING_FRAMES];

  f->offset = offset;
  f->size = size;
  f->flags = flags;
  f->running_ns = running_ns;
  f->generation = __atomic_load_n(&slot->generation, __ATOMIC_RELAXED);
  f->reserved = 0;
  __atomic_store_n(&slot->data_written, offset + size, __ATOMIC_RELEASE);
  __atomic_store_n(&slot->frames_written, slot->frames_written + 1,
                   __ATOMIC_RELEASE);
}

// Reader: the next frame, if there is one (a copy of its description).
static inline int
video_slot_peek(const video_slot_t* slot, video_frame_t* frame) {
  uint64_t n = __atomic_load_n(&slot->frames_written, __ATOMIC_ACQUIRE);

  if(slot->frames_read >= n) {
    return 0;
  }
  *frame = slot->frames[slot->frames_read % VIDEO_RING_FRAMES];
  return 1;
}

static inline const uint8_t*
video_slot_bytes(const video_slot_t* slot, const video_frame_t* frame) {
  return slot->data + frame->offset % VIDEO_RING_DATA;
}

// Reader: done with that frame and its bytes.
static inline void
video_slot_release(video_slot_t* slot, const video_frame_t* frame) {
  __atomic_store_n(&slot->data_read, frame->offset + frame->size,
                   __ATOMIC_RELEASE);
  __atomic_store_n(&slot->frames_read, slot->frames_read + 1, __ATOMIC_RELEASE);
}
