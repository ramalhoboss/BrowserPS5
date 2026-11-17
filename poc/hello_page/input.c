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

// The controller, read with libScePad from inside the app process (the app
// in front gets the pad), turned into input_event_t.

#ifndef POC_NAME  // the browser builds these too, under its own name
#define POC_NAME "hello_page"
#endif

#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "input.h"
#include "poc_log.h"

// libScePad and libSceUserService (the SDK has the stubs, not the headers).
int scePadInit(void);
int scePadOpen(int user_id, int type, int index, const void* param);
int scePadGetHandle(int user_id, int type, int index);
int scePadReadState(int handle, void* data);
int sceUserServiceInitialize(const void* params);
int sceUserServiceGetInitialUser(int* user_id);
int sceUserServiceGetForegroundUser(int* user_id);

// The start of what scePadReadState fills, as in the public homebrew
// headers (OpenOrbis). The rest (motion, touch pad, ...) is not used; the
// buffer is big enough for all of it.
typedef struct pad_state {
  uint32_t buttons;
  uint8_t lx, ly, rx, ry;  // 0..255, 128 in the middle
  uint8_t l2, r2;
  uint8_t rest[512];
} pad_state_t;

#define PAD_L3       0x00000002
#define PAD_R3       0x00000004
#define PAD_OPTIONS  0x00000008
#define PAD_UP       0x00000010
#define PAD_RIGHT    0x00000020
#define PAD_DOWN     0x00000040
#define PAD_LEFT     0x00000080
#define PAD_L2       0x00000100
#define PAD_R2       0x00000200
#define PAD_L1       0x00000400
#define PAD_R1       0x00000800
#define PAD_TRIANGLE 0x00001000
#define PAD_CIRCLE   0x00002000
#define PAD_CROSS    0x00004000
#define PAD_SQUARE   0x00008000
#define PAD_TOUCHPAD 0x00100000  // its click
// The system has the controller: its keyboard or menu is open.
#define PAD_INTERCEPTED 0x80000000

#define DEAD_ZONE      24     // of 128
#define CURSOR_SPEED   22.0f  // pixels per poll at full tilt
#define SCROLL_SPEED   60.0f
#define DPAD_STEP      120.0f
#define POLL_US        16000

#ifdef INPUT_TOUCH_SURFACE
// 2026-11-17 (BrowserPS5 RN's title): the touch pad's surface as a laptop's
// (touch_surface.h): a finger moves the cursor, a tap clicks, two fingers
// scroll. The fingers are in what scePadReadState fills, after the sticks,
// the triggers and the motion (the public homebrew headers' ScePadData:
// touchData at byte 52: how many, 7 bytes more, then two fingers of 8 bytes,
// x and y in 16 bits each and the finger's id). The file /app0/no-touch-cursor
// turns it off; /app0/touch-probe writes what is read to the log (to see the
// layout on a console: 20 lines, one a second while a finger is down).
#include <time.h>

#include "touch_surface.h"

#define PAD_TOUCH_AT 52

static touch_state_t touch;  // (the input thread's alone)

static int
pad_fingers(const void* state, touch_point_t p[2]) {
  const uint8_t* b = (const uint8_t*)state + PAD_TOUCH_AT;
  for(int i = 0; i < 2; i++) {
    const uint8_t* t = b + 8 + i * 8;
    p[i].x = t[0] | t[1] << 8;
    p[i].y = t[2] | t[3] << 8;
    p[i].id = t[4];
  }
  return b[0];
}

static unsigned
touch_ms(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (unsigned)(t.tv_sec * 1000u + t.tv_nsec / 1000000u);
}

// /app0/touch-probe: the state's first bytes as they came, for the log.
static void
touch_probe(const void* state, unsigned now) {
  static unsigned said_ms, lines;
  const uint8_t* b = (const uint8_t*)state;
  if(lines >= 20 || now - said_ms < 1000) {
    return;
  }
  char hex[3 * 48 + 1];
  for(int i = 0; i < 48; i++) {
    static const char digits[] = "0123456789abcdef";
    const uint8_t v = b[PAD_TOUCH_AT - 8 + i];
    hex[i * 3] = digits[v >> 4];
    hex[i * 3 + 1] = digits[v & 15];
    hex[i * 3 + 2] = ' ';
  }
  hex[3 * 48] = 0;
  said_ms = now;
  lines++;
  POC_LOG("touch probe %u: bytes %d.. %s", lines, PAD_TOUCH_AT - 8, hex);
}
#endif

static struct {
  input_handler_t handler;
  int user_id;
  int handle;
  int width, height;
  pthread_mutex_t lock;
  float x, y;
  float cursor_speed, scroll_speed;  // input_set_speed()
  int raw;                           // input_set_raw()
  int raw_fresh;                     // just on: the state goes out once
} in = {.lock = PTHREAD_MUTEX_INITIALIZER, .cursor_speed = CURSOR_SPEED,
        .scroll_speed = SCROLL_SPEED};


