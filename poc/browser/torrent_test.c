/*   Copyright (C) 2026 BrowserPS5 contributors. GPL-3.0-or-later.
 *
 * torrent.c on the build machine (make test): base64, the torrent-add
 * request and its answers, then the whole way against a fake daemon on a
 * free port (the 409 with the session id first, as Transmission's), a
 * .torrent file, a daemon that starts late, and one that never does.
 */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "torrent.h"

extern int (*torrent_test_launch)(void);

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

/* The fake daemon: 409 to a request without the session id, then the
 * answer the test set; what it was asked is kept. */
static int listener = -1;
static int fake_port;
static char last_request[1 << 16];
static const char* answer_body;
static int requests;

static void*
fake_daemon(void* arg) {
  (void)arg;
  for(;;) {
    int c = accept(listener, 0, 0);
    if(c < 0) {
      return 0;
    }
    char buf[1 << 16];
    size_t got = 0;
    // Headers, then Content-Length bytes.
    for(;;) {
      ssize_t n = recv(c, buf + got, sizeof(buf) - 1 - got, 0);
      if(n <= 0) {
        break;
      }
      got += (size_t)n;
      buf[got] = 0;
      char* end = strstr(buf, "\r\n\r\n");
      char* length = strstr(buf, "Content-Length: ");
      if(end && length && got >= (size_t)(end + 4 - buf) + (size_t)atoi(length + 16)) {
        break;
      }
    }
    buf[got] = 0;
    if(!strstr(buf, "POST /transmission/rpc")) {  // answers()' probe: nothing sent
      close(c);
      continue;
    }
    requests++;
    char reply[4096];
    if(!strstr(buf, "X-Transmission-Session-Id: abc123")) {
      snprintf(reply, sizeof(reply),
               "HTTP/1.1 409 Conflict\r\nx-transmission-session-id: abc123\r\nContent-Length: 0\r\n\r\n");
    } else {
      snprintf(last_request, sizeof(last_request), "%s", buf);
      snprintf(reply, sizeof(reply), "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %zu\r\n\r\n%s",
               strlen(answer_body), answer_body);
    }
    send(c, reply, strlen(reply), 0);
    close(c);
  }
}

static int
fake_listen(void) {
  struct sockaddr_in a;
  socklen_t size = sizeof(a);
  listener = socket(AF_INET, SOCK_STREAM, 0);
  memset(&a, 0, sizeof(a));
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if(bind(listener, (struct sockaddr*)&a, sizeof(a)) || listen(listener, 8) ||
     getsockname(listener, (struct sockaddr*)&a, &size)) {
    return -1;
  }
  fake_port = ntohs(a.sin_port);
  pthread_t t;
  pthread_create(&t, 0, fake_daemon, 0);
  pthread_detach(t);
  return 0;
}

/* The callbacks: one result at a time. */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static int got_result = -1;
static char got_name[512];

static void
done(int result, const char* name, void* user) {
  (void)user;
  pthread_mutex_lock(&lock);
  got_result = result;
  snprintf(got_name, sizeof(got_name), "%s", name);
  pthread_cond_signal(&cond);
  pthread_mutex_unlock(&lock);
}

static int
wait_result(void) {
  pthread_mutex_lock(&lock);
  while(got_result < 0) {
    pthread_cond_wait(&cond, &lock);
  }
  int r = got_result;
  got_result = -1;
  pthread_mutex_unlock(&lock);
  return r;
}

static int launches;
static int launch_never(void) {
  launches++;
  return -1;
}
static int launch_late(void) {
  launches++;
  return fake_listen();  // the daemon answers from now on
}

