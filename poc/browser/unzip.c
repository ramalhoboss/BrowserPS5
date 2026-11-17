/*   Copyright (C) 2026 BrowserPS5 contributors
 *
 * This file is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * A downloaded .zip into a folder (unzip.h). Two passes over the central
 * directory: the first checks it all and adds up the sizes (the free space
 * is checked before anything is written), the second writes.
 */
#include "unzip.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif

#define CHUNK (256 << 10)            // what is read and written at a time
#define REPORT_EVERY (1LL << 20)     // progress about every MiB
#define SIZE_MAX_TAKEN (1LL << 50)   // no entry says more than this

#define SIG_LOCAL 0x04034b50u
#define SIG_CENTRAL 0x02014b50u
#define SIG_END 0x06054b50u
#define SIG_END64 0x06064b50u
#define SIG_LOCATOR64 0x07064b50u

enum { ENTRY_FILE, ENTRY_DIR, ENTRY_SKIP };

typedef struct entry {
  int kind;
  uint16_t flags, method;
  uint32_t crc;
  long long csize, usize, offset;
  char name[1024];  // safe (unzip_safe_name), when not skipped
} entry_t;

typedef struct zip {
  int fd;
  long long size;
  unsigned char* directory;
  size_t directory_size;
  long long directory_offset;  // the entries' data is all before it
  long long entries;
} zip_t;

static uint16_t
le16(const unsigned char* p) {
  return (uint16_t)(p[0] | p[1] << 8);
}

