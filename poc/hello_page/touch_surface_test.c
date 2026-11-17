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

// The PC test of touch_surface.h: fingers on the pad, a poll (16 ms) at a
// time, and what comes out of them.
//   cc -O2 -Wall -Werror touch_surface_test.c -lm -o touch_test && ./touch_test

#include <stdio.h>

#include "touch_surface.h"

static int passed, failed;

#define CHECK(cond, ...) do { \
    if(cond) { \
      passed++; \
      printf("PASS "); \
    } else { \
      failed++; \
      printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    } \
    printf(__VA_ARGS__); \
    printf("\n"); \
  } while(0)

typedef struct sum {
  float mx, my, sx, sy;
  int taps, polls_scrolling;
} sum_t;

static touch_state_t s;
static unsigned now;

static void
poll(sum_t* sum, int count, int x0, int y0, int x1, int y1, int clicked) {
  touch_point_t p[2] = {{x0, y0, 1}, {x1, y1, 2}};
  touch_out_t out;
  touch_step(&s, count, p, clicked, now, 1, 1, &out);
  now += 16;
  sum->mx += out.move_x;
  sum->my += out.move_y;
  sum->sx += out.scroll_x;
  sum->sy += out.scroll_y;
  sum->taps += out.tap;
  sum->polls_scrolling += out.scroll_x || out.scroll_y;
}

static void
rest(sum_t* sum, int polls) {
  for(int i = 0; i < polls; i++) {
    poll(sum, 0, 0, 0, 0, 0, 0);
  }
}

