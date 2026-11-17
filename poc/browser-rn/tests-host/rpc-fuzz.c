/* The answers of services, read by the browser (2026-11-14): browser/torrent.c's reading of JSON (the torrents'
   daemon's answers, and TorBox's, which come from the Internet: torbox.c reads them with the same function), and
   what it writes for them from a link a page gave (a magnet). AS IT IS, with AddressSanitizer and
   UndefinedBehaviorSanitizer and once more with glibc's own checks (build-and-run.sh).

     - a value read out of any answer at all ends inside its place, is UTF-8, and reading never leaves the answer;
     - what is written for the daemon from any link at all is JSON (a quote in a magnet does not end its string);
     - base64 has the length it must have, in its place.

       rpc_fuzz [rounds]                                                                                        */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "torrent.h"
#include "json-check.h"

static int checks, failed, round_now;
static unsigned long long rng = 0xD6E8FEB86659FD93ull;

static unsigned
rnd(void) {
  rng ^= rng << 13;
  rng ^= rng >> 7;
  rng ^= rng << 17;
  return (unsigned)(rng >> 11);
}

static void
check(int fine, const char* what, const char* with) {
  checks++;
  if(!fine) {
    failed++;
    if(failed <= 15) {
      printf("FAIL (round %d): %s  [", round_now, what);
      for(const unsigned char* c = (const unsigned char*)(with ? with : ""); *c && c - (const unsigned char*)with < 220; c++) {
        printf(*c >= 0x20 && *c < 0x7f && *c != '\\' ? "%c" : "\\x%02x", *c);
      }
      printf("]\n");
    }
  }
}

/* Text made of the pieces answers are made of, and of anything. Its own block, ended where it ends. */
static char*
some_text(int max_pieces) {
  static const char* const pieces[] = {"{", "}", "[", "]", ":", ",", "\"", "\\", "\\\"", "\\\\", "\\n", "\\u00e9", "\\ud83c\\udfae", "\\ud83c", "\\udfae",
                                       "\\u0000", "\\u12", "\\uZZZZ", "\\x", "name", "\"name\"", "\"name\":", "\"name\": \"", "result", "\"result\":\"success\"",
                                       "torrent-added", "torrent-duplicate", "\"torrent-added\":{\"name\":\"", "arguments", "error", "detail", "\"detail\":\"",
                                       " ", "\n", "\t", "caf\xc3\xa9", "\xe4\xb8\xad", "\xff", "\xc3", "0", "-1", "null", "true", "magnet:?xt=urn:btih:",
                                       "&dn=", "%22", "a b", "<", "'"};
  const int count = rnd() % (max_pieces + 1);
  size_t n = 0, room = 64;
  char* text = (char*)malloc(room);
  for(int i = 0; i < count; i++) {
    char one[2] = {(char)(1 + rnd() % 255), 0};
    const char* p = rnd() % 12 ? pieces[rnd() % (sizeof(pieces) / sizeof(pieces[0]))] : one;
    const int times = rnd() % 80 == 0 ? 300 : 1;
    for(int t = 0; t < times; t++) {
      const size_t len = strlen(p);
      if(n + len + 1 > room) {
        room = (n + len + 1) * 2;
        text = (char*)realloc(text, room);
      }
      memcpy(text + n, p, len);
      n += len;
    }
  }
  char* exact = (char*)malloc(n + 1);  /* (a byte read past its 0 is seen) */
  memcpy(exact, text, n);
  exact[n] = 0;
  free(text);
  return exact;
}

int
main(int argc, char** argv) {
  const int rounds = argc > 1 ? atoi(argv[1]) : 100000;
  char name[300];

  /* fixed cases */
  check(!torrent_json_string("{\"name\": \"caf\\u00e9 \\\"x\\\"\"}", "name", name, sizeof(name)) && !strcmp(name, "caf\xc3\xa9 \"x\""), "a fixed value", name);
  check(!torrent_json_string("{\"name\":\"\\ud83c\\udfae\"}", "name", name, sizeof(name)) && !strcmp(name, "\xf0\x9f\x8e\xae"), "a pair of surrogates is one letter", name);
  check(torrent_json_string("{\"other\":\"x\"}", "name", name, sizeof(name)) == -1, "a key that is not there", "");
  {
    char* json = torrent_add_json("magnet:?xt=urn:btih:abc&dn=a\"b\\c\nd", 0);
    check(json_valid(json), "what is written for a magnet with a quote in it is JSON", json);
    free(json);
  }
  for(round_now = 1; round_now <= rounds; round_now++) {
    /* a value read out of any answer */
    char* body = some_text(14);
    static const char* const keys[] = {"name", "result", "detail", "error", "", "\"", "torrent-added", "a"};
    const char* key = keys[rnd() % 8];
    const size_t size = rnd() % 3 == 0 ? 1 + rnd() % 24 : 300;
    char* out = (char*)malloc(size);
    memset(out, 'Z', size);
    const int found = torrent_json_string(body, key, out, size);
    check(found == 0 || found == -1, "reading a value answers yes or no", body);
    if(found == 0) {
      const int ended = memchr(out, 0, size) != 0;
      check(ended, "a value read ends inside its place", body);
      /* (bytes that are not UTF-8 in the answer itself pass as they are, and run_script() mends them; a value cut
         for its place may end inside a letter) */
      if(ended && utf8_valid(body) && size == 300 && strlen(out) + 8 < size) {
        check(utf8_valid(out), "a value read out of an answer that is UTF-8 is UTF-8, whatever its escapes say", body);
        check(!strstr(out, "\xed\xa0") && !strstr(out, "\xed\xb0"), "half a letter in an escape is not written as bytes that are no letter", body);
      }
    }
    free(out);
    /* what a torrent-add answered */
    const size_t nsize = rnd() % 3 == 0 ? 1 + rnd() % 24 : sizeof(name);
    char* got = (char*)malloc(nsize);
    memset(got, 'Z', nsize);
    const int result = torrent_add_result(body, got, nsize);
    check(result >= 0 && result <= 3, "an answer is one of those there are", body);
    check(memchr(got, 0, nsize) != 0, "the name or the reason given ends inside its place", body);
    free(got);
    free(body);
    /* what is written for the daemon from any link */
    char* link = some_text(8);
    char* json = rnd() % 2 ? torrent_add_json(link, 0) : torrent_add_json(0, link);
    check(!json || json_shape(json), "what is written for the daemon is JSON, whatever the link", link);
    free(json);
    /* base64 */
    const size_t n = rnd() % 200;
    const size_t room = rnd() % 4 == 0 ? rnd() % 300 : 400;
    char* b64 = (char*)malloc(room ? room : 1);
    memset(b64, 'Z', room ? room : 1);
    const size_t made = torrent_base64((const unsigned char*)link, strlen(link) < n ? strlen(link) : n, b64, room);
    const size_t in_n = strlen(link) < n ? strlen(link) : n;
    const size_t want = (in_n + 2) / 3 * 4;
    check(made == 0 || (made == want && made < room && b64[made] == 0), "base64 has the length it must have, in its place", link);
    check(made != 0 || want + 1 > room || in_n == 0, "base64 that fits is made", link);
    free(b64);
    free(link);
  }
  printf("%s: %d checks, %d failed (%d rounds: services' answers read, requests written)\n", failed ? "FAIL" : "PASS", checks, failed, rounds);
  return failed ? 1 : 0;
}
