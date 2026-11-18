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

// BrowserPS5-check.elf (2026-11-17): what a console has of BrowserPS5 and how its last start went, as text.
//
// BrowserPS5 was made and tested on one console (firmware 13.60). On another console, or another firmware, a
// start that fails leaves its reasons in four files of the title's folder, and a person who is not a developer
// has to know of them and fetch them. This payload reads what there is and says it in one report: in the
// output of whoever sent it (the payload sender's terminal) and in one text file, REPORT_FILE, for senders that
// show no output. It is the way smplog of XPS5, the author's other PS5 app, shows why its title was or was not
// added.
//
// It changes nothing on the console but that one file (and reads only: the firmware's version, the browser's own
// files and logs, ShadowMountPlus's log lines that name this title). What it leaves out on purpose, because a
// report is pasted where others read it: addresses of pages (cut from every line), and the lines of the
// browser's log that are not about its start (they name sites, games and accounts).

#define POC_NAME "BrowserPS5-check"

#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>

#include "poc_log.h"

// The release it came with (build-release-rn.sh writes release.h); "dev" when built alone.
#if defined(__has_include) && __has_include("release.h")
#include "release.h"
#define CHECK_VERSION RELEASE_VERSION
#else
#define CHECK_VERSION "dev"
#endif

#define TITLE_ID     "PPSA97024"
#define TITLE_DIR    "/data/homebrew/" TITLE_ID
#define ENGINE_DIR   "/user/homebrew/browserps5-rn"
#define RELEASE_FILE ENGINE_DIR "/release.txt"
#define REPORT_FILE  TITLE_DIR "/BrowserPS5-check.txt"
#define SMP_LOG      "/data/shadowmount/debug.log"
#define MAX_LOG      (8 << 20)

typedef struct sw_version {
  uint64_t size;
  char text[0x1c];
  uint32_t version;  // 0xMMmm....: "MM.mm" in hexadecimal digits
} sw_version_t;
int sceKernelGetProsperoSystemSwVersion(sw_version_t* version);

static FILE* report;


// A line of the report: to the sender and to the file.
__attribute__((format(printf, 1, 2))) static void
say(const char* fmt, ...) {
  char line[1024];
  va_list args;

  va_start(args, fmt);
  vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);
  printf("%s\n", line);
  fflush(stdout);
  if(report) {
    fprintf(report, "%s\n", line);
  }
}


static void
show_file(const char* path) {
  struct stat st;

  if(stat(path, &st)) {
    say("  missing             %s", path);
  } else if(S_ISDIR(st.st_mode)) {
    say("  folder              %s", path);
  } else {
    say("  %12lld bytes  %s", (long long)st.st_size, path);
  }
}


// The end of a file (MAX_LOG at most), NUL-terminated; 0 when it cannot be read.
static char*
read_end(const char* path, size_t* length) {
  struct stat st;
  const int fd = open(path, O_RDONLY);
  size_t size, got = 0;
  char* text;
  ssize_t n;

  if(fd < 0) {
    return 0;
  }
  if(fstat(fd, &st) || st.st_size < 0) {
    close(fd);
    return 0;
  }
  size = st.st_size > MAX_LOG ? MAX_LOG : (size_t)st.st_size;
  if(st.st_size > MAX_LOG) {
    lseek(fd, st.st_size - MAX_LOG, SEEK_SET);
  }
  if(!(text=malloc(size + 1))) {
    close(fd);
    return 0;
  }
  while(got < size && (n=read(fd, text + got, size - got)) > 0) {
    got += (size_t)n;
  }
  close(fd);
  text[got] = 0;
  *length = got;
  return text;
}


// A line as the report has it: no page's address (from "http://" or "https://" to the next space, quote or
// bracket), no number of the console's user ("user 0x..."), no control characters, 300 bytes at most.
static void
say_line(const char* line) {
  char out[320];
  size_t n = 0;

  for(const char* c = line; *c && n < 300;) {
    if(!strncmp(c, "http://", 7) || !strncmp(c, "https://", 8)) {
      n += (size_t)snprintf(out + n, sizeof(out) - n, "<address>");
      while(*c && *c != ' ' && *c != '"' && *c != ')' && *c != ']' && *c != '>') {
        c++;
      }
    } else if(!strncmp(c, "user 0x", 7)) {
      n += (size_t)snprintf(out + n, sizeof(out) - n, "user <id>");
      c += 7;
      while((*c >= '0' && *c <= '9') || (*c >= 'a' && *c <= 'f')) {
        c++;
      }
    } else {
      out[n++] = (unsigned char)*c < 0x20 ? ' ' : *c;
      c++;
    }
  }
  out[n] = 0;
  say("  %s", out);
}


// Of a log: its first `first` lines and its last `last` (the ones with one of `only`, when given), without the
// pictures a test build writes into it ("SHOT").
static void
show_log(const char* path, int first, int last, const char* const* only) {
  size_t length = 0;
  char* text = read_end(path, &length);
  char** lines;
  int count = 0, room = 0;

  if(!text) {
    say("  (no %s)", path);
    return;
  }
  for(char* s = text; s < text + length;) {  // (first: how many lines)
    char* end = strchr(s, '\n');
    room++;
    s = end ? end + 1 : text + length;
  }
  if(!(lines=calloc((size_t)room + 1, sizeof(*lines)))) {
    free(text);
    return;
  }
  for(char* s = text; s < text + length;) {
    char* end = strchr(s, '\n');
    if(end) {
      *end = 0;
    }
    int wanted = strncmp(s, "SHOT", 4) != 0 && s[0];
    if(wanted && only) {
      wanted = 0;
      for(const char* const* o = only; *o && !wanted; o++) {
        wanted = strstr(s, *o) != 0;
      }
    }
    if(wanted) {
      lines[count++] = s;
    }
    s = end ? end + 1 : text + length;
  }
  say("  %s: %d line(s) of the kind shown", path, count);
  for(int i = 0; i < count; i++) {
    if(i < first || i >= count - last) {
      say_line(lines[i]);
    } else if(i == first) {
      say("  ... (%d more)", count - first - last);
    }
  }
  free(lines);
  free(text);
}


