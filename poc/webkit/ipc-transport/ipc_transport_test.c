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

// The PC test of ipc_transport.c (make test): what ConnectionUnix.cpp does
// with it, in threads of one process. A "UI" makes a pair, sends three
// messages before anyone came, and hands the client end over an AF_UNIX
// socket with SCM_RIGHTS (the launcher's way). The "child" reads them with
// a monitor like WebKit's (select() on the number, recvmsg() until EAGAIN),
// then sends shared memory and the client end of a second pair; the UI maps
// the one and talks over the other. Then 200 messages of mixed sizes, the
// end of the connection, and the names of the shared memory taken away.

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#define IPC_TRANSPORT_CLEANER
#include "ipc_transport.h"

static int failures;

// The size of message i, the same in both threads (rand() is shared).
static size_t
size_of(int i) {
  return (size_t)((unsigned)i * 2654435761u % 4097u);
}

#define CHECK(cond, ...) do { \
    if(!(cond)) { \
      failures++; \
      printf("FAIL %s:%d: ", __FILE__, __LINE__); \
      printf(__VA_ARGS__); \
      printf("\n"); \
    } \
  } while(0)

static void
set_nonblock(int fd) {
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
}

// sendmsg() with these bytes and these descriptors.
static ssize_t
send_message(int fd, const void* data, size_t size, const int* attach, int count) {
  struct iovec iov = {(void*)data, size};
  char control[CMSG_SPACE(sizeof(int) * 4)];
  struct msghdr m = {0};
  m.msg_iov = &iov;
  m.msg_iovlen = 1;
  if(count) {
    memset(control, 0, sizeof(control));
    m.msg_control = control;
    m.msg_controllen = CMSG_SPACE(sizeof(int) * count);
    struct cmsghdr* c = CMSG_FIRSTHDR(&m);
    c->cmsg_level = SOL_SOCKET;
    c->cmsg_type = SCM_RIGHTS;
    c->cmsg_len = CMSG_LEN(sizeof(int) * count);
    memcpy(CMSG_DATA(c), attach, sizeof(int) * count);
  }
  ssize_t n;
  while((n = sendmsg(fd, &m, MSG_NOSIGNAL)) < 0 && errno == EAGAIN) {
    usleep(1000);
  }
  return n;
}

// As the socket monitor: wait for the number to be readable, then one
// recvmsg(); -1 and EAGAIN mean wait again.
static ssize_t
receive_message(int fd, void* data, size_t size, int* attach, int* count) {
  for(;;) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(fd, &set);
    struct timeval limit = {5, 0};
    if(select(fd + 1, &set, 0, 0, &limit) <= 0) {
      errno = ETIMEDOUT;
      return -1;
    }
    struct iovec iov = {data, size};
    char control[CMSG_SPACE(sizeof(int) * 254)];
    struct msghdr m = {0};
    m.msg_iov = &iov;
    m.msg_iovlen = 1;
    m.msg_control = control;
    m.msg_controllen = sizeof(control);
    ssize_t n = recvmsg(fd, &m, MSG_NOSIGNAL);
    if(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      continue;
    }
    *count = 0;
    if(n >= 0 && m.msg_controllen) {
      struct cmsghdr* c = CMSG_FIRSTHDR(&m);
      *count = (int)((c->cmsg_len - CMSG_LEN(0)) / sizeof(int));
      memcpy(attach, CMSG_DATA(c), sizeof(int) * *count);
    }
    return n;
  }
}

// 2c-2: the "title" gets only the line (no descriptor can reach it), makes
// its end from it, says hello, and answers what comes.
static void*
title_side(void* arg) {
  const char* line = arg;
  char buffer[256];
  int attach[4], count;
  int fd = ipc_transport_endpoint_from_line(line);
  CHECK(fd >= 0, "the title's end from the line: %s", strerror(errno));
  if(fd < 0) {
    return 0;
  }
  set_nonblock(fd);
  CHECK(send_message(fd, "hello from the title", 20, 0, 0) == 20, "the title's hello");
  ssize_t n = receive_message(fd, buffer, sizeof(buffer), attach, &count);
  CHECK(n == 8 && !memcmp(buffer, "and back", 8), "the UI's answer in the title: %zd", n);
  close(fd);
  return 0;
}

