/* What comes from OUTSIDE, given to the browser's own code (2026-11-14): a .zip a site sent, the paths and headers the
   phone's page asks with, what is typed (or sent) as an address. This is browser/unzip.c, files.c and address.c AS
   THEY ARE, with AddressSanitizer and UndefinedBehaviorSanitizer (build-and-run.sh), on fixed cases and on
   thousands made by chance from good ones. Checked besides "no fall, nothing read or written outside its memory":

     - a .zip, whatever its names say ("../", "/tmp/...", "C:\", a link) and however broken, writes ONLY inside the
       folder it is extracted into, makes no links, and no more bytes than the room it was told there is;
     - a path the phone asks for is taken only if it is really inside a shared folder (links followed) and not in a
       private one; a Range taken is inside the file; what goes into a header has no line end in it;
     - the list of a folder is JSON a page can take, whatever its files are called;
     - what is typed becomes an address or a search that ends inside its place, and "javascript:" is never an address.

       inputs_fuzz <folder with the zips of make-zips.py> <an empty folder> [rounds]                              */

#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "address.h"
#include "files.h"
#include "unzip.h"
#include "json-check.h"

static int checks, failed, round_now;
static unsigned long long rng = 0x2545F4914F6CDD1Dull;

static unsigned
rnd(void) {
  rng ^= rng << 13;
  rng ^= rng >> 7;
  rng ^= rng << 17;
  return (unsigned)(rng >> 11);
}

/* What is being given to the browser's code now: said if the program is stopped (glibc's own checks stop it with
   "buffer overflow detected" where a place's size was told wrong, which no sanitizer sees). */
static const char* stage_now = "the start";
static char stage_with[3200];

static void
stage(const char* what, const char* with) {
  stage_now = what;
  snprintf(stage_with, sizeof(stage_with), "%s", with ? with : "");
}

static void
stopped(int sig) {
  char line[3600];
  int n = snprintf(line, sizeof(line), "STOPPED (signal %d) in round %d, at: %s  [", sig, round_now, stage_now);
  for(const unsigned char* c = (const unsigned char*)stage_with; *c && n + 8 < (int)sizeof(line); c++) {
    n += snprintf(line + n, sizeof(line) - (size_t)n, *c >= 0x20 && *c < 0x7f && *c != '\\' ? "%c" : "\\x%02x", *c);
  }
  n += snprintf(line + n, sizeof(line) - (size_t)n, "]\n");
  if(write(1, line, (size_t)n) < 0) {
  }
  _exit(3);
}

static void
check(int fine, const char* what, const char* with) {
  checks++;
  if(!fine) {
    failed++;
    if(failed <= 15) {
      printf("FAIL (round %d): %s  [", round_now, what);
      for(const unsigned char* c = (const unsigned char*)(with ? with : ""); *c && c - (const unsigned char*)with < 200; c++) {
        printf(*c >= 0x20 && *c < 0x7f && *c != '\\' ? "%c" : "\\x%02x", *c);
      }
      printf("]\n");
    }
  }
}

static char*
file_bytes(const char* path, size_t* length) {
  FILE* f = fopen(path, "rb");
  char* data = 0;
  *length = 0;
  if(f) {
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = (char*)malloc((size_t)(n > 0 ? n : 0) + 1);
    if(data) {
      *length = fread(data, 1, (size_t)(n > 0 ? n : 0), f);
    }
    fclose(f);
  }
  return data;
}

static void
file_put(const char* path, const char* data, size_t length) {
  FILE* f = fopen(path, "wb");
  if(f) {
    fwrite(data, 1, length, f);
    fclose(f);
  }
}

/* ---- the .zip ---- */

static char box[PATH_MAX], box_out[PATH_MAX], box_cwd[PATH_MAX];
static long long tree_bytes;
static int tree_outside, tree_links;
static char tree_bad[PATH_MAX];

