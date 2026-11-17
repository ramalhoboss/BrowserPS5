/* The files the browser reads at its start, BROKEN (2026-11-14): the settings, the history, the tabs of the last
   time, the saved logins, the extensions' list and their scripts. A console is switched off at the wall, a disk
   fills up, a write stops half way: the next start must read whatever is there and go on, never fall.

   This is browser/settings.c, session.c and logins.c AS THEY ARE, compiled with AddressSanitizer and
   UndefinedBehaviorSanitizer (build-and-run.sh), and given thousands of files made from good ones (written by the
   modules' own writers) and then cut short, with bytes changed, with any bytes put in, with a piece repeated many
   times, or made of nothing but chance. Checked besides "no fall, nothing read or written outside its memory":

     - settings read from a broken file, saved and read again, save to the very same text (they are whole);
     - the tabs read are web addresses, each ending inside its place, the one in front among them;
     - the history, the sites opened last, the extensions and the users of a site come out as JSON a page can take
     (the lists go to the pages inside a script: one byte that is not UTF-8 and the page gets nothing).

       files_fuzz <an empty folder> [rounds]                                                                    */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "settings.h"
#include "session.h"
#include "logins.h"
#include "json-check.h"

static int checks, failed;
static unsigned long long rng = 0x9E3779B97F4A7C15ull;
static int round_now;

static unsigned
rnd(void) {
  rng ^= rng << 13;
  rng ^= rng >> 7;
  rng ^= rng << 17;
  return (unsigned)(rng >> 11);
}

