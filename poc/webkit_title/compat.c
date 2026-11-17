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

// The libc functions WebKit and its libraries call that a title has no
// import for: in payloads they come from the payload SDK's libc.a, which a
// title does not link (it would export libc's names, and the title
// converter refuses that). build-title.sh binds each name to the bps5_
// function here with --defsym, as PS5_Vulkan's radv-link.sh does for its
// platform layer. Each is the plainest version that does the job.

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <pthread_np.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

int ps5____mb_cur_max_l(void* locale);


// thr_self(2): the kernel's ID for the calling thread.
int
bps5_thr_self(long* id) {
  *id = pthread_getthreadid_np();
  return 0;
}


// pipe2(2): pipe() and then the flags.
int
bps5_pipe2(int fds[2], int flags) {
  if(pipe(fds)) {
    return -1;
  }
  for(int i = 0; i < 2; i++) {
    if((flags & O_CLOEXEC) && fcntl(fds[i], F_SETFD, FD_CLOEXEC) < 0) {
      goto fail;
    }
    if((flags & O_NONBLOCK) && fcntl(fds[i], F_SETFL, fcntl(fds[i], F_GETFL) | O_NONBLOCK) < 0) {
      goto fail;
    }
  }
  return 0;
fail: {
    int err = errno;
    close(fds[0]);
    close(fds[1]);
    errno = err;
    return -1;
  }
}


// ppoll(2) as poll(2): the signal mask is not changed (GLib's main loop
// and GStreamer pass none).
int
bps5_ppoll(struct pollfd* fds, nfds_t count, const struct timespec* timeout, const void* sigmask) {
  (void)sigmask;
  int ms = -1;
  if(timeout) {
    long long total = (long long)timeout->tv_sec * 1000 + (timeout->tv_nsec + 999999) / 1000000;
    ms = total > 0x7fffffff ? 0x7fffffff : (int)total;
  }
  return poll(fds, count, ms);
}


// mkostemp(3): the template's Xs replaced until a new file opens.
int
bps5_mkostemp(char* path, int flags) {
  static const char letters[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  size_t length = strlen(path);
  size_t xs = 0;
  while(xs < length && path[length - 1 - xs] == 'X') {
    xs++;
  }
  if(xs < 6) {
    errno = EINVAL;
    return -1;
  }
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  unsigned long long state = (unsigned long long)now.tv_nsec ^ ((unsigned long long)getpid() << 32);
  for(int attempt = 0; attempt < 1000; attempt++) {
    for(size_t i = length - xs; i < length; i++) {
      state = state * 6364136223846793005ull + 1442695040888963407ull;
      path[i] = letters[(state >> 33) % (sizeof(letters) - 1)];
    }
    int fd = open(path, O_RDWR | O_CREAT | O_EXCL | (flags & (O_CLOEXEC | O_APPEND | O_SYNC)), 0600);
    if(fd >= 0 || errno != EEXIST) {
      return fd;
    }
  }
  errno = EEXIST;
  return -1;
}


// No user database on the console: GLib then uses $HOME.
void*
bps5_getpwuid(unsigned uid) {
  (void)uid;
  errno = ENOENT;
  return 0;
}


int
bps5_getpwnam_r(const char* name, void* entry, char* buffer, size_t size, void** result) {
  (void)name;
  (void)entry;
  (void)buffer;
  (void)size;
  *result = 0;
  return 0;
}


// A thread's CPU clock: not offered here (only the load probe's reports
// use it, and they say so when it fails).
int
bps5_clock_getcpuclockid2(long long id, int which, void* clock) {
  (void)id;
  (void)which;
  (void)clock;
  return ENOSYS;
}


// The C locale is the only one: the thread keeps it.
void*
bps5_uselocale(void* locale) {
  (void)locale;
  return (void*)-1;  // LC_GLOBAL_LOCALE
}


int
bps5____mb_cur_max(void) {
  return ps5____mb_cur_max_l((void*)-1);
}


// LeakSanitizer's hook, which WTF references weakly: there is no sanitizer
// here, and the title converter wants every import to come from a module.
void
__lsan_ignore_object(const void* object) {
  (void)object;
}


// The title's imports that only libkernel_sys exports: a title gets
// libkernel, so they were NULL there (2026-11-06 11:47, 2c2l: fontconfig's
// FcFStatFs called fstatfs at pc 0 on the WebProcess's first font). Found by
// comparing the title's undefined dynamic symbols with the SDK's stubs of
// libkernel_sys and of the modules a title has (docs/05). Each answers as a
// file system without the feature would, which their callers expect.

// fstatfs(2): no file system details. fontconfig then takes the directory's
// times as good and mmap as safe, as on any ordinary file system.
int
bps5_fstatfs(int fd, void* buf) {
  (void)fd;
  (void)buf;
  errno = ENOSYS;
  return -1;
}


// fchown(2): one user on the console, so the owner is already the right one
// (SQLite's journals ask only when running as root).
int
bps5_fchown(int fd, uid_t owner, gid_t group) {
  (void)fd;
  (void)owner;
  (void)group;
  return 0;
}


// link(2) and symlink(2): no links here, as on FAT; WTF's hardLinkOrCopyFile
// copies instead.
int
bps5_link(const char* from, const char* to) {
  (void)from;
  (void)to;
  errno = EPERM;
  return -1;
}


int
bps5_symlink(const char* from, const char* to) {
  (void)from;
  (void)to;
  errno = EPERM;
  return -1;
}


// readlink(2): libc-fixes' wrapper only calls it for a path that lstat()
// says is a link, which the title's own files never are.
ssize_t
bps5_readlink(const char* path, char* buf, size_t size) {
  (void)path;
  (void)buf;
  (void)size;
  errno = ENOSYS;
  return -1;
}


// timegm(3): seconds since 1970 for a UTC date, without the time zone
// (days_from_civil, H. Hinnant's public-domain algorithm).
time_t
bps5_timegm(struct tm* tm) {
  long long year = tm->tm_year + 1900LL;
  long long month = tm->tm_mon;  // 0-11; normalized below
  year += month / 12;
  month %= 12;
  if(month < 0) {
    month += 12;
    year--;
  }
  month += 1;
  year -= month <= 2;
  long long era = (year >= 0 ? year : year - 399) / 400;
  long long yoe = year - era * 400;
  long long doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + tm->tm_mday - 1;
  long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  long long days = era * 146097 + doe - 719468;
  return (time_t)(days * 86400 + tm->tm_hour * 3600LL + tm->tm_min * 60LL + tm->tm_sec);
}
