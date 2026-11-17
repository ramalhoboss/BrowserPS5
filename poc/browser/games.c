/* Copyright (C) 2026 BrowserPS5 contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING. If not, see
<http://www.gnu.org/licenses/>.  */

// The games on this console, see games.h. How a game is found and named is XPS5's way (xps5/game.c;
// XPS5's src/notify.c: open_game(), game_name_of(), is_game()), written again here for the browser.

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <plutovg.h>

#include "games.h"
#include "xps5/game.h"
#include "xps5/json.h"
#include "xps5/trophy.h"

int sceLncUtilGetAppId(const char* title_id);

#define APPMETA        "/user/appmeta"
#define APPMETA_SYSTEM "/system_data/priv/appmeta"
#define HOMES          "/user/home"
#define CAPTURES       "/user/av_contents"
#define CAPTURES_APP   "NPXS40087"  // the folder of the PS5's own captures (XPS5, src/captures.h)
#define IDS_MOST       1024
#define USERS_MOST     8
#define LOOKED_IN      64           // entries of a folder whose times are looked at
#define ASKED_OPEN     32           // games the system is asked about

typedef struct seen {
  char      id[16];
  long long played;  // the newest trace
  long long there;   // when its data came to the console (its appmeta folder's time): the order of the never played
  int       open;
} seen_t;


static char*
read_small(const char* path, size_t most, size_t* len) {
  FILE* f = fopen(path, "rb");
  char* buf = 0;
  long size;

  if(!f) {
    return 0;
  }
  if(!fseek(f, 0, SEEK_END) && (size = ftell(f)) > 0 && (size_t)size <= most && !fseek(f, 0, SEEK_SET)
     && (buf = (char*)malloc((size_t)size + 1))) {
    *len = fread(buf, 1, (size_t)size, f);
    buf[*len] = 0;
    if(!*len) {
      free(buf);
      buf = 0;
    }
  }
  fclose(f);
  return buf;
}


// The newest time of a folder and of what is directly in it (LOOKED_IN entries at most); 0 when it is not there.
static long long
newest_in(const char* dir) {
  struct stat st;
  long long newest = 0;
  char path[320];
  int looked = 0;

  if(stat(dir, &st) || !S_ISDIR(st.st_mode)) {
    return 0;
  }
  newest = (long long)st.st_mtime;
  DIR* d = opendir(dir);
  if(!d) {
    return newest;
  }
  for(struct dirent* e; looked < LOOKED_IN && (e = readdir(d));) {
    if(!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) {
      continue;
    }
    looked++;
    if(snprintf(path, sizeof(path), "%s/%s", dir, e->d_name) < (int)sizeof(path) && !stat(path, &st)
       && (long long)st.st_mtime > newest) {
      newest = (long long)st.st_mtime;
    }
  }
  closedir(d);
  return newest;
}


static int
exists(const char* path) {
  struct stat st;
  return !stat(path, &st) && S_ISREG(st.st_mode) && st.st_size > 0;
}


// A game's name and kind: 1 with name when it is a game with a name, 0 when it is not one (a media app, or
// nothing of it can be read).
static int
game_of(const char* id, const char* lang, char* name, size_t size, int* ps4, int* files) {
  static const char* const dirs[] = {APPMETA "/", APPMETA_SYSTEM "/"};
  char path[96], *buf;
  size_t len;
  int category = -2;

  name[0] = 0;
  *ps4 = 0;
  for(size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]) && !name[0] && category <= 0; i++) {
    snprintf(path, sizeof(path), "%s%s/param.json", dirs[i], id);
    if((buf = read_small(path, 1 << 20, &len))) {
      (*files)++;
      char* again = (char*)malloc(len + 1);  // (both readers change the text in place)
      if(again) {
        memcpy(again, buf, len + 1);
        category = game_category_json(again, len);
        free(again);
      }
      if(category <= 0) {
        game_name_json(buf, len, lang, name, size);
      }
      free(buf);
      continue;
    }
    snprintf(path, sizeof(path), "%s%s/param.sfo", dirs[i], id);
    if((buf = read_small(path, 1 << 16, &len))) {
      (*files)++;
      if(!game_name_sfo((const unsigned char*)buf, len, name, size)) {
        *ps4 = 1;
      }
      free(buf);
    }
  }
  return category <= 0 && name[0];
}


