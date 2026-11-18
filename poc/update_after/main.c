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

// update_after: BrowserPS5 RN installs its own update (2026-11-17; browser/update.h says how one is found).
//
// The installer of a release writes nothing while the browser is open, and closes no app (release-rn/install.c).
// So the browser, with a newer release's installer fetched and checked, leaves as "Sair" does, and this small
// payload, which it sends to the console's payload loader (127.0.0.1:9021) just before, does the rest from the
// home screen, as play_after does for a game:
//   - reads the request (update_after.h) and removes it, so it is used once, and only while fresh;
//   - waits until both processes of the browser are gone (30 s at most), then until the system has no big app
//     and the browser's title state is old, for 2 s in a row (30 s at most); otherwise nothing is installed, and
//     the installer stays for the next time;
//   - reads the installer again and checks its size and its SHA-256 against the request's (the file may have
//     waited on the disk since another session);
//   - hands it to the payload loader, as anyone sends it by hand: the installer does what it always does, its
//     notifications included;
//   - waits until the installed release (release.txt) is the new one (4 min at most), removes the installer,
//     and opens the browser again, unless an app is in front by then.
// Whatever stops it on the way (the browser not gone, an app in front, the loader, the installer), the browser has
// counted this try before it left (UPDATE_TRIES, removed here once the release is installed): after two for the
// same release it does not leave for it again.
// All of it goes to UPDATE_LOG; nothing is said on the screen here. It sleeps between looks: no spinning (payload
// threads are FIFO 700).

#define POC_NAME "update_after"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>

#include "poc_log.h"
#include "link.h"
#include "sha256.h"
#include "update_after.h"

typedef struct app_launch_ctx {
  uint32_t structsize;
  uint32_t user_id;
  uint32_t app_opt;
  uint64_t crash_report;
  uint32_t check_flag;
} app_launch_ctx_t;

int sceUserServiceInitialize(void*);
int sceUserServiceGetForegroundUser(uint32_t* user_id);
int sceSystemServiceGetAppIdOfRunningBigApp(void);
int sceSystemServiceLaunchApp(const char* title_id, char** argv, app_launch_ctx_t* ctx);

#define WAIT_MS     30000   // for the browser to be gone, and then for the system to have no app in front
#define QUIET_MS    2000    // no app in front, in a row
#define INSTALL_MS  240000  // for the installer to have written the new release
#define LOOK_US     (200 * 1000)
#define LOADER_PORT 9021

static int log_fd = -1;
static double start_ms;


static void
say(const char* fmt, ...) {
  char line[400];
  va_list args;

  int n = snprintf(line, sizeof(line), "%6.0f ms: ", poc_now_ms() - start_ms);
  va_start(args, fmt);
  vsnprintf(line + n, sizeof(line) - (size_t)n - 1, fmt, args);
  va_end(args);
  size_t len = strlen(line);
  line[len++] = '\n';
  POC_LOG("%.*s", (int)len - 1, line);
  if(log_fd >= 0 && write(log_fd, line, len) > 0) {
    fsync(log_fd);
  }
}


static int
alive(int pid) {
  return pid > 1 && (!kill(pid, 0) || errno == EPERM);
}


// The browser's title state (link.h) written less than LINK_TITLE_FRESH_MS ago: as launch_title's.
static int
browser_title_open(void) {
  int open = 0;
  int fd = shm_open(LINK_TITLE_SHM, O_RDONLY, 0);

  if(fd < 0) {
    return 0;
  }
  void* memory = mmap(0, LINK_TITLE_BYTES, PROT_READ, MAP_SHARED, fd, 0);
  close(fd);
  if(memory == MAP_FAILED) {
    return 0;
  }
  const link_title_t* title = (const link_title_t*)memory;
  const double age = poc_now_ms() - (double)__atomic_load_n(&title->heartbeat_ms, __ATOMIC_ACQUIRE);
  if(__atomic_load_n(&title->magic, __ATOMIC_ACQUIRE) == LINK_MAGIC) {
    open = age < LINK_TITLE_FRESH_MS;
  }
  munmap(memory, LINK_TITLE_BYTES);
  return open;
}


