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

// BrowserPS5's installer (2026-11-14): one ELF for any payload sender, the
// way XPS5, the author's other PS5 app, is installed.
//
// It carries the whole browser packed with xz (build-release-rn.sh makes the
// pack from the same tars the tests install): the title, in
// /data/homebrew/PPSA97024, and the engine's library, its processes and the
// PDF reader, in /user/homebrew. A thread here unpacks it into a pipe, and
// install_tar's own code (../install_tar/main.c) reads the tar from that
// pipe as it reads one from the PC: only files under /user/homebrew/ and
// /data/homebrew/, each written beside its place and then renamed.
//
// What the browser keeps of its user (/user/homebrew/browserps5-rn/data:
// favorites, history, settings, logins) is not in the pack and is never
// touched: installing over a version keeps it all.
//
// With the browser open nothing is written: the installer says so with a
// PS5 notification and ends. It closes no app, the browser's included.
// (Open or not: the browser listens on 127.0.0.1:9040 for as long as it
// runs. sceLncUtilGetAppId answers a payload with an error on FW 13.60,
// 0x80940004: the first run of this installer, 2026-11-14.)
//
// The title is not put on the home screen here: ShadowMountPlus does that,
// about 15 s after the title's folder appears (Games tab).

#define POC_NAME "BrowserPS5"

#include <pthread.h>
#include <signal.h>

#include <lzma.h>

#define TITLE_ID     "PPSA97024"
#define APPMETA_DIR  "/user/appmeta/" TITLE_ID
#define RELEASE_FILE "/user/homebrew/browserps5-rn/release.txt"

// What other homebrew on the console shares with the browser (Mesa's libraries in /user/homebrew/lib, the
// certificates in /user/homebrew/etc) is written only where there is none: a copy that is there, another
// app's or the user's own build, stays as it is. The browser's own files are always written.
static int keep_shared(const char* path);
#define INSTALL_TAR_KEEP(path) keep_shared(path)

#define main install_tar_main
#include "../install_tar/main.c"
#undef main

#include "release.h"  // RELEASE_VERSION, RELEASE_PACK, RELEASE_PACK_SHA256 (build-release-rn.sh)

#include <arpa/inet.h>
#include <netinet/in.h>

#include <sys/socket.h>
#include <sys/time.h>

#define BROWSER_PORT 9040  // the browser's own, on this console only (poc/common/title_link.h)


/**
 * Is the browser running? Something answers on its port.
 **/