// Does something answer on a port of this console?
static int
answers(int port) {
  struct timeval tv = {2, 0};
  struct sockaddr_in a;
  const int s = socket(AF_INET, SOCK_STREAM, 0);
  int open;

  if(s < 0) {
    return 0;
  }
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
  memset(&a, 0, sizeof(a));
  a.sin_family = AF_INET;
  a.sin_port = htons((uint16_t)port);
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  open = !connect(s, (struct sockaddr*)&a, sizeof(a));
  close(s);
  return open;
}


// The console's pipes: a byte written to one and read back. When the system's pool for them is used up a pipe
// is still made and the write fails; releases up to 1.0.0-rc13 load no page then, until the console is restarted.
static void
pipe_check(void) {
  int fds[2];
  char byte = 1;

  if(pipe(fds)) {
    say("pipes: none can be made (%s)", strerror(errno));
    return;
  }
  if(write(fds[1], &byte, 1) == 1 && read(fds[0], &byte, 1) == 1) {
    say("pipes: working");
  } else {
    say("pipes: a write fails (%s): restart the console (releases up to 1.0.0-rc13 load no page like this)",
        strerror(errno));
  }
  close(fds[0]);
  close(fds[1]);
}


int
main(void) {
  static const char* const start_lines[] = {"[launcher]", "[browser] pid", "engine settings", "the title is",
                                            "waiting up to", "disk cache", "compositing on", "[process-launcher]",
                                            "[ipc-tcp] on", "JIT pool", "[WebKit] pid", "navigation started",
                                            "first frame", "first bytes", "page loaded", "FAIL", "CRASH", "crash",
                                            "refused", "quit:", "run loop ended", "[launcher] control", 0};
  sw_version_t v;
  char release[64] = "";
  char firmware[40] = "unknown";
  FILE* f;

  report = fopen(REPORT_FILE, "w");
  memset(&v, 0, sizeof(v));
  v.size = sizeof(v);
  if(!sceKernelGetProsperoSystemSwVersion(&v)) {
    if(v.version >> 24) {
      snprintf(firmware, sizeof(firmware), "%x.%02x", v.version >> 24, (v.version >> 16) & 0xff);
    } else {
      v.text[sizeof(v.text) - 1] = 0;
      snprintf(firmware, sizeof(firmware), "%s", v.text);
    }
  }
  if((f=fopen(RELEASE_FILE, "r"))) {
    if(fgets(release, sizeof(release), f)) {
      release[strcspn(release, "\r\n")] = 0;
    }
    fclose(f);
  }

  say("BrowserPS5 RN check %s", CHECK_VERSION);
  say("firmware: %s", firmware);
  say("BrowserPS5 RN installed: %s", release[0] ? release : "no release file (" RELEASE_FILE ")");
  say("ELF loader on port 9021: %s", answers(9021) ? "answers" : "does NOT answer");
  say("BrowserPS5 RN open now: %s", answers(9040) ? "yes" : "no");
  pipe_check();

  say("");
  say("== the title (" TITLE_DIR ")");
  show_file(TITLE_DIR "/eboot.bin");
  show_file(TITLE_DIR "/browser.elf");
  show_file(TITLE_DIR "/sce_sys/param.json");
  show_file(TITLE_DIR "/sce_module/libc.prx");
  show_file(TITLE_DIR "/exit_loadexec");
  show_file("/system_ex/app/" TITLE_ID);
  show_file("/user/appmeta/" TITLE_ID);
  say("== the engine");
  show_file("/user/homebrew/lib/libWebKitFull.so.SOVERSION");
  show_file("/user/homebrew/lib/libEGL.so.1");
  show_file("/user/homebrew/lib/libvulkan.so.1");
  show_file(ENGINE_DIR "/full/WebProcess.elf");
  show_file(ENGINE_DIR "/full/NetworkProcess.elf");
  show_file(ENGINE_DIR "/data");
  show_file("/user/homebrew/browserps5/data/tmp");

  say("");
  say("== ShadowMountPlus's log, the lines that name " TITLE_ID);
  {
    static const char* const ours[] = {TITLE_ID, 0};
    show_log(SMP_LOG, 0, 12, ours);
  }
  say("");
  say("== the title's last start");
  show_log(TITLE_DIR "/browser-title.txt", 45, 25, 0);
  say("");
  say("== the GPU");
  show_log(TITLE_DIR "/gpu-broker-results.txt", 12, 8, 0);
  say("");
  say("== the browser's last start (its start only; no page's address)");
  show_log(TITLE_DIR "/browser-run.txt", 22, 4, start_lines);
  say("");
  say("== the pages' process");
  {
    static const char* const trouble[] = {"FAIL", "CRASH", "crashed", "signal ", "abort", 0};
    show_log(TITLE_DIR "/web-stderr.txt", 0, 15, trouble);
  }
  say("");
  say("end of the report%s", report ? " (also in " REPORT_FILE ")" : "");
  if(report) {
    fclose(report);
  }
  return 0;
}
