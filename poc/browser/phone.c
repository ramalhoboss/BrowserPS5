/* Copyright (C) 2026 BrowserPS5 contributors. GPL-3.0-or-later.
   The phone's server (phone.h). One thread, one request at a time, each on
   its own connection ("Connection: close"); it sleeps in accept() and recv()
   between them (payload threads must never spin). A file going to a phone
   has a thread of its own (PHONE_SENDS at most), so that a video playing on
   the phone does not stop the rest. */

#include "phone.h"
#include "files.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define HEAD_MAX 8192
#define CHUNK (256 * 1024)
#define TIMEOUT_S 15

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static phone_calls_t calls;
static const char* page_html;
static int listener = -1;
static int port;
static volatile int running;
static char pin[8];
static time_t pin_until;
static int pin_tries;
static char tokens[PHONE_MAX][33];
static int token_next;
// Files a phone may fetch by address alone (/api/get?t=): an <a download>,
// <video> or <img> cannot send the X-Token header. Each is for one file and
// one phone, and ends PHONE_TICKET_SECONDS after it was last used, or when
// that phone is forgotten.
static struct ticket {
  char id[33];
  char path[FILES_PATH_MAX];  // the real path
  time_t until;
  int slot;                   // the phone's
} tickets[PHONE_TICKETS];
static int ticket_next;
static int senders;           // files going out now


// ---------------------------------------------------------------------------
// Random numbers: the system's; if it has none, the clock and addresses
// mixed (still unguessable from the network in a few minutes, PIN first).

static uint64_t
mix(uint64_t x) {
  x += 0x9e3779b97f4a7c15ull;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
  return x ^ (x >> 31);
}

static void
random_bytes(unsigned char* out, size_t size) {
  static uint64_t state;
  int fd = open("/dev/urandom", O_RDONLY);
  size_t got = 0;

  if(fd >= 0) {
    while(got < size) {
      ssize_t n = read(fd, out + got, size - got);
      if(n <= 0) {
        break;
      }
      got += (size_t)n;
    }
    close(fd);
  }
  if(got == size) {
    return;
  }
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  state ^= mix((uint64_t)ts.tv_nsec ^ (uint64_t)ts.tv_sec << 32 ^ (uintptr_t)&ts
               ^ (uint64_t)getpid() << 16);
  for(size_t i = 0; i < size; i++) {
    if(!(i & 7)) {
      clock_gettime(CLOCK_REALTIME, &ts);
      state = mix(state ^ (uint64_t)ts.tv_nsec);
    }
    out[i] ^= (unsigned char)(state >> (8 * (i & 7)));
  }
}


// ---------------------------------------------------------------------------
// Small things.

int
phone_address_allowed(unsigned long a) {
  a &= 0xffffffffu;
  return (a >> 24) == 10           // 10.0.0.0/8
      || (a >> 20) == 0xac1        // 172.16.0.0/12
      || (a >> 16) == 0xc0a8       // 192.168.0.0/16
      || (a >> 16) == 0xa9fe       // 169.254.0.0/16, a link with no router
      || (a >> 24) == 127;         // this machine
}

int
phone_key_known(const char* name) {
  static const char* const known[] = {
    "back", "forward", "reload", "home", "up", "down", "top", "bottom",
    "enter", "tab", "escape", "backspace", "tabs", "newtab", "fullscreen", "sidecard",
  };
  for(size_t i = 0; i < sizeof(known) / sizeof(known[0]); i++) {
    if(!strcmp(name, known[i])) {
      return 1;
    }
  }
  return 0;
}

int
phone_download_action_known(const char* name) {
  static const char* const known[] = {"cancel", "retry", "open", "unzip"};
  for(size_t i = 0; i < sizeof(known) / sizeof(known[0]); i++) {
    if(!strcmp(name, known[i])) {
      return 1;
    }
  }
  return 0;
}

int
phone_game_action_known(const char* name) {
  return !strcmp(name, "note") || !strcmp(name, "keep") || !strcmp(name, "unpin");
}

int
phone_game_id_ok(const char* id) {
  for(int i = 0; i < 9; i++) {
    if(i < 4 ? !(id[i] >= 'A' && id[i] <= 'Z') : !(id[i] >= '0' && id[i] <= '9')) {
      return 0;
    }
  }
  return !id[9];
}

static int
send_all(int fd, const void* data, size_t size) {
  const char* p = (const char*)data;
  while(size) {
    ssize_t n = send(fd, p, size, MSG_NOSIGNAL);
    if(n <= 0) {
      if(n < 0 && errno == EINTR) {
        continue;
      }
      return -1;
    }
    p += n;
    size -= (size_t)n;
  }
  return 0;
}

static void
reply(int fd, int status, const char* type, const char* body, size_t length) {
  const char* reason = status == 200 ? "OK" : status == 400 ? "Bad Request"
                     : status == 401 ? "Unauthorized" : status == 403 ? "Forbidden"
                     : status == 404 ? "Not Found" : status == 409 ? "Conflict"
                     : status == 410 ? "Gone" : status == 411 ? "Length Required"
                     : status == 413 ? "Payload Too Large" : status == 429 ? "Too Many Requests"
                     : status == 431 ? "Request Header Fields Too Large"
                     : status == 507 ? "Insufficient Storage" : "Internal Server Error";
  char head[512];
  int n = snprintf(head, sizeof(head),
                   "HTTP/1.1 %d %s\r\n"
                   "Content-Type: %s\r\n"
                   "Content-Length: %zu\r\n"
                   "Cache-Control: no-store\r\n"
                   "X-Content-Type-Options: nosniff\r\n"
                   "X-Frame-Options: DENY\r\n"
                   "Referrer-Policy: no-referrer\r\n"
                   "Connection: close\r\n\r\n",
                   status, reason, type, length);
  if(send_all(fd, head, (size_t)n) == 0 && length) {
    send_all(fd, body, length);
  }
}

