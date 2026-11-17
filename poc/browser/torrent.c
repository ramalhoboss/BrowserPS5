/*   Copyright (C) 2026 BrowserPS5 contributors
 *
 * This file is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * See torrent.h. Transmission's remote control (its RPC, docs/rpc-spec.md
 * in its source): POST /transmission/rpc with a JSON request; the first
 * answer is 409 with the X-Transmission-Session-Id to send from then on.
 * torrent_test.c checks it all on the build machine against a fake daemon.
 */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include "torrent.h"

#ifdef TORRENT_HOST_TEST
#define LOG(...) (void)0
/* The test's stand-in for the payload loader: 0 when it "started" one. */
int (*torrent_test_launch)(void);
#else
#define POC_NAME "browser"
#include "poc_log.h"
#define LOG(...) POC_LOG(__VA_ARGS__)
#endif

#define LOADER_PORT 9021           // elfldr, as title.cpp's ELFLDR_PORT
#define START_WAIT_MS 20000        // the daemon's first answer after the send
#define TORRENT_FILE_MAX (16 << 20)
#define REPLY_MAX (256 << 10)
#define THREAD_STACK (256 << 10)   // the PS5 gives 64 KiB

static int rpc_port = TORRENT_RPC_PORT;
static pthread_mutex_t start_lock = PTHREAD_MUTEX_INITIALIZER;  // one start at a time
static pthread_mutex_t session_lock = PTHREAD_MUTEX_INITIALIZER;
static char session_id[128];

void
torrent_set_rpc(int port) {
  rpc_port = port;
}


static void
sleep_ms(int ms) {
  usleep((useconds_t)ms * 1000);
}