// A version as update.h has them: digits and dots, maybe "-rc" and digits.
static int
version_ok(const char* v) {
  const size_t n = strlen(v);
  if(n < 1 || n > 30 || v[0] < '0' || v[0] > '9') {
    return 0;
  }
  for(size_t i = 0; i < n; i++) {
    const char c = v[i];
    if(!((c >= '0' && c <= '9') || c == '.' || c == '-' || c == 'r' || c == 'c')) {
      return 0;
    }
  }
  return 1;
}


// Is the installed release this version?
static int
installed(const char* version) {
  char line[64] = "";
  FILE* f = fopen(UPDATE_RELEASE, "r");

  if(!f) {
    return 0;
  }
  if(fgets(line, sizeof(line), f)) {
    line[strcspn(line, "\r\n")] = 0;
  }
  fclose(f);
  return !strcmp(line, version);
}


// The installer: its size and SHA-256 as the request says them? Then to the payload loader, all of it.
static int
hand_over(unsigned long long size, const char* sha_hex) {
  static unsigned char block[1 << 20];
  unsigned char digest[32];
  char hex[65];
  struct stat st;
  sha256_t sha;
  ssize_t n;
  int fd = open(UPDATE_ELF, O_RDONLY);

  if(fd < 0 || fstat(fd, &st) || (unsigned long long)st.st_size != size) {
    say("the installer is not there, or not of %llu bytes: nothing handed over", size);
    if(fd >= 0) {
      close(fd);
    }
    return -1;
  }
  sha256_begin(&sha);
  while((n=read(fd, block, sizeof(block))) > 0) {
    sha256_add(&sha, block, (size_t)n);
  }
  sha256_end(&sha, digest);
  for(int i = 0; i < 32; i++) {
    snprintf(hex + 2 * i, 3, "%02x", digest[i]);
  }
  if(n < 0 || strcmp(hex, sha_hex)) {
    say("the installer's SHA-256 is %s, not the release's: nothing handed over", hex);
    close(fd);
    return -1;
  }
  say("the installer is whole (%llu bytes, its SHA-256 the release's)", size);

  struct sockaddr_in to;
  struct timeval wait = {10, 0};
  const int s = socket(AF_INET, SOCK_STREAM, 0);
  int ok = s >= 0;
  unsigned long long sent = 0;

  memset(&to, 0, sizeof(to));
  to.sin_family = AF_INET;
  to.sin_port = htons(LOADER_PORT);
  to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if(ok) {
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &wait, sizeof(wait));
    ok = !connect(s, (struct sockaddr*)&to, sizeof(to));
  }
  if(ok) {
    ok = lseek(fd, 0, SEEK_SET) == 0;
  }
  while(ok && (n=read(fd, block, sizeof(block))) > 0) {
    for(ssize_t done = 0; ok && done < n;) {
      const ssize_t w = send(s, block + done, (size_t)(n - done), 0);
      ok = w > 0;
      done += ok ? w : 0;
    }
    sent += ok ? (unsigned long long)n : 0;
  }
  close(fd);
  if(s >= 0) {
    close(s);
  }
  if(!ok || sent != size) {
    say("the payload loader on port %d took %llu of %llu bytes (%s): not installed", LOADER_PORT, sent, size, strerror(errno));
    return -1;
  }
  say("the payload loader has the installer (%llu bytes)", sent);
  return 0;
}


