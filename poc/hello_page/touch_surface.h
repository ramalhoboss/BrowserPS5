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

// The DualSense's touch pad as a laptop's (2026-11-17, BrowserPS5 RN): what
// the fingers on it mean, one poll of the controller at a time. No system
// call here: input.c gives the fingers it read and does what comes out, and
// touch_surface_test.c runs the same code on the PC.
//   one finger moving    the cursor follows it (slow: fine, fast: far)
//   one finger, a tap    a click
//   two fingers moving   the page scrolls with them, and glides on a little
//                        after they leave
// The pad's own click (the browser's tabs) is not a tap. The header is all of
// it (static functions), so a title's build needs no new file.

#pragma once

#include <math.h>
#include <string.h>

#define TOUCH_W 1920  // the pad's own units, as libScePad gives them
#define TOUCH_H 1080
#define TOUCH_TAP_MS 220       // a tap: down and up within this,
#define TOUCH_TAP_TRAVEL 36.f  // having moved less than this
#define TOUCH_START 7.f        // a finger that only rests does not move the cursor
#define TOUCH_GAIN_SLOW 0.5f   // pixels a unit, for a slow finger
#define TOUCH_GAIN_STEP 0.0125f  // and more for each unit a poll it goes faster
#define TOUCH_GAIN_MOST 2.5f
#define TOUCH_SCROLL 1.6f  // pixels scrolled a unit the two fingers move
#define TOUCH_GLIDE_FROM 6.f   // pixels a poll: slower than this when they leave, no glide
#define TOUCH_GLIDE_KEEP 0.955f  // of the speed, a poll later
#define TOUCH_GLIDE_END 0.8f

typedef struct touch_point {
  int x, y;  // 0..TOUCH_W-1, 0..TOUCH_H-1
  int id;    // the same while that finger stays down
} touch_point_t;

typedef struct touch_out {
  float move_x, move_y;      // the cursor, in pixels
  float scroll_x, scroll_y;  // the page, in pixels (positive: right, down)
  int tap;                   // 1: a click, now
} touch_out_t;

typedef struct touch_state {
  int fingers;            // of the poll before
  touch_point_t last[2];  // where they were
  unsigned down_ms;       // when the first finger came down,
  touch_point_t down;     // where,
  float travel;           // and the farthest from there it has been since
  int moving;             // past TOUCH_START: the cursor follows
  int spoiled;            // no tap when it leaves: two fingers were there, or the pad was clicked
  int waiting;            // after two fingers: the one that stays does nothing until all leave
  float glide_x, glide_y; // the scroll's speed, pixels a poll; gliding: it goes on by itself
  int gliding;
} touch_state_t;

// Whether what was read looks like fingers on the pad (a wrong layout of the
// controller's state would give anything).
static inline int
touch_plausible(int count, const touch_point_t* p) {
  if(count < 0 || count > 2) {
    return 0;
  }
  for(int i = 0; i < count; i++) {
    if(p[i].x < 0 || p[i].x >= TOUCH_W || p[i].y < 0 || p[i].y >= TOUCH_H) {
      return 0;
    }
  }
  return 1;
}