// -1..1 with a dead zone in the middle, squared for finer slow moves.
static float
axis(uint8_t v) {
  int d = (int)v - 128;
  float f;

  if(d > -DEAD_ZONE && d < DEAD_ZONE) {
    return 0;
  }
  f = (float)((d < 0 ? -d : d) - DEAD_ZONE) / (128 - DEAD_ZONE);
  if(f > 1) {
    f = 1;
  }
  return d < 0 ? -f * f : f * f;
}


static void
emit(input_kind_t kind, float dx, float dy, int pressed) {
  input_event_t e = {kind, 0, 0, dx, dy, pressed};

  input_cursor(&e.x, &e.y);
  in.handler(&e);
}


// -1..1 as it is, with only a small dead zone: a game makes its own.
static float
raw_axis(uint8_t v) {
  float f = ((int)v - 128) / 127.0f;
  if(f > -0.06f && f < 0.06f) {
    return 0;
  }
  return f < -1 ? -1 : f > 1 ? 1 : f;
}


// INPUT_PAD, when what the game would see changed (input.h).
static void
pad_raw(const pad_state_t* pad, uint32_t buttons, int fresh) {
  static uint32_t last_bits;
  static int last_axes[4];
  const uint32_t keep = 0x0000fffe;  // the buttons, not the touch pad (the way out)
  float axes[4] = {raw_axis(pad->lx), raw_axis(pad->ly), raw_axis(pad->rx), raw_axis(pad->ry)};
  uint32_t bits = (buttons & keep) | (uint32_t)(pad->l2 >> 4) << INPUT_PAD_L2_SHIFT
                  | (uint32_t)(pad->r2 >> 4) << INPUT_PAD_R2_SHIFT;
  int changed = fresh || bits != last_bits;
  for(int i = 0; i < 4; i++) {
    int q = (int)(axes[i] * 64);
    if(q != last_axes[i]) {
      last_axes[i] = q;
      changed = 1;
    }
  }
  if(!changed) {
    return;
  }
  last_bits = bits;
  input_event_t e = {INPUT_PAD, axes[0], axes[1], axes[2], axes[3], (int)bits};
  in.handler(&e);
}


