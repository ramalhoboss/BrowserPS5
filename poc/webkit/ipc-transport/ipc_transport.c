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

// WebKit's IPC without passing descriptors (docs/05-webprocess-no-titulo.md,
// "2c: o IPC por TCP e memória com nome"), linked into libWebKit with
// -Wl,--wrap= socketpair, sendmsg, recvmsg, shm_open and shm_unlink
// (build-webkit.sh, IPC_TCP=1).
//
// A title cannot get a descriptor from a payload: there are no named
// AF_UNIX sockets, and TCP and named shared memory are all the two share
// (poc/title_ipc). WebKit's IPC (Platform/IPC/unix/ConnectionUnix.cpp)
// sends its connections and its shared memory as descriptors (SCM_RIGHTS)
// over socketpair(AF_UNIX, SOCK_SEQPACKET). Under it, here:
//
//  - socketpair(AF_UNIX, SOCK_SEQPACKET) makes a TCP pair "by meeting".
//    The end WebKit keeps (sv[1], the server) listens on 127.0.0.1. The
//    other (sv[0], the client, which goes to the other process) is a
//    placeholder that says where to meet: "BPS5RV1 <port> <secret>\n"
//    waiting to be read in a local socket (a pipe where there is none).
//    In a message it travels as those bytes, and the receiver gets a
//    placeholder of its own, so it can be sent on (the UIProcess passes
//    the NetworkProcess's ends to the WebProcess). Through the launcher it
//    goes as a descriptor, payload to payload, with the same bytes in it.
//  - The first sendmsg() or recvmsg() on a placeholder connects and sends
//    the secret. The listening end accepts in recvmsg(), on the socket
//    monitor's thread (ConnectionUnix.cpp, PLATFORM(PLAYSTATION): a thread
//    in select() on the descriptor's number), when the connection makes it
//    readable; what was sent before waits in a queue and goes first. Either
//    way the TCP socket is dup2()'d over the descriptor, so the number stays
//    the one WebKit holds; O_NONBLOCK and FD_CLOEXEC are carried over. Only
//    that thread accepts: a dup2() under another thread's select() could
//    leave it waiting on the closed socket.
//  - SOCK_SEQPACKET's messages over TCP: sendmsg() writes a frame (a
//    header, the attachments, the bytes) whole, under a lock; recvmsg()
//    reads one whole frame and gives the attachments back as descriptors,
//    in SCM_RIGHTS, as the socket would.
//  - shm_open(SHM_ANON), or WebKit's "/WK2SharedMemory." name (which it
//    unlinks at once): a name of ours, "/bps5wk-<pid>-<n>", in a table by
//    object (st_dev, st_ino). An attachment that is one goes by its name;
//    the receiver opens the name. The creator unlinks it once it has no
//    descriptor on it left and nothing was sent for reap_after_ms (a
//    thread looks every reap_every_ms): a receiver may still be on its way
//    to opening it. The names not unlinked yet are also in a table in
//    shared memory, "/bps5wkt-<pid>" (ipc_transport.h), so that the
//    launcher unlinks them when the process ends without doing it (killed
//    or crashed; launcher.c, 2026-11-05 17:30).
//
// BROWSERPS5_IPC_TCP=0 in the environment (env.txt) makes all of it the
// plain calls; every process must then have it so.
//
// In a title (the WebProcess in the browser title, 2c-2; after
// ipc_transport_title_mode()) there is no dup() or dup2() (EPERM): an end
// that comes as a line or an attachment is connected at once and handed
// over as the TCP socket itself, and dup() of shared memory with a name of
// ours opens the name again. Listening ends cannot be met there (their
// accept needs dup2()); the WebProcess never makes any (2c-1 on the
// console: "pairs 0").

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <poll.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

int __real_socketpair(int domain, int type, int protocol, int sv[2]);
ssize_t __real_sendmsg(int fd, const struct msghdr* msg, int flags);
ssize_t __real_recvmsg(int fd, struct msghdr* msg, int flags);
int __real_shm_open(const char* name, int oflag, mode_t mode);
int __real_shm_unlink(const char* name);
int __real_dup(int fd);

#define IPC_TRANSPORT_CLEANER
#define IPC_TRANSPORT_REAL(f) __real_##f
#include "ipc_transport.h"

#ifndef SHM_ANON
#define SHM_ANON ((char*)1)
#endif
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define RV_MAGIC        "BPS5RV1 "
#define RV_LINE_MAX     64
#define FRAME_MAGIC     0x46355042u   // "BP5F"
#define FRAME_BODY_MAX  (64 * 1024)   // inline bodies are 4096 at most
#define ATTACH_MAX      254           // ConnectionUnix's attachmentMaxAmount
#define NAME_BYTES      40
#define FD_TABLE_SIZE   4096
#define SHM_TABLE_SIZE  4096          // live shared memory objects with our names
#define WAIT_MS         10000         // the secret; the rest of a frame
#define STATS_EVERY_MS  30000         // the counters in the log

enum { ATTACH_SHM = 1, ATTACH_MEET = 2 };

typedef struct frame_header {
  uint32_t magic;
  uint32_t body;     // bytes
  uint32_t count;    // attachments
  uint32_t zero;
} frame_header_t;

typedef struct attachment {
  uint32_t kind;
  uint16_t port;     // ATTACH_MEET
  uint16_t zero;
  uint64_t secret;   // ATTACH_MEET
  char name[NAME_BYTES];  // ATTACH_SHM
} attachment_t;

typedef struct pending {
  struct pending* next;
  size_t size;
  char frame[];
} pending_t;

// A descriptor of ours: a listening end, or a connected one.
enum { FD_NONE, FD_LISTENING, FD_CONNECTED };
typedef struct fd_entry {
  int state;
  uint16_t port;     // the listening port, or the local one
  uint16_t peer;     // FD_CONNECTED: the other end's port
  uint64_t secret;   // FD_LISTENING
  pending_t* head;   // FD_LISTENING: frames sent before the other end came
  pending_t* tail;
  pthread_mutex_t meet_lock;  // one meeting at a time
  pthread_mutex_t send_lock;  // a frame goes out whole; the state changes
} fd_entry_t;