static void
reply_json(int fd, int status, const char* json) {
  reply(fd, status, "application/json", json, strlen(json));
}

// %XX decoded, in place; -1 for a 0 or a broken escape.
static int
decode(char* s) {
  char* out = s;
  for(char* p = s; *p; p++) {
    if(*p == '%') {
      int hi, lo;
      if(sscanf(p + 1, "%1x%1x", (unsigned*)&hi, (unsigned*)&lo) != 2) {
        return -1;
      }
      int c = hi << 4 | lo;
      if(!c) {
        return -1;
      }
      *out++ = (char)c;
      p += 2;
    } else {
      *out++ = *p;
    }
  }
  *out = 0;
  return 0;
}

// The value of name= in a query (decoded into out), or -1.
static int
query_value(const char* query, const char* name, char* out, size_t size) {
  size_t length = strlen(name);
  const char* p = query;

  while(p && *p) {
    if(!strncmp(p, name, length) && p[length] == '=') {
      const char* v = p + length + 1;
      size_t n = strcspn(v, "&");
      if(n >= size) {
        return -1;
      }
      memcpy(out, v, n);
      out[n] = 0;
      return decode(out);
    }
    p = strchr(p, '&');
    if(p) {
      p++;
    }
  }
  return -1;
}


// ---------------------------------------------------------------------------
// PINs and tokens.

void
phone_pin_open(char* out) {
  unsigned char r[4];
  random_bytes(r, sizeof(r));
  uint32_t n = ((uint32_t)r[0] << 24 | (uint32_t)r[1] << 16 | (uint32_t)r[2] << 8 | r[3])
             % 1000000u;
  pthread_mutex_lock(&lock);
  snprintf(pin, sizeof(pin), "%06u", (unsigned)n);
  pin_until = time(0) + PHONE_PIN_SECONDS;
  pin_tries = 0;
  memcpy(out, pin, 7);
  pthread_mutex_unlock(&lock);
}

void
phone_pin_close(void) {
  pthread_mutex_lock(&lock);
  pin[0] = 0;
  pthread_mutex_unlock(&lock);
}

int
phone_pin_seconds(void) {
  pthread_mutex_lock(&lock);
  long left = pin[0] ? (long)(pin_until - time(0)) : 0;
  pthread_mutex_unlock(&lock);
  return left > 0 ? (int)left : 0;
}

static int
count_locked(void) {
  int n = 0;
  for(int i = 0; i < PHONE_MAX; i++) {
    n += tokens[i][0] != 0;
  }
  return n;
}

int
phone_count(void) {
  pthread_mutex_lock(&lock);
  int n = count_locked();
  pthread_mutex_unlock(&lock);
  return n;
}

void
phone_forget(void) {
  pthread_mutex_lock(&lock);
  memset(tokens, 0, sizeof(tokens));
  memset(tickets, 0, sizeof(tickets));
  pthread_mutex_unlock(&lock);
}

// Which slot has this token (compared in full every time), or -1.
static int
token_slot(const char* token) {
  int found = -1;
  if(strlen(token) != 32) {
    return -1;
  }
  pthread_mutex_lock(&lock);
  for(int i = 0; i < PHONE_MAX; i++) {
    unsigned diff = tokens[i][0] == 0;
    for(int j = 0; j < 32; j++) {
      diff |= (unsigned char)(tokens[i][j] ^ token[j]);
    }
    if(!diff) {
      found = i;
    }
  }
  pthread_mutex_unlock(&lock);
  return found;
}

static void
drop_tickets_locked(int slot) {
  for(int i = 0; i < PHONE_TICKETS; i++) {
    if(tickets[i].id[0] && tickets[i].slot == slot) {
      memset(&tickets[i], 0, sizeof(tickets[i]));
    }
  }
}

