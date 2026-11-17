/* Copyright (C) 2026 BrowserPS5 contributors. GPL-3.0-or-later.
   files.c on the PC (make test): a folder made for it in /tmp, with hidden
   names, links that lead out, a private folder and names that are not UTF-8. */

#define _XOPEN_SOURCE 700
#include "files.h"

#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures, passes;

#define CHECK(c, ...) do { if(c) { passes++; printf("PASS "); } else { failures++; \
  printf("FAIL line %d: ", __LINE__); } printf(__VA_ARGS__); printf("\n"); } while(0)

static char top[256];

static void
make_file(const char* rel, const char* text) {
  char path[512];
  snprintf(path, sizeof(path), "%s/%s", top, rel);
  FILE* f = fopen(path, "w");
  if(f) {
    fputs(text, f);
    fclose(f);
  }
}

static void
make_dir(const char* rel) {
  char path[512];
  snprintf(path, sizeof(path), "%s/%s", top, rel);
  mkdir(path, 0777);
}

static int
gone(const char* path, const struct stat* st, int flag, struct FTW* ftw) {
  (void)st; (void)flag; (void)ftw;
  return remove(path);
}

int
main(void) {
  snprintf(top, sizeof(top), "/tmp/files_test.XXXXXX");
  if(!mkdtemp(top)) {
    printf("FAIL: no folder in /tmp\n");
    return 1;
  }
  char down[300], media[300], priv[300], path[600], out[1200];
  snprintf(down, sizeof(down), "%s/downloads", top);
  snprintf(media, sizeof(media), "%s/media", top);
  snprintf(priv, sizeof(priv), "%s/downloads/private", top);
  make_dir("downloads");
  make_dir("media");
  make_dir("downloads/private");
  make_dir("downloads/Sub");
  make_dir("downloads/empty");
  make_dir("outside");
  make_file("downloads/b.mp4", "0123456789");
  make_file("downloads/a.txt", "hello");
  make_file("downloads/.hidden", "x");
  make_file("downloads/Sub/c.zip", "zip");
  make_file("downloads/private/secret.txt", "s");
  make_file("downloads/quote\"back\\.txt", "q");
  make_file("downloads/bad\xff.txt", "b");
  make_file("outside/o.txt", "o");
  snprintf(path, sizeof(path), "%s/outside/o.txt", top);
  char link[600];
  snprintf(link, sizeof(link), "%s/link-out.txt", down);
  CHECK(!symlink(path, link), "a link made");
  snprintf(link, sizeof(link), "%s/link-dir", down);
  snprintf(path, sizeof(path), "%s/outside", top);
  CHECK(!symlink(path, link), "a link made");

  const char* roots[] = {down, media};
  const char* privs[] = {priv};

  // By the text.
  CHECK(files_inside(down, roots, 2) == 0, "a root itself is in");
  snprintf(path, sizeof(path), "%s/a.txt", media);
  CHECK(files_inside(path, roots, 2) == 1, "a file of the second root");
  snprintf(path, sizeof(path), "%s/../outside/o.txt", down);
  CHECK(files_inside(path, roots, 2) < 0, ".. is refused");
  snprintf(path, sizeof(path), "%s/./a.txt", down);
  CHECK(files_inside(path, roots, 2) < 0, ". is refused");
  snprintf(path, sizeof(path), "%s//a.txt", down);
  CHECK(files_inside(path, roots, 2) < 0, "an empty part is refused");
  snprintf(path, sizeof(path), "%s/", down);
  CHECK(files_inside(path, roots, 2) < 0, "an ending / is refused");
  snprintf(path, sizeof(path), "%s/.hidden", down);
  CHECK(files_inside(path, roots, 2) < 0, "a hidden name is refused");
  snprintf(path, sizeof(path), "%s-other/a.txt", down);
  CHECK(files_inside(path, roots, 2) < 0, "a folder that only starts like a root is not in it");
  CHECK(files_inside("downloads/a.txt", roots, 2) < 0, "a relative path is refused");
  char longer[1100];
  memset(longer, 'a', sizeof(longer) - 1);
  longer[0] = '/';
  longer[sizeof(longer) - 1] = 0;
  CHECK(files_inside(longer, roots, 2) < 0, "a path of 1 KiB or more is refused");

  // By the real path.
  char real[1100];
  snprintf(path, sizeof(path), "%s/a.txt", down);
  CHECK(files_resolve(path, roots, 2, privs, 1, real, sizeof(real)) == 0 && strstr(real, "/downloads/a.txt"),
        "a plain file resolves (%s)", real);
  snprintf(path, sizeof(path), "%s/link-out.txt", down);
  CHECK(files_resolve(path, roots, 2, privs, 1, real, sizeof(real)) < 0, "a link leading out is refused");
  snprintf(path, sizeof(path), "%s/link-dir/o.txt", down);
  CHECK(files_resolve(path, roots, 2, privs, 1, real, sizeof(real)) < 0, "a folder link leading out is refused");
  snprintf(path, sizeof(path), "%s/private/secret.txt", down);
  CHECK(files_resolve(path, roots, 2, privs, 1, real, sizeof(real)) < 0, "a private folder's file is refused");
  CHECK(files_resolve(priv, roots, 2, privs, 1, real, sizeof(real)) < 0, "the private folder itself is refused");
  snprintf(path, sizeof(path), "%s/nothing.txt", down);
  CHECK(files_resolve(path, roots, 2, privs, 1, real, sizeof(real)) < 0, "what is not there is refused");
  snprintf(path, sizeof(path), "%s/a.txt", down);
  CHECK(files_resolve(path, roots, 2, privs, 1, real, 8) < 0, "a real path that does not fit is refused");

  // A folder.
  char* json = files_list_json(down, privs, 1);
  CHECK(json != 0, "the folder reads");
  if(json) {
    const char* sub = strstr(json, "\"Sub\"");
    const char* empty = strstr(json, "\"empty\"");
    const char* a = strstr(json, "\"a.txt\"");
    const char* b = strstr(json, "\"b.mp4\"");
    CHECK(sub && empty && a && b && empty < a && sub < a && a < b, "folders first, then by name");
    CHECK(empty < sub, "case ignored (empty before Sub)");
    CHECK(!strstr(json, "hidden"), "hidden names are left out");
    CHECK(!strstr(json, "private"), "the private folder is left out");
    CHECK(!strstr(json, "link-out") && !strstr(json, "link-dir"), "links are left out");
    CHECK(strstr(json, "\"name\": \"b.mp4\", \"dir\": false, \"size\": 10") != 0, "a file's size");
    CHECK(strstr(json, "\"name\": \"Sub\", \"dir\": true, \"size\": 0") != 0, "a folder");
    CHECK(strstr(json, "\"quote\\\"back\\\\.txt\"") != 0, "quotes and backslashes escaped");
    CHECK(strstr(json, "\"bad\\ufffd.txt\"") && strstr(json, "\"bad\": true"), "a name not UTF-8 is marked");
    CHECK(strstr(json, "\"more\": 0}") != 0, "nothing more");
    free(json);
  }
  CHECK(files_list_json("/nothing/here", 0, 0) == 0, "a folder that is not there gives 0");
  json = files_list_json(down, 0, 0);
  CHECK(json && strstr(json, "\"private\""), "with no private folders, it shows");
  free(json);

  // Types.
  CHECK(!strcmp(files_type("x/Film.MP4"), "video/mp4"), "mp4, any case");
  CHECK(!strcmp(files_type("a.html"), "application/octet-stream"), "HTML is a download");
  CHECK(!strcmp(files_type("a.svg"), "application/octet-stream"), "SVG is a download");
  CHECK(!strcmp(files_type("noext"), "application/octet-stream"), "no extension");
  CHECK(!strcmp(files_type("dir.mp4/readme"), "application/octet-stream"), "a folder's dot does not count");
  CHECK(files_media("a.jpg") && files_media("b.flac") && !files_media("c.zip") && !files_media("d.txt"),
        "media names");

  // The name it goes out with.
  files_disposition("/x/Férias \"2026\";%.mp4", out, sizeof(out));
  CHECK(!strcmp(out, "attachment; filename=\"F__rias _2026___.mp4\"; "
                     "filename*=UTF-8''F%C3%A9rias%20%222026%22%3B%25.mp4"), "the name: %s", out);
  char small[70];
  memset(longer, 'n', 400);
  longer[0] = '/';
  longer[400] = 0;
  files_disposition(longer, small, sizeof(small));
  CHECK(strlen(small) < sizeof(small) && !strncmp(small, "attachment; filename=\"", 22), "a long name is cut whole");

  // Ranges.
  long long from = -1, to = -1;
  CHECK(files_range(0, 100, &from, &to) == 1, "no range: all of it");
  CHECK(files_range("bytes=0-", 100, &from, &to) == 0 && from == 0 && to == 99, "0-");
  CHECK(files_range("bytes=10-19", 100, &from, &to) == 0 && from == 10 && to == 19, "10-19");
  CHECK(files_range("bytes=90-500", 100, &from, &to) == 0 && from == 90 && to == 99, "past the end is cut");
  CHECK(files_range("bytes=-30", 100, &from, &to) == 0 && from == 70 && to == 99, "the last 30");
  CHECK(files_range("bytes=-300", 100, &from, &to) == 0 && from == 0 && to == 99, "the last 300 of 100");
  CHECK(files_range("bytes=100-", 100, &from, &to) == -1, "from the end: 416");
  CHECK(files_range("bytes=-0", 100, &from, &to) == -1, "the last 0: 416");
  CHECK(files_range("bytes=0-", 0, &from, &to) == -1, "an empty file: 416");
  CHECK(files_range("bytes=5-1", 100, &from, &to) == 1, "backwards: all of it");
  CHECK(files_range("bytes=0-1,5-6", 100, &from, &to) == 1, "several: all of it");
  CHECK(files_range("items=0-5", 100, &from, &to) == 1, "another unit: all of it");
  CHECK(files_range("bytes=x-5", 100, &from, &to) == 1, "garbage: all of it");
  CHECK(files_range("bytes=5000000000-", 6000000000LL, &from, &to) == 0 && from == 5000000000LL,
        "past 4 GiB");

  // Removing.
  snprintf(path, sizeof(path), "%s/Sub", down);
  CHECK(files_remove(path) == ENOTEMPTY, "a folder with things in it stays");
  snprintf(path, sizeof(path), "%s/empty", down);
  CHECK(files_remove(path) == 0 && access(path, F_OK), "an empty folder goes");
  snprintf(path, sizeof(path), "%s/a.txt", down);
  CHECK(files_remove(path) == 0 && access(path, F_OK), "a file goes");
  snprintf(path, sizeof(path), "%s/link-out.txt", down);
  char target[600];
  snprintf(target, sizeof(target), "%s/outside/o.txt", top);
  CHECK(files_remove(path) == EPERM && !access(target, F_OK), "a link stays, and so does its target");
  CHECK(files_remove("/nothing/here") == ENOENT, "what is not there");

  nftw(top, gone, 16, FTW_DEPTH | FTW_PHYS);
  printf("%d PASS, %d FAIL\n", passes, failures);
  return failures != 0;
}