// The same in title mode: no placeholder (the end is connected when it
// comes back), and the received memory duplicated by name.
static void*
title_mode_side(void* arg) {
  const char* line = arg;
  char buffer[256];
  int attach[4], count = 0;
  int fd = ipc_transport_endpoint_from_line(line);
  CHECK(fd >= 0, "the title-mode end from the line: %s", strerror(errno));
  if(fd < 0) {
    return 0;
  }
  struct sockaddr_in peer;
  socklen_t length = sizeof(peer);
  CHECK(!getpeername(fd, (struct sockaddr*)&peer, &length), "connected already: %s", strerror(errno));
  set_nonblock(fd);
  CHECK(send_message(fd, "ready", 5, 0, 0) == 5, "the title-mode ready");
  ssize_t n = receive_message(fd, buffer, sizeof(buffer), attach, &count);
  CHECK(n == 6 && count == 1, "the memory in the title: %zd bytes, %d descriptors", n, count);
  if(n == 6 && count == 1) {
    int copy = dup(attach[0]);  // as WTF's dupCloseOnExec()
    CHECK(copy >= 0 && copy != attach[0], "dup of the memory: %d (%s)", copy, strerror(errno));
    close(attach[0]);  // the copy alone must hold it
    if(copy >= 0) {
      const unsigned char* bytes = mmap(0, 4096, PROT_READ, MAP_SHARED, copy, 0);
      int same = bytes != MAP_FAILED;
      for(int i = 0; same && i < 4096; i++) {
        same = bytes[i] == (unsigned char)(i * 7);
      }
      CHECK(same, "the copy's bytes");
      if(bytes != MAP_FAILED) {
        munmap((void*)bytes, 4096);
      }
      close(copy);
    }
  }
  close(fd);
  return 0;
}

typedef struct child_args {
  int launcher;   // the AF_UNIX socket the client end comes over
  int second;     // out: the listening end of the second pair, kept here
  char shm_name[64];
} child_args_t;

static void*
child(void* arg) {
  child_args_t* a = arg;
  char buffer[8192];
  int attach[4], count;

  // The client end, as the launcher gives it.
  ssize_t n = receive_message(a->launcher, buffer, sizeof(buffer), attach, &count);
  CHECK(n == 5 && count == 1, "the launcher's message: %zd bytes, %d descriptors", n, count);
  int fd = attach[0];
  set_nonblock(fd);

  // The three sent before we came, in order.
  for(int i = 0; i < 3; i++) {
    n = receive_message(fd, buffer, sizeof(buffer), attach, &count);
    char want[32];
    snprintf(want, sizeof(want), "early %d", i);
    CHECK(n == (ssize_t)strlen(want) && !memcmp(buffer, want, (size_t)n),
          "early message %d: %zd bytes", i, n);
  }

  // Shared memory, WebKit's way without memfd (a name, unlinked at once),
  // and the client end of a second pair.
  int shm = shm_open("/WK2SharedMemory.12345", O_CREAT | O_RDWR, 0600);
  CHECK(shm >= 0, "shm_open: %s", strerror(errno));
  CHECK(!shm_unlink("/WK2SharedMemory.12345"), "WebKit's shm_unlink");
  CHECK(!ftruncate(shm, 65536), "ftruncate");
  char* map = mmap(0, 65536, PROT_READ | PROT_WRITE, MAP_SHARED, shm, 0);
  for(int i = 0; i < 65536; i++) {
    map[i] = (char)(i * 7);
  }
  munmap(map, 65536);
  int pair[2];
  CHECK(!socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair), "second pair");
  set_nonblock(pair[1]);
  a->second = pair[1];
  int both[2] = {shm, pair[0]};
  CHECK(send_message(fd, "with two", 8, both, 2) == 8, "the message with attachments");
  close(shm);  // WebKit drops its copies once sent
  close(pair[0]);

  // On the second pair: what the UI says through the end it got.
  n = receive_message(pair[1], buffer, sizeof(buffer), attach, &count);
  CHECK(n == 6 && !memcmp(buffer, "second", 6), "on the second pair: %zd bytes", n);

  // 200 of mixed sizes, 0 to 4096, each its own message.
  for(int i = 0; i < 200; i++) {
    size_t size = size_of(i);
    for(size_t k = 0; k < size; k++) {
      buffer[k] = (char)(i + k);
    }
    CHECK(send_message(fd, buffer, size, 0, 0) == (ssize_t)size, "message %d", i);
  }
  close(pair[1]);
  close(fd);
  return 0;
}