static uint32_t
le32(const unsigned char* p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t
le64(const unsigned char* p) {
  return le32(p) | (uint64_t)le32(p + 4) << 32;
}

static int
read_at(int fd, void* buffer, size_t n, long long offset) {
  size_t got = 0;
  while(got < n) {
    const ssize_t r = pread(fd, (char*)buffer + got, n - got, (off_t)(offset + (long long)got));
    if(r < 0 && errno == EINTR) {
      continue;
    }
    if(r <= 0) {
      return -1;
    }
    got += (size_t)r;
  }
  return 0;
}

static int
write_all(int fd, const void* buffer, size_t n) {
  size_t done = 0;
  while(done < n) {
    const ssize_t w = write(fd, (const char*)buffer + done, n - done);
    if(w < 0 && errno == EINTR) {
      continue;
    }
    if(w <= 0) {
      return -1;
    }
    done += (size_t)w;
  }
  return 0;
}

/* The length of the UTF-8 character at p (at most n bytes), 0 if it is not
 * one (or an overlong one, or a surrogate). */
static size_t
utf8_length(const unsigned char* p, size_t n) {
  size_t length;
  if(p[0] >= 0xc2 && p[0] <= 0xdf) {
    length = 2;
  } else if(p[0] >= 0xe0 && p[0] <= 0xef) {
    length = 3;
  } else if(p[0] >= 0xf0 && p[0] <= 0xf4) {
    length = 4;
  } else {
    return 0;
  }
  if(length > n) {
    return 0;
  }
  for(size_t i = 1; i < length; i++) {
    if((p[i] & 0xc0) != 0x80) {
      return 0;
    }
  }
  if((p[0] == 0xe0 && p[1] < 0xa0) || (p[0] == 0xed && p[1] > 0x9f) ||
     (p[0] == 0xf0 && p[1] < 0x90) || (p[0] == 0xf4 && p[1] > 0x8f)) {
    return 0;
  }
  return length;
}

int
unzip_safe_name(const char* in, size_t length, char* out, size_t size) {
  size_t o = 0, i = 0;

  if(!size) {
    return -1;
  }
  if(length && (in[0] == '/' || in[0] == '\\')) {
    return -1;
  }
  if(length >= 2 && ((in[0] | 0x20) >= 'a' && (in[0] | 0x20) <= 'z') && in[1] == ':') {
    return -1;
  }
  while(i < length) {
    const size_t start = i;
    while(i < length && in[i] != '/' && in[i] != '\\') {
      i++;
    }
    const size_t n = i - start;
    if(i < length) {
      i++;  // the separator
    }
    if(!n || (n == 1 && in[start] == '.')) {
      continue;
    }
    if(n == 2 && in[start] == '.' && in[start + 1] == '.') {
      return -1;
    }
    if(o) {
      if(o + 1 >= size) {
        return -1;
      }
      out[o++] = '/';
    }
    const size_t part = o;
    for(size_t k = start; k < start + n;) {
      const unsigned char c = (unsigned char)in[k];
      const size_t utf8 = c < 0x80 ? 1 : utf8_length((const unsigned char*)in + k, start + n - k);
      const size_t taken = utf8 ? utf8 : 1;
      if(o - part + taken > 255 || o + taken >= size) {
        return -1;
      }
      if(!utf8 || (utf8 == 1 && (c < 0x20 || c == 0x7f || strchr(":*?\"<>|", c)))) {
        out[o++] = '_';
      } else {
        memcpy(out + o, in + k, utf8);
        o += utf8;
      }
      k += taken;
    }
  }
  if(!o) {
    return -1;
  }
  out[o] = 0;
  return 0;
}

void
unzip_folder_name(const char* zip_name, char* out, size_t size) {
  const char* base = zip_name;
  for(const char* p = zip_name; *p; p++) {
    if(*p == '/' || *p == '\\') {
      base = p + 1;
    }
  }
  size_t n = strlen(base);
  if(n >= 4 && !strcasecmp(base + n - 4, ".zip")) {
    n -= 4;
  }
  while(n && (*base == '.' || *base == ' ')) {
    base++;
    n--;
  }
  while(n && (base[n - 1] == '.' || base[n - 1] == ' ')) {
    n--;
  }
  if(!n || unzip_safe_name(base, n, out, size) || strchr(out, '/')) {
    snprintf(out, size, "zip");
  }
}

/* The end of the central directory (and ZIP64's, when there is one). */
static int
find_directory(zip_t* z) {
  const size_t tail = z->size < 22 + 65535 ? (size_t)z->size : 22 + 65535;
  if(z->size < 22) {
    return UNZIP_ERR_FORMAT;
  }
  unsigned char* buffer = (unsigned char*)malloc(tail);
  if(!buffer) {
    return UNZIP_ERR_OPEN;
  }
  if(read_at(z->fd, buffer, tail, z->size - (long long)tail)) {
    free(buffer);
    return UNZIP_ERR_OPEN;
  }
  long long end = -1;
  for(long long i = (long long)tail - 22; i >= 0; i--) {
    if(le32(buffer + i) == SIG_END && (size_t)i + 22 + le16(buffer + i + 20) <= tail) {
      end = i;
      break;
    }
  }
  if(end < 0) {
    free(buffer);
    return UNZIP_ERR_FORMAT;
  }
  const unsigned char* e = buffer + end;
  uint64_t disk = le16(e + 4), directory_disk = le16(e + 6), here = le16(e + 8), entries = le16(e + 10);
  uint64_t directory_size = le32(e + 12), directory_offset = le32(e + 16);
  const long long end_at = z->size - (long long)tail + end;
  long long limit = end_at;  // where the central directory must end
  free(buffer);

  if(end_at >= 20) {
    unsigned char locator[20];
    if(read_at(z->fd, locator, sizeof(locator), end_at - 20)) {
      return UNZIP_ERR_OPEN;
    }
    if(le32(locator) == SIG_LOCATOR64) {
      unsigned char record[56];
      const uint64_t at = le64(locator + 8);
      if(le32(locator + 4) || le32(locator + 16) > 1) {
        return UNZIP_ERR_FORMAT;  // in parts
      }
      if(end_at - 20 < (long long)sizeof(record) || at > (uint64_t)(end_at - 20) - sizeof(record) ||
         read_at(z->fd, record, sizeof(record), (long long)at) || le32(record) != SIG_END64) {
        return UNZIP_ERR_FORMAT;
      }
      disk = le32(record + 16);
      directory_disk = le32(record + 20);
      here = le64(record + 24);
      entries = le64(record + 32);
      directory_size = le64(record + 40);
      directory_offset = le64(record + 48);
      limit = (long long)at;
    }
  }
  if(disk || directory_disk || here != entries) {
    return UNZIP_ERR_FORMAT;  // in parts
  }
  if(entries > UNZIP_ENTRIES_MAX || directory_size > UNZIP_DIRECTORY_MAX ||
     directory_offset > (uint64_t)limit || directory_size > (uint64_t)limit - directory_offset) {
    return UNZIP_ERR_FORMAT;
  }
  z->entries = (long long)entries;
  z->directory_size = (size_t)directory_size;
  z->directory_offset = (long long)directory_offset;
  z->directory = (unsigned char*)malloc(z->directory_size ? z->directory_size : 1);
  if(!z->directory) {
    return UNZIP_ERR_OPEN;
  }
  if(z->directory_size && read_at(z->fd, z->directory, z->directory_size, z->directory_offset)) {
    return UNZIP_ERR_OPEN;
  }
  return UNZIP_OK;
}

/* The entry at *at in the central directory; *at moves past it. */
static int
next_entry(const zip_t* z, size_t* at, entry_t* e) {
  const unsigned char* p = z->directory + *at;
  if(*at + 46 > z->directory_size || le32(p) != SIG_CENTRAL) {
    return UNZIP_ERR_FORMAT;
  }
  const uint16_t made = le16(p + 4);
  const size_t name_length = le16(p + 28), extra_length = le16(p + 30), comment = le16(p + 32);
  if(*at + 46 + name_length + extra_length + comment > z->directory_size) {
    return UNZIP_ERR_FORMAT;
  }
  uint64_t usize = le32(p + 24), csize = le32(p + 20), offset = le32(p + 42);
  uint32_t disk = le16(p + 34);
  const uint32_t attributes = le32(p + 38);
  e->flags = le16(p + 8);
  e->method = le16(p + 10);
  e->crc = le32(p + 16);

  // ZIP64: the sizes and the offset that did not fit, in this order.
  const unsigned char* extra = p + 46 + name_length;
  for(size_t x = 0; x + 4 <= extra_length;) {
    const uint16_t id = le16(extra + x), length = le16(extra + x + 2);
    if(x + 4 + length > extra_length) {
      break;
    }
    if(id == 0x0001) {
      const unsigned char* f = extra + x + 4;
      size_t i = 0;
      if(usize == 0xffffffffu) {
        if(i + 8 > length) {
          return UNZIP_ERR_FORMAT;
        }
        usize = le64(f + i);
        i += 8;
      }
      if(csize == 0xffffffffu) {
        if(i + 8 > length) {
          return UNZIP_ERR_FORMAT;
        }
        csize = le64(f + i);
        i += 8;
      }
      if(offset == 0xffffffffu) {
        if(i + 8 > length) {
          return UNZIP_ERR_FORMAT;
        }
        offset = le64(f + i);
        i += 8;
      }
      if(disk == 0xffff && i + 4 <= length) {
        disk = le32(f + i);
      }
    }
    x += 4 + length;
  }
  if(disk || usize > SIZE_MAX_TAKEN || csize > SIZE_MAX_TAKEN || offset > SIZE_MAX_TAKEN) {
    return UNZIP_ERR_FORMAT;
  }
  e->usize = (long long)usize;
  e->csize = (long long)csize;
  e->offset = (long long)offset;

  const char* name = (const char*)p + 46;
  const unsigned unix_type = (made >> 8) == 3 ? (attributes >> 16) & 0170000 : 0;
  if(unzip_safe_name(name, name_length, e->name, sizeof(e->name))) {
    e->kind = ENTRY_SKIP;
    snprintf(e->name, sizeof(e->name), "%.*s", (int)(name_length < 200 ? name_length : 200), name);
  } else if((name_length && (name[name_length - 1] == '/' || name[name_length - 1] == '\\')) ||
            unix_type == 0040000) {
    e->kind = ENTRY_DIR;
  } else if(unix_type == 0120000 || (e->flags & 1) || (e->method != 0 && e->method != 8)) {
    e->kind = ENTRY_SKIP;  // a link, encrypted, or a method we do not have
  } else {
    e->kind = ENTRY_FILE;
  }
  *at += 46 + name_length + extra_length + comment;
  return UNZIP_OK;
}

/* dest/name's folders, each one a folder (not a file, not a link). */
static int
make_folders(const char* dest, const char* name, int with_last) {
  char path[2048];
  struct stat st;
  const int base = snprintf(path, sizeof(path), "%s/", dest);
  if(base < 0 || (size_t)base + strlen(name) >= sizeof(path)) {
    return -1;
  }
  strcpy(path + base, name);
  for(char* p = path + base;; p++) {
    if(*p == '/' || (!*p && with_last)) {
      const char c = *p;
      *p = 0;
      if(mkdir(path, 0777) && errno != EEXIST) {
        return -1;
      }
      if(lstat(path, &st) || !S_ISDIR(st.st_mode)) {
        return -1;
      }
      *p = c;
    }
    if(!*p) {
      return 0;
    }
  }
}

typedef struct writer {
  int fd;
  uint32_t crc;
  long long written, expected;
  unzip_result_t* result;
  unzip_progress_fn progress;
  void* ctx;
  long long next_report;
} writer_t;

static int
put(writer_t* w, const unsigned char* data, size_t n) {
  if(w->written + (long long)n > w->expected) {
    return UNZIP_ERR_DATA;  // bigger than it says
  }
  if(write_all(w->fd, data, n)) {
    return UNZIP_ERR_WRITE;
  }
  w->crc = (uint32_t)crc32(w->crc, data, (uInt)n);
  w->written += (long long)n;
  w->result->bytes += (long long)n;
  if(w->result->bytes >= w->next_report) {
    w->next_report = w->result->bytes + REPORT_EVERY;
    if(w->progress && w->progress(w->ctx, w->result->bytes, w->result->total)) {
      return UNZIP_ERR_CANCELLED;
    }
  }
  return UNZIP_OK;
}

static int
extract_file(const zip_t* z, const entry_t* e, const char* dest, unsigned char* in,
             unsigned char* out, writer_t* w) {
  unsigned char local[30];
  char path[2048];

  if(read_at(z->fd, local, sizeof(local), e->offset) || le32(local) != SIG_LOCAL) {
    return UNZIP_ERR_FORMAT;
  }
  const long long data = e->offset + 30 + le16(local + 26) + le16(local + 28);
  if(data > z->directory_offset || e->csize > z->directory_offset - data ||
     (e->method == 0 && e->csize != e->usize)) {
    return UNZIP_ERR_FORMAT;
  }
  if(make_folders(dest, e->name, 0)) {
    return UNZIP_ERR_WRITE;
  }
  snprintf(path, sizeof(path), "%s/%s", dest, e->name);
  w->fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0666);
  if(w->fd < 0) {
    return UNZIP_ERR_WRITE;
  }
  w->crc = (uint32_t)crc32(0, 0, 0);
  w->written = 0;
  w->expected = e->usize;

  int err = UNZIP_OK;
  long long left = e->csize, at = data;
  if(e->method == 0) {
    while(!err && left > 0) {
      const size_t n = left < CHUNK ? (size_t)left : CHUNK;
      err = read_at(z->fd, in, n, at) ? UNZIP_ERR_OPEN : put(w, in, n);
      at += (long long)n;
      left -= (long long)n;
    }
  } else if(e->csize == 0) {
    // Deflated, with no stream at all: some tools write empty files so.
  } else {
    z_stream s;
    memset(&s, 0, sizeof(s));
    if(inflateInit2(&s, -MAX_WBITS) != Z_OK) {
      err = UNZIP_ERR_OPEN;
    }
    int ended = 0;
    while(!err && !ended) {
      if(!s.avail_in) {
        if(left <= 0) {
          err = UNZIP_ERR_DATA;  // the stream stops short
          break;
        }
        const size_t n = left < CHUNK ? (size_t)left : CHUNK;
        if(read_at(z->fd, in, n, at)) {
          err = UNZIP_ERR_OPEN;
          break;
        }
        at += (long long)n;
        left -= (long long)n;
        s.next_in = in;
        s.avail_in = (uInt)n;
      }
      s.next_out = out;
      s.avail_out = CHUNK;
      const int r = inflate(&s, Z_NO_FLUSH);
      if(r != Z_OK && r != Z_STREAM_END && !(r == Z_BUF_ERROR && s.avail_in == 0)) {
        err = UNZIP_ERR_DATA;
        break;
      }
      err = put(w, out, CHUNK - s.avail_out);
      ended = r == Z_STREAM_END;
    }
    inflateEnd(&s);
  }
  if(!err && (w->written != e->usize || w->crc != e->crc)) {
    err = UNZIP_ERR_DATA;
  }
  if(close(w->fd) && !err) {
    err = UNZIP_ERR_WRITE;
  }
  if(err) {
    unlink(path);
  }
  return err;
}