static void*
input_thread(void* arg) {
  pad_state_t pad;
  uint32_t old = 0;
  uint32_t old_raw = 0;
  unsigned errors = 0;

  (void)arg;
  for(;;) {
    memset(&pad, 0, sizeof(pad));
    int err = scePadReadState(in.handle, &pad);
    if(err) {
      if(errors++ < 3) {
        POC_LOG("scePadReadState: 0x%x", err);
      }
      usleep(POLL_US);
      continue;
    }
    pthread_mutex_lock(&in.lock);
    float cursor_speed = in.cursor_speed;
    float scroll_speed = in.scroll_speed;
    int raw = in.raw, fresh = in.raw_fresh;
    in.raw_fresh = 0;
    pthread_mutex_unlock(&in.lock);
    if(pad.buttons != old_raw) {
      if(!raw) {  // a game's presses are not logged, there are many
        POC_LOG("pad buttons 0x%x (sticks %u,%u %u,%u)", pad.buttons, pad.lx,
                pad.ly, pad.rx, pad.ry);
      }
      old_raw = pad.buttons;
    }
    uint32_t now = pad.buttons;
    if(now & PAD_INTERCEPTED) {  // as if let go
      now = 0;
      pad.lx = pad.ly = pad.rx = pad.ry = 128;
      pad.l2 = pad.r2 = 0;
    }
    uint32_t pressed = now & ~old;
    uint32_t released = old & ~now;

    if(raw) {
      if(pressed & PAD_TOUCHPAD) {
        emit(INPUT_TOUCHPAD, 0, 0, 0);
      }
      pad_raw(&pad, now, fresh);
#ifdef INPUT_TOUCH_SURFACE
      memset(&touch, 0, sizeof(touch));  // (a game has the controller: a finger then is not one for the cursor)
#endif
      old = now;  // back to the cursor: what is held is not a press
      usleep(POLL_US);
      continue;
    }

#ifdef INPUT_TOUCH_SURFACE
    {
      static int touch_off = -1, probe, tap_up, said;
      if(touch_off < 0) {
        touch_off = !access("/app0/no-touch-cursor", F_OK);
        probe = !access("/app0/touch-probe", F_OK);
        POC_LOG("touch pad: %s%s", touch_off ? "only its click (/app0/no-touch-cursor)"
                                              : "a finger moves the cursor, a tap clicks, two fingers scroll",
                probe ? "; what is read goes to the log (/app0/touch-probe)" : "");
      }
      touch_point_t fingers[2];
      touch_out_t t;
      const unsigned ms = touch_ms();
      // While the system has the controller, no finger counts.
      const int count = pad.buttons & PAD_INTERCEPTED ? 0 : pad_fingers(&pad, fingers);
      if(probe && count) {
        touch_probe(&pad, ms);
      }
      if(tap_up) {  // the tap's click ends a poll after it began, as a press of cross would
        emit(INPUT_BUTTON_UP, 0, 0, 0);
        tap_up = 0;
      }
      if(!touch_off) {
        const int before = touch.fingers;
        touch_step(&touch, count, fingers, !!(now & PAD_TOUCHPAD), ms, cursor_speed / CURSOR_SPEED,
                   scroll_speed / SCROLL_SPEED, &t);
        if(touch.fingers != before && said < 40) {  // the first ones in the log (is the layout right?)
          said++;
          POC_LOG("touch pad: %d finger(s)%s, the first at %d,%d (id %d)", touch.fingers,
                  touch_plausible(count, fingers) ? "" : " (what was read is not a finger: left out)", fingers[0].x,
                  fingers[0].y, fingers[0].id);
        }
        if(t.move_x || t.move_y) {
          pthread_mutex_lock(&in.lock);
          in.x += t.move_x;
          in.y += t.move_y;
          in.x = in.x < 0 ? 0 : in.x > in.width - 1 ? in.width - 1 : in.x;
          in.y = in.y < 0 ? 0 : in.y > in.height - 1 ? in.height - 1 : in.y;
          pthread_mutex_unlock(&in.lock);
          emit(INPUT_MOVE, 0, 0, !!(now & PAD_CROSS));
        }
        if(t.scroll_x || t.scroll_y) {
          emit(INPUT_SCROLL, t.scroll_x, t.scroll_y, 0);
        }
        if(t.tap && !(now & PAD_CROSS)) {
          if(said < 40) {
            said++;
            POC_LOG("touch pad: a tap, a click");
          }
          emit(INPUT_BUTTON_DOWN, 0, 0, 1);
          tap_up = 1;
        }
      }
    }
#endif

    float mx = axis(pad.lx) * cursor_speed;
    float my = axis(pad.ly) * cursor_speed;
    if(mx || my) {
      pthread_mutex_lock(&in.lock);
      in.x += mx;
      in.y += my;
      in.x = in.x < 0 ? 0 : in.x > in.width - 1 ? in.width - 1 : in.x;
      in.y = in.y < 0 ? 0 : in.y > in.height - 1 ? in.height - 1 : in.y;
      pthread_mutex_unlock(&in.lock);
      emit(INPUT_MOVE, 0, 0, !!(now & PAD_CROSS));
    }

    if(pressed & PAD_CROSS) {
      emit(INPUT_BUTTON_DOWN, 0, 0, 1);
    }
    if(released & PAD_CROSS) {
      emit(INPUT_BUTTON_UP, 0, 0, 0);
    }

    // The right stick, then the D-pad on its own with pressed 1: the
    // browser moves from element to element with it (nav.js). A UI from
    // before that takes it as a scroll of DPAD_STEP, as it always was.
    float sx = axis(pad.rx) * scroll_speed;
    float sy = axis(pad.ry) * scroll_speed;
    if(sx || sy) {
      emit(INPUT_SCROLL, sx, sy, 0);
    }
    float px = 0, py = 0;
    if(pressed & PAD_UP) {
      py -= DPAD_STEP;
    }
    if(pressed & PAD_DOWN) {
      py += DPAD_STEP;
    }
    if(pressed & PAD_LEFT) {
      px -= DPAD_STEP;
    }
    if(pressed & PAD_RIGHT) {
      px += DPAD_STEP;
    }
    if(px || py) {
      emit(INPUT_SCROLL, px, py, 1);
    }

    if(pressed & PAD_CIRCLE) {
      emit(INPUT_BACK, 0, 0, 0);
    }
    if(pressed & PAD_L1) {
      // The same event, as always; dx -1 tells BrowserPS5 RN's keyboard,
      // where L1 moves the caret and circle closes (2026-11-11).
      emit(INPUT_BACK, -1, 0, 0);
    }
    if(pressed & PAD_R1) {
      emit(INPUT_FORWARD, 0, 0, 0);
    }
    if(pressed & PAD_TRIANGLE) {
      emit(INPUT_KEYBOARD, 0, 0, 0);
    }
    if(pressed & PAD_SQUARE) {
      emit(INPUT_ENTER, 0, 0, 0);
    }
#ifdef INPUT_SQUARE_REPEAT
    // BrowserPS5 RN's title (2026-11-11): square held goes on, as a key
    // held on a keyboard, 450 ms after the press and then every ~64 ms, with
    // dx 1: RN's own keyboard deletes on with it, as the PS5's does, and
    // the browser takes no notice of it anywhere else (main.cpp).
    {
      static unsigned square_polls;
      const unsigned first = 450000 / POLL_US, every = 64000 / POLL_US ? 64000 / POLL_US : 1;
      if(now & PAD_SQUARE) {
        square_polls++;
        if(square_polls >= first && (square_polls - first) % every == 0) {
          emit(INPUT_ENTER, 1, 0, 0);
        }
      } else {
        square_polls = 0;
      }
    }
#endif
    // The triggers, down and up: held, a radial menu is open.
    if(pressed & PAD_L2) {
      emit(INPUT_RADIAL, -1, 0, 1);
    }
    if(released & PAD_L2) {
      emit(INPUT_RADIAL, -1, 0, 0);
    }
    if(pressed & PAD_R2) {
      emit(INPUT_RADIAL, 1, 0, 1);
    }
    if(released & PAD_R2) {
      emit(INPUT_RADIAL, 1, 0, 0);
    }
    if(pressed & PAD_TOUCHPAD) {
      emit(INPUT_TOUCHPAD, 0, 0, 0);
    }
    if(pressed & PAD_L3) {
      emit(INPUT_STICK, -1, 0, 1);
    }
    if(pressed & PAD_R3) {
      emit(INPUT_STICK, 1, 0, 1);
    }
    if(pressed & PAD_OPTIONS) {
      emit(INPUT_QUIT, 0, 0, 0);
    }

    old = now;
    usleep(POLL_US);
  }
  return 0;
}


