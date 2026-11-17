/* Copyright (C) 2026 BrowserPS5 contributors. GPL-3.0-or-later.
   The tabs of the last time (session.h). */

#include "session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

// An address worth keeping: http: or https:, something after, one line.
static int
web(const char* url, size_t length) {
  if(length >= SESSION_URL_MAX
     || !((length > 7 && !strncasecmp(url, "http://", 7))
          || (length > 8 && !strncasecmp(url, "https://", 8)))) {
    return 0;
  }
  for(size_t i = 0; i < length; i++) {
    if((unsigned char)url[i] < 0x20 || url[i] == 0x7f) {
      return 0;
    }
  }
  return 1;
}

size_t
session_text(const char* const* urls, int count, int current, char* out, size_t size) {
  int kept = 0, front = 0;
  size_t n;

  // Which of those kept is in front: 0 when current is not one of them.
  for(int i = 0; i < count && kept < SESSION_TABS_MAX; i++) {
    if(urls[i] && web(urls[i], strlen(urls[i]))) {
      if(i == current) {
        front = kept;
      }
      kept++;
    }
  }
  if(!size) {
    return 0;
  }
  n = (size_t)snprintf(out, size, "current %d\n", front);
  if(n >= size) {
    out[0] = 0;
    return 0;
  }
  kept = 0;
  for(int i = 0; i < count && kept < SESSION_TABS_MAX; i++) {
    size_t length = urls[i] ? strlen(urls[i]) : 0;
    if(!length || !web(urls[i], length)) {
      continue;
    }
    if(n + length + 2 > size) {
      out[0] = 0;
      return 0;
    }
    memcpy(out + n, urls[i], length);
    n += length;
    out[n++] = '\n';
    out[n] = 0;
    kept++;
  }
  return n;
}

int
session_write(const char* path, const char* text) {
  char fresh[1100];
  if((size_t)snprintf(fresh, sizeof(fresh), "%s.new", path) >= sizeof(fresh)) {
    return -1;
  }
  FILE* f = fopen(fresh, "w");
  if(!f) {
    return -1;
  }
  size_t length = strlen(text);
  int ok = fwrite(text, 1, length, f) == length;
  ok &= fflush(f) == 0;
  ok &= fclose(f) == 0;
  if(!ok || rename(fresh, path)) {
    unlink(fresh);
    return -1;
  }
  return 0;
}

int
session_read(const char* path, char (*urls)[SESSION_URL_MAX], int max, int* current) {
  FILE* f = fopen(path, "r");
  char* line = (char*)malloc(SESSION_URL_MAX + 2);
  int count = 0, front = 0, first = 1;

  *current = 0;
  if(!f || !line) {
    if(f) {
      fclose(f);
    }
    free(line);
    return 0;
  }
  while(count < max && fgets(line, SESSION_URL_MAX + 2, f)) {
    size_t length = strlen(line);
    int whole = length && line[length - 1] == '\n';
    if(!whole && !feof(f)) {  // too long: the rest of it goes too
      int c;
      while((c = fgetc(f)) != EOF && c != '\n') {
      }
      first = 0;
      continue;
    }
    while(length && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
      line[--length] = 0;
    }
    if(first && !strncmp(line, "current ", 8)) {
      front = atoi(line + 8);
      first = 0;
      continue;
    }
    first = 0;
    if(web(line, length)) {
      memcpy(urls[count++], line, length + 1);
    }
  }
  fclose(f);
  free(line);
  *current = front >= 0 && front < count ? front : 0;
  return count;
}
