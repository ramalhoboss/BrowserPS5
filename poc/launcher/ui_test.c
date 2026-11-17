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

// App side of the M1b test: runs inside the app, where WebKit's UIProcess
// will, and does what ProcessLauncherPlayStation.cpp does through
// process-launcher: create a socketpair, ask the launcher payload to start
// a child with one end, then talk to the child over the other end. The
// child is spawn_ipc's child.elf, installed on the PS5 beforehand.

#define POC_NAME "launcher/ui_test"

#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>

#include <ps5/kernel.h>

#include "poc_log.h"
#include "../spawn_ipc/ipc_proto.h"
#include "launcher_client.h"

#define CHILD_PATH "/user/homebrew/browserps5/poc/child.elf"


static int
test_ping(int sock) {
  double t0 = poc_now_ms();
  ipc_hdr_t hdr;
  double dt;

  for(uint32_t seq=0; seq<IPC_PING_ROUNDS; seq++) {
    if(ipc_send(sock, MSG_PING, seq, 0, 0, -1) < 0
       || ipc_recv(sock, &hdr, 0, 0, 0) < 0
       || hdr.type != MSG_PONG || hdr.seq != seq) {
      POC_PERROR("ping");
      POC_RESULT(0, "ping/pong failed at round %u", seq);
      return -1;
    }
  }

  dt = poc_now_ms() - t0;
  POC_RESULT(1, "%d ping/pong rounds in %.1f ms (%.1f us/round)",
             IPC_PING_ROUNDS, dt, dt * 1000.0 / IPC_PING_ROUNDS);
  return 0;
}


static int
test_shm(int sock) {
  uint32_t* mem;
  ipc_hdr_t hdr;
  int ok = 0;
  int fd;

  if((fd=shm_open(SHM_ANON, O_RDWR, 0600)) < 0 || ftruncate(fd, IPC_SHM_SIZE)) {
    POC_PERROR("shm_open(SHM_ANON)");
    POC_RESULT(0, "shared memory inside the app");
    return -1;
  }
  mem = mmap(0, IPC_SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if(mem == MAP_FAILED) {
    POC_PERROR("mmap(shm)");
    close(fd);
    return -1;
  }
  for(uint32_t i=0; i<IPC_SHM_SIZE/sizeof(uint32_t); i++) {
    mem[i] = i;
  }

  if(ipc_send(sock, MSG_SHM, 0, 0, 0, fd) < 0) {
    POC_PERROR("ipc_send(SHM)");
  } else if(ipc_recv(sock, &hdr, &ok, sizeof(ok), 0) < 0
            || hdr.type != MSG_SHM_ACK) {
    POC_PERROR("ipc_recv(SHM_ACK)");
    ok = 0;
  }
  close(fd);

  // The child writes these after checking our data.
  ok = ok && mem[0] == IPC_SHM_MAGIC && mem[1] == 0;
  POC_RESULT(ok, "shared memory from the app passed with SCM_RIGHTS, the "
             "child read our data and we read theirs");
  munmap(mem, IPC_SHM_SIZE);
  return ok ? 0 : -1;
}


/**
 * The app is not the child's parent, so no waitpid(): the child has gone
 * when its end of the socket closes.
 **/
static int
test_quit(int sock, pid_t pid) {
  struct pollfd pfd = {sock, POLLIN, 0};
  char byte;
  int32_t err;

  if(ipc_send(sock, MSG_QUIT, 0, 0, 0, -1) < 0) {
    POC_PERROR("ipc_send(QUIT)");
    return -1;
  }
  if(poll(&pfd, 1, 5000) != 1 || recv(sock, &byte, 1, 0) != 0) {
    POC_RESULT(0, "child %d did not close its socket within 5 s", pid);
    launcher_kill(pid);
    return -1;
  }
  POC_RESULT(1, "child %d exited and closed its socket", pid);

  // Already gone, so the launcher should say so.
  err = launcher_kill(pid);
  POC_LOG("launcher_kill(%d) after exit = %d (%s)", pid, err,
          err ? strerror(-err) : "ok");
  return 0;
}


int
main(void) {
  int size = WEBKIT_SOCKET_BUFFER;
  int failures = 0;
  ipc_hello_t hello;
  ipc_hdr_t hdr;
  int sv[2];
  pid_t pid;

  POC_LOG("app: pid=%d uid=%d authid=0x%016lx", getpid(), getuid(),
          kernel_get_ucred_authid(getpid()));

  if(fcntl(BROWSERPS5_LAUNCHER_FD, F_GETFD) < 0) {
    POC_PERROR("fcntl(BROWSERPS5_LAUNCHER_FD)");
    POC_RESULT(0, "control socket at fd %d", BROWSERPS5_LAUNCHER_FD);
    return 1;
  }
  POC_RESULT(1, "control socket at fd %d", BROWSERPS5_LAUNCHER_FD);

  // What ProcessLauncherPlayStation.cpp does before launchProcess().
  if(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sv)) {
    POC_PERROR("socketpair");
    return 1;
  }
  for(int i=0; i<2; i++) {
    setsockopt(sv[i], SOL_SOCKET, SO_SNDBUF, &size, sizeof(size));
    setsockopt(sv[i], SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
  }

  // child.elf reads its socket's number from argv[1].
  pid = launcher_spawn(CHILD_PATH, "100", sv[1], -1);
  close(sv[1]);
  if(pid < 0) {
    POC_RESULT(0, "launcher started %s: %s", CHILD_PATH, strerror(-pid));
    return 1;
  }
  POC_RESULT(1, "launcher started %s (pid %d)", CHILD_PATH, pid);

  if(ipc_recv(sv[0], &hdr, &hello, sizeof(hello), 0) != sizeof(hello)
     || hdr.type != MSG_HELLO) {
    POC_PERROR("ipc_recv(HELLO)");
    POC_RESULT(0, "child said hello over the socket the app created");
    launcher_kill(pid);
    return 1;
  }
  POC_RESULT(hello.pid == pid, "child said hello over the socket the app "
             "created (pid %d, uid %d, authid 0x%016lx)", hello.pid,
             hello.uid, hello.authid);
  failures += hello.pid != pid;

  failures += test_ping(sv[0]) != 0;
  failures += test_shm(sv[0]) != 0;
  failures += test_quit(sv[0], pid) != 0;
  close(sv[0]);

  POC_LOG("done: %d failure(s)", failures);
  return failures ? 1 : 0;
}
