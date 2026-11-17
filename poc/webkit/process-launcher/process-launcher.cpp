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

// Sony's process-launcher API on top of launcher_client.h (in the
// UIProcess), and the child side of process-initialization (in
// WebProcess/NetworkProcess).
//
// 2c-2 (docs/05-webprocess-no-titulo.md): a WebProcess goes to the browser
// title first, if the title hosts one (web_host_proto.h) and WebKit's IPC
// is over TCP (ipc_transport.c, IPC_TCP=1: only then can its end be given
// as a line). Otherwise, and for every other process, the launcher payload
// starts it as before. BROWSERPS5_WEB_IN_TITLE=0 in the environment keeps
// every WebProcess a payload.

#include "process-launcher.h"
#include "process-initialization/nk-webprocess.h"
#include "../launcher_client.h"
#include "web_host_proto.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

// ipc_transport.h's, there only with IPC_TCP=1.
extern "C" int ipc_transport_endpoint_line(int fd, char* line, size_t size) __attribute__((weak));

namespace PlayStation {

namespace {

int32_t title_web_pid;  // the title's pid while it runs our WebProcess
int connection_identifier = BROWSERPS5_CHILD_IPC_FD;

bool
ends_with(const char* text, const char* end) {
  size_t a = strlen(text), b = strlen(end);
  return a >= b && !strcmp(text + a - b, end);
}

// A connection to the title's host, or -1 at once when nothing listens.
int
host_connect() {
  int s = socket(AF_INET, SOCK_STREAM, 0);
  if(s < 0) {
    return -1;
  }
  struct sockaddr_in a = {};
  a.sin_family = AF_INET;
  a.sin_port = htons(WEB_HOST_PORT);
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  int flags = fcntl(s, F_GETFL);
  fcntl(s, F_SETFL, flags | O_NONBLOCK);
  int err = connect(s, (struct sockaddr*)&a, sizeof(a));
  if(err && errno == EINPROGRESS) {
    struct pollfd p = {s, POLLOUT, 0};
    int error = 0;
    socklen_t size = sizeof(error);
    err = poll(&p, 1, 500) == 1 && !getsockopt(s, SOL_SOCKET, SO_ERROR, &error, &size) && !error
          ? 0 : -1;
  }
  if(err) {
    close(s);
    return -1;
  }
  fcntl(s, F_SETFL, flags);
  return s;
}

// The title's answer, one line, within timeout_ms.
bool
host_answer(int s, char* line, size_t size, int timeout_ms) {
  size_t got = 0;
  while(got + 1 < size) {
    struct pollfd p = {s, POLLIN, 0};
    if(poll(&p, 1, timeout_ms) != 1) {
      return false;
    }
    ssize_t n = read(s, line + got, 1);
    if(n <= 0) {
      return false;
    }
    if(line[got++] == '\n') {
      break;
    }
  }
  line[got] = 0;
  return got > 0 && line[got - 1] == '\n';
}

// The WebProcess in the title: its pid, or -1 to start it as before.
int32_t
launch_in_title(const char* identifier, int ipc_socket) {
  const char* off = getenv("BROWSERPS5_WEB_IN_TITLE");
  if((off && off[0] == '0') || !ipc_transport_endpoint_line || title_web_pid) {
    return -1;
  }
  int s = host_connect();
  if(s < 0) {
    return -1;  // no title hosts it (a payload-only run)
  }
  char meeting[64];
  char line[WEB_HOST_LINE_MAX];
  int32_t pid = -1;
  if(ipc_transport_endpoint_line(ipc_socket, meeting, sizeof(meeting))) {
    fprintf(stderr, "[process-launcher] the WebProcess's end has no line: %s\n", strerror(errno));
  } else {
    int n = snprintf(line, sizeof(line), WEB_HOST_MAGIC "%s %s", identifier, meeting);
    if(n > 0 && (size_t)n < sizeof(line) && write(s, line, (size_t)n) == n
       && host_answer(s, line, sizeof(line), 10000)) {
      long value = 0;
      if(sscanf(line, "OK %ld", &value) == 1 && value > 0) {
        pid = (int32_t)value;
        title_web_pid = pid;
        fprintf(stderr, "[process-launcher] the WebProcess runs in the title (pid %d)\n", pid);
      } else {
        line[strcspn(line, "\n")] = 0;
        fprintf(stderr, "[process-launcher] the title does not run the WebProcess: %s\n", line);
      }
    } else {
      fprintf(stderr, "[process-launcher] no answer from the title's host\n");
    }
  }
  close(s);
  return pid;
}

} // namespace

int32_t
launchProcess(const char* path, char* const argv[], const LaunchParam& param) {
  if(!argv || !argv[0]) {
    return -EINVAL;
  }

  if(ends_with(path, "/WebProcess.elf")) {
    int32_t pid = launch_in_title(argv[0], param.ipcSocket);
    if(pid > 0) {
      return pid;
    }
  }
  int32_t pid = launcher_spawn(path, argv[0], param.ipcSocket, param.userId);
  if(pid < 0) {
    fprintf(stderr, "[process-launcher] %s: %s\n", path, strerror(-pid));
  }
  return pid;
}


void
terminateProcess(int32_t pid) {
  if(pid && pid == title_web_pid) {
    // A thread of the title: nothing to kill. It ends with its connection
    // (WebKit closes it), and the title then ends too.
    fprintf(stderr, "[process-launcher] the WebProcess in the title (pid %d) is not killed\n", pid);
    return;
  }
  launcher_kill(pid);
}


int32_t
titleWebProcessId() {
  return title_web_pid;
}


int
getConnectionIdentifier() {
  return connection_identifier;
}


void
setConnectionIdentifier(int fd) {
  connection_identifier = fd;
}

} // namespace PlayStation
