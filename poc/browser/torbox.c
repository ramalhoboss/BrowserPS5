/*   Copyright (C) 2026 BrowserPS5 contributors
 *
 * This file is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * The user's TorBox account: torbox.h. The requests and their answers are
 * here; the HTTP is torbox_http.c's (curl), or the tests' fake. The key
 * goes into no log: the lines below name the request, its status and its
 * size, never its URL (requestdl's query holds the key).
 */
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "torbox.h"
#include "torrent.h"  // torrent_json_string()

#ifdef TORBOX_HOST_TEST
#define LOG(...) (void)0
#else
#define POC_NAME "browser"
#include "poc_log.h"
#define LOG(...) POC_LOG(__VA_ARGS__)
#endif

#define THREAD_STACK (512 * 1024)  // curl and OpenSSL's handshake
#define TORRENT_FILE_MAX (16 << 20)

static pthread_mutex_t key_lock = PTHREAD_MUTEX_INITIALIZER;
static char key[TORBOX_KEY_MAX];
static torbox_transport_t transport;
static char api[256] = TORBOX_API;


void
torbox_set_transport(torbox_transport_t t) {
  transport = t;
}

void
torbox_set_api(const char* base) {
  snprintf(api, sizeof(api), "%s", base);
}

void
torbox_result_free(torbox_result_t* r) {
  if(r) {
    free(r->torrents);
    free(r->web);
    free(r);
  }
}


// A key as TorBox gives them (a UUID): letters, digits, - and _.
static int
key_valid(const char* k) {
  size_t n = strlen(k);
  if(n < 8 || n >= TORBOX_KEY_MAX) {
    return 0;
  }
  for(const char* p = k; *p; p++) {
    if(!isalnum((unsigned char)*p) && *p != '-' && *p != '_') {
      return 0;
    }
  }
  return 1;
}

// Spaces around it (a paste from the phone) away.
static void
trim(char* s) {
  size_t n = strlen(s);
  while(n && isspace((unsigned char)s[n - 1])) {
    s[--n] = 0;
  }
  size_t start = strspn(s, " \t\r\n");
  memmove(s, s + start, n - start + 1);
}

int
torbox_key_load(const char* path) {
  char k[TORBOX_KEY_MAX] = "";
  FILE* f = fopen(path, "r");
  if(!f) {
    return -1;
  }
  size_t n = fread(k, 1, sizeof(k) - 1, f);
  fclose(f);
  k[n] = 0;
  trim(k);
  if(!key_valid(k)) {
    LOG("torbox: the saved key is not one (%zu characters): not used", strlen(k));
    return -1;
  }
  pthread_mutex_lock(&key_lock);
  snprintf(key, sizeof(key), "%s", k);
  pthread_mutex_unlock(&key_lock);
  LOG("torbox: a key is saved");
  return 0;
}

int
torbox_key_save(const char* path, const char* new_key) {
  char k[TORBOX_KEY_MAX];
  snprintf(k, sizeof(k), "%s", new_key ? new_key : "");
  trim(k);
  if(!*k) {
    pthread_mutex_lock(&key_lock);
    memset(key, 0, sizeof(key));
    pthread_mutex_unlock(&key_lock);
    unlink(path);
    LOG("torbox: the key is forgotten");
    return 0;
  }
  if(!key_valid(k)) {
    return -1;
  }
  const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if(fd < 0) {
    LOG("torbox: the key was not saved (%s)", strerror(errno));
    return -1;
  }
  fchmod(fd, 0600);
  const size_t n = strlen(k);
  const int ok = write(fd, k, n) == (ssize_t)n;
  close(fd);
  if(!ok) {
    unlink(path);
    return -1;
  }
  pthread_mutex_lock(&key_lock);
  snprintf(key, sizeof(key), "%s", k);
  pthread_mutex_unlock(&key_lock);
  LOG("torbox: a key is saved");
  return 0;
}

int
torbox_key_hint(char* hint, size_t size) {
  pthread_mutex_lock(&key_lock);
  const size_t n = strlen(key);
  snprintf(hint, size, "%s", n >= 4 ? key + n - 4 : "");
  pthread_mutex_unlock(&key_lock);
  return n > 0;
}


// The host of an http(s) link, lower case, without "www." or "m.".
static void
link_host(const char* link, char* host, size_t size) {
  const char* p = strstr(link, "://");
  size_t n = 0;
  p = p ? p + 3 : link;
  const char* at = p + strcspn(p, "/?#");
  const char* user = memchr(p, '@', (size_t)(at - p));  // user:pass@host
  if(user) {
    p = user + 1;
  }
  for(; *p && !strchr("/:?#", *p) && n + 1 < size; p++) {
    host[n++] = (char)tolower((unsigned char)*p);
  }
  host[n] = 0;
}

