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

// install_tar: copy files to the PS5 for payloads that use shared
// libraries (the payload loader opens them from /user/homebrew/lib).
//
// It reads a tar archive on stdin, which send.ps1 -InputFile sends right
// after the ELF, and writes its regular files under /user/homebrew/, or
// under /data/homebrew/, where ShadowMountPlus finds directory titles.
// Other paths are refused, so it never writes anywhere else on the
// console. Each file is written next to its place and then renamed,
// so a dropped connection never leaves half a library behind.
//
//   .\send.ps1 -Ps5Host IP -Payload install_tar\install_tar.elf `
//              -InputFile gl_soft\mesa-soft-libs.tar

#ifndef POC_NAME  // (release-rn/install.c is this file under its own name)
#define POC_NAME "install_tar"
#endif

#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/stat.h>

#include "poc_log.h"

#define BLOCK   512
#define CHUNK   (1 << 20)
#define ALLOWED  "user/homebrew/"
#define ALLOWED2 "data/homebrew/"


/**
 * Read exactly len bytes. Returns 0, 1 on end of input before the first
 * byte, or -1 on an error or a short read.
 **/
static int
read_full(int fd, void* buf, size_t len) {
  size_t got = 0;

  while(got < len) {
    ssize_t n = read(fd, (char*)buf + got, len - got);

    if(n < 0) {
      return -1;
    }
    if(n == 0) {
      return got ? -1 : 1;
    }
    got += n;
  }
  return 0;
}


static int
is_zero(const char* block) {
  for(int i=0; i<BLOCK; i++) {
    if(block[i]) {
      return 0;
    }
  }
  return 1;
}


static uint64_t
octal(const char* p, size_t len) {
  uint64_t v = 0;

  for(size_t i=0; i<len && p[i]; i++) {
    if(p[i] >= '0' && p[i] <= '7') {
      v = v * 8 + (p[i] - '0');
    }
  }
  return v;
}


/**
 * Only relative paths under user/homebrew/ or data/homebrew/, with no ".."
 * component.
 **/
static int
allowed_name(const char* name) {
  size_t len = strlen(name);

  if((strncmp(name, ALLOWED, strlen(ALLOWED)) || len <= strlen(ALLOWED)) &&
     (strncmp(name, ALLOWED2, strlen(ALLOWED2)) || len <= strlen(ALLOWED2))) {
    return 0;
  }
  if(strstr(name, "/../") || strstr(name, "//") ||
     (len >= 3 && !strcmp(name + len - 3, "/.."))) {
    return 0;
  }
  return 1;
}


/**
 * Create the directories in path up to its last '/'.
 **/
static int
make_parents(char* path) {
  for(char* p=path + 1; *p; p++) {
    if(*p != '/') {
      continue;
    }
    *p = 0;
    if(mkdir(path, 0777) && errno != EEXIST) {
      POC_PERROR(path);
      *p = '/';
      return -1;
    }
    *p = '/';
  }
  return 0;
}


static int
skip(uint64_t size) {
  static char buf[BLOCK];

  for(uint64_t left=(size + BLOCK - 1) / BLOCK; left; left--) {
    if(read_full(0, buf, BLOCK)) {
      return -1;
    }
  }
  return 0;
}


static int
write_file(const char* path, uint64_t size) {
  char part[300];
  char* buf;
  int fd;

  snprintf(part, sizeof(part), "%s.part", path);
  if((fd=open(part, O_WRONLY | O_CREAT | O_TRUNC, 0755)) < 0) {
    POC_PERROR(part);
    return -1;
  }
  if(!(buf=malloc(CHUNK))) {
    close(fd);
    return -1;
  }
  for(uint64_t left=size; left; ) {
    size_t n = left < CHUNK ? left : CHUNK;

    if(read_full(0, buf, n) || write(fd, buf, n) != (ssize_t)n) {
      POC_LOG("%s: short read or write", part);
      free(buf);
      close(fd);
      unlink(part);
      return -1;
    }
    left -= n;
  }
  free(buf);
  close(fd);

  // The tar data is padded to a whole block.
  if(size % BLOCK) {
    char pad[BLOCK];

    if(read_full(0, pad, BLOCK - size % BLOCK)) {
      unlink(part);
      return -1;
    }
  }
  if(rename(part, path)) {
    POC_PERROR(path);
    unlink(part);
    return -1;
  }
  return 0;
}


int
main(void) {
  double start = poc_now_ms();
  uint64_t bytes = 0;
  int files = 0, skipped = 0;
  char hdr[BLOCK];

  for(;;) {
    char name[256], path[260];
    uint64_t size;
    int r, type;

    if((r=read_full(0, hdr, BLOCK)) == 1) {
      break;
    }
    if(r) {
      POC_RESULT(0, "the archive ended in the middle of a header");
      return 1;
    }
    if(is_zero(hdr)) {
      break;  // the end-of-archive block
    }
    if(strncmp(hdr + 257, "ustar", 5)) {
      POC_RESULT(0, "not a tar archive (no ustar header)");
      return 1;
    }

    // ustar splits long names into a prefix (155 bytes) and a name (100).
    if(hdr[345]) {
      snprintf(name, sizeof(name), "%.155s/%.100s", hdr + 345, hdr);
    } else {
      snprintf(name, sizeof(name), "%.100s", hdr);
    }
    size = octal(hdr + 124, 12);
    type = hdr[156];

    if(!allowed_name(name)) {
      POC_RESULT(0, "refused %s: only paths under %s or %s are installed",
		 name, ALLOWED, ALLOWED2);
      return 1;
    }
    snprintf(path, sizeof(path), "/%s", name);

    if(type == '5') {
      if(make_parents(path)) {
	return 1;
      }
      continue;
    }
    if(type != '0' && type != 0) {
      POC_LOG("skipped %s (tar entry type '%c', not a regular file)", path,
	      type);
      skipped++;
      if(skip(size)) {
	return 1;
      }
      continue;
    }
#ifdef INSTALL_TAR_KEEP  // (release-rn/install.c: a file other apps share is written only where there is none)
    if(INSTALL_TAR_KEEP(path)) {
      POC_LOG("  kept as it is: %s", path);
      skipped++;
      if(skip(size)) {
	return 1;
      }
      continue;
    }
#endif
    if(make_parents(path) || write_file(path, size)) {
      POC_RESULT(0, "install stopped after %d files", files);
      return 1;
    }
    POC_LOG("  %10llu %s", (unsigned long long)size, path);
    files++;
    bytes += size;
  }

  POC_RESULT(files > 0, "installed %d files (%.1f MiB) in %.1f s, skipped %d",
	     files, bytes / 1048576.0, (poc_now_ms() - start) / 1000.0,
	     skipped);
  return files > 0 ? 0 : 1;
}