static int
tree_look(const char* path, const struct stat* st, int type, struct FTW* f) {
  const size_t n = strlen(box_out);
  (void)f;
  if(!strcmp(path, box) || !strcmp(path, box_cwd)) {
    return 0;
  }
  if(strncmp(path, box_out, n) || (path[n] && path[n] != '/')) {
    tree_outside++;
    snprintf(tree_bad, sizeof(tree_bad), "%s", path);
  }
  if(type == FTW_SL || type == FTW_SLN) {
    tree_links++;
    snprintf(tree_bad, sizeof(tree_bad), "%s", path);
  } else if(type == FTW_F) {
    tree_bytes += st->st_size;
  }
  return 0;
}

static int
tree_remove(const char* path, const struct stat* st, int type, struct FTW* f) {
  (void)st;
  (void)type;
  (void)f;
  if(strcmp(path, box) && strcmp(path, box_cwd) && strcmp(path, box_out)) {
    remove(path);
  }
  return 0;
}

static int progress_stop_at;
static int
progress(void* ctx, long long done, long long total) {
  (void)ctx;
  (void)total;
  return progress_stop_at > 0 && done >= progress_stop_at;
}

static void
zip_broken(const char* good, const char* to) {
  static const char odd[] = {0, '/', '\\', '.', '.', '/', ':', 'P', 'K', 1, 2, 5, 6, 8, (char)0xff, (char)0x80, ' ', '*'};
  size_t n = 0;
  char* data = file_bytes(good, &n);
  if(!data) {
    return;
  }
  const int ways = rnd() % 4;  /* (0: as it is) */
  for(int w = 0; w < ways && n > 30; w++) {
    const int how = rnd() % 6;
    if(how == 0) {  /* cut short */
      n = 22 + rnd() % (n - 22);
    } else if(how == 1) {  /* bytes changed anywhere */
      const int k = 1 + rnd() % 6;
      for(int i = 0; i < k; i++) {
        data[rnd() % n] = rnd() % 2 ? odd[rnd() % sizeof(odd)] : (char)rnd();
      }
    } else if(how == 2) {  /* bytes changed near the end: the list of entries and its numbers */
      const size_t tail = n < 900 ? n : 900;
      const int k = 1 + rnd() % 5;
      for(int i = 0; i < k; i++) {
        data[n - 1 - rnd() % tail] = rnd() % 2 ? odd[rnd() % sizeof(odd)] : (char)rnd();
      }
    } else if(how == 3) {  /* "../" written over a piece of a name (or of anything) */
      static const char* const ups[] = {"../", "..\\", "/../", "//", "/tmp/", "C:\\"};
      const char* up = ups[rnd() % 6];
      const size_t at = rnd() % (n - 8);
      memcpy(data + at, up, strlen(up));
    } else if(how == 4) {  /* a number of four bytes becomes huge, or nothing */
      const size_t at = rnd() % (n - 4);
      const unsigned v = rnd() % 3 == 0 ? 0 : rnd() % 2 ? 0xffffffffu : 0x7fffffffu - rnd() % 9;
      memcpy(data + at, &v, 4);
    } else {  /* two pieces change places */
      const size_t a = rnd() % (n - 16), b = rnd() % (n - 16);
      char piece[16];
      memcpy(piece, data + a, 16);
      memmove(data + a, data + b, 16);
      memcpy(data + b, piece, 16);
    }
  }
  file_put(to, data, n);
  free(data);
}