// The colour of a picture taken as a whole: the mean of its pixels that are neither nearly black, nearly white
// nor see-through (one in 6 each way), 0 when the file is no picture or has none such.
static unsigned
colour_of(const char* png) {
  plutovg_surface_t* s = plutovg_surface_load_from_image_file(png);
  unsigned long long r = 0, g = 0, b = 0, n = 0;

  if(!s) {
    return 0;
  }
  const unsigned char* data = plutovg_surface_get_data(s);
  const int w = plutovg_surface_get_width(s), h = plutovg_surface_get_height(s), stride = plutovg_surface_get_stride(s);
  for(int y = 0; data && y < h; y += 6) {
    const unsigned char* row = data + (size_t)y * (size_t)stride;
    for(int x = 0; x < w; x += 6) {
      const unsigned char* p = row + (size_t)x * 4;  // blue, green, red, alpha (premultiplied)
      const unsigned light = (unsigned)p[0] + p[1] + p[2];
      if(p[3] < 200 || light < 90 || light > 690) {
        continue;
      }
      b += p[0];
      g += p[1];
      r += p[2];
      n++;
    }
  }
  plutovg_surface_destroy(s);
  if(!n) {
    return 0;
  }
  unsigned rgb = (unsigned)(r / n) << 16 | (unsigned)(g / n) << 8 | (unsigned)(b / n);
  return rgb ? rgb : 1;
}


// len bytes of a file from off, a 0 after them; 0 when they are not all there or more than TROPHY_FILE_MOST
// (XPS5, src/notify.c, read_part()).
static unsigned char*
read_part(const char* path, unsigned long long off, unsigned long long len) {
  unsigned char* buf = 0;
  int fd;

  if(!len || len > TROPHY_FILE_MOST || (fd = open(path, O_RDONLY)) < 0) {
    return 0;
  }
  if((buf = (unsigned char*)malloc((size_t)len + 1)) && pread(fd, buf, (size_t)len, (off_t)off) == (ssize_t)len) {
    buf[len] = 0;
  } else {
    free(buf);
    buf = 0;
  }
  close(fd);
  return buf;
}


// The user's trophies of a PS5 game, as XPS5 finds them (src/notify.c: set_of_game(), record_of_set(),
// trophy_about()): the set the game names, how many trophies the set has, and how many of them the record
// written last among the users says they have. 1 with both numbers, 0 when the game has no set here or it does
// not read.
static int
trophies_of(const char* id, char users[][24], int user_count, int* total, int* have) {
  static const char* const dirs[] = {APPMETA "/", APPMETA "/external/", "/user/app/"};
  char set[TROPHY_SET_LEN] = "", path[200], record[200] = "";
  unsigned char *head = 0, *table = 0, *text = 0;
  unsigned long long off, len;
  long long newest = -1;
  struct stat st;
  size_t size;
  char* buf;
  int count, found = 0;

  *total = *have = 0;
  for(size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]) && !set[0]; i++) {
    snprintf(path, sizeof(path), "%s%s/%strophy2/npbind.dat", dirs[i], id, i == 2 ? "sce_sys/" : "");
    if((buf = read_small(path, 1 << 16, &size))) {
      trophy_set_of((const unsigned char*)buf, size, set);
      free(buf);
    }
  }
  if(!set[0]) {
    return 0;
  }
  snprintf(path, sizeof(path), "/user/trophy2/nobackup/conf/%s/TROPHY.UCP", set);
  if(!(head = read_part(path, 0, TROPHY_UCP_HEAD)) || (count = trophy_ucp_files(head, TROPHY_UCP_HEAD)) < 0) {
    goto out;
  }
  size = (size_t)count * TROPHY_UCP_ENTRY;
  if(!(table = read_part(path, TROPHY_UCP_HEAD, size)) || trophy_ucp_find(table, size, count, "tropconf.json", &off, &len)
     || !(text = read_part(path, off, len))) {
    goto out;
  }
  {
    size_t bad;
    json_doc_t* doc = json_parse((char*)text, (size_t)len, &bad);
    const json_node_t* list = doc ? json_get(json_root(doc), "trophies") : 0;
    if(list && list->type == JSON_ARRAY) {
      for(const json_node_t* item = list->child; item; item = item->next) {
        *total += item->type == JSON_OBJECT;
      }
    }
    if(doc) {
      json_free(doc);
    }
  }
  if(*total <= 0) {
    goto out;
  }
  found = 1;
  for(int u = 0; u < user_count; u++) {
    snprintf(path, sizeof(path), HOMES "/%s/trophy2/nobackup/data/%s/TRPTITLE.DAT", users[u], set);
    if(!stat(path, &st) && (long long)st.st_mtime > newest) {
      newest = (long long)st.st_mtime;
      snprintf(record, sizeof(record), "%s", path);
    }
  }
  if(record[0] && (buf = read_small(record, TROPHY_RECORD_MOST, &size))) {
    trophy_earned_t earned;
    const int n = trophy_earned((const unsigned char*)buf, size, &earned);
    *have = n > 0 ? (n > *total ? *total : n) : 0;
    free(buf);
  }
