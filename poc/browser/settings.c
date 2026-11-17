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

#include "settings.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define HISTORY_MAX_BYTES (2 * 1024 * 1024)
#define SOURCE_MAX_BYTES  (1024 * 1024)


// A growing string, for the JSON. fail: out of memory, the rest is dropped.
typedef struct sb {
  char* p;
  size_t n, cap;
  int fail;
} sb_t;


static void
sb_add(sb_t* b, const char* s, size_t len) {
  if(b->fail) {
    return;
  }
  if(b->n + len + 1 > b->cap) {
    size_t cap = b->cap ? b->cap : 256;
    while(cap < b->n + len + 1) {
      cap *= 2;
    }
    char* p = (char*)realloc(b->p, cap);
    if(!p) {
      b->fail = 1;
      return;
    }
    b->p = p;
    b->cap = cap;
  }
  memcpy(b->p + b->n, s, len);
  b->n += len;
  b->p[b->n] = 0;
}


static void
sb_str(sb_t* b, const char* s) {
  sb_add(b, s, strlen(s));
}


static void
sb_printf(sb_t* b, const char* format, ...) {
  char buf[512];
  va_list ap;

  va_start(ap, format);
  int n = vsnprintf(buf, sizeof(buf), format, ap);
  va_end(ap);
  if(n > 0) {
    sb_add(b, buf, (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1);
  }
}


// s, len bytes of it, as a JSON string.
static void
sb_quote_n(sb_t* b, const char* s, size_t len) {
  static const char hex[] = "0123456789abcdef";
  size_t start = 0;

  sb_add(b, "\"", 1);
  for(size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)s[i];
    if(c == '"' || c == '\\' || c < 0x20) {
      sb_add(b, s + start, i - start);
      char esc[8];
      if(c == '"' || c == '\\') {
        esc[0] = '\\';
        esc[1] = (char)c;
        sb_add(b, esc, 2);
      } else {
        snprintf(esc, sizeof(esc), "\\u00%c%c", hex[c >> 4], hex[c & 15]);
        sb_add(b, esc, 6);
      }
      start = i + 1;
    }
  }
  sb_add(b, s + start, len - start);
  sb_add(b, "\"", 1);
}


static void
sb_quote(sb_t* b, const char* s) {
  sb_quote_n(b, s, strlen(s));
}


static char*
sb_done(sb_t* b) {
  if(b->fail) {
    free(b->p);
    return 0;
  }
  if(!b->p) {
    sb_add(b, "", 0);
  }
  return b->p;
}


size_t
json_quote(char* out, size_t size, const char* s) {
  sb_t b = {0};
  sb_quote(&b, s);
  char* p = sb_done(&b);
  size_t n = p ? strlen(p) : 0;
  if(!p || n + 1 > size) {
    free(p);
    if(size) {
      out[0] = 0;
    }
    return 0;
  }
  memcpy(out, p, n + 1);
  free(p);
  return n;
}


char*
read_whole(const char* path, size_t max, size_t* length) {
  FILE* f = fopen(path, "rb");
  if(!f) {
    return 0;
  }
  char* p = (char*)malloc(max + 1);
  size_t n = p ? fread(p, 1, max, f) : 0;
  fclose(f);
  if(p) {
    p[n] = 0;
  }
  if(length) {
    *length = n;
  }
  return p;
}


static int
write_whole(const char* path, const char* data, size_t length) {
  char temporary[512];

  snprintf(temporary, sizeof(temporary), "%s.new", path);
  FILE* f = fopen(temporary, "wb");
  if(!f) {
    return -1;
  }
  int ok = fwrite(data, 1, length, f) == length;
  ok = !fclose(f) && ok;
  if(!ok || rename(temporary, path)) {
    unlink(temporary);
    return -1;
  }
  return 0;
}


// ---------------------------------------------------------------- settings

// "estacao" is no colours of its own: the season's theme, by the date
// (settings_theme_for(), docs/19-temas-da-estacao.md).
static const char* const themes[] = {
  "meia-noite", "grafite", "oled", "claro", "oceano", "ametista", "rubi",
  "floresta", "champanhe", "neon", "estacao", "inverno", "primavera", "verao",
  "outono", "natal", "halloween", 0,
};
static const uint32_t accents[] = {
  0x5b8cff, 0x2fd1c5, 0x7aa7ff, 0x1a6dff, 0x27c3ff, 0xb07cff, 0xff4d6d,
  0x3ddc84, 0xe0bb6e, 0x00e5ff, 0x5b8cff, 0x8fd3ff, 0xff7eb6, 0xffb347,
  0xff8a3d, 0xff4d4d, 0xff8c1a,
};
_Static_assert(sizeof(accents) / sizeof(accents[0]) == sizeof(themes) / sizeof(themes[0]) - 1,
               "a colour for each theme");
