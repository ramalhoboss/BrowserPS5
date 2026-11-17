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

// The WebProcess inside the browser title (web_host.cpp; 2c-2), in a title
// built with WEBKIT=1 build.sh.

#pragma once

#include <stdint.h>

// Listens for the UIProcess's request (web_host_proto.h). 0, or -1 with
// the reason in the report.
int web_host_start(void);

// A crash on a thread of the WebProcess must not end the title the hard
// way: a title that dies with the GPU and its clients open takes the
// console down (2026-11-05 21:01, the first time the WebProcess ran here).
// From title.cpp's crash handler: 0 when the thread is not the WebProcess's
// (the title dies of it as before); otherwise its stack goes into the
// WebProcess's log, the title is told (title_web_failed()), and the thread
// stays parked: it never returns.
int web_host_crashed(int number, const void *context);  // context: the handler's ucontext_t

// From the title's close, before its GPU client and screen go: every other
// thread of the WebProcess stops where it is (parked, no more GPU work).
void web_host_freeze(void);

// The WebProcess's log (web-stderr.txt) goes to the system's log too, as it
// grows; this sends what is left now (the title's close, its crash).
void web_host_log_flush(void);

#ifdef __cplusplus
extern "C" {
#endif
// title.cpp: the WebProcess has failed (crash, WebKit's own crash, exit with
// an error); the title closes as when the link closes. Safe in a signal
// handler.
void title_web_failed(const char *why);
#ifdef __cplusplus
}
#endif