typedef struct shm_entry {
  int used;
  dev_t dev;
  ino_t ino;
  int ours;            // made here: unlinked here
  uint64_t touched_ms; // made, received or last sent
  char name[NAME_BYTES];
} shm_entry_t;

_Static_assert(SHM_TABLE_SIZE == IPC_TRANSPORT_LIST_SLOTS, "the list has a slot per entry");
_Static_assert(NAME_BYTES == IPC_TRANSPORT_NAME_BYTES, "the list's names are ours");

static fd_entry_t fds[FD_TABLE_SIZE];
static shm_entry_t shms[SHM_TABLE_SIZE];
// The names of ours not unlinked yet, slot i for shms[i], for whoever
// outlives this process (ipc_transport.h); written under table_lock.
static ipc_transport_list_t* list;
static pthread_mutex_t table_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t init_once = PTHREAD_ONCE_INIT;
static int enabled = 1;
static int no_dup;  // a title: ipc_transport_title_mode()
static unsigned shm_counter;
static unsigned reap_every_ms = 2000;
static unsigned reap_after_ms = 10000;
static ipc_transport_stats_t stats;
static int log_fd = 2;  // ipc_transport_log_fd(): a title's own file
// O_NONBLOCK as asked for an end the title would not set it on
// (ipc_transport_keep_nonblock()).
static int kept_nonblock[FD_TABLE_SIZE];

#define COUNT(field, n) __atomic_add_fetch(&stats.field, (n), __ATOMIC_RELAXED)


void
ipc_transport_log_fd(int fd) {
  log_fd = fd;
}


static uint64_t
now_ms(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000;
}


static void
note(const char* format, ...) {
  char line[256] = "[ipc-tcp] ";
  size_t at = strlen(line);
  va_list ap;
  va_start(ap, format);
  int n = vsnprintf(line + at, sizeof(line) - at - 1, format, ap);
  va_end(ap);
  if(n < 0) {
    return;
  }
  at += (size_t)n < sizeof(line) - at - 1 ? (size_t)n : sizeof(line) - at - 2;
  line[at++] = '\n';
  (void)!write(log_fd, line, at);
}


static uint64_t
random_secret(void) {
  uint64_t value = 0;
  int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
  if(fd >= 0) {
    (void)!read(fd, &value, sizeof(value));
    close(fd);
  }
  if(!value) {  // no /dev/urandom: the clock, an address, the pid
    value = now_ms() * 0x9e3779b97f4a7c15ull ^ (uintptr_t)&value ^ (uint64_t)getpid() << 32;
  }
  return value;
}


static void* reaper(void* arg);

