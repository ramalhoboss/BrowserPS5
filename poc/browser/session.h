/* Copyright (C) 2026 BrowserPS5 contributors. GPL-3.0-or-later.

   The tabs of the last time (docs/20-separadores-da-ultima-vez.md): the web
   addresses of the open tabs and which one was in front, in a small text
   file the app writes when they change, through a new file renamed over it
   (a crash in between leaves the old one). With "Ao ligar: a última página"
   they all come back (only the one in front loads; the others when shown);
   otherwise the start page offers them once. Plain C with no state; tested
   on the PC by session_test.c (make test). */

#ifndef SESSION_H
#define SESSION_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SESSION_URL_MAX 4096   // an address, with its 0; longer ones are left out
#define SESSION_TABS_MAX 16    // read at most

// The file's text for urls (only http: and https: ones without line ends or
// control characters, at most SESSION_TABS_MAX), current the one in front:
// "current N\n" and one address a line. Its length (0: it did not fit).
size_t session_text(const char* const* urls, int count, int current, char* out, size_t size);

// Writes text to path through path.new; 0, or -1.
int session_write(const char* path, const char* text);

// The tabs in the file: up to max addresses into urls, the one in front
// into *current (0 when the file does not say or says one not there). How
// many; 0 when there is no file or nothing good in it. Lines that are not
// web addresses are left out.
int session_read(const char* path, char (*urls)[SESSION_URL_MAX], int max, int* current);

#ifdef __cplusplus
}
#endif

#endif
