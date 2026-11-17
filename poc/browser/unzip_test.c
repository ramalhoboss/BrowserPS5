/*   Copyright (C) 2026 BrowserPS5 contributors. GPL-3.0-or-later.
 *
 * unzip.c on the build machine (make test). The ZIPs are written here, by
 * a small writer of our own (zlib for the deflated ones): a normal one, one
 * with names that try to leave the folder and entries to skip, a broken
 * CRC, an entry bigger than it says, ZIP64, not a ZIP, cut short, no room,
 * stopped half-way. And the names: unzip_safe_name, unzip_folder_name.
 */
#define _XOPEN_SOURCE 700
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

#include "unzip.h"

static int failed;

#define CHECK(what, cond)                                   \
  do {                                                      \
    if(cond) {                                              \
      printf("PASS %s\n", what);                            \
    } else {                                                \
      printf("FAIL %s\n", what);                            \
      failed = 1;                                           \
    }                                                       \
  } while(0)

/* The writer --------------------------------------------------------------- */

enum {
  Z_ENCRYPTED = 1,  // flag bit 0
  Z_LINK = 2,       // made on Unix, a symbolic link
  Z_BAD_CRC = 4,
  Z_ZIP64 = 8,      // sizes and offset in the ZIP64 extra field
  Z_LIE = 16,       // says it is 1000 bytes
  Z_BZIP2 = 32,     // method 12
};

typedef struct zw {
  FILE* f;
  unsigned char cd[1 << 16];
  size_t cd_length;
  int count;
} zw_t;

static void
put16(unsigned char* p, unsigned v) {
  p[0] = (unsigned char)v;
  p[1] = (unsigned char)(v >> 8);
}

static void
put32(unsigned char* p, unsigned long v) {
  put16(p, (unsigned)(v & 0xffff));
  put16(p + 2, (unsigned)(v >> 16));
}

static void
put64(unsigned char* p, unsigned long long v) {
  put32(p, (unsigned long)(v & 0xffffffffu));
  put32(p + 4, (unsigned long)(v >> 32));
}