static int
host_is(const char* host, const char* domain) {
  const size_t a = strlen(host), b = strlen(domain);
  return a >= b && !strcmp(host + a - b, domain) && (a == b || host[a - b - 1] == '.');
}

int
torbox_link_allowed(const char* link) {
  static const char* const youtube[] = {
    "youtube.com", "youtu.be", "youtube-nocookie.com", "googlevideo.com", "youtubekids.com",
  };
  if(!link || !*link) {
    return 0;
  }
  if(!strncasecmp(link, "magnet:?", 8)) {
    return 1;
  }
  if(strncasecmp(link, "http://", 7) && strncasecmp(link, "https://", 8)) {
    return 0;
  }
  char host[256];
  link_host(link, host, sizeof(host));
  if(!*host) {
    return 0;
  }
  for(size_t i = 0; i < sizeof(youtube) / sizeof(youtube[0]); i++) {
    if(host_is(host, youtube[i])) {
      return 0;
    }
  }
  return 1;
}


char*
torbox_form_value(const char* value) {
  static const char hex[] = "0123456789ABCDEF";
  char* out = (char*)malloc(strlen(value) * 3 + 1);
  size_t n = 0;
  if(!out) {
    return 0;
  }
  for(const unsigned char* p = (const unsigned char*)value; *p; p++) {
    if(isalnum(*p) || strchr("-._~", *p)) {
      out[n++] = (char)*p;
    } else {
      out[n++] = '%';
      out[n++] = hex[*p >> 4];
      out[n++] = hex[*p & 15];
    }
  }
  out[n] = 0;
  return out;
}


static int
contains(const char* data, size_t size, const char* what) {
  const size_t n = strlen(what);
  for(size_t i = 0; n <= size && i <= size - n; i++) {
    if(data[i] == what[0] && !memcmp(data + i, what, n)) {
      return 1;
    }
  }
  return 0;
}

char*
torbox_multipart(const char* name, const char* filename, const char* type, const char* data,
                 size_t data_size, char* boundary, size_t boundary_size, size_t* size) {
  static unsigned serial;
  char head[512];
  for(int attempt = 0;; attempt++) {
    snprintf(boundary, boundary_size, "BrowserPS5-%08lx%08x", (unsigned long)time(0),
             ++serial * 2654435761u);
    if(!contains(data, data_size, boundary)) {
      break;
    }
    if(attempt > 16) {
      return 0;
    }
  }
  int head_size;
  if(filename) {
    head_size = snprintf(head, sizeof(head),
                         "--%s\r\nContent-Disposition: form-data; name=\"%s\"; filename=\"%s\"\r\n"
                         "Content-Type: %s\r\n\r\n",
                         boundary, name, filename, type ? type : "application/octet-stream");
  } else {
    head_size = snprintf(head, sizeof(head), "--%s\r\nContent-Disposition: form-data; name=\"%s\"\r\n\r\n",
                         boundary, name);
  }
  char tail[96];
  const int tail_size = snprintf(tail, sizeof(tail), "\r\n--%s--\r\n", boundary);
  char* body = (char*)malloc((size_t)head_size + data_size + (size_t)tail_size + 1);
  if(!body) {
    return 0;
  }
  memcpy(body, head, (size_t)head_size);
  memcpy(body + head_size, data, data_size);
  memcpy(body + head_size + data_size, tail, (size_t)tail_size + 1);
  *size = (size_t)head_size + data_size + (size_t)tail_size;
  return body;
}


// "success": true in TorBox's answer.
static int
success(const char* body) {
  const char* p = body ? strstr(body, "\"success\"") : 0;
  if(!p) {
    return 0;
  }
  p += 9;
  while(*p == ' ' || *p == ':') {
    p++;
  }
  return !strncmp(p, "true", 4);
}

// Why a request failed, for the bar.
static void
failure(int status, const char* body, char* message, size_t size) {
  char detail[300] = "";
  if(status == 0) {
    snprintf(message, size, "O TorBox não respondeu (rede?)");
  } else if(status == 401 || status == 403) {
    snprintf(message, size, "O TorBox recusou a chave: vê-a em Definições, TorBox");
  } else if(status == 429) {
    snprintf(message, size, "O TorBox pede para esperar: muitos pedidos seguidos");
  } else if(body && (!torrent_json_string(body, "detail", detail, sizeof(detail)) ||
                     !torrent_json_string(body, "error", detail, sizeof(detail))) &&
            *detail) {
    snprintf(message, size, "O TorBox não aceitou: %s", detail);
  } else {
    snprintf(message, size, "O TorBox respondeu %d", status);
  }
}