// The table of our names in shared memory (ipc_transport.h). Without it
// the names work as before, but a killed process leaves its last ones.
static void
list_open(void) {
  const int pid = (int)getpid();
  char name[32];
  snprintf(name, sizeof(name), IPC_TRANSPORT_LIST_PREFIX "%d", pid);
  const int left = ipc_transport_clean_pid(pid);  // an earlier process with this pid
  if(left > 0) {
    note("%d name(s) left by an earlier process %d: unlinked", left, pid);
  }
  int fd = __real_shm_open(name, O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC, 0600);
  if(fd < 0) {
    note("%s: %s (the names are not listed)", name, strerror(errno));
    return;
  }
  void* memory = MAP_FAILED;
  if(!ftruncate(fd, sizeof(ipc_transport_list_t))) {
    memory = mmap(0, sizeof(ipc_transport_list_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  }
  close(fd);
  if(memory == MAP_FAILED) {
    note("%s: %s (the names are not listed)", name, strerror(errno));
    __real_shm_unlink(name);
    return;
  }
  ipc_transport_list_t* l = (ipc_transport_list_t*)memory;
  l->pid = pid;
  l->slots = IPC_TRANSPORT_LIST_SLOTS;
  __atomic_store_n(&l->magic, IPC_TRANSPORT_LIST_MAGIC, __ATOMIC_RELEASE);
  list = l;
}


// Slot i of the list follows shms[i]: its name while it is ours, else "".
static void
list_set(int i) {  // with table_lock
  if(!list) {
    return;
  }
  if(shms[i].used && shms[i].ours) {
    memcpy(list->names[i], shms[i].name, NAME_BYTES);
  } else {
    list->names[i][0] = 0;
  }
}


static void
init(void) {
  const char* e = getenv("BROWSERPS5_IPC_TCP");
  enabled = !(e && e[0] == '0');
  // Zeroes are PTHREAD_MUTEX_INITIALIZER on FreeBSD (a lock made at its
  // first use) and in glibc: the table's 8192 locks then cost nothing
  // until a descriptor needs them.
  static const pthread_mutex_t initializer = PTHREAD_MUTEX_INITIALIZER;
  static const char zeroes[sizeof(pthread_mutex_t)];
  if(memcmp(&initializer, zeroes, sizeof(zeroes))) {
    for(int i = 0; i < FD_TABLE_SIZE; i++) {
      pthread_mutex_init(&fds[i].meet_lock, 0);
      pthread_mutex_init(&fds[i].send_lock, 0);
    }
  }
  if(enabled) {
    note("on in process %d", (int)getpid());
    list_open();
    pthread_t thread;
    if(!pthread_create(&thread, 0, reaper, 0)) {
      pthread_detach(thread);
    }
  }
}


static int
on(void) {
  pthread_once(&init_once, init);
  return enabled;
}


void
ipc_transport_set_reap_ms(unsigned every_ms, unsigned after_ms) {
  reap_every_ms = every_ms;
  reap_after_ms = after_ms;
}


// ---------------------------------------------------------------------------
// Whole reads and writes, waiting a while for each piece.

static int
wait_for(int fd, short events, int timeout_ms) {
  struct pollfd p = {fd, events, 0};
  for(;;) {
    int n = poll(&p, 1, timeout_ms);
    if(n > 0) {
      return 0;
    }
    if(!n) {
      errno = ETIMEDOUT;
      return -1;
    }
    if(errno != EINTR) {
      return -1;
    }
  }
}


static int
write_all(int fd, const void* data, size_t size) {
  const char* p = data;
  while(size) {
    ssize_t n = send(fd, p, size, MSG_NOSIGNAL);
    if(n > 0) {
      p += n;
      size -= (size_t)n;
    } else if(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      if(wait_for(fd, POLLOUT, WAIT_MS)) {
        return -1;
      }
    } else if(n < 0 && errno == EINTR) {
      continue;
    } else {
      if(!n) {
        errno = EPIPE;
      }
      return -1;
    }
  }
  return 0;
}


// The rest of what has started: the other end writes frames whole.
static int
read_all(int fd, void* data, size_t size) {
  char* p = data;
  while(size) {
    ssize_t n = recv(fd, p, size, 0);
    if(n > 0) {
      p += n;
      size -= (size_t)n;
    } else if(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      if(wait_for(fd, POLLIN, WAIT_MS)) {
        return -1;
      }
    } else if(n < 0 && errno == EINTR) {
      continue;
    } else {
      if(!n) {
        errno = ECONNRESET;
      }
      return -1;
    }
  }
  return 0;
}


// ---------------------------------------------------------------------------
// The table of our sockets. WebKit closes ours and a number can come back
// as something else: every use checks the ports again.

static uint16_t
local_port(int fd) {
  struct sockaddr_in a;
  socklen_t length = sizeof(a);
  if(getsockname(fd, (struct sockaddr*)&a, &length) || a.sin_family != AF_INET) {
    return 0;
  }
  return ntohs(a.sin_port);
}


static uint16_t
peer_port(int fd) {
  struct sockaddr_in a;
  socklen_t length = sizeof(a);
  if(getpeername(fd, (struct sockaddr*)&a, &length) || a.sin_family != AF_INET) {
    return 0;
  }
  return ntohs(a.sin_port);
}


static void
drop_pending(fd_entry_t* e) {  // with its send_lock
  while(e->head) {
    pending_t* p = e->head;
    e->head = p->next;
    free(p);
  }
  e->tail = 0;
}


// What fd is to us now: FD_NONE, FD_LISTENING or FD_CONNECTED.
static int
fd_state(int fd) {
  if(fd < 0 || fd >= FD_TABLE_SIZE) {
    return FD_NONE;
  }
  pthread_mutex_lock(&table_lock);
  int state = fds[fd].state;
  uint16_t port = fds[fd].port;
  uint16_t peer = fds[fd].peer;
  pthread_mutex_unlock(&table_lock);
  if(state == FD_NONE) {
    return FD_NONE;
  }
  if(local_port(fd) != port || (state == FD_CONNECTED && peer_port(fd) != peer)) {
    pthread_mutex_lock(&table_lock);
    fds[fd].state = FD_NONE;  // closed, and the number is another's now
    pthread_mutex_unlock(&table_lock);
    return FD_NONE;
  }
  return state;
}


static void
fd_set_state(int fd, int state, uint64_t secret) {
  uint16_t port = local_port(fd);
  uint16_t peer = state == FD_CONNECTED ? peer_port(fd) : 0;
  __atomic_store_n(&kept_nonblock[fd], 0, __ATOMIC_RELEASE);  // a new end starts blocking
  pthread_mutex_lock(&table_lock);
  fds[fd].state = state;
  fds[fd].port = port;
  fds[fd].peer = peer;
  fds[fd].secret = secret;
  pthread_mutex_unlock(&table_lock);
}


// The TCP socket takes the place of the descriptor WebKit holds, with its
// flags.
static int
take_place(int socket_fd, int fd) {
  int flags = fcntl(fd, F_GETFL);
  int fd_flags = fcntl(fd, F_GETFD);
  int one = 1;
  setsockopt(socket_fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
#ifdef SO_NOSIGPIPE
  setsockopt(socket_fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
  if(dup2(socket_fd, fd) < 0) {
    int saved = errno;
    close(socket_fd);
    errno = saved;
    return -1;
  }
  close(socket_fd);
  if(flags >= 0) {
    int now = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, (now & ~O_NONBLOCK) | (flags & O_NONBLOCK));
  }
  if(fd_flags >= 0) {
    fcntl(fd, F_SETFD, fd_flags);
  }
  fd_set_state(fd, FD_CONNECTED, 0);
  return 0;
}


// ---------------------------------------------------------------------------
// The meeting.

// A placeholder holding line: a local socket with it waiting to be read,
// or a pipe where there are no AF_UNIX pairs.
static int
placeholder(const char* line) {
  int pair[2];
  size_t length = strlen(line);
  if(!__real_socketpair(AF_UNIX, SOCK_STREAM, 0, pair)) {
    int err = write_all(pair[1], line, length);
    close(pair[1]);
    if(err) {
      close(pair[0]);
      return -1;
    }
    return pair[0];
  }
  if(pipe(pair)) {
    return -1;
  }
  (void)!write(pair[1], line, length);
  close(pair[1]);
  return pair[0];
}


// Is fd a placeholder? Then where to meet. A socket keeps its line (it is
// only peeked at); a pipe's is read, and the pipe is left at its end.
static int
placeholder_peek(int fd, uint16_t* port, uint64_t* secret) {
  char line[RV_LINE_MAX];
  ssize_t n = recv(fd, line, sizeof(line) - 1, MSG_PEEK | MSG_DONTWAIT);
  if(n < 0 && errno == ENOTSOCK) {
    struct stat st;
    if(fstat(fd, &st) || !S_ISFIFO(st.st_mode)) {
      return 0;
    }
    int flags = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    n = read(fd, line, sizeof(line) - 1);
    fcntl(fd, F_SETFL, flags);
  }
  if(n < (ssize_t)strlen(RV_MAGIC) || memcmp(line, RV_MAGIC, strlen(RV_MAGIC))) {
    return 0;
  }
  line[n] = 0;
  unsigned p = 0;
  unsigned long long s = 0;
  if(sscanf(line + strlen(RV_MAGIC), "%u %llx", &p, &s) != 2 || !p || p > 65535) {
    return 0;
  }
  *port = (uint16_t)p;
  *secret = s;
  return 1;
}


// ipc_transport.h: an end as its meeting line, for a process that cannot
// get descriptors (the title, 2c-2), and a placeholder from such a line.
int
ipc_transport_endpoint_line(int fd, char* line, size_t size) {
  uint16_t port;
  uint64_t secret;
  if(!on() || fd_state(fd) != FD_NONE || !placeholder_peek(fd, &port, &secret)) {
    errno = EINVAL;
    return -1;
  }
  int n = snprintf(line, size, RV_MAGIC "%u %llx\n", port, (unsigned long long)secret);
  if(n < 0 || (size_t)n >= size) {
    errno = ENAMETOOLONG;
    return -1;
  }
  return 0;
}


// A TCP socket connected to the meeting on port, the secret sent.
static int
dial(uint16_t port, uint64_t secret) {
  int s = socket(AF_INET, SOCK_STREAM, 0);
  if(s < 0) {
    return -1;
  }
  struct sockaddr_in a = {0};
  a.sin_family = AF_INET;
  a.sin_port = htons(port);
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  int err;
  do {
    err = connect(s, (struct sockaddr*)&a, sizeof(a));
  } while(err && errno == EINTR);
  if(err || write_all(s, &secret, sizeof(secret))) {
    int saved = errno;
    note("meeting on port %u: %s", port, strerror(saved));
    close(s);
    errno = saved;
    return -1;
  }
  COUNT(met, 1);
  return s;
}


// In a title: the end met now, as the TCP socket itself (no dup2 there).
static int
connect_now(uint16_t port, uint64_t secret) {
  int s = dial(port, secret);
  if(s < 0) {
    return -1;
  }
  if(s >= FD_TABLE_SIZE) {
    note("descriptor %d is past the table (%d)", s, FD_TABLE_SIZE);
    close(s);
    errno = EMFILE;
    return -1;
  }
  int one = 1;
  setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
#ifdef SO_NOSIGPIPE
  setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
  fcntl(s, F_SETFD, FD_CLOEXEC);
  fd_set_state(s, FD_CONNECTED, 0);
  return s;
}


int
ipc_transport_endpoint_from_line(const char* line) {
  unsigned p = 0;
  unsigned long long s = 0;
  const size_t magic = strlen(RV_MAGIC);
  if(!on() || strlen(line) >= RV_LINE_MAX || strncmp(line, RV_MAGIC, magic)
     || sscanf(line + magic, "%u %llx", &p, &s) != 2 || !p || p > 65535) {
    errno = EINVAL;
    return -1;
  }
  if(no_dup) {
    return connect_now((uint16_t)p, s);
  }
  return placeholder(line);  // as the client end socketpair() gives
}


void
ipc_transport_title_mode(void) {
  if(on()) {
    no_dup = 1;
    note("title mode in process %d: ends are met at once, dup() opens names again",
         (int)getpid());
  }
}


static int
meet_connect(int fd, uint16_t port, uint64_t secret) {
  int s = dial(port, secret);
  return s < 0 ? -1 : take_place(s, fd);
}


// The listening end, in recvmsg(): a connection with the right secret, if
// one is waiting; then what was queued goes first. EAGAIN if none is.
static int
meet_accept(int fd) {
  fd_entry_t* e = &fds[fd];
  for(;;) {
    int s = accept(fd, 0, 0);
    if(s < 0) {
      if(errno == EINTR) {
        continue;
      }
      if(errno == EWOULDBLOCK) {
        errno = EAGAIN;
      }
      return -1;
    }
    int flags = fcntl(s, F_GETFL);
    fcntl(s, F_SETFL, flags & ~O_NONBLOCK);
    uint64_t got = 0;
    if(wait_for(s, POLLIN, WAIT_MS) || read_all(s, &got, sizeof(got)) || got != e->secret) {
      note("a connection on port %u without the secret: closed", e->port);
      close(s);
      if(wait_for(fd, POLLIN, 0)) {
        errno = EAGAIN;
        return -1;
      }
      continue;
    }
    pthread_mutex_lock(&e->send_lock);
    int err = take_place(s, fd);  // dup2 closes the listening socket
    while(!err && e->head) {
      pending_t* p = e->head;
      err = write_all(fd, p->frame, p->size);
      e->head = p->next;
      free(p);
    }
    if(err) {
      drop_pending(e);
    }
    e->tail = 0;
    pthread_mutex_unlock(&e->send_lock);
    if(!err) {
      COUNT(met, 1);
    }
    return err;
  }
}


// Before a frame comes or goes on fd: FD_CONNECTED, FD_LISTENING (not met
// yet; only recvmsg() meets it), FD_NONE (not ours), or -1.
static int
ready(int fd, int may_accept) {
  int state = fd_state(fd);
  if(state == FD_CONNECTED) {
    return state;
  }
  uint16_t port;
  uint64_t secret;
  if(state == FD_NONE && !placeholder_peek(fd, &port, &secret)) {
    return FD_NONE;
  }
  if(fd < 0 || fd >= FD_TABLE_SIZE) {
    note("descriptor %d is past the table (%d)", fd, FD_TABLE_SIZE);
    errno = EMFILE;
    return -1;
  }
  pthread_mutex_lock(&fds[fd].meet_lock);
  state = fd_state(fd);  // another thread may have met it meanwhile
  int err = 0;
  if(state == FD_LISTENING && may_accept) {
    err = meet_accept(fd);
  } else if(state == FD_NONE) {
    err = meet_connect(fd, port, secret);
  }
  pthread_mutex_unlock(&fds[fd].meet_lock);
  return err ? -1 : fd_state(fd);
}


int
__wrap_socketpair(int domain, int type, int protocol, int sv[2]) {
  int base = type & ~(SOCK_CLOEXEC | SOCK_NONBLOCK);
  if(!on() || domain != AF_UNIX || base != SOCK_SEQPACKET) {
    return __real_socketpair(domain, type, protocol, sv);
  }
  int listener = socket(AF_INET, SOCK_STREAM, 0);
  if(listener < 0) {
    return -1;
  }
  struct sockaddr_in a = {0};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if(listener >= FD_TABLE_SIZE || bind(listener, (struct sockaddr*)&a, sizeof(a))
     || listen(listener, 4)) {
    int saved = listener >= FD_TABLE_SIZE ? EMFILE : errno;
    close(listener);
    errno = saved;
    return -1;
  }
  uint64_t secret = random_secret();
  char line[RV_LINE_MAX];
  snprintf(line, sizeof(line), RV_MAGIC "%u %llx\n", local_port(listener),
           (unsigned long long)secret);
  int client = placeholder(line);
  if(client < 0) {
    int saved = errno;
    close(listener);
    errno = saved;
    return -1;
  }
  if(type & SOCK_CLOEXEC) {
    fcntl(listener, F_SETFD, FD_CLOEXEC);
    fcntl(client, F_SETFD, FD_CLOEXEC);
  }
  if(type & SOCK_NONBLOCK) {
    fcntl(listener, F_SETFL, O_NONBLOCK);
    fcntl(client, F_SETFL, O_NONBLOCK);
  }
  pthread_mutex_lock(&fds[listener].send_lock);
  drop_pending(&fds[listener]);  // a closed one's, never met
  pthread_mutex_unlock(&fds[listener].send_lock);
  fd_set_state(listener, FD_LISTENING, secret);
  COUNT(pairs, 1);
  sv[0] = client;    // WebKit's client end: it goes to the other process
  sv[1] = listener;  // its server end: it stays
  return 0;
}


// ---------------------------------------------------------------------------
// Shared memory by name.

static int
shm_find(dev_t dev, ino_t ino) {  // with table_lock
  for(int i = 0; i < SHM_TABLE_SIZE; i++) {
    if(shms[i].used && shms[i].dev == dev && shms[i].ino == ino) {
      return i;
    }
  }
  return -1;
}


// One round of the reaper, now (below).
static void reap_round(uint64_t ours_ms, uint64_t others_ms);

static void
shm_remember(int fd, const char* name, int ours) {
  struct stat st;
  if(fstat(fd, &st)) {
    return;
  }
  pthread_mutex_lock(&table_lock);
  int i = shm_find(st.st_dev, st.st_ino);
  for(int j = 0; i < 0 && j < SHM_TABLE_SIZE; j++) {
    if(!shms[j].used) {
      i = j;
    }
  }
  if(i < 0) {
    // 2026-11-11: full. The reaper frees an entry 10 s after its last use,
    // and WhatsApp Web syncing sent the NetworkProcess more than 4096
    // objects in less: its own next object had no entry, could not be sent
    // by name ("an attachment that cannot go by name", EINVAL), WebKit
    // dropped that message and left its queue of outgoing messages for good
    // (ConnectionUnix), and the page that waited for a sync reply behind it
    // stood still with every other page (docs/28). A round at once: what
    // came from another process and is closed here goes now, ours a second
    // after its last send.
    pthread_mutex_unlock(&table_lock);
    reap_round(1000, 0);
    pthread_mutex_lock(&table_lock);
    i = shm_find(st.st_dev, st.st_ino);
    for(int j = 0; i < 0 && j < SHM_TABLE_SIZE; j++) {
      if(!shms[j].used) {
        i = j;
      }
    }
  }
  if(i >= 0) {
    if(!shms[i].used || ours) {
      shms[i].ours = ours;
    }
    shms[i].used = 1;
    shms[i].dev = st.st_dev;
    shms[i].ino = st.st_ino;
    shms[i].touched_ms = now_ms();
    snprintf(shms[i].name, sizeof(shms[i].name), "%s", name);
    list_set(i);
  }
  pthread_mutex_unlock(&table_lock);
  if(i < 0) {
    note("the table of shared memory is full (%d): %s is not in it", SHM_TABLE_SIZE, name);
  }
}


int __wrap_shm_open(const char* name, int oflag, mode_t mode);
static int shm_name_of(int fd, char name[NAME_BYTES]);

// 2026-11-10: a file opened only to read goes as a copy in shared memory with
// a name of ours (a file has no name a receiver could open). WebKit maps a
// file from the disk and sends its descriptor as shared memory
// (NetworkCache::Data::tryCreateSharedMemory: a service worker's script kept
// on the disk): WhatsApp Web's second visit, its worker's script from the
// disk, hung on "Error sending IPC message". A file opened to write still
// cannot go (a copy would lose the writes). The copy's descriptor closes
// here: its name stays for reap_after_ms after this send, as any other's.
#define FILE_AS_SHM_MAX (64u << 20)
static int
file_as_shm(int file, char name[NAME_BYTES]) {
  struct stat st;
  const int flags = fcntl(file, F_GETFL);
  if(flags < 0 || (flags & O_ACCMODE) != O_RDONLY || fstat(file, &st) || !S_ISREG(st.st_mode) ||
     st.st_size <= 0 || (unsigned long long)st.st_size > FILE_AS_SHM_MAX) {
    return 0;
  }
  const int fd = __wrap_shm_open(SHM_ANON, O_RDWR | O_CLOEXEC, 0600);
  if(fd < 0) {
    return 0;
  }
  int ok = 0;
  if(!ftruncate(fd, st.st_size)) {
    char* to = mmap(0, (size_t)st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(to != MAP_FAILED) {
      off_t at = 0;
      while(at < st.st_size) {
        const ssize_t n = pread(file, to + at, (size_t)(st.st_size - at), at);
        if(n <= 0) {
          break;
        }
        at += n;
      }
      ok = at == st.st_size;
      munmap(to, (size_t)st.st_size);
    }
  }
  ok = ok && shm_name_of(fd, name);
  close(fd);
  note("a file to read (descriptor %d, %lld bytes) goes as a copy: %s", file, (long long)st.st_size,
       ok ? name : "failed");
  COUNT(file_copies, 1);
  return ok;
}


// The name of the object behind fd, if it has one of ours.
static int
shm_name_of(int fd, char name[NAME_BYTES]) {
  struct stat st;
  if(fstat(fd, &st)) {
    return 0;
  }
  pthread_mutex_lock(&table_lock);
  int i = shm_find(st.st_dev, st.st_ino);
  if(i >= 0) {
    shms[i].touched_ms = now_ms();
    memcpy(name, shms[i].name, NAME_BYTES);
  }
  pthread_mutex_unlock(&table_lock);
  return i >= 0;
}


int
__wrap_shm_open(const char* name, int oflag, mode_t mode) {
  if(!on() || !(name == SHM_ANON || !strncmp(name, "/WK2SharedMemory.", 17))) {
    return __real_shm_open(name, oflag, mode);
  }
  for(int tries = 0; tries < 8; tries++) {
    char ours[NAME_BYTES];
    snprintf(ours, sizeof(ours), "/bps5wk-%d-%u", (int)getpid(),
             __atomic_add_fetch(&shm_counter, 1, __ATOMIC_RELAXED));
    // Readable and writable by every process: the browser title (2c-2) runs
    // with other credentials than the payloads, and WebKit's own mode (0600)
    // gave it "Permission denied" on every one (2026-11-05 22:30: each
    // message with memory came without it, and WebKit ended the WebProcess).
    // fchmod: past the umask.
    (void)mode;
    int fd = __real_shm_open(ours, O_CREAT | O_EXCL | O_RDWR | (oflag & O_CLOEXEC), 0666);
    if(fd >= 0) {
      fchmod(fd, 0666);
      shm_remember(fd, ours, 1);
      COUNT(named, 1);
      return fd;
    }
    if(errno != EEXIST) {  // EEXIST: left by an earlier process with our pid
      return -1;
    }
  }
  return -1;
}


// WTF's dupCloseOnExec() on the PlayStation is dup() (UniStdExtrasPlayStation
// .cpp); WebKit duplicates shared memory to send it (SharedMemoryUnix.cpp).
// A title has no dup(): there, an object with a name of ours (made here or
// received) is opened again by that name. The creator keeps the name while
// it has a descriptor on it, and reap_after_ms after its last send.
int
__wrap_dup(int fd) {
  if(!no_dup) {
    return __real_dup(fd);
  }
  char name[NAME_BYTES];
  if(shm_name_of(fd, name)) {
    int copy = __real_shm_open(name, O_RDWR | O_CLOEXEC, 0);
    if(copy >= 0) {
      return copy;
    }
    note("dup of %s: %s", name, strerror(errno));
    return -1;
  }
  int copy = __real_dup(fd);
  if(copy < 0) {
    note("dup of descriptor %d (no name of ours): %s", fd, strerror(errno));
  }
  return copy;
}


int
__wrap_shm_unlink(const char* name) {
  if(on() && name != SHM_ANON && !strncmp(name, "/WK2SharedMemory.", 17)) {
    return 0;  // WebKit's name never was made: ours waits for the reaper
  }
  return __real_shm_unlink(name);
}


typedef struct object_id {
  dev_t dev;
  ino_t ino;
} object_id_t;

// The objects our descriptors are on: a fstat() of each, once a round.
static size_t
open_objects(object_id_t* out, size_t max) {
  size_t n = 0;
  for(int fd = 0; fd < FD_TABLE_SIZE && n < max; fd++) {
    struct stat st;
    if(!fstat(fd, &st) && S_ISREG(st.st_mode)) {
      out[n].dev = st.st_dev;
      out[n].ino = st.st_ino;
      n++;
    }
  }
  return n;
}


static void*
reaper(void* arg) {
  (void)arg;
  uint64_t said_ms = now_ms();
  uint64_t said_frames = 0;
  for(;;) {
    usleep(reap_every_ms * 1000);
    uint64_t now = now_ms();
    // The counters in the log every 30 s while frames go (for the tests).
    uint64_t frames = stats.frames_sent + stats.frames_received;
    if(now - said_ms >= STATS_EVERY_MS && frames != said_frames) {
      ipc_transport_stats_t s;
      ipc_transport_stats(&s);
      note("process %d: pairs %llu, met %llu, sent %llu (%llu queued), received %llu, "
           "attachments %llu (%llu lost, %llu refused), named %llu, unlinked %llu",
           (int)getpid(), (unsigned long long)s.pairs, (unsigned long long)s.met,
           (unsigned long long)s.frames_sent, (unsigned long long)s.frames_queued,
           (unsigned long long)s.frames_received, (unsigned long long)s.attachments_sent,
           (unsigned long long)s.attachments_lost, (unsigned long long)s.refused,
           (unsigned long long)s.named, (unsigned long long)s.unlinked);
      said_ms = now;
      said_frames = frames;
    }
    // Ours stay reap_after_ms after their last send (a receiver may still
    // be on its way to the name); another process's only while a descriptor
    // here is on them, 2 s at most after that (2026-11-11: they filled the
    // table).
    reap_round(reap_after_ms, reap_after_ms < 2000 ? reap_after_ms : 2000);
  }
  return 0;
}


// Whether an entry's last use is old enough for a round that began at now.
// 2026-11-17: one used after the round took its time (made or sent while the
// round looked at the descriptors, which it then did not see) was newer than
// now, and now - touched came out as a huge unsigned number: its name went at
// once, with a descriptor still on it, before the receiver opened it ("shared
// memory /bps5wk-...: No such file or directory" in 10 console runs of 399; a
// message that came without its memory; ipc_reap_race_test.c).
static int
old_enough(const shm_entry_t* e, uint64_t now, uint64_t ours_ms, uint64_t others_ms) {
  return e->used && e->touched_ms <= now && now - e->touched_ms >= (e->ours ? ours_ms : others_ms);
}


// The entries no descriptor of this process is on any more go: ours (made
// here, unlinked here) ours_ms after their last use, the others others_ms
// after theirs.
static void
reap_round(uint64_t ours_ms, uint64_t others_ms) {
  static object_id_t open[FD_TABLE_SIZE];
  static pthread_mutex_t round_lock = PTHREAD_MUTEX_INITIALIZER;
  const uint64_t now = now_ms();
  int due = 0;

  pthread_mutex_lock(&round_lock);  // the reaper's thread, or whoever found the table full
  pthread_mutex_lock(&table_lock);
  for(int i = 0; i < SHM_TABLE_SIZE && !due; i++) {
    due = old_enough(&shms[i], now, ours_ms, others_ms);
  }
  pthread_mutex_unlock(&table_lock);
  if(!due) {
    pthread_mutex_unlock(&round_lock);
    return;
  }
  size_t n = open_objects(open, FD_TABLE_SIZE);
  for(int i = 0; i < SHM_TABLE_SIZE; i++) {
    pthread_mutex_lock(&table_lock);
    shm_entry_t e = shms[i];
    pthread_mutex_unlock(&table_lock);
    if(!old_enough(&e, now, ours_ms, others_ms)) {
      continue;
    }
    int still = 0;
    for(size_t k = 0; k < n && !still; k++) {
      still = open[k].dev == e.dev && open[k].ino == e.ino;
    }
    if(still) {
      continue;
    }
    pthread_mutex_lock(&table_lock);
    int same = shms[i].used && shms[i].dev == e.dev && shms[i].ino == e.ino
               && shms[i].touched_ms == e.touched_ms;
    if(same) {
      shms[i].used = 0;
    }
    pthread_mutex_unlock(&table_lock);
    if(same && e.ours) {
      __real_shm_unlink(e.name);
      COUNT(unlinked, 1);
      // Off the list after the unlink (a process killed in between
      // leaves only a name that is gone), unless the slot was taken.
      pthread_mutex_lock(&table_lock);
      if(!shms[i].used) {
        list_set(i);
      }
      pthread_mutex_unlock(&table_lock);
    }
  }
  pthread_mutex_unlock(&round_lock);
}


// ---------------------------------------------------------------------------
// Messages.

ssize_t
__wrap_sendmsg(int fd, const struct msghdr* msg, int flags) {
  if(!on()) {
    return __real_sendmsg(fd, msg, flags);
  }
  int state = ready(fd, 0);
  static unsigned traced;  // a title's first calls (as recvmsg's)
  if(no_dup && __atomic_add_fetch(&traced, 1, __ATOMIC_RELAXED) <= 20) {
    note("sendmsg on descriptor %d: state %d", fd, state);
  }
  if(state == FD_NONE) {
    return __real_sendmsg(fd, msg, flags);
  }
  if(state < 0) {
    return -1;
  }

  // The attachments: names and meetings.
  size_t count = 0;
  for(struct cmsghdr* c = msg->msg_controllen ? CMSG_FIRSTHDR((struct msghdr*)msg) : 0; c;
      c = CMSG_NXTHDR((struct msghdr*)msg, c)) {
    if(c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS) {
      count += (c->cmsg_len - CMSG_LEN(0)) / sizeof(int);
    }
  }
  size_t body = 0;
  for(size_t i = 0; i < (size_t)msg->msg_iovlen; i++) {
    body += msg->msg_iov[i].iov_len;
  }
  if(count > ATTACH_MAX || body > FRAME_BODY_MAX) {
    errno = EMSGSIZE;
    return -1;
  }
  size_t size = sizeof(frame_header_t) + count * sizeof(attachment_t) + body;
  pending_t* p = malloc(sizeof(pending_t) + size);
  if(!p) {
    errno = ENOMEM;
    return -1;
  }
  p->next = 0;
  p->size = size;
  frame_header_t h = {FRAME_MAGIC, (uint32_t)body, (uint32_t)count, 0};
  memcpy(p->frame, &h, sizeof(h));
  attachment_t* list = (attachment_t*)(p->frame + sizeof(h));
  size_t k = 0;
  for(struct cmsghdr* c = msg->msg_controllen ? CMSG_FIRSTHDR((struct msghdr*)msg) : 0; c;
      c = CMSG_NXTHDR((struct msghdr*)msg, c)) {
    if(c->cmsg_level != SOL_SOCKET || c->cmsg_type != SCM_RIGHTS) {
      continue;
    }
    size_t n = (c->cmsg_len - CMSG_LEN(0)) / sizeof(int);
    const int* sent = (const int*)CMSG_DATA(c);
    for(size_t j = 0; j < n; j++, k++) {
      attachment_t* a = &list[k];
      memset(a, 0, sizeof(*a));
      uint16_t port;
      uint64_t secret;
      if(shm_name_of(sent[j], a->name)) {
        a->kind = ATTACH_SHM;
      } else if(placeholder_peek(sent[j], &port, &secret)) {
        a->kind = ATTACH_MEET;
        a->port = port;
        a->secret = secret;
      } else if(file_as_shm(sent[j], a->name)) {  // a file to read: a copy goes
        a->kind = ATTACH_SHM;
      } else {  // a listening end, a connected one, a file to write: none can go
        // 2026-11-11: what it is (a page waited for good behind one of these).
        struct stat what;
        memset(&what, 0, sizeof(what));
        const int stat_err = fstat(sent[j], &what) ? errno : 0;
        note("an attachment that cannot go by name (descriptor %d, ours: %d): mode %o, %lld bytes, "
             "flags %#x, fstat %d, object %llx:%llx", sent[j], fd_state(sent[j]), (unsigned)what.st_mode,
             (long long)what.st_size, (unsigned)fcntl(sent[j], F_GETFL), stat_err,
             (unsigned long long)what.st_dev, (unsigned long long)what.st_ino);
        COUNT(refused, 1);
        free(p);
        errno = EINVAL;
        return -1;
      }
    }
  }
  char* at = p->frame + sizeof(h) + count * sizeof(attachment_t);
  for(size_t i = 0; i < (size_t)msg->msg_iovlen; i++) {
    memcpy(at, msg->msg_iov[i].iov_base, msg->msg_iov[i].iov_len);
    at += msg->msg_iov[i].iov_len;
  }

  fd_entry_t* e = &fds[fd];
  int err = 0;
  pthread_mutex_lock(&e->send_lock);
  if(fd_state(fd) == FD_LISTENING) {  // the other end has not come yet
    if(e->tail) {
      e->tail->next = p;
    } else {
      e->head = p;
    }
    e->tail = p;
    p = 0;
    COUNT(frames_queued, 1);
  } else {
    err = write_all(fd, p->frame, p->size);
  }
  pthread_mutex_unlock(&e->send_lock);
  free(p);
  if(err) {
    return -1;
  }
  COUNT(frames_sent, 1);
  COUNT(attachments_sent, count);
  return (ssize_t)body;
}


// One attachment back as a descriptor.
static int
attachment_open(const attachment_t* a) {
  if(a->kind == ATTACH_SHM) {
    char name[NAME_BYTES];
    memcpy(name, a->name, sizeof(name));
    name[sizeof(name) - 1] = 0;
    int fd = __real_shm_open(name, O_RDWR, 0);
    if(fd < 0) {
      note("shared memory %s: %s", name, strerror(errno));
      return -1;
    }
    shm_remember(fd, name, 0);  // so it can be sent on
    return fd;
  }
  if(a->kind == ATTACH_MEET) {
    if(no_dup) {
      return connect_now(a->port, a->secret);
    }
    char line[RV_LINE_MAX];
    snprintf(line, sizeof(line), RV_MAGIC "%u %llx\n", a->port, (unsigned long long)a->secret);
    return placeholder(line);
  }
  errno = EINVAL;
  return -1;
}


ssize_t
__wrap_recvmsg(int fd, struct msghdr* msg, int flags) {
  if(!on()) {
    return __real_recvmsg(fd, msg, flags);
  }
  int state = ready(fd, 1);
  // A title's first calls, in the log (2c-2: its WebProcess got nothing,
  // 2026-11-05 20:00).
  static unsigned traced;
  if(no_dup && __atomic_add_fetch(&traced, 1, __ATOMIC_RELAXED) <= 20) {
    note("recvmsg on descriptor %d: state %d", fd, state);
  }
  if(state == FD_NONE) {
    return __real_recvmsg(fd, msg, flags);
  }
  if(state != FD_CONNECTED) {
    return -1;  // EAGAIN: nobody has come to the listening end yet
  }

  // Nothing yet: as the socket would say. Once a frame has started, the
  // rest of it is on its way.
  int blocking = !(fcntl(fd, F_GETFL) & O_NONBLOCK) && !(flags & MSG_DONTWAIT);
  frame_header_t h;
  ssize_t n;
  do {
    n = recv(fd, &h, sizeof(h), MSG_PEEK | (blocking ? 0 : MSG_DONTWAIT));
  } while(n < 0 && errno == EINTR);
  if(n <= 0) {
    if(!n) {
      // As the socket: no control data. WebKit's readBytesFromSocket()
      // walks the control messages even when it read 0 bytes; the length
      // it set (its whole buffer) made it read stale bytes as descriptors
      // (2026-11-05 17:55, ipc_transport_test at -O2).
      msg->msg_controllen = 0;
      msg->msg_flags = 0;
    }
    return n;  // 0: the other end closed; -1, EAGAIN: nothing yet
  }
  if(read_all(fd, &h, sizeof(h))) {
    return -1;
  }
  if(h.magic != FRAME_MAGIC || h.count > ATTACH_MAX || h.body > FRAME_BODY_MAX) {
    note("descriptor %d: not a frame (magic %08x, %u attachments, %u bytes)", fd, h.magic,
         h.count, h.body);
    errno = EPROTO;
    return -1;
  }
  size_t list_size = h.count * sizeof(attachment_t);
  char* data = malloc(list_size + h.body + 1);
  if(!data) {
    errno = ENOMEM;
    return -1;
  }
  if(read_all(fd, data, list_size + h.body)) {
    free(data);
    return -1;
  }
  const attachment_t* list = (const attachment_t*)data;
  const char* body = data + list_size;

  size_t room = 0;
  for(size_t i = 0; i < (size_t)msg->msg_iovlen; i++) {
    room += msg->msg_iov[i].iov_len;
  }
  size_t left = h.body < room ? h.body : room;
  size_t given = left;
  for(size_t i = 0; left && i < (size_t)msg->msg_iovlen; i++) {
    size_t piece = msg->msg_iov[i].iov_len < left ? msg->msg_iov[i].iov_len : left;
    memcpy(msg->msg_iov[i].iov_base, body, piece);
    body += piece;
    left -= piece;
  }
  msg->msg_flags = h.body > room ? MSG_TRUNC : 0;

  size_t needed = h.count ? CMSG_SPACE(h.count * sizeof(int)) : 0;
  if(h.count && needed > msg->msg_controllen) {
    msg->msg_flags |= MSG_CTRUNC;
    msg->msg_controllen = 0;
    note("descriptor %d: %u attachments and no room for them", fd, h.count);
  } else if(h.count) {
    struct cmsghdr* c = CMSG_FIRSTHDR(msg);
    c->cmsg_level = SOL_SOCKET;
    c->cmsg_type = SCM_RIGHTS;
    c->cmsg_len = CMSG_LEN(h.count * sizeof(int));
    int* out = (int*)CMSG_DATA(c);
    for(uint32_t k = 0; k < h.count; k++) {
      out[k] = attachment_open(&list[k]);
      if(out[k] < 0) {  // the message comes all the same: its decoder says no
        COUNT(attachments_lost, 1);
      }
    }
    msg->msg_controllen = needed;
  } else {
    msg->msg_controllen = 0;
  }
  free(data);
  COUNT(frames_received, 1);
  return (ssize_t)given;
}


void
ipc_transport_stats(ipc_transport_stats_t* out) {
  pthread_mutex_lock(&table_lock);
  *out = stats;
  pthread_mutex_unlock(&table_lock);
}


int
ipc_transport_keeps(int fd) {
  return no_dup && fd >= 0 && fd < FD_TABLE_SIZE &&
         __atomic_load_n(&fds[fd].state, __ATOMIC_ACQUIRE) != FD_NONE;
}


void
ipc_transport_keep_nonblock(int fd, int on) {
  if(fd >= 0 && fd < FD_TABLE_SIZE) {
    __atomic_store_n(&kept_nonblock[fd], on ? 1 : 0, __ATOMIC_RELEASE);
  }
}


int
ipc_transport_kept_nonblock(int fd) {
  return fd >= 0 && fd < FD_TABLE_SIZE && __atomic_load_n(&kept_nonblock[fd], __ATOMIC_ACQUIRE);
}


int
ipc_transport_describe(int fd, char* out, size_t size) {
  if(fd < 0 || fd >= FD_TABLE_SIZE) {
    return snprintf(out, size, "descriptor %d: past the table", fd);
  }
  pthread_mutex_lock(&table_lock);
  int state = fds[fd].state;
  unsigned port = fds[fd].port, peer = fds[fd].peer;
  pthread_mutex_unlock(&table_lock);
  return snprintf(out, size, "descriptor %d: state %d (ports %u to %u), now ports %u to %u", fd,
                  state, port, peer, (unsigned)local_port(fd), (unsigned)peer_port(fd));
}
