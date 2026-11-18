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

// See update_net.h.

#include "update_net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <curl/curl.h>

#include "../common/sha256.h"

#define CA_BUNDLE "/user/homebrew/etc/ssl/cert.pem"  // as share.c's
#define FEED_MOST (1u << 20)

typedef struct feed {
  char* text;
  size_t len;
} feed_t;

static size_t
feed_take(char* data, size_t size, size_t count, void* user) {
  feed_t* f = (feed_t*)user;
  const size_t n = size * count;

  if(f->len + n > FEED_MOST) {
    return 0;  // not a list of ten releases: given up
  }
  char* more = (char*)realloc(f->text, f->len + n + 1);
  if(!more) {
    return 0;
  }
  f->text = more;
  memcpy(f->text + f->len, data, n);
  f->len += n;
  f->text[f->len] = 0;
  return n;
}

static void
common(CURL* c, const char* url, const char* agent) {
  curl_easy_setopt(c, CURLOPT_URL, url);
  curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 20L);
  curl_easy_setopt(c, CURLOPT_CAINFO, CA_BUNDLE);
  curl_easy_setopt(c, CURLOPT_USERAGENT, agent);
  curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "https");
  curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS_STR, "https");
  curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(c, CURLOPT_MAXREDIRS, 5L);
}

int
update_net_feed(const char* url, const char* agent, char** text, size_t* len, int* status, int* curl_err) {
  feed_t f = {0, 0};
  long code = 0;

  *text = 0;
  *len = 0;
  *status = 0;
  *curl_err = 0;
  CURL* c = curl_easy_init();
  if(!c) {
    return -1;
  }
  struct curl_slist* headers = curl_slist_append(0, "Accept: application/vnd.github+json");
  common(c, url, agent);
  curl_easy_setopt(c, CURLOPT_TIMEOUT, 40L);
  curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, feed_take);
  curl_easy_setopt(c, CURLOPT_WRITEDATA, &f);
  const CURLcode err = curl_easy_perform(c);
  curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
  curl_easy_cleanup(c);
  curl_slist_free_all(headers);
  *status = (int)code;
  *curl_err = (int)err;
  if(err != CURLE_OK || code != 200 || !f.text) {
    free(f.text);
    return -1;
  }
  *text = f.text;
  *len = f.len;
  return 0;
}

typedef struct file_take {
  FILE* file;
  sha256_t sha;
  unsigned long long got, most;
  volatile unsigned long long* told;
  volatile int* stop;
} file_take_t;

static size_t
file_take(char* data, size_t size, size_t count, void* user) {
  file_take_t* t = (file_take_t*)user;
  const size_t n = size * count;

  if((t->stop && *t->stop) || t->got + n > t->most || fwrite(data, 1, n, t->file) != n) {
    return 0;  // asked to end, more than the release's file, or the disk
  }
  sha256_add(&t->sha, data, n);
  t->got += n;
  if(t->told) {
    *t->told = t->got;
  }
  return n;
}

int
update_net_file(const char* url, const char* agent, const char* path, unsigned long long size,
                const unsigned char sha256[32], volatile unsigned long long* got, volatile int* stop, int* status,
                int* curl_err) {
  file_take_t t;
  unsigned char digest[32];
  long code = 0;

  *status = 0;
  *curl_err = 0;
  memset(&t, 0, sizeof(t));
  t.most = size;
  t.told = got;
  t.stop = stop;
  if(!(t.file=fopen(path, "wb"))) {
    return -1;
  }
  CURL* c = curl_easy_init();
  if(!c) {
    fclose(t.file);
    unlink(path);
    return -1;
  }
  sha256_begin(&t.sha);
  common(c, url, agent);
  curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 2000L);  // under 2 kB/s for a minute: given up, another time
  curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 60L);
  curl_easy_setopt(c, CURLOPT_TIMEOUT, 3600L);
  curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
  curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, file_take);
  curl_easy_setopt(c, CURLOPT_WRITEDATA, &t);
  const CURLcode err = curl_easy_perform(c);
  curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
  curl_easy_cleanup(c);
  const int closed = fclose(t.file);
  *status = (int)code;
  *curl_err = (int)err;
  if(err != CURLE_OK || code != 200 || closed) {
    unlink(path);
    return -1;
  }
  sha256_end(&t.sha, digest);
  if(t.got != size || memcmp(digest, sha256, 32)) {
    unlink(path);
    return -2;
  }
  return 0;
}