int
main(void) {
  ipc_transport_set_reap_ms(100, 300);
  char buffer[8192];
  int attach[4], count;

  int pair[2];
  CHECK(!socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair), "socketpair");
  int server = pair[1], client = pair[0];
  set_nonblock(server);
  set_nonblock(client);
  for(int i = 0; i < 3; i++) {  // before anyone came: queued
    char text[32];
    snprintf(text, sizeof(text), "early %d", i);
    CHECK(send_message(server, text, strlen(text), 0, 0) == (ssize_t)strlen(text), "early %d", i);
  }

  // The launcher's socket: a plain AF_UNIX pair, SCM_RIGHTS for real.
  int launcher[2];
  CHECK(!socketpair(AF_UNIX, SOCK_STREAM, 0, launcher), "the launcher's pair");
  child_args_t args = {launcher[1], -1, ""};
  pthread_t thread;
  pthread_create(&thread, 0, child, &args);
  CHECK(send_message(launcher[0], "spawn", 5, &client, 1) == 5, "to the launcher");
  close(client);

  ssize_t n = receive_message(server, buffer, sizeof(buffer), attach, &count);
  CHECK(n == 8 && count == 2, "the message with attachments: %zd bytes, %d", n, count);
  if(count == 2) {
    struct stat st;
    CHECK(!fstat(attach[0], &st) && st.st_size == 65536, "the shared memory's size");
    char* map = mmap(0, 65536, PROT_READ, MAP_SHARED, attach[0], 0);
    int same = map != MAP_FAILED;
    for(int i = 0; same && i < 65536; i++) {
      same = map[i] == (char)(i * 7);
    }
    CHECK(same, "the shared memory's bytes");
    if(map != MAP_FAILED) {
      munmap(map, 65536);
    }
    close(attach[0]);
    int second = attach[1];
    set_nonblock(second);
    CHECK(send_message(second, "second", 6, 0, 0) == 6, "on the second pair");
    close(second);
  }

  for(int i = 0; i < 200; i++) {
    size_t size = size_of(i);
    n = receive_message(server, buffer, sizeof(buffer), attach, &count);
    int same = n == (ssize_t)size;
    for(size_t k = 0; same && k < size; k++) {
      same = buffer[k] == (char)(i + k);
    }
    CHECK(same, "message %d: %zd bytes for %zu", i, n, size);
  }
  n = receive_message(server, buffer, sizeof(buffer), attach, &count);
  CHECK(n == 0, "the end of the connection: %zd", n);
  pthread_join(thread, 0);
  close(server);

  usleep(800 * 1000);  // the reaper: every 100 ms, 300 ms after last use
  ipc_transport_stats_t s;
  ipc_transport_stats(&s);
  printf("pairs %llu, met %llu, sent %llu (%llu queued), received %llu, attachments %llu "
         "(%llu lost, %llu refused), named %llu, unlinked %llu\n",
         (unsigned long long)s.pairs, (unsigned long long)s.met,
         (unsigned long long)s.frames_sent, (unsigned long long)s.frames_queued,
         (unsigned long long)s.frames_received, (unsigned long long)s.attachments_sent,
         (unsigned long long)s.attachments_lost, (unsigned long long)s.refused,
         (unsigned long long)s.named, (unsigned long long)s.unlinked);
  const char* off = getenv("BROWSERPS5_IPC_TCP");
  if(off && off[0] == '0') {  // the plain calls: nothing of ours
    CHECK(!s.pairs && !s.frames_sent && !s.named, "BROWSERPS5_IPC_TCP=0: nothing went through us");
  } else {
    CHECK(s.pairs == 2 && s.met == 4, "two pairs, both ends met");
    CHECK(s.frames_queued == 3, "three queued");
    CHECK(s.named == 1 && s.unlinked == 1, "the shared memory's name taken away");
  }
  CHECK(!s.attachments_lost && !s.refused, "nothing lost or refused");

  // 2c-2: an end handed over as its line, to a process that cannot get
  // descriptors (a thread here), both ways.
  if(!(off && off[0] == '0')) {
    int pair[2];
    char line[64] = "";
    CHECK(!socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair), "a pair for the title");
    CHECK(!ipc_transport_endpoint_line(pair[0], line, sizeof(line)), "the client end's line");
    CHECK(!strncmp(line, "BPS5RV1 ", 8), "the line: %s", line);
    char other[64];
    CHECK(ipc_transport_endpoint_line(pair[1], other, sizeof(other)) < 0 && errno == EINVAL,
          "the server end has no line");
    CHECK(ipc_transport_endpoint_from_line("BPS5RV1 0 1\n") < 0 && errno == EINVAL,
          "a line without a port is refused");
    CHECK(ipc_transport_endpoint_from_line("hello") < 0, "not a line at all is refused");
    close(pair[0]);  // as the process launcher, once the end went over
    int server = pair[1];
    set_nonblock(server);
    pthread_t title;
    pthread_create(&title, 0, title_side, line);
    ssize_t got = receive_message(server, buffer, sizeof(buffer), attach, &count);
    CHECK(got == 20 && !memcmp(buffer, "hello from the title", 20), "the title's hello in the UI: %zd", got);
    CHECK(send_message(server, "and back", 8, 0, 0) == 8, "the UI's answer");
    pthread_join(title, 0);
    got = receive_message(server, buffer, sizeof(buffer), attach, &count);
    CHECK(got == 0, "the title's end closed: %zd", got);
    close(server);
  }

  // The list for whoever outlives this process (ipc_transport.h): empty
  // once the reaper unlinked everything; then a name held open (so the
  // reaper keeps it) is listed, and the launcher's cleaner unlinks it and
  // the list.
  if(!(off && off[0] == '0')) {
    char list_name[32], held[IPC_TRANSPORT_NAME_BYTES] = "";
    snprintf(list_name, sizeof(list_name), IPC_TRANSPORT_LIST_PREFIX "%d", (int)getpid());
    int fd = shm_open(list_name, O_RDONLY, 0);
    const ipc_transport_list_t* list = MAP_FAILED;
    if(fd >= 0) {
      list = mmap(0, sizeof(*list), PROT_READ, MAP_SHARED, fd, 0);
      close(fd);
    }
    CHECK(list != MAP_FAILED && list->magic == IPC_TRANSPORT_LIST_MAGIC
          && list->pid == (int)getpid(), "the list of names is there");
    int listed = 0;
    for(int i = 0; list != MAP_FAILED && i < IPC_TRANSPORT_LIST_SLOTS; i++) {
      listed += list->names[i][0] != 0;
    }
    CHECK(listed == 0, "nothing listed once all were unlinked: %d", listed);
    int kept = shm_open("/WK2SharedMemory.67890", O_CREAT | O_RDWR, 0600);  // as WebKit
    CHECK(kept >= 0 && !ftruncate(kept, 4096), "a shared memory object held open");
    for(int i = 0; list != MAP_FAILED && i < IPC_TRANSPORT_LIST_SLOTS; i++) {
      if(list->names[i][0]) {
        memcpy(held, list->names[i], sizeof(held));
        listed++;
      }
    }
    CHECK(listed == 1 && held[0], "it is listed: %d, %s", listed, held);
    usleep(500 * 1000);  // longer than reap_after_ms: still held, still there
    int count = ipc_transport_clean_pid((int)getpid());
    CHECK(count == 1, "the cleaner unlinked %d name(s)", count);
    int again = shm_open(held, O_RDONLY, 0);
    CHECK(again < 0 && errno == ENOENT, "the name is gone");
    if(again >= 0) {
      close(again);
    }
    again = shm_open(list_name, O_RDONLY, 0);
    CHECK(again < 0 && errno == ENOENT, "the list is gone");
    if(again >= 0) {
      close(again);
    }
    CHECK(ipc_transport_clean_pid((int)getpid()) == -1, "a second clean finds no list");
    if(list != MAP_FAILED) {
      munmap((void*)list, sizeof(*list));
    }
    if(kept >= 0) {
      close(kept);
    }
  }

  // 2c-2 in a title (last: the mode is the whole process's): the end from
  // the line is already connected, and dup() of received shared memory
  // opens its name again (a title has no dup()).
  if(!(off && off[0] == '0')) {
    ipc_transport_title_mode();
    int pair[2];
    char line[64] = "";
    CHECK(!socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair), "a pair for the title mode");
    CHECK(!ipc_transport_endpoint_line(pair[0], line, sizeof(line)), "its line");
    close(pair[0]);
    int server = pair[1];
    set_nonblock(server);
    pthread_t title;
    pthread_create(&title, 0, title_mode_side, line);
    ssize_t got = receive_message(server, buffer, sizeof(buffer), attach, &count);
    CHECK(got == 5 && !memcmp(buffer, "ready", 5), "the title's ready: %zd", got);
    int shm = shm_open("/WK2SharedMemory.24680", O_CREAT | O_RDWR, 0600);  // as WebKit
    CHECK(shm >= 0 && !ftruncate(shm, 4096), "shared memory for the title");
    unsigned char* bytes = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, shm, 0);
    CHECK(bytes != MAP_FAILED, "its mapping");
    if(bytes != MAP_FAILED) {
      for(int i = 0; i < 4096; i++) {
        bytes[i] = (unsigned char)(i * 7);
      }
      munmap(bytes, 4096);
    }
    CHECK(send_message(server, "memory", 6, &shm, 1) == 6, "the memory to the title");
    close(shm);
    pthread_join(title, 0);
    got = receive_message(server, buffer, sizeof(buffer), attach, &count);
    CHECK(got == 0, "the title's end closed: %zd", got);
    close(server);
    usleep(800 * 1000);  // past reap_after_ms: the reaper takes the name
    char name[64];
    snprintf(name, sizeof(name), IPC_TRANSPORT_NAME_PREFIX "%d-", (int)getpid());
    int listed = 0;
    for(int i = 0; i < 64; i++) {
      char one[80];
      snprintf(one, sizeof(one), "%s%d", name, i);
      int fd = shm_open(one, O_RDONLY, 0);
      if(fd >= 0) {
        listed++;
        close(fd);
      }
    }
    CHECK(!listed, "no name of ours left: %d", listed);
  }
  if(!failures) {
    printf("all passed\n");
  }
  return failures ? 1 : 0;
}