out:
  free(head);
  free(table);
  free(text);
  return found;
}


static unsigned long
le32(const unsigned char* p) {
  return p[0] | (unsigned long)p[1] << 8 | (unsigned long)p[2] << 16 | (unsigned long)p[3] << 24;
}


// The text a PS4 game's param.sfo has under key ("APP_VER"); 0, or -1.
static int
sfo_text(const unsigned char* b, size_t len, const char* key, char* out, size_t size) {
  if(len < 0x14 || memcmp(b, "\0PSF", 4)) {
    return -1;
  }
  const unsigned long keys = le32(b + 8), data = le32(b + 12), count = le32(b + 16);
  if(count > 256 || 0x14 + count * 16 > len || keys >= len || data >= len) {
    return -1;
  }
  for(unsigned long i = 0; i < count; i++) {
    const unsigned char* e = b + 0x14 + i * 16;
    const unsigned long k = keys + (e[0] | (unsigned long)e[1] << 8), n = le32(e + 4), at = data + le32(e + 12);
    if(k >= len || at >= len || n > len - at || strnlen((const char*)b + k, len - k) == len - k
       || strcmp((const char*)b + k, key)) {
      continue;
    }
    const size_t m = strnlen((const char*)b + at, n);
    snprintf(out, size, "%.*s", (int)(m < size ? m : size - 1), (const char*)b + at);
    return 0;
  }
  return -1;
}


// The version of the game the console has: a PS5 one's contentVersion, a PS4 one's APP_VER; "" when neither says.
static void
version_of(const char* id, char* version, size_t size) {
  static const char* const dirs[] = {APPMETA "/", APPMETA_SYSTEM "/"};
  char path[96], *buf;
  size_t len;

  version[0] = 0;
  for(size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]) && !version[0]; i++) {
    snprintf(path, sizeof(path), "%s%s/param.json", dirs[i], id);
    if((buf = read_small(path, 1 << 20, &len))) {
      size_t bad;
      json_doc_t* doc = json_parse(buf, len, &bad);
      if(doc) {
        const json_node_t* node = json_get(json_root(doc), "contentVersion");
        const char* v = node ? json_string(node) : 0;
        if(!v && (node = json_get(json_root(doc), "masterVersion"))) {
          v = json_string(node);
        }
        if(v) {
          snprintf(version, size, "%s", v);
        }
        json_free(doc);
      }
      free(buf);
      continue;
    }
    snprintf(path, sizeof(path), "%s%s/param.sfo", dirs[i], id);
    if((buf = read_small(path, 1 << 16, &len))) {
      sfo_text((const unsigned char*)buf, len, "APP_VER", version, size);
      free(buf);
    }
  }
}