static void
one_zip(const char* zips, const char* work) {
  static const char* const names[] = {"plain.zip", "evil.zip", "evil.zip", "bomb.zip"};
  static const long long rooms[] = {-1, 0, 4096, 1 << 20, 64 << 20};
  const char* name = names[rnd() % 4];
  char good[PATH_MAX], in[PATH_MAX];
  unzip_result_t result;
  long long room = rooms[rnd() % 5];

  if(!strcmp(name, "bomb.zip") && room < 0 && rnd() % 20) {
    room = 1 << 20;  /* (24 MiB written every time would be most of the test's time) */
  }
  snprintf(good, sizeof(good), "%s/%s", zips, name);
  snprintf(in, sizeof(in), "%s/in.zip", work);
  zip_broken(good, in);
  progress_stop_at = rnd() % 6 == 0 ? 1 + (int)(rnd() % 300000) : 0;
  memset(&result, 0x55, sizeof(result));
  stage("unzip_extract", name);
  const int err = unzip_extract(in, box_out, room, rnd() % 4 ? progress : 0, 0, &result);
  check(err <= 0 && err >= UNZIP_ERR_CANCELLED, "the .zip's answer is one of those there are", name);
  check(memchr(result.entry, 0, sizeof(result.entry)) != 0, "the entry an error is about ends inside its place", name);
  check(result.files >= 0 && result.dirs >= 0 && result.skipped >= 0 && result.bytes >= 0, "what was done is counted from nothing up", name);
  tree_bytes = 0;
  tree_outside = tree_links = 0;
  tree_bad[0] = 0;
  nftw(box, tree_look, 32, FTW_PHYS);
  check(!tree_outside, "a .zip writes only inside the folder it is extracted into", tree_bad);
  check(!tree_links, "a .zip makes no links", tree_bad);
  struct stat st;
  check(lstat("/tmp/bps5-zip-escape-test", &st) != 0, "a .zip writes nothing at a place its names choose", name);
  if(room >= 0) {
    check(tree_bytes <= room + (1 << 16), "a .zip writes no more than the room it was told there is", name);
  }
  if(!strcmp(name, "evil.zip")) {  /* (whatever was done to it: what says ESCAPED is never where it wanted to be) */
    char up[PATH_MAX];
    snprintf(up, sizeof(up), "%s/up1.txt", box);
    check(lstat(up, &st) != 0, "\"../\" in a name does not leave the folder", up);
  }
  nftw(box, tree_remove, 32, FTW_PHYS | FTW_DEPTH);
}

static void
safe_names(void) {
  static const char* const parts[] = {"a", "name.txt", "..", ".", "", "/", "\\", "C:", "caf\xc3\xa9", "\xff", "\xc3", "x y", "..\\", "../", ":", "*",
                                      "?", "\"", "<", ">", "|", "\x01", "\x7f", " ", ".hidden", "\xe4\xb8\xad", "con", "nul"};
  char name[2100];
  size_t n = 0;
  const int count = 1 + rnd() % 8;
  for(int i = 0; i < count && n < 1700; i++) {
    const char* p = parts[rnd() % (sizeof(parts) / sizeof(parts[0]))];
    const int times = rnd() % 30 == 0 ? 300 : 1;
    for(int t = 0; t < times && n + strlen(p) + 2 < sizeof(name); t++) {
      memcpy(name + n, p, strlen(p));
      n += strlen(p);
    }
    if(rnd() % 3) {
      name[n++] = rnd() % 4 ? '/' : '\\';
    }
  }
  char* exact = (char*)malloc(n ? n : 1);  /* (not ended with a 0: a byte read past it is seen) */
  memcpy(exact, name, n);
  name[n] = 0;
  const size_t size = rnd() % 4 == 0 ? 1 + rnd() % 40 : 700;
  char* out = (char*)malloc(size);
  memset(out, 'Z', size);
  stage("unzip_safe_name", name);
  const int err = unzip_safe_name(exact, n, out, size);
  if(!err) {
    int fine = memchr(out, 0, size) != 0;
    check(fine, "a safe name ends inside its place", name);
    if(fine) {
      check(out[0] && out[0] != '/', "a safe name is not empty and does not start at the root", name);
      check(utf8_valid(out), "a safe name is UTF-8", name);
      int parts_fine = 1;
      for(const char* p = out; *p;) {
        const size_t len = strcspn(p, "/");
        parts_fine = parts_fine && len > 0 && len <= 255 && !(len == 1 && p[0] == '.') && !(len == 2 && p[0] == '.' && p[1] == '.');
        p += len;
        if(*p == '/') {
          p++;
          parts_fine = parts_fine && *p;  /* (no "/" at its end, no "//") */
        }
      }
      check(parts_fine, "a safe name has no \"..\", \".\", empty or too long part", name);
      int chars_fine = 1;
      for(const unsigned char* c = (const unsigned char*)out; *c; c++) {
        chars_fine = chars_fine && *c >= 0x20 && *c != 0x7f && !strchr("\\:*?\"<>|", *c);
      }
      check(chars_fine, "a safe name has no character the drives refuse", name);
    }
  }
  free(out);
  free(exact);
  char folder[64];
  const size_t fsize = 2 + rnd() % 60;
  memset(folder, 'Z', sizeof(folder));
  stage("unzip_folder_name", name);
  unzip_folder_name(name, folder, fsize);
  check(memchr(folder, 0, fsize) != 0 && (fsize < 5 || folder[0]) && !strchr(folder, '/'), "the folder a .zip goes into has a name, with no folder in it", name);
}

