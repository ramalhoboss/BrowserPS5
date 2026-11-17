/* Copyright (C) 2026 BrowserPS5 contributors. GPL-3.0-or-later.
   The console's files for the phone (files.h). */

#include "files.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif


// ---------------------------------------------------------------------------
// Paths.

// Whether rest ("" or "/a/b") has only plain parts: none empty, none hidden.
static int
plain_parts(const char* rest) {
  if(!*rest) {
    return 1;
  }
  if(*rest != '/') {
    return 0;
  }
  for(const char* p = rest; *p == '/';) {
    p++;
    if(!*p || *p == '/' || *p == '.') {
      return 0;
    }
    p += strcspn(p, "/");
  }
  return 1;
}

// What follows root in path ("" or "/..."), or 0 when it is not in it.
static const char*
under(const char* path, const char* root) {
  size_t n = strlen(root);
  while(n > 1 && root[n - 1] == '/') {
    n--;
  }
  if(n == 1 && root[0] == '/') {
    return path[0] == '/' ? path : 0;
  }
  if(!n || strncmp(path, root, n) || (path[n] && path[n] != '/')) {
    return 0;
  }
  return path + n;
}

int
files_inside(const char* path, const char* const* roots, int count) {
  if(!path || path[0] != '/' || strlen(path) >= FILES_PATH_MAX) {
    return -1;
  }
  for(int i = 0; i < count; i++) {
    if(!roots[i] || roots[i][0] != '/') {
      continue;
    }
    const char* rest = under(path, roots[i]);
    if(rest && plain_parts(rest)) {
      return i;
    }
  }
  return -1;
}

int
files_resolve(const char* path, const char* const* roots, int count,
              const char* const* private_dirs, int private_count, char* out, size_t size) {
  char real[PATH_MAX], root[PATH_MAX], hidden[PATH_MAX];

  if(files_inside(path, roots, count) < 0 || !realpath(path, real)) {
    return -1;
  }
  for(int i = 0; i < private_count; i++) {
    if(private_dirs[i] && private_dirs[i][0] == '/' && realpath(private_dirs[i], hidden)
       && under(real, hidden)) {
      return -1;
    }
  }
  for(int i = 0; i < count; i++) {
    if(!roots[i] || roots[i][0] != '/' || !realpath(roots[i], root)) {
      continue;
    }
    const char* rest = under(real, root);
    if(rest && plain_parts(rest)) {
      if(strlen(real) >= size) {
        return -1;
      }
      strcpy(out, real);
      return i;
    }
  }
  return -1;
}


// ---------------------------------------------------------------------------
// A folder as JSON.

typedef struct entry {
  char* name;
  int dir;
  long long size;
  long long time;
} entry_t;

typedef struct text {
  char* s;
  size_t n, cap;
  int failed;
} text_t;

static void
put(text_t* t, const char* s, size_t n) {
  if(t->failed) {
    return;
  }
  if(t->n + n + 1 > t->cap) {
    size_t cap = t->cap ? t->cap : 4096;
    while(cap < t->n + n + 1) {
      cap *= 2;
    }
    char* bigger = (char*)realloc(t->s, cap);
    if(!bigger) {
      t->failed = 1;
      return;
    }
    t->s = bigger;
    t->cap = cap;
  }
  memcpy(t->s + t->n, s, n);
  t->n += n;
  t->s[t->n] = 0;
}

static void
puts_(text_t* t, const char* s) {
  put(t, s, strlen(s));
}

// The length of the UTF-8 character at p (1-4), or 0 when it is not one.
static int
utf8_length(const unsigned char* p) {
  unsigned char c = p[0];
  if(c < 0x80) {
    return 1;
  }
  int n = c >= 0xc2 && c <= 0xdf ? 2 : c >= 0xe0 && c <= 0xef ? 3 : c >= 0xf0 && c <= 0xf4 ? 4 : 0;
  for(int i = 1; i < n; i++) {
    if((p[i] & 0xc0) != 0x80) {
      return 0;
    }
  }
  if((c == 0xe0 && p[1] < 0xa0) || (c == 0xed && p[1] > 0x9f)      // too long, surrogates
     || (c == 0xf0 && p[1] < 0x90) || (c == 0xf4 && p[1] > 0x8f)) {  // too long, too big
    return 0;
  }
  return n;
}