static const char* const startups[] = {"home", "last", 0};
// ("es" to "nl": BrowserPS5 RN's, whose pages have tables for them, browser-rn/pages/lang-more.js)
static const char* const langs[] = {"pt", "en", "es", "fr", "de", "it", "nl", 0};
static const char* const home_kinds[] = {"start", "url", 0};
static const char* const dpads[] = {"elements", "scroll", "keys", 0};  // "keys": 2026-11-11, the arrow keys to the page

// Google first, the default (2026-11-05). Its searches
// from the PS5 may end on its robot check (address.h); the others are a
// choice away, in the settings and on the start page.
static const settings_engine_t engines[] = {
  {"google", "Google", "https://www.google.com/search?q="},
  {"duckduckgo", "DuckDuckGo", "https://duckduckgo.com/?q="},
  {"bing", "Bing", "https://www.bing.com/search?q="},
  {"ecosia", "Ecosia", "https://www.ecosia.org/search?q="},
  {"startpage", "Startpage", "https://www.startpage.com/do/search?q="},
  {"brave", "Brave Search", "https://search.brave.com/search?q="},
};
static const char* const engine_ids[] = {
  "google", "duckduckgo", "bing", "ecosia", "startpage", "brave", 0,
};

typedef enum { T_CHOICE, T_TEXT, T_URL, T_PATH, T_INT, T_BOOL } type_t;

typedef struct field {
  const char* key;
  type_t type;
  size_t offset;
  size_t size;               // T_CHOICE, T_TEXT, T_URL: the buffer's
  const char* const* choices;
  int min, max;              // T_INT
} field_t;

