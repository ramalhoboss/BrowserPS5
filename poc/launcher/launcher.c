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

// Launcher payload (milestone M1b in docs/02-desenho-sem-gpu.md).
//
// WebKit's UIProcess has to run inside an app (only there is sceVideoOut),
// but an app cannot start processes. So this plain payload starts them
// for it:
//   1. create the control socket (SOCK_SEQPACKET)
//   2. start the embedded starter.elf (bigapp_gl's launcher built with
//      -DBIGAPP_CTL_FD) with one end at BROWSERPS5_LAUNCHER_FD; the starter
//      opens the app and hands the socket on to it
//   3. answer the app's requests (launcher_proto.h) until it closes the
//      socket, then kill whatever it left running.

#define POC_NAME "launcher"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/wait.h>

#include "poc_log.h"
#include "elfldr/asset.h"
#include "elfldr/elfldr.h"
#include "launcher_proto.h"
#define IPC_TRANSPORT_CLEANER
#include "../webkit/ipc-transport/ipc_transport.h"

#define MAX_CHILDREN 32

// What it starts first. The browser's GPU build (poc/browser/title,
// docs/03-video-e-gpu.md) starts WebKit's UIProcess itself, as a payload
// with the socket at BROWSERPS5_LAUNCHER_FD; that payload opens the
// browser's title.
#ifndef LAUNCHER_FIRST_CHILD
#define LAUNCHER_FIRST_CHILD "starter.elf"
#endif

INCASSET(starter_elf, LAUNCHER_FIRST_CHILD);

static pid_t children[MAX_CHILDREN];


static void
track(pid_t pid) {
  for(int i=0; i<MAX_CHILDREN; i++) {
    if(children[i] <= 0) {
      children[i] = pid;
      return;
    }
  }
  POC_LOG("more than %d children, pid %d is not tracked", MAX_CHILDREN, pid);
}


/**
 * The shared memory names a WebKit process made and had not unlinked when
 * it ended (killed or crashed): with WebKit's IPC over TCP (IPC_TCP=1,
 * webkit/ipc-transport) they would keep their memory until the console
 * restarts. The process lists them; nothing is done without that list.
 **/
static void
clean_names(pid_t pid) {
  const int left = ipc_transport_clean_pid(pid);
  if(left > 0) {
    POC_LOG("process %d left %d shared memory name(s): unlinked", pid, left);
  }
}


/**
 * Collect the children that exited, so they do not stay zombies.
 **/
static void
reap(void) {
  int status;
  pid_t pid;

  while((pid=waitpid(-1, &status, WNOHANG)) > 0) {
    POC_LOG("process %d exited, status 0x%x", pid, status);
    clean_names(pid);
    for(int i=0; i<MAX_CHILDREN; i++) {
      if(children[i] == pid) {
        children[i] = 0;
      }
    }
  }
}


/**
 * LAUNCHER_KILL: SIGKILL, then wait (sleeping, up to 3 s) until the
 * process has exited and reap it, so that the answer comes when it is
 * gone and kill(pid, 0) says ESRCH: the app has the system close the title
 * only once the WebProcess, a client of the title's GPU, no longer lives
 * (title/remote.c close_title(); 2026-11-05 16:51 the console went down
 * when the title was closed with it still there). Returns 0 or -errno
 * (-ETIMEDOUT: killed, but still there after 3 s).
 **/
// 2026-11-10: the stop watch. Once a second, whether a child was stopped
// by a signal (waitpid WUNTRACED: the launcher is the parent): the page's
// WebProcess stopped whole after Telegram Web, its probe threads too, and the
// kernel does not show another process's threads (sysctl KERN_PROC_PID with
// another pid: ENOENT). A stopped child gets SIGCONT, to see whether the page
// comes back. None was seen stopped yet (17:58).
static void*
stop_watch(void* arg) {
  (void)arg;
  int told_echild = 0;
  // 18:20: the user's WhatsApp page (pid 204) was stopped by SIGSTOP (17) and
  // SIGCONT did not wake it: no "continued", no line from it again (the PS5
  // suspends processes its own way, sceKernelSuspendProcess, and its
  // sceKernelResumeProcess is not in the launcher's libkernel: 0x80020003).
  // 18:35: killing a stopped child is not safe: the speed test stopped and
  // killed the Telegram page's WebProcess, the title closed and the PS5 went
  // down. A stopped child gets SIGCONT and a log line, nothing more.
  for(;;) {
    sleep(1);
    for(int i = 0; i < MAX_CHILDREN; i++) {
      const pid_t pid = __atomic_load_n(&children[i], __ATOMIC_RELAXED);
      int status;
      if(pid <= 0) {
        continue;
      }
      const pid_t got = waitpid(pid, &status, WNOHANG | WUNTRACED | WCONTINUED);
      if(got < 0 && errno == ECHILD && !told_echild) {
        POC_LOG("stop watch: process %d is not our child", pid);
        told_echild = 1;
      }
      if(got != pid) {
        continue;
      }
      if(WIFSTOPPED(status)) {
        const int continued = kill(pid, SIGCONT);
        POC_LOG("process %d STOPPED by signal %d: SIGCONT %d", pid, WSTOPSIG(status), continued);
      } else if(WIFCONTINUED(status)) {
        POC_LOG("process %d continued", pid);
      } else {
        POC_LOG("process %d exited, status 0x%x (stop watch)", pid, status);
        __atomic_store_n(&children[i], 0, __ATOMIC_RELAXED);
        clean_names(pid);
      }
    }
  }
  return 0;
}