// pair: the PIN in the body. 200 {"token"}, 403 {"error":"pin","left"},
// 410 {"error":"closed"} (none, out of time or used), 429 {"error":"tries"}.
static void
pair(int fd, const char* body) {
  char digits[16];
  size_t n = 0;
  for(const char* p = body; *p && n < sizeof(digits) - 1; p++) {
    if(*p >= '0' && *p <= '9') {
      digits[n++] = *p;
    }
  }
  digits[n] = 0;

  pthread_mutex_lock(&lock);
  if(!pin[0] || time(0) > pin_until) {
    pin[0] = 0;
    pthread_mutex_unlock(&lock);
    reply_json(fd, 410, "{\"error\":\"closed\"}");
    return;
  }
  unsigned diff = strlen(digits) != 6;
  for(int i = 0; i < 6 && digits[i]; i++) {
    diff |= (unsigned char)(digits[i] ^ pin[i]);
  }
  if(diff) {
    int left = PHONE_TRIES - ++pin_tries;
    if(left <= 0) {
      pin[0] = 0;
    }
    pthread_mutex_unlock(&lock);
    sleep(1);  // a little slower for anyone guessing
    char json[64];
    if(left <= 0) {
      reply_json(fd, 429, "{\"error\":\"tries\"}");
    } else {
      snprintf(json, sizeof(json), "{\"error\":\"pin\",\"left\":%d}", left);
      reply_json(fd, 403, json);
    }
    return;
  }
  unsigned char r[16];
  char token[33];
  random_bytes(r, sizeof(r));
  for(int i = 0; i < 16; i++) {
    snprintf(token + 2 * i, 3, "%02x", r[i]);
  }
  int slot = -1;
  for(int i = 0; i < PHONE_MAX && slot < 0; i++) {
    if(!tokens[i][0]) {
      slot = i;
    }
  }
  if(slot < 0) {  // full: the oldest one goes
    slot = token_next % PHONE_MAX;
  }
  token_next = slot + 1;
  memcpy(tokens[slot], token, 33);
  drop_tickets_locked(slot);  // a phone that had this slot before
  pin[0] = 0;  // one phone per PIN
  int count = count_locked();
  pthread_mutex_unlock(&lock);

  char json[64];
  snprintf(json, sizeof(json), "{\"token\":\"%s\"}", token);
  reply_json(fd, 200, json);
  if(calls.paired) {
    calls.paired(count, calls.ctx);
  }
}


// ---------------------------------------------------------------------------
// A file: the body, written to a .part beside where it goes, then renamed.

static void
take_file(int fd, const char* query, long long size, const char* have, size_t have_size) {
  char name[512], where[32], path[1024], part[1100];

  if(query_value(query, "name", name, sizeof(name)) || !name[0]) {
    reply_json(fd, 400, "{\"error\":\"name\"}");
    return;
  }
  if(query_value(query, "where", where, sizeof(where))) {
    snprintf(where, sizeof(where), "auto");
  }
  int no = calls.file_path ? calls.file_path(name, where, size, path, sizeof(path), calls.ctx)
                           : PHONE_NO_PLACE;
  if(no) {
    reply_json(fd, no == PHONE_NO_ROOM ? 507 : no == PHONE_NO_USB ? 409 : 403,
               no == PHONE_NO_ROOM ? "{\"error\":\"room\"}"
               : no == PHONE_NO_USB ? "{\"error\":\"usb\"}" : "{\"error\":\"place\"}");
    return;
  }
  snprintf(part, sizeof(part), "%s.part", path);
  int out = open(part, O_WRONLY | O_CREAT | O_EXCL, 0666);
  if(out < 0) {
    reply_json(fd, 403, "{\"error\":\"place\"}");
    if(calls.file_done) {
      calls.file_done(0, size, calls.ctx);
    }
    return;
  }
  char* chunk = (char*)malloc(CHUNK);
  long long got = 0;
  int ok = chunk != 0;
  if(ok && have_size) {
    size_t n = have_size > (unsigned long long)size ? (size_t)size : have_size;
    ok = write(out, have, n) == (ssize_t)n;
    got = (long long)n;
  }
  while(ok && got < size) {
    size_t want = size - got < CHUNK ? (size_t)(size - got) : CHUNK;
    ssize_t n = recv(fd, chunk, want, 0);
    if(n < 0 && errno == EINTR) {
      continue;
    }
    if(n <= 0) {  // the phone went away, or 15 s of nothing
      ok = 0;
      break;
    }
    ok = write(out, chunk, (size_t)n) == n;
    got += n;
  }
  free(chunk);
  ok &= close(out) == 0;
  if(ok && got == size && rename(part, path) == 0) {
    const char* base = strrchr(path, '/');
    char json[700];
    int n = snprintf(json, sizeof(json), "{\"saved\":\"");
    for(const char* p = base ? base + 1 : path; *p && n < (int)sizeof(json) - 8; p++) {
      if(*p == '"' || *p == '\\') {
        json[n++] = '\\';
      }
      json[n++] = (unsigned char)*p < 0x20 ? ' ' : *p;
    }
    snprintf(json + n, sizeof(json) - n, "\"}");
    reply_json(fd, 200, json);
    if(calls.file_done) {
      calls.file_done(path, size, calls.ctx);
    }
    return;
  }
  unlink(part);
  reply_json(fd, 500, "{\"error\":\"write\"}");
  if(calls.file_done) {
    calls.file_done(0, size, calls.ctx);
  }
}


// ---------------------------------------------------------------------------
// The console's files (files.h, docs/18): the places, a folder, a ticket for
// a file, the file itself on its own thread, removing, opening on the TV.

// s as a JSON string at out + n; the new length (cut short when it does not fit).
static size_t
json_string(char* out, size_t size, size_t n, const char* s) {
  if(n + 3 >= size) {
    return n;
  }
  out[n++] = '"';
  for(const unsigned char* p = (const unsigned char*)s; *p && n + 8 < size; p++) {
    if(*p == '"' || *p == '\\') {
      out[n++] = '\\';
      out[n++] = (char)*p;
    } else if(*p < 0x20) {
      n += (size_t)snprintf(out + n, size - n, "\\u%04x", *p);
    } else {
      out[n++] = (char)*p;
    }
  }
  out[n++] = '"';
  out[n] = 0;
  return n;
}

typedef struct place_lists {
  phone_place_t places[PHONE_PLACES];
  const char* roots[PHONE_PLACES];
  const char* hidden[PHONE_PLACES];
  int count, root_count, hidden_count;
} place_lists_t;

