/* The phone's server, asked by someone who is NOT the phone (2026-11-14). While the browser runs, a small HTTP server
   listens on the local network (browser/phone.c: the phone's page, typing, keys, files). Any device on the same
   Wi-Fi can send it whatever it likes. This is phone.c and files.c AS THEY ARE, started on this machine with
   AddressSanitizer and UndefinedBehaviorSanitizer (and once more with glibc's own checks), one phone paired, and
   then thousands of requests made from good ones and broken: bytes changed, any bytes put in, lengths that lie,
   headers twice, lines of 60 KiB, requests cut short, tokens that are nearly the right one.

   Checked, besides "no fall, nothing read or written outside its memory":
     - without the phone's token NOTHING is done on the console (no page opened, no key, no text, no download
       touched, no file taken or shown) and nothing of it is told (no file's content, no folder, no state);
     - no request, with the token or without, gets a private folder's file, removes it, or pairs another phone;
     - the server goes on answering the phone after every one of them.

       phone_fuzz <an empty folder> [rounds]                                                                    */

#define _GNU_SOURCE
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include "phone.h"

static int checks, failed, round_now;
static unsigned long long rng = 0x94D049BB133111EBull;
static volatile int acted;  /* something was done on the console */
static char dir[4096], files_dir[4200], token[40];  /* (realpath() wants PATH_MAX bytes) */
static char sent[80000];
static size_t sent_n;

static unsigned
rnd(void) {
  rng ^= rng << 13;
  rng ^= rng >> 7;
  rng ^= rng << 17;
  return (unsigned)(rng >> 11);
}

static void
show_sent(void) {
  printf("  [");
  for(size_t i = 0; i < sent_n && i < 260; i++) {
    const unsigned char c = (unsigned char)sent[i];
    printf(c >= 0x20 && c < 0x7f && c != '\\' ? "%c" : "\\x%02x", c);
  }
  printf("%s]\n", sent_n > 260 ? "..." : "");
}

static void
check(int fine, const char* what) {
  checks++;
  if(!fine) {
    failed++;
    if(failed <= 12) {
      printf("FAIL (round %d): %s\n", round_now, what);
      show_sent();
    }
  }
}

static void
stopped(int sig) {
  printf("STOPPED (signal %d) in round %d, after this request:\n", sig, round_now);
  show_sent();
  fflush(stdout);
  _exit(3);
}

/* ---- what the browser would do: here it is only counted ---- */
static void on_open(const char* t, void* c) { (void)t; (void)c; acted++; }
static void on_type(const char* t, int e, void* c) { (void)t; (void)e; (void)c; acted++; }
static void on_key(const char* k, void* c) { (void)k; (void)c; acted++; }
static int
on_file_path(const char* name, const char* where, long long size, char* path, size_t path_size, void* c) {
  (void)c;
  acted++;
  if(!strcmp(where, "usb")) {
    return PHONE_NO_USB;
  }
  if(size > 4LL * 1024 * 1024) {
    return PHONE_NO_ROOM;
  }
  if(strchr(name, '/')) {
    return PHONE_NO_PLACE;
  }
  snprintf(path, path_size, "%s/up-%u.bin", dir, rnd() % 4);
  return 0;
}
static void on_file_done(const char* path, long long size, void* c) { (void)path; (void)size; (void)c; }
static void on_paired(int n, void* c) { (void)n; (void)c; }
static size_t
on_state(char* json, size_t size, void* c) {
  (void)c;
  return (size_t)snprintf(json, size, "{\"title\":\"STATE-OF-THE-TV\",\"url\":\"https://a.pt/\"}");
}
static size_t
on_downloads(char* json, size_t size, void* c) {
  (void)c;
  return (size_t)snprintf(json, size, "{\"downloads\": [{\"id\": 3, \"name\": \"NAME-OF-A-DOWNLOAD.zip\", \"state\": \"done\"}]}");
}
static void on_download(const char* a, int id, void* c) { (void)a; (void)id; (void)c; acted++; }
static int
on_places(phone_place_t* places, int max, void* c) {
  (void)c;
  if(max < 2) {
    return 0;
  }
  snprintf(places[0].kind, sizeof(places[0].kind), "downloads");
  snprintf(places[0].path, sizeof(places[0].path), "%s/down", files_dir);
  snprintf(places[1].kind, sizeof(places[1].kind), "private");
  snprintf(places[1].path, sizeof(places[1].path), "%s/down/private", files_dir);
  return 2;
}
static void on_show(const char* path, void* c) { (void)path; (void)c; acted++; }
static void on_sending(void* c) { (void)c; }

static void
put_file(const char* rel, const char* text) {
  char path[4300];
  snprintf(path, sizeof(path), "%s/%s", files_dir, rel);
  FILE* f = fopen(path, "w");
  if(f) {
    fputs(text, f);
    fclose(f);
  }
}

