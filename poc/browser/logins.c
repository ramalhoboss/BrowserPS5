/*   Copyright (C) 2026 BrowserPS5 contributors
 *
 * This file is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * The saved logins: logins.h.
 */
#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "logins.h"

typedef struct entry {
  char origin[LOGIN_FIELD];
  char user[LOGIN_FIELD];
  char pass[LOGIN_FIELD];
  long time;
  int never;
} entry_t;

static entry_t entries[LOGINS_MAX];
static int count;


int
logins_origin(const char* url, char* out, size_t size) {
  const char* rest;
  int https;
  if(!strncasecmp(url, "https://", 8)) {
    rest = url + 8;
    https = 1;
  } else if(!strncasecmp(url, "http://", 7)) {
    rest = url + 7;
    https = 0;
  } else {
    return -1;
  }
  size_t n = strcspn(rest, "/?#");
  const char* at = memchr(rest, '@', n);  // user:pass@ is not the host
  if(at) {
    n -= (size_t)(at + 1 - rest);
    rest = at + 1;
  }
  char host[LOGIN_FIELD];
  if(!n || n >= sizeof(host)) {
    return -1;
  }
  for(size_t i = 0; i < n; i++) {
    host[i] = (char)tolower((unsigned char)rest[i]);
  }
  host[n] = 0;
  const char* port = strrchr(host, ':');
  if(port && !strchr(port, ']') && !strcmp(port + 1, https ? "443" : "80")) {
    host[port - host] = 0;
  }
  if(!host[0] || (size_t)snprintf(out, size, "%s://%s", https ? "https" : "http", host) >= size) {
    return -1;
  }
  return 0;
}


void
logins_clear(void) {
  memset(entries, 0, sizeof(entries));
  count = 0;
}


// A field into the file: \t, \n, \r and \ escaped.
static void
put_field(FILE* f, const char* s) {
  for(; *s; s++) {
    if(*s == '\\') {
      fputs("\\\\", f);
    } else if(*s == '\t') {
      fputs("\\t", f);
    } else if(*s == '\n') {
      fputs("\\n", f);
    } else if(*s == '\r') {
      fputs("\\r", f);
    } else {
      fputc(*s, f);
    }
  }
}

// The next field of line (from *p, up to a tab or the end), unescaped.
static int
take_field(char** p, char* out, size_t size) {
  size_t n = 0;
  char* s = *p;
  for(; *s && *s != '\t' && *s != '\n'; s++) {
    char c = *s;
    if(c == '\\' && s[1]) {
      s++;
      c = *s == 't' ? '\t' : *s == 'n' ? '\n' : *s == 'r' ? '\r' : *s;
    }
    if(n + 1 >= size) {
      return -1;
    }
    out[n++] = c;
  }
  out[n] = 0;
  *p = *s == '\t' ? s + 1 : s;
  return 0;
}

int
logins_load(const char* path) {
  static char line[4 * LOGIN_FIELD * 2 + 64];
  FILE* f = fopen(path, "r");
  logins_clear();
  if(!f) {
    return -1;
  }
  while(count < LOGINS_MAX && fgets(line, sizeof(line), f)) {
    entry_t* e = &entries[count];
    char kind[4], time[32];
    char* p = line;
    if(take_field(&p, kind, sizeof(kind)) || take_field(&p, e->origin, sizeof(e->origin)) ||
       take_field(&p, e->user, sizeof(e->user)) || take_field(&p, e->pass, sizeof(e->pass)) ||
       take_field(&p, time, sizeof(time)) || (strcmp(kind, "L") && strcmp(kind, "N")) || !e->origin[0]) {
      memset(e, 0, sizeof(*e));
      continue;
    }
    e->never = !strcmp(kind, "N");
    e->time = atol(time);
    if(e->never) {
      e->user[0] = e->pass[0] = 0;
    } else if(!e->pass[0]) {
      memset(e, 0, sizeof(*e));
      continue;
    }
    count++;
  }
  memset(line, 0, sizeof(line));
  fclose(f);
  return count;
}

