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

// WebKit's IPC over TCP and named shared memory (ipc_transport.c).

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ipc_transport_stats {
  uint64_t pairs;             // socketpair()s made into meetings
  uint64_t met;               // ends connected (both sides count)
  uint64_t frames_sent;
  uint64_t frames_queued;     // sent before the other end came
  uint64_t frames_received;
  uint64_t attachments_sent;
  uint64_t attachments_lost;  // received, but could not be opened
  uint64_t refused;           // attachments that cannot go by name
  uint64_t file_copies;       // files to read sent as copies (2026-11-10)
  uint64_t named;             // shared memory objects given our names
  uint64_t unlinked;          // and their names taken away
} ipc_transport_stats_t;

void ipc_transport_stats(ipc_transport_stats_t* out);

// For the PC test: how long a name of ours stays after its last use.
void ipc_transport_set_reap_ms(unsigned every_ms, unsigned after_ms);

// An end of a connection for a process that cannot get descriptors (the
// title runs the WebProcess, 2c-2; docs/05-webprocess-no-titulo.md): the
// client end socketpair() gave here is only a meeting line, "BPS5RV1
// <port> <secret>\n" (a 127.0.0.1 port and the secret to give it).
// ipc_transport_endpoint_line() writes fd's line into line (64 bytes are
// enough); the caller then closes fd. ipc_transport_endpoint_from_line(),
// in the other process, makes a descriptor from it that WebKit takes as
// the connection's (it meets on first use). Both -1 with errno on error
// (EINVAL: not such an end or line, or the transport is off).
int ipc_transport_endpoint_line(int fd, char* line, size_t size);
int ipc_transport_endpoint_from_line(const char* line);

// For a process with no dup()/dup2() (a title: EPERM), before WebKit runs:
// from then on an end that comes as a line or as an attachment is connected
// at once (no placeholder), and dup() (wrapped, -Wl,--wrap=dup) of shared
// memory with a name of ours opens the name again. Listening ends cannot be
// met in this mode.
void ipc_transport_title_mode(void);

// Where the "[ipc-tcp]" lines go (descriptor 2 by default): a title keeps
// no descriptor 2.
void ipc_transport_log_fd(int fd);

// What the transport holds for fd, and its ports now, as a line (tests).
int ipc_transport_describe(int fd, char* out, size_t size);

// A title refuses fcntl(F_SETFL, O_NONBLOCK) on a socket (2026-11-05 20:47:
// WebKit's Connection::open() stopped at setNonBlock() and never started
// the thread that reads its end). For the transport's ends in title mode,
// the mode is kept here instead (the title's --wrap=fcntl, web_host.cpp):
// recvmsg() then reads with MSG_DONTWAIT, and sendmsg() writes whole frames
// either way. ipc_transport_keeps(): 1 for such an end. Lock-free: may be
// called from inside the transport.
int ipc_transport_keeps(int fd);
void ipc_transport_keep_nonblock(int fd, int on);
int ipc_transport_kept_nonblock(int fd);

// ---------------------------------------------------------------------------
// The names a process still has, for whoever outlives it. Each process
// keeps the names it made and has not unlinked yet in a table of its own,
// in shared memory named IPC_TRANSPORT_LIST_PREFIX + pid. A process that
// ends without unlinking them (killed: the pause, the quit, the launcher's
// leftovers; or crashed) would leave their memory taken until the console
// restarts (2c-1, 2026-11-05 16:24: the UIProcess ended with 8 made and 7
// unlinked). The launcher, which reaps all three WebKit processes, calls
// ipc_transport_clean_pid() for each one that ends (launcher.c); a process
// does it too at its start for an earlier one with its pid.

#define IPC_TRANSPORT_LIST_PREFIX "/bps5wkt-"
#define IPC_TRANSPORT_NAME_PREFIX "/bps5wk-"  // + pid + "-" + n
#define IPC_TRANSPORT_LIST_MAGIC  0x4c4b5742u  // "BWKL"
#define IPC_TRANSPORT_LIST_SLOTS  4096         // ipc_transport.c's SHM_TABLE_SIZE
#define IPC_TRANSPORT_NAME_BYTES  40           // and NAME_BYTES

typedef struct ipc_transport_list {
  uint32_t magic;  // IPC_TRANSPORT_LIST_MAGIC once filled in
  int32_t pid;
  uint32_t slots;  // IPC_TRANSPORT_LIST_SLOTS
  uint32_t reserved;
  char names[IPC_TRANSPORT_LIST_SLOTS][IPC_TRANSPORT_NAME_BYTES];  // "" when free
} ipc_transport_list_t;

#ifdef IPC_TRANSPORT_CLEANER
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

// Unlinks the names pid left (only "/bps5wk-<pid>-..." ones), then its
// table. Returns how many names were still there (unlinked now), or -1
// when pid left no table. Uses the real shm_open/shm_unlink: built where
// they are not wrapped (the launcher), or through __real_ in
// ipc_transport.c (IPC_TRANSPORT_REAL).
#ifndef IPC_TRANSPORT_REAL
#define IPC_TRANSPORT_REAL(f) f
#endif
static inline int
ipc_transport_clean_pid(int pid) {
  char list_name[32], prefix[32];
  snprintf(list_name, sizeof(list_name), IPC_TRANSPORT_LIST_PREFIX "%d", pid);
  snprintf(prefix, sizeof(prefix), IPC_TRANSPORT_NAME_PREFIX "%d-", pid);
  int fd = IPC_TRANSPORT_REAL(shm_open)(list_name, O_RDONLY, 0);
  if(fd < 0) {
    return -1;
  }
  int count = 0;
  void* memory = mmap(0, sizeof(ipc_transport_list_t), PROT_READ, MAP_SHARED, fd, 0);
  close(fd);
  if(memory != MAP_FAILED) {
    const ipc_transport_list_t* list = (const ipc_transport_list_t*)memory;
    if(list->magic == IPC_TRANSPORT_LIST_MAGIC && list->pid == pid) {
      const size_t prefix_bytes = strlen(prefix);
      for(unsigned i = 0; i < IPC_TRANSPORT_LIST_SLOTS; i++) {
        char name[IPC_TRANSPORT_NAME_BYTES];
        memcpy(name, list->names[i], sizeof(name));
        name[sizeof(name) - 1] = 0;
        if(!strncmp(name, prefix, prefix_bytes) && !IPC_TRANSPORT_REAL(shm_unlink)(name)) {
          count++;
        }
      }
    }
    munmap(memory, sizeof(ipc_transport_list_t));
  }
  IPC_TRANSPORT_REAL(shm_unlink)(list_name);
  return count;
}
#endif

#ifdef __cplusplus
}
#endif
