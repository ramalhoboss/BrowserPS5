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

// pipe_probe (2026-11-17): the console's pipes, measured without the browser.
//
// The start page stopped loading (every start, from 18:14 that day): the NetworkProcess's curl thread slept in
// curl_multi_poll() with requests queued behind it. curl 8.18 wakes that poll through a pipe and its resolver's
// thread says "done" through another; on the console every write to a pipe failed with ENOMEM (the pool the
// system keeps for pipes' buffers, shared by all its processes, had run out), while a socketpair() worked. The
// engine's pipes are pairs of sockets since (webkit/libc-fixes/libc-fixes.c).
//
// pipe_probe.elf:
//   1. pipe2(O_NONBLOCK | O_CLOEXEC), pipe() and socketpair(AF_UNIX): made? does a byte written to one end wake a
//      thread in poll() on the other, and after how long?
//   2. the processes there are, counted by name (kern.proc.proc): one of the engine's left behind by a browser
//      that is gone shows here ("SceNKNetworkProcess" with no browser open).
//   3. getaddrinfo() of a few names: the answer and how long it took.
// pipe_room.elf (-DPIPE_ROOM): how many pipes take a byte now, up to PIPE_ROOM of them, all closed again at once.
//   It stops at the first that does not: that many were left in the pool. Run it before and after something to
//   see what that something keeps (a browser's run, a hundred payloads).
//
// Neither writes anywhere; every wait is a sleep or a poll with an end. (The console does not give
// kern.ipc.pipekva and its like, nor the open files of other processes: both were tried.)

#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/user.h>

#ifdef PIPE_ROOM
#define POC_NAME "pipe_room"
#else
#define POC_NAME "pipe_probe"
#endif
#include "poc_log.h"


#ifdef PIPE_ROOM

int
main(void) {
  static int fds[PIPE_ROOM][2];
  int made = 0, wrote = 0, error = 0;
  const char* stopped = "the limit of this test";
  char byte = 1;

  for(; made < PIPE_ROOM; made++) {
    if(pipe(fds[made])) {
      error = errno;
      stopped = "pipe() failed";
      break;
    }
    if(write(fds[made][1], &byte, 1) != 1) {
      error = errno;
      stopped = "a write failed";
      close(fds[made][0]);
      close(fds[made][1]);
      break;
    }
    wrote++;
  }
  for(int i = 0; i < wrote; i++) {
    close(fds[i][0]);
    close(fds[i][1]);
  }
  POC_RESULT(wrote > 0, "%d pipe(s) took a byte, of %d tried at most; stopped by: %s%s%s", wrote, PIPE_ROOM, stopped,
             error ? ": " : "", error ? strerror(error) : "");
  return 0;
}

#else

static double
now_ms(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}


typedef struct waiter {
  int fd;
  int result;       // poll()'s
  int revents;
  double woke_ms;
  volatile int in_poll;
} waiter_t;

static void*
waiter_thread(void* arg) {
  waiter_t* w = (waiter_t*)arg;
  struct pollfd p = {w->fd, POLLIN, 0};
  w->in_poll = 1;
  w->result = poll(&p, 1, 2500);
  w->revents = p.revents;
  w->woke_ms = now_ms();
  return 0;
}


// kind: 0 pipe2(flags), 1 pipe(), 2 socketpair(AF_UNIX).
static void
pair_test(const char* what, int kind, int flags) {
  int fds[2] = {-1, -1};
  waiter_t w;
  pthread_t thread;
  char byte = 1;

  if(kind == 0 ? pipe2(fds, flags) : kind == 1 ? pipe(fds) : socketpair(AF_UNIX, SOCK_STREAM, 0, fds)) {
    POC_RESULT(0, "%s: not made: %s", what, strerror(errno));
    return;
  }
  memset(&w, 0, sizeof(w));
  w.fd = fds[0];
  if(pthread_create(&thread, 0, waiter_thread, &w)) {
    POC_PERROR("pthread_create");
  } else {
    for(int i = 0; i < 200 && !w.in_poll; i++) {
      usleep(5000);
    }
    usleep(300000);  // (it is inside poll() by now)
    const double wrote_ms = now_ms();
    const ssize_t n = write(fds[1], &byte, 1);
    const int write_errno = errno;
    pthread_join(thread, 0);
    POC_RESULT(n == 1 && w.result == 1 && w.woke_ms - wrote_ms < 100,
               "%s: made; write: %zd (%s); the thread in poll() on the other end woke %.1f ms after it (poll %d, revents 0x%x)",
               what, n, n < 0 ? strerror(write_errno) : "no error", w.woke_ms - wrote_ms, w.result, w.revents);
  }
  close(fds[0]);
  close(fds[1]);
}


