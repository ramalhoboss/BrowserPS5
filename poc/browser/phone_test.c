/* Copyright (C) 2026 BrowserPS5 contributors. GPL-3.0-or-later.
   phone.c on the PC (make test): a real server on 127.0.0.1 and requests as
   a phone would send them. */

#include "phone.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

static int failures;
static char dir[256];
static char last_open[PHONE_TEXT_MAX + 1], last_type[PHONE_TEXT_MAX + 1], last_key[32];
static int last_enter, paired_count = -1, files_done, files_failed;
static char last_file[1024];

#define CHECK(c, ...) do { if(!(c)) { failures++; printf("FAIL line %d: ", __LINE__); \
  printf(__VA_ARGS__); printf("\n"); } } while(0)

static void on_open(const char* t, void* c) { (void)c; snprintf(last_open, sizeof(last_open), "%s", t); }
static void on_type(const char* t, int e, void* c) {
  (void)c; snprintf(last_type, sizeof(last_type), "%s", t); last_enter = e; }
static void on_key(const char* k, void* c) { (void)c; snprintf(last_key, sizeof(last_key), "%s", k); }
static int on_file_path(const char* name, const char* where, long long size, char* path,
                        size_t path_size, void* c) {
  (void)c;
  if(!strcmp(where, "usb")) return PHONE_NO_USB;
  if(size > 64LL * 1024 * 1024) return PHONE_NO_ROOM;
  if(strchr(name, '/')) return PHONE_NO_PLACE;
  snprintf(path, path_size, "%s/%s", dir, name);
  return 0;
}
static void on_file_done(const char* path, long long size, void* c) {
  (void)c; (void)size;
  if(path) { files_done++; snprintf(last_file, sizeof(last_file), "%s", path); }
  else files_failed++;
}
static void on_paired(int n, void* c) { (void)c; paired_count = n; }
static size_t on_state(char* json, size_t size, void* c) {
  (void)c; return (size_t)snprintf(json, size, "{\"title\":\"Teste\",\"url\":\"https://a.pt/\"}");
}
static int downloads_empty;
static size_t on_downloads(char* json, size_t size, void* c) {
  (void)c;
  if(downloads_empty) return 0;
  return (size_t)snprintf(json, size, "{\"downloads\": [{\"id\": 3, \"name\": \"a.zip\", \"state\": \"done\"}]}");
}
static char last_action[16];
static int last_id;
static void on_download(const char* a, int id, void* c) {
  (void)c; snprintf(last_action, sizeof(last_action), "%s", a); last_id = id; }
static char files_dir[256], last_show[1100];
static int sending_count;
static int on_places(phone_place_t* places, int max, void* c) {
  (void)c;
  if(max < 3) return 0;
  snprintf(places[0].kind, sizeof(places[0].kind), "downloads");
  snprintf(places[0].path, sizeof(places[0].path), "%s/down", files_dir);
  snprintf(places[1].kind, sizeof(places[1].kind), "usb");
  snprintf(places[1].path, sizeof(places[1].path), "%s/usb0", files_dir);  // not there
  snprintf(places[2].kind, sizeof(places[2].kind), "private");
  snprintf(places[2].path, sizeof(places[2].path), "%s/down/private", files_dir);
  return 3;
}
static void on_show(const char* path, void* c) { (void)c; snprintf(last_show, sizeof(last_show), "%s", path); }
static void on_sending(void* c) { (void)c; sending_count++; }
static char last_game_id[16], last_game_action[12], last_game_body[PHONE_TEXT_MAX + 1];
static int game_none;
static size_t on_game(const char* id, char* json, size_t size, void* c) {
  (void)c; snprintf(last_game_id, sizeof(last_game_id), "%s", id);
  if(game_none) return 0;
  return (size_t)snprintf(json, size, "{\"games\": [{\"id\": \"PPSA01341\", \"name\": \"Demon's Souls\", \"open\": 1}],"
                          " \"id\": \"PPSA01341\", \"notes\": \"\", \"pages\": []}");
}
static void on_game_do(const char* a, const char* id, const char* body, void* c) {
  (void)c; snprintf(last_game_action, sizeof(last_game_action), "%s", a);
  snprintf(last_game_id, sizeof(last_game_id), "%s", id); snprintf(last_game_body, sizeof(last_game_body), "%s", body); }