static long
node_number(const json_node_t* n) {
  long v = 0;

  if(!n || (n->type != JSON_NUMBER && n->type != JSON_STRING) || !n->len || n->len > 9) {
    return -1;
  }
  for(size_t i = 0; i < n->len; i++) {
    if(n->str[i] < '0' || n->str[i] > '9') {
      return -1;
    }
    v = v * 10 + (n->str[i] - '0');
  }
  return v;
}


// A text of the set on one line, cut at a whole character.
static void
one_line(const char* text, char* out, size_t size) {
  size_t n = 0;

  out[0] = 0;
  if(!text || !size) {
    return;
  }
  while(*text && (unsigned char)*text <= 0x20) {
    text++;
  }
  for(; *text && n + 1 < size; text++) {
    out[n++] = (unsigned char)*text < 0x20 || *text == 0x7f ? ' ' : *text;
  }
  if(*text) {  // cut: never in the middle of a character
    while(n && ((unsigned char)out[n - 1] & 0xc0) == 0x80) {
      n--;
    }
    if(n && ((unsigned char)out[n - 1] & 0x80)) {
      n--;
    }
  }
  while(n && out[n - 1] == ' ') {
    n--;
  }
  out[n] = 0;
}


int
games_trophies(const char* id, const char* lang, game_trophy_t* out, int max) {
  static const char* const dirs[] = {APPMETA "/", APPMETA "/external/", "/user/app/"};
  char set[TROPHY_SET_LEN] = "", path[200], record[200] = "", file[48];
  unsigned char *head = 0, *table = 0, *text = 0;
  unsigned long long off, len;
  long long newest = -1;
  struct stat st;
  size_t size, bad;
  char* buf;
  int files, count = 0;

  if(!game_id_ok(id) || max <= 0) {
    return 0;
  }
  for(size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]) && !set[0]; i++) {
    snprintf(path, sizeof(path), "%s%s/%strophy2/npbind.dat", dirs[i], id, i == 2 ? "sce_sys/" : "");
    if((buf = read_small(path, 1 << 16, &size))) {
      trophy_set_of((const unsigned char*)buf, size, set);
      free(buf);
    }
  }
  if(!set[0]) {
    return 0;
  }
  snprintf(path, sizeof(path), "/user/trophy2/nobackup/conf/%s/TROPHY.UCP", set);
  if(!(head = read_part(path, 0, TROPHY_UCP_HEAD)) || (files = trophy_ucp_files(head, TROPHY_UCP_HEAD)) < 0) {
    goto out;
  }
  size = (size_t)files * TROPHY_UCP_ENTRY;
  if(!(table = read_part(path, TROPHY_UCP_HEAD, size)) || trophy_ucp_find(table, size, files, "tropconf.json", &off, &len)
     || !(text = read_part(path, off, len))) {
    goto out;
  }
  {  // the set's trophies and their grades
    json_doc_t* doc = json_parse((char*)text, (size_t)len, &bad);
    const json_node_t* list = doc ? json_get(json_root(doc), "trophies") : 0;
    if(list && list->type == JSON_ARRAY) {
      for(const json_node_t* item = list->child; item && count < max; item = item->next) {
        const long n = item->type == JSON_OBJECT ? node_number(json_get(item, "id")) : -1;
        if(n < 0) {
          continue;
        }
        game_trophy_t* t = &out[count++];
        memset(t, 0, sizeof(*t));
        t->id = (int)n;
        const json_node_t* grade = json_get(item, "grade");
        const char* g = grade ? json_string(grade) : 0;
        t->grade = !g ? 0 : g[0] == 'P' || g[0] == 'p' ? 3 : g[0] == 'G' || g[0] == 'g' ? 2 : g[0] == 'S' || g[0] == 's' ? 1 : 0;
        const json_node_t* hidden = json_get(item, "hidden");
        t->hidden = hidden && (hidden->type == JSON_TRUE || (hidden->type == JSON_NUMBER && node_number(hidden) > 0));
      }
    }
    if(doc) {
      json_free(doc);
    }
  }
  free(text);
  text = 0;
  if(!count) {
    goto out;
  }
  // their names, in the language asked for (else English, else any)
  if(!trophy_ucp_meta(table, size, files, lang, file, sizeof(file)) && !trophy_ucp_find(table, size, files, file, &off, &len)
     && (text = read_part(path, off, len))) {
    json_doc_t* doc = json_parse((char*)text, (size_t)len, &bad);
    const json_node_t* data = doc ? json_get(json_root(doc), "metadata") : 0;
    const json_node_t* list = data ? json_get(data, "trophyMetadata") : 0;
    if(list && list->type == JSON_ARRAY) {
      for(const json_node_t* item = list->child; item; item = item->next) {
        const long n = item->type == JSON_OBJECT ? node_number(json_get(item, "id")) : -1;
        for(int i = 0; n >= 0 && i < count; i++) {
          if(out[i].id == (int)n) {
            const json_node_t* name = json_get(item, "name");
            const json_node_t* detail = json_get(item, "detail");
            one_line(name ? json_string(name) : 0, out[i].name, sizeof(out[i].name));
            one_line(detail ? json_string(detail) : 0, out[i].detail, sizeof(out[i].detail));
            break;
          }
        }
      }
    }
    if(doc) {
      json_free(doc);
    }
  }
  {  // which the user has: the record written last among the users
    DIR* d = opendir(HOMES);
    for(struct dirent* e; d && (e = readdir(d));) {
      if(e->d_name[0] == '.' || strlen(e->d_name) > 16) {
        continue;
      }
      char at[200];
      snprintf(at, sizeof(at), HOMES "/%s/trophy2/nobackup/data/%s/TRPTITLE.DAT", e->d_name, set);
      if(!stat(at, &st) && (long long)st.st_mtime > newest) {
        newest = (long long)st.st_mtime;
        snprintf(record, sizeof(record), "%s", at);
      }
    }
    if(d) {
      closedir(d);
    }
    if(record[0] && (buf = read_small(record, TROPHY_RECORD_MOST, &size))) {
      trophy_earned_t earned;
      if(trophy_earned((const unsigned char*)buf, size, &earned) > 0) {
        for(int i = 0; i < count; i++) {
          out[i].have = trophy_has(&earned, out[i].id);
        }
      }
      free(buf);
    }
  }
