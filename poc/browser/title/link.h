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

// Between the browser's title (title.cpp: the GPU, the screen, the
// controller and the keyboard) and WebKit's UIProcess in a payload
// (remote.c, under the browser's main.cpp); docs/03-video-e-gpu.md, G1.
//
// Two channels, because a title and a payload share no descriptors
// (docs/02, GPU step 1):
//   - TCP on 127.0.0.1:LINK_PORT, the UIProcess listening: link_msg_t
//     records, both ways;
//   - named shared memory LINK_VIEW_SHM, made by the UIProcess: the screen
//     in three buffers. The writer and the reader each own one buffer and
//     swap theirs with the shared middle one (link_view_t.middle), so
//     neither ever touches the buffer the other one is using.

#pragma once

#include <stdint.h>

#define LINK_PORT      9050
#define LINK_VIEW_SHM  "/bps5view"
#define LINK_MAGIC     0x42505335u  // "BPS5"
#define LINK_VERSION   1

#define LINK_WIDTH     1920
#define LINK_HEIGHT    1080
#define LINK_BUFFERS   3
#define LINK_FRAME_BYTES ((uint64_t)LINK_WIDTH * LINK_HEIGHT * 4)
#define LINK_HEADER_BYTES 4096
#define LINK_VIEW_BYTES (LINK_HEADER_BYTES + LINK_BUFFERS * LINK_FRAME_BYTES)

#define LINK_FRESH     0x100u  // in middle: a frame the reader has not taken

// The same header in C (remote.c) and C++ (title.cpp): plain fields, used
// with the compiler's __atomic builtins.
typedef struct link_view {
  uint32_t magic;
  uint32_t version;
  uint32_t width, height, stride;  // pixels, pixels, bytes
  uint32_t middle;                 // buffer index, | LINK_FRESH
  uint64_t published;              // frames the writer has handed over
} link_view_t;

static inline uint32_t*
link_buffer(link_view_t* view, unsigned index) {
  return (uint32_t*)((char*)view + LINK_HEADER_BYTES + index * LINK_FRAME_BYTES);
}

// Writer: hand over the buffer it wrote, get back the one to write next.
static inline unsigned
link_publish(link_view_t* view, unsigned back) {
  unsigned next = __atomic_exchange_n(&view->middle, back | LINK_FRESH,
                                      __ATOMIC_ACQ_REL);
  __atomic_fetch_add(&view->published, 1, __ATOMIC_RELAXED);
  return next & ~LINK_FRESH;
}

// Reader: the newest frame if there is one (its own buffer goes to the
// middle), else the buffer it already had.
static inline unsigned
link_take(link_view_t* view, unsigned front, int* fresh) {
  *fresh = 0;
  if(__atomic_load_n(&view->middle, __ATOMIC_ACQUIRE) & LINK_FRESH) {
    *fresh = 1;
    return __atomic_exchange_n(&view->middle, front, __ATOMIC_ACQ_REL)
      & ~LINK_FRESH;
  }
  return front;
}

// Fields are set, then magic last, with a release store.
static inline void
link_view_init(link_view_t* view) {
  view->width = LINK_WIDTH;
  view->height = LINK_HEIGHT;
  view->stride = LINK_WIDTH * 4;
  view->version = LINK_VERSION;
  __atomic_store_n(&view->middle, 1, __ATOMIC_RELAXED);
  __atomic_store_n(&view->published, 0, __ATOMIC_RELAXED);
  __atomic_store_n(&view->magic, LINK_MAGIC, __ATOMIC_RELEASE);
}

static inline int
link_view_ready(link_view_t* view) {
  return __atomic_load_n(&view->magic, __ATOMIC_ACQUIRE) == LINK_MAGIC
    && view->version == LINK_VERSION;
}

// The title's state, for a UIProcess about to open it (named shared memory,
// made by the title, unlinked when it closes). Opened from the home screen,
// the title sends the browser to elfldr itself (title.cpp, start_browser()),
// and that UIProcess must not close and reopen it (remote.c,
// launch_title()). A UIProcess that finds the title showing another
// UIProcess's pages must not close it either: closing the GPU's owner with
// a client drawing took the console down (2026-11-04 15:53). The title
// beats every ~0.1 s; a heartbeat older than LINK_TITLE_FRESH_MS is a title
// that is gone (killed or crashed).
#define LINK_TITLE_SHM       "/bps5title"
#define LINK_TITLE_BYTES     4096
#define LINK_TITLE_WAITING   1     // open, no UIProcess yet
#define LINK_TITLE_CONNECTED 2     // showing a UIProcess's frames
// The quit (LINK_ENDED for LINK_ENDED_QUIT): the title let go of its GPU
// client and still shows the screen, as in the pause; the UIProcess may now
// have the system close it (remote.c, close_title()). A title cannot end
// its own process without the system's error dialog (SIGSYS, "Erro no
// jogo"), and the system service gives a title no app id (0x8094000f,
// 2026-11-05 16:39). Never once the title has closed its GPU, video and
// sound: the system's close then took the console down (16:51 and 17:14,
// the old state 3, no longer used).
#define LINK_TITLE_READY     4
#define LINK_ENDED_QUIT      2     // link_msg_t ended.for_away: for the quit
#define LINK_TITLE_FRESH_MS  3000