static char last_hook[PHONE_TEXT_MAX + 1];
static int hook_sets;
static size_t on_discord(char* json, size_t size, void* c) {
  (void)c; return (size_t)snprintf(json, size, "{\"set\": %d, \"where\": \"%s\"}", last_hook[0] ? 1 : 0,
                                   last_hook[0] ? "discord.com/api/webhooks/1234..." : ""); }
static int on_discord_set(const char* url, void* c) {
  (void)c; hook_sets++;
  if(url[0] && strncmp(url, "https://discord.com/api/webhooks/", 33)) return 0;
  snprintf(last_hook, sizeof(last_hook), "%s", url); return 1; }

// A request sent as it is; the status, and the whole answer (head and body).
static int
raw(const char* text, char* out, size_t out_size) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in to;
  memset(&to, 0, sizeof(to));
  to.sin_family = AF_INET;
  to.sin_port = htons((uint16_t)phone_port());
  to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  struct timeval tv = {20, 0};
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  if(connect(fd, (struct sockaddr*)&to, sizeof(to))) {
    close(fd);
    return -1;
  }
  send(fd, text, strlen(text), MSG_NOSIGNAL);
  size_t have = 0;
  for(;;) {
    ssize_t k = recv(fd, out + have, out_size - 1 - have, 0);
    if(k <= 0) break;
    have += k;
  }
  out[have] = 0;
  close(fd);
  int status = -1;
  sscanf(out, "HTTP/1.1 %d", &status);
  return status;
}
static void
put_file(const char* rel, const char* text) {
  char path[600];
  snprintf(path, sizeof(path), "%s/%s", files_dir, rel);
  FILE* f = fopen(path, "w");
  if(f) { fputs(text, f); fclose(f); }
}
static void
put_dir(const char* rel) {
  char path[600];
  snprintf(path, sizeof(path), "%s/%s", files_dir, rel);
  mkdir(path, 0777);
}
static void
take(const char* rel) {
  char path[600];
  snprintf(path, sizeof(path), "%s/%s", files_dir, rel);
  if(remove(path)) rmdir(path);
}

// A request; the status, and the body into out.
static int
request(const char* head_fmt, const void* body, size_t body_size, size_t declared,
        char* out, size_t out_size, ...) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in to;
  memset(&to, 0, sizeof(to));
  to.sin_family = AF_INET;
  to.sin_port = htons((uint16_t)phone_port());
  to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  struct timeval tv = {20, 0};
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  if(connect(fd, (struct sockaddr*)&to, sizeof(to))) {
    close(fd);
    return -1;
  }
  char head[2048];
  va_list ap;
  va_start(ap, out_size);
  int n = vsnprintf(head, sizeof(head), head_fmt, ap);
  va_end(ap);
  if(body) {
    n += snprintf(head + n, sizeof(head) - n, "Content-Length: %zu\r\n", declared);
  }
  n += snprintf(head + n, sizeof(head) - n, "\r\n");
  send(fd, head, n, MSG_NOSIGNAL);
  if(body_size) {
    const char* p = body;
    size_t left = body_size;
    while(left) {
      ssize_t k = send(fd, p, left, MSG_NOSIGNAL);
      if(k <= 0) break;
      p += k;
      left -= k;
    }
  }
  if(declared > body_size) {  // a phone that goes away in the middle
    shutdown(fd, SHUT_WR);
  }
  static char reply[1 << 20];
  size_t have = 0;
  for(;;) {
    ssize_t k = recv(fd, reply + have, sizeof(reply) - 1 - have, 0);
    if(k <= 0) break;
    have += k;
  }
  reply[have] = 0;
  close(fd);
  int status = -1;
  sscanf(reply, "HTTP/1.1 %d", &status);
  char* b = strstr(reply, "\r\n\r\n");
  snprintf(out, out_size, "%s", b ? b + 4 : "");
  return status;
}

#define GET(path, out, ...) request("GET " path " HTTP/1.1\r\nHost: x\r\n" __VA_ARGS__, 0, 0, 0, out, sizeof(out))
static int
post(const char* path, const char* token, const char* body, char* out, size_t out_size) {
  char head[512];
  snprintf(head, sizeof(head), "POST %s HTTP/1.1\r\nHost: x\r\n%s%s%s", path,
           token ? "X-Token: " : "", token ? token : "", token ? "\r\n" : "");
  return request("%s", body, strlen(body), strlen(body), out, out_size, head);
}

