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

// remove_files: remove what this project put on the PS5 and no longer
// needs (test titles, libraries of old experiments).
//
// It reads paths on stdin, one per line, which send.ps1 -InputFile sends
// right after the ELF. A file is removed, a directory with everything in
// it. Like install_tar, only paths inside /user/homebrew/ and
// /data/homebrew/ are touched, never those folders themselves.
//
//   .\send.ps1 -Ps5Host IP -Payload remove_files\remove_files.elf `
//              -InputFile paths.txt

#ifndef POC_NAME  // (release-rn/uninstall.c is this file under its own name)
#define POC_NAME "remove_files"
#endif

#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/stat.h>

#include "poc_log.h"

#define ALLOWED  "/user/homebrew/"
#define ALLOWED2 "/data/homebrew/"


static int
allowed_path(const char* path) {
  size_t len = strlen(path);

  if((strncmp(path, ALLOWED, strlen(ALLOWED)) || len <= strlen(ALLOWED)) &&
     (strncmp(path, ALLOWED2, strlen(ALLOWED2)) || len <= strlen(ALLOWED2))) {
    return 0;
  }
  return !strstr(path, "/../") && !strstr(path, "//") &&
    strcmp(path + len - 1, "/") && !(len >= 3 && !strcmp(path + len - 3, "/.."));
}


/**
 * Remove path and, for a directory, everything in it. Returns the number
 * of entries removed, or -1 on an error.
 **/
static int
remove_tree(const char* path) {
  struct stat st;
  char child[1024];
  struct dirent* e;
  DIR* dir;
  int count = 0;
  int n;

  if(lstat(path, &st)) {
    POC_PERROR(path);
    return -1;
  }
  if(S_ISDIR(st.st_mode)) {
    int failed = 0;
    chmod(path, 0777);  // (a folder closed to this process: its files could not be taken out of it)
    // 2026-11-13: a folder a title made for itself (PPSA97024/web: mode without access for this process) could
    // not be opened, and the walk stopped there with half of the title gone. Its mode is opened up first, and
    // one entry that cannot go no longer keeps the others.
    if(!(dir=opendir(path))) {
      POC_LOG("%s: mode %o, owner %d: not opened (%s); with its mode changed:", path, (unsigned)(st.st_mode & 07777),
              (int)st.st_uid, strerror(errno));
      chmod(path, 0777);
      dir = opendir(path);
    }
    if(!dir) {
      POC_PERROR(path);
      return -1;
    }
    while((e=readdir(dir))) {
      if(!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) {
	continue;
      }
      snprintf(child, sizeof(child), "%s/%s", path, e->d_name);
      if((n=remove_tree(child)) < 0) {
	failed = 1;
	continue;
      }
      count += n;
    }
    closedir(dir);
    if(failed) {
      return -1;
    }
    if(rmdir(path)) {
      POC_PERROR(path);
      return -1;
    }
  } else if(unlink(path)) {
    POC_PERROR(path);
    return -1;
  }
  return count + 1;
}


int
main(void) {
  char line[1024];
  int removed = 0, failed = 0;
  int n;

  while(fgets(line, sizeof(line), stdin)) {
    line[strcspn(line, "\r\n")] = 0;
    if(!line[0]) {
      continue;
    }
    if(!allowed_path(line)) {
      POC_LOG("refused %s: only paths inside %s or %s are removed", line,
	      ALLOWED, ALLOWED2);
      failed++;
      continue;
    }
    if((n=remove_tree(line)) < 0) {
      failed++;
    } else {
      POC_LOG("removed %s (%d entries)", line, n);
      removed++;
    }
  }
  POC_RESULT(removed && !failed, "removed %d paths, %d failed", removed,
	     failed);
  return 0;
}