/* ---- the files the phone asks for ---- */

static char root_a[PATH_MAX], root_b[PATH_MAX], outside[PATH_MAX], private_a[PATH_MAX];

static int
part_fine(const char* path) {
  if(path[0] != '/' || strlen(path) >= FILES_PATH_MAX || (path[1] && path[strlen(path) - 1] == '/')) {
    return 0;
  }
  for(const char* p = path + 1; *p;) {
    const size_t len = strcspn(p, "/");
    if(!len || p[0] == '.') {
      return 0;
    }
    p += len + (p[len] == '/');
  }
  return 1;
}

static int
under_dir(const char* path, const char* dir) {
  const size_t n = strlen(dir);
  return !strncmp(path, dir, n) && (!path[n] || path[n] == '/');
}

static void
one_path(void) {
  const char* const roots[] = {root_a, root_b};
  const char* const privates[] = {private_a};
  static const char* const parts[] = {"sub", "file.txt", "inner.txt", "..", ".", "link-out", "link-in", "link-private", "link-up", "private",
                                      "secret.txt", "b.txt", "passwd", ".hidden", "etc", "", "link-file-out", "o.txt", "nothing-here", "\xff", " "};
  char path[3000];
  size_t n = 0;
  const int start = rnd() % 10;
  n = (size_t)snprintf(path, sizeof(path), "%s", start < 5 ? root_a : start < 8 ? root_b : start == 8 ? outside : "");
  const int count = rnd() % 7;
  for(int i = 0; i < count && n < 2000; i++) {
    const char* p = parts[rnd() % (sizeof(parts) / sizeof(parts[0]))];
    const int times = rnd() % 60 == 0 ? 400 : 1;
    n += (size_t)snprintf(path + n, sizeof(path) - n, "%s", rnd() % 12 ? "/" : rnd() % 2 ? "//" : "");
    for(int t = 0; t < times && n < 2400; t++) {
      n += (size_t)snprintf(path + n, sizeof(path) - n, "%s", p);
    }
  }
  if(rnd() % 15 == 0) {
    n += (size_t)snprintf(path + n, sizeof(path) - n, "/");
  }
  stage("files_inside", path);
  const int in = files_inside(path, roots, 2);
  check(in >= -1 && in < 2, "a path is in one of the roots, or in none", path);
  if(in >= 0) {
    check(part_fine(path) && under_dir(path, roots[in]), "a path taken by its text is under its root, with no \"..\", hidden or empty part", path);
  }
  char real[PATH_MAX];  /* (realpath() wants PATH_MAX bytes, whatever the path) */
  const size_t size = rnd() % 5 == 0 ? 1 + rnd() % 80 : FILES_PATH_MAX;
  char* out = (char*)malloc(size);
  memset(out, 'Z', size);
  stage("files_resolve", path);
  const int root = files_resolve(path, roots, 2, privates, 1, out, size);
  check(root >= -1 && root < 2, "a real path is in one of the roots, or in none", path);
  if(root >= 0) {
    const int ended = memchr(out, 0, size) != 0;
    check(ended, "a real path ends inside its place", path);
    if(ended) {
      struct stat st;
      check(under_dir(out, root_a) || under_dir(out, root_b), "a path taken is REALLY inside a shared folder (links followed)", path);
      check(!under_dir(out, private_a), "a path taken is not in a private folder", path);
      check(!lstat(out, &st) && !S_ISLNK(st.st_mode), "a path taken is there, and is not a link", path);
      check(realpath(out, real) && !strcmp(real, out), "the real path given is the real one", path);
    }
  }
  free(out);
}