int
main(void) {
  char out[8192], token[40], token2[40], pin[8];
  int status;

  snprintf(dir, sizeof(dir), "/tmp/phone_testXXXXXX");
  if(!mkdtemp(dir)) {
    perror("mkdtemp");
    return 1;
  }

  // Addresses and keys.
  CHECK(phone_address_allowed(0xc0a80149), "192.168.1.51");
  CHECK(phone_address_allowed(0x0a000001), "10.0.0.1");
  CHECK(phone_address_allowed(0xac1f0001), "172.31.0.1");
  CHECK(!phone_address_allowed(0xac200001), "172.32.0.1 is not local");
  CHECK(phone_address_allowed(0x7f000001), "127.0.0.1");
  CHECK(!phone_address_allowed(0x08080808), "8.8.8.8 is not local");
  CHECK(!phone_address_allowed(0x64400001), "100.64.0.1 is not local");
  CHECK(phone_key_known("back") && phone_key_known("sidecard") && !phone_key_known("rm") && !phone_key_known(""), "keys");

  phone_calls_t calls = {on_open, on_type, on_key, on_file_path, on_file_done, on_paired,
                         on_state, on_downloads, on_download, on_places, on_show,
                         on_sending, 0};
  calls.game = on_game;
  calls.game_do = on_game_do;
  calls.discord = on_discord;
  calls.discord_set = on_discord_set;
  int port = phone_start(&calls, "<!doctype html><title>Telemóvel</title>");
  CHECK(port >= PHONE_PORT && port < PHONE_PORT + 5, "port %d", port);
  CHECK(phone_start(&calls, "x") == port, "a second start is the same server");

  status = GET("/", out);
  CHECK(status == 200 && strstr(out, "Telemóvel"), "the page: %d", status);
  status = GET("/nothing", out);
  CHECK(status == 404, "404: %d", status);
  status = GET("/api/state", out);
  CHECK(status == 401, "no token: %d", status);
  status = GET("/api/state", out, "X-Token: 0123456789abcdef0123456789abcdef\r\n");
  CHECK(status == 401, "a made-up token: %d", status);

  // No PIN yet.
  status = post("/api/pair", 0, "123456", out, sizeof(out));
  CHECK(status == 410 && strstr(out, "closed"), "no PIN: %d %s", status, out);

  // Wrong PINs, then too many.
  phone_pin_open(pin);
  CHECK(strlen(pin) == 6 && phone_pin_seconds() > PHONE_PIN_SECONDS - 3, "pin %s", pin);
  char wrong[8];
  snprintf(wrong, sizeof(wrong), "%06d", (atoi(pin) + 1) % 1000000);
  status = post("/api/pair", 0, wrong, out, sizeof(out));
  CHECK(status == 403 && strstr(out, "\"left\":4"), "wrong PIN: %d %s", status, out);
  for(int i = 0; i < 3; i++) {
    post("/api/pair", 0, wrong, out, sizeof(out));
  }
  status = post("/api/pair", 0, wrong, out, sizeof(out));
  CHECK(status == 429, "too many: %d %s", status, out);
  status = post("/api/pair", 0, pin, out, sizeof(out));
  CHECK(status == 410, "the right PIN after too many: %d %s", status, out);
  CHECK(phone_pin_seconds() == 0, "closed");

  // The right one.
  phone_pin_open(pin);
  char spaced[16];
  snprintf(spaced, sizeof(spaced), "%.3s %.3s", pin, pin + 3);
  status = post("/api/pair", 0, spaced, out, sizeof(out));
  CHECK(status == 200 && sscanf(out, "{\"token\":\"%32[0-9a-f]\"}", token) == 1,
        "pair: %d %s", status, out);
  CHECK(paired_count == 1 && phone_count() == 1, "count %d", paired_count);
  status = post("/api/pair", 0, pin, out, sizeof(out));
  CHECK(status == 410, "a PIN is for one phone: %d", status);

  char auth[64];
  snprintf(auth, sizeof(auth), "X-Token: %s\r\n", token);
  status = request("GET /api/state HTTP/1.1\r\n%s", 0, 0, 0, out, sizeof(out), auth);
  CHECK(status == 200 && strstr(out, "\"title\":\"Teste\""), "state: %d %s", status, out);

  status = post("/api/open", token, "rtp.pt", out, sizeof(out));
  CHECK(status == 200 && !strcmp(last_open, "rtp.pt"), "open: %d %s", status, last_open);
  status = post("/api/open", token, "", out, sizeof(out));
  CHECK(status == 400, "open nothing: %d", status);
  status = post("/api/type?enter=1", token, "olá mundo", out, sizeof(out));
  CHECK(status == 200 && !strcmp(last_type, "olá mundo") && last_enter == 1, "type: %d", status);
  status = post("/api/type", token, "a\nb", out, sizeof(out));
  CHECK(status == 200 && !strcmp(last_type, "a\nb") && last_enter == 0, "type 2: %d", status);
  status = post("/api/key?k=back", token, "", out, sizeof(out));
  CHECK(status == 200 && !strcmp(last_key, "back"), "key: %d %s", status, last_key);
  status = post("/api/key?k=shutdown", token, "", out, sizeof(out));
  CHECK(status == 400, "unknown key: %d", status);

  // The game's card (2026-11-16, RN): its notes and pages kept, written from the phone.
  CHECK(phone_game_id_ok("PPSA01341") && phone_game_id_ok("CUSA01765") && !phone_game_id_ok("PPSA0134")
        && !phone_game_id_ok("PPSA013411") && !phone_game_id_ok("ppsa01341") && !phone_game_id_ok("") && !phone_game_id_ok("../../x1"), "ids");
  CHECK(phone_game_action_known("note") && phone_game_action_known("keep") && phone_game_action_known("unpin")
        && !phone_game_action_known("open") && !phone_game_action_known(""), "game actions");
  status = GET("/api/game", out);
  CHECK(status == 401, "game, no token: %d", status);
  status = request("GET /api/game HTTP/1.1\r\n%s", 0, 0, 0, out, sizeof(out), auth);
  CHECK(status == 200 && strstr(out, "\"games\": [") && !last_game_id[0], "game: %d %s", status, out);
  status = request("GET /api/game?id=PPSA01341 HTTP/1.1\r\n%s", 0, 0, 0, out, sizeof(out), auth);
  CHECK(status == 200 && !strcmp(last_game_id, "PPSA01341"), "game by id: %d %s", status, last_game_id);
  status = request("GET /api/game?id=..%%2Fx HTTP/1.1\r\n%s", 0, 0, 0, out, sizeof(out), auth);
  CHECK(status == 400, "a bad id: %d", status);
  status = request("GET /api/game?id=ppsa01341 HTTP/1.1\r\n%s", 0, 0, 0, out, sizeof(out), auth);
  CHECK(status == 400, "an id in small letters: %d", status);
  game_none = 1;
  status = request("GET /api/game HTTP/1.1\r\n%s", 0, 0, 0, out, sizeof(out), auth);
  CHECK(status == 404, "no card: %d", status);
  game_none = 0;
  status = post("/api/game?do=note&id=CUSA01765", token, "código 4-7-1\nboss", out, sizeof(out));
  CHECK(status == 200 && !strcmp(last_game_action, "note") && !strcmp(last_game_id, "CUSA01765")
        && !strcmp(last_game_body, "código 4-7-1\nboss"), "note: %d %s %s", status, last_game_action, last_game_body);
  status = post("/api/game?id=PPSA01341&do=keep", token, "", out, sizeof(out));
  CHECK(status == 200 && !strcmp(last_game_action, "keep") && !last_game_body[0], "keep the TV's page: %d", status);
  status = post("/api/game?do=keep&id=PPSA01341", token, "https://a.pt/guia\tO guia", out, sizeof(out));
  CHECK(status == 200 && !strcmp(last_game_body, "https://a.pt/guia\tO guia"), "keep a link: %d %s", status, last_game_body);
  last_game_action[0] = 0;
  status = post("/api/game?do=install&id=PPSA01341", token, "x", out, sizeof(out));
  CHECK(status == 400 && !last_game_action[0], "an unknown action: %d", status);
  status = post("/api/game?do=note", token, "x", out, sizeof(out));
  CHECK(status == 400 && !last_game_action[0], "no id: %d", status);
  status = post("/api/game?do=note&id=PPSA0134", token, "x", out, sizeof(out));
  CHECK(status == 400 && !last_game_action[0], "a short id: %d", status);
  status = post("/api/game?do=note&id=PPSA01341", 0, "x", out, sizeof(out));
  CHECK(status == 401 && !last_game_action[0], "game, no token: %d", status);

  // 2026-11-16: the card "Discord": the webhook kept or not (only its start), given, refused, forgotten.
  status = GET("/api/discord", out);
  CHECK(status == 401, "discord, no token: %d", status);
  status = request("GET /api/discord HTTP/1.1\r\n%s", 0, 0, 0, out, sizeof(out), auth);
  CHECK(status == 200 && strstr(out, "\"set\": 0"), "discord, none: %d %s", status, out);
  status = post("/api/discord", token, "https://discord.com/api/webhooks/123456789/AbCdEfGhIjKlMnOpQrStUv", out, sizeof(out));
  CHECK(status == 200 && !strcmp(last_hook, "https://discord.com/api/webhooks/123456789/AbCdEfGhIjKlMnOpQrStUv"), "discord, kept: %d", status);
  status = request("GET /api/discord HTTP/1.1\r\n%s", 0, 0, 0, out, sizeof(out), auth);
  CHECK(status == 200 && strstr(out, "\"set\": 1") && !strstr(out, "AbCd"), "discord, kept, no token shown: %d %s", status, out);
  status = post("/api/discord", token, "https://example.com/x", out, sizeof(out));
  CHECK(status == 400 && strstr(out, "webhook") && strstr(last_hook, "discord.com"), "discord, not a webhook: %d", status);
  char long_hook[700];
  memset(long_hook, 'a', sizeof(long_hook) - 1);
  long_hook[sizeof(long_hook) - 1] = 0;
  const int sets = hook_sets;
  status = post("/api/discord", token, long_hook, out, sizeof(out));
  CHECK(status == 400 && hook_sets == sets, "discord, too long, not even asked: %d", status);
  status = post("/api/discord", 0, "", out, sizeof(out));
  CHECK(status == 401 && last_hook[0], "discord, no token: %d", status);
  status = post("/api/discord", token, "", out, sizeof(out));
  CHECK(status == 200 && !last_hook[0], "discord, forgotten: %d", status);

  // The downloads (docs/17): the list, and what may be done to one.
  status = GET("/api/downloads", out);
  CHECK(status == 401, "downloads, no token: %d", status);
  status = request("GET /api/downloads HTTP/1.1\r\n%s", 0, 0, 0, out, sizeof(out), auth);
  CHECK(status == 200 && strstr(out, "\"name\": \"a.zip\""), "downloads: %d %s", status, out);
  downloads_empty = 1;
  status = request("GET /api/downloads HTTP/1.1\r\n%s", 0, 0, 0, out, sizeof(out), auth);
  CHECK(status == 200 && strstr(out, "\"downloads\": []"), "no downloads: %d %s", status, out);
  downloads_empty = 0;
  status = post("/api/download?do=unzip&id=3", token, "", out, sizeof(out));
  CHECK(status == 200 && !strcmp(last_action, "unzip") && last_id == 3, "unzip 3: %d", status);
  status = post("/api/download?id=12&do=cancel", token, "", out, sizeof(out));
  CHECK(status == 200 && !strcmp(last_action, "cancel") && last_id == 12, "cancel 12: %d", status);
  last_action[0] = 0;
  status = post("/api/download?do=delete&id=3", token, "", out, sizeof(out));
  CHECK(status == 400 && !last_action[0], "an unknown action: %d", status);
  status = post("/api/download?do=open&id=3a", token, "", out, sizeof(out));
  CHECK(status == 400 && !last_action[0], "an id that is not a number: %d", status);
  status = post("/api/download?do=open&id=0", token, "", out, sizeof(out));
  CHECK(status == 400 && !last_action[0], "id 0: %d", status);
  status = post("/api/download?do=open", token, "", out, sizeof(out));
  CHECK(status == 400 && !last_action[0], "no id: %d", status);
  status = post("/api/download?do=open&id=3", 0, "", out, sizeof(out));
  CHECK(status == 401 && !last_action[0], "no token: %d", status);
  status = request("GET /api/download?do=open&id=3 HTTP/1.1\r\n%s", 0, 0, 0, out, sizeof(out), auth);
  CHECK(status == 404 && !last_action[0], "a GET does nothing: %d", status);
  CHECK(phone_download_action_known("retry") && phone_download_action_known("open")
        && !phone_download_action_known("") && !phone_download_action_known("rm"),
        "the actions");
  {
    static char big[PHONE_TEXT_MAX + 2];
    memset(big, 'a', sizeof(big) - 1);
    status = post("/api/type", token, big, out, sizeof(out));
    CHECK(status == 413, "too long: %d", status);
  }
  status = request("POST /api/type HTTP/1.1\r\n%sTransfer-Encoding: chunked\r\n", 0, 0, 0, out,
                   sizeof(out), auth);
  CHECK(status == 400, "chunked: %d", status);

  // A file of 3 MB and a bit, in pieces.
  size_t size = 3 * 1024 * 1024 + 123;
  unsigned char* data = malloc(size);
  for(size_t i = 0; i < size; i++) {
    data[i] = (unsigned char)(i * 2654435761u >> 13);
  }
  status = request("POST /api/file?name=v%%C3%%ADdeo%%201.mp4&where=auto HTTP/1.1\r\n%s",
                   data, size, size, out, sizeof(out), auth);
  CHECK(status == 200 && strstr(out, "\"saved\":\"vídeo 1.mp4\""), "file: %d %s", status, out);
  char path[600];
  snprintf(path, sizeof(path), "%s/vídeo 1.mp4", dir);
  FILE* f = fopen(path, "rb");
  size_t same = 0;
  if(f) {
    unsigned char buffer[65536];
    size_t n, at = 0;
    while((n = fread(buffer, 1, sizeof(buffer), f)) > 0) {
      for(size_t i = 0; i < n && at + i < size; i++) {
        same += buffer[i] == data[at + i];
      }
      at += n;
    }
    fclose(f);
  }
  CHECK(same == size && files_done == 1, "the file arrived whole: %zu of %zu", same, size);

  // One that stops in the middle: nothing is left.
  status = request("POST /api/file?name=half.bin HTTP/1.1\r\n%s", data, 1000, size, out,
                   sizeof(out), auth);
  struct stat st;
  char half[600], part[700];
  snprintf(half, sizeof(half), "%s/half.bin", dir);
  snprintf(part, sizeof(part), "%s/half.bin.part", dir);
  CHECK(stat(half, &st) && stat(part, &st) && files_failed == 1,
        "a broken file leaves nothing: %d %d", status, files_failed);
  status = request("POST /api/file?name=x.bin&where=usb HTTP/1.1\r\n%s", "abc", 3, 3, out,
                   sizeof(out), auth);
  CHECK(status == 409 && strstr(out, "usb"), "no USB: %d %s", status, out);
  status = request("POST /api/file?name=x.bin HTTP/1.1\r\n%s", "abc", 3, 100LL << 20, out,
                   sizeof(out), auth);
  CHECK(status == 507, "no room: %d %s", status, out);
  status = request("POST /api/file?name=a%%2Fb HTTP/1.1\r\n%s", "abc", 3, 3, out, sizeof(out),
                   auth);
  CHECK(status == 403, "a name with a folder: %d", status);
  status = request("GET /api/room?name=a.mp4&size=1000 HTTP/1.1\r\n%s", 0, 0, 0, out,
                   sizeof(out), auth);
  CHECK(status == 200, "room: %d %s", status, out);
  status = request("GET /api/room?name=a.mp4&size=999999999999 HTTP/1.1\r\n%s", 0, 0, 0, out,
                   sizeof(out), auth);
  CHECK(status == 507, "no room asked first: %d %s", status, out);
  status = request("POST /api/file?name=a%%00b HTTP/1.1\r\n%s", "abc", 3, 3, out, sizeof(out),
                   auth);
  CHECK(status == 400, "a name with a 0: %d", status);
  free(data);

  // The console's files (docs/18).
  snprintf(files_dir, sizeof(files_dir), "/tmp/phone_files_XXXXXX");
  CHECK(mkdtemp(files_dir) != 0, "files folder");
  put_dir("down");
  put_dir("down/Sub");
  put_dir("down/empty");
  put_dir("down/private");
  put_dir("outside");
  put_file("down/a.txt", "hello world");
  put_file("down/v.mp4", "0123456789");
  put_file("down/Sub/x.txt", "x");
  put_file("down/.secret", "s");
  put_file("down/private/key.txt", "k");
  put_file("outside/o.txt", "o");
  char link_to[600], link_at[600];
  snprintf(link_to, sizeof(link_to), "%s/outside/o.txt", files_dir);
  snprintf(link_at, sizeof(link_at), "%s/down/out.txt", files_dir);
  CHECK(!symlink(link_to, link_at), "a link out made");

  static char big[1 << 16];
  char q[1400], t1[40], t2[40];
  status = request("GET /api/files HTTP/1.1\r\n", 0, 0, 0, out, sizeof(out));
  CHECK(status == 401, "files without a token: %d", status);
  status = request("GET /api/files HTTP/1.1\r\n%s", 0, 0, 0, out, sizeof(out), auth);
  CHECK(status == 200 && strstr(out, "\"kind\": \"downloads\"") && strstr(out, "/down\"")
        && !strstr(out, "usb") && !strstr(out, "private"), "the places: %d %s", status, out);
  status = request("GET /api/files?path=%s/down HTTP/1.1\r\n%s", 0, 0, 0, out, sizeof(out),
                   files_dir, auth);
  CHECK(status == 200 && strstr(out, "\"Sub\"") && strstr(out, "\"empty\"") && strstr(out, "\"a.txt\"")
        && strstr(out, "\"v.mp4\"") && !strstr(out, "secret") && !strstr(out, "out.txt")
        && !strstr(out, "private"), "a folder: %d %s", status, out);
  status = request("GET /api/files?path=%s/down/private HTTP/1.1\r\n%s", 0, 0, 0, out, sizeof(out),
                   files_dir, auth);
  CHECK(status == 403, "the private folder: %d", status);
  status = request("GET /api/files?path=%s/down/..%%2Foutside HTTP/1.1\r\n%s", 0, 0, 0, out,
                   sizeof(out), files_dir, auth);
  CHECK(status == 403, "out by ..: %d", status);
  status = request("GET /api/files?path=%s/down/a.txt HTTP/1.1\r\n%s", 0, 0, 0, out, sizeof(out),
                   files_dir, auth);
  CHECK(status == 400, "a file is not a folder: %d", status);

  snprintf(q, sizeof(q), "/api/ticket?path=%s/down/out.txt", files_dir);
  status = post(q, token, "", out, sizeof(out));
  CHECK(status == 403, "a ticket through a link out: %d", status);
  snprintf(q, sizeof(q), "/api/ticket?path=%s/down/Sub", files_dir);
  status = post(q, token, "", out, sizeof(out));
  CHECK(status == 400, "a ticket for a folder: %d", status);
  snprintf(q, sizeof(q), "/api/ticket?path=%s/down/a.txt", files_dir);
  status = post(q, 0, "", out, sizeof(out));
  CHECK(status == 401, "a ticket without a token: %d", status);
  status = post(q, token, "", out, sizeof(out));
  CHECK(status == 200 && sscanf(out, "{\"ticket\":\"%32[0-9a-f]\"}", t1) == 1, "a ticket: %d %s", status, out);

  snprintf(q, sizeof(q), "GET /api/get?t=%s HTTP/1.1\r\nHost: x\r\n\r\n", t1);
  status = raw(q, big, sizeof(big));
  CHECK(status == 200 && strstr(big, "\r\n\r\nhello world") && strstr(big, "Content-Length: 11\r\n")
        && strstr(big, "Content-Type: text/plain") && strstr(big, "Content-Security-Policy: sandbox")
        && strstr(big, "Content-Disposition: attachment; filename=\"a.txt\"")
        && strstr(big, "Accept-Ranges: bytes") && sending_count == 1, "the file, by ticket alone: %d %s",
        status, big);
  snprintf(q, sizeof(q), "GET /api/get?t=%s HTTP/1.1\r\nHost: x\r\nRange: bytes=6-\r\n\r\n", t1);
  status = raw(q, big, sizeof(big));
  CHECK(status == 206 && strstr(big, "\r\n\r\nworld") && strstr(big, "Content-Range: bytes 6-10/11")
        && strstr(big, "Content-Length: 5\r\n"), "a part: %d %s", status, big);
  snprintf(q, sizeof(q), "GET /api/get?t=%s HTTP/1.1\r\nHost: x\r\nRange: bytes=50-\r\n\r\n", t1);
  status = raw(q, big, sizeof(big));
  CHECK(status == 416 && strstr(big, "Content-Range: bytes */11"), "past the end: %d", status);
  snprintf(q, sizeof(q), "HEAD /api/get?t=%s HTTP/1.1\r\nHost: x\r\n\r\n", t1);
  status = raw(q, big, sizeof(big));
  CHECK(status == 200 && strstr(big, "Content-Length: 11\r\n") && !strstr(big, "hello"),
        "HEAD: what it is, not the file: %d", status);
  status = raw("GET /api/get?t=00000000000000000000000000000000 HTTP/1.1\r\nHost: x\r\n\r\n", big,
               sizeof(big));
  CHECK(status == 404, "a ticket made up: %d", status);
  snprintf(q, sizeof(q), "POST /api/get?t=%s HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\n\r\n", t1);
  status = raw(q, big, sizeof(big));
  CHECK(status == 404, "POST to get: %d", status);
  status = raw("HEAD /api/state HTTP/1.1\r\nHost: x\r\n\r\n", big, sizeof(big));
  CHECK(status == 404, "HEAD of anything else: %d", status);

  snprintf(q, sizeof(q), "/api/show?path=%s/down/v.mp4", files_dir);
  status = post(q, token, "", out, sizeof(out));
  CHECK(status == 200 && strstr(last_show, "/down/v.mp4"), "a video on the TV: %d %s", status, last_show);
  last_show[0] = 0;
  snprintf(q, sizeof(q), "/api/show?path=%s/down/a.txt", files_dir);
  status = post(q, token, "", out, sizeof(out));
  CHECK(status == 400 && !last_show[0], "a text is not shown on the TV: %d", status);

  snprintf(q, sizeof(q), "/api/remove?path=%s/down", files_dir);
  status = post(q, token, "", out, sizeof(out));
  CHECK(status == 403, "a place itself stays: %d", status);
  snprintf(q, sizeof(q), "/api/remove?path=%s/down/Sub", files_dir);
  status = post(q, token, "", out, sizeof(out));
  CHECK(status == 409, "a folder with things in it stays: %d", status);
  snprintf(q, sizeof(q), "/api/remove?path=%s/down/empty", files_dir);
  status = post(q, token, "", out, sizeof(out));
  CHECK(status == 200, "an empty folder goes: %d", status);
  snprintf(q, sizeof(q), "/api/remove?path=%s/down/out.txt", files_dir);
  status = post(q, token, "", out, sizeof(out));
  CHECK(status == 403 && !access(link_to, F_OK), "a link out: its target stays: %d", status);
  snprintf(q, sizeof(q), "/api/remove?path=%s/down/private/key.txt", files_dir);
  status = post(q, token, "", out, sizeof(out));
  CHECK(status == 403, "a private file stays: %d", status);
  snprintf(q, sizeof(q), "/api/remove?path=%s/down/a.txt", files_dir);
  status = post(q, 0, "", out, sizeof(out));
  CHECK(status == 401, "remove without a token: %d", status);
  status = post(q, token, "", out, sizeof(out));
  CHECK(status == 200, "a file goes: %d", status);
  snprintf(q, sizeof(q), "GET /api/get?t=%s HTTP/1.1\r\nHost: x\r\n\r\n", t1);
  status = raw(q, big, sizeof(big));
  CHECK(status == 404, "its ticket finds nothing: %d", status);

  snprintf(q, sizeof(q), "/api/ticket?path=%s/down/v.mp4", files_dir);
  status = post(q, token, "", out, sizeof(out));
  CHECK(status == 200 && sscanf(out, "{\"ticket\":\"%32[0-9a-f]\"}", t2) == 1, "a ticket kept for later: %d", status);

  // A second phone, then the first forgets itself.
  phone_pin_open(pin);
  status = post("/api/pair", 0, pin, out, sizeof(out));
  CHECK(status == 200 && sscanf(out, "{\"token\":\"%32[0-9a-f]\"}", token2) == 1
        && strcmp(token, token2) && phone_count() == 2, "second phone: %d", status);
  status = post("/api/forget", token, "", out, sizeof(out));
  CHECK(status == 200 && phone_count() == 1 && paired_count == 1, "forget: %d", status);
  status = post("/api/open", token, "x.pt", out, sizeof(out));
  CHECK(status == 401, "forgotten: %d", status);
  snprintf(q, sizeof(q), "GET /api/get?t=%s HTTP/1.1\r\nHost: x\r\n\r\n", t2);
  status = raw(q, big, sizeof(big));
  CHECK(status == 404, "a forgotten phone's ticket: %d", status);
  status = post("/api/open", token2, "x.pt", out, sizeof(out));
  CHECK(status == 200, "the other still works: %d", status);
  phone_forget();
  status = post("/api/open", token2, "x.pt", out, sizeof(out));
  CHECK(status == 401 && phone_count() == 0, "all forgotten: %d", status);

  char ip[64];
  if(!phone_local_address(ip, sizeof(ip))) {
    printf("local address: %s\n", ip);
  }
  phone_stop();
  remove(path);
  rmdir(dir);
  unlink(link_at);
  take("down/v.mp4");
  take("down/Sub/x.txt");
  take("down/Sub");
  take("down/.secret");
  take("down/private/key.txt");
  take("down/private");
  take("down");
  take("outside/o.txt");
  take("outside");
  rmdir(files_dir);
  if(failures) {
    printf("%d failure(s)\n", failures);
    return 1;
  }
  printf("all passed\n");
  return 0;
}
