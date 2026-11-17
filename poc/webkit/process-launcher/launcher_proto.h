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

// Protocol between WebKit's UIProcess (inside the app) and the launcher
// payload, which starts WebProcess and NetworkProcess for it. The app
// cannot start processes itself: see docs/02-desenho-sem-gpu.md (M1a).
//
// The control socket is SOCK_SEQPACKET, so every request and reply is one
// message. A SPAWN request carries WebKit's end of the IPC socketpair as
// SCM_RIGHTS; the launcher puts it at BROWSERPS5_CHILD_IPC_FD in the child.

#pragma once

#include <stdint.h>

// Where the app finds its end of the control socket.
#define BROWSERPS5_LAUNCHER_FD 101

// Where WebProcess/NetworkProcess find their WebKit IPC socket.
#define BROWSERPS5_CHILD_IPC_FD 100

#define LAUNCHER_PATH_MAX 256
#define LAUNCHER_ARG_MAX  64

enum launcher_msg_type {
  LAUNCHER_SPAWN = 1,  // start path with argv {path, arg}; fd attached
  LAUNCHER_KILL  = 2,  // SIGKILL pid
};

typedef struct launcher_req {
  uint32_t type;
  int32_t  pid;                      // LAUNCHER_KILL
  int32_t  user_id;                  // LAUNCHER_SPAWN, from WebKit
  char     path[LAUNCHER_PATH_MAX];  // LAUNCHER_SPAWN, ELF on the PS5
  char     arg[LAUNCHER_ARG_MAX];    // LAUNCHER_SPAWN, WebKit's argv[0]
} launcher_req_t;

typedef struct launcher_rep {
  int32_t result;  // pid of the new process, or a negative errno
} launcher_rep_t;