static void
processes(void) {
  int mib[3] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC};
  size_t size = 0;
  struct { char name[20]; int count, threads, first, last; } seen[96];
  int kinds = 0, total = 0;

  if(sysctl(mib, 3, 0, &size, 0, 0)) {
    POC_PERROR("kern.proc.proc (size)");
    return;
  }
  size += size / 4;
  struct kinfo_proc* list = (struct kinfo_proc*)malloc(size);
  if(!list || sysctl(mib, 3, list, &size, 0, 0)) {
    POC_PERROR("kern.proc.proc");
    free(list);
    return;
  }
  memset(seen, 0, sizeof(seen));
  for(char* at = (char*)list; at + sizeof(int) <= (char*)list + size;) {
    struct kinfo_proc* k = (struct kinfo_proc*)at;
    if(k->ki_structsize <= 0) {
      break;
    }
    int i = 0;
    for(; i < kinds && strncmp(seen[i].name, k->ki_comm, sizeof(seen[i].name) - 1); i++) {
    }
    if(i == kinds && kinds < 96) {
      snprintf(seen[kinds].name, sizeof(seen[kinds].name), "%s", k->ki_comm);
      seen[kinds].first = k->ki_pid;
      kinds++;
    }
    if(i < 96) {
      seen[i].count++;
      seen[i].threads += k->ki_numthreads;
      seen[i].last = k->ki_pid;
    }
    total++;
    at += k->ki_structsize;
  }
  free(list);
  POC_LOG("processes: %d, %d different names", total, kinds);
  for(int i = 0; i < kinds; i++) {  // the engine's, the payloads, and any name there is more than one of
    if(seen[i].count > 1 || strstr(seen[i].name, ".elf") || strstr(seen[i].name, "rowser") || strstr(seen[i].name, "Process")) {
      POC_LOG("  %s: %d (pids %d..%d), %d thread(s)", seen[i].name, seen[i].count, seen[i].first, seen[i].last, seen[i].threads);
    }
  }
}


static void
resolve_test(const char* name) {
  struct addrinfo hints, *res = 0;
  char text[64] = "";

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  const double began = now_ms();
  const int e = getaddrinfo(name, 0, &hints, &res);
  const double took = now_ms() - began;
  if(!e && res && res->ai_family == AF_INET) {
    inet_ntop(AF_INET, &((struct sockaddr_in*)res->ai_addr)->sin_addr, text, sizeof(text));
  } else if(!e && res && res->ai_family == AF_INET6) {
    inet_ntop(AF_INET6, &((struct sockaddr_in6*)res->ai_addr)->sin6_addr, text, sizeof(text));
  }
  POC_RESULT(!e, "getaddrinfo(%s): %s in %.0f ms%s%s", name, e ? gai_strerror(e) : "resolved", took, text[0] ? ": " : "", text);
  if(res) {
    freeaddrinfo(res);
  }
}


int
main(void) {
  static const char* const names[] = {"www.google.com", "www.youtube.com", "example.com"};

  pair_test("pipe2(O_NONBLOCK | O_CLOEXEC)", 0, O_NONBLOCK | O_CLOEXEC);
  pair_test("pipe()", 1, 0);
  pair_test("socketpair(AF_UNIX)", 2, 0);
  processes();
  for(unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
    resolve_test(names[i]);
  }
  POC_LOG("done");
  return 0;
}

#endif