static void
places_get(place_lists_t* l) {
  memset(l, 0, sizeof(*l));
  l->count = calls.places ? calls.places(l->places, PHONE_PLACES, calls.ctx) : 0;
  if(l->count < 0 || l->count > PHONE_PLACES) {
    l->count = 0;
  }
  for(int i = 0; i < l->count; i++) {
    l->places[i].kind[sizeof(l->places[i].kind) - 1] = 0;
    l->places[i].path[sizeof(l->places[i].path) - 1] = 0;
    if(!strcmp(l->places[i].kind, "private")) {
      l->hidden[l->hidden_count++] = l->places[i].path;
    } else {
      l->roots[l->root_count++] = l->places[i].path;
    }
  }
}

// The real path of a path a phone sent (query's "path"), if it may see it:
// 0, or the status to answer (400 none, 403 not allowed or not there).
// is_root: whether it is one of the places themselves.
static int
phone_path(const char* query, char* real, size_t size, int* is_root) {
  char path[FILES_PATH_MAX];
  place_lists_t l;

  if(query_value(query, "path", path, sizeof(path)) || !path[0]) {
    return 400;
  }
  places_get(&l);
  int root = files_resolve(path, l.roots, l.root_count, l.hidden, l.hidden_count, real, size);
  if(root < 0) {
    return 403;
  }
  if(is_root) {
    char root_real[PATH_MAX];
    *is_root = !strcmp(path, l.roots[root])
            || (realpath(l.roots[root], root_real) && !strcmp(root_real, real));
  }
  return 0;
}

// files: the places ({"places": [{"kind", "path"}]}, those there now), or
// with ?path= one folder of them (files_list_json()).
static void
files_folder(int fd, const char* query) {
  char path[FILES_PATH_MAX], real[FILES_PATH_MAX];
  struct stat st;

  if(query_value(query, "path", path, sizeof(path)) || !path[0]) {
    place_lists_t l;
    char json[PHONE_PLACES * 1100 + 32];
    size_t n = (size_t)snprintf(json, sizeof(json), "{\"places\": [");
    int first = 1;
    places_get(&l);
    for(int i = 0; i < l.count; i++) {
      if(!strcmp(l.places[i].kind, "private") || stat(l.places[i].path, &st)
         || !S_ISDIR(st.st_mode)) {
        continue;
      }
      n += (size_t)snprintf(json + n, sizeof(json) - n, "%s{\"kind\": ", first ? "" : ", ");
      n = json_string(json, sizeof(json), n, l.places[i].kind);
      n += (size_t)snprintf(json + n, sizeof(json) - n, ", \"path\": ");
      n = json_string(json, sizeof(json), n, l.places[i].path);
      n += (size_t)snprintf(json + n, sizeof(json) - n, "}");
      first = 0;
    }
    snprintf(json + n, sizeof(json) - n, "]}");
    reply_json(fd, 200, json);
    return;
  }
  int no = phone_path(query, real, sizeof(real), 0);
  if(no) {
    reply_json(fd, no, "{\"error\":\"place\"}");
    return;
  }
  if(stat(real, &st) || !S_ISDIR(st.st_mode)) {
    reply_json(fd, 400, "{\"error\":\"folder\"}");
    return;
  }
  place_lists_t l;
  places_get(&l);
  char* json = files_list_json(real, l.hidden, l.hidden_count);
  reply_json(fd, json ? 200 : 500, json ? json : "{\"error\":\"read\"}");
  free(json);
}

// ticket: for one file (?path=), {"ticket"}; /api/get?t= then sends it.
static void
files_ticket(int fd, const char* query, int slot) {
  char real[FILES_PATH_MAX];
  struct stat st;

  int no = phone_path(query, real, sizeof(real), 0);
  if(no || stat(real, &st) || !S_ISREG(st.st_mode)) {
    reply_json(fd, no ? no : 400, "{\"error\":\"file\"}");
    return;
  }
  unsigned char r[16];
  char id[33];
  random_bytes(r, sizeof(r));
  for(int i = 0; i < 16; i++) {
    snprintf(id + 2 * i, 3, "%02x", r[i]);
  }
  pthread_mutex_lock(&lock);
  time_t now = time(0);
  int at = -1;
  for(int i = 0; i < PHONE_TICKETS && at < 0; i++) {  // a free one, or one out of time
    if(!tickets[i].id[0] || tickets[i].until < now) {
      at = i;
    }
  }
  if(at < 0) {  // all in use: the oldest one goes
    at = ticket_next % PHONE_TICKETS;
  }
  ticket_next = at + 1;
  memcpy(tickets[at].id, id, 33);
  snprintf(tickets[at].path, sizeof(tickets[at].path), "%s", real);
  tickets[at].until = now + PHONE_TICKET_SECONDS;
  tickets[at].slot = slot;
  pthread_mutex_unlock(&lock);

  char json[64];
  snprintf(json, sizeof(json), "{\"ticket\":\"%s\"}", id);
  reply_json(fd, 200, json);
}

typedef struct sender {
  int fd;
  int head;            // HEAD: only what it is
  char path[FILES_PATH_MAX];
  char range[64];
} sender_t;