static int
kill_and_reap(pid_t pid) {
  const double start = poc_now_ms();
  int status;

  if(kill(pid, SIGKILL)) {
    return -errno;
  }
  for(;;) {
    pid_t got = waitpid(pid, &status, WNOHANG);
    if(got == pid || (got < 0 && errno == ECHILD)) {  // ECHILD: not ours, or reaped
      break;
    }
    if(poc_now_ms() - start > 3000) {
      POC_LOG("process %d killed, still there after 3 s", pid);
      return -ETIMEDOUT;
    }
    usleep(5 * 1000);
  }
  for(int i=0; i<MAX_CHILDREN; i++) {
    if(children[i] == pid) {
      children[i] = 0;
    }
  }
  POC_LOG("process %d killed, gone in %.0f ms", pid, poc_now_ms() - start);
  clean_names(pid);
  return 0;
}


static int
read_file(const char* path, uint8_t** data, size_t* size) {
  struct stat info;
  uint8_t* buf;
  int fd;

  if((fd=open(path, O_RDONLY)) < 0 || fstat(fd, &info)) {
    int err = errno;
    POC_PERROR(path);
    if(fd >= 0) {
      close(fd);
    }
    return -err;
  }
  if(!(buf=malloc(info.st_size))) {
    close(fd);
    return -ENOMEM;
  }
  if(read(fd, buf, info.st_size) != info.st_size) {
    POC_PERROR(path);
    free(buf);
    close(fd);
    return -EIO;
  }
  close(fd);

  *data = buf;
  *size = info.st_size;
  return 0;
}


static int32_t
do_spawn(const launcher_req_t* req, int ipc_fd) {
  char path[LAUNCHER_PATH_MAX];
  char arg[LAUNCHER_ARG_MAX];
  char* argv[] = {path, arg, 0};
  uint8_t* elf;
  size_t size;
  pid_t pid;
  int err;

  memcpy(path, req->path, sizeof(path));
  memcpy(arg, req->arg, sizeof(arg));
  path[sizeof(path)-1] = 0;
  arg[sizeof(arg)-1] = 0;

  if((err=read_file(path, &elf, &size))) {
    return err;
  }
  pid = elfldr_spawn_ipc(STDOUT_FILENO, ipc_fd, BROWSERPS5_CHILD_IPC_FD, argv,
                         elf, size);
  free(elf);

  if(pid < 0) {
    POC_LOG("could not start %s", path);
    return -EIO;
  }
  track(pid);
  POC_LOG("started %s %s (pid %d, user %d)", path, arg, pid, req->user_id);
  return pid;
}


/**
 * Receive one request, with the descriptor it carries (or -1).
 **/
static ssize_t
recv_req(int sock, launcher_req_t* req, int* fd) {
  char control[CMSG_SPACE(sizeof(int))];
  struct msghdr msg = {0};
  struct cmsghdr* cmsg;
  struct iovec iov;
  ssize_t n;

  iov.iov_base = req;
  iov.iov_len = sizeof(*req);
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = control;
  msg.msg_controllen = sizeof(control);

  *fd = -1;
  if((n=recvmsg(sock, &msg, 0)) <= 0) {
    return n;
  }
  for(cmsg=CMSG_FIRSTHDR(&msg); cmsg; cmsg=CMSG_NXTHDR(&msg, cmsg)) {
    if(cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
      memcpy(fd, CMSG_DATA(cmsg), sizeof(int));
    }
  }
  return n;
}


static void
reply(int sock, int32_t result) {
  launcher_rep_t rep = {result};

  if(send(sock, &rep, sizeof(rep), 0) < 0) {
    POC_PERROR("send(reply)");
  }
}


int
main(void) {
  char* argv[] = {"BrowserPS5Starter", 0};
  launcher_req_t req;
  pid_t starter;
  int served = 0;
  int ctl[2];
  ssize_t n;
  int fd;

  signal(SIGPIPE, SIG_IGN);

  if(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, ctl)) {
    POC_PERROR("socketpair");
    return 1;
  }

  starter = elfldr_spawn_ipc(STDOUT_FILENO, ctl[1], BROWSERPS5_LAUNCHER_FD,
                             argv, starter_elf, starter_elf_size);
  close(ctl[1]);
  if(starter < 0) {
    POC_NOTIFY("FAIL could not start the starter");
    return 1;
  }
  POC_LOG("starter running (pid %d), waiting for the app's requests", starter);
  pthread_t watch;
  if(pthread_create(&watch, 0, stop_watch, 0) == 0) {
    pthread_detach(watch);
  }

  // Ends when the app (and the starter) have closed their ends.
  while((n=recv_req(ctl[0], &req, &fd)) != 0) {
    if(n < 0) {
      if(errno == EINTR) {
        continue;
      }
      POC_PERROR("recvmsg");
      break;
    }

    if(n != sizeof(req)) {
      reply(ctl[0], -EPROTO);
    } else if(req.type == LAUNCHER_SPAWN) {
      reply(ctl[0], fd < 0 ? -EBADF : do_spawn(&req, fd));
    } else if(req.type == LAUNCHER_KILL) {
      reply(ctl[0], kill_and_reap(req.pid));
    } else {
      reply(ctl[0], -EINVAL);
    }
    if(fd >= 0) {
      close(fd);
    }
    served++;
    reap();
  }
  close(ctl[0]);

  POC_LOG("control socket closed after %d request(s)", served);
  for(int i=0; i<MAX_CHILDREN; i++) {
    if(children[i] > 0) {
      POC_LOG("killing leftover process %d", children[i]);
      kill(children[i], SIGKILL);
    }
  }
  // The starter waits for the app to exit, then exits too.
  waitpid(starter, 0, 0);
  clean_names(starter);
  reap();
  return 0;
}
