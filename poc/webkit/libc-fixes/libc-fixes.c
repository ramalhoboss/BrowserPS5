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

// Fixes for libc calls that misbehave on the PS5, linked in with
// -Wl,--wrap=<name> (build-webkit.sh).
//
// readlink() on a file that is not a symbolic link returns 22 (EINVAL's
// number) with errno 0, instead of -1 and errno EINVAL (poc/fc_probe,
// 2026-11-03). fontconfig then takes fonts.conf for a link with a 22-byte
// target, loads no configuration and finds no font, and the WebProcess
// stops in FontCache::lastResortFallbackFont. Only real links go to the
// libc call; anything else fails the way POSIX says.
//
// pthread_create() gives a thread whose creator asks for no stack size
// 64 KiB (poc/gst_probe, 2026-11-04), GLib's and FFmpeg's threads among
// them. libopus overflowed that decoding YouTube's Opus on a GStreamer
// streaming thread, and the process stopped with no trace (V3). A thread
// that would get less than MIN_THREAD_STACK gets that much instead: macOS's
// size for threads other than the main one, which GStreamer and FFmpeg are
// used to. A stack the creator provided itself is left alone.
//
// Every thread made through it also goes into a table with its CPU clock
// while it runs (known_threads.h): the load probe in crash-report.c names
// the threads that used the CPU each second, and the spin guard there looks
// for a compositor thread that never stops. The YouTube page froze while
// the WebProcess kept ~1.9 cores busy, and the process's total did not say
// which threads those were (2026-11-04 17:18). Such a thread starts in
// known_start(), which takes a slot and gives it back when the thread ends.
// Only with both BROWSERPS5_LOAD_PROBE and BROWSERPS5_SPIN_GUARD off
// (env.txt) are threads made as they are.

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/thr.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "known_threads.h"

#define MIN_THREAD_STACK (512 * 1024)

ssize_t __real_readlink(const char* path, char* buf, size_t size);
int __real_pthread_create(pthread_t* thread, const pthread_attr_t* attr,
                          void* (*start)(void*), void* arg);


ssize_t
__wrap_readlink(const char* path, char* buf, size_t size) {
  struct stat st;

  if(lstat(path, &st)) {
    return -1;
  }
  if(!S_ISLNK(st.st_mode)) {
    errno = EINVAL;
    return -1;
  }
  return __real_readlink(path, buf, size);
}


static browserps5_known_thread_t known[BROWSERPS5_MAX_KNOWN_THREADS];
static unsigned known_count;

typedef struct {
  void* (*start)(void*);
  void* arg;
} known_start_t;


const browserps5_known_thread_t*
browserps5_known_threads(unsigned* count) {
  unsigned n = __atomic_load_n(&known_count, __ATOMIC_ACQUIRE);
  *count = n < BROWSERPS5_MAX_KNOWN_THREADS ? n : BROWSERPS5_MAX_KNOWN_THREADS;
  return known;
}


// pthread_getcpuclockid() answers CLOCK_THREAD_CPUTIME_ID, "the calling
// thread", whichever thread it is asked about, as FreeBSD's old libthr did:
// every thread then showed the probe's own time (cpu_probe, 2026-11-04
// 17:32). clock_getcpuclockid2() with the thread's id gives its own clock.
int
browserps5_thread_clock(clockid_t* clock) {
  long tid = 0;

  if(thr_self(&tid)) {
    return -1;
  }
  return clock_getcpuclockid2((id_t)tid, CPUCLOCK_WHICH_TID, clock) ? -1 : 0;
}


static void
known_end(void* p) {
  browserps5_known_thread_t* slot = p;
  __atomic_store_n(&slot->alive, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&slot->used, 0, __ATOMIC_RELEASE);
}


