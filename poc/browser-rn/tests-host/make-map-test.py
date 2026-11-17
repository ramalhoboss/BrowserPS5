"""The map of an unfinished download (main.cpp multi_map_text() / multi_map_load()), tested on the PC (2026-11-14).

The browser reads these files at its start (multi_kept_scan()): a broken one must never take it down. This cuts
the two functions and what they need out of ../../browser/main.cpp as they are, puts them in a small program with
stand-ins for the rest, and build-and-run.sh compiles it with AddressSanitizer and UndefinedBehaviorSanitizer:
a round trip (a set's map written and read back gives the same missing ranges), then many broken maps.

    python3 make-map-test.py <out.cpp>
"""
import re, sys, os

HERE = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(HERE, '..', '..', 'browser', 'main.cpp'), encoding='utf-8').read()


def cut(start, end):
    a = src.index(start)
    b = src.index(end, a) + len(end)
    return src[a:b]


def function(head):
    a = src.index(head)
    b = src.index('\n}\n', a) + 3
    return src[a:b]


parts = [
    cut('#define MULTI_SEGS 64', '\n'),
    cut('enum { MS_FREE,', '\n'),
    cut('typedef struct multi_seg {', '} multi_seg_t;\n'),
    cut('typedef struct multi {', '} multi_t;\n'),
    function('static bool\nurl_is_web('),
    function('static bool\nname_ends_with('),
    function('static char*\nmulti_map_text('),
    function('static bool\nmulti_map_load('),
    function('static void\nerror_html_text('),
]
program = r'''// MADE by make-map-test.py from browser/main.cpp: the map's two functions as they are, with stand-ins around them.
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/stat.h>

typedef void* WKPageRef;
typedef void* WKDownloadRef;
static char partial_dir[600];
#define PARTIAL_DIR partial_dir_literal
#define PARTIAL_NEAR ".parcial"
// (the browser's PARTIAL_DIR is a string literal; here the folder is made at run time: see dir_is() below)

typedef struct download {
  int id, state;
  bool chosen;
  char kind[8], name[256], host[256], dir[512], partial[600];
  long long received, total;
  char url[4096], final_url[4096];
} download_t;

static char*
read_whole(const char* path, size_t max, size_t* length) {
  FILE* f = fopen(path, "rb");
  if(!f) {
    return 0;
  }
  char* text = (char*)malloc(max + 1);
  const size_t n = text ? fread(text, 1, max, f) : 0;
  fclose(f);
  if(!text) {
    return 0;
  }
  text[n] = 0;
  *length = n;
  return text;
}

@PARTS@

// ---- the test
static unsigned long long seed = 88172645463325252ull;
static unsigned
rnd(void) {
  seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17;
  return (unsigned)(seed >> 11);
}

static int checks, failed;
static void
check(bool ok, const char* what, long long a, long long b) {
  checks++;
  if(!ok) {
    failed++;
    if(failed < 30) {
      printf("FAIL: %s (%lld, %lld)\n", what, a, b);
    }
  }
}

static void
put(const char* path, const char* text, size_t n) {
  FILE* f = fopen(path, "wb");
  fwrite(text, 1, n, f);
  fclose(f);
}

int
main(int argc, char** argv) {
  static multi_t m, back;
  static download_t d, dback;
  char map[700], data[700];
  const int rounds = argc > 2 ? atoi(argv[2]) : 20000;

  snprintf(partial_dir, sizeof(partial_dir), "%s", argv[1]);
  snprintf(map, sizeof(map), "%s/7-1731550000.part.map", partial_dir);
  snprintf(data, sizeof(data), "%s/7-1731550000.part.multi", partial_dir);
  put(data, "x", 1);

  // 1. a set's map written and read back: the same missing ranges, the same count of what is in
  for(int round = 0; round < 300; round++) {
    memset(&m, 0, sizeof(m));
    memset(&d, 0, sizeof(d));
    m.total = (1 + rnd() % 60000) * 1048576LL + rnd() % 1048576;
    m.segs = 1 + rnd() % MULTI_SEGS;
    long long at = 0, missing = 0;
    const long long each = m.total / m.segs;
    for(int i = 0; i < m.segs; i++) {
      multi_seg_t* s = &m.seg[i];
      s->from = at;
      s->to = i == m.segs - 1 ? m.total : at + each;
      at = s->to;
      const long long all = s->to - s->from;
      s->state = rnd() % 6;
      s->got = rnd() % 5 == 0 ? all + rnd() % 100 : all ? rnd() % (all + 1) : 0;
      const long long got = s->state == MS_DONE || s->got > all ? all : s->got;
      missing += all - got;
    }
    snprintf(m.file, sizeof(m.file), "%s", data);
    snprintf(m.url, sizeof(m.url), "https://example.com/a file%u.bin?x=%u", rnd(), rnd());
    snprintf(d.url, sizeof(d.url), "https://example.com/start?%u", rnd());
    snprintf(d.name, sizeof(d.name), "a name with spaces %u.bin", rnd());
    snprintf(d.kind, sizeof(d.kind), "file");
    snprintf(d.dir, sizeof(d.dir), "/data/downloads");
    snprintf(d.host, sizeof(d.host), "example.com");
    d.chosen = rnd() % 4 != 0;
    char* text = multi_map_text(&m, &d);
    check(text != 0, "the map's text", round, 0);
    put(map, text, strlen(text));
    free(text);
    memset(&dback, 0, sizeof(dback));
    const bool fine = multi_map_load(map, &dback, &back);
    check(fine, "a map just written is read", round, 0);
    if(fine) {
      long long miss = 0;
      for(int i = 0; i < back.segs; i++) {
        check(back.seg[i].from >= 0 && back.seg[i].from < back.seg[i].to && back.seg[i].to <= back.total, "a range inside the file", back.seg[i].from, back.seg[i].to);
        miss += back.seg[i].to - back.seg[i].from;
      }
      check(back.total == m.total, "the total", back.total, m.total);
      check(miss == missing, "what is missing", miss, missing);
      check(back.banked == m.total - missing && dback.received == back.banked, "what is in", back.banked, m.total - missing);
      check(!strcmp(dback.name, d.name) && !strcmp(dback.url, d.url) && !strcmp(back.url, m.url), "the name and the addresses", 0, 0);
      check(!strcmp(dback.dir, d.chosen ? d.dir : ""), "the folder", 0, 0);
      check(!strcmp(back.file, data) && !strcmp(dback.partial + strlen(dback.partial) - 5, ".part"), "the file and the partial's name", 0, 0);
    }
  }

  // 2. broken maps: none may read or write outside its memory, and one that is taken keeps the rules
  memset(&m, 0, sizeof(m));
  memset(&d, 0, sizeof(d));
  m.total = 671088640;
  m.segs = 8;
  for(int i = 0; i < 8; i++) {
    m.seg[i].from = i * 83886080LL;
    m.seg[i].to = (i + 1) * 83886080LL;
    m.seg[i].got = 1048576LL * (i * 9);
  }
  snprintf(m.file, sizeof(m.file), "%s", data);
  snprintf(m.url, sizeof(m.url), "http://127.0.0.1:8111/bps5-test.bin");
  snprintf(d.url, sizeof(d.url), "http://127.0.0.1:8111/bps5-test.bin");
  snprintf(d.name, sizeof(d.name), "bps5-test.bin");
  snprintf(d.dir, sizeof(d.dir), "/data/downloads");
  d.chosen = true;
  char* good = multi_map_text(&m, &d);
  const size_t good_n = strlen(good);
  static char bad[70000];
  static const char* const pieces[] = {
    "miss -5 10\n", "miss 10 5\n", "miss 0 99999999999999999999999999\n", "miss 9223372036854775807 9223372036854775807\n",
    "total -1\n", "total 9223372036854775807\n", "total 99999999999999999999999\n", "total\n", "file /etc/passwd\n",
    "file /data/../../x.part.multi\n", "file \n", "url javascript:alert(1)\n", "url \n", "end\n", "miss\n", "miss 1\n",
    "miss a b\n", "name \n", "\n\n\n", " \n", "bps5map 2\n", "have 5\n", "kind aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n",
  };
  int taken = 0;
  for(int round = 0; round < rounds; round++) {
    size_t n = good_n;
    memcpy(bad, good, good_n + 1);
    const int how = rnd() % 9;
    if(how == 0) {  // cut short
      n = rnd() % (good_n + 1);
    } else if(how == 1) {  // some bytes changed
      for(int k = 1 + rnd() % 8; k > 0; k--) {
        bad[rnd() % good_n] = (char)rnd();
      }
    } else if(how == 2) {  // a piece put in somewhere, at a line's start
      const char* piece = pieces[rnd() % (sizeof(pieces) / sizeof(pieces[0]))];
      size_t at = rnd() % good_n;
      while(at && bad[at - 1] != '\n') {
        at--;
      }
      const size_t len = strlen(piece);
      memmove(bad + at + len, bad + at, good_n - at + 1);
      memcpy(bad + at, piece, len);
      n = good_n + len;
    } else if(how == 3) {  // far too many ranges
      n = 0;
      n += (size_t)snprintf(bad + n, sizeof(bad) - n, "bps5map 1\ntotal 671088640\nfile %s\nurl http://a/b\n", data);
      for(int k = 0; k < 400 && n + 64 < sizeof(bad); k++) {
        n += (size_t)snprintf(bad + n, sizeof(bad) - n, "miss %d %d\n", k * 1000, k * 1000 + 500);
      }
      n += (size_t)snprintf(bad + n, sizeof(bad) - n, "end\n");
    } else if(how == 4) {  // any bytes at all
      n = rnd() % 3000;
      for(size_t k = 0; k < n; k++) {
        bad[k] = (char)rnd();
      }
    } else if(how == 5) {  // one very long line
      n = 0;
      n += (size_t)snprintf(bad + n, sizeof(bad) - n, "bps5map 1\ntotal 671088640\nname ");
      for(int k = 0; k < 60000 && n + 2 < sizeof(bad); k++) {
        bad[n++] = 'a' + rnd() % 26;
      }
      n += (size_t)snprintf(bad + n, sizeof(bad) - n, "\nend\n");
    } else if(how == 6) {  // a line taken out
      size_t at = rnd() % good_n;
      while(at && bad[at - 1] != '\n') {
        at--;
      }
      const char* stop = strchr(bad + at, '\n');
      if(stop) {
        memmove(bad + at, stop + 1, strlen(stop + 1) + 1);
        n = strlen(bad);
      }
    } else if(how == 7) {  // zeros inside
      bad[rnd() % good_n] = 0;
    }  // 8: as it is
    put(map, bad, n);
    memset(&dback, 0, sizeof(dback));
    if(multi_map_load(map, rnd() % 2 ? &dback : 0, &back)) {
      taken++;
      long long miss = 0;
      check(back.segs >= 0 && back.segs <= MULTI_SEGS, "no more ranges than fit", back.segs, 0);
      for(int i = 0; i < back.segs && i < MULTI_SEGS; i++) {
        check(back.seg[i].from >= 0 && back.seg[i].from < back.seg[i].to && back.seg[i].to <= back.total, "a range inside the file", back.seg[i].from, back.seg[i].to);
        miss += back.seg[i].to - back.seg[i].from;
      }
      check(back.total > 0 && back.banked >= 0 && back.banked <= back.total, "what is in, within the total", back.banked, back.total);
      check(url_is_web(back.url) && name_ends_with(back.file, ".part.multi"), "a web address and a partial's file", 0, 0);
    }
  }
  free(good);

  // 3. the text put into the page of a site that did not load (error_html_text(): the address, the site's name,
  //    the engine's words): nothing of it may be markup, whatever bytes it has and however small its room
  for(int round = 0; round < rounds; round++) {
    char in[300];
    const size_t n = rnd() % (sizeof(in) - 1);
    static const char marks[] = "<>&\"'";
    for(size_t k = 0; k < n; k++) {
      in[k] = rnd() % 3 == 0 ? marks[rnd() % 5] : (char)(1 + rnd() % 255);
    }
    in[n] = 0;
    const size_t room = 1 + rnd() % 400;
    char* out = (char*)malloc(room);  // (its own block: a byte past it is seen)
    error_html_text(in, out, room);
    const size_t len = strnlen(out, room);
    check(len < room, "the escaped text ends inside its room", (long long)len, (long long)room);
    bool clean = true;
    for(size_t k = 0; k < len; k++) {
      clean = clean && out[k] != '<' && out[k] != '>' && out[k] != '"' && out[k] != '\''
              && (out[k] != '&' || !strncmp(out + k, "&amp;", 5) || !strncmp(out + k, "&lt;", 4) || !strncmp(out + k, "&gt;", 4)
                  || !strncmp(out + k, "&quot;", 6) || !strncmp(out + k, "&#39;", 5));
    }
    check(clean, "no markup left in the escaped text", round, (long long)len);
    free(out);
  }
  printf("%s: %d checks, %d failed; %d of %d broken maps were still taken (and kept the rules)\n", failed ? "FAIL" : "PASS", checks, failed, taken, rounds);
  return failed ? 1 : 0;
}
'''
body = '\n'.join(parts)
# PARTIAL_DIR is a string literal in the browser ("..." "/"): here a folder made at run time
body = body.replace('!strncmp(m->file, PARTIAL_DIR "/", sizeof(PARTIAL_DIR))',
                    '(!strncmp(m->file, partial_dir, strlen(partial_dir)) && m->file[strlen(partial_dir)] == \'/\')')
code_only = '\n'.join(line.split('//')[0] for line in body.split('\n'))
assert 'PARTIAL_DIR' not in code_only, 'another use of PARTIAL_DIR in what was cut'
open(sys.argv[1], 'w', encoding='utf-8', newline='\n').write(program.replace('@PARTS@', body))
print('map test written:', sys.argv[1], '(%d bytes of the browser\'s own code)' % len(body))