int
unzip_extract(const char* zip_path, const char* dest_dir, long long free_bytes,
              unzip_progress_fn progress, void* ctx, unzip_result_t* result) {
  zip_t z;
  struct stat st;
  entry_t* e = (entry_t*)malloc(sizeof(entry_t));
  unsigned char* in = (unsigned char*)malloc(CHUNK);
  unsigned char* out = (unsigned char*)malloc(CHUNK);
  int err = UNZIP_OK;

  memset(result, 0, sizeof(*result));
  memset(&z, 0, sizeof(z));
  z.fd = open(zip_path, O_RDONLY);
  if(!e || !in || !out || z.fd < 0 || fstat(z.fd, &st)) {
    err = UNZIP_ERR_OPEN;
    goto end;
  }
  z.size = (long long)st.st_size;
  err = find_directory(&z);

  // First: all of it holds together, and what it takes.
  size_t at = 0;
  for(long long i = 0; !err && i < z.entries; i++) {
    err = next_entry(&z, &at, e);
    if(!err && e->kind == ENTRY_FILE) {
      result->total += e->usize;
    }
  }
  if(!err && free_bytes >= 0 && result->total + UNZIP_SPACE_MARGIN > free_bytes) {
    err = UNZIP_ERR_SPACE;
  }

  // Then: the files.
  writer_t w;
  memset(&w, 0, sizeof(w));
  w.result = result;
  w.progress = progress;
  w.ctx = ctx;
  w.next_report = REPORT_EVERY;
  at = 0;
  for(long long i = 0; !err && i < z.entries; i++) {
    err = next_entry(&z, &at, e);
    if(err) {
      break;
    }
    if(e->kind == ENTRY_SKIP) {
      result->skipped++;
    } else if(e->kind == ENTRY_DIR) {
      if(make_folders(dest_dir, e->name, 1)) {
        err = UNZIP_ERR_WRITE;
      } else {
        result->dirs++;
      }
    } else {
      err = extract_file(&z, e, dest_dir, in, out, &w);
      if(!err) {
        result->files++;
      }
    }
    if(err) {
      snprintf(result->entry, sizeof(result->entry), "%.255s", e->name);
    }
  }
  if(!err && progress && progress(ctx, result->bytes, result->total)) {
    err = UNZIP_ERR_CANCELLED;
  }

end:
  if(z.fd >= 0) {
    close(z.fd);
  }
  free(z.directory);
  free(e);
  free(in);
  free(out);
  return err;
}