out:
  free(head);
  free(table);
  free(text);
  return count;
}


int
games_name(const char* id, const char* lang, char* name, size_t size) {
  int ps4, files = 0;

  if(!size) {
    return 0;
  }
  name[0] = 0;
  return game_id_ok(id) && game_of(id, lang, name, size, &ps4, &files);
}


static int
by_recent(const void* a, const void* b) {
  const seen_t* x = (const seen_t*)a;
  const seen_t* y = (const seen_t*)b;
  if(x->open != y->open) {
    return y->open - x->open;
  }
  if(x->played != y->played) {
    return x->played < y->played ? 1 : -1;
  }
  if(x->there != y->there) {
    return x->there < y->there ? 1 : -1;
  }
  return strcmp(x->id, y->id);
}


static int
games_scan(game_info_t* out, int max, const char* lang, char* note, size_t note_size, int every_trophy) {
  static seen_t seen[IDS_MOST];
  char users[USERS_MOST][24];
  int ids = 0, entries = 0, user_count = 0, traces = 0, open = 0, files = 0, count = 0, with_trophies = 0;
  char path[320];
  struct stat st;
  DIR* d;

  if(note && note_size) {
    note[0] = 0;
  }
  if(!(d = opendir(APPMETA))) {
    if(note) {
      snprintf(note, note_size, "%s cannot be read", APPMETA);
    }
    return 0;
  }
  for(struct dirent* e; ids < IDS_MOST && (e = readdir(d));) {
    entries++;
    if(!game_id_ok(e->d_name)) {
      continue;
    }
    seen_t* s = &seen[ids++];
    memset(s, 0, sizeof(*s));
    snprintf(s->id, sizeof(s->id), "%s", e->d_name);
    snprintf(path, sizeof(path), APPMETA "/%s", s->id);
    s->there = stat(path, &st) ? 0 : (long long)st.st_mtime;
  }
  closedir(d);

  if((d = opendir(HOMES))) {
    for(struct dirent* e; user_count < USERS_MOST && (e = readdir(d));) {
      if(e->d_name[0] != '.' && strlen(e->d_name) < sizeof(users[0])) {
        snprintf(users[user_count++], sizeof(users[0]), "%s", e->d_name);
      }
    }
    closedir(d);
  }
  for(int i = 0; i < ids; i++) {
    seen_t* s = &seen[i];
    long long t;
    for(int u = 0; u < user_count; u++) {
      snprintf(path, sizeof(path), HOMES "/%s/savedata_prospero/%s", users[u], s->id);
      if((t = newest_in(path)) > s->played) {
        s->played = t;
      }
      snprintf(path, sizeof(path), HOMES "/%s/savedata/%s", users[u], s->id);
      if((t = newest_in(path)) > s->played) {
        s->played = t;
      }
    }
    snprintf(path, sizeof(path), CAPTURES "/photo/" CAPTURES_APP "/%s", s->id);
    if((t = newest_in(path)) > s->played) {
      s->played = t;
    }
    snprintf(path, sizeof(path), CAPTURES "/video/" CAPTURES_APP "/%s", s->id);
    if((t = newest_in(path)) > s->played) {
      s->played = t;
    }
    traces += s->played > 0;
  }
  qsort(seen, (size_t)ids, sizeof(seen[0]), by_recent);
  // Which is open now, behind the browser: the system is asked about the ones played last.
  for(int i = 0; i < ids && i < ASKED_OPEN; i++) {
    seen[i].open = sceLncUtilGetAppId(seen[i].id) > 0;
    open += seen[i].open;
  }
  if(open) {
    qsort(seen, (size_t)ids, sizeof(seen[0]), by_recent);
  }
  for(int i = 0; i < ids && count < max; i++) {
    game_info_t* g = &out[count];
    memset(g, 0, sizeof(*g));
    if(!game_of(seen[i].id, lang, g->name, sizeof(g->name), &g->ps4, &files)) {
      continue;
    }
    snprintf(g->id, sizeof(g->id), "%s", seen[i].id);
    version_of(g->id, g->version, sizeof(g->version));
    g->played = seen[i].played;
    g->open = seen[i].open;
    static const char* const dirs[] = {APPMETA, APPMETA_SYSTEM};
    for(size_t k = 0; k < sizeof(dirs) / sizeof(dirs[0]); k++) {
      if(!g->icon[0] && snprintf(path, sizeof(path), "%s/%s/icon0.png", dirs[k], g->id) < (int)sizeof(g->icon) && exists(path)) {
        snprintf(g->icon, sizeof(g->icon), "%s", path);
      }
      static const char* const pictures[] = {"pic0.png", "pic1.png"};
      for(size_t p = 0; p < 2 && !g->picture[0]; p++) {
        if(snprintf(path, sizeof(path), "%s/%s/%s", dirs[k], g->id, pictures[p]) < (int)sizeof(g->picture) && exists(path)) {
          snprintf(g->picture, sizeof(g->picture), "%s", path);
        }
      }
    }
    if(count < GAMES_COLOURS && g->icon[0]) {
      g->colour = colour_of(g->icon);
    }
    if((count < GAMES_COLOURS || every_trophy) && !g->ps4) {
      with_trophies += trophies_of(g->id, users, user_count, &g->trophies, &g->trophies_have);
    }
    count++;
  }
  if(note) {
    snprintf(note, note_size, "%d folder(s) in %s, %d with a title's id, %d user(s), %d with traces of play, %d open now, "
             "%d file(s) of names read: %d game(s) given, %d with their trophies read", entries, APPMETA, ids, user_count, traces,
             open, files, count, with_trophies);
  }
  return count;
}


int
games_recent(game_info_t* out, int max, const char* lang, char* note, size_t note_size) {
  return games_scan(out, max < GAMES_MOST ? max : GAMES_MOST, lang, note, note_size, 0);
}


int
games_library(game_info_t* out, int max, const char* lang, char* note, size_t note_size) {
  return games_scan(out, max, lang, note, note_size, 1);
}
