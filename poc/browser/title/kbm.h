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

// kbm (2026-11-18, BrowserPS5 RN's title, TITLE_KBM=1): a USB or Bluetooth mouse and keyboard plugged into the
// console. The system's libraries for them (libSceMouse, libSceKeyboard) are imported and loaded by the system
// at run time, as the video decoder's is (kbm.c): on a console or firmware where that fails, nothing changes and
// the log says why.
//
//   - The mouse moves the browser's cursor (INPUT_MOVE, as the left stick), its left button is ✕ (INPUT_BUTTON_DOWN
//     and _UP; held, a drag), its wheel scrolls in steps (INPUT_SCROLL, as the D-pad), its side buttons are back
//     and forward. Nothing new for the browser's interface to know.
//   - The keyboard: each key down, held (repeated) and up as INPUT_KEY (input.h), with its USB HID usage and the
//     modifiers; the browser makes the characters. The log says when it comes and goes, never the keys.
#pragma once

#include "input.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Loads the libraries, opens the user's mouse and keyboard and reads them in a thread of its own (every 8 ms,
 * sleeping in between). The events go to handler, on that thread, with the cursor kept by input.c
 * (input_cursor, input_set_cursor) on a width x height page. Returns 0 when at least one of the two is open.
 **/
int kbm_start(input_handler_t handler, int width, int height);

#ifdef __cplusplus
}
#endif