static int
connect_local(int port, int timeout_s) {
  struct sockaddr_in address;
  struct timeval timeout = {timeout_s, 0};
  int sock = socket(AF_INET, SOCK_STREAM, 0);

  if(sock < 0) {
    return -1;
  }
#ifdef SO_NOSIGPIPE
  int yes = 1;
  setsockopt(sock, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
#endif
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_port = htons((unsigned short)port);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if(connect(sock, (struct sockaddr*)&address, sizeof(address))) {
    close(sock);
    return -1;
  }
  return sock;
}


static int
send_all(int sock, const char* p, size_t size) {
#ifdef MSG_NOSIGNAL
  const int flags = MSG_NOSIGNAL;
#else
  const int flags = 0;
#endif
  while(size) {
    const ssize_t n = send(sock, p, size, flags);
    if(n <= 0) {
      return -1;
    }
    p += n;
    size -= (size_t)n;
  }
  return 0;
}


static int
answers(void) {
  const int sock = connect_local(rpc_port, 2);
  if(sock < 0) {
    return 0;
  }
  close(sock);
  return 1;
}


static char*
read_file(const char* path, size_t limit, size_t* size) {
  struct stat st;
  char* data;
  size_t got = 0;
  int fd = open(path, O_RDONLY);

  if(fd < 0) {
    return 0;
  }
  if(fstat(fd, &st) || st.st_size <= 0 || (size_t)st.st_size > limit ||
     !(data = (char*)malloc((size_t)st.st_size))) {
    close(fd);
    return 0;
  }
  while(got < (size_t)st.st_size) {
    const ssize_t n = read(fd, data + got, (size_t)st.st_size - got);
    if(n <= 0) {
      break;
    }
    got += (size_t)n;
  }
  close(fd);
  if(got != (size_t)st.st_size) {
    free(data);
    return 0;
  }
  *size = got;
  return data;
}


// torrentd.elf to the payload loader, which starts it (as the title does
// with the browser). Its output does not come back here: it writes its own
// log (torrentd_main.cc).
static const char*
launch(void) {
#ifdef TORRENT_HOST_TEST
  return torrent_test_launch && !torrent_test_launch() ? 0 : "O carregador de payloads não respondeu";
#else
  size_t size = 0;
  char* elf = read_file(TORRENT_ELF, 64 << 20, &size);
  if(!elf) {
    LOG("torrents: no %s (%s)", TORRENT_ELF, strerror(errno));
    return "Falta o torrentd.elf: instala o pacote dos torrents";
  }
  const int sock = connect_local(LOADER_PORT, 10);
  if(sock < 0) {
    free(elf);
    LOG("torrents: the payload loader does not answer on 127.0.0.1:%d", LOADER_PORT);
    return "O carregador de payloads não respondeu";
  }
  const int err = send_all(sock, elf, size);
  free(elf);
  close(sock);
  if(err) {
    return "O carregador de payloads não recebeu o Transmission";
  }
  LOG("torrents: sent %s (%zu bytes) to the payload loader", TORRENT_ELF, size);
  return 0;
#endif
}


// The daemon answering, started if it was not. 0, or why not.
static const char*
ensure_running(void) {
  const char* why = 0;

  if(answers()) {
    return 0;
  }
  pthread_mutex_lock(&start_lock);
  if(!answers()) {
    why = launch();
    for(int waited = 0; !why && !answers(); waited += 250) {
      if(waited >= START_WAIT_MS) {
        why = "O Transmission não respondeu";
        break;
      }
      sleep_ms(250);
    }
    if(!why) {
      LOG("torrents: the daemon answers on 127.0.0.1:%d", rpc_port);
    }
  }
  pthread_mutex_unlock(&start_lock);
  return why;
}


// One POST to the RPC: 200's body in reply, or -1. A 409 gives the session
// id, and the request goes again once.
static int
rpc(const char* body, char* reply, size_t reply_size) {
  for(int attempt = 0; attempt < 2; attempt++) {
    char header[512];
    char id[sizeof(session_id)];
    size_t got = 0;

    pthread_mutex_lock(&session_lock);
    snprintf(id, sizeof(id), "%s", session_id);
    pthread_mutex_unlock(&session_lock);
    const int sock = connect_local(rpc_port, 15);
    if(sock < 0) {
      return -1;
    }
    const int n = snprintf(header, sizeof(header),
                           "POST /transmission/rpc HTTP/1.1\r\n"
                           "Host: 127.0.0.1:%d\r\n"
                           "Content-Type: application/json\r\n"
                           "Content-Length: %zu\r\n"
                           "Connection: close\r\n"
                           "%s%s%s"
                           "\r\n",
                           rpc_port, strlen(body), id[0] ? "X-Transmission-Session-Id: " : "", id,
                           id[0] ? "\r\n" : "");
    if(send_all(sock, header, (size_t)n) || send_all(sock, body, strlen(body))) {
      close(sock);
      return -1;
    }
    while(got + 1 < reply_size) {
      const ssize_t r = recv(sock, reply + got, reply_size - 1 - got, 0);
      if(r <= 0) {
        break;
      }
      got += (size_t)r;
    }
    close(sock);
    reply[got] = 0;
    int status = 0;
    if(sscanf(reply, "HTTP/1.%*d %d", &status) != 1) {
      return -1;
    }
    char* end = strstr(reply, "\r\n\r\n");
    if(!end) {
      return -1;
    }
    if(status == 409) {
      // The header's name in any case (HTTP); its value up to the line's end.
      for(char* line = strstr(reply, "\r\n"); line && line < end; line = strstr(line + 2, "\r\n")) {
        static const char name[] = "X-Transmission-Session-Id:";
        if(!strncasecmp(line + 2, name, sizeof(name) - 1)) {
          char* value = line + 2 + sizeof(name) - 1;
          while(*value == ' ') {
            value++;
          }
          pthread_mutex_lock(&session_lock);
          snprintf(session_id, sizeof(session_id), "%.*s", (int)strcspn(value, "\r\n"), value);
          pthread_mutex_unlock(&session_lock);
          break;
        }
      }
      continue;
    }
    if(status != 200) {
      LOG("torrents: the RPC answered %d", status);
      return -1;
    }
    memmove(reply, end + 4, strlen(end + 4) + 1);
    return 0;
  }
  return -1;
}


size_t
torrent_base64(const unsigned char* in, size_t size, char* out, size_t out_size) {
  static const char digits[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  const size_t need = (size + 2) / 3 * 4;
  size_t n = 0;

  if(need + 1 > out_size) {
    return 0;
  }
  for(size_t i = 0; i < size; i += 3) {
    const unsigned v = (unsigned)in[i] << 16 | (i + 1 < size ? (unsigned)in[i + 1] << 8 : 0) |
                       (i + 2 < size ? in[i + 2] : 0);
    out[n++] = digits[v >> 18 & 63];
    out[n++] = digits[v >> 12 & 63];
    out[n++] = i + 1 < size ? digits[v >> 6 & 63] : '=';
    out[n++] = i + 2 < size ? digits[v & 63] : '=';
  }
  out[n] = 0;
  return n;
}


char*
torrent_add_json(const char* filename, const char* metainfo_base64) {
  const char* value = filename ? filename : metainfo_base64;
  if(!value) {
    return 0;
  }
  const size_t size = strlen(value) * 6 + 128;
  char* json = (char*)malloc(size);
  if(!json) {
    return 0;
  }
  size_t n = (size_t)snprintf(json, size, "{\"method\":\"torrent-add\",\"arguments\":{\"paused\":false,\"%s\":\"",
                              filename ? "filename" : "metainfo");
  for(const unsigned char* p = (const unsigned char*)value; *p; p++) {
    if(*p == '"' || *p == '\\') {
      json[n++] = '\\';
      json[n++] = (char)*p;
    } else if(*p < 0x20) {
      n += (size_t)snprintf(json + n, size - n, "\\u%04x", *p);
    } else {
      json[n++] = (char)*p;
    }
  }
  snprintf(json + n, size - n, "\"}}");
  return json;
}


// A JSON string's value after "key": (the first one), unescaped into out.
int
torrent_json_string(const char* body, const char* key, char* out, size_t size) {
  char quoted[64];
  snprintf(quoted, sizeof(quoted), "\"%s\"", key);
  const char* p = strstr(body, quoted);
  size_t n = 0;

  if(!p || !size) {
    return -1;
  }
  p += strlen(quoted);
  while(*p == ' ' || *p == ':') {
    p++;
  }
  if(*p++ != '"') {
    return -1;
  }
  for(; *p && *p != '"' && n + 4 < size; p++) {
    if(*p != '\\') {
      out[n++] = *p;
      continue;
    }
    p++;
    if(*p == 'u') {
      unsigned c = 0;
#ifdef APP_RN
      // 2026-11-14 (found on the PC, browser-rn/tests-host/rpc-fuzz.c): the four digits must all be there. An
      // answer cut in the middle of one ("\u12", then its end) was read four bytes on all the same: past its end,
      // and on until a quote somewhere in the memory after it. And the two halves of a letter over U+FFFF
      // ("🎮": an emoji in a torrent's name, as most have) are one letter of four bytes: each was written
      // as three bytes that are no UTF-8, and the bar's message with that name never showed (run_script()).
      int digits = 0;
      for(; digits < 4; digits++) {
        const char h = p[1 + digits];
        const int v = h >= '0' && h <= '9' ? h - '0' : (h | 0x20) >= 'a' && (h | 0x20) <= 'f' ? (h | 0x20) - 'a' + 10 : -1;
        if(v < 0) {
          break;
        }
        c = c << 4 | (unsigned)v;
      }
      if(digits < 4) {
        break;
      }
      p += 4;
      if(c >= 0xd800 && c <= 0xdbff && p[1] == '\\' && p[2] == 'u') {  // the first half: is the second next?
        unsigned low = 0;
        int d = 0;
        for(; d < 4; d++) {
          const char h = p[3 + d];
          const int v = h >= '0' && h <= '9' ? h - '0' : (h | 0x20) >= 'a' && (h | 0x20) <= 'f' ? (h | 0x20) - 'a' + 10 : -1;
          if(v < 0) {
            break;
          }
          low = low << 4 | (unsigned)v;
        }
        if(d == 4 && low >= 0xdc00 && low <= 0xdfff) {
          c = 0x10000 + ((c - 0xd800) << 10) + (low - 0xdc00);
          p += 6;
        }
      }
      if(c >= 0xd800 && c <= 0xdfff) {  // half a letter by itself
        c = 0xfffd;
      }
      if(!c) {  // (a 0 would end the text there)
        c = ' ';
      }
      if(c >= 0x10000) {
        out[n++] = (char)(0xf0 | c >> 18);
        out[n++] = (char)(0x80 | (c >> 12 & 63));
        out[n++] = (char)(0x80 | (c >> 6 & 63));
        out[n++] = (char)(0x80 | (c & 63));
        continue;
      }
#else
      if(sscanf(p + 1, "%4x", &c) != 1) {
        break;
      }
      p += 4;
#endif
      if(c < 0x80) {
        out[n++] = (char)c;
      } else if(c < 0x800) {
        out[n++] = (char)(0xc0 | c >> 6);
        out[n++] = (char)(0x80 | (c & 63));
      } else {
        out[n++] = (char)(0xe0 | c >> 12);
        out[n++] = (char)(0x80 | (c >> 6 & 63));
        out[n++] = (char)(0x80 | (c & 63));
      }
    } else if(*p == 'n' || *p == 't' || *p == 'r') {
      out[n++] = ' ';
    } else if(*p) {
      out[n++] = *p;
    } else {
      break;
    }
  }
  out[n] = 0;
  return 0;
}


int
torrent_add_result(const char* body, char* name, size_t name_size) {
  char result[200];

  if(torrent_json_string(body, "result", result, sizeof(result))) {
    snprintf(name, name_size, "Resposta estranha do Transmission");
    return TORRENT_FAILED;
  }
  if(strcmp(result, "success")) {
    snprintf(name, name_size, "O Transmission recusou: %s", result);
    return TORRENT_FAILED;
  }
  const char* added = strstr(body, "\"torrent-added\"");
  const char* duplicate = strstr(body, "\"torrent-duplicate\"");
  const char* where = added ? added : duplicate;
  if(!where || torrent_json_string(where, "name", name, name_size)) {
    snprintf(name, name_size, "torrent");
  }
  return added ? TORRENT_ADDED : duplicate ? TORRENT_DUPLICATE : TORRENT_FAILED;
}


typedef struct job {
  char* magnet;          // or
  char* path;            // a .torrent, deleted after
  int start_only;
  torrent_done_t done;
  void* user;
} job_t;

static void*
job_thread(void* arg) {
  job_t* job = (job_t*)arg;
  char* reply = (char*)malloc(REPLY_MAX);
  char name[512] = "";
  int result = TORRENT_FAILED;
  char* base64 = 0;
  char* json = 0;
  const char* why = 0;

  if(job->path) {
    size_t size = 0;
    char* data = read_file(job->path, TORRENT_FILE_MAX, &size);
    unlink(job->path);
    if(!data || data[0] != 'd') {  // a .torrent is a bencoded dictionary
      why = "Este ficheiro .torrent não se lê";
    } else if((base64 = (char*)malloc((size + 2) / 3 * 4 + 1))) {
      torrent_base64((const unsigned char*)data, size, base64, (size + 2) / 3 * 4 + 1);
    }
    free(data);
  }
  if(!why && !reply) {
    why = "Sem memória";
  }
  if(!why) {
    why = ensure_running();
  }
  if(!why && job->start_only) {
    result = TORRENT_RUNNING;
  } else if(!why) {
    json = torrent_add_json(job->magnet, base64);
    if(!json || (job->path && !base64)) {
      why = "Sem memória";
    } else if(rpc(json, reply, REPLY_MAX)) {
      why = "O Transmission não respondeu ao pedido";
    } else {
      result = torrent_add_result(reply, name, sizeof(name));
    }
  }
  if(why) {
    snprintf(name, sizeof(name), "%s", why);
    result = TORRENT_FAILED;
  }
  LOG("torrents: %s: %s", job->start_only ? "start" : job->magnet ? "a magnet" : "a .torrent",
      result == TORRENT_ADDED ? "added" : result == TORRENT_DUPLICATE ? "there already"
      : result == TORRENT_RUNNING ? "running" : name);
  if(job->done) {
    job->done(result, name, job->user);
  }
  free(json);
  free(base64);
  free(reply);
  free(job->magnet);
  free(job->path);
  free(job);
  return 0;
}


static int
start_job(job_t* job) {
  pthread_attr_t attr;
  pthread_t thread;

  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, THREAD_STACK);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  const int err = pthread_create(&thread, &attr, job_thread, job);
  pthread_attr_destroy(&attr);
  if(err) {
    free(job->magnet);
    free(job->path);
    free(job);
    return -1;
  }
  return 0;
}


int
torrent_magnet(const char* link, torrent_done_t done, void* user) {
  job_t* job = (job_t*)calloc(1, sizeof(job_t));
  if(!job || !link || strncasecmp(link, "magnet:", 7) || !(job->magnet = strdup(link))) {
    free(job);
    return -1;
  }
  job->done = done;
  job->user = user;
  return start_job(job);
}


int
torrent_file(const char* path, torrent_done_t done, void* user) {
  job_t* job = (job_t*)calloc(1, sizeof(job_t));
  if(!job || !path || !(job->path = strdup(path))) {
    free(job);
    return -1;
  }
  job->done = done;
  job->user = user;
  return start_job(job);
}


int
torrent_start(torrent_done_t done, void* user) {
  job_t* job = (job_t*)calloc(1, sizeof(job_t));
  if(!job) {
    return -1;
  }
  job->start_only = 1;
  job->done = done;
  job->user = user;
  return start_job(job);
}