/* What is in `sent` goes to the server as it is; its whole answer comes into out. The status, or -1. */
static int
ask(char* out, size_t out_size) {
  const int fd = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in to;
  memset(&to, 0, sizeof(to));
  to.sin_family = AF_INET;
  to.sin_port = htons((uint16_t)phone_port());
  to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  struct timeval tv = {20, 0};
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  out[0] = 0;
  if(connect(fd, (struct sockaddr*)&to, sizeof(to))) {
    close(fd);
    return -1;
  }
  for(size_t at = 0; at < sent_n;) {
    const ssize_t k = send(fd, sent + at, sent_n - at, MSG_NOSIGNAL);
    if(k <= 0) {
      break;
    }
    at += (size_t)k;
  }
  shutdown(fd, SHUT_WR);  /* (nothing more comes: a request cut short is not waited for) */
  size_t have = 0;
  for(;;) {
    const ssize_t k = recv(fd, out + have, out_size - 1 - have, 0);
    if(k <= 0) {
      break;
    }
    have += (size_t)k;
    if(have + 1 >= out_size) {
      break;
    }
  }
  out[have] = 0;
  for(size_t i = 0; i < have; i++) {  /* (so that strstr() sees all of it) */
    if(!out[i]) {
      out[i] = ' ';
    }
  }
  close(fd);
  int status = -1;
  sscanf(out, "HTTP/1.1 %d", &status);
  return status;
}