int
torbox_add_answer(int status, const char* body, char* message, size_t size) {
  char name[300] = "";
  if(status != 200 || !success(body)) {
    failure(status, body, message, size);
    return 0;
  }
  const char* data = strstr(body, "\"data\"");
  if(data && !torrent_json_string(data, "name", name, sizeof(name)) && *name) {
    snprintf(message, size, "No TorBox: %s", name);
  } else {
    snprintf(message, size, "Adicionado ao TorBox");
  }
  return 1;
}


// One request, with the key. 0 when it answered (status set).
static int
call(const char* method, const char* url, const char* content_type, const char* body,
     size_t body_size, int* status, char** answer) {
  char k[TORBOX_KEY_MAX];
  size_t size = 0;

  pthread_mutex_lock(&key_lock);
  snprintf(k, sizeof(k), "%s", key);
  pthread_mutex_unlock(&key_lock);
  *status = 0;
  *answer = 0;
  if(!transport) {
    return -1;
  }
  torbox_request_t req = {method, url, k, content_type, body, body_size};
  const int err = transport(&req, status, answer, &size);
  memset(k, 0, sizeof(k));
  return err;
}


typedef struct job {
  int kind;
  int web;          // TORBOX_LINK: 1 for a web download
  long id, file;    // TORBOX_LINK
  char* link;       // TORBOX_ADD: a magnet or an http(s) link
  char* path;       // TORBOX_ADD: a .torrent file
  torbox_done_t done;
  void* user;
} job_t;

static char*
read_all(const char* path, size_t max, size_t* size) {
  FILE* f = fopen(path, "rb");
  char* data = 0;
  if(!f) {
    return 0;
  }
  if(!fseek(f, 0, SEEK_END)) {
    const long n = ftell(f);
    if(n > 0 && (size_t)n <= max && !fseek(f, 0, SEEK_SET) && (data = (char*)malloc((size_t)n + 1))) {
      *size = fread(data, 1, (size_t)n, f);
      data[*size] = 0;
    }
  }
  fclose(f);
  return data;
}

static void
run_add(job_t* job, torbox_result_t* r) {
  char url[512];
  char boundary[64], type[128];
  char* body = 0;
  size_t size = 0;
  int status = 0;
  char* answer = 0;

  if(job->path) {  // a .torrent
    size_t file_size = 0;
    char* file = read_all(job->path, TORRENT_FILE_MAX, &file_size);
    unlink(job->path);
    if(!file || !file_size || file[0] != 'd') {
      free(file);
      snprintf(r->message, sizeof(r->message), "Este ficheiro .torrent não se lê");
      return;
    }
    body = torbox_multipart("file", "browserps5.torrent", "application/x-bittorrent", file, file_size,
                            boundary, sizeof(boundary), &size);
    free(file);
    snprintf(url, sizeof(url), "%s/torrents/createtorrent", api);
  } else if(!strncasecmp(job->link, "magnet:", 7)) {
    body = torbox_multipart("magnet", 0, 0, job->link, strlen(job->link), boundary, sizeof(boundary),
                            &size);
    snprintf(url, sizeof(url), "%s/torrents/createtorrent", api);
  } else {  // a file's link: a web download
    char* value = torbox_form_value(job->link);
    if(value) {
      size = strlen(value) + 5;
      body = (char*)malloc(size + 1);
      if(body) {
        snprintf(body, size + 1, "link=%s", value);
      }
      free(value);
    }
    snprintf(url, sizeof(url), "%s/webdl/createwebdownload", api);
  }
  if(!body) {
    snprintf(r->message, sizeof(r->message), "Sem memória para o pedido ao TorBox");
    return;
  }
  if(job->path || !strncasecmp(job->link, "magnet:", 7)) {
    snprintf(type, sizeof(type), "multipart/form-data; boundary=%s", boundary);
  } else {
    snprintf(type, sizeof(type), "application/x-www-form-urlencoded");
  }
  call("POST", url, type, body, size, &status, &answer);
  free(body);
  r->ok = torbox_add_answer(status, answer, r->message, sizeof(r->message));
  LOG("torbox: %s: %d (%s)", job->path ? "a .torrent" : strncasecmp(job->link, "magnet:", 7) ? "a link" : "a magnet",
      status, r->ok ? "added" : "not added");
  free(answer);
}