static void
headers(void) {
  static const char* const pieces[] = {"bytes=", "bytes", "=", "-", "0", "1", "99", "100", "101", "4294967296", "9223372036854775807",
                                       "99999999999999999999999", ",", " ", "\t", "-1", "abc", "\r\n", "items=", ""};
  char header[400];
  size_t n = 0;
  const int count = 1 + rnd() % 6;
  for(int i = 0; i < count; i++) {
    n += (size_t)snprintf(header + n, sizeof(header) - n, "%s", pieces[rnd() % (sizeof(pieces) / sizeof(pieces[0]))]);
  }
  static const long long sizes[] = {0, 1, 100, 101, 5000000000LL, 0x7fffffffffffffffLL};
  const long long size = sizes[rnd() % 6];
  long long from = -5, to = -5;
  stage("files_range", header);
  const int r = files_range(rnd() % 30 ? header : 0, size, &from, &to);
  check(r >= -1 && r <= 1, "a Range's answer is one of those there are", header);
  if(r == 0) {
    check(from >= 0 && from <= to && to < size, "a Range taken is inside the file", header);
  }
  /* a name into a header */
  static const char* const names[] = {"a.txt", "caf\xc3\xa9.mp4", "q\"uote.bin", "line\r\nSet-Cookie: x=1", "back\\slash", "\xff\xfe.dat", "semi;colon",
                                      "x", "", "/", "nul", "%41", "\xe4\xb8\xad\xe6\x96\x87.pdf", "tab\tname"};
  char path[300];
  snprintf(path, sizeof(path), "/data/%s%s", rnd() % 4 ? "" : "dir/", names[rnd() % (sizeof(names) / sizeof(names[0]))]);
  const size_t room = rnd() % 4 == 0 ? 1 + rnd() % 60 : 1200;
  char* out = (char*)malloc(room);
  memset(out, 'Z', room);
  stage("files_disposition", path);
  files_disposition(path, out, room);
  const int ended = memchr(out, 0, room) != 0;
  check(ended, "what goes into a header ends inside its place", path);
  if(ended) {
    int fine = 1;
    for(const unsigned char* c = (const unsigned char*)out; *c; c++) {
      fine = fine && *c >= 0x20 && *c < 0x7f;
    }
    check(fine, "what goes into a header is plain text of one line", path);
    const char* q = strstr(out, "filename=\"");
    if(q && room >= 1200) {
      const char* end = strchr(q + 10, '"');
      check(end && (end[1] == ';' || !end[1]), "the quoted name in a header ends where its quote does", path);
    }
  }
  free(out);
  static const char* const ends[] = {".html", ".HTM", ".svg", ".xhtml", ".js", ".mp4", ".txt", ".xml", ".SVGZ", "", ".pdf", ".shtml", ".mht"};
  char file[80];
  snprintf(file, sizeof(file), "page%s", ends[rnd() % (sizeof(ends) / sizeof(ends[0]))]);
  stage("files_type", file);
  const char* type = files_type(file);
  check(type && !strcasestr(type, "html") && !strcasestr(type, "svg") && !strcasestr(type, "script") && !strcasestr(type, "xml"),
        "a file never goes out as something the phone's page would run", file);
}