static void
zw_add(zw_t* z, const char* name, const void* data, size_t length, int deflated, int opts) {
  static unsigned char packed[4 << 20];
  const unsigned char* body = (const unsigned char*)data;
  size_t body_length = length;
  if(deflated) {
    z_stream s;
    memset(&s, 0, sizeof(s));
    deflateInit2(&s, 6, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
    s.next_in = (unsigned char*)data;
    s.avail_in = (uInt)length;
    s.next_out = packed;
    s.avail_out = sizeof(packed);
    deflate(&s, Z_FINISH);
    body = packed;
    body_length = sizeof(packed) - s.avail_out;
    deflateEnd(&s);
  }
  unsigned long crc = crc32(0, (const unsigned char*)data, (uInt)length);
  if(opts & Z_BAD_CRC) {
    crc ^= 1;
  }
  const unsigned long long declared = (opts & Z_LIE) ? 1000 : length;
  const unsigned method = (opts & Z_BZIP2) ? 12 : deflated ? 8 : 0;
  const unsigned flags = (opts & Z_ENCRYPTED) ? 1 : 0;
  const size_t n = strlen(name);
  const long offset = ftell(z->f);

  unsigned char local[30] = {0};
  put32(local, 0x04034b50);
  put16(local + 4, 20);
  put16(local + 6, flags);
  put16(local + 8, method);
  put16(local + 12, 0x21);
  put32(local + 14, crc);
  put32(local + 18, (unsigned long)body_length);
  put32(local + 22, (unsigned long)declared);
  put16(local + 26, (unsigned)n);
  fwrite(local, 1, sizeof(local), z->f);
  fwrite(name, 1, n, z->f);
  fwrite(body, 1, body_length, z->f);

  unsigned char* c = z->cd + z->cd_length;
  memset(c, 0, 46);
  put32(c, 0x02014b50);
  put16(c + 4, (opts & Z_LINK) ? 0x0314 : 0x0014);
  put16(c + 6, 20);
  put16(c + 8, flags);
  put16(c + 10, method);
  put16(c + 14, 0x21);
  put32(c + 16, crc);
  const size_t extra = (opts & Z_ZIP64) ? 28 : 0;
  put32(c + 20, (opts & Z_ZIP64) ? 0xffffffffu : (unsigned long)body_length);
  put32(c + 24, (opts & Z_ZIP64) ? 0xffffffffu : (unsigned long)declared);
  put16(c + 28, (unsigned)n);
  put16(c + 30, (unsigned)extra);
  put32(c + 38, (opts & Z_LINK) ? 0120777ul << 16 : 0);
  put32(c + 42, (opts & Z_ZIP64) ? 0xffffffffu : (unsigned long)offset);
  memcpy(c + 46, name, n);
  if(opts & Z_ZIP64) {
    unsigned char* x = c + 46 + n;
    put16(x, 0x0001);
    put16(x + 2, 24);
    put64(x + 4, declared);
    put64(x + 12, body_length);
    put64(x + 20, (unsigned long long)offset);
  }
  z->cd_length += 46 + n + extra;
  z->count++;
}

static void
zw_end(zw_t* z, int zip64) {
  const long cd_offset = ftell(z->f);
  fwrite(z->cd, 1, z->cd_length, z->f);
  if(zip64) {
    const long at = ftell(z->f);
    unsigned char record[56] = {0}, locator[20] = {0};
    put32(record, 0x06064b50);
    put64(record + 4, 44);
    put16(record + 12, 45);
    put16(record + 14, 45);
    put64(record + 24, (unsigned long long)z->count);
    put64(record + 32, (unsigned long long)z->count);
    put64(record + 40, z->cd_length);
    put64(record + 48, (unsigned long long)cd_offset);
    fwrite(record, 1, sizeof(record), z->f);
    put32(locator, 0x07064b50);
    put64(locator + 8, (unsigned long long)at);
    put32(locator + 16, 1);
    fwrite(locator, 1, sizeof(locator), z->f);
  }
  unsigned char end[22] = {0};
  put32(end, 0x06054b50);
  put16(end + 8, zip64 ? 0xffff : (unsigned)z->count);
  put16(end + 10, zip64 ? 0xffff : (unsigned)z->count);
  put32(end + 12, zip64 ? 0xffffffffu : (unsigned long)z->cd_length);
  put32(end + 16, zip64 ? 0xffffffffu : (unsigned long)cd_offset);
  fwrite(end, 1, sizeof(end), z->f);
  fclose(z->f);
}

static zw_t*
zw_open(const char* path) {
  static zw_t z;
  memset(&z, 0, sizeof(z));
  z.f = fopen(path, "wb");
  return &z;
}

/* Helpers ------------------------------------------------------------------ */

static char root[64];

static int
rm_one(const char* path, const struct stat* st, int type, struct FTW* ftw) {
  (void)st;
  (void)type;
  (void)ftw;
  return remove(path);
}

static int
exists(const char* dir, const char* name) {
  char path[1024];
  struct stat st;
  snprintf(path, sizeof(path), "%s/%s", dir, name);
  return !lstat(path, &st);
}

static int
same_file(const char* dir, const char* name, const void* data, size_t length) {
  char path[1024];
  snprintf(path, sizeof(path), "%s/%s", dir, name);
  FILE* f = fopen(path, "rb");
  if(!f) {
    return 0;
  }
  unsigned char* got = (unsigned char*)malloc(length + 1);
  const size_t n = fread(got, 1, length + 1, f);
  fclose(f);
  const int same = n == length && !memcmp(got, data, length);
  free(got);
  return same;
}

static void
fresh_dir(char* out, size_t size, const char* name) {
  snprintf(out, size, "%s/%s", root, name);
  mkdir(out, 0777);
}

static int stop_at_first;
static int reports;

static int
progress(void* ctx, long long done, long long total) {
  (void)ctx;
  (void)done;
  (void)total;
  reports++;
  return stop_at_first;
}

int
main(void) {
  char zip[256], dest[256], out[1100];
  unzip_result_t r;
  static unsigned char big[2 << 20];
  const char* hello = "Olá, consola!\n";

  for(size_t i = 0; i < sizeof(big); i++) {  // compressible, not all the same
    big[i] = (unsigned char)("BrowserPS5 "[i % 11] + (i / 4096) % 7);
  }
  snprintf(root, sizeof(root), "/tmp/unzip_testXXXXXX");
  if(!mkdtemp(root)) {
    perror("mkdtemp");
    return 1;
  }

  /* Names. */
  CHECK("a plain name", !unzip_safe_name("a.txt", 5, out, sizeof(out)) && !strcmp(out, "a.txt"));
  CHECK("dots and doubled slashes go", !unzip_safe_name("a/./b//c", 8, out, sizeof(out)) && !strcmp(out, "a/b/c"));
  CHECK("backslashes are folders", !unzip_safe_name("a\\b\\c.txt", 9, out, sizeof(out)) && !strcmp(out, "a/b/c.txt"));
  CHECK("a colon inside is _", !unzip_safe_name("ab:c?.txt", 9, out, sizeof(out)) && !strcmp(out, "ab_c_.txt"));
  CHECK("\"..\" is refused", unzip_safe_name("a/../b", 6, out, sizeof(out)) == -1);
  CHECK("\"..\" at the start is refused", unzip_safe_name("../b", 4, out, sizeof(out)) == -1);
  CHECK("an absolute name is refused", unzip_safe_name("/etc/x", 6, out, sizeof(out)) == -1);
  CHECK("a drive letter is refused", unzip_safe_name("C:/x", 4, out, sizeof(out)) == -1);
  CHECK("\"...\" is a name", !unzip_safe_name("...", 3, out, sizeof(out)) && !strcmp(out, "..."));
  CHECK("UTF-8 kept", !unzip_safe_name("café.txt", strlen("café.txt"), out, sizeof(out)) && !strcmp(out, "café.txt"));
  CHECK("not UTF-8 is _", !unzip_safe_name("caf\xe9.txt", 8, out, sizeof(out)) && !strcmp(out, "caf_.txt"));
  CHECK("a 0 byte inside is _", !unzip_safe_name("a\0b", 3, out, sizeof(out)) && !strcmp(out, "a_b"));
  char longname[300];
  memset(longname, 'x', sizeof(longname));
  CHECK("a part over 255 bytes is refused", unzip_safe_name(longname, 256, out, sizeof(out)) == -1);
  CHECK("255 bytes is fine", !unzip_safe_name(longname, 255, out, sizeof(out)));
  CHECK("empty is refused", unzip_safe_name("", 0, out, sizeof(out)) == -1 &&
                                unzip_safe_name("./", 2, out, sizeof(out)) == -1);
  CHECK("too long for out is refused", unzip_safe_name("abcdef", 6, out, 4) == -1);
  unzip_folder_name("/data/downloads/Fotos 2025.ZIP", out, sizeof(out));
  CHECK("folder: no .zip", !strcmp(out, "Fotos 2025"));
  unzip_folder_name("a.b.zip", out, sizeof(out));
  CHECK("folder: a.b", !strcmp(out, "a.b"));
  unzip_folder_name(".zip", out, sizeof(out));
  CHECK("folder: nothing left is zip", !strcmp(out, "zip"));
  unzip_folder_name("..zip", out, sizeof(out));
  CHECK("folder: dots only is zip", !strcmp(out, "zip"));
  unzip_folder_name("ab:c.zip", out, sizeof(out));
  CHECK("folder: cleaned", !strcmp(out, "ab_c"));

  /* A normal one. */
  snprintf(zip, sizeof(zip), "%s/normal.zip", root);
  zw_t* z = zw_open(zip);
  zw_add(z, "hello.txt", hello, strlen(hello), 0, 0);
  zw_add(z, "pasta/", "", 0, 0, 0);
  zw_add(z, "pasta/sub/big.bin", big, sizeof(big), 1, 0);
  zw_add(z, "empty.txt", "", 0, 0, 0);
  zw_add(z, "empty-deflated.txt", "", 0, 1, 0);
  zw_add(z, "outra/sem-pasta.txt", hello, strlen(hello), 1, 0);
  zw_end(z, 0);
  fresh_dir(dest, sizeof(dest), "normal");
  reports = 0;
  int err = unzip_extract(zip, dest, -1, progress, 0, &r);
  CHECK("a normal ZIP: OK", err == UNZIP_OK);
  CHECK("5 files, 1 folder, nothing skipped", r.files == 5 && r.dirs == 1 && r.skipped == 0);
  CHECK("its bytes", r.bytes == (long long)(sizeof(big) + 2 * strlen(hello)) && r.total == r.bytes);
  CHECK("stored file whole", same_file(dest, "hello.txt", hello, strlen(hello)));
  CHECK("deflated 2 MiB whole", same_file(dest, "pasta/sub/big.bin", big, sizeof(big)));
  CHECK("empty files", same_file(dest, "empty.txt", "", 0) && same_file(dest, "empty-deflated.txt", "", 0));
  CHECK("a file whose folder has no entry", same_file(dest, "outra/sem-pasta.txt", hello, strlen(hello)));
  CHECK("progress told (2 MiB: 2 + the end)", reports >= 3);

  /* Names that try to leave, and entries to skip. */
  snprintf(zip, sizeof(zip), "%s/evil.zip", root);
  z = zw_open(zip);
  zw_add(z, "../evil.txt", hello, strlen(hello), 0, 0);
  zw_add(z, "/abs.txt", hello, strlen(hello), 0, 0);
  zw_add(z, "a/../../b.txt", hello, strlen(hello), 0, 0);
  zw_add(z, "C:\\win.txt", hello, strlen(hello), 0, 0);
  zw_add(z, "link", "/etc/passwd", 11, 0, Z_LINK);
  zw_add(z, "secret.txt", hello, strlen(hello), 0, Z_ENCRYPTED);
  zw_add(z, "bz.txt", hello, strlen(hello), 0, Z_BZIP2);
  zw_add(z, "ok\\win\\style.txt", hello, strlen(hello), 0, 0);
  zw_end(z, 0);
  fresh_dir(dest, sizeof(dest), "evil");
  err = unzip_extract(zip, dest, -1, 0, 0, &r);
  CHECK("names that leave: OK, 7 skipped, 1 file", err == UNZIP_OK && r.skipped == 7 && r.files == 1);
  CHECK("nothing written outside", !exists(root, "evil.txt") && !exists(root, "b.txt") && !exists("/", "abs.txt"));
  CHECK("no link, nothing encrypted, no bzip2", !exists(dest, "link") && !exists(dest, "secret.txt") &&
                                                    !exists(dest, "bz.txt"));
  CHECK("backslash folders made", same_file(dest, "ok/win/style.txt", hello, strlen(hello)));

  /* A broken CRC: an error, and the file is not left. */
  snprintf(zip, sizeof(zip), "%s/crc.zip", root);
  z = zw_open(zip);
  zw_add(z, "good.txt", hello, strlen(hello), 0, 0);
  zw_add(z, "bad.txt", big, 100000, 1, Z_BAD_CRC);
  zw_end(z, 0);
  fresh_dir(dest, sizeof(dest), "crc");
  err = unzip_extract(zip, dest, -1, 0, 0, &r);
  CHECK("bad CRC: a data error", err == UNZIP_ERR_DATA && !strcmp(r.entry, "bad.txt"));
  CHECK("bad CRC: the good one stays, the bad one goes", exists(dest, "good.txt") && !exists(dest, "bad.txt"));

  /* Bigger than it says (a ZIP bomb's way): stops at what it said. */
  snprintf(zip, sizeof(zip), "%s/lie.zip", root);
  z = zw_open(zip);
  zw_add(z, "lie.bin", big, sizeof(big), 1, Z_LIE);
  zw_end(z, 0);
  fresh_dir(dest, sizeof(dest), "lie");
  err = unzip_extract(zip, dest, -1, 0, 0, &r);
  CHECK("bigger than it says: a data error", err == UNZIP_ERR_DATA && !exists(dest, "lie.bin"));
  CHECK("bigger than it says: no more written than said", r.bytes <= 1000);

  /* ZIP64. */
  snprintf(zip, sizeof(zip), "%s/zip64.zip", root);
  z = zw_open(zip);
  zw_add(z, "z64/big.bin", big, sizeof(big), 1, Z_ZIP64);
  zw_add(z, "z64/hello.txt", hello, strlen(hello), 0, Z_ZIP64);
  zw_end(z, 1);
  fresh_dir(dest, sizeof(dest), "zip64");
  err = unzip_extract(zip, dest, -1, 0, 0, &r);
  CHECK("ZIP64: OK", err == UNZIP_OK && r.files == 2);
  CHECK("ZIP64: whole", same_file(dest, "z64/big.bin", big, sizeof(big)) &&
                            same_file(dest, "z64/hello.txt", hello, strlen(hello)));

  /* Not a ZIP; cut short. */
  snprintf(zip, sizeof(zip), "%s/not.zip", root);
  FILE* f = fopen(zip, "wb");
  fwrite(big, 1, 5000, f);
  fclose(f);
  fresh_dir(dest, sizeof(dest), "not");
  CHECK("not a ZIP: a format error", unzip_extract(zip, dest, -1, 0, 0, &r) == UNZIP_ERR_FORMAT);
  snprintf(zip, sizeof(zip), "%s/normal.zip", root);
  char cut[256];
  snprintf(cut, sizeof(cut), "%s/cut.zip", root);
  f = fopen(zip, "rb");
  FILE* g = fopen(cut, "wb");
  static unsigned char copy[4 << 20];
  const size_t n = fread(copy, 1, sizeof(copy), f);
  fwrite(copy, 1, n / 2, g);
  fclose(f);
  fclose(g);
  CHECK("cut short: a format error", unzip_extract(cut, dest, -1, 0, 0, &r) == UNZIP_ERR_FORMAT);
  snprintf(cut, sizeof(cut), "%s/none.zip", root);
  CHECK("no file: an open error", unzip_extract(cut, dest, -1, 0, 0, &r) == UNZIP_ERR_OPEN);

  /* No room: nothing written. */
  fresh_dir(dest, sizeof(dest), "room");
  err = unzip_extract(zip, dest, 1 << 20, 0, 0, &r);
  CHECK("no room: a space error before anything", err == UNZIP_ERR_SPACE && r.files == 0 && !exists(dest, "hello.txt"));
  err = unzip_extract(zip, dest, 1LL << 40, 0, 0, &r);
  CHECK("room: OK", err == UNZIP_OK && r.files == 5);

  /* Stopped half-way. */
  fresh_dir(dest, sizeof(dest), "stop");
  stop_at_first = 1;
  err = unzip_extract(zip, dest, -1, progress, 0, &r);
  stop_at_first = 0;
  CHECK("stopped: cancelled", err == UNZIP_ERR_CANCELLED && !exists(dest, "pasta/sub/big.bin"));

  nftw(root, rm_one, 16, FTW_DEPTH | FTW_PHYS);
  printf(failed ? "unzip_test: FAIL\n" : "unzip_test: all passed\n");
  return failed;
}
