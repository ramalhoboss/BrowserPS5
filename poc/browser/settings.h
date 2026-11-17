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

#pragma once

// What the browser keeps in its data directory besides WebKit's own
// (docs/07-navegacao-e-definicoes.md): the settings (settings.txt, a
// key=value a line), the history (history.tsv, time, URL and title a line)
// and the extensions, user scripts as Greasemonkey's and Tampermonkey's
// (extensions/<id>.user.js and extensions/index.tsv). Plain C with no
// WebKit, so settings_test.c runs it on the build machine.

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct settings {
  char theme[32];        // theme.css's data-theme
  char lang[4];          // the own pages' language: "pt" or "en" (lang.js, docs/13-idioma.md)
  char startup[8];       // "home", or "last": the page open at the end
  char home_kind[8];     // "start" (start.html) or "url"
  char home_url[1024];
  char search[16];       // settings_engines[]'s id
  int search_chosen;     // 1 once the user picked it; else the default (Google)
  int zoom;              // the pages', in percent
  int favbar;            // the favorites under the address
  int javascript, images, popups, autoplay, desktop, third_party_cookies;
  int system_keyboard;  // BrowserPS5 RN: the system's keyboard instead of the browser's (main.cpp, open_keyboard())
  int adblock, dark_sites, fullscreen;  // the built-in extensions
  char dpad[12];         // "elements" (nav.js), "scroll" or "keys" (the arrow keys, to the page)
  int cursor_speed, scroll_speed;  // percent
  int magnet;            // the cursor drawn to what is clickable when the stick stops
  int sounds;            // the interface's sounds (the title plays them)
  int welcomed;          // the welcome (a style to choose) was seen
  int phone;             // phones may connect (phone.c, settings "Telemóvel")
  int haptics;           // short vibrations at the interface's moments (title/feel.c)
  int triggers;          // L2 and R2 click (the adaptive triggers)
  int lightbar;          // the light bar in the theme's colour
  int cursor_glow;       // the neon light that chases the cursor (BrowserPS5 RN, main.cpp glow_draw())
  int gamepad;           // web games may have the controller (ext/gamepad.js)
  int cinema;            // a video in full screen: no cursor, sounds or vibrations, the light low
  int devtools;          // R3 opens the developer console
  int ask_downloads;     // the "Guardar como" dialog for each download
  int side_mobile;       // RN: "Abrir ao lado do jogo" asks for the sites' phone pages (side_card_user_agent())
  int multipart;         // RN: large downloads by several connections at once (main.cpp multi_*)
  int logins;            // offer to keep a site's password (logins.h)
  int torbox_magnets;    // magnets and .torrent files to TorBox (torbox.h), not Transmission
  int disk_cache;        // the pages' HTTP cache in DATA_DIR/cache (docs/21); taken at the start
  int web_in_title;      // the WebProcess runs in the browser's title (2c-2, docs/05); taken at the start
  char downloads_dir[512];  // where each kind of file goes by default
  char pkg_dir[512];
  char media_dir[512];
  char last_url[2048];
  // The sites where the ad blocker does nothing: hosts without "www.",
  // separated by spaces (settings_ad_allow()).
  char ad_allow[1024];
} settings_t;

typedef struct settings_engine {
  const char* id;
  const char* name;
  const char* prefix;    // the words go after it
} settings_engine_t;

void settings_defaults(settings_t* s);
// Missing keys keep their defaults. 0 when the file was read.
int settings_load(settings_t* s, const char* path);
// Through a new file renamed over it. 0 on success.
int settings_save(const settings_t* s, const char* path);
// One setting from text (the settings page's set?key=value). 1 when it
// changed, 0 when it was so already, -1 for a key or value it does not take.
int settings_set(settings_t* s, const char* key, const char* value);
// The settings as a JSON object, at out. Its length, or 0 if it did not fit.
size_t settings_json(const settings_t* s, char* out, size_t size);
const settings_engine_t* settings_engine(const settings_t* s);
// The theme's accent colour, 0xRRGGBB (the D-pad's ring).
uint32_t settings_accent(const char* theme);
// The theme to show on a day (month 1-12): the theme itself, or for
// "estacao" the season's (docs/19-temas-da-estacao.md): "natal" from 15
// December to 6 January, "halloween" from 24 to 31 October, then "inverno"
// (December to February), "primavera" (March to May), "verao" (June to
// August) and "outono" (September to November), as the seasons go in
// Portugal.
const char* settings_theme_for(const char* theme, int month, int day);

// The ad blocker's exceptions (ad_allow). A site is a host name, kept in
// lower case and without "www."; a host is allowed when it is one of them or
// under one (m.example.com under example.com).
// The site of a host, at out: 0, or -1 when it is not a host name.
int settings_site(const char* host, char* out, size_t size);
// 1 when the host is allowed.
int settings_ad_allowed(const settings_t* s, const char* host);
// Allows the host's site (on), or stops allowing every site the host is or
// is under (off). 1 when it changed, 0 when it was so already, -1 for no
// host name or no more room.
int settings_ad_allow(settings_t* s, const char* host, int on);

// The history, newest at the end of the file. history_add() leaves out the
// same URL twice in a row (a reload) and keeps the file under ~2 MiB.
int history_add(const char* path, long when, const char* url, const char* title);
// [[time, "url", "title"], ...], the newest first, at most max; malloc'd.
char* history_json(const char* path, int max);
// The sites opened last, one entry a host, as [{"url": ..., "title": ...}],
// at most max; malloc'd.
char* history_recent_json(const char* path, int max);
int history_clear(const char* path);

// Extensions. A user script's own description of itself, from its
// ==UserScript== block.
typedef struct ext_meta {
  char name[128];
  char version[32];
  char description[256];
  char matches[1024];    // @match and @include, \n between them
  char excludes[512];    // @exclude
  int run_at_start;      // @run-at document-start
  int noframes;          // @noframes
} ext_meta_t;

typedef struct ext {
  int id;
  int on;
  ext_meta_t meta;
} ext_t;

#define EXT_MAX 64

typedef struct ext_list {
  char dir[256];
  int count;
  ext_t items[EXT_MAX];
} ext_list_t;

// 0 when source has a ==UserScript== block with a @name.
int ext_parse(const char* source, ext_meta_t* meta);
// dir/index.tsv (a missing one is an empty list).
int ext_load(ext_list_t* list, const char* dir);
int ext_save(const ext_list_t* list);
// Saves the script and adds it, on; its id, or -1.
int ext_install(ext_list_t* list, const char* source, const ext_meta_t* meta);
int ext_set_on(ext_list_t* list, int id, int on);
int ext_remove(ext_list_t* list, int id);
// [{"id":..,"name":..,"version":..,"description":..,"matches":[..],"on":..}]
char* ext_list_json(const ext_list_t* list);
// The script as WebKit gets it: run only where its @match/@include say and
// not where @exclude does, with the GM_* functions most scripts use, given
// by gm (ext/gm.js, a function of (info, run)). malloc'd, or 0.
char* ext_wrap(const ext_t* ext, const char* source, const char* gm);
// dir/<id>.user.js, malloc'd, or 0.
char* ext_source(const ext_list_t* list, int id);

// A folder the browser may write into: under /data or a USB drive
// (/mnt/usbN, /mnt/extN), with no "..". 1 if so.
int path_allowed(const char* path);

// A file read whole (at most max bytes), malloc'd with a 0 after it.
char* read_whole(const char* path, size_t max, size_t* length);
// s as a JSON string (with the quotes) at out; its length.
size_t json_quote(char* out, size_t size, const char* s);

#ifdef __cplusplus
}
#endif
