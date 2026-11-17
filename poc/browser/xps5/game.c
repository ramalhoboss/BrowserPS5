/* From XPS5 (github.com/ramalhoboss/XPS5, GPL-3.0-or-later), the same author's X client for the PS5:
   src/game.c, taken on 2026-11-14 for BrowserPS5 RN (the game that is played: its name, its kind, its pictures).
   Its first part (the id, the names, the hashtag); what X's posts need of it is left out. */

/* The game that runs beside the title, see game.h. */

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "json.h"


int
game_id_ok(const char *id) {
  for(int i=0; i<4; i++) {
    if(id[i] < 'A' || id[i] > 'Z') {
      return 0;
    }
  }
  for(int i=4; i<9; i++) {
    if(id[i] < '0' || id[i] > '9') {
      return 0;
    }
  }
  return !id[9] && strncmp(id, "NPXS", 4) && strcmp(id, "TWPS00001")
         && strncmp(id, "PPSA970", 7);  // (BrowserPS5: nor this browser's own titles)
}


/**
 * s, up to len bytes, into name on one line: control characters become
 * spaces, the spaces at the ends go, and a cut never splits a character;
 * 0, or -1 when nothing is left
 **/
static int
clean_name(const char *s, size_t len, char *name, size_t size) {
  size_t n = 0;

  if(!size) {
    return -1;
  }
  for(size_t i=0; i < len && s[i]; i++) {
    unsigned char c = (unsigned char)s[i];
    if(n + 1 >= size) {
      break;
    }
    if(c < 0x20 || c == 0x7f) {
      c = ' ';
    }
    if(c == ' ' && (!n || name[n - 1] == ' ')) {
      continue;
    }
    name[n++] = (char)c;
  }
  // not half a character at the end
  if(n + 1 >= size) {
    size_t at = n;
    while(at && ((unsigned char)name[at - 1] & 0xc0) == 0x80) {
      at--;
    }
    if(at && (unsigned char)name[at - 1] >= 0xc0) {
      unsigned char lead = (unsigned char)name[at - 1];
      size_t want = lead >= 0xf0 ? 4 : lead >= 0xe0 ? 3 : 2;
      if(n - (at - 1) < want) {
        n = at - 1;
      }
    }
  }
  while(n && name[n - 1] == ' ') {
    n--;
  }
  name[n] = 0;
  return n ? 0 : -1;
}


int
game_name_json(char *text, size_t len, const char *lang, char *name, size_t size) {
  const json_node_t *lp, *entry;
  const char *tries[4], *s;
  json_doc_t *doc;
  size_t off = 0;
  int ret = -1;

  if(!(doc=json_parse(text, len, &off))) {
    return -1;
  }
  lp = json_get(json_root(doc), "localizedParameters");
  tries[0] = lang;
  tries[1] = json_string(json_get(lp, "defaultLanguage"));
  tries[2] = "en-US";
  tries[3] = "en-GB";
  for(int i=0; i<4 && ret; i++) {
    if(tries[i] && (entry=json_get(lp, tries[i])) && (s=json_string(json_get(entry, "titleName")))) {
      ret = clean_name(s, strlen(s), name, size);
    }
  }
  json_free(doc);
  return ret;
}


int
game_category_json(char *text, size_t len) {
  const json_node_t *n;
  json_doc_t *doc;
  size_t off = 0;
  char *end;
  long v;
  int ret = -1;

  if(!(doc=json_parse(text, len, &off))) {
    return -1;
  }
  n = json_get(json_root(doc), "applicationCategoryType");
  if(n && n->type == JSON_NUMBER && n->str) {
    v = strtol(n->str, &end, 10);
    if(end != n->str && v >= 0 && v <= INT_MAX) {
      ret = (int)v;
    }
  }
  json_free(doc);
  return ret;
}


static unsigned
u16le(const unsigned char *p) {
  return p[0] | (unsigned)p[1] << 8;
}


static uint32_t
u32le(const unsigned char *p) {
  return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}


int
game_name_sfo(const unsigned char *buf, size_t len, char *name, size_t size) {
  uint32_t keys, data, count;

  if(len < 20 || memcmp(buf, "\0PSF", 4)) {
    return -1;
  }
  keys = u32le(buf + 8);
  data = u32le(buf + 12);
  count = u32le(buf + 16);
  if(keys >= len || data >= len || count > 1024 || 20 + (size_t)count * 16 > len) {
    return -1;
  }
  for(uint32_t i=0; i < count; i++) {
    const unsigned char *e = buf + 20 + (size_t)i * 16;
    size_t key = (size_t)keys + u16le(e), at = (size_t)data + u32le(e + 12), n = u32le(e + 4);
    if(key >= len || !memchr(buf + key, 0, len - key) || strcmp((const char *)buf + key, "TITLE") ||
       (u16le(e + 2) & 0xff) != 0x04) {
      continue;
    }
    if(at >= len || n > len - at) {
      return -1;
    }
    return clean_name((const char *)buf + at, n, name, size);
  }
  return -1;
}


int
game_hashtag(const char *name, char *tag, size_t size) {
  const unsigned char *s = (const unsigned char *)name;
  size_t n = 0;
  int letters = 0;

  if(size < 3) {
    return -1;
  }
  tag[n++] = '#';
  while(*s) {
    if((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9')) {
      if(n + 1 >= size) {
        break;
      }
      letters += !(*s >= '0' && *s <= '9');
      tag[n++] = (char)*s++;
    } else if(s[0] == 0xc3 && s[1] >= 0x80 && s[1] <= 0xbf && s[1] != 0x97 && s[1] != 0xb7) {
      // À to ÿ, without × and ÷
      if(n + 2 >= size) {
        break;
      }
      letters++;
      tag[n++] = (char)*s++;
      tag[n++] = (char)*s++;
    } else {
      s++;
    }
  }
  tag[n] = 0;
  return letters ? 0 : -1;
}