// A slot for a new thread: one never used, else one whose thread ended
// (a session makes ~1 thread a second, 2026-11-05). 0 if all are taken.
static browserps5_known_thread_t*
take_slot(void) {
  unsigned i = __atomic_load_n(&known_count, __ATOMIC_ACQUIRE);
  while(i < BROWSERPS5_MAX_KNOWN_THREADS) {
    if(__atomic_compare_exchange_n(&known_count, &i, i + 1, 0, __ATOMIC_ACQ_REL,
                                   __ATOMIC_ACQUIRE)) {
      __atomic_store_n(&known[i].used, 1, __ATOMIC_RELEASE);
      return &known[i];
    }
  }
  for(unsigned j = 0; j < BROWSERPS5_MAX_KNOWN_THREADS; j++) {
    int none = 0;
    if(__atomic_compare_exchange_n(&known[j].used, &none, 1, 0, __ATOMIC_ACQ_REL,
                                   __ATOMIC_ACQUIRE)) {
      return &known[j];
    }
  }
  return 0;
}


static void*
known_start(void* arg) {
  known_start_t s = *(known_start_t*)arg;
  void* result;

  free(arg);
  // Where the thread's stack is (docs/04-jit.md, J2: the JIT compiler
  // threads crashed with rsp inside the JIT pool's addresses).
  long tid = 0;
  thr_self(&tid);
  // 2026-11-11: not with BROWSERPS5_THREAD_LOG=0 (the browser title sets it
  // when built with TITLE_QUIET_LOG): its stderr is a file, a line is a
  // write() and a write() waits for the disk; the thread that made this one
  // (a work queue's maker: the page's main thread) waits for it meanwhile.
  const char* log = getenv("BROWSERPS5_THREAD_LOG");
  if(!log || log[0] != '0') {
    fprintf(stderr, "[WebKit] thread %ld starts, stack near %p\n", tid,
            (void*)&result);
  }
  browserps5_known_thread_t* slot = take_slot();
  if(!slot) {
    return s.start(s.arg);
  }
  slot->thread = pthread_self();
  if(browserps5_thread_clock(&slot->clock)) {
    __atomic_store_n(&slot->used, 0, __ATOMIC_RELEASE);
    return s.start(s.arg);
  }
  __atomic_add_fetch(&slot->generation, 1, __ATOMIC_ACQ_REL);
  __atomic_store_n(&slot->alive, 1, __ATOMIC_RELEASE);
  pthread_cleanup_push(known_end, slot);
  result = s.start(s.arg);
  pthread_cleanup_pop(1);
  return result;
}


static int
create(pthread_t* thread, const pthread_attr_t* attr, void* (*start)(void*),
       void* arg) {
  const char* probe = getenv("BROWSERPS5_LOAD_PROBE");
  const char* guard = getenv("BROWSERPS5_SPIN_GUARD");

  if((probe && *probe && *probe != '0') || !guard || *guard != '0') {
    known_start_t* s = malloc(sizeof(*s));
    if(s) {
      s->start = start;
      s->arg = arg;
      int err = __real_pthread_create(thread, attr, known_start, s);
      if(err) {
        free(s);
      }
      return err;
    }
  }
  return __real_pthread_create(thread, attr, start, arg);
}


int
__wrap_pthread_create(pthread_t* thread, const pthread_attr_t* attr,
                      void* (*start)(void*), void* arg) {
  pthread_attr_t own;
  void* stack = 0;
  size_t size = 0;
  int err;

  if(!attr) {
    if((err = pthread_attr_init(&own))) {
      return err;
    }
    pthread_attr_setstacksize(&own, MIN_THREAD_STACK);
    err = create(thread, &own, start, arg);
    pthread_attr_destroy(&own);
    return err;
  }
  if(pthread_attr_getstack(attr, &stack, &size) || stack || size >= MIN_THREAD_STACK) {
    return create(thread, attr, start, arg);
  }
  // The creator's attributes, with only the size changed while the thread
  // starts (pthread_attr_t is a handle to them: no copy to make).
  pthread_attr_t* changed = (pthread_attr_t*)attr;
  pthread_attr_setstacksize(changed, MIN_THREAD_STACK);
  err = create(thread, attr, start, arg);
  pthread_attr_setstacksize(changed, size);
  return err;
}