static void
say(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
#include <stdarg.h>
static void
say(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  const int n = vsnprintf(sent, sizeof(sent), fmt, args);
  va_end(args);
  sent_n = n < 0 ? 0 : (size_t)n >= sizeof(sent) ? sizeof(sent) - 1 : (size_t)n;
}

static void
put_in(size_t at, const char* what, size_t n) {
  if(sent_n + n >= sizeof(sent) || at > sent_n) {
    return;
  }
  memmove(sent + at + n, sent + at, sent_n - at);
  memcpy(sent + at, what, n);
  sent_n += n;
}

/* A request the phone's page would make, with `auth` as its token's line (or none). */
static void
a_request(const char* auth) {
  static const char* const keys[] = {"back", "home", "up", "enter", "sidecard", "shutdown", "", "rm -rf", "%00"};
  const int which = rnd() % 16;
  char body[400];
  snprintf(body, sizeof(body), "%s", rnd() % 2 ? "rtp.pt" : "some words to type \xc3\xa9");
  const char* b = body;
  char line[1400];
  switch(which) {
  case 0: snprintf(line, sizeof(line), "GET /api/state"); b = 0; break;
  case 1: snprintf(line, sizeof(line), "POST /api/open"); break;
  case 2: snprintf(line, sizeof(line), "POST /api/type?enter=%d", rnd() % 2); break;
  case 3: snprintf(line, sizeof(line), "POST /api/key?k=%s", keys[rnd() % 9]); b = ""; break;
  case 4: snprintf(line, sizeof(line), "GET /api/downloads"); b = 0; break;
  case 5: snprintf(line, sizeof(line), "GET /api/download?do=%s&id=%d", rnd() % 2 ? "cancel" : "open", (int)(rnd() % 5)); b = 0; break;
  case 6: snprintf(line, sizeof(line), "POST /api/file?name=x%d.bin&where=%s", (int)(rnd() % 3), rnd() % 3 ? "auto" : "usb"); break;
  case 7: snprintf(line, sizeof(line), "GET /api/room?name=a.mp4&size=%u", rnd()); b = 0; break;
  case 8: snprintf(line, sizeof(line), "GET /api/files"); b = 0; break;
  case 9: snprintf(line, sizeof(line), "GET /api/files?path=%s/down%s", files_dir, rnd() % 3 ? "" : rnd() % 2 ? "/private" : "/..%2Foutside"); b = 0; break;
  case 10: snprintf(line, sizeof(line), "GET /api/get?t=%08x%08x%08x%08x", rnd(), rnd(), rnd(), rnd()); b = 0; break;
  case 11: snprintf(line, sizeof(line), "POST /api/show?path=%s/down/%s", files_dir, rnd() % 2 ? "v.mp4" : "private/key.txt"); b = ""; break;
  case 12: snprintf(line, sizeof(line), "POST /api/remove?path=%s/down/%s", files_dir, rnd() % 3 ? "private/key.txt" : rnd() % 2 ? "gone.txt" : "../outside.txt"); b = ""; break;
  case 13: snprintf(line, sizeof(line), "POST /api/pair"); snprintf(body, sizeof(body), "%06u", rnd() % 1000000); break;
  case 14: snprintf(line, sizeof(line), "GET /api/get?path=%s/down/private/key.txt", files_dir); b = 0; break;
  default: snprintf(line, sizeof(line), "GET /%s", rnd() % 2 ? "" : "nothing"); b = 0; break;
  }
  if(b) {
    say("%s HTTP/1.1\r\nHost: x\r\n%sContent-Length: %zu\r\n\r\n%s", line, auth, strlen(b), b);
  } else {
    say("%s HTTP/1.1\r\nHost: x\r\n%s\r\n", line, auth);
  }
}

/* The request in `sent`, broken some way. */
static void
break_it(void) {
  static const char* const lines[] = {"Content-Length: 99999999999999999999\r\n", "Content-Length: -1\r\n", "Content-Length: abc\r\n",
                                      "Content-Length: 4294967296\r\n", "Transfer-Encoding: chunked\r\n", "X-Token: \r\n", "X-Token\r\n",
                                      "Range: bytes=0-\r\n", "Range: bytes=-99999999999999999999\r\n", "Expect: 100-continue\r\n",
                                      "Origin: http://evil.example\r\n", "Connection: keep-alive\r\n", ": empty name\r\n", "Host: \r\n",
                                      "Content-Length: 5\r\nContent-Length: 500\r\n"};
  const int ways = 1 + rnd() % 3;
  for(int w = 0; w < ways && sent_n > 8; w++) {
    const int how = rnd() % 9;
    if(how == 0) {  /* cut short */
      sent_n = rnd() % sent_n;
    } else if(how == 1) {  /* bytes changed */
      const int k = 1 + rnd() % 5;
      for(int i = 0; i < k; i++) {
        sent[rnd() % sent_n] = (char)rnd();
      }
    } else if(how == 2) {  /* any bytes put in */
      char some[64];
      const size_t k = 1 + rnd() % 60;
      for(size_t i = 0; i < k; i++) {
        some[i] = (char)rnd();
      }
      put_in(rnd() % (sent_n + 1), some, k);
    } else if(how == 3) {  /* a header line that lies, after the first line */
      const char* first = memchr(sent, '\n', sent_n);
      const char* l = lines[rnd() % (sizeof(lines) / sizeof(lines[0]))];
      if(first) {
        put_in((size_t)(first + 1 - sent), l, strlen(l));
      }
    } else if(how == 4) {  /* a path of many kilobytes */
      static char many[61000];
      const size_t k = 2000 + rnd() % 58000;
      memset(many, rnd() % 2 ? 'a' : '/', k);
      const char* space = memchr(sent, ' ', sent_n);
      if(space) {
        put_in((size_t)(space + 1 - sent) + (rnd() % 2 ? 1 : 0), many, k);
      }
    } else if(how == 5) {  /* line ends of one byte */
      for(size_t i = 0; i + 1 < sent_n; i++) {
        if(sent[i] == '\r' && sent[i + 1] == '\n') {
          memmove(sent + i, sent + i + 1, sent_n - i - 1);
          sent_n--;
        }
      }
    } else if(how == 6) {  /* what tries to leave a folder, anywhere */
      static const char* const ups[] = {"/../", "%2e%2e%2f", "..%2F", "%00", "//", "\\", "%2Fetc%2Fpasswd", "?path=/etc/passwd&"};
      const char* u = ups[rnd() % 8];
      put_in(rnd() % (sent_n + 1), u, strlen(u));
    } else if(how == 7) {  /* many headers */
      const int n = 20 + rnd() % 400;
      const char* first = memchr(sent, '\n', sent_n);
      for(int i = 0; i < n && first; i++) {
        put_in((size_t)(first + 1 - sent), "X-Filler: yes\r\n", 15);
      }
    } else {  /* another method, another version */
      static const char* const starts[] = {"HEAD", "PUT", "DELETE", "OPTIONS", "get", "POST", "PATCH", "CONNECT", ""};
      const char* space = memchr(sent, ' ', sent_n);
      if(space) {
        const size_t old = (size_t)(space - sent);
        const char* m = starts[rnd() % 9];
        memmove(sent, sent + old, sent_n - old);
        sent_n -= old;
        put_in(0, m, strlen(m));
      }
    }
  }
}

static int
has(const char* text, size_t n, const char* what) {
  return memmem(text, n, what, strlen(what)) != 0;
}

int
main(int argc, char** argv) {
  const int rounds = argc > 2 ? atoi(argv[2]) : 4000;
  static char out[300000];
  char pin[8], auth[80], path[4300];

  if(argc < 2 || !realpath(argv[1], dir)) {
    printf("phone_fuzz <an empty folder> [rounds]\n");
    return 2;
  }
  signal(SIGABRT, stopped);
  signal(SIGPIPE, SIG_IGN);
  snprintf(files_dir, sizeof(files_dir), "%s/files", dir);
  mkdir(files_dir, 0755);
  snprintf(path, sizeof(path), "%s/down", files_dir);
  mkdir(path, 0755);
  snprintf(path, sizeof(path), "%s/down/private", files_dir);
  mkdir(path, 0755);
  put_file("down/a.txt", "CONTENT-OF-A-SHARED-FILE");
  put_file("down/v.mp4", "CONTENT-OF-A-SHARED-FILE");
  put_file("down/private/key.txt", "CONTENT-OF-A-PRIVATE-FILE");
  put_file("outside.txt", "CONTENT-OF-A-FILE-OUTSIDE");

  phone_calls_t calls = {on_open, on_type, on_key, on_file_path, on_file_done, on_paired, on_state, on_downloads, on_download,
                         on_places, on_show, on_sending, 0};
  const int port = phone_start(&calls, "<!doctype html><title>THE-PHONE-PAGE</title>");
  if(port <= 0) {
    printf("the server did not start\n");
    return 2;
  }
  phone_pin_open(pin);
  say("POST /api/pair HTTP/1.1\r\nHost: x\r\nContent-Length: 6\r\n\r\n%s", pin);
  int status = ask(out, sizeof(out));
  const char* t = strstr(out, "{\"token\":\"");
  if(status != 200 || !t || sscanf(t, "{\"token\":\"%32[0-9a-f]\"}", token) != 1 || phone_count() != 1) {
    printf("the phone was not paired: %d\n", status);
    return 2;
  }
  snprintf(auth, sizeof(auth), "X-Token: %s\r\n", token);

  for(round_now = 1; round_now <= rounds; round_now++) {
    /* whose request is it? */
    char line[120] = "";
    const int who = rnd() % 10;
    if(who == 0) {  /* the phone's own, broken: it may do things; it may not fall, nor reach what is private */
      snprintf(line, sizeof(line), "%s", auth);
    } else if(who == 1) {
      snprintf(line, sizeof(line), "X-Token: %08x%08x%08x%08x\r\n", rnd(), rnd(), rnd(), rnd());
    } else if(who == 2) {  /* nearly the right one */
      char near[40];
      snprintf(near, sizeof(near), "%s", token);
      const int at = rnd() % 32;
      near[at] = near[at] == 'f' ? '0' : 'f';
      snprintf(line, sizeof(line), "X-Token: %s\r\n", near);
    } else if(who == 3) {  /* its beginning only; or more than it */
      char part[80];
      snprintf(part, sizeof(part), "%.*s%s", rnd() % 2 ? (int)(rnd() % 32) : 32, token, rnd() % 2 ? "" : "00");
      if(!strcmp(part, token)) {
        part[0] = part[0] == 'a' ? 'b' : 'a';
      }
      snprintf(line, sizeof(line), "X-Token: %s\r\n", part);
    } else if(who == 4) {
      snprintf(line, sizeof(line), "Cookie: token=%.16s\r\nAuthorization: Bearer %.16s\r\n", "0123456789abcdef", "0123456789abcdef");
    }
    a_request(line);
    if(rnd() % 4) {
      break_it();
    }
    const int theirs = has(sent, sent_n, token);  /* (the whole token is in it: the phone's) */
    const int before = acted;
    status = ask(out, sizeof(out));
    const size_t got = strlen(out);
    if(!theirs) {
      check(acted == before, "without the phone's token nothing is done on the console");
      check(!has(out, got, "CONTENT-OF-A-SHARED-FILE") && !has(out, got, "STATE-OF-THE-TV") && !has(out, got, "NAME-OF-A-DOWNLOAD")
            && !has(out, got, files_dir) && !has(out, got, "a.txt"),
            "without the phone's token nothing of the console is told");
    }
    check(!has(out, got, "CONTENT-OF-A-PRIVATE-FILE") && !has(out, got, "CONTENT-OF-A-FILE-OUTSIDE"),
          "no request gets a private folder's file, or one outside the shared folders");
    check(!has(out, got, "{\"token\":\"") && phone_count() == 1, "no request pairs another phone with no PIN shown");
    if(round_now % 40 == 0) {  /* is the phone still answered? */
      say("GET /api/state HTTP/1.1\r\nHost: x\r\n%s\r\n", auth);
      status = ask(out, sizeof(out));
      check(status == 200 && strstr(out, "STATE-OF-THE-TV") != 0, "the server goes on answering the phone");
    }
  }
  struct stat st;
  snprintf(path, sizeof(path), "%s/down/private/key.txt", files_dir);
  check(!stat(path, &st), "the private folder's file is still there");
  snprintf(path, sizeof(path), "%s/outside.txt", files_dir);
  check(!stat(path, &st), "the file outside the shared folders is still there");
  phone_stop();
  printf("%s: %d checks, %d failed (%d requests to the phone's server from someone who is not the phone)\n", failed ? "FAIL" : "PASS", checks,
         failed, rounds);
  return failed ? 1 : 0;
}