static void
check(int fine, const char* what) {
  checks++;
  if(!fine) {
    failed++;
    if(failed <= 12) {
      printf("FAIL (round %d): %s\n", round_now, what);
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

/* A good file, broken one way or several. */
static void
broken(const char* good, const char* to) {
  static const char odd[] = {0, '\n', '\r', '\t', '=', '"', '\\', '{', '}', '[', ']', ',', ':', ' ', '%', '-', '9', (char)0xff, (char)0xc3, (char)0x80, 0x7f, 1};
  size_t n = 0;
  char* data = file_bytes(good, &n);
  size_t room = n * 40 + 70000;
  char* out = (char*)malloc(room);
  if(!data || !out) {
    free(data);
    free(out);
    return;
  }
  memcpy(out, data, n);
  const int ways = 1 + rnd() % 3;
  for(int w = 0; w < ways; w++) {
    const int how = rnd() % 8;
    if(how == 0 && n) {  /* cut short: a write that stopped */
      n = rnd() % n;
    } else if(how == 1 && n) {  /* bytes changed */
      const int k = 1 + rnd() % 8;
      for(int i = 0; i < k; i++) {
        out[rnd() % n] = rnd() % 3 ? odd[rnd() % sizeof(odd)] : (char)rnd();
      }
    } else if(how == 2 && n + 64 < room) {  /* any bytes put in */
      const size_t at = rnd() % (n + 1), k = 1 + rnd() % 60;
      memmove(out + at + k, out + at, n - at);
      for(size_t i = 0; i < k; i++) {
        out[at + i] = rnd() % 2 ? odd[rnd() % sizeof(odd)] : (char)rnd();
      }
      n += k;
    } else if(how == 3 && n > 4) {  /* a piece repeated: lines far longer than any place for them */
      const size_t from = rnd() % (n - 1), len = 1 + rnd() % (n - from > 300 ? 300 : n - from);
      const int times = 1 + rnd() % 30;
      char piece[300];
      memcpy(piece, out + from, len);
      for(int t = 0; t < times && n + len < room; t++) {
        const size_t at = from;
        memmove(out + at + len, out + at, n - at);
        memcpy(out + at, piece, len);
        n += len;
      }
    } else if(how == 4) {  /* nothing but chance */
      n = rnd() % 3000;
      for(size_t i = 0; i < n; i++) {
        out[i] = (char)rnd();
      }
    } else if(how == 5) {  /* empty */
      n = 0;
    } else if(how == 6 && n > 2) {  /* the line ends go */
      for(size_t i = 0; i < n; i++) {
        if(out[i] == '\n' && rnd() % 3 == 0) {
          out[i] = rnd() % 2 ? ' ' : '\t';
        }
      }
    } else if(n > 8) {  /* numbers become huge, or less than nothing */
      const size_t at = rnd() % (n - 1);
      static const char* const big[] = {"99999999999999999999", "-1", "-2147483649", "4294967296", "1e999", "0x7fffffff", "+"};
      const char* b = big[rnd() % 7];
      const size_t k = strlen(b);
      if(n + k < room) {
        memmove(out + at + k, out + at, n - at);
        memcpy(out + at, b, k);
        n += k;
      }
    }
  }
  file_put(to, out, n);
  free(data);
  free(out);
}

static int
same_files(const char* a, const char* b) {
  size_t na = 0, nb = 0;
  char* da = file_bytes(a, &na);
  char* db = file_bytes(b, &nb);
  const int same = da && db && na == nb && !memcmp(da, db, na);
  free(da);
  free(db);
  return same;
}

static const char script[] =
  "// ==UserScript==\n"
  "// @name         Dark Thing\n"
  "// @name:pt      Coisa escura\n"
  "// @version      1.2\n"
  "// @description  Makes it dark\n"
  "// @match        https://*.example.com/*\n"
  "// @include      http://other.example/*\n"
  "// @exclude      https://no.example.com/*\n"
  "// @run-at       document-start\n"
  "// @noframes\n"
  "// ==/UserScript==\n"
  "document.body.style.background = 'black';\n";

int
main(int argc, char** argv) {
  const int rounds = argc > 2 ? atoi(argv[2]) : 20000;
  static settings_t s, s2, s3;
  static ext_list_t list;
  static char urls[SESSION_TABS_MAX][SESSION_URL_MAX];
  static char text[70000];
  ext_meta_t meta;

  if(argc < 2 || chdir(argv[1])) {
    perror("folder");
    return 2;
  }
  /* ---- good files, by the modules' own writers */
  settings_defaults(&s);
  settings_set(&s, "theme", "neon");
  settings_set(&s, "lang", "en");
  settings_set(&s, "zoom", "125");
  settings_set(&s, "home", "https://example.com/start?a=b&c=d");
  settings_set(&s, "downloads_dir", "/data/downloads");
  settings_ad_allow(&s, "news.example.com", 1);
  settings_ad_allow(&s, "shop.example.org", 1);
  check(!settings_save(&s, "settings.good"), "the good settings are written");
  for(int i = 0; i < 60; i++) {
    char url[200], title[200];
    snprintf(url, sizeof(url), "https://site%d.example.com/page/%d?q=a+b#x", i % 9, i);
    snprintf(title, sizeof(title), i % 7 ? "A page \"%d\" \xc3\xa9 \\ tab\there" : "", i);
    history_add("history.good", 1700000000 + i * 60, url, title);
  }
  {
    const char* tabs[] = {"https://example.com/", "http://127.0.0.1:8080/a?b=c", "https://www.youtube.com/watch?v=abc", "https://x.com/home"};
    check(session_text(tabs, 4, 2, text, sizeof(text)) > 0 && !session_write("session.good", text), "the good tabs are written");
  }
  logins_clear();
  logins_put("https://example.com", "ana", "p\xc3\xa1ss word=1", 1700000000);
  logins_put("https://example.com", "rui", "x\"y\\z", 1700000100);
  logins_put("https://other.example", "a@b.c", "123456", 1700000200);
  logins_set_never("https://never.example", 1700000300);
  check(!logins_save("logins.good"), "the good logins are written");
  mkdir("ext.good", 0755);
  ext_load(&list, "ext.good");
  check(!ext_parse(script, &meta) && ext_install(&list, script, &meta) > 0 && ext_install(&list, script, &meta) > 0,
        "the good extensions are written");
  mkdir("ext", 0755);
  {  /* what the good files give is JSON and UTF-8, long titles too (cut between letters, not inside one) */
    char* json = history_json("history.good", 500);
    check(json_valid(json), "the good history is JSON and UTF-8");
    free(json);
    json = history_recent_json("history.good", 40);
    check(json_valid(json), "the good sites opened last are JSON and UTF-8");
    free(json);
    json = ext_list_json(&list);
    check(json_valid(json), "the good extensions' list is JSON and UTF-8");
    free(json);
    for(int shift = 0; shift < 4; shift++) {
      static char title[1024], url[5000];
      size_t n = 0, u = 0;
      memset(title, 0, sizeof(title));
      for(int i = 0; i < shift; i++) {
        title[n++] = 'a';
      }
      while(n + 4 < 900) {  /* letters of two, three and four bytes, so that one of them is across the 512th byte */
        static const char* const some[] = {"\xe4\xb8\xad", "\xd0\xb6", "\xf0\x9f\x8e\xae"};
        const char* c = some[(n + (size_t)shift) % 3];
        memcpy(title + n, c, strlen(c));
        n += strlen(c);
      }
      u = (size_t)snprintf(url, sizeof(url), "https://long%d.example/", shift);
      while(u + 4 < 4500) {
        memcpy(url + u, "\xe4\xb8\xad", 3);
        u += 3;
      }
      url[u] = 0;
      history_add("long.txt", 1700000000 + shift, url, title);
    }
    json = history_json("long.txt", 10);
    check(json_valid(json), "titles and addresses too long for their line are cut between letters: the history stays UTF-8");
    free(json);
  }

  /* ---- and broken ones */
  for(round_now = 1; round_now <= rounds; round_now++) {
    /* the settings */
    broken("settings.good", "settings.txt");
    settings_load(&s2, "settings.txt");
    check(s2.zoom >= 25 && s2.zoom <= 300, "the zoom read is one a page can have");
    check(memchr(s2.lang, 0, sizeof(s2.lang)) && memchr(s2.theme, 0, sizeof(s2.theme)), "the settings' texts end inside their places");
    if(!settings_save(&s2, "settings.out")) {
      settings_load(&s3, "settings.out");
      settings_save(&s3, "settings.out2");
      check(same_files("settings.out", "settings.out2"), "settings read from a broken file, saved and read again, save to the same text");
    }
    settings_ad_allowed(&s2, "news.example.com");
    settings_ad_allow(&s2, "new.example.net", 1);
    /* the history */
    broken("history.good", "history.txt");
    char* json = history_json("history.txt", 1 + (int)(rnd() % 80));
    check(!json || json_shape(json), "the history of a broken file is still JSON its page can take");
    check(!json || strlen(json) < (8u << 20), "the history's list is a text that ends");
    free(json);
    json = history_recent_json("history.txt", 1 + (int)(rnd() % 12));
    check(!json || json_shape(json), "the sites opened last, of a broken file, are still JSON the start page can take");
    free(json);
    char after[80];  /* (another each round: the same address twice in a row is a reload, left out) */
    snprintf(after, sizeof(after), "https://after%d.example/", round_now);
    history_add("history.txt", 1700009999, after, "after");
    json = history_json("history.txt", 5);
    check(json && strstr(json, after), "a page added to a broken history is in it");
    free(json);
    /* the tabs of the last time */
    broken("session.good", "session.txt");
    int current = -7;
    memset(urls, 'x', sizeof(urls));
    const int max = 1 + (int)(rnd() % SESSION_TABS_MAX);
    const int tabs = session_read("session.txt", urls, max, &current);
    check(tabs >= 0 && tabs <= max, "no more tabs are read than there is place for");
    check(tabs == 0 || (current >= 0 && current < tabs), "the tab in front is one of those read");
    for(int i = 0; i < tabs; i++) {
      check(memchr(urls[i], 0, SESSION_URL_MAX) != 0, "a tab's address ends inside its place");
      check(!strncasecmp(urls[i], "http://", 7) || !strncasecmp(urls[i], "https://", 8), "a tab read is a web address");  /* (HTTP:// is one too) */
      check(!strpbrk(urls[i], "\n\r"), "a tab's address has no line end in it");
    }
    /* the saved logins */
    broken("logins.good", "logins.txt");
    logins_clear();
    logins_load("logins.txt");
    logins_count("https://example.com");
    {
      const size_t room = rnd() % 3 ? sizeof(text) : 1 + rnd() % 4000;
      memset(text, 'Z', sizeof(text));
      const int users = logins_users_json("https://example.com", text, room);
      /* (-1: they do not fit, and the text is not one: the browser puts "[]" in its place, logins_for_keys()) */
      check(users < 0 || memchr(text, 0, room) != 0, "the users of a site, when they fit, are a text that ends inside its place");
      if(users >= 0 && room == sizeof(text) && memchr(text, 0, room)) {
        check(json_shape(text), "the users of a site, of a broken file, are still JSON the page can take");
      }
    }
    logins_known("https://example.com", "ana", "x");
    logins_never("https://never.example");
    logins_put("https://new.example", "u", "p", 1700001000);
    logins_save("logins.out");
    logins_clear();
    /* the extensions: their list, and a script of theirs */
    if(rnd() % 2) {
      broken("ext.good/index.tsv", "ext/index.tsv");
      size_t n = 0;
      char* good = file_bytes("ext.good/1.user.js", &n);
      if(good) {
        file_put("ext/1.user.js", good, n);
        file_put("ext/2.user.js", good, n);
        free(good);
      }
    } else {
      size_t n = 0;
      char* good = file_bytes("ext.good/index.tsv", &n);
      if(good) {
        file_put("ext/index.tsv", good, n);
        free(good);
      }
      broken("ext.good/1.user.js", "ext/1.user.js");
      broken("ext.good/2.user.js", "ext/2.user.js");
    }
    ext_load(&list, "ext");
    check(list.count >= 0 && list.count <= EXT_MAX, "no more extensions are read than there is place for");
    json = ext_list_json(&list);
    check(!json || json_shape(json), "the extensions' list, of broken files, is still JSON its page can take");
    free(json);
    for(int i = 0; i < list.count; i++) {
      char* source = ext_source(&list, list.items[i].id);
      if(source) {
        char* wrapped = ext_wrap(&list.items[i], source, "(function (info, run) { run(); })");
        free(wrapped);
      }
      free(source);
    }
  }
  printf("%s: %d checks, %d failed (%d rounds of broken files: settings, history, tabs, logins, extensions)\n", failed ? "FAIL" : "PASS",
         checks, failed, rounds);
  return failed ? 1 : 0;
}