#define STR(name, type, choices) \
  {#name, type, offsetof(settings_t, name), sizeof(((settings_t*)0)->name), choices, 0, 0}
#define INT(name, lo, hi) {#name, T_INT, offsetof(settings_t, name), 0, 0, lo, hi}
#define BOOL(name) {#name, T_BOOL, offsetof(settings_t, name), 0, 0, 0, 1}

static const field_t fields[] = {
  STR(theme, T_CHOICE, themes),
  STR(lang, T_CHOICE, langs),
  STR(startup, T_CHOICE, startups),
  STR(home_kind, T_CHOICE, home_kinds),
  STR(home_url, T_URL, 0),
  STR(search, T_CHOICE, engine_ids),
  BOOL(search_chosen),
  INT(zoom, 25, 300),
  BOOL(favbar),
  BOOL(javascript),
  BOOL(images),
  BOOL(popups),
  // (2026-11-14: "ps5_keyboard", was "system_keyboard". That switch was never shown and did nothing in a
  // normal build, and its 0 is in every settings file written since; under the new name the system's
  // keyboard is on for everyone until they turn it off themselves.)
  {"ps5_keyboard", T_BOOL, offsetof(settings_t, system_keyboard), 0, 0, 0, 1},
  BOOL(autoplay),
  BOOL(desktop),
  BOOL(third_party_cookies),
  BOOL(adblock),
  BOOL(dark_sites),
  BOOL(fullscreen),
  STR(dpad, T_CHOICE, dpads),
  INT(cursor_speed, 25, 400),
  INT(scroll_speed, 25, 400),
  BOOL(magnet),
  BOOL(sounds),
  BOOL(welcomed),
  BOOL(phone),
  BOOL(haptics),
  BOOL(triggers),
  BOOL(lightbar),
  BOOL(cursor_glow),
  BOOL(gamepad),
  BOOL(cinema),
  BOOL(devtools),
  BOOL(ask_downloads),
  BOOL(side_mobile),
  BOOL(multipart),
  BOOL(logins),
  BOOL(torbox_magnets),
  BOOL(disk_cache),
  BOOL(web_in_title),
  STR(downloads_dir, T_PATH, 0),
  STR(pkg_dir, T_PATH, 0),
  STR(media_dir, T_PATH, 0),
  STR(last_url, T_TEXT, 0),
  STR(ad_allow, T_TEXT, 0),
};
#define FIELDS (sizeof(fields) / sizeof(*fields))


void
settings_defaults(settings_t* s) {
  memset(s, 0, sizeof(*s));
  strcpy(s->theme, "meia-noite");
  strcpy(s->lang, "pt");  // a settings file from before has none: Portuguese, as it was
  strcpy(s->startup, "home");
  strcpy(s->home_kind, "start");
  strcpy(s->search, engines[0].id);
  s->search_chosen = 0;
  s->zoom = 100;
  s->favbar = 1;
  s->javascript = 1;
  s->images = 1;
  s->popups = 0;
  s->autoplay = 1;
  s->desktop = 1;
  s->third_party_cookies = 1;
  s->adblock = 1;  // built in, as asked; off in the settings or the menu
  s->dark_sites = 0;
  s->fullscreen = 1;
  strcpy(s->dpad, "elements");
  s->cursor_speed = 100;
  s->scroll_speed = 100;
  s->magnet = 1;
  s->sounds = 1;
  s->welcomed = 0;  // a settings file from before has none: the welcome shows once
  s->phone = 1;
  s->haptics = 1;
  s->triggers = 1;
  s->lightbar = 1;
  s->cursor_glow = 1;
  s->gamepad = 1;
  s->cinema = 1;
  s->devtools = 0;
  s->ask_downloads = 1;
  s->side_mobile = 1;
  s->multipart = 1;
#ifdef APP_RN
  s->system_keyboard = 1;  // (2026-11-14) the PS5's own keyboard; the browser's when it is not there
#endif
  s->logins = 1;
  s->torbox_magnets = 0;
  s->disk_cache = 1;  // a settings file from before has none: on, as a browser does
  s->web_in_title = 1;  // where the title can host it (2c-2); off: a payload, as before
  strcpy(s->downloads_dir, "/data/downloads");
  strcpy(s->pkg_dir, "/data/pkg");
  strcpy(s->media_dir, "/data/media");
}


static int
in_list(const char* const* list, const char* value) {
  for(; *list; list++) {
    if(!strcmp(*list, value)) {
      return 1;
    }
  }
  return 0;
}


int
path_allowed(const char* path) {
  static const char* const roots[] = {"/data", "/mnt/usb", "/mnt/ext", 0};

  if(path[0] != '/' || strstr(path, "/../") || strstr(path, "/./")
     || strstr(path, "//")) {
    return 0;
  }
  size_t n = strlen(path);
  if(n >= 3 && !strcmp(path + n - 3, "/..")) {
    return 0;
  }
  for(int i = 0; roots[i]; i++) {
    size_t r = strlen(roots[i]);
    if(!strncmp(path, roots[i], r)) {
      const char* rest = path + r;
      if(r > 5 && *rest >= '0' && *rest <= '9') {  // /mnt/usb0, /mnt/ext1
        rest++;
      } else if(r > 5) {
        continue;
      }
      if(!*rest || *rest == '/') {
        return 1;
      }
    }
  }
  return 0;
}


int
settings_set(settings_t* s, const char* key, const char* value) {
  for(size_t i = 0; i < FIELDS; i++) {
    const field_t* f = &fields[i];
    if(strcmp(f->key, key)) {
      continue;
    }
    char* text = (char*)s + f->offset;
    int* number = (int*)((char*)s + f->offset);
    switch(f->type) {
    case T_CHOICE:
      if(!in_list(f->choices, value)) {
        return -1;
      }
      // fallthrough
    case T_TEXT:
    case T_URL:
    case T_PATH:
      if(strlen(value) >= f->size || strchr(value, '\n') || strchr(value, '\r')) {
        return -1;
      }
      if(f->type == T_URL && *value && strncmp(value, "https://", 8)
         && strncmp(value, "http://", 7)) {
        return -1;
      }
      if(f->type == T_PATH && !path_allowed(value)) {
        return -1;
      }
      if(!strcmp(text, value)) {
        return 0;
      }
      strcpy(text, value);
      return 1;
    case T_INT:
    case T_BOOL: {
      char* end;
      long v = strtol(value, &end, 10);
      if(end == value || *end || v < f->min || v > f->max) {
        return -1;
      }
      if(*number == (int)v) {
        return 0;
      }
      *number = (int)v;
      return 1;
    }
    }
  }
  return -1;
}


int
settings_load(settings_t* s, const char* path) {
  settings_defaults(s);
  size_t length;
  char* text = read_whole(path, 64 * 1024, &length);
  if(!text) {
    return -1;
  }
  for(char* line = text; line && *line;) {
    char* next = strchr(line, '\n');
    if(next) {
      *next++ = 0;
    }
    char* eq = strchr(line, '=');
    if(eq && line[0] != '#') {
      *eq = 0;
      settings_set(s, line, eq + 1);  // one it does not take keeps its default
    }
    line = next;
  }
  free(text);
  // An engine nobody picked is the default's: files from before 2026-11-05
  // have DuckDuckGo, the default then, written in them.
  if(!s->search_chosen) {
    strcpy(s->search, engines[0].id);
  }
  return 0;
}


static void
settings_value(const settings_t* s, const field_t* f, sb_t* b, int json) {
  const char* text = (const char*)s + f->offset;
  const int* number = (const int*)((const char*)s + f->offset);
  if(f->type == T_INT || f->type == T_BOOL) {
    sb_printf(b, "%d", *number);
  } else if(json) {
    sb_quote(b, text);
  } else {
    sb_str(b, text);
  }
}


int
settings_save(const settings_t* s, const char* path) {
  sb_t b = {0};
  sb_str(&b, "# BrowserPS5's settings (the settings page writes them).\n");
  for(size_t i = 0; i < FIELDS; i++) {
    sb_str(&b, fields[i].key);
    sb_str(&b, "=");
    settings_value(s, &fields[i], &b, 0);
    sb_str(&b, "\n");
  }
  char* text = sb_done(&b);
  int err = text ? write_whole(path, text, strlen(text)) : -1;
  free(text);
  return err;
}


size_t
settings_json(const settings_t* s, char* out, size_t size) {
  sb_t b = {0};
  sb_str(&b, "{");
  for(size_t i = 0; i < FIELDS; i++) {
    sb_str(&b, i ? ", " : "");
    sb_quote(&b, fields[i].key);
    sb_str(&b, ": ");
    settings_value(s, &fields[i], &b, 1);
  }
  sb_str(&b, "}");
  char* text = sb_done(&b);
  size_t n = text ? strlen(text) : 0;
  if(!text || n + 1 > size) {
    free(text);
    return 0;
  }
  memcpy(out, text, n + 1);
  free(text);
  return n;
}


int
settings_site(const char* host, char* out, size_t size) {
  size_t n = 0;
  if(!host) {
    return -1;
  }
  if((host[0] | 0x20) == 'w' && (host[1] | 0x20) == 'w' && (host[2] | 0x20) == 'w' && host[3] == '.'
     && host[4]) {
    host += 4;
  }
  if(!*host || *host == '.' || *host == '-') {
    return -1;
  }
  for(const char* c = host; *c; c++) {
    char ch = *c >= 'A' && *c <= 'Z' ? *c - 'A' + 'a' : *c;
    if(!(ch >= 'a' && ch <= 'z') && !(ch >= '0' && ch <= '9') && ch != '.' && ch != '-') {
      return -1;
    }
    if(n + 1 >= size || n >= 253) {
      return -1;
    }
    out[n++] = ch;
  }
  out[n] = 0;
  if(out[n - 1] == '.' || strstr(out, "..")) {
    return -1;
  }
  return 0;
}


// The site in the list: where it starts, or 0.
static const char*
allow_find(const char* list, const char* site) {
  size_t n = strlen(site);
  for(const char* p = list; *p;) {
    while(*p == ' ') {
      p++;
    }
    const char* end = strchr(p, ' ');
    size_t len = end ? (size_t)(end - p) : strlen(p);
    if(len == n && !strncmp(p, site, n)) {
      return p;
    }
    p += len;
  }
  return 0;
}


int
settings_ad_allowed(const settings_t* s, const char* host) {
  char site[256];
  if(settings_site(host, site, sizeof(site))) {
    return 0;
  }
  for(const char* h = site; h;) {
    if(allow_find(s->ad_allow, h)) {
      return 1;
    }
    h = strchr(h, '.');
    h = h && strchr(h + 1, '.') ? h + 1 : 0;  // not a bare top-level domain
  }
  return 0;
}


int
settings_ad_allow(settings_t* s, const char* host, int on) {
  char site[256];
  if(settings_site(host, site, sizeof(site))) {
    return -1;
  }
  if(on) {
    if(settings_ad_allowed(s, site)) {
      return 0;
    }
    size_t used = strlen(s->ad_allow);
    if(used + (used ? 1 : 0) + strlen(site) >= sizeof(s->ad_allow)) {
      return -1;
    }
    if(used) {
      s->ad_allow[used++] = ' ';
    }
    strcpy(s->ad_allow + used, site);
    return 1;
  }
  int changed = 0;
  for(char* h = site; h;) {
    const char* at = allow_find(s->ad_allow, h);
    if(at) {
      char* p = s->ad_allow + (at - s->ad_allow);
      size_t len = strlen(h);
      if(p[len] == ' ') {
        len++;
      } else if(p > s->ad_allow) {  // the last: the space before it goes
        p--;
        len++;
      }
      memmove(p, p + len, strlen(p + len) + 1);
      changed = 1;
      continue;  // the same name again, in case it was there twice
    }
    h = strchr(h, '.');
    h = h && strchr(h + 1, '.') ? h + 1 : 0;
  }
  return changed;
}


const settings_engine_t*
settings_engine(const settings_t* s) {
  for(size_t i = 0; i < sizeof(engines) / sizeof(*engines); i++) {
    if(!strcmp(engines[i].id, s->search)) {
      return &engines[i];
    }
  }
  return &engines[0];
}


const char*
settings_theme_for(const char* theme, int month, int day) {
  if(strcmp(theme, "estacao")) {
    return theme;
  }
  if((month == 12 && day >= 15) || (month == 1 && day <= 6)) {  // to the Kings' Day
    return "natal";
  }
  if(month == 10 && day >= 24) {
    return "halloween";
  }
  if(month == 12 || month <= 2) {
    return "inverno";
  }
  return month <= 5 ? "primavera" : month <= 8 ? "verao" : "outono";
}

uint32_t
settings_accent(const char* theme) {
  for(int i = 0; themes[i]; i++) {
    if(!strcmp(themes[i], theme)) {
      return accents[i];
    }
  }
  return accents[0];
}


// ----------------------------------------------------------------- history

// A title or URL on one line of the file: no tabs, no line ends.
static void
put_clean(FILE* f, const char* s, size_t max) {
#ifdef APP_RN
  // 2026-11-14 (tests-host/files-fuzz.c): a title of more than 512 bytes was cut at the 512th, in the middle of a
  // letter if it fell there (a long title in Japanese, in Russian, with emoji): a line that is not UTF-8 in the
  // history. Cut before the letter that does not fit.
  size_t fits = 0;
  while(fits < max && s[fits]) {
    fits++;
  }
  if(s[fits]) {
    while(fits && ((unsigned char)s[fits] & 0xc0) == 0x80) {
      fits--;
    }
  }
  max = fits;
#endif
  for(size_t i = 0; s[i] && i < max; i++) {
    fputc(s[i] == '\t' || s[i] == '\n' || s[i] == '\r' ? ' ' : s[i], f);
  }
}


// Keeps the newer half of a file grown past HISTORY_MAX_BYTES.
static void
history_trim(const char* path) {
  struct stat st;
  if(stat(path, &st) || st.st_size <= HISTORY_MAX_BYTES) {
    return;
  }
  size_t length;
  char* text = read_whole(path, (size_t)st.st_size, &length);
  if(!text) {
    return;
  }
  char* from = strchr(text + length / 2, '\n');
  if(from) {
    from++;
    write_whole(path, from, length - (size_t)(from - text));
  }
  free(text);
}


int
history_add(const char* path, long when, const char* url, const char* title) {
  static char last[2048];

  if(strncmp(url, "http://", 7) && strncmp(url, "https://", 8)) {
    return 0;  // the browser's own pages are not history
  }
  if(!strncmp(last, url, sizeof(last) - 1)) {
    return 0;
  }
  snprintf(last, sizeof(last), "%s", url);
  FILE* f = fopen(path, "a");
  if(!f) {
    return -1;
  }
  fprintf(f, "%ld\t", when);
  put_clean(f, url, 4096);
  fputc('\t', f);
  put_clean(f, title ? title : "", 512);
  fputc('\n', f);
  int err = fclose(f) ? -1 : 0;
  history_trim(path);
  return err;
}


// The lines of the file, the newest first: calls each(time, url, url_len,
// title, title_len, ctx) until it returns nonzero.
typedef int (*history_each_t)(long when, const char* url, size_t url_len,
                              const char* title, size_t title_len, void* ctx);

static void
history_walk(const char* path, history_each_t each, void* ctx) {
  size_t length;
  char* text = read_whole(path, HISTORY_MAX_BYTES + 65536, &length);
  if(!text) {
    return;
  }
  size_t end = length;
  while(end > 0) {
    size_t start = end - 1;
    while(start > 0 && text[start - 1] != '\n') {
      start--;
    }
    // text[start, end) is a line, maybe with its '\n'.
    size_t stop = end;
    if(stop > start && text[stop - 1] == '\n') {
      stop--;
    }
    char* line = text + start;
    char* tab1 = (char*)memchr(line, '\t', stop - start);
    char* tab2 = tab1 ? (char*)memchr(tab1 + 1, '\t', (size_t)(text + stop - tab1 - 1)) : 0;
    if(tab1 && tab2) {
      long when = strtol(line, 0, 10);
      if(each(when, tab1 + 1, (size_t)(tab2 - tab1 - 1), tab2 + 1,
              (size_t)(text + stop - tab2 - 1), ctx)) {
        break;
      }
    }
    end = start;
  }
  free(text);
}


typedef struct history_out {
  sb_t b;
  int count, max;
  // history_recent_json: the hosts so far.
  char hosts[64][128];
  int host_count;
} history_out_t;


static int
history_one(long when, const char* url, size_t url_len, const char* title,
            size_t title_len, void* ctx) {
  history_out_t* o = (history_out_t*)ctx;
  sb_str(&o->b, o->count ? ",\n" : "");
  sb_printf(&o->b, "[%ld, ", when);
  sb_quote_n(&o->b, url, url_len);
  sb_str(&o->b, ", ");
  sb_quote_n(&o->b, title, title_len);
  sb_str(&o->b, "]");
  return ++o->count >= o->max;
}


char*
history_json(const char* path, int max) {
  history_out_t* o = (history_out_t*)calloc(1, sizeof(*o));
  if(!o) {
    return 0;
  }
  o->max = max;
  sb_str(&o->b, "[");
  history_walk(path, history_one, o);
  sb_str(&o->b, "]");
  char* json = sb_done(&o->b);
  free(o);
  return json;
}


static int
history_recent_one(long when, const char* url, size_t url_len, const char* title,
                   size_t title_len, void* ctx) {
  history_out_t* o = (history_out_t*)ctx;
  (void)when;
  const char* host = (const char*)memchr(url, ':', url_len);
  if(!host || (size_t)(host - url) + 3 > url_len) {
    return 0;
  }
  host += 3;
  size_t host_len = 0;
  while(host + host_len < url + url_len && host[host_len] != '/'
        && host[host_len] != '?' && host[host_len] != '#') {
    host_len++;
  }
  if(host_len >= 4 && !strncmp(host, "www.", 4)) {
    host += 4;
    host_len -= 4;
  }
  if(!host_len || host_len >= sizeof(o->hosts[0])) {
    return 0;
  }
  for(int i = 0; i < o->host_count; i++) {
    if(strlen(o->hosts[i]) == host_len && !strncmp(o->hosts[i], host, host_len)) {
      return 0;
    }
  }
  if(o->host_count < 64) {
    memcpy(o->hosts[o->host_count], host, host_len);
    o->hosts[o->host_count++][host_len] = 0;
  }
  sb_str(&o->b, o->count ? ",\n" : "");
  sb_str(&o->b, "{\"url\": ");
  sb_quote_n(&o->b, url, url_len);
  sb_str(&o->b, ", \"title\": ");
  sb_quote_n(&o->b, title, title_len);
  sb_str(&o->b, "}");
  return ++o->count >= o->max;
}


char*
history_recent_json(const char* path, int max) {
  history_out_t* o = (history_out_t*)calloc(1, sizeof(*o));
  if(!o) {
    return 0;
  }
  o->max = max < 64 ? max : 64;
  sb_str(&o->b, "[");
  history_walk(path, history_recent_one, o);
  sb_str(&o->b, "]");
  char* json = sb_done(&o->b);
  free(o);
  return json;
}


int
history_clear(const char* path) {
  return unlink(path) && access(path, F_OK) == 0 ? -1 : 0;
}


// -------------------------------------------------------------- extensions

static void
copy_trimmed(char* out, size_t size, const char* s, size_t len) {
  while(len && (*s == ' ' || *s == '\t')) {
    s++;
    len--;
  }
  while(len && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r')) {
    len--;
  }
  if(len >= size) {
    len = size - 1;
  }
  for(size_t i = 0; i < len; i++) {
    out[i] = s[i] == '\t' ? ' ' : s[i];
  }
  out[len] = 0;
}


static void
append_line(char* list, size_t size, const char* value) {
  size_t n = strlen(list);
  if(n + strlen(value) + 2 > size) {
    return;
  }
  if(n) {
    list[n++] = '\n';
  }
  strcpy(list + n, value);
}


int
ext_parse(const char* source, ext_meta_t* meta) {
  memset(meta, 0, sizeof(*meta));
  const char* begin = strstr(source, "==UserScript==");
  const char* end = begin ? strstr(begin, "==/UserScript==") : 0;
  if(!end) {
    return -1;
  }
  int name_local = 0, description_local = 0;
  for(const char* line = strchr(begin, '\n'); line && line < end;) {
    line++;
    const char* stop = strchr(line, '\n');
    if(!stop || stop > end) {
      stop = end;
    }
    const char* p = line;
    while(p < stop && (*p == ' ' || *p == '\t' || *p == '/')) {
      p++;
    }
    if(p < stop && *p == '@') {
      const char* key = ++p;
      while(p < stop && *p != ' ' && *p != '\t') {
        p++;
      }
      size_t key_len = (size_t)(p - key);
      char value[1024];
      copy_trimmed(value, sizeof(value), p, (size_t)(stop - p));
#define KEY(k) (key_len == strlen(k) && !strncmp(key, k, key_len))
      // A name or description in Portuguese wins over the plain one.
      int local = key_len > 5 && (!strncmp(key + key_len - 3, ":pt", 3)
                                  || !strncmp(key + key_len - 6, ":pt-PT", 6)
                                  || !strncmp(key + key_len - 6, ":pt-BR", 6));
      if(KEY("name") || (local && !strncmp(key, "name:", 5))) {
        if(!meta->name[0] || (local && !name_local)) {
          copy_trimmed(meta->name, sizeof(meta->name), value, strlen(value));
          name_local |= local;
        }
      } else if(KEY("description") || (local && !strncmp(key, "description:", 12))) {
        if(!meta->description[0] || (local && !description_local)) {
          copy_trimmed(meta->description, sizeof(meta->description), value, strlen(value));
          description_local |= local;
        }
      } else if(KEY("version")) {
        copy_trimmed(meta->version, sizeof(meta->version), value, strlen(value));
      } else if(KEY("match") || KEY("include")) {
        append_line(meta->matches, sizeof(meta->matches), value);
      } else if(KEY("exclude") || KEY("exclude-match")) {
        append_line(meta->excludes, sizeof(meta->excludes), value);
      } else if(KEY("run-at")) {
        meta->run_at_start = !strcmp(value, "document-start");
      } else if(KEY("noframes")) {
        meta->noframes = 1;
      }
#undef KEY
    }
    line = stop < end ? stop : 0;
  }
  return meta->name[0] ? 0 : -1;
}


static void
source_path(const ext_list_t* list, int id, char* path, size_t size) {
  snprintf(path, size, "%s/%d.user.js", list->dir, id);
}


char*
ext_source(const ext_list_t* list, int id) {
  char path[512];
  source_path(list, id, path, sizeof(path));
  return read_whole(path, SOURCE_MAX_BYTES, 0);
}


int
ext_load(ext_list_t* list, const char* dir) {
  char path[512];

  memset(list, 0, sizeof(*list));
  snprintf(list->dir, sizeof(list->dir), "%s", dir);
  snprintf(path, sizeof(path), "%s/index.tsv", dir);
  char* text = read_whole(path, 64 * 1024, 0);
  if(!text) {
    return 0;
  }
  for(char* line = text; line && *line && list->count < EXT_MAX;) {
    char* next = strchr(line, '\n');
    if(next) {
      *next++ = 0;
    }
    int id, on;
    if(sscanf(line, "%d\t%d", &id, &on) == 2 && id > 0) {
      char* source = ext_source(list, id);
      ext_t* x = &list->items[list->count];
      if(source && !ext_parse(source, &x->meta)) {
        x->id = id;
        x->on = on ? 1 : 0;
        list->count++;
      }
      free(source);
    }
    line = next;
  }
  free(text);
  return 0;
}


int
ext_save(const ext_list_t* list) {
  char path[512];
  sb_t b = {0};

  mkdir(list->dir, 0755);
  for(int i = 0; i < list->count; i++) {
    sb_printf(&b, "%d\t%d\n", list->items[i].id, list->items[i].on);
  }
  char* text = sb_done(&b);
  snprintf(path, sizeof(path), "%s/index.tsv", list->dir);
  int err = text ? write_whole(path, text, strlen(text)) : -1;
  free(text);
  return err;
}


int
ext_install(ext_list_t* list, const char* source, const ext_meta_t* meta) {
  char path[512];
  int id = 1;

  if(list->count >= EXT_MAX) {
    return -1;
  }
  // The same script again (same name): it replaces the old one.
  for(int i = 0; i < list->count; i++) {
    if(!strcmp(list->items[i].meta.name, meta->name)) {
      id = list->items[i].id;
      source_path(list, id, path, sizeof(path));
      if(write_whole(path, source, strlen(source))) {
        return -1;
      }
      list->items[i].meta = *meta;
      list->items[i].on = 1;
      return ext_save(list) ? -1 : id;
    }
    if(list->items[i].id >= id) {
      id = list->items[i].id + 1;
    }
  }
  mkdir(list->dir, 0755);
  source_path(list, id, path, sizeof(path));
  if(write_whole(path, source, strlen(source))) {
    return -1;
  }
  ext_t* x = &list->items[list->count++];
  x->id = id;
  x->on = 1;
  x->meta = *meta;
  return ext_save(list) ? -1 : id;
}


static ext_t*
ext_find(ext_list_t* list, int id) {
  for(int i = 0; i < list->count; i++) {
    if(list->items[i].id == id) {
      return &list->items[i];
    }
  }
  return 0;
}


int
ext_set_on(ext_list_t* list, int id, int on) {
  ext_t* x = ext_find(list, id);
  if(!x) {
    return -1;
  }
  x->on = on ? 1 : 0;
  return ext_save(list);
}


int
ext_remove(ext_list_t* list, int id) {
  char path[512];
  ext_t* x = ext_find(list, id);
  if(!x) {
    return -1;
  }
  source_path(list, id, path, sizeof(path));
  unlink(path);
  memmove(x, x + 1, (size_t)(list->items + list->count - x - 1) * sizeof(*x));
  list->count--;
  return ext_save(list);
}


// The lines of a \n list as a JSON array of strings.
static void
sb_lines(sb_t* b, const char* lines) {
  sb_str(b, "[");
  int first = 1;
  while(*lines) {
    size_t len = strcspn(lines, "\n");
    if(len) {
      sb_str(b, first ? "" : ", ");
      sb_quote_n(b, lines, len);
      first = 0;
    }
    lines += len + (lines[len] == '\n');
  }
  sb_str(b, "]");
}


char*
ext_list_json(const ext_list_t* list) {
  sb_t b = {0};
  sb_str(&b, "[");
  for(int i = 0; i < list->count; i++) {
    const ext_t* x = &list->items[i];
    sb_printf(&b, "%s{\"id\": %d, \"on\": %s, \"name\": ", i ? ",\n" : "", x->id,
              x->on ? "true" : "false");
    sb_quote(&b, x->meta.name);
    sb_str(&b, ", \"version\": ");
    sb_quote(&b, x->meta.version);
    sb_str(&b, ", \"description\": ");
    sb_quote(&b, x->meta.description);
    sb_str(&b, ", \"matches\": ");
    sb_lines(&b, x->meta.matches);
    sb_str(&b, "}");
  }
  sb_str(&b, "]");
  return sb_done(&b);
}


char*
ext_wrap(const ext_t* ext, const char* source, const char* gm) {
  sb_t b = {0};
  sb_str(&b, "(");
  sb_str(&b, gm);
  sb_printf(&b, ")({id: %d, name: ", ext->id);
  sb_quote(&b, ext->meta.name);
  sb_str(&b, ", version: ");
  sb_quote(&b, ext->meta.version);
  sb_str(&b, ", description: ");
  sb_quote(&b, ext->meta.description);
  sb_str(&b, ", matches: ");
  sb_lines(&b, ext->meta.matches);
  sb_str(&b, ", excludes: ");
  sb_lines(&b, ext->meta.excludes);
  // The script's own lines start on a line of their own and end before
  // one: a last line that is a // comment does not swallow the "});".
  sb_str(&b, "}, function (GM_info, GM_addStyle, GM_getValue, GM_setValue, "
             "GM_deleteValue, GM_listValues, GM_xmlhttpRequest, GM_openInTab, "
             "GM_setClipboard, GM_notification, GM_registerMenuCommand, "
             "GM_unregisterMenuCommand, GM_getResourceText, GM_getResourceURL, "
             "GM_log, unsafeWindow, GM) {\n");
  sb_str(&b, source);
  sb_str(&b, "\n});\n");
  return sb_done(&b);
}