int
main(void) {
  char out[64], name[256];

  /* base64 (RFC 4648's vectors). */
  CHECK("base64 f", torrent_base64((const unsigned char*)"f", 1, out, sizeof(out)) == 4 && !strcmp(out, "Zg=="));
  CHECK("base64 fo", torrent_base64((const unsigned char*)"fo", 2, out, sizeof(out)) && !strcmp(out, "Zm8="));
  CHECK("base64 foobar", torrent_base64((const unsigned char*)"foobar", 6, out, sizeof(out)) &&
                             !strcmp(out, "Zm9vYmFy"));
  CHECK("base64 too small", torrent_base64((const unsigned char*)"foobar", 6, out, 8) == 0);

  /* The request. */
  char* json = torrent_add_json("magnet:?xt=urn:btih:abc&dn=A \"b\"\\c", 0);
  CHECK("magnet request", json && !strcmp(json, "{\"method\":\"torrent-add\",\"arguments\":{\"paused\":false,"
                                               "\"filename\":\"magnet:?xt=urn:btih:abc&dn=A \\\"b\\\"\\\\c\"}}"));
  free(json);
  json = torrent_add_json(0, "ZGU=");
  CHECK("file request", json && strstr(json, "\"metainfo\":\"ZGU=\""));
  free(json);

  /* The answers. */
  CHECK("added", torrent_add_result("{\"arguments\":{\"torrent-added\":{\"hashString\":\"aa\",\"id\":1,"
                                    "\"name\":\"Debian \\\"12\\\" \\u00e9\"}},\"result\":\"success\"}",
                                    name, sizeof(name)) == TORRENT_ADDED &&
                     !strcmp(name, "Debian \"12\" \xc3\xa9"));
  CHECK("duplicate", torrent_add_result("{\"arguments\":{\"torrent-duplicate\":{\"name\":\"x\"}},"
                                        "\"result\":\"success\"}", name, sizeof(name)) == TORRENT_DUPLICATE &&
                         !strcmp(name, "x"));
  CHECK("refused", torrent_add_result("{\"arguments\":{},\"result\":\"invalid or corrupt torrent file\"}", name,
                                      sizeof(name)) == TORRENT_FAILED &&
                       strstr(name, "invalid or corrupt"));
  CHECK("not JSON", torrent_add_result("<html>", name, sizeof(name)) == TORRENT_FAILED);

  /* No daemon, and the loader takes nothing: a reason, no hang. */
  {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    socklen_t size = sizeof(a);
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(s, (struct sockaddr*)&a, sizeof(a));
    getsockname(s, (struct sockaddr*)&a, &size);
    close(s);  // a port nobody listens on
    torrent_set_rpc(ntohs(a.sin_port));
  }
  torrent_test_launch = launch_never;
  CHECK("magnet asked (no daemon)", !torrent_magnet("magnet:?xt=urn:btih:abc", done, 0));
  CHECK("no daemon: failed with a reason", wait_result() == TORRENT_FAILED && launches == 1 &&
                                               strstr(got_name, "carregador"));

  /* A daemon that answers (the fake on a free port): no launch. */
  torrent_test_launch = launch_late;
  answer_body = "{\"arguments\":{\"torrent-added\":{\"name\":\"Ubuntu\"}},\"result\":\"success\"}";
  CHECK("fake daemon up", !launch_late());
  torrent_set_rpc(fake_port);
  launches = 0;
  CHECK("magnet asked", !torrent_magnet("magnet:?xt=urn:btih:def&dn=Ubuntu", done, 0));
  CHECK("magnet added, after the 409", wait_result() == TORRENT_ADDED && !strcmp(got_name, "Ubuntu") &&
                                            requests == 2 && launches == 0);
  CHECK("the request had the session and the magnet",
        strstr(last_request, "X-Transmission-Session-Id: abc123") &&
            strstr(last_request, "\"filename\":\"magnet:?xt=urn:btih:def&dn=Ubuntu\""));

  /* A .torrent file: sent as base64, then deleted. */
  char path[] = "/tmp/torrent_testXXXXXX";
  int fd = mkstemp(path);
  (void)!write(fd, "d4:infod4:name3:abcee", 21);
  close(fd);
  answer_body = "{\"arguments\":{\"torrent-duplicate\":{\"name\":\"abc\"}},\"result\":\"success\"}";
  CHECK("file asked", !torrent_file(path, done, 0));
  CHECK("file there already", wait_result() == TORRENT_DUPLICATE && !strcmp(got_name, "abc"));
  CHECK("file sent as base64", strstr(last_request, "\"metainfo\":\"ZDQ6aW5mb2Q0Om5hbWUzOmFiY2Vl\"") != 0);
  CHECK("file deleted", access(path, F_OK) != 0);

  /* Not a .torrent. */
  fd = mkstemp(strcpy(path, "/tmp/torrent_testXXXXXX"));
  (void)!write(fd, "<html>", 6);
  close(fd);
  CHECK("bad file asked", !torrent_file(path, done, 0));
  CHECK("bad file refused", wait_result() == TORRENT_FAILED && strstr(got_name, ".torrent"));

  /* Start only. */
  CHECK("start asked", !torrent_start(done, 0));
  CHECK("running", wait_result() == TORRENT_RUNNING);

  printf(failed ? "torrent_test: FAIL\n" : "torrent_test: all passed\n");
  return failed;
}
