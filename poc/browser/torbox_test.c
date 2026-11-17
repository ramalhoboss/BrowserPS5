/*   Copyright (C) 2026 BrowserPS5 contributors. GPL-3.0-or-later.
 *
 * torbox.c on the build machine (make test), with a fake transport for
 * TorBox's API: the key's file, the links allowed (no YouTube), the
 * requests' bodies and headers, the answers (added, refused, a bad key,
 * too many requests, no network), the lists and a file's address; and
 * that the key is in no message.
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "torbox.h"

static int failed;

#define CHECK(what, cond)                \
  do {                                   \
    if(cond) {                           \
      printf("PASS %s\n", what);         \
    } else {                             \
      printf("FAIL %s\n", what);         \
      failed = 1;                        \
    }                                    \
  } while(0)

#define KEY "1a2b3c4d-0000-4e5f-9a8b-abcdef123456"

/* The fake API: what it was asked, and what it answers. */
static char asked_method[8], asked_url[1024], asked_key[256], asked_type[256];
static char asked_body[4096];
static size_t asked_body_size;
static int answer_status;
static const char* answer_body;
static const char* answer_web;  // /webdl/mylist's

static int
fake(const torbox_request_t* req, int* status, char** body, size_t* size) {
  snprintf(asked_method, sizeof(asked_method), "%s", req->method);
  snprintf(asked_url, sizeof(asked_url), "%s", req->url);
  snprintf(asked_key, sizeof(asked_key), "%s", req->key ? req->key : "");
  snprintf(asked_type, sizeof(asked_type), "%s", req->content_type ? req->content_type : "");
  asked_body_size = req->body_size < sizeof(asked_body) ? req->body_size : sizeof(asked_body) - 1;
  memcpy(asked_body, req->body ? req->body : "", req->body ? asked_body_size : 0);
  asked_body[req->body ? asked_body_size : 0] = 0;
  if(answer_status == 0) {
    *status = 0;
    return -1;
  }
  *status = answer_status;
  const char* text = answer_web && strstr(req->url, "/webdl/mylist") ? answer_web : answer_body;
  *body = strdup(text);
  *size = strlen(text);
  return 0;
}

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static torbox_result_t* got;

static void
done(torbox_result_t* r) {
  pthread_mutex_lock(&lock);
  got = r;
  pthread_cond_signal(&cond);
  pthread_mutex_unlock(&lock);
}

static torbox_result_t*
wait_result(void) {
  pthread_mutex_lock(&lock);
  while(!got) {
    pthread_cond_wait(&cond, &lock);
  }
  torbox_result_t* r = got;
  got = 0;
  pthread_mutex_unlock(&lock);
  return r;
}

