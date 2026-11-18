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

// BrowserPS5's uninstaller (2026-11-14; the model is XPS5's, the author's
// other PS5 app):
//
//   1. ShadowMountPlus, which put the title on the home screen, is asked to
//      take it off, through its own API on 127.0.0.1:10101
//      (/api/v1/games/uninstall). Nothing else is asked of the system: with
//      no answer from it, the notification says to delete the tile by hand
//      (Options, Delete).
//   2. What the installer wrote is removed: the title's folder, the engine's
//      library and processes, the PDF reader and the ad blocker's lists, and
//      the folder of the browser's own updates. Only those paths, all
//      inside /user/homebrew/ and /data/homebrew/ (remove_files' own code
//      and rule).
//
// What the browser keeps of its user (/user/homebrew/browserps5-rn/data:
// favorites, history, settings, logins, unfinished downloads' maps) stays,
// so that installing again finds it. Built with -DREMOVE_DATA
// (BrowserPS5-uninstall-all.elf) it goes too. Files the user downloaded
// (/data/downloads and the other folders chosen) are never touched.
//
// With the browser open nothing is removed: it says so and ends. It closes
// no app.
//
// Built with -DDRY_RUN (make dry: BrowserPS5-uninstall-dry.elf, a test's,
// not in the release) it removes nothing and asks ShadowMountPlus nothing:
// it says which of its paths are there, and whether ShadowMountPlus's API
// answers.

#define POC_NAME "BrowserPS5 RN"

#include <arpa/inet.h>
#include <netinet/in.h>

#include <sys/socket.h>
#include <sys/time.h>

#define main remove_files_main
#include "../remove_files/main.c"
#undef main

#define TITLE_ID "PPSA97024"
#define SMP_PORT 10101

#define BROWSER_PORT 9040  // the browser's own, for as long as it runs (poc/common/title_link.h)


/**
 * A connection to this console's own port, or -1 when nothing listens there.
 **/
static int
local_connect(int port, int seconds) {
  struct timeval tv = {seconds, 0};
  struct sockaddr_in a;
  int s;

  if((s=socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    return -1;
  }
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
  memset(&a, 0, sizeof(a));
  a.sin_family = AF_INET;
  a.sin_port = htons(port);
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if(connect(s, (struct sockaddr*)&a, sizeof(a))) {
    close(s);
    return -1;
  }
  return s;
}


/**
 * ShadowMountPlus asked to take the title off the home screen: its HTTP
 * status, or -1 when its API does not answer.
 **/
static int
smp_uninstall(void) {
  static const char body[] = "{\"title_id\":\"" TITLE_ID "\"}";
  char req[320], answer[512];
  int s, status = -1;
  ssize_t n, got = 0;

  if((s=local_connect(SMP_PORT, 30)) < 0) {
    return -1;
  }
  n = snprintf(req, sizeof(req),
               "POST /api/v1/games/uninstall HTTP/1.1\r\nHost: 127.0.0.1:%d\r\n"
               "Content-Type: application/json\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s",
               SMP_PORT, sizeof(body) - 1, body);
  if(write(s, req, (size_t)n) != n) {
    close(s);
    return -1;
  }
  while(got < (ssize_t)sizeof(answer) - 1 && (n=read(s, answer + got, sizeof(answer) - 1 - (size_t)got)) > 0) {
    got += n;
  }
  close(s);
  answer[got] = 0;
  if(sscanf(answer, "HTTP/1.%*d %d", &status) != 1) {
    return -1;
  }
  return status;
}


int
main(void) {
  static const char* const paths[] = {
    "/data/homebrew/" TITLE_ID,
    "/user/homebrew/browserps5-rn/full",
    "/user/homebrew/browserps5-rn/plus",
    "/user/homebrew/browserps5-rn/pdfjs",
    // 2026-11-18 (1.0.0-rc14): two folders that stayed behind. The ad blocker's lists are the installer's (since
    // 1.0.0-rc5); the update folder is the browser's own (from 1.0.0-rc14: a fetched installer may wait there,
    // 116 MB, with its notes). Neither is the user's data.
    "/user/homebrew/browserps5-rn/adblock",
    "/user/homebrew/browserps5-rn/update",
    "/user/homebrew/browserps5-rn/engine-env.txt",
    "/user/homebrew/browserps5-rn/release.txt",
    "/user/homebrew/lib/libWebKitFull.so.SOVERSION",
    "/user/homebrew/lib/libWebKitPlus.so.SOVERSION",
#ifdef REMOVE_DATA
    "/user/homebrew/browserps5-rn",
#endif
  };
  struct stat st;
  int removed = 0, failed = 0, smp = -1;

  if((smp=local_connect(BROWSER_PORT, 2)) >= 0) {  // (sceLncUtilGetAppId answers a payload with an error: install.c)
    close(smp);
    POC_NOTIFY("is open: nothing was removed. Close it (Options, Close application) and send the uninstaller again.");
    return 1;
  }
  smp = -1;
#ifdef DRY_RUN
  if((smp=local_connect(SMP_PORT, 5)) >= 0) {
    close(smp);
  }
  POC_LOG("dry run: ShadowMountPlus's API on 127.0.0.1:%d %s", SMP_PORT, smp >= 0 ? "answers" : "does NOT answer");
  for(size_t i=0; i < sizeof(paths) / sizeof(paths[0]); i++) {
    const int there = !lstat(paths[i], &st);
    POC_LOG("dry run: %s: %s%s", paths[i], there ? (S_ISDIR(st.st_mode) ? "a folder, would be removed" : "a file, would be removed") : "not there",
            there && !allowed_path(paths[i]) ? " (REFUSED by the rule)" : "");
    removed += there;
  }
  POC_RESULT(removed > 0, "dry run: %d path(s) would be removed, nothing was touched", removed);
  return 0;
#endif
  if(!lstat("/data/homebrew/" TITLE_ID, &st)) {  // on the home screen through ShadowMountPlus: it takes the tile off
    smp = smp_uninstall();
    POC_LOG("ShadowMountPlus answered %d", smp);
  }
  for(size_t i=0; i < sizeof(paths) / sizeof(paths[0]); i++) {
    int n;

    if(lstat(paths[i], &st)) {
      continue;  // not there
    }
    if(!allowed_path(paths[i]) || (n=remove_tree(paths[i])) < 0) {
      failed++;
      continue;
    }
    POC_LOG("removed %s (%d entries)", paths[i], n);
    removed++;
  }
  if(failed) {
    POC_NOTIFY("could not be removed whole (%d of its folders stayed). Send the uninstaller again.", failed);
    return 1;
  }
  if(!removed) {
    POC_NOTIFY("is not installed on this console: nothing to remove.");
    return 0;
  }
#ifdef REMOVE_DATA
#define KEPT "Its favorites, history, settings and logins went with it."
#else
#define KEPT "Your favorites, history and settings stay for the next install (BrowserPS5-uninstall-all.elf removes them too)."
#endif
  if(smp == 200) {
    POC_NOTIFY("was removed, and its tile taken off the home screen. " KEPT);
  } else {
    POC_NOTIFY("was removed. " KEPT " If its tile is still on the home screen, delete it there (Options, Delete).");
  }
  return 0;
}