typedef struct link_title {
  uint32_t magic;         // LINK_MAGIC
  uint32_t state;         // LINK_TITLE_*
  int32_t pid;
  // (2026-11-14, was reserved) The largest free piece of the title's direct memory, in MiB and never
  // under 1; 0 from a title before it. A media application has 3 to 3.3 GiB of it in all, and the pages,
  // the title's heap and the video decoder share it: the browser has WebKit collect when it is low.
  uint32_t direct_free_mib;
  uint64_t heartbeat_ms;  // CLOCK_MONOTONIC
  // The capture (LINK_CAPTURE, title/capture.c): the pieces fully written,
  // and 1 while one is being recorded or written. 0 from a title before it.
  uint32_t capture_done;
  uint32_t capture_busy;
  // (2026-11-14) 1: the system keyboard's library is loaded and bound in the title (sysime.c), so
  // LINK_IME_OPEN opens it; 0 from a title that has not, or from one before this: the browser's own keyboard.
  uint32_t ime_ready;
} link_title_t;

// Messages. The text of LINK_IME_OPEN and LINK_IME_DONE is UTF-8 and
// follows the record (text_bytes, no terminator).
typedef enum link_type {
  LINK_HELLO = 1,   // title -> UI: user_id (the user in front)
  LINK_INPUT,       // title -> UI: an input_event_t (hello_page/input.h)
  LINK_VSYNC,       // title -> UI: a frame went to the screen
  LINK_IME_OPEN,    // UI -> title: open the keyboard (kind, enter, text)
  LINK_IME_DONE,    // title -> UI: ok, and the text if ok
  LINK_QUIT,        // UI -> title: the browser is closing
  // The pause (docs/03-video-e-gpu.md, "Pausa"): closing the title from
  // the system while the WebProcess draws through its GPU took the console
  // down, and a close always comes after the system's menu or home screen
  // is over the app, which the title sees in its status.
  LINK_AWAY,        // title -> UI: away (1: the system is over the app;
                    // 0: back), test (1: faked by /app0/away_test)
  LINK_ENDED,       // UI -> title: a WebProcess ended (pid); for_away (1:
                    // ended for the pause, so the title can let go of its
                    // GPU client; LINK_ENDED_QUIT: for the quit, the title
                    // lets go of it and says LINK_TITLE_READY)
  // The D-pad's choice and the settings (docs/07-navegacao-e-
  // definicoes.md). A title from before these ignores them: the browser
  // still works, only the cursor stays where it was.
  LINK_CURSOR,      // UI -> title: put the cursor at (x, y)
  LINK_CONFIG,      // UI -> title: the sticks' speeds, in percent
  LINK_SOUND,       // UI -> title: play an interface sound (LINK_SOUND_*)
  // The DualSense, felt (title/feel.h; docs/07, "O comando"). A title from
  // before these ignores them.
  LINK_FEEL,        // UI -> title: a short vibration
  LINK_TRIGGERS,    // UI -> title: the adaptive triggers (LINK_TRIGGERS_*)
  LINK_LIGHT,       // UI -> title: the light bar's colour (on 0: the system's)
  LINK_GAMEPAD,     // UI -> title: the controller to a web game as it is (input_set_raw)
  // The video of the browser (title/capture.h; docs/09-video.md): the
  // demo's UIProcess (make DEMO=1) records the screen and the sound in
  // pieces. The text is the piece's name. A title from before it ignores it.
  LINK_CAPTURE,     // UI -> title: on 1: start a capture of up to seconds; on 0: end it
} link_type_t;

enum {
  LINK_TRIGGERS_OFF,
  LINK_TRIGGERS_CLICK,  // a light click partway down (the radial menus)
};

// The interface's sounds, made by the title (short and quiet: the D-pad's
// ring moving, a panel opening or closing, a choice made).
enum {
  LINK_SOUND_MOVE,
  LINK_SOUND_OPEN,
  LINK_SOUND_CLOSE,
  LINK_SOUND_SELECT,
  LINK_SOUNDS
};

#define LINK_TEXT_MAX 4096

typedef struct link_msg {
  uint32_t type;
  uint32_t text_bytes;
  union {
    struct {
      int32_t user_id;
    } hello;
    struct {
      int32_t kind;
      float x, y, dx, dy;
      int32_t pressed;
    } input;
    struct {
      uint64_t frames;
    } vsync;
    struct {
      int32_t kind;   // ime_kind_t
      int32_t enter;  // ime_enter_t
    } ime_open;
    struct {
      int32_t ok;
    } ime_done;
    struct {
      int32_t away;
      int32_t test;
      // (2026-11-14, BrowserPS5 RN) 1: the PS5's home screen has been over the app for a moment: the browser
      // quits now, its own clean way (as its menu's "Sair"). A close from the system took the console down
      // every time since the title has decoded the video (docs/08-4k.md); this way the system finds no
      // title to close. 0 from a title before this.
      int32_t leave;
    } away;
    struct {
      int32_t pid;
      int32_t for_away;
    } ended;
    struct {
      float x, y;
    } cursor;
    struct {
      int32_t cursor_percent;
      int32_t scroll_percent;
    } config;
    struct {
      int32_t which;    // LINK_SOUND_*
      int32_t percent;  // its loudness
    } sound;
    struct {
      int32_t large, small;  // the motors, 0..255
      int32_t ms;            // how long, 10..1000
    } feel;
    struct {
      int32_t mode;      // LINK_TRIGGERS_*
      int32_t strength;  // 1..8
    } triggers;
    struct {
      int32_t on;
      int32_t r, g, b;
    } light;
    struct {
      int32_t on;
    } gamepad;
    struct {
      int32_t on;
      int32_t seconds;  // at most 600
    } capture;
  } u;
} link_msg_t;