// One poll: count fingers at p (0 to 2), whether the pad is clicked down,
// the time. speed, scroll: 1 for the default (the browser's settings).
static inline void
touch_step(touch_state_t* s, int count, const touch_point_t* p, int clicked, unsigned now_ms, float speed,
           float scroll, touch_out_t* out) {
  memset(out, 0, sizeof(*out));
  if(!touch_plausible(count, p)) {
    count = 0;
  }
  if(count && s->gliding) {  // a finger back on the pad stops the page
    s->gliding = 0;
    s->glide_x = s->glide_y = 0;
  }
  if(count == 0) {
    if(s->fingers == 1 && !s->spoiled && !s->waiting && !clicked && now_ms - s->down_ms <= TOUCH_TAP_MS
       && s->travel <= TOUCH_TAP_TRAVEL) {
      out->tap = 1;
    }
    if(s->fingers == 2 && hypotf(s->glide_x, s->glide_y) >= TOUCH_GLIDE_FROM) {
      s->gliding = 1;
    }
    if(s->gliding) {
      out->scroll_x = s->glide_x;
      out->scroll_y = s->glide_y;
      s->glide_x *= TOUCH_GLIDE_KEEP;
      s->glide_y *= TOUCH_GLIDE_KEEP;
      if(hypotf(s->glide_x, s->glide_y) < TOUCH_GLIDE_END) {
        s->gliding = 0;
      }
    }
    if(!s->gliding) {
      s->glide_x = s->glide_y = 0;
    }
    s->fingers = 0;
    s->spoiled = s->waiting = s->moving = 0;
    s->travel = 0;
    return;
  }
  if(clicked) {
    s->spoiled = 1;
  }
  if(count == 1) {
    if(s->fingers == 2) {  // one of two left: the glide starts here, the other does nothing more
      s->waiting = 1;
      if(hypotf(s->glide_x, s->glide_y) >= TOUCH_GLIDE_FROM) {
        s->gliding = 1;
      }
    } else if(s->fingers == 0) {
      s->down_ms = now_ms;
      s->down = p[0];
      s->travel = 0;
      s->moving = 0;
      s->glide_x = s->glide_y = 0;
    } else if(p[0].id != s->last[0].id) {  // another finger in its place: from where it is
      s->down = p[0];
      s->spoiled = 1;
      s->moving = 0;
    } else if(!s->waiting) {
      const float dx = (float)(p[0].x - s->last[0].x), dy = (float)(p[0].y - s->last[0].y);
      const float far = hypotf(dx, dy);
      const float from_down = hypotf((float)(p[0].x - s->down.x), (float)(p[0].y - s->down.y));
      if(from_down > s->travel) {
        s->travel = from_down;
      }
      if(!s->moving && from_down >= TOUCH_START) {
        s->moving = 1;
      } else if(s->moving) {
        float gain = TOUCH_GAIN_SLOW + TOUCH_GAIN_STEP * far;
        gain = (gain > TOUCH_GAIN_MOST ? TOUCH_GAIN_MOST : gain) * speed;
        out->move_x = dx * gain;
        out->move_y = dy * gain;
      }
    }
    if(s->waiting && s->gliding) {  // (the glide goes on under the finger that stays)
      out->scroll_x = s->glide_x;
      out->scroll_y = s->glide_y;
      s->glide_x *= TOUCH_GLIDE_KEEP;
      s->glide_y *= TOUCH_GLIDE_KEEP;
      if(hypotf(s->glide_x, s->glide_y) < TOUCH_GLIDE_END) {
        s->gliding = 0;
      }
    }
    s->last[0] = p[0];
    s->fingers = 1;
    return;
  }
  // Two fingers: the page goes with them (the mean of the two).
  s->spoiled = 1;
  s->waiting = 0;
  if(s->fingers == 0) {
    s->down_ms = now_ms;
  }
  if(s->fingers == 2 && p[0].id == s->last[0].id && p[1].id == s->last[1].id) {
    const float dx = ((float)(p[0].x - s->last[0].x) + (float)(p[1].x - s->last[1].x)) / 2;
    const float dy = ((float)(p[0].y - s->last[0].y) + (float)(p[1].y - s->last[1].y)) / 2;
    out->scroll_x = -dx * TOUCH_SCROLL * scroll;
    out->scroll_y = -dy * TOUCH_SCROLL * scroll;
    s->glide_x = s->glide_x * 0.6f + out->scroll_x * 0.4f;
    s->glide_y = s->glide_y * 0.6f + out->scroll_y * 0.4f;
  } else {
    s->glide_x = s->glide_y = 0;
  }
  s->last[0] = p[0];
  s->last[1] = p[1];
  s->fingers = 2;
}