static void*
send_file(void* arg) {
  sender_t* s = (sender_t*)arg;
  struct stat st;
  char head[1700], disposition[1200];

  if(calls.sending) {
    calls.sending(calls.ctx);
  }
  struct timeval tv = {60, 0};  // a phone's player stops reading while it has enough
  setsockopt(s->fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
  int in = open(s->path, O_RDONLY);
  if(in < 0 || fstat(in, &st) || !S_ISREG(st.st_mode)) {
    reply_json(s->fd, 404, "{\"error\":\"file\"}");
  } else {
    long long size = (long long)st.st_size, from = 0, to = size - 1;
    int whole = files_range(s->range[0] ? s->range : 0, size, &from, &to);
    if(whole < 0) {
      int n = snprintf(head, sizeof(head),
                       "HTTP/1.1 416 Range Not Satisfiable\r\n"
                       "Content-Range: bytes */%lld\r\n"
                       "Content-Length: 0\r\n"
                       "Connection: close\r\n\r\n", size);
      send_all(s->fd, head, (size_t)n);
    } else {
      if(whole) {
        from = 0;
        to = size - 1;
      }
      long long length = size ? to - from + 1 : 0;
      char content_range[96] = "";
      if(!whole) {
        snprintf(content_range, sizeof(content_range), "Content-Range: bytes %lld-%lld/%lld\r\n",
                 from, to, size);
      }
      files_disposition(s->path, disposition, sizeof(disposition));
      // Never shown as a page of this server: always a download when opened
      // (a <video> or <img> takes it all the same), with no scripts.
      int n = snprintf(head, sizeof(head),
                       "HTTP/1.1 %s\r\n"
                       "Content-Type: %s\r\n"
                       "Content-Length: %lld\r\n"
                       "%s"
                       "Accept-Ranges: bytes\r\n"
                       "Content-Disposition: %s\r\n"
                       "Cache-Control: no-store\r\n"
                       "X-Content-Type-Options: nosniff\r\n"
                       "Content-Security-Policy: sandbox\r\n"
                       "Referrer-Policy: no-referrer\r\n"
                       "Connection: close\r\n\r\n",
                       whole ? "200 OK" : "206 Partial Content", files_type(s->path), length,
                       content_range, disposition);
      int ok = n > 0 && (size_t)n < sizeof(head) && send_all(s->fd, head, (size_t)n) == 0;
      char* chunk = ok && !s->head && length ? (char*)malloc(CHUNK) : 0;
      if(chunk && lseek(in, (off_t)from, SEEK_SET) == (off_t)from) {
        long long left = length, since_rest = 0;
        while(left > 0) {
          ssize_t got = read(in, chunk, left < CHUNK ? (size_t)left : CHUNK);
          if(got < 0 && errno == EINTR) {
            continue;
          }
          if(got <= 0 || send_all(s->fd, chunk, (size_t)got)) {
            break;  // the file shrank, or the phone went away
          }
          left -= got;
          since_rest += got;
          if(since_rest >= 1024 * 1024) {  // a little rest after each MiB
            since_rest = 0;
            usleep(1000);
          }
        }
      }
      free(chunk);
    }
  }
  if(in >= 0) {
    close(in);
  }
  shutdown(s->fd, SHUT_WR);
  close(s->fd);
  pthread_mutex_lock(&lock);
  senders--;
  pthread_mutex_unlock(&lock);
  free(s);
  return 0;
}

// get?t=: the ticket's file, on a thread of its own; 1 when the connection
// went to it (it closes it), 0 when it was answered here.
static int
files_get(int fd, const char* query, int head, const char* range) {
  char id[40];
  if(query_value(query, "t", id, sizeof(id)) || strlen(id) != 32) {
    reply_json(fd, 404, "{\"error\":\"ticket\"}");
    return 0;
  }
  sender_t* s = (sender_t*)calloc(1, sizeof(sender_t));
  if(!s) {
    reply_json(fd, 500, "{\"error\":\"memory\"}");
    return 0;
  }
  pthread_mutex_lock(&lock);
  time_t now = time(0);
  int found = -1;
  for(int i = 0; i < PHONE_TICKETS; i++) {
    unsigned diff = !tickets[i].id[0] || tickets[i].until < now;
    for(int j = 0; j < 32; j++) {
      diff |= (unsigned char)(tickets[i].id[j] ^ id[j]);
    }
    if(!diff) {
      found = i;
    }
  }
  int busy = senders >= PHONE_SENDS;
  if(found >= 0 && !busy) {
    tickets[found].until = now + PHONE_TICKET_SECONDS;
    snprintf(s->path, sizeof(s->path), "%s", tickets[found].path);
    senders++;
  }
  pthread_mutex_unlock(&lock);
  if(found < 0 || busy) {
    free(s);
    reply_json(fd, found < 0 ? 404 : 503, found < 0 ? "{\"error\":\"ticket\"}" : "{\"error\":\"busy\"}");
    return 0;
  }
  s->fd = fd;
  s->head = head;
  snprintf(s->range, sizeof(s->range), "%s", range);

  pthread_attr_t attr;
  pthread_t thread;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 256 * 1024);  // a PS5 thread gets 64 KiB without
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  int failed = pthread_create(&thread, &attr, send_file, s);
  pthread_attr_destroy(&attr);
  if(failed) {
    pthread_mutex_lock(&lock);
    senders--;
    pthread_mutex_unlock(&lock);
    free(s);
    reply_json(fd, 503, "{\"error\":\"busy\"}");
    return 0;
  }
  return 1;
}