static void
run_list(torbox_result_t* r) {
  char url[512];
  int status = 0, web_status = 0;

  snprintf(url, sizeof(url), "%s/torrents/mylist?bypass_cache=true", api);
  call("GET", url, 0, 0, 0, &status, &r->torrents);
  if(status == 200) {
    snprintf(url, sizeof(url), "%s/webdl/mylist?bypass_cache=true", api);
    call("GET", url, 0, 0, 0, &web_status, &r->web);
  }
  r->ok = status == 200 && web_status == 200;
  if(!r->ok) {
    failure(status != 200 ? status : web_status, status != 200 ? r->torrents : r->web, r->message,
            sizeof(r->message));
  }
  LOG("torbox: the lists: %d (%zu bytes), %d (%zu bytes)", status, r->torrents ? strlen(r->torrents) : 0,
      web_status, r->web ? strlen(r->web) : 0);
}

static void
run_link(job_t* job, torbox_result_t* r) {
  char k[TORBOX_KEY_MAX];
  char url[768];
  int status = 0;
  char* answer = 0;

  pthread_mutex_lock(&key_lock);
  snprintf(k, sizeof(k), "%s", key);
  pthread_mutex_unlock(&key_lock);
  // requestdl takes the key in its query (TorBox's API): this URL is never
  // logged nor shown.
  snprintf(url, sizeof(url), "%s/%s/requestdl?token=%s&%s=%ld&file_id=%ld&zip_link=false&redirect=false",
           api, job->web ? "webdl" : "torrents", k, job->web ? "web_id" : "torrent_id", job->id, job->file);
  memset(k, 0, sizeof(k));
  call("GET", url, 0, 0, 0, &status, &answer);
  memset(url, 0, sizeof(url));
  if(status == 200 && success(answer) && !torrent_json_string(answer, "data", r->url, sizeof(r->url)) &&
     !strncmp(r->url, "https://", 8) && !strpbrk(r->url, " \"'<>\\\r\n")) {
    r->ok = 1;
    snprintf(r->message, sizeof(r->message), "A trazer o ficheiro do TorBox...");
  } else {
    r->url[0] = 0;
    failure(status, answer, r->message, sizeof(r->message));
  }
  LOG("torbox: a file's address (%s %ld, file %ld): %d", job->web ? "web" : "torrent", job->id, job->file,
      status);
  free(answer);
}

static void*
run(void* arg) {
  job_t* job = (job_t*)arg;
  torbox_result_t* r = (torbox_result_t*)calloc(1, sizeof(torbox_result_t));

  if(r) {
    char hint[8];
    r->kind = job->kind;
    r->user = job->user;
    if(!torbox_key_hint(hint, sizeof(hint))) {
      snprintf(r->message, sizeof(r->message), "Falta a chave do TorBox: põe-na em Definições, TorBox");
      if(job->path) {
        unlink(job->path);
      }
    } else if(job->kind == TORBOX_ADD) {
      run_add(job, r);
    } else if(job->kind == TORBOX_LIST) {
      run_list(r);
    } else {
      run_link(job, r);
    }
    job->done(r);
  }
  free(job->link);
  free(job->path);
  free(job);
  return 0;
}

static int
start(job_t* job) {
  pthread_attr_t attr;
  pthread_t thread;

  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, THREAD_STACK);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  const int err = pthread_create(&thread, &attr, run, job);
  pthread_attr_destroy(&attr);
  if(err) {
    free(job->link);
    free(job->path);
    free(job);
    return -1;
  }
  return 0;
}

static job_t*
new_job(int kind, torbox_done_t done, void* user) {
  job_t* job = (job_t*)calloc(1, sizeof(job_t));
  if(job) {
    job->kind = kind;
    job->done = done;
    job->user = user;
  }
  return job;
}

int
torbox_add_link(const char* link, torbox_done_t done, void* user) {
  if(!torbox_link_allowed(link)) {
    return -2;
  }
  job_t* job = new_job(TORBOX_ADD, done, user);
  if(!job || !(job->link = strdup(link))) {
    free(job);
    return -1;
  }
  return start(job);
}

int
torbox_add_file(const char* path, torbox_done_t done, void* user) {
  job_t* job = new_job(TORBOX_ADD, done, user);
  if(!job || !path || !(job->path = strdup(path))) {
    free(job);
    return -1;
  }
  return start(job);
}

int
torbox_list(torbox_done_t done, void* user) {
  job_t* job = new_job(TORBOX_LIST, done, user);
  return job ? start(job) : -1;
}

int
torbox_link(int web, long id, long file, torbox_done_t done, void* user) {
  if(id < 0 || file < 0) {
    return -1;
  }
  job_t* job = new_job(TORBOX_LINK, done, user);
  if(!job) {
    return -1;
  }
  job->web = web != 0;
  job->id = id;
  job->file = file;
  return start(job);
}
