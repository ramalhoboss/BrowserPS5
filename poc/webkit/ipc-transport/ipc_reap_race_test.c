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

// The PC test of the reaper's race (2026-11-17): shared memory with a name of
// ours must keep its name while a descriptor of this process is on it. A
// round of the reaper takes the time, then looks at the descriptors; an
// object made in between was newer than that time, its age came out as a
// huge unsigned number, and its name went at once (on the console 10 runs of
// 399 had "shared memory /bps5wk-...: No such file or directory" in the
// receiver: a message of the NetworkProcess that came without its memory).
// Here: the reaper every millisecond, objects made one after the other and
// held a moment; the name (st_nlink) must stay while they are held.
//   cc -O2 -pthread ipc_reap_race_test.c ipc_transport.c -Wl,--wrap=shm_open
//      -Wl,--wrap=shm_unlink -Wl,--wrap=dup -Wl,--wrap=socketpair
//      -Wl,--wrap=sendmsg -Wl,--wrap=recvmsg -o reap_race && ./reap_race

#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "ipc_transport.h"

#ifndef SHM_ANON
#define SHM_ANON ((char*)1)
#endif

int __wrap_shm_open(const char* name, int oflag, mode_t mode);

static int
named(int fd) {
  struct stat st;
  return !fstat(fd, &st) && st.st_nlink > 0;
}

int
main(void) {
  ipc_transport_set_reap_ms(1, 2);
  const int rounds = 20000;
  int gone_at_once = 0, gone_held = 0, made = 0;
  const struct timespec moment = {0, 300 * 1000};
  for(int i = 0; i < rounds; i++) {
    int fd = __wrap_shm_open(SHM_ANON, O_RDWR | O_CLOEXEC, 0600);
    if(fd < 0) {
      continue;
    }
    made++;
    gone_at_once += !named(fd);
    if(i % 8 == 0) {
      nanosleep(&moment, 0);
      gone_held += !named(fd);
    }
    close(fd);
  }
  ipc_transport_stats_t s;
  ipc_transport_stats(&s);
  printf("%d objects made, named %llu, unlinked %llu; names gone while held: %d at once, %d a moment after\n",
         made, (unsigned long long)s.named, (unsigned long long)s.unlinked, gone_at_once, gone_held);
  if(gone_at_once || gone_held || made < rounds / 2) {
    printf("FAIL: a name went while a descriptor was on it\n");
    return 1;
  }
  printf("PASS\n");
  return 0;
}