// remove?path=: a file, or a folder with nothing in it; never a place itself.
static void
files_remove_one(int fd, const char* query) {
  char real[FILES_PATH_MAX], path[FILES_PATH_MAX];
  int is_root = 0;

  int no = phone_path(query, real, sizeof(real), &is_root);
  if(no || is_root) {
    reply_json(fd, no ? no : 403, "{\"error\":\"place\"}");
    return;
  }
  query_value(query, "path", path, sizeof(path));
  int err = files_remove(path);  // the path itself: a link goes nowhere (EPERM)
  reply_json(fd, !err ? 200 : err == ENOTEMPTY ? 409 : err == ENOENT ? 404 : 403,
             !err ? "{}" : err == ENOTEMPTY ? "{\"error\":\"full\"}" : "{\"error\":\"file\"}");
}

// show?path=: a video, sound or picture opens on the TV.
static void
files_show(int fd, const char* query) {
  char real[FILES_PATH_MAX];
  struct stat st;

  int no = phone_path(query, real, sizeof(real), 0);
  if(no || stat(real, &st) || !S_ISREG(st.st_mode) || !files_media(real)) {
    reply_json(fd, no ? no : 400, "{\"error\":\"file\"}");
    return;
  }
  reply_json(fd, 200, "{}");
  if(calls.show) {
    calls.show(real, calls.ctx);
  }
}


// ---------------------------------------------------------------------------
// One request.

