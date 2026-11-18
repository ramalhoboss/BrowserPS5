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

// update_test [a saved answer of GitHub's releases list]: update.c on the PC ("make test" runs it with the
// sanitizers; with a file, also what that real answer gives).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "update.h"
#include "../common/sha256.h"

static int fails;

#define CHECK(what, ok) do { \
    if(!(ok)) { fails++; } \
    printf("%s %s\n", (ok) ? "PASS" : "FAIL", what); \
  } while(0)

#define SHA_A "b90f8a409e97ad1e627d0c6e7764626012527a1343642e644f8cfe63af7b3449"
#define SHA_B "da523488b0f6b8851802a5a631e608d71796fa409389528a026feba60410b652"

// One release as GitHub lists it (only what is read), with what a test wants changed.
static int
release(char* out, size_t room, const char* tag, const char* draft, const char* name, const char* state,
        const char* url_tag, const char* size, const char* digest) {
  return snprintf(out, room,
                  "{\"tag_name\":\"%s\",\"draft\":%s,\"prerelease\":true,\"body\":\"notes \\\"quoted\\\" \\u00e9\","
                  "\"assets\":[{\"name\":\"BrowserPS5-uninstall.elf\",\"state\":\"uploaded\",\"size\":112112,"
                  "\"digest\":\"sha256:" SHA_B "\",\"browser_download_url\":\"https://github.com/o/r/releases/download/%s/"
                  "BrowserPS5-uninstall.elf\"},{\"name\":\"%s\",\"state\":\"%s\",\"size\":%s,\"digest\":%s,"
                  "\"browser_download_url\":\"https://github.com/o/r/releases/download/%s/BrowserPS5.elf\"}]}",
                  tag, draft, tag, name, state, size, digest, url_tag);
}

static int
list(char* out, size_t room, const char* a, const char* b) {
  return snprintf(out, room, "[%s%s%s]", a, b ? "," : "", b ? b : "");
}

static int
found(const char* text, update_t* u) {
  static char copy[8192];
  snprintf(copy, sizeof(copy), "%s", text);
  return update_from_releases(copy, strlen(copy), "o/r", u);
}

// SHA-256 of n bytes, given step bytes at a time, as hex.
static const char*
sha_hex(const void* data, size_t n, size_t step) {
  static char hex[65];
  unsigned char out[32];
  sha256_t s;

  sha256_begin(&s);
  for(size_t at = 0; at < n; at += step) {
    sha256_add(&s, (const char*)data + at, n - at < step ? n - at : step);
  }
  sha256_end(&s, out);
  for(int i = 0; i < 32; i++) {
    snprintf(hex + 2 * i, 3, "%02x", out[i]);
  }
  return hex;
}

