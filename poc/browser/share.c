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

// share.h says what this is. share_discord() is a multipart POST (Discord's "files[0]" and "payload_json"), the
// way Discord's own documentation sends a file to a webhook. SHARE_NO_CURL: only the checks (share_test.c, on
// the build machine).

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "share.h"

static const char* const HOSTS[] = {"discord.com", "discordapp.com", "ptb.discord.com", "canary.discord.com"};

// After "https://<host>" or the local test's "http://127.0.0.1:<port>": where "/api/webhooks/" starts, or 0.
static const char*
hook_path(const char* url, int local) {
  if(!strncmp(url, "https://", 8)) {
    const char* host = url + 8;
    for(size_t i = 0; i < sizeof(HOSTS) / sizeof(HOSTS[0]); i++) {
      const size_t n = strlen(HOSTS[i]);
      if(!strncmp(host, HOSTS[i], n) && host[n] == '/') {
        return host + n;
      }
    }
    return 0;
  }
  if(local && !strncmp(url, "http://127.0.0.1:", 17)) {
    const char* p = url + 17;
    int digits = 0;
    while(isdigit((unsigned char)*p) && digits < 5) {
      p++;
      digits++;
    }
    return digits && *p == '/' ? p : 0;
  }
  return 0;
}

int
share_hook_ok(const char* url, int local) {
  if(!url || strlen(url) >= SHARE_HOOK_MAX) {
    return 0;
  }
  const char* p = hook_path(url, local);
  if(!p || strncmp(p, "/api/webhooks/", 14)) {
    return 0;
  }
  p += 14;
  int id = 0;
  while(isdigit((unsigned char)*p)) {
    p++;
    id++;
  }
  if(id < 5 || id > 25 || *p++ != '/') {
    return 0;
  }
  int token = 0;
  while(isalnum((unsigned char)*p) || *p == '-' || *p == '_') {
    p++;
    token++;
  }
  return token >= 20 && token <= 200 && !*p;
}

void
share_hook_short(const char* url, char* out, size_t size) {
  if(!size) {
    return;
  }
  out[0] = 0;
  if(!url) {
    return;
  }
  const char* from = strstr(url, "://");
  from = from ? from + 3 : url;
  const char* hooks = strstr(from, "/api/webhooks/");
  if(!hooks) {
    snprintf(out, size, "(not a webhook)");
    return;
  }
  const char* id = hooks + 14;
  size_t digits = 0;
  while(digits < 4 && isdigit((unsigned char)id[digits])) {
    digits++;
  }
  snprintf(out, size, "%.*s%.*s...", (int)(id - from), from, (int)digits, id);
}

int
share_payload(const char* text, char* out, size_t size) {
  size_t n = 0;
  const char* head = "{\"content\":\"";
  if(size < strlen(head) + 3) {
    return -1;
  }
  memcpy(out, head, strlen(head));
  n = strlen(head);
  for(const unsigned char* c = (const unsigned char*)(text ? text : ""); *c; c++) {
    char one[8];
    if(*c == '"' || *c == '\\') {
      snprintf(one, sizeof(one), "\\%c", *c);
    } else if(*c < 0x20) {
      snprintf(one, sizeof(one), "\\u%04x", *c);
    } else {
      one[0] = (char)*c;
      one[1] = 0;
    }
    const size_t m = strlen(one);
    if(n + m + 3 > size) {
      return -1;
    }
    memcpy(out + n, one, m);
    n += m;
  }
  memcpy(out + n, "\"}", 3);
  return (int)(n + 2);
}


#ifndef SHARE_NO_CURL
#include <curl/curl.h>

#define CA_BUNDLE "/user/homebrew/etc/ssl/cert.pem"  // as torbox_http.c's

static size_t
dropped(char* data, size_t size, size_t count, void* user) {
  (void)data;
  (void)user;
  return size * count;  // Discord's answer (the message, or why not) is not needed: its code says it
}

int
share_discord(const char* hook, const char* path, const char* text, int* status, int* curl_err) {
  char payload[2048];
  *status = 0;
  *curl_err = 0;
  if(share_payload(text, payload, sizeof(payload)) < 0) {
    return -1;
  }
  CURL* c = curl_easy_init();
  if(!c) {
    return -1;
  }
  curl_mime* mime = curl_mime_init(c);
  curl_mimepart* part = curl_mime_addpart(mime);
  curl_mime_name(part, "payload_json");
  curl_mime_data(part, payload, CURL_ZERO_TERMINATED);
  curl_mime_type(part, "application/json");
  part = curl_mime_addpart(mime);
  curl_mime_name(part, "files[0]");
  const CURLcode file = curl_mime_filedata(part, path);  // (its name: the file's own)
  curl_easy_setopt(c, CURLOPT_URL, hook);
  curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 15L);
  curl_easy_setopt(c, CURLOPT_TIMEOUT, 300L);
  curl_easy_setopt(c, CURLOPT_CAINFO, CA_BUNDLE);
  curl_easy_setopt(c, CURLOPT_USERAGENT, "BrowserPS5");
  curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "https,http");
  curl_easy_setopt(c, CURLOPT_MIMEPOST, mime);
  curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, dropped);
  const CURLcode err = file != CURLE_OK ? file : curl_easy_perform(c);
  long code = 0;
  curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
  curl_easy_cleanup(c);
  curl_mime_free(mime);
  *status = (int)code;
  *curl_err = (int)err;
  return err == CURLE_OK && code >= 200 && code < 300 ? 0 : -1;
}
#endif