static int
serve(int fd) {
  char head[HEAD_MAX + 1];
  size_t have = 0;
  char* end = 0;

  while(!end) {
    if(have == HEAD_MAX) {
      reply_json(fd, 431, "{\"error\":\"head\"}");
      return 0;
    }
    ssize_t n = recv(fd, head + have, HEAD_MAX - have, 0);
    if(n < 0 && errno == EINTR) {
      continue;
    }
    if(n <= 0) {
      return 0;
    }
    have += (size_t)n;
    head[have] = 0;
    end = strstr(head, "\r\n\r\n");
  }
  *end = 0;
  const char* body_have = end + 4;
  size_t body_have_size = have - (size_t)(body_have - head);

  // The request line.
  char* target = strchr(head, ' ');
  char* version = target ? strchr(target + 1, ' ') : 0;
  if(!target || !version) {
    reply_json(fd, 400, "{\"error\":\"request\"}");
    return 0;
  }
  *target++ = 0;
  *version = 0;
  const char* method = head;
  char* query = strchr(target, '?');
  if(query) {
    *query++ = 0;
  } else {
    query = (char*)"";
  }

  // The headers that matter.
  long long length = -1;
  char token[40] = "";
  char range[64] = "";
  for(char* line = strstr(version + 1, "\r\n"); line; line = strstr(line, "\r\n")) {
    line += 2;
    char* colon = strchr(line, ':');
    char* next = strstr(line, "\r\n");
    if(!colon || (next && colon > next)) {
      continue;
    }
    const char* value = colon + 1;
    while(*value == ' ' || *value == '\t') {
      value++;
    }
    size_t value_length = next ? (size_t)(next - value) : strlen(value);
    if(!strncasecmp(line, "Content-Length:", 15)) {
      char* stop;
      length = strtoll(value, &stop, 10);
      if(stop == value || length < 0) {
        length = -2;
      }
    } else if(!strncasecmp(line, "X-Token:", 8) && value_length < sizeof(token)) {
      memcpy(token, value, value_length);
      token[value_length] = 0;
    } else if(!strncasecmp(line, "Range:", 6) && value_length < sizeof(range)) {
      memcpy(range, value, value_length);
      range[value_length] = 0;
    } else if(!strncasecmp(line, "Transfer-Encoding:", 18)) {
      length = -2;  // chunked bodies are not taken
    }
  }
  if(length == -2) {
    reply_json(fd, 400, "{\"error\":\"length\"}");
    return 0;
  }

  int get = !strcmp(method, "GET"), post = !strcmp(method, "POST"), head_only = !strcmp(method, "HEAD");
  if(get && (!strcmp(target, "/") || !strcmp(target, "/index.html"))) {
    reply(fd, 200, "text/html; charset=utf-8", page_html, strlen(page_html));
    return 0;
  }
  if(strncmp(target, "/api/", 5) || (!get && !post && !(head_only && !strcmp(target, "/api/get")))) {
    reply_json(fd, 404, "{\"error\":\"none\"}");
    return 0;
  }
  const char* api = target + 5;

  // A file a phone fetches by its ticket alone (an <a>, a <video>, an <img>).
  if(!strcmp(api, "get")) {
    if(post) {
      reply_json(fd, 404, "{\"error\":\"none\"}");
      return 0;
    }
    return files_get(fd, query, head_only, range);
  }

  // Every call but pair needs a token.
  int slot = -1;
  if(strcmp(api, "pair")) {
    slot = token_slot(token);
    if(slot < 0) {
      reply_json(fd, 401, "{\"error\":\"token\"}");
      return 0;
    }
  }

  if(post && !strcmp(api, "file")) {
    if(length < 0) {
      reply_json(fd, 411, "{\"error\":\"length\"}");
      return 0;
    }
    take_file(fd, query, length, body_have, body_have_size);
    return 0;
  }

  // The rest have a short body, or none.
  char body[PHONE_TEXT_MAX + 1];
  size_t size = 0;
  if(post) {
    if(length < 0) {
      reply_json(fd, 411, "{\"error\":\"length\"}");
      return 0;
    }
    if(length > PHONE_TEXT_MAX) {
      reply_json(fd, 413, "{\"error\":\"size\"}");
      return 0;
    }
    size = body_have_size < (size_t)length ? body_have_size : (size_t)length;
    memcpy(body, body_have, size);
    while(size < (size_t)length) {
      ssize_t n = recv(fd, body + size, (size_t)length - size, 0);
      if(n < 0 && errno == EINTR) {
        continue;
      }
      if(n <= 0) {
        return 0;
      }
      size += (size_t)n;
    }
  }
  body[size] = 0;
  if(memchr(body, 0, size)) {
    reply_json(fd, 400, "{\"error\":\"text\"}");
    return 0;
  }

  if(post && !strcmp(api, "pair")) {
    pair(fd, body);
  } else if(get && !strcmp(api, "state")) {
    char json[4096];
    size_t n = calls.state ? calls.state(json, sizeof(json), calls.ctx) : 0;
    if(!n || n >= sizeof(json)) {
      snprintf(json, sizeof(json), "{}");
    }
    reply_json(fd, 200, json);
  } else if(get && !strcmp(api, "downloads")) {
    char* json = (char*)malloc(PHONE_DOWNLOADS_MAX);
    size_t n = json && calls.downloads ? calls.downloads(json, PHONE_DOWNLOADS_MAX, calls.ctx) : 0;
    reply_json(fd, 200, n && n < PHONE_DOWNLOADS_MAX ? json : "{\"downloads\": []}");
    free(json);
  } else if(post && !strcmp(api, "download")) {
    char action[12], id_text[12];
    int id_ok = !query_value(query, "id", id_text, sizeof(id_text)) && id_text[0];
    for(const char* p = id_text; id_ok && *p; p++) {
      id_ok = *p >= '0' && *p <= '9';
    }
    if(query_value(query, "do", action, sizeof(action)) || !phone_download_action_known(action)
       || !id_ok || atoi(id_text) <= 0) {
      reply_json(fd, 400, "{\"error\":\"download\"}");
      return 0;
    }
    reply_json(fd, 200, "{}");
    if(calls.download) {
      calls.download(action, atoi(id_text), calls.ctx);
    }
  } else if(get && !strcmp(api, "room")) {  // before a file: whether it fits
    char name[512], where[32], size_text[24], path[1024];
    if(query_value(query, "name", name, sizeof(name)) || !name[0]
       || query_value(query, "size", size_text, sizeof(size_text))) {
      reply_json(fd, 400, "{\"error\":\"name\"}");
      return 0;
    }
    if(query_value(query, "where", where, sizeof(where))) {
      snprintf(where, sizeof(where), "auto");
    }
    int no = calls.file_path ? calls.file_path(name, where, atoll(size_text), path,
                                               sizeof(path), calls.ctx) : PHONE_NO_PLACE;
    reply_json(fd, no == 0 ? 200 : no == PHONE_NO_ROOM ? 507 : no == PHONE_NO_USB ? 409 : 403,
               no == 0 ? "{}" : no == PHONE_NO_ROOM ? "{\"error\":\"room\"}"
               : no == PHONE_NO_USB ? "{\"error\":\"usb\"}" : "{\"error\":\"place\"}");
  } else if(post && !strcmp(api, "open")) {
    if(!size) {
      reply_json(fd, 400, "{\"error\":\"text\"}");
      return 0;
    }
    reply_json(fd, 200, "{}");
    if(calls.open) {
      calls.open(body, calls.ctx);
    }
  } else if(post && !strcmp(api, "type")) {
    char enter[4];
    int with_enter = !query_value(query, "enter", enter, sizeof(enter)) && enter[0] == '1';
    reply_json(fd, 200, "{}");
    if(calls.type) {
      calls.type(body, with_enter, calls.ctx);
    }
  } else if(post && !strcmp(api, "key")) {
    char name[20];
    if(query_value(query, "k", name, sizeof(name)) || !phone_key_known(name)) {
      reply_json(fd, 400, "{\"error\":\"key\"}");
      return 0;
    }
    reply_json(fd, 200, "{}");
    if(calls.key) {
      calls.key(name, calls.ctx);
    }
  } else if(post && !strcmp(api, "forget")) {
    pthread_mutex_lock(&lock);
    tokens[slot][0] = 0;
    drop_tickets_locked(slot);
    int count = count_locked();
    pthread_mutex_unlock(&lock);
    reply_json(fd, 200, "{}");
    if(calls.paired) {
      calls.paired(count, calls.ctx);
    }
  } else if(get && !strcmp(api, "files")) {
    files_folder(fd, query);
  } else if(post && !strcmp(api, "ticket")) {
    files_ticket(fd, query, slot);
  } else if(post && !strcmp(api, "remove")) {
    files_remove_one(fd, query);
  } else if(post && !strcmp(api, "show")) {
    files_show(fd, query);
  } else if(get && !strcmp(api, "game") && calls.game) {  // the card "O teu jogo" (RN)
    char id[16];
    if(query_value(query, "id", id, sizeof(id))) {
      id[0] = 0;
    }
    if(id[0] && !phone_game_id_ok(id)) {
      reply_json(fd, 400, "{\"error\":\"game\"}");
      return 0;
    }
    char* json = (char*)malloc(PHONE_GAME_MAX);
    size_t n = json ? calls.game(id, json, PHONE_GAME_MAX, calls.ctx) : 0;
    if(n && n < PHONE_GAME_MAX) {
      json[n] = 0;
      reply_json(fd, 200, json);
    } else {
      reply_json(fd, 404, "{\"error\":\"none\"}");
    }
    free(json);
  } else if(post && !strcmp(api, "game") && calls.game_do) {
    char id[16], action[12];
    if(query_value(query, "id", id, sizeof(id)) || !phone_game_id_ok(id)
       || query_value(query, "do", action, sizeof(action)) || !phone_game_action_known(action)) {
      reply_json(fd, 400, "{\"error\":\"game\"}");
      return 0;
    }
    reply_json(fd, 200, "{}");
    calls.game_do(action, id, body, calls.ctx);
  } else if(get && !strcmp(api, "discord") && calls.discord) {  // the card "Discord" (RN): kept or not, its start
    char json[256];
    const size_t n = calls.discord(json, sizeof(json), calls.ctx);
    if(n && n < sizeof(json)) {
      json[n] = 0;
      reply_json(fd, 200, json);
    } else {
      reply_json(fd, 404, "{\"error\":\"none\"}");
    }
  } else if(post && !strcmp(api, "discord") && calls.discord_set) {  // body: a webhook's address, or "" to forget it
    const int ok = strlen(body) < PHONE_HOOK_MAX && calls.discord_set(body, calls.ctx);
    reply_json(fd, ok ? 200 : 400, ok ? "{}" : "{\"error\":\"webhook\"}");
  } else {
    reply_json(fd, 404, "{\"error\":\"none\"}");
  }
  return 0;
}