// A JSON string of name; 1 when it was not all UTF-8.
static int
put_name(text_t* t, const char* name) {
  int bad = 0;
  put(t, "\"", 1);
  for(const unsigned char* p = (const unsigned char*)name; *p;) {
    int n = utf8_length(p);
    if(!n) {
      puts_(t, "\\ufffd");
      bad = 1;
      p++;
    } else if(*p == '"' || *p == '\\') {
      char e[2] = {'\\', (char)*p};
      put(t, e, 2);
      p++;
    } else if(*p < 0x20 || *p == 0x7f) {
      char e[8];
      snprintf(e, sizeof(e), "\\u%04x", *p);
      puts_(t, e);
      p++;
    } else {
      put(t, (const char*)p, (size_t)n);
      p += n;
    }
  }
  put(t, "\"", 1);
  return bad;
}

static int
by_name(const void* a, const void* b) {
  const entry_t* x = (const entry_t*)a;
  const entry_t* y = (const entry_t*)b;
  if(x->dir != y->dir) {
    return y->dir - x->dir;
  }
  int c = strcasecmp(x->name, y->name);
  return c ? c : strcmp(x->name, y->name);
}

// Whether path (a folder) is one of the private ones (their real paths).
static int
skipped(const char* path, char (*skip)[PATH_MAX], int count) {
  char real[PATH_MAX];
  if(!count || !realpath(path, real)) {
    return 0;
  }
  for(int i = 0; i < count; i++) {
    if(!strcmp(real, skip[i])) {
      return 1;
    }
  }
  return 0;
}

char*
files_list_json(const char* dir, const char* const* skip, int skip_count) {
  char (*skip_real)[PATH_MAX] = 0;
  int skip_real_count = 0;
  if(skip_count > 0) {
    skip_real = (char (*)[PATH_MAX])calloc((size_t)skip_count, PATH_MAX);
    if(!skip_real) {
      return 0;
    }
    for(int i = 0; i < skip_count; i++) {
      if(skip[i] && realpath(skip[i], skip_real[skip_real_count])) {
        skip_real_count++;
      }
    }
  }
  DIR* d = opendir(dir);
  entry_t* list = d ? (entry_t*)calloc(FILES_LIST_MAX, sizeof(entry_t)) : 0;
  if(!list) {
    if(d) {
      closedir(d);
    }
    free(skip_real);
    return 0;
  }
  int count = 0;
  long long more = 0;
  struct dirent* e;
  char path[PATH_MAX];
  while((e = readdir(d))) {
    struct stat st;
    if(e->d_name[0] == '.'
       || (size_t)snprintf(path, sizeof(path), "%s/%s", dir, e->d_name) >= sizeof(path)
       || lstat(path, &st) || (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode))
       || (S_ISDIR(st.st_mode) && skipped(path, skip_real, skip_real_count))) {
      continue;
    }
    if(count == FILES_LIST_MAX) {
      more++;
      continue;
    }
    list[count].name = strdup(e->d_name);
    if(!list[count].name) {
      continue;
    }
    list[count].dir = S_ISDIR(st.st_mode);
    list[count].size = list[count].dir ? 0 : (long long)st.st_size;
    list[count].time = (long long)st.st_mtime;
    count++;
  }
  closedir(d);
  free(skip_real);
  qsort(list, (size_t)count, sizeof(entry_t), by_name);

  text_t t = {0, 0, 0, 0};
  puts_(&t, "{\"entries\": [");
  for(int i = 0; i < count; i++) {
    char numbers[96];
    puts_(&t, i ? ", {\"name\": " : "{\"name\": ");
    int bad = put_name(&t, list[i].name);
    snprintf(numbers, sizeof(numbers), ", \"dir\": %s, \"size\": %lld, \"time\": %lld%s}",
             list[i].dir ? "true" : "false", list[i].size, list[i].time,
             bad ? ", \"bad\": true" : "");
    puts_(&t, numbers);
    free(list[i].name);
  }
  char end[48];
  snprintf(end, sizeof(end), "], \"more\": %lld}", more);
  puts_(&t, end);
  free(list);
  if(t.failed) {
    free(t.s);
    return 0;
  }
  return t.s;
}


// ---------------------------------------------------------------------------
// Names and types.

static const char*
extension(const char* name) {
  const char* slash = strrchr(name, '/');
  const char* dot = strrchr(slash ? slash + 1 : name, '.');
  return dot ? dot + 1 : "";
}

