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

// The threads libc-fixes.c's pthread_create() made, for the load probe and
// the spin guard in crash-report.c (unless env.txt has both off).

#pragma once

#include <pthread.h>
#include <time.h>

#define BROWSERPS5_MAX_KNOWN_THREADS 1024

typedef struct {
  pthread_t thread;
  clockid_t clock;      // its CPU time
  int alive;            // 1 from its start until it ends
  int used;             // libc-fixes.c's: the slot is taken
  unsigned generation;  // +1 each time a thread takes the slot
} browserps5_known_thread_t;

#ifdef __cplusplus
extern "C" {
#endif

// The table, and in *count how many slots have been taken so far. A slot
// whose thread ended goes to a later thread, with a new generation (a
// reader that keeps a slot's last CPU time starts again from 0 when the
// generation changes). Read thread and clock only while alive is 1.
const browserps5_known_thread_t* browserps5_known_threads(unsigned* count);

// The calling thread's CPU clock, one other threads can read too (not
// pthread_getcpuclockid(): see libc-fixes.c). 0, or -1 on failure.
int browserps5_thread_clock(clockid_t* clock);

#ifdef __cplusplus
}
#endif