static int
browser_open(void) {
  struct timeval tv = {2, 0};
  struct sockaddr_in a;
  int s, open;

  if((s=socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    return 0;
  }
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
  memset(&a, 0, sizeof(a));
  a.sin_family = AF_INET;
  a.sin_port = htons(BROWSER_PORT);
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  open = !connect(s, (struct sockaddr*)&a, sizeof(a));
  close(s);
  return open;
}


static int
keep_shared(const char* path) {
  static const char* const own[] = {
    "/data/homebrew/" TITLE_ID "/", "/user/homebrew/browserps5-rn/", "/user/homebrew/lib/libWebKitFull.so.",
  };

  for(size_t i=0; i < sizeof(own) / sizeof(own[0]); i++) {
    if(!strncmp(path, own[i], strlen(own[i]))) {
      return 0;
    }
  }
  return !access(path, F_OK);
}

// The pack, as the file build-release-rn.sh names.
__asm__(".section .rodata\n"
        ".p2align 4\n"
        ".globl release_pack\n"
        ".hidden release_pack\n"
        "release_pack:\n"
        ".incbin \"" RELEASE_PACK "\"\n"
        ".globl release_pack_end\n"
        ".hidden release_pack_end\n"
        "release_pack_end:\n"
        ".previous\n");
extern const unsigned char release_pack[] __asm__("release_pack");
extern const unsigned char release_pack_end[] __asm__("release_pack_end");

static volatile int unpack_error;  // lzma's code, or -1: the pipe
static volatile int unpack_stop;   // the reader gave up: no more is unpacked


/**
 * Below the console's other threads (a payload's are FIFO 700, and a bigger
 * number is a lower priority): unpacking takes a core for some seconds.
 **/
static void
step_down(void) {
  struct sched_param param;
  int policy;

  if(!pthread_getschedparam(pthread_self(), &policy, &param)) {
    param.sched_priority = 760;
    pthread_setschedparam(pthread_self(), policy, &param);
  }
}


/**
 * The pack, unpacked a MiB at a time into the pipe install_tar reads.
 **/
static void*
unpack_thread(void* arg) {
  static unsigned char block[1 << 20];
  const int out = (int)(intptr_t)arg;
  lzma_stream s = LZMA_STREAM_INIT;
  lzma_ret ret;

  step_down();
  if(lzma_stream_decoder(&s, 96 << 20, 0) != LZMA_OK) {
    unpack_error = 100;
    close(out);
    return 0;
  }
  s.next_in = release_pack;
  s.avail_in = (size_t)(release_pack_end - release_pack);
  do {
    s.next_out = block;
    s.avail_out = sizeof(block);
    ret = lzma_code(&s, LZMA_FINISH);
    const size_t n = sizeof(block) - s.avail_out;
    for(size_t done=0; done < n; ) {
      ssize_t w = write(out, block + done, n - done);

      if(w <= 0) {  // the reader gave up (it said why)
        unpack_error = -1;
        ret = LZMA_PROG_ERROR;
        break;
      }
      done += (size_t)w;
    }
    usleep(300);  // never a core to itself
  } while(ret == LZMA_OK && !unpack_stop);
  if(ret != LZMA_STREAM_END && !unpack_error && !unpack_stop) {
    unpack_error = (int)ret;
  }
  lzma_end(&s);
  close(out);
  return 0;
}


int
main(void) {
  static const char stamp[] = RELEASE_VERSION "\n" RELEASE_PACK_SHA256 "\n";
  pthread_t thread;
  struct stat st;
  int fds[2];
  int failed, fd;

  if(browser_open()) {
    POC_NOTIFY("is open: nothing was installed. Close it (Options, Close application) and send the installer again.");
    return 1;
  }
  signal(SIGPIPE, SIG_IGN);
  step_down();
  POC_NOTIFY(RELEASE_VERSION ": installing, about half a minute. Do not open it yet.");
  if(pipe(fds) || pthread_create(&thread, 0, unpack_thread, (void*)(intptr_t)fds[1])) {
    POC_NOTIFY("was NOT installed: the installer could not start (errno %d).", errno);
    return 1;
  }
  dup2(fds[0], 0);  // install_tar reads its tar on stdin
  close(fds[0]);
  failed = install_tar_main();
  // The pipe is read to its end before it is closed: the tar's last blocks (install_tar stops at the first
  // empty one) were still being written when it was closed at once, and the installer ended there without
  // a word, with everything written (2026-11-14, the first run: a write to a pipe nobody reads).
  if(failed) {
    unpack_stop = 1;
  }
  {
    static char rest[1 << 16];
    while(read(0, rest, sizeof(rest)) > 0) {
    }
  }
  pthread_join(thread, 0);
  close(0);
  if(failed || unpack_error) {
    POC_NOTIFY("was NOT installed (unpacking: %d). Send the installer again; what was there before may be incomplete.",
               unpack_error);
    return 1;
  }
  if((fd=open(RELEASE_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644)) >= 0) {  // which release is there (the browser's About)
    if(write(fd, stamp, sizeof(stamp) - 1) != (ssize_t)(sizeof(stamp) - 1)) {
      POC_PERROR(RELEASE_FILE);
    }
    close(fd);
  }
  if(!stat(APPMETA_DIR, &st)) {
    POC_NOTIFY(RELEASE_VERSION " is installed. Open it from Games on the home screen.");
  } else {
    POC_NOTIFY(RELEASE_VERSION " is installed. ShadowMountPlus adds it to Games on the home screen in about 15 s.");
  }
  return 0;
}