int
main(void) {
  char hint[16], boundary[64], message[512];
  char key_path[] = "/tmp/torbox_keyXXXXXX";
  close(mkstemp(key_path));
  unlink(key_path);
  torbox_set_transport(fake);
  torbox_set_api("https://api.test/v1/api");

  /* The links. */
  CHECK("magnet allowed", torbox_link_allowed("magnet:?xt=urn:btih:abc"));
  CHECK("https file allowed", torbox_link_allowed("https://example.org/a/file.iso"));
  CHECK("YouTube refused", !torbox_link_allowed("https://www.youtube.com/watch?v=x"));
  CHECK("youtu.be refused", !torbox_link_allowed("https://youtu.be/x"));
  CHECK("music.youtube refused", !torbox_link_allowed("http://music.youtube.com/x"));
  CHECK("googlevideo refused", !torbox_link_allowed("https://r1---sn.googlevideo.com/videoplayback"));
  CHECK("user@youtube refused", !torbox_link_allowed("https://a:b@m.youtube.com/x"));
  CHECK("notyoutube.com allowed", torbox_link_allowed("https://notyoutube.com/x"));
  CHECK("javascript: refused", !torbox_link_allowed("javascript:alert(1)"));
  CHECK("file: refused", !torbox_link_allowed("file:///etc/passwd"));

  /* The bodies. */
  char* v = torbox_form_value("https://a.b/c d?e=f&g");
  CHECK("form value", v && !strcmp(v, "https%3A%2F%2Fa.b%2Fc%20d%3Fe%3Df%26g"));
  free(v);
  size_t size = 0;
  char* m = torbox_multipart("magnet", 0, 0, "magnet:?x", 9, boundary, sizeof(boundary), &size);
  char expect[512];
  snprintf(expect, sizeof(expect),
           "--%s\r\nContent-Disposition: form-data; name=\"magnet\"\r\n\r\nmagnet:?x\r\n--%s--\r\n", boundary,
           boundary);
  CHECK("multipart field", m && size == strlen(expect) && !memcmp(m, expect, size));
  free(m);
  m = torbox_multipart("file", "a.torrent", "application/x-bittorrent", "d1:a\0b", 6, boundary, sizeof(boundary),
                       &size);
  CHECK("multipart file", m && strstr(m, "filename=\"a.torrent\"\r\nContent-Type: application/x-bittorrent\r\n\r\n") &&
                              size > 6);
  free(m);

  /* The answers to an add. */
  CHECK("added", torbox_add_answer(200, "{\"success\":true,\"error\":null,\"detail\":\"Found Cached Torrent.\","
                                        "\"data\":{\"torrent_id\":7,\"name\":\"Debian 12\",\"hash\":\"aa\"}}",
                                   message, sizeof(message)) &&
                     !strcmp(message, "No TorBox: Debian 12"));
  CHECK("refused", !torbox_add_answer(200, "{\"success\": false, \"error\": \"DOWNLOAD_TOO_LARGE\", "
                                           "\"detail\": \"Too large for your plan.\", \"data\": null}",
                                      message, sizeof(message)) &&
                       !strcmp(message, "O TorBox não aceitou: Too large for your plan."));
  CHECK("bad key", !torbox_add_answer(401, "{\"detail\":\"Not authenticated\"}", message, sizeof(message)) &&
                       strstr(message, "recusou a chave"));
  CHECK("too many", !torbox_add_answer(429, "", message, sizeof(message)) && strstr(message, "esperar"));
  CHECK("no network", !torbox_add_answer(0, 0, message, sizeof(message)) && strstr(message, "não respondeu"));

  /* No key: nothing asked. */
  CHECK("no key yet", !torbox_key_hint(hint, sizeof(hint)));
  answer_status = 200;
  answer_body = "{\"success\":true,\"data\":{\"name\":\"x\"}}";
  asked_url[0] = 0;
  CHECK("add asked (no key)", !torbox_add_link("magnet:?xt=urn:btih:abc", done, 0));
  torbox_result_t* r = wait_result();
  CHECK("no key: why, nothing sent", !r->ok && strstr(r->message, "Falta a chave") && !asked_url[0]);
  torbox_result_free(r);

  /* The key. */
  CHECK("bad key refused", torbox_key_save(key_path, "not a key!") == -1);
  CHECK("key saved", !torbox_key_save(key_path, "  " KEY "\n"));
  struct stat st;
  CHECK("key file 0600", !stat(key_path, &st) && (st.st_mode & 0777) == 0600);
  CHECK("hint", torbox_key_hint(hint, sizeof(hint)) && !strcmp(hint, "3456"));
  CHECK("key forgotten", !torbox_key_save(key_path, "") && !torbox_key_hint(hint, sizeof(hint)) &&
                             access(key_path, F_OK));
  {
    FILE* f = fopen(key_path, "w");
    fprintf(f, "%s\n", KEY);
    fclose(f);
  }
  CHECK("key loaded", !torbox_key_load(key_path) && torbox_key_hint(hint, sizeof(hint)));

  /* A magnet. */
  answer_body = "{\"success\":true,\"detail\":\"ok\",\"data\":{\"torrent_id\":9,\"name\":\"Ubuntu \\u00e9\"}}";
  CHECK("magnet asked", !torbox_add_link("magnet:?xt=urn:btih:def&dn=Ubuntu", done, 0));
  r = wait_result();
  CHECK("magnet added", r->ok && r->kind == TORBOX_ADD && !strcmp(r->message, "No TorBox: Ubuntu \xc3\xa9"));
  CHECK("magnet request", !strcmp(asked_method, "POST") &&
                              !strcmp(asked_url, "https://api.test/v1/api/torrents/createtorrent") &&
                              !strcmp(asked_key, KEY) && strstr(asked_type, "multipart/form-data; boundary=") &&
                              strstr(asked_body, "name=\"magnet\"\r\n\r\nmagnet:?xt=urn:btih:def&dn=Ubuntu\r\n"));
  CHECK("the key is not in the message", !strstr(r->message, KEY));
  torbox_result_free(r);

  /* A file's link: a web download. */
  answer_body = "{\"success\":true,\"data\":{\"webdownload_id\":3,\"name\":\"file.iso\"}}";
  CHECK("link asked", !torbox_add_link("https://example.org/file.iso", done, 0));
  r = wait_result();
  CHECK("link added", r->ok && !strcmp(r->message, "No TorBox: file.iso"));
  CHECK("link request", !strcmp(asked_url, "https://api.test/v1/api/webdl/createwebdownload") &&
                            !strcmp(asked_type, "application/x-www-form-urlencoded") &&
                            !strcmp(asked_body, "link=https%3A%2F%2Fexample.org%2Ffile.iso"));
  torbox_result_free(r);
  CHECK("YouTube not asked", torbox_add_link("https://youtu.be/abc", done, 0) == -2);

  /* A .torrent file: sent, then deleted. */
  char path[] = "/tmp/torbox_testXXXXXX";
  int fd = mkstemp(path);
  (void)!write(fd, "d4:infod4:name3:abcee", 21);
  close(fd);
  answer_body = "{\"success\":true,\"data\":{\"torrent_id\":4,\"name\":\"abc\"}}";
  CHECK("file asked", !torbox_add_file(path, done, 0));
  r = wait_result();
  CHECK("file added", r->ok && !strcmp(r->message, "No TorBox: abc"));
  CHECK("file sent as a file", strstr(asked_body, "name=\"file\"; filename=") &&
                                   strstr(asked_body, "d4:infod4:name3:abcee"));
  CHECK("file deleted", access(path, F_OK) != 0);
  torbox_result_free(r);

  /* The lists. */
  answer_body = "{\"success\":true,\"data\":[{\"id\":1,\"name\":\"t\"}]}";
  answer_web = "{\"success\":true,\"data\":[]}";
  CHECK("lists asked", !torbox_list(done, (void*)5));
  r = wait_result();
  CHECK("lists", r->ok && r->kind == TORBOX_LIST && r->user == (void*)5 && strstr(r->torrents, "\"name\":\"t\"") &&
                     !strcmp(r->web, answer_web));
  CHECK("lists fresh", strstr(asked_url, "/webdl/mylist?bypass_cache=true") != 0);
  torbox_result_free(r);
  answer_web = 0;

  /* A file's address: the key only in requestdl's query. */
  answer_body = "{\"success\":true,\"detail\":\"\",\"data\":\"https://store-1.torbox.app/dl/abc?token=t\"}";
  CHECK("address asked", !torbox_link(0, 12, 3, done, 0));
  r = wait_result();
  CHECK("address", r->ok && r->kind == TORBOX_LINK && !strcmp(r->url, "https://store-1.torbox.app/dl/abc?token=t"));
  CHECK("address request", strstr(asked_url, "/torrents/requestdl?token=" KEY "&torrent_id=12&file_id=3") &&
                               strstr(asked_url, "redirect=false"));
  CHECK("address: no key in the message", !strstr(r->message, KEY));
  torbox_result_free(r);
  CHECK("web address asked", !torbox_link(1, 5, 0, done, 0));
  r = wait_result();
  CHECK("web address request", r->ok && strstr(asked_url, "/webdl/requestdl?") && strstr(asked_url, "web_id=5"));
  torbox_result_free(r);
  answer_body = "{\"success\":true,\"data\":\"javascript:alert(1)\"}";
  CHECK("bad address asked", !torbox_link(0, 1, 0, done, 0));
  r = wait_result();
  CHECK("bad address refused", !r->ok && !r->url[0]);
  torbox_result_free(r);

  /* A bad key, then no network. */
  answer_status = 403;
  answer_body = "{\"success\":false,\"error\":\"BAD_TOKEN\",\"detail\":\"Invalid token\"}";
  CHECK("lists asked (bad key)", !torbox_list(done, 0));
  r = wait_result();
  CHECK("bad key said", !r->ok && strstr(r->message, "recusou a chave"));
  torbox_result_free(r);
  answer_status = 0;
  CHECK("lists asked (no network)", !torbox_list(done, 0));
  r = wait_result();
  CHECK("no network said", !r->ok && strstr(r->message, "não respondeu"));
  torbox_result_free(r);

  unlink(key_path);
  printf(failed ? "torbox_test: FAIL\n" : "torbox_test: all passed\n");
  return failed;
}