int
main(void) {
  sum_t a = {0};

  // A tap: down four polls without moving, then up.
  for(int i = 0; i < 4; i++) {
    poll(&a, 1, 900, 500, 0, 0, 0);
  }
  rest(&a, 2);
  CHECK(a.taps == 1 && a.mx == 0 && a.my == 0, "a tap is one click and no move (%d, %.1f %.1f)", a.taps, a.mx, a.my);

  // A finger that rests a long time and leaves: no click.
  a = (sum_t){0};
  for(int i = 0; i < 40; i++) {
    poll(&a, 1, 900 + (i & 1), 500, 0, 0, 0);
  }
  rest(&a, 2);
  CHECK(a.taps == 0, "a finger that rested 640 ms is not a tap (%d)", a.taps);
  CHECK(a.mx == 0 && a.my == 0, "and one that only trembles moves nothing (%.1f %.1f)", a.mx, a.my);

  // A slow move right: 3 units a poll for 100 polls.
  a = (sum_t){0};
  for(int i = 0; i < 100; i++) {
    poll(&a, 1, 400 + i * 3, 500, 0, 0, 0);
  }
  rest(&a, 2);
  const float slow = a.mx;
  CHECK(a.taps == 0 && slow > 100 && slow < 200 && a.my == 0, "a slow finger over 300 units: %.0f pixels, fine", slow);

  // The same distance fast: 60 units a poll for 5 polls.
  a = (sum_t){0};
  for(int i = 0; i <= 5; i++) {
    poll(&a, 1, 400 + i * 60, 500, 0, 0, 0);
  }
  rest(&a, 2);
  CHECK(a.mx > slow * 1.5f, "the same 300 units fast: %.0f pixels, farther", a.mx);
  CHECK(a.taps == 0, "a quick swipe is not a tap (%d)", a.taps);

  // Across the pad at a middling speed: about the screen's width.
  a = (sum_t){0};
  for(int i = 0; i <= 45; i++) {
    poll(&a, 1, 60 + i * 40, 500, 0, 0, 0);
  }
  rest(&a, 2);
  CHECK(a.mx > 1500 && a.mx < 2300, "across the pad in 0.7 s: %.0f pixels (a screen is 1920)", a.mx);

  // Another finger takes its place in the same poll (another id): no jump.
  a = (sum_t){0};
  for(int i = 0; i < 20; i++) {
    poll(&a, 1, 300 + i * 10, 500, 0, 0, 0);
  }
  {
    touch_point_t p[2] = {{1700, 900, 7}, {0, 0, 0}};
    touch_out_t out;
    touch_step(&s, 1, p, 0, now, 1, 1, &out);
    now += 16;
    CHECK(out.move_x == 0 && out.move_y == 0, "another finger in its place: the cursor does not jump (%.0f %.0f)",
          out.move_x, out.move_y);
  }
  rest(&a, 2);

  // Two fingers up the pad: the page goes down, then glides on and stops.
  a = (sum_t){0};
  for(int i = 0; i <= 12; i++) {
    poll(&a, 2, 800, 800 - i * 25, 1100, 820 - i * 25, 0);
  }
  const float with_fingers = a.sy;
  CHECK(with_fingers > 400 && a.sx == 0 && a.mx == 0 && a.my == 0,
        "two fingers 300 units up: the page %.0f pixels down, the cursor still", with_fingers);
  rest(&a, 200);
  CHECK(a.sy > with_fingers * 1.5f && a.taps == 0, "it glides on after they leave: %.0f pixels in all, no click", a.sy);
  const int polls = a.polls_scrolling;
  rest(&a, 50);
  CHECK(a.polls_scrolling == polls && polls < 150, "and stops by itself (%d polls of scrolling)", polls);

  // A glide is stopped by a finger.
  a = (sum_t){0};
  for(int i = 0; i <= 12; i++) {
    poll(&a, 2, 800, 800 - i * 25, 1100, 820 - i * 25, 0);
  }
  rest(&a, 5);
  const float before = a.sy;
  for(int i = 0; i < 30; i++) {
    poll(&a, 1, 900, 500, 0, 0, 0);
  }
  CHECK(a.sy == before, "a finger on the pad stops the glide (%.0f then %.0f)", before, a.sy);
  rest(&a, 2);
  CHECK(a.taps == 0, "(and that finger, 480 ms down, is no tap)");

  // Two fingers, one leaves first: the other does not move the cursor, no click.
  a = (sum_t){0};
  for(int i = 0; i <= 10; i++) {
    poll(&a, 2, 800, 700 - i * 4, 1100, 720 - i * 4, 0);
  }
  for(int i = 0; i < 3; i++) {
    poll(&a, 1, 1100 + i * 30, 680, 0, 0, 0);
  }
  rest(&a, 3);
  CHECK(a.mx == 0 && a.my == 0 && a.taps == 0, "one of two left behind: no move, no click (%.0f %.0f, %d)", a.mx, a.my,
        a.taps);

  // A slow two-finger scroll that stops before leaving: no glide.
  a = (sum_t){0};
  for(int i = 0; i <= 20; i++) {
    poll(&a, 2, 800, 700 - i * 2, 1100, 720 - i * 2, 0);
  }
  for(int i = 0; i < 10; i++) {
    poll(&a, 2, 800, 660, 1100, 680, 0);
  }
  const float stopped = a.sy;
  rest(&a, 30);
  CHECK(a.sy == stopped, "two fingers that stop and then leave: no glide (%.0f then %.0f)", stopped, a.sy);

  // The pad's own click with a finger on it: the browser's tabs, not a tap.
  a = (sum_t){0};
  poll(&a, 1, 900, 500, 0, 0, 0);
  poll(&a, 1, 900, 500, 0, 0, 1);
  poll(&a, 1, 900, 500, 0, 0, 0);
  rest(&a, 2);
  CHECK(a.taps == 0, "the pad clicked down is not a tap (%d)", a.taps);

  // What is not fingers (a wrong layout would give anything) does nothing.
  a = (sum_t){0};
  for(int i = 0; i < 10; i++) {
    poll(&a, 1, 40000 + i * 100, 500, 0, 0, 0);
    poll(&a, 7, 100, 100, 200, 200, 0);
  }
  rest(&a, 2);
  CHECK(a.mx == 0 && a.my == 0 && a.sx == 0 && a.sy == 0 && a.taps == 0, "what cannot be fingers does nothing");

  printf("%d PASS, %d FAIL\n", passed, failed);
  return failed != 0;
}
