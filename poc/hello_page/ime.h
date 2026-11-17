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

// The kind of text field, for the keyboard's layout.
typedef enum ime_kind {
  IME_TEXT,
  IME_URL,
  IME_MAIL,
  IME_NUMBER,
} ime_kind_t;

// What the keyboard's enter key says (libSceImeDialog's values).
typedef enum ime_enter {
  IME_ENTER_DEFAULT = 0,
  IME_ENTER_SEARCH = 2,
  IME_ENTER_GO = 3,
} ime_enter_t;

// Called on the keyboard's thread when it closes: the text as UTF-8, or 0 if
// the user cancelled.
typedef void (*ime_done_t)(const char* text, void* ctx);

/**
 * Opens the system keyboard (libSceImeDialog) over the app for the user
 * with that id, starting with text (UTF-8, may be 0). Returns 0, 1 if it is
 * open already, or the libSceImeDialog error.
 **/
int ime_open(int user_id, ime_kind_t kind, ime_enter_t enter,
             const char* text, ime_done_t done, void* ctx);

/**
 * Whether the keyboard is open (the controller is the system's meanwhile).
 **/
int ime_is_open(void);

/**
 * BrowserPS5 RN's title only (TITLE_SYSIME, browser/title/sysime.c): loads the
 * keyboard's library into the title and binds it, once; 0 when it is there.
 * ime_open() opens nothing before this said 0. ime_abort() closes an open
 * keyboard (sceImeDialogAbort), -1 when it cannot.
 **/
int ime_load(void);
int ime_abort(void);

#ifdef __cplusplus
}
#endif
