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

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// What the controller does (milestone M3):
//   left stick         moves the cursor
//   right stick, D-pad scrolls
//   cross              clicks (held: drags)
//   circle, L1         back
//   R1                 forward
//   triangle           the system keyboard, for the text field in focus
//   square             Enter
//   options            quits
//   L2, R2             the browser's radial menus (INPUT_RADIAL)
//   touch pad (click)  the browser's tabs (INPUT_TOUCHPAD)
//   touch pad, fingers (BrowserPS5 RN's title, INPUT_TOUCH_SURFACE): one moves
//                      the cursor (INPUT_MOVE), a tap clicks (INPUT_BUTTON_DOWN
//                      and _UP), two scroll (INPUT_SCROLL): touch_surface.h
// While the system has the controller (its keyboard is open), none.
typedef enum input_kind {
  INPUT_MOVE,
  INPUT_BUTTON_DOWN,
  INPUT_BUTTON_UP,
  INPUT_SCROLL,
  INPUT_BACK,
  INPUT_FORWARD,
  INPUT_KEYBOARD,
  INPUT_ENTER,
  INPUT_QUIT,
  // L2 (dx -1) or R2 (dx +1): pressed 1 when it goes down, 0 when it comes
  // back up (the browser's radial menus, docs/07). Last, so a UI from
  // before it does not see its kinds move.
  INPUT_RADIAL,
  // The touch pad, clicked: the browser's tabs (docs/07).
  INPUT_TOUCHPAD,
  // A stick clicked: L3 (dx -1) or R3 (dx +1; the browser's developer
  // console).
  INPUT_STICK,
  // input_set_raw(1) only: the controller as it is, for a web game's Gamepad
  // API (docs/07, "Modo jogo"). x, y: the left stick and dx, dy: the right
  // one, -1..1 (a small dead zone); pressed: the INPUT_PAD_* buttons, with
  // how far L2 is pulled (0..15) in bits 21-24 and R2 in bits 25-28. Sent
  // when it changes. Not the cursor: x, y are not where it is.
  INPUT_PAD,
} input_kind_t;

// INPUT_PAD's buttons (libScePad's bits).
#define INPUT_PAD_L3       0x00000002
#define INPUT_PAD_R3       0x00000004
#define INPUT_PAD_OPTIONS  0x00000008
#define INPUT_PAD_UP       0x00000010
#define INPUT_PAD_RIGHT    0x00000020
#define INPUT_PAD_DOWN     0x00000040
#define INPUT_PAD_LEFT     0x00000080
#define INPUT_PAD_L2       0x00000100
#define INPUT_PAD_R2       0x00000200
#define INPUT_PAD_L1       0x00000400
#define INPUT_PAD_R1       0x00000800
#define INPUT_PAD_TRIANGLE 0x00001000
#define INPUT_PAD_CIRCLE   0x00002000
#define INPUT_PAD_CROSS    0x00004000
#define INPUT_PAD_SQUARE   0x00008000
#define INPUT_PAD_L2_SHIFT 21
#define INPUT_PAD_R2_SHIFT 25

typedef struct input_event {
  input_kind_t kind;
  float x, y;    // the cursor, in page pixels
  float dx, dy;  // INPUT_SCROLL: pixels, positive scrolls right/down
  int pressed;   // INPUT_MOVE: cross is held (a drag); INPUT_SCROLL: 1 for
                 // the D-pad (a step of 120), 0 for the right stick
} input_event_t;

// Called on the input thread.
typedef void (*input_handler_t)(const input_event_t* event);

/**
 * Opens the controller of the user who started the app and starts reading
 * it 60 times a second. The cursor starts in the middle of a width x height
 * page. Returns 0, or the libScePad/libSceUserService error.
 **/
int input_start(input_handler_t handler, int width, int height);

/**
 * Where the cursor is now.
 **/
void input_cursor(float* x, float* y);

/**
 * Puts the cursor there (the browser's D-pad moves it onto the element it
 * chose).
 **/
void input_set_cursor(float x, float y);

/**
 * How fast the sticks go, in percent of the default (cursor and scrolling,
 * 25-400).
 **/
void input_set_speed(int cursor_percent, int scroll_percent);

/**
 * 1: the controller goes to a web game as it is (INPUT_PAD), and only the
 * touch pad's click still comes as itself (INPUT_TOUCHPAD: the way out).
 * 0: back to the cursor and the browser's buttons.
 **/
void input_set_raw(int on);

/**
 * The user whose controller it is (for the system keyboard).
 **/
int input_user(void);

/**
 * The controller's libScePad handle, after input_start() returned 0 (the
 * title's feel.c: vibration, triggers, light bar).
 **/
int input_handle(void);

#ifdef __cplusplus
}
#endif