static const struct {
  const char* ext;
  const char* type;
  int media;
} types[] = {
  {"mp4", "video/mp4", 1}, {"m4v", "video/mp4", 1}, {"webm", "video/webm", 1},
  {"mkv", "video/x-matroska", 1}, {"mov", "video/quicktime", 1}, {"avi", "video/x-msvideo", 1},
  {"ts", "video/mp2t", 1}, {"mp3", "audio/mpeg", 1}, {"m4a", "audio/mp4", 1},
  {"aac", "audio/aac", 1}, {"flac", "audio/flac", 1}, {"wav", "audio/wav", 1},
  {"ogg", "audio/ogg", 1}, {"opus", "audio/ogg", 1}, {"jpg", "image/jpeg", 1},
  {"jpeg", "image/jpeg", 1}, {"png", "image/png", 1}, {"gif", "image/gif", 1},
  {"webp", "image/webp", 1}, {"bmp", "image/bmp", 0}, {"txt", "text/plain; charset=utf-8", 0},
  {"pdf", "application/pdf", 0}, {"zip", "application/zip", 0},
};

const char*
files_type(const char* name) {
  const char* ext = extension(name);
  for(size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
    if(!strcasecmp(ext, types[i].ext)) {
      return types[i].type;
    }
  }
  return "application/octet-stream";
}

int
files_media(const char* name) {
  const char* ext = extension(name);
  for(size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
    if(!strcasecmp(ext, types[i].ext)) {
      return types[i].media;
    }
  }
  return 0;
}

void
files_disposition(const char* path, char* out, size_t size) {
  const char* slash = strrchr(path, '/');
  const unsigned char* name = (const unsigned char*)(slash ? slash + 1 : path);
  if(size < 64) {  // never asked for so little
    if(size) {
      out[0] = 0;
    }
    return;
  }
  size_t n = (size_t)snprintf(out, size, "attachment; filename=\"");

  for(const unsigned char* p = name; *p && n + 32 < size; p++) {
    out[n++] = *p >= 0x20 && *p < 0x7f && !strchr("\"\\;%", *p) ? (char)*p : '_';
  }
  n += (size_t)snprintf(out + n, size - n, "\"; filename*=UTF-8''");
  for(const unsigned char* p = name; *p && n + 4 < size; p++) {
    if((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9')
       || strchr("-._~", *p)) {
      out[n++] = (char)*p;
    } else {
      n += (size_t)snprintf(out + n, size - n, "%%%02X", *p);
    }
  }
  out[n] = 0;
}


// ---------------------------------------------------------------------------
// Ranges and removing.

int
files_range(const char* header, long long size, long long* from, long long* to) {
  if(!header) {
    return 1;
  }
  while(*header == ' ') {
    header++;
  }
  if(strncasecmp(header, "bytes=", 6) || strchr(header, ',')) {
    return 1;  // none, or several: all of it
  }
  const char* p = header + 6;
  while(*p == ' ') {
    p++;
  }
  char* stop;
  if(*p == '-') {  // the last n bytes
    long long n = strtoll(p + 1, &stop, 10);
    if(stop == p + 1 || *stop || n < 0) {
      return 1;
    }
    if(n == 0 || size <= 0) {
      return -1;
    }
    *from = n >= size ? 0 : size - n;
    *to = size - 1;
    return 0;
  }
  if(*p < '0' || *p > '9') {
    return 1;
  }
  long long a = strtoll(p, &stop, 10);
  if(*stop != '-') {
    return 1;
  }
  p = stop + 1;
  long long b = size - 1;
  if(*p) {
    b = strtoll(p, &stop, 10);
    if(stop == p || *stop || b < a) {
      return 1;
    }
  }
  if(a >= size) {
    return -1;
  }
  *from = a;
  *to = b < size - 1 ? b : size - 1;
  return 0;
}

int
files_remove(const char* path) {
  struct stat st;
  if(lstat(path, &st)) {
    return errno;
  }
  if(S_ISREG(st.st_mode)) {
    return unlink(path) ? errno : 0;
  }
  if(S_ISDIR(st.st_mode)) {
    if(!rmdir(path)) {
      return 0;
    }
    return errno == EEXIST ? ENOTEMPTY : errno;
  }
  return EPERM;
}