// ---------------------------------------------------------------------------
// The thread.

static void*
server(void* arg) {
  (void)arg;
  while(running) {
    struct sockaddr_in from;
    socklen_t from_size = sizeof(from);
    int fd = accept(listener, (struct sockaddr*)&from, &from_size);
    if(fd < 0) {
      if(errno != EINTR && running) {
        usleep(200 * 1000);  // out of descriptors or the like: no spinning
      }
      continue;
    }
    if(!running) {
      close(fd);
      break;
    }
    if(from.sin_family != AF_INET
       || !phone_address_allowed(ntohl(from.sin_addr.s_addr))) {
      close(fd);
      continue;
    }
    struct timeval tv = {TIMEOUT_S, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#ifdef SO_NOSIGPIPE
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
    if(serve(fd)) {
      continue;  // a file going to a phone: its thread closes it
    }
    // A clean end: a body left unread (a file refused) would otherwise make
    // the close a reset, and the phone might lose the answer before it.
    shutdown(fd, SHUT_WR);
    struct timeval brief = {1, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &brief, sizeof(brief));
    char sink[4096];
    for(size_t drained = 0; drained < 1024 * 1024;) {
      ssize_t n = recv(fd, sink, sizeof(sink), 0);
      if(n <= 0) {
        break;
      }
      drained += (size_t)n;
    }
    close(fd);
  }
  close(listener);
  listener = -1;
  return 0;
}

int
phone_start(const phone_calls_t* c, const char* page) {
  if(running) {
    return port;
  }
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if(fd < 0) {
    return -1;
  }
  int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  int chosen = -1;
  for(int p = PHONE_PORT; p < PHONE_PORT + 5 && chosen < 0; p++) {
    struct sockaddr_in at;
    memset(&at, 0, sizeof(at));
    at.sin_family = AF_INET;
    at.sin_port = htons((uint16_t)p);
    at.sin_addr.s_addr = htonl(INADDR_ANY);
    if(!bind(fd, (struct sockaddr*)&at, sizeof(at))) {
      chosen = p;
    }
  }
  if(chosen < 0 || listen(fd, 8)) {
    close(fd);
    return -1;
  }
  calls = *c;
  page_html = page;
  listener = fd;
  port = chosen;
  running = 1;

  pthread_attr_t attr;
  pthread_t thread;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 512 * 1024);  // a PS5 thread gets 64 KiB without
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  int failed = pthread_create(&thread, &attr, server, 0);
  pthread_attr_destroy(&attr);
  if(failed) {
    running = 0;
    close(fd);
    listener = -1;
    return -1;
  }
  return port;
}

// The thread sleeps in accept(): a connection of our own wakes it.
void
phone_stop(void) {
  if(!running) {
    return;
  }
  running = 0;
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if(fd >= 0) {
    struct sockaddr_in to;
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons((uint16_t)port);
    to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    connect(fd, (struct sockaddr*)&to, sizeof(to));
    close(fd);
  }
}

int
phone_port(void) {
  return running ? port : 0;
}

// The address the system would send from to the internet (UDP connect()
// sends nothing): the one a phone on the same network reaches.
int
phone_local_address(char* ip, size_t size) {
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if(fd < 0) {
    return -1;
  }
  struct sockaddr_in to, me;
  socklen_t me_size = sizeof(me);
  memset(&to, 0, sizeof(to));
  to.sin_family = AF_INET;
  to.sin_port = htons(53);
  to.sin_addr.s_addr = htonl(0x01010101);
  int ok = !connect(fd, (struct sockaddr*)&to, sizeof(to))
        && !getsockname(fd, (struct sockaddr*)&me, &me_size) && me.sin_addr.s_addr;
  close(fd);
  if(!ok || !inet_ntop(AF_INET, &me.sin_addr, ip, (socklen_t)size)) {
    return -1;
  }
  return 0;
}