int
main(int argc, char** argv) {
  static char a[4096], b[4096], l[8192];
  update_t u;

  // SHA-256: the standard's examples, whole and in pieces
  {
    static char million[1000000];
    const char* two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    memset(million, 'a', sizeof(million));
    CHECK("sha256 of nothing", !strcmp(sha_hex("", 0, 1), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    CHECK("sha256 of abc", !strcmp(sha_hex("abc", 3, 64), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK("sha256 of the 56-byte example", !strcmp(sha_hex(two, 56, 64), "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
    CHECK("the same, 7 bytes at a time", !strcmp(sha_hex(two, 56, 7), "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
    CHECK("sha256 of a million a's", !strcmp(sha_hex(million, sizeof(million), 4096), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
    CHECK("the same, 61 bytes at a time", !strcmp(sha_hex(million, sizeof(million), 61), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
  }

  // which is newer
  CHECK("rc13 < rc14", update_newer("1.0.0-rc13", "1.0.0-rc14"));
  CHECK("rc14 is not newer than itself", !update_newer("1.0.0-rc14", "1.0.0-rc14"));
  CHECK("rc14 is not older than rc13", !update_newer("1.0.0-rc14", "1.0.0-rc13"));
  CHECK("rc9 < rc10 (numbers, not letters)", update_newer("1.0.0-rc9", "1.0.0-rc10"));
  CHECK("a candidate < its release", update_newer("1.0.0-rc14", "1.0.0"));
  CHECK("a release is not older than its candidate", !update_newer("1.0.0", "1.0.0-rc99"));
  CHECK("1.0.0 < 1.0.1-rc1", update_newer("1.0.0", "1.0.1-rc1"));
  CHECK("1.9.2 < 1.10.0", update_newer("1.9.2", "1.10.0"));
  CHECK("1.0 = 1.0.0", !update_newer("1.0", "1.0.0") && !update_newer("1.0.0", "1.0"));
  CHECK("not versions: nothing is newer", !update_newer("", "1.0.0") && !update_newer("1.0.0", "next") &&
        !update_newer("1.0.0", "1.0.0-beta1") && !update_newer("1.0.0", "1..0") && !update_newer("1.0.0", "1.0.0-rc") &&
        !update_newer(0, "1.0.0") && !update_newer("1.0.0", "1.0.0.0.1") && !update_newer("1.0.0", "v1.0.1"));

  // what a list of releases gives
  release(a, sizeof(a), "v1.0.0-rc14", "false", "BrowserPS5.elf", "uploaded", "v1.0.0-rc14", "115989472", "\"sha256:" SHA_A "\"");
  release(b, sizeof(b), "v1.0.0-rc13", "false", "BrowserPS5.elf", "uploaded", "v1.0.0-rc13", "115989392", "\"sha256:" SHA_B "\"");
  list(l, sizeof(l), a, b);
  CHECK("two releases: the newer one", !found(l, &u) && !strcmp(u.version, "1.0.0-rc14") && u.size == 115989472ull &&
        !strcmp(u.url, "https://github.com/o/r/releases/download/v1.0.0-rc14/BrowserPS5.elf") && u.sha256[0] == 0xb9 &&
        u.sha256[31] == 0x49);
  list(l, sizeof(l), b, a);
  CHECK("the same two the other way round: still the newer one", !found(l, &u) && !strcmp(u.version, "1.0.0-rc14"));

  release(a, sizeof(a), "v1.0.0-rc15", "true", "BrowserPS5.elf", "uploaded", "v1.0.0-rc15", "115989472", "\"sha256:" SHA_A "\"");
  list(l, sizeof(l), a, b);
  CHECK("a draft is not an update", !found(l, &u) && !strcmp(u.version, "1.0.0-rc13"));

  release(a, sizeof(a), "v1.0.0-rc15", "false", "BrowserPS5.zip", "uploaded", "v1.0.0-rc15", "115989472", "\"sha256:" SHA_A "\"");
  list(l, sizeof(l), a, b);
  CHECK("a release without the installer is passed over", !found(l, &u) && !strcmp(u.version, "1.0.0-rc13"));

  release(a, sizeof(a), "v1.0.0-rc15", "false", "BrowserPS5.elf", "new", "v1.0.0-rc15", "115989472", "\"sha256:" SHA_A "\"");
  list(l, sizeof(l), a, b);
  CHECK("an installer still being uploaded is passed over", !found(l, &u) && !strcmp(u.version, "1.0.0-rc13"));

  release(a, sizeof(a), "v1.0.0-rc15", "false", "BrowserPS5.elf", "uploaded", "v1.0.0-rc15", "115989472", "null");
  list(l, sizeof(l), a, 0);
  CHECK("no SHA-256: no update", found(l, &u) == -1 && !u.version[0]);

  release(a, sizeof(a), "v1.0.0-rc15", "false", "BrowserPS5.elf", "uploaded", "v1.0.0-rc15", "115989472", "\"sha256:zz" SHA_A "\"");
  list(l, sizeof(l), a, 0);
  CHECK("a SHA-256 of the wrong length: no update", found(l, &u) == -1);

  release(a, sizeof(a), "v1.0.0-rc15", "false", "BrowserPS5.elf", "uploaded", "v1.0.0-rc14", "115989472", "\"sha256:" SHA_A "\"");
  list(l, sizeof(l), a, 0);
  CHECK("the file's address is another tag's: no update", found(l, &u) == -1);

  release(a, sizeof(a), "v1.0.0-rc15", "false", "BrowserPS5.elf", "uploaded", "v1.0.0-rc15", "0", "\"sha256:" SHA_A "\"");
  list(l, sizeof(l), a, 0);
  CHECK("an empty file: no update", found(l, &u) == -1);

  release(a, sizeof(a), "v1.0.0-rc15", "false", "BrowserPS5.elf", "uploaded", "v1.0.0-rc15", "9999999999999", "\"sha256:" SHA_A "\"");
  list(l, sizeof(l), a, 0);
  CHECK("a file larger than any installer: no update", found(l, &u) == -1);

  release(a, sizeof(a), "nightly", "false", "BrowserPS5.elf", "uploaded", "nightly", "115989472", "\"sha256:" SHA_A "\"");
  list(l, sizeof(l), a, b);
  CHECK("a tag that is not a version is passed over", !found(l, &u) && !strcmp(u.version, "1.0.0-rc13"));

  CHECK("not a list", found("{\"message\":\"API rate limit exceeded\"}", &u) == -1);
  CHECK("an empty list", found("[]", &u) == -1);
  CHECK("broken text", found("[{\"tag_name\":\"v1.0.0\",", &u) == -1);
  {
    char one[64] = "[]";
    CHECK("a repository's name that is not one", update_from_releases(one, 2, "o/r/x", &u) == -1 &&
          update_from_releases(one, 2, "../r", &u) == -1 && update_from_releases(one, 2, "o r", &u) == -1);
  }

  if(argc > 1) {  // GitHub's own answer, saved
    FILE* f = fopen(argv[1], "rb");
    static char real[1 << 20];
    const size_t n = f ? fread(real, 1, sizeof(real) - 1, f) : 0;
    if(f) {
      fclose(f);
    }
    real[n] = 0;
    const int r = update_from_releases(real, n, UPDATE_REPO, &u);
    printf("the saved answer (%zu bytes): %s; version %s, %llu bytes, %s\n", n, r ? "nothing" : "an update", u.version, u.size,
           u.url);
    CHECK("the saved answer has a release with its installer", !r && u.size > 1000000 && update_newer("1.0.0-rc12", u.version));
  }
  if(argc > 3) {  // a real installer and the digest GitHub gives for it
    FILE* f = fopen(argv[2], "rb");
    static unsigned char block[1 << 16];
    unsigned char out[32];
    char hex[65];
    sha256_t sh;
    size_t n, all = 0;
    sha256_begin(&sh);
    while(f && (n = fread(block, 1, sizeof(block), f)) > 0) {
      sha256_add(&sh, block, n);
      all += n;
    }
    if(f) {
      fclose(f);
    }
    sha256_end(&sh, out);
    for(int i = 0; i < 32; i++) {
      snprintf(hex + 2 * i, 3, "%02x", out[i]);
    }
    printf("%s: %zu bytes, sha256 %s\n", argv[2], all, hex);
    CHECK("the installer on the disk has the digest GitHub gives", all > 0 && !strcmp(hex, argv[3]));
  }
  printf("%d FAIL\n", fails);
  return fails != 0;
}
