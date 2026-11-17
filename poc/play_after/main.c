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

// play_after: the start page's "Play" in BrowserPS5 RN (2026-11-16).
//
// The browser asked for the game from its own process, once its title had left, and the console went down
// twice (14:48, PPSA26917; 14:59, CUSA03280): each time within 1.5 s of sceSystemServiceLaunchApp, with the
// browser's processes (the app, its launcher and NetworkProcess) still there. The user's own way never did that:
// "Sair", and the game from the home screen. So the browser now leaves exactly as "Sair" does, and this small
// payload, which it sends to the console's payload loader (elfldr, 127.0.0.1:9021) just before, does the rest
// from the home screen, the way launch_title opened this project's title many times:
//   - reads the request (play_after.h) and removes it, so it is used once, and only while fresh;
//   - waits until both processes of the browser are gone (30 s at most; otherwise nothing is asked for);
//   - waits until the system has no big app and the browser's title state is old, for 2 s in a row (30 s at
//     most), then 2 s more; an app in front then (the user started one) is left alone;
//   - asks for the game once (sceSystemServiceLaunchApp, as launch_title does); a refusal is said on the screen,
//     and the user starts the game from the home screen.
// All of it goes to PLAY_AFTER_LOG. It sleeps between looks: no spinning (payload threads are FIFO 700).

#define POC_NAME "play_after"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/stat.h>

#include "poc_log.h"
#include "link.h"
#include "play_after.h"

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

#define WAIT_MS  30000  // for the browser to be gone, and then for the system to have no app in front
#define QUIET_MS 2000   // no app in front, in a row
#define LOOK_US  (200 * 1000)

static int log_fd = -1;
static double start_ms;


static void
say(const char* fmt, ...) {
  char line[320];
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


// Four capitals and five digits: a game's title id (PPSA..., CUSA...).
static int
id_ok(const char* id) {
  if(strlen(id) != 9) {
    return 0;
  }
  for(int i = 0; i < 9; i++) {
    if(i < 4 ? !isupper((unsigned char)id[i]) : !isdigit((unsigned char)id[i])) {
      return 0;
    }
  }
  return 1;
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


int
main(void) {
  char line[96] = {0};
  char id[16] = {0};
  char lang[4] = {0};
  int ui = 0, launcher = 0, app = 0;
  struct stat st;

  start_ms = poc_now_ms();
  log_fd = open(PLAY_AFTER_LOG, O_WRONLY | O_CREAT | O_TRUNC, 0644);

  FILE* f = fopen(PLAY_AFTER_ASK, "r");
  if(!f) {
    say("no request (%s): nothing asked for", PLAY_AFTER_ASK);
    return 1;
  }
  const time_t now = time(0);
  const int fresh = !fstat(fileno(f), &st) && now - st.st_mtime < 120 && st.st_mtime - now < 5;
  const int got = fgets(line, sizeof(line), f) ? sscanf(line, "%15s %d %d %3s", id, &ui, &launcher, lang) : 0;
  fclose(f);
  unlink(PLAY_AFTER_ASK);
  line[strcspn(line, "\r\n")] = 0;
  const int en = !strcmp(lang, "en");
  if(got < 3 || !id_ok(id) || !fresh) {
    say("a request that is %s (\"%s\"): nothing asked for", fresh ? "not right" : "old", line);
    return 1;
  }
  say("%s asked for; the browser's app %d and launcher %d", id, ui, launcher);

  while((alive(ui) || alive(launcher)) && poc_now_ms() - start_ms < WAIT_MS) {
    usleep(LOOK_US);
  }
  if(alive(ui) || alive(launcher)) {
    say("the browser is still there after %d s: nothing asked for", WAIT_MS / 1000);
    poc_notify(en ? "Play: the browser did not close in time, %s was not started"
                  : "Jogar: o browser n\xc3\xa3o fechou a tempo, %s n\xc3\xa3o foi aberto", id);
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
    say("the system still has an app in front (0x%x) after %d s: nothing asked for", (unsigned)app, WAIT_MS / 1000);
    poc_notify(en ? "Play: the system still had an app open, %s was not started"
                  : "Jogar: o sistema ainda tinha uma app aberta, %s n\xc3\xa3o foi aberto", id);
    return 1;
  }
  say("no app in front for %d ms", QUIET_MS);
  sleep(2);
  if((app = sceSystemServiceGetAppIdOfRunningBigApp()) > 0) {
    say("app 0x%x is in front now (the user's own?): nothing asked for", (unsigned)app);
    return 0;
  }

  app_launch_ctx_t ctx = {0};
  sceUserServiceInitialize(0);
  int err = sceUserServiceGetForegroundUser(&ctx.user_id);
  if(err) {
    say("sceUserServiceGetForegroundUser = 0x%x: nothing asked for", (unsigned)err);
  } else {
    ctx.structsize = sizeof(ctx);
    say("asking for %s (user 0x%x)", id, ctx.user_id);
    err = sceSystemServiceLaunchApp(id, 0, &ctx);
    say("sceSystemServiceLaunchApp(%s) = 0x%x", id, (unsigned)err);
    if(err > 0) {
      return 0;
    }
  }
  poc_notify(en ? "Play: %s did not start (0x%x): start it from the home screen"
                : "Jogar: %s n\xc3\xa3o abriu (0x%x): abre-o no ecr\xc3\xa3 inicial", id, (unsigned)err);
  return 1;
}