int
main(void) {
  char line[200] = {0};
  char version[40] = {0};
  char sha[80] = {0};
  unsigned long long size = 0;
  int ui = 0, launcher = 0, app = 0;
  struct stat st;

  start_ms = poc_now_ms();
  log_fd = open(UPDATE_LOG, O_WRONLY | O_CREAT | O_TRUNC, 0644);

  FILE* f = fopen(UPDATE_ASK, "r");
  if(!f) {
    say("no request (%s): nothing to install", UPDATE_ASK);
    return 1;
  }
  const time_t now = time(0);
  const int fresh = !fstat(fileno(f), &st) && now - st.st_mtime < 120 && st.st_mtime - now < 5;
  const int got = fgets(line, sizeof(line), f) ? sscanf(line, "%39s %llu %79s %d %d", version, &size, sha, &ui, &launcher) : 0;
  fclose(f);
  unlink(UPDATE_ASK);
  line[strcspn(line, "\r\n")] = 0;
  if(got < 5 || !fresh || !version_ok(version) || strlen(sha) != 64 || !size) {
    say("a request that is %s (\"%s\"): nothing installed", fresh ? "not right" : "old", line);
    return 1;
  }
  say("release %s is to be installed; the browser's app %d and launcher %d", version, ui, launcher);

  while((alive(ui) || alive(launcher)) && poc_now_ms() - start_ms < WAIT_MS) {
    usleep(LOOK_US);
  }
  if(alive(ui) || alive(launcher)) {
    say("the browser is still there after %d s: nothing installed (the installer stays for the next time)", WAIT_MS / 1000);
    return 1;
  }
  say("the browser's processes are gone");

  const double from = poc_now_ms();
  double quiet = 0;
  while(poc_now_ms() - from < WAIT_MS) {
    app = sceSystemServiceGetAppIdOfRunningBigApp();
    if(app > 0 || browser_title_open()) {
      quiet = 0;
    } else if(!quiet) {
      quiet = poc_now_ms();
    } else if(poc_now_ms() - quiet >= QUIET_MS) {
      break;
    }
    usleep(LOOK_US);
  }
  if(!quiet || poc_now_ms() - quiet < QUIET_MS) {
    say("the system still has an app in front (0x%x) after %d s: nothing installed (the installer stays)", (unsigned)app,
        WAIT_MS / 1000);
    return 1;
  }
  say("no app in front for %d ms", QUIET_MS);

  if(hand_over(size, sha)) {
    unlink(UPDATE_ELF);  // not the release's file, or not taken: fetched again another time
    unlink(UPDATE_READY);
    return 1;
  }
  const double handed = poc_now_ms();
  while(!installed(version) && poc_now_ms() - handed < INSTALL_MS) {
    usleep(500 * 1000);
  }
  unlink(UPDATE_ELF);
  unlink(UPDATE_READY);
  if(!installed(version)) {
    say("the installed release is not %s after %d s: its installer did not install", version, INSTALL_MS / 1000);
    const int fd = open(UPDATE_FAILED, O_WRONLY | O_CREAT | O_TRUNC, 0644);  // not tried again by itself
    if(fd >= 0) {
      (void)!write(fd, version, strlen(version));
      (void)!write(fd, "\n", 1);
      close(fd);
    }
    return 1;
  }
  say("release %s is installed, %.0f ms after the installer was handed over", version, poc_now_ms() - handed);
  unlink(UPDATE_FAILED);
  unlink(UPDATE_TRIES);

  if(!access(UPDATE_STAY, F_OK)) {
    say("%s is there: the browser is not opened again", UPDATE_STAY);
    return 0;
  }
  sleep(2);  // (the installer's last notification, and ShadowMountPlus with the title's folder)
  if((app = sceSystemServiceGetAppIdOfRunningBigApp()) > 0) {
    say("app 0x%x is in front now (the user's own?): the browser is not opened", (unsigned)app);
    return 0;
  }
  app_launch_ctx_t ctx = {0};
  sceUserServiceInitialize(0);
  int err = sceUserServiceGetForegroundUser(&ctx.user_id);
  if(err) {
    say("sceUserServiceGetForegroundUser = 0x%x: the browser is not opened", (unsigned)err);
    return 0;
  }
  ctx.structsize = sizeof(ctx);
  err = sceSystemServiceLaunchApp(UPDATE_TITLE, 0, &ctx);
  say("sceSystemServiceLaunchApp(%s) = 0x%x", UPDATE_TITLE, (unsigned)err);
  return 0;
}