/* ---- what is typed as an address ---- */

static void
typed(void) {
  static const char* const pieces[] = {"example.com", "localhost", "192.168.1.1", "nas:8080", "http://", "https://", "file:///", "javascript:", "data:text/html,",
                                       "about:blank", " ", "?q=", "#", "/", "[::1]", "\xc3\xa9", "%", "\n", "\t", "a b", ":", "..", "@", "JAVASCRIPT:", "www.",
                                       ".pt", "256.1.1.1", ":99999", "-", "x", "\xff", "<script>", "&", "+", "\r"};
  char text[900];
  size_t n = 0;
  const int count = rnd() % 7;
  for(int i = 0; i < count; i++) {
    const char* p = pieces[rnd() % (sizeof(pieces) / sizeof(pieces[0]))];
    const int times = rnd() % 50 == 0 ? 60 : 1;
    for(int t = 0; t < times && n + 20 < sizeof(text); t++) {
      n += (size_t)snprintf(text + n, sizeof(text) - n, "%s", p);
    }
  }
  text[n] = 0;
  /* (the place given is always longer than where searches go: every caller's is 2048 or more) */
  const size_t size = rnd() % 3 == 0 ? 80 + rnd() % 300 : 2048;
  char* url = (char*)malloc(size);
  memset(url, 'Z', size);
  stage("address_to_url", text);
  const address_kind_t kind = address_to_url(text, url, size);
  const int ended = memchr(url, 0, size) != 0;
  check(ended, "what is typed becomes a text that ends inside its place", text);
  if(ended) {
    check(kind != ADDRESS_NONE || !url[0], "nothing typed is nothing to load", text);
    check(kind == ADDRESS_NONE || url[0], "something typed is something to load", text);
    check(strncasecmp(url, "javascript:", 11) != 0, "\"javascript:\" typed is never an address", text);
    if(kind == ADDRESS_SEARCH) {
      int fine = !strncmp(url, ADDRESS_SEARCH_URL, strlen(ADDRESS_SEARCH_URL));
      for(const unsigned char* c = (const unsigned char*)url + strlen(ADDRESS_SEARCH_URL); fine && *c; c++) {
        fine = (*c >= '0' && *c <= '9') || (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || strchr("-_.~+%", *c);
      }
      check(fine, "a search is where searches go, with its words encoded", text);
    }
    if(kind == ADDRESS_URL) {
      check(!strchr(url, ' '), "an address has no space in it", text);
    }
  }
  free(url);
  const size_t room = 1 + rnd() % 200;
  char* out = (char*)malloc(room);
  memset(out, 'Z', room);
  stage("address_decode", text);
  address_decode(text, out, room);
  check(memchr(out, 0, room) != 0, "what is decoded ends inside its place", text);
  free(out);
}

int
main(int argc, char** argv) {
  const int rounds = argc > 3 ? atoi(argv[3]) : 20000;
  char work[PATH_MAX], path[PATH_MAX + 64];

  if(argc < 3 || !realpath(argv[2], work)) {
    printf("inputs_fuzz <folder with the zips> <an empty folder> [rounds]\n");
    return 2;
  }
  signal(SIGABRT, stopped);
  snprintf(box, sizeof(box), "%s/box", work);
  snprintf(box_out, sizeof(box_out), "%s/box/out", work);
  snprintf(box_cwd, sizeof(box_cwd), "%s/box/cwd", work);
  mkdir(box, 0755);
  mkdir(box_cwd, 0755);
  /* the shared folders: A (with a private one, and links that lead out, to B, to the private one, up), B; and outside */
  snprintf(root_a, sizeof(root_a), "%s/roots/A", work);
  snprintf(root_b, sizeof(root_b), "%s/roots/B", work);
  snprintf(outside, sizeof(outside), "%s/outside", work);
  snprintf(private_a, sizeof(private_a), "%s/roots/A/private", work);
  snprintf(path, sizeof(path), "%s/roots", work);
  mkdir(path, 0755);
  mkdir(root_a, 0755);
  mkdir(root_b, 0755);
  mkdir(outside, 0755);
  mkdir(private_a, 0755);
  snprintf(path, sizeof(path), "%s/sub", root_a);
  mkdir(path, 0755);
  snprintf(path, sizeof(path), "%s/file.txt", root_a);
  file_put(path, "a", 1);
  snprintf(path, sizeof(path), "%s/.hidden", root_a);
  file_put(path, "h", 1);
  snprintf(path, sizeof(path), "%s/sub/inner.txt", root_a);
  file_put(path, "i", 1);
  snprintf(path, sizeof(path), "%s/secret.txt", private_a);
  file_put(path, "s", 1);
  snprintf(path, sizeof(path), "%s/b.txt", root_b);
  file_put(path, "b", 1);
  snprintf(path, sizeof(path), "%s/o.txt", outside);
  file_put(path, "o", 1);
  snprintf(path, sizeof(path), "%s/link-out", root_a);
  check(!symlink("/etc", path), "the test's links are made", path);
  snprintf(path, sizeof(path), "%s/link-in", root_a);
  check(!symlink("../B", path), "the test's links are made", path);
  snprintf(path, sizeof(path), "%s/link-private", root_a);
  check(!symlink("private", path), "the test's links are made", path);
  snprintf(path, sizeof(path), "%s/link-up", root_a);
  check(!symlink("..", path), "the test's links are made", path);
  snprintf(path, sizeof(path), "%s/sub/link-file-out", root_a);
  check(!symlink("/etc/passwd", path), "the test's links are made", path);
  if(chdir(box_cwd)) {
    perror(box_cwd);
    return 2;
  }
  /* a folder whose files have any names: its list is JSON all the same */
  {
    static const char* const odd[] = {"a\"quote.txt", "back\\slash.bin", "new\nline", "tab\tname", "\xff\xfe-not-utf8", "ok.txt", "caf\xc3\xa9.mp4",
                                      ".hidden", "ctl\x01\x1f", "half\xc3", "long-"};
    char dir[PATH_MAX];
    snprintf(dir, sizeof(dir), "%s/odd", work);
    mkdir(dir, 0755);
    for(const char* const* o = odd; o < odd + sizeof(odd) / sizeof(odd[0]); o++) {
      snprintf(path, sizeof(path), "%s/%s", dir, *o);
      file_put(path, "x", 1);
    }
    snprintf(path, sizeof(path), "%s/a folder", dir);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/a link", dir);
    check(!symlink("/etc", path), "the test's links are made", path);
    stage("files_list_json", dir);
    char* json = files_list_json(dir, 0, 0);
    check(json_valid(json), "the list of a folder is JSON a page can take, whatever its files are called", json);
    check(json && !strstr(json, "a link") && !strstr(json, ".hidden"), "links and hidden names are not in a folder's list", json);
    free(json);
    json = files_list_json(root_a, (const char* const[]){private_a}, 1);
    check(json_valid(json) && !strstr(json, "private") && !strstr(json, "link-") && strstr(json, "file.txt"),
          "a shared folder's list has its files, not its private folder nor its links", json);
    free(json);
  }
  for(round_now = 1; round_now <= rounds; round_now++) {
    mkdir(box_out, 0755);
    if(round_now % 4 == 0) {  /* (a .zip is written to the disk each time: the slowest of these) */
      one_zip(argv[1], work);
    }
    safe_names();
    one_path();
    headers();
    typed();
  }
  printf("%s: %d checks, %d failed (%d rounds: .zip files, the phone's paths and headers, what is typed)\n", failed ? "FAIL" : "PASS", checks,
         failed, rounds);
  return failed ? 1 : 0;
}
