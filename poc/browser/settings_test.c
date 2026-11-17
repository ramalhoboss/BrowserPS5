/* Copyright (C) 2026 BrowserPS5 contributors. GPL-3.0-or-later.

settings.c's tests, on the build machine (make test): the settings, the
history and the extensions, in a folder of /tmp. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "settings.h"

static int failures;

#define CHECK(cond)                                                     \
  do {                                                                  \
    if(!(cond)) {                                                       \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
      failures++;                                                       \
    }                                                                   \
  } while(0)

static const char script[] =
  "// ==UserScript==\n"
  "// @name         Dark Thing\n"
  "// @name:pt      Coisa escura\n"
  "// @version      1.2\n"
  "// @description  Makes it dark\n"
  "// @match        https://*.example.com/*\n"
  "// @include      http://other.org/*\n"
  "// @exclude      https://www.example.com/private/*\n"
  "// @run-at       document-start\n"
  "// @noframes\n"
  "// @grant        GM_addStyle\n"
  "// ==/UserScript==\n"
  "GM_addStyle('body { background: #000 }'); // last line is a comment";


int
main(void) {
  char dir[] = "/tmp/settings_testXXXXXX";
  char path[512];
  char json[8192];

  if(!mkdtemp(dir)) {
    perror("mkdtemp");
    return 1;
  }

  // Settings.
  settings_t s, t;
  settings_defaults(&s);
  CHECK(!strcmp(s.theme, "meia-noite") && s.zoom == 100 && s.javascript == 1);
  CHECK(settings_set(&s, "theme", "rubi") == 1);
  CHECK(settings_set(&s, "theme", "rubi") == 0);
  CHECK(settings_set(&s, "theme", "nope") == -1);
  // The season's theme (docs/19-temas-da-estacao.md).
  {
    settings_t v;  // not s: memcmp() below would see the longer names' bytes
    settings_defaults(&v);
    CHECK(settings_set(&v, "theme", "estacao") == 1 && settings_set(&v, "theme", "verao") == 1);
  }
  CHECK(!strcmp(settings_theme_for("rubi", 12, 25), "rubi"));
  CHECK(!strcmp(settings_theme_for("estacao", 1, 1), "natal"));
  CHECK(!strcmp(settings_theme_for("estacao", 1, 6), "natal"));
  CHECK(!strcmp(settings_theme_for("estacao", 1, 7), "inverno"));
  CHECK(!strcmp(settings_theme_for("estacao", 2, 29), "inverno"));
  CHECK(!strcmp(settings_theme_for("estacao", 3, 1), "primavera"));
  CHECK(!strcmp(settings_theme_for("estacao", 5, 31), "primavera"));
  CHECK(!strcmp(settings_theme_for("estacao", 6, 1), "verao"));
  CHECK(!strcmp(settings_theme_for("estacao", 8, 31), "verao"));
  CHECK(!strcmp(settings_theme_for("estacao", 9, 1), "outono"));
  CHECK(!strcmp(settings_theme_for("estacao", 10, 23), "outono"));
  CHECK(!strcmp(settings_theme_for("estacao", 10, 24), "halloween"));
  CHECK(!strcmp(settings_theme_for("estacao", 10, 31), "halloween"));
  CHECK(!strcmp(settings_theme_for("estacao", 11, 1), "outono"));
  CHECK(!strcmp(settings_theme_for("estacao", 11, 6), "outono"));
  CHECK(!strcmp(settings_theme_for("estacao", 12, 14), "inverno"));
  CHECK(!strcmp(settings_theme_for("estacao", 12, 15), "natal"));
  CHECK(!strcmp(settings_theme_for("estacao", 12, 31), "natal"));
  CHECK(settings_accent("inverno") == 0x8fd3ff && settings_accent("halloween") == 0xff8c1a);
  CHECK(settings_accent("neon") == 0x00e5ff && settings_accent("nope") == 0x5b8cff);
  {
    // Each season's theme is one the settings take.
    static const char* const seasons[] = {"inverno", "primavera", "verao", "outono", "natal", "halloween"};
    settings_t u;
    settings_defaults(&u);
    for(int i = 0; i < 6; i++) {
      CHECK(settings_set(&u, "theme", seasons[i]) == 1);
    }
  }
  // The disk cache (docs/21-cache-no-disco.md): on by default, a switch.
  {
    settings_t c;
    settings_defaults(&c);
    CHECK(c.disk_cache == 1);
    CHECK(settings_set(&c, "disk_cache", "0") == 1 && c.disk_cache == 0);
    CHECK(settings_set(&c, "disk_cache", "0") == 0);
    CHECK(settings_set(&c, "disk_cache", "yes") == -1 && c.disk_cache == 0);
  }
  // The WebProcess in the browser's title (docs/05, 2c-2): on by default, a
  // switch, and a settings file from before has it on.
  {
    settings_t c;
    settings_defaults(&c);
    CHECK(c.web_in_title == 1);
    CHECK(settings_set(&c, "web_in_title", "0") == 1 && c.web_in_title == 0);
    CHECK(settings_set(&c, "web_in_title", "0") == 0);
    CHECK(settings_set(&c, "web_in_title", "2") == -1 && c.web_in_title == 0);
    CHECK(settings_set(&c, "web_in_title", "1") == 1 && c.web_in_title == 1);
  }
  // The language (docs/13-idioma.md): Portuguese first; since 2026-11-14 also
  // en, es, fr, de, it and nl (RN's pages have tables for them); others refused.
  CHECK(!strcmp(s.lang, "pt"));
  CHECK(settings_set(&s, "lang", "en") == 1 && !strcmp(s.lang, "en"));
  CHECK(settings_set(&s, "lang", "xx") == -1 && !strcmp(s.lang, "en"));
  CHECK(settings_set(&s, "lang", "fr") == 1 && !strcmp(s.lang, "fr"));
  CHECK(settings_set(&s, "lang", "nl") == 1 && !strcmp(s.lang, "nl"));
  CHECK(settings_set(&s, "lang", "pt") == 1);
  CHECK(settings_set(&s, "zoom", "125") == 1 && s.zoom == 125);
  CHECK(settings_set(&s, "zoom", "1000") == -1);
  CHECK(settings_set(&s, "zoom", "12x") == -1);
  CHECK(settings_set(&s, "home_url", "javascript:alert(1)") == -1);
  CHECK(settings_set(&s, "home_url", "https://www.rtp.pt/") == 1);
  CHECK(settings_set(&s, "pkg_dir", "/mnt/usb0") == 1);
  CHECK(settings_set(&s, "pkg_dir", "/mnt/usb0/../../system") == -1);
  CHECK(settings_set(&s, "pkg_dir", "/system/pkg") == -1);
  CHECK(settings_set(&s, "pkg_dir", "/mnt/usbx") == -1);
  CHECK(settings_set(&s, "downloads_dir", "/data/downloads/2026") == 1);
  CHECK(settings_set(&s, "unknown", "1") == -1);
  CHECK(settings_set(&s, "last_url", "a\nb") == -1);

  // The ad blocker's exceptions.
  char site[256];
  CHECK(!settings_site("WWW.Example.COM", site, sizeof(site)) && !strcmp(site, "example.com"));
  CHECK(!settings_site("www", site, sizeof(site)) && !strcmp(site, "www"));
  CHECK(settings_site("a b.com", site, sizeof(site)) == -1);
  CHECK(settings_site("a..com", site, sizeof(site)) == -1);
  CHECK(settings_site("\"x\".com", site, sizeof(site)) == -1);
  CHECK(settings_site("", site, sizeof(site)) == -1 && settings_site(0, site, sizeof(site)) == -1);
  CHECK(settings_ad_allow(&s, "www.example.com", 1) == 1 && !strcmp(s.ad_allow, "example.com"));
  CHECK(settings_ad_allow(&s, "example.com", 1) == 0);
  CHECK(settings_ad_allow(&s, "news.example.com", 1) == 0);  // under it already
  CHECK(settings_ad_allowed(&s, "m.example.com") && settings_ad_allowed(&s, "EXAMPLE.com"));
  CHECK(!settings_ad_allowed(&s, "example.org") && !settings_ad_allowed(&s, "com"));
  CHECK(!settings_ad_allowed(&s, "badexample.com"));
  CHECK(settings_ad_allow(&s, "rtp.pt", 1) == 1 && settings_ad_allow(&s, "sapo.pt", 1) == 1);
  CHECK(!strcmp(s.ad_allow, "example.com rtp.pt sapo.pt"));
  CHECK(settings_ad_allow(&s, "www.rtp.pt", 0) == 1 && !strcmp(s.ad_allow, "example.com sapo.pt"));
  CHECK(settings_ad_allow(&s, "a.b.sapo.pt", 0) == 1 && !strcmp(s.ad_allow, "example.com"));
  CHECK(settings_ad_allow(&s, "sapo.pt", 0) == 0);
  CHECK(settings_ad_allow(&s, "x y", 1) == -1);
  for(int i = 0; i < 200; i++) {  // fills up, then refuses
    char host[32];
    snprintf(host, sizeof(host), "site%d.example.net", i);
    if(settings_ad_allow(&s, host, 1) == -1) {
      CHECK(i > 30 && strlen(s.ad_allow) < sizeof(s.ad_allow));
      break;
    }
    CHECK(i < 199);
  }
  memset(s.ad_allow, 0, sizeof(s.ad_allow));  // memcmp() below: no bytes left after the end
  settings_set(&s, "ad_allow", "example.com");
  snprintf(path, sizeof(path), "%s/settings.txt", dir);
  CHECK(!settings_save(&s, path));
  CHECK(!settings_load(&t, path));
  CHECK(!memcmp(&s, &t, sizeof(s)));
  CHECK(settings_json(&s, json, sizeof(json)) > 0);
  CHECK(strstr(json, "\"theme\": \"rubi\"") && strstr(json, "\"zoom\": 125"));
  CHECK(!strcmp(settings_engine(&s)->id, "google"));
  // The engine: a file from before 2026-11-05 (DuckDuckGo, never picked)
  // gets Google, the default now; one the user picked stays.
  {
    FILE* f = fopen(path, "w");
    CHECK(f && fputs("theme=rubi\nsearch=duckduckgo\n", f) >= 0 && !fclose(f));
    CHECK(!settings_load(&t, path));
    CHECK(!strcmp(t.search, "google") && !t.search_chosen && !strcmp(t.theme, "rubi"));
    f = fopen(path, "w");
    CHECK(f && fputs("search=duckduckgo\nsearch_chosen=1\n", f) >= 0 && !fclose(f));
    CHECK(!settings_load(&t, path));
    CHECK(!strcmp(t.search, "duckduckgo") && t.search_chosen);
    CHECK(!strcmp(settings_engine(&t)->name, "DuckDuckGo"));
  }
  CHECK(settings_accent("rubi") == 0xff4d6d && settings_accent("?") == 0x5b8cff);

  // History.
  snprintf(path, sizeof(path), "%s/history.tsv", dir);
  CHECK(!history_add(path, 100, "https://a.pt/1", "Um\ttítulo"));
  CHECK(!history_add(path, 101, "https://a.pt/1", "again"));  // left out
  CHECK(!history_add(path, 102, "file:///data/x.html", "ours"));  // left out
  CHECK(!history_add(path, 103, "https://www.b.com/x?y=\"z\"", "B"));
  CHECK(!history_add(path, 104, "https://a.pt/2", "Dois"));
  char* h = history_json(path, 10);
  CHECK(h && !strcmp(h, "[[104, \"https://a.pt/2\", \"Dois\"],\n"
                        "[103, \"https://www.b.com/x?y=\\\"z\\\"\", \"B\"],\n"
                        "[100, \"https://a.pt/1\", \"Um título\"]]"));
  free(h);
  h = history_recent_json(path, 10);
  CHECK(h && strstr(h, "a.pt/2") && strstr(h, "b.com") && !strstr(h, "a.pt/1"));
  free(h);
  CHECK(!history_clear(path));
  h = history_json(path, 10);
  CHECK(h && !strcmp(h, "[]"));
  free(h);

  // Extensions.
  ext_meta_t meta;
  CHECK(!ext_parse(script, &meta));
  CHECK(!strcmp(meta.name, "Coisa escura") && !strcmp(meta.version, "1.2"));
  CHECK(!strcmp(meta.matches, "https://*.example.com/*\nhttp://other.org/*"));
  CHECK(!strcmp(meta.excludes, "https://www.example.com/private/*"));
  CHECK(meta.run_at_start && meta.noframes);
  CHECK(ext_parse("no block here", &meta) == -1);
  ext_parse(script, &meta);

  ext_list_t list, again;
  snprintf(path, sizeof(path), "%s/extensions", dir);
  CHECK(!ext_load(&list, path) && list.count == 0);
  int id = ext_install(&list, script, &meta);
  CHECK(id == 1 && list.count == 1);
  CHECK(ext_install(&list, script, &meta) == 1 && list.count == 1);  // replaced
  CHECK(!ext_set_on(&list, 1, 0));
  CHECK(!ext_load(&again, path) && again.count == 1 && !again.items[0].on);
  char* j = ext_list_json(&again);
  CHECK(j && strstr(j, "\"name\": \"Coisa escura\"") && strstr(j, "\"on\": false"));
  free(j);
  char* source = ext_source(&again, 1);
  CHECK(source && !strcmp(source, script));
  char* wrapped = source ? ext_wrap(&again.items[0], source, "function (info, run) {}") : 0;
  CHECK(wrapped && strstr(wrapped, "(function (info, run) {})({id: 1,"));
  CHECK(wrapped && strstr(wrapped, "// last line is a comment\n});\n"));
  if(wrapped) {
    snprintf(path, sizeof(path), "%s/wrapped.js", dir);
    FILE* f = fopen(path, "w");
    if(f) {
      fputs(wrapped, f);
      fclose(f);
    }
  }
  free(wrapped);
  free(source);
  CHECK(!ext_remove(&again, 1) && again.count == 0);
  CHECK(ext_remove(&again, 1) == -1);

  printf("%s (%s)\n", failures ? "FAILED" : "all passed", dir);
  return failures != 0;
}
