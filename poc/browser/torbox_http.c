/*   Copyright (C) 2026 BrowserPS5 contributors
 *
 * This file is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * torbox.c's HTTP on the console: the SDK's libcurl (OpenSSL 3), with the
 * browser's CA list (poc/webkit/package.sh, as WebKit's). One easy handle
 * per request, on torbox.c's thread; no signals (NOSIGNAL), timeouts on
 * the connection and the whole request, at most TORBOX_BODY_MAX back.
 * Nothing of the request (its URL holds the key for requestdl) is logged.
 */
#include <curl/curl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "torbox.h"

#define CA_BUNDLE "/user/homebrew/etc/ssl/cert.pem"  // BROWSERPS5_CA_BUNDLE

typedef struct answer {
  char* data;
  size_t size;
  int too_big;
} answer_t;

static size_t
take(char* data, size_t size, size_t count, void* user) {
  answer_t* a = (answer_t*)user;
  const size_t n = size * count;
  if(a->size + n > TORBOX_BODY_MAX) {
    a->too_big = 1;
    return 0;  // curl stops
  }
  char* more = (char*)realloc(a->data, a->size + n + 1);
  if(!more) {
    return 0;
  }
  memcpy(more + a->size, data, n);
  a->data = more;
  a->size += n;
  a->data[a->size] = 0;
  return n;
}

static pthread_once_t once = PTHREAD_ONCE_INIT;
static void
init(void) {
  curl_global_init(CURL_GLOBAL_DEFAULT);
}

static int
torbox_curl(const torbox_request_t* req, int* status, char** body, size_t* size) {
  answer_t a = {0};
  struct curl_slist* headers = 0;
  char line[TORBOX_KEY_MAX + 64];

  pthread_once(&once, init);
  CURL* c = curl_easy_init();
  if(!c) {
    return -1;
  }
  curl_easy_setopt(c, CURLOPT_URL, req->url);
  curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 15L);
  curl_easy_setopt(c, CURLOPT_TIMEOUT, 60L);
  curl_easy_setopt(c, CURLOPT_CAINFO, CA_BUNDLE);
  curl_easy_setopt(c, CURLOPT_USERAGENT, "BrowserPS5");
  curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
  curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "https,http");
  curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, take);
  curl_easy_setopt(c, CURLOPT_WRITEDATA, &a);
  if(req->key && *req->key) {
    snprintf(line, sizeof(line), "Authorization: Bearer %s", req->key);
    headers = curl_slist_append(headers, line);
    memset(line, 0, sizeof(line));
  }
  if(!strcmp(req->method, "POST")) {
    snprintf(line, sizeof(line), "Content-Type: %s", req->content_type ? req->content_type : "");
    headers = curl_slist_append(headers, line);
    curl_easy_setopt(c, CURLOPT_POST, 1L);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, req->body);
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)req->body_size);
  }
  curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
  const CURLcode err = curl_easy_perform(c);
  long code = 0;
  curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
  curl_easy_cleanup(c);
  // The key's header line, gone from memory with the list.
  for(struct curl_slist* h = headers; h; h = h->next) {
    memset(h->data, 0, strlen(h->data));
  }
  curl_slist_free_all(headers);
  if(err != CURLE_OK || a.too_big) {
    free(a.data);
    *status = 0;
    return -1;
  }
  *status = (int)code;
  *body = a.data ? a.data : strdup("");
  *size = a.size;
  return 0;
}

/* main.cpp calls it once, at the start. */
void
torbox_http_init(void) {
  torbox_set_transport(torbox_curl);
}