int
logins_save(const char* path) {
  char temporary[512];
  snprintf(temporary, sizeof(temporary), "%s.new", path);
  const int fd = open(temporary, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if(fd < 0) {
    return -1;
  }
  fchmod(fd, 0600);
  FILE* f = fdopen(fd, "w");
  if(!f) {
    close(fd);
    unlink(temporary);
    return -1;
  }
  for(int i = 0; i < count; i++) {
    const entry_t* e = &entries[i];
    fputs(e->never ? "N\t" : "L\t", f);
    put_field(f, e->origin);
    fputc('\t', f);
    put_field(f, e->user);
    fputc('\t', f);
    put_field(f, e->pass);
    fprintf(f, "\t%ld\n", e->time);
  }
  const int ok = !ferror(f);
  if(fclose(f) || !ok || rename(temporary, path)) {
    unlink(temporary);
    return -1;
  }
  return 0;
}


static entry_t*
find(const char* origin, const char* user, int never) {
  for(int i = 0; i < count; i++) {
    entry_t* e = &entries[i];
    if(e->never == never && !strcmp(e->origin, origin) && (never || !strcmp(e->user, user))) {
      return e;
    }
  }
  return 0;
}

static void
drop(entry_t* e) {
  const int i = (int)(e - entries);
  memset(e, 0, sizeof(*e));
  memmove(&entries[i], &entries[i + 1], sizeof(entry_t) * (size_t)(count - i - 1));
  count--;
  memset(&entries[count], 0, sizeof(entry_t));
}

int
logins_put(const char* origin, const char* user, const char* pass, long now) {
  if(!origin || !user || !pass || !origin[0] || !pass[0] || strlen(origin) >= LOGIN_FIELD ||
     strlen(user) >= LOGIN_FIELD || strlen(pass) >= LOGIN_FIELD) {
    return -1;
  }
  entry_t* never = find(origin, 0, 1);
  if(never) {  // saved after all: no longer "never"
    drop(never);
  }
  entry_t* e = find(origin, user, 0);
  if(e) {
    if(!strcmp(e->pass, pass)) {
      e->time = now;
      return 0;
    }
    memset(e->pass, 0, sizeof(e->pass));
    snprintf(e->pass, sizeof(e->pass), "%s", pass);
    e->time = now;
    return 2;
  }
  if(count >= LOGINS_MAX) {
    return -1;
  }
  e = &entries[count++];
  memset(e, 0, sizeof(*e));
  snprintf(e->origin, sizeof(e->origin), "%s", origin);
  snprintf(e->user, sizeof(e->user), "%s", user);
  snprintf(e->pass, sizeof(e->pass), "%s", pass);
  e->time = now;
  return 1;
}

int
logins_known(const char* origin, const char* user, const char* pass) {
  const entry_t* e = find(origin, user, 0);
  return e && !strcmp(e->pass, pass);
}

int
logins_has_user(const char* origin, const char* user) {
  return find(origin, user, 0) != 0;
}

int
logins_set_never(const char* origin, long now) {
  if(!origin[0] || strlen(origin) >= LOGIN_FIELD) {
    return -1;
  }
  if(find(origin, 0, 1)) {
    return 0;
  }
  if(count >= LOGINS_MAX) {
    return -1;
  }
  entry_t* e = &entries[count++];
  memset(e, 0, sizeof(*e));
  snprintf(e->origin, sizeof(e->origin), "%s", origin);
  e->never = 1;
  e->time = now;
  return 1;
}

int
logins_never(const char* origin) {
  return find(origin, 0, 1) != 0;
}

int
logins_remove(const char* origin, const char* user) {
  entry_t* e = user ? find(origin, user, 0) : find(origin, 0, 1);
  if(!e) {
    return -1;
  }
  drop(e);
  return 0;
}


// The logins of origin, the latest first: their indexes in entries.
static int
of_origin(const char* origin, int* out, int max) {
  int n = 0;
  for(int i = 0; i < count; i++) {
    if(entries[i].never || strcmp(entries[i].origin, origin)) {
      continue;
    }
    if(n == max) {  // full: in place of the oldest kept, if newer
      if(entries[i].time <= entries[out[max - 1]].time) {
        continue;
      }
      n--;
    }
    out[n++] = i;
    // Sorted, the newest first (a few at most).
    for(int j = n - 1; j > 0 && entries[out[j]].time > entries[out[j - 1]].time; j--) {
      const int t = out[j];
      out[j] = out[j - 1];
      out[j - 1] = t;
    }
  }
  return n;
}

int
logins_count(const char* origin) {
  int n = 0;
  for(int i = 0; i < count; i++) {
    n += !entries[i].never && !strcmp(entries[i].origin, origin);
  }
  return n;
}

static size_t
json_escaped(char* out, size_t size, const char* s) {
  size_t n = 0;
  if(n < size) {
    out[n] = '"';
  }
  n++;
  for(const unsigned char* p = (const unsigned char*)s; *p; p++) {
    char buf[8];
    const char* piece = buf;
    if(*p == '"' || *p == '\\') {
      buf[0] = '\\';
      buf[1] = (char)*p;
      buf[2] = 0;
    } else if(*p < 0x20 || *p == 0x7f) {
      snprintf(buf, sizeof(buf), "\\u%04x", *p);
    } else if(*p == '<') {  // never "</script>" in a page
      piece = "\\u003c";
    } else {
      buf[0] = (char)*p;
      buf[1] = 0;
    }
    for(; *piece; piece++, n++) {
      if(n < size) {
        out[n] = *piece;
      }
    }
  }
  if(n < size) {
    out[n] = '"';
  }
  n++;
  return n;
}

int
logins_users_json(const char* origin, char* out, size_t size) {
  int at[LOGINS_OFFERED];
  const int n = of_origin(origin, at, LOGINS_OFFERED);
  size_t used = 0;
  if(size < 3) {
    return -1;
  }
  out[used++] = '[';
  for(int i = 0; i < n; i++) {
    char item[LOGIN_FIELD * 6 + 4];
    size_t len = json_escaped(item, sizeof(item), entries[at[i]].user);
    if(len >= sizeof(item) || used + len + 3 > size) {
      return -1;
    }
    if(i) {
      out[used++] = ',';
    }
    memcpy(out + used, item, len);
    used += len;
  }
  out[used++] = ']';
  out[used] = 0;
  return n;
}

int
logins_get(const char* origin, int index, char* user, size_t user_size, char* pass, size_t pass_size) {
  int at[LOGINS_OFFERED];
  const int n = of_origin(origin, at, LOGINS_OFFERED);
  if(index < 0 || index >= n) {
    return -1;
  }
  snprintf(user, user_size, "%s", entries[at[index]].user);
  snprintf(pass, pass_size, "%s", entries[at[index]].pass);
  return 0;
}

char*
logins_list_json(void) {
  const size_t size = (size_t)count * (LOGIN_FIELD * 12 + 96) + 4;
  char* out = (char*)malloc(size);
  size_t n = 0;
  if(!out) {
    return 0;
  }
  out[n++] = '[';
  for(int i = 0; i < count; i++) {
    const entry_t* e = &entries[i];
    n += (size_t)snprintf(out + n, size - n, "%s{\"origin\": ", i ? ", " : "");
    n += json_escaped(out + n, size - n, e->origin);
    n += (size_t)snprintf(out + n, size - n, ", \"user\": ");
    n += json_escaped(out + n, size - n, e->user);
    n += (size_t)snprintf(out + n, size - n, ", \"time\": %ld, \"never\": %s}", e->time,
                          e->never ? "true" : "false");
  }
  snprintf(out + n, size - n, "]");
  return out;
}