int
input_start(input_handler_t handler, int width, int height) {
  pthread_t thread;
  int user_id = -1;
  int err;

  in.handler = handler;
  in.width = width;
  in.height = height;
  in.x = width / 2.0f;
  in.y = height / 2.0f;

  if((err=sceUserServiceInitialize(0))) {
    POC_LOG("sceUserServiceInitialize: 0x%x", err);  // fine if done already
  }
  // Our app was not started by a user, so it has no initial user
  // (0x80960006, 2026-11-03): take whoever is in front.
  if((err=sceUserServiceGetInitialUser(&user_id))
     && (err=sceUserServiceGetForegroundUser(&user_id))) {
    POC_LOG("sceUserServiceGetForegroundUser: 0x%x", err);
    return err;
  }
  if((err=scePadInit())) {
    POC_LOG("scePadInit: 0x%x", err);
    return err;
  }
  // If the system has the pad open for this process already, use that.
  in.handle = scePadOpen(user_id, 0, 0, 0);
  if(in.handle < 0) {
    POC_LOG("scePadOpen(user 0x%x): 0x%x, trying scePadGetHandle", user_id,
            in.handle);
    in.handle = scePadGetHandle(user_id, 0, 0);
  }
  if(in.handle < 0) {
    POC_LOG("scePadGetHandle: 0x%x", in.handle);
    return in.handle;
  }
  POC_LOG("controller open: user 0x%x, handle %d", user_id, in.handle);
  in.user_id = user_id;

  if((err=pthread_create(&thread, 0, input_thread, 0))) {
    return err;
  }
  pthread_detach(thread);
  return 0;
}


void
input_cursor(float* x, float* y) {
  pthread_mutex_lock(&in.lock);
  *x = in.x;
  *y = in.y;
  pthread_mutex_unlock(&in.lock);
}


void
input_set_cursor(float x, float y) {
  pthread_mutex_lock(&in.lock);
  in.x = x < 0 ? 0 : x > in.width - 1 ? in.width - 1 : x;
  in.y = y < 0 ? 0 : y > in.height - 1 ? in.height - 1 : y;
  pthread_mutex_unlock(&in.lock);
}


void
input_set_speed(int cursor_percent, int scroll_percent) {
  cursor_percent = cursor_percent < 25 ? 25 : cursor_percent > 400 ? 400 : cursor_percent;
  scroll_percent = scroll_percent < 25 ? 25 : scroll_percent > 400 ? 400 : scroll_percent;
  pthread_mutex_lock(&in.lock);
  in.cursor_speed = CURSOR_SPEED * cursor_percent / 100.0f;
  in.scroll_speed = SCROLL_SPEED * scroll_percent / 100.0f;
  pthread_mutex_unlock(&in.lock);
}


void
input_set_raw(int on) {
  pthread_mutex_lock(&in.lock);
  if(on && !in.raw) {
    in.raw_fresh = 1;
  }
  in.raw = !!on;
  pthread_mutex_unlock(&in.lock);
  POC_LOG("controller %s", on ? "to the page's game (raw)" : "back to the cursor");
}


int
input_user(void) {
  return in.user_id;
}


int
input_handle(void) {
  return in.handle;
}
