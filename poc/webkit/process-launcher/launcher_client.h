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

// App side of launcher_proto.h, in C so that both process-launcher.cpp
// (WebKit's UIProcess) and the PoCs in poc/launcher use it.

#pragma once

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include "launcher_proto.h"


/**
 * Send one request (with fd attached when fd >= 0) and wait for the reply.
 * Returns the reply's result, or a negative errno.
 **/
static inline int32_t
launcher_request(const launcher_req_t* req, int fd) {
  // One request at a time: replies carry no id.
  static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
  char control[CMSG_SPACE(sizeof(int))];
  launcher_rep_t rep;
  struct msghdr msg;
  struct iovec iov;
  ssize_t n;

  memset(&msg, 0, sizeof(msg));
  iov.iov_base = (void*)req;
  iov.iov_len = sizeof(*req);
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;

  if(fd >= 0) {
    struct cmsghdr* cmsg;

    memset(control, 0, sizeof(control));
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);
    cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(cmsg), &fd, sizeof(int));
  }

  pthread_mutex_lock(&lock);
  if(sendmsg(BROWSERPS5_LAUNCHER_FD, &msg, 0) < 0) {
    n = -errno;
  } else if((n=recv(BROWSERPS5_LAUNCHER_FD, &rep, sizeof(rep), 0)) < 0) {
    n = -errno;
  } else if(n != sizeof(rep)) {
    n = -EPROTO;
  } else {
    n = rep.result;
  }
  pthread_mutex_unlock(&lock);

  return (int32_t)n;
}


/**
 * Start the ELF at path with argv {path, arg}, and ipc_fd at
 * BROWSERPS5_CHILD_IPC_FD in the new process. Returns its pid, or a
 * negative errno.
 **/
static inline int32_t
launcher_spawn(const char* path, const char* arg, int ipc_fd, int32_t user_id) {
  launcher_req_t req;

  memset(&req, 0, sizeof(req));
  req.type = LAUNCHER_SPAWN;
  req.user_id = user_id;
  if(snprintf(req.path, sizeof(req.path), "%s", path) >= (int)sizeof(req.path)
     || snprintf(req.arg, sizeof(req.arg), "%s", arg) >= (int)sizeof(req.arg)) {
    return -ENAMETOOLONG;
  }

  return launcher_request(&req, ipc_fd);
}


/**
 * SIGKILL a process the launcher started. Returns 0 or a negative errno.
 **/
static inline int32_t
launcher_kill(int32_t pid) {
  launcher_req_t req;

  memset(&req, 0, sizeof(req));
  req.type = LAUNCHER_KILL;
  req.pid = pid;
  return launcher_request(&req, -1);
}
