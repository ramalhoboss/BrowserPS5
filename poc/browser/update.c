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

// See update.h.

#include "update.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xps5/json.h"

// A version's numbers (missing ones 0) and its candidate's number (0 for "-rc0"; none: RELEASED, after them all).
#define RELEASED 1000000L

typedef struct version {
  long n[4];
  long rc;
} version_t;

static int
version_read(const char* v, version_t* out) {
  int part = 0, digits = 0;

  memset(out, 0, sizeof(*out));
  out->rc = RELEASED;
  if(!v || !*v || strlen(v) >= UPDATE_VERSION) {
    return 0;
  }
  for(; *v && *v != '-'; v++) {
    if(*v >= '0' && *v <= '9') {
      if(++digits > 5) {
        return 0;
      }
      out->n[part] = out->n[part] * 10 + (*v - '0');
    } else if(*v == '.' && digits && part < 3) {
      part++;
      digits = 0;
    } else {
      return 0;
    }
  }
  if(!digits) {
    return 0;
  }
  if(*v == '-') {
    if(strncmp(v, "-rc", 3) || !v[3]) {
      return 0;
    }
    out->rc = 0;
    digits = 0;
    for(v += 3; *v; v++) {
      if(*v < '0' || *v > '9' || ++digits > 5) {
        return 0;
      }
      out->rc = out->rc * 10 + (*v - '0');
    }
  }
  return 1;
}

static int
version_compare(const version_t* a, const version_t* b) {
  for(int i = 0; i < 4; i++) {
    if(a->n[i] != b->n[i]) {
      return a->n[i] < b->n[i] ? -1 : 1;
    }
  }
  return a->rc == b->rc ? 0 : a->rc < b->rc ? -1 : 1;
}

int
update_newer(const char* have, const char* found) {
  version_t a, b;
  return version_read(have, &a) && version_read(found, &b) && version_compare(&a, &b) < 0;
}

static int
hex_value(char c) {
  return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

// "owner/name": letters, digits, '.', '-' and '_' on each side of one '/'.
static int
repo_ok(const char* repo) {
  int slashes = 0;
  size_t n = repo ? strlen(repo) : 0;

  if(n < 3 || n > 100 || repo[0] == '/' || repo[n - 1] == '/') {
    return 0;
  }
  for(size_t i = 0; i < n; i++) {
    const char c = repo[i];
    if(c == '/') {
      slashes++;
    } else if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_')) {
      return 0;
    }
  }
  return slashes == 1;
}

// One release's installer into u (its version already there); 0 when it has it whole.
static int
asset_take(const json_node_t* release, const char* repo, const char* tag, update_t* u) {
  const json_node_t* assets = json_get(release, "assets");
  char want[UPDATE_URL];

  if(!assets || assets->type != JSON_ARRAY) {
    return -1;
  }
  if(snprintf(want, sizeof(want), "https://github.com/%s/releases/download/%s/" UPDATE_ASSET, repo, tag) >= (int)sizeof(want)) {
    return -1;
  }
  for(const json_node_t* a = assets->child; a; a = a->next) {
    const char* name = json_string(json_get(a, "name"));
    const char* state = json_string(json_get(a, "state"));
    const char* url = json_string(json_get(a, "browser_download_url"));
    const char* digest = json_string(json_get(a, "digest"));
    const json_node_t* size = json_get(a, "size");
    char* end = 0;

    if(!name || strcmp(name, UPDATE_ASSET)) {
      continue;
    }
    if(!state || strcmp(state, "uploaded") || !url || strcmp(url, want) || !digest || strncmp(digest, "sha256:", 7)
       || strlen(digest) != 7 + 64 || !size || size->type != JSON_NUMBER || !size->str || size->str[0] < '1'
       || size->str[0] > '9') {
      return -1;
    }
    const unsigned long long bytes = strtoull(size->str, &end, 10);
    if((size_t)(end - size->str) != size->len || bytes > UPDATE_MAX) {
      return -1;
    }
    for(int i = 0; i < 32; i++) {
      const int hi = hex_value(digest[7 + 2 * i]), lo = hex_value(digest[8 + 2 * i]);
      if(hi < 0 || lo < 0) {
        return -1;
      }
      u->sha256[i] = (unsigned char)(hi << 4 | lo);
    }
    u->size = bytes;
    memcpy(u->url, want, strlen(want) + 1);
    return 0;
  }
  return -1;
}

int
update_from_releases(char* json, size_t len, const char* repo, update_t* u) {
  size_t err = 0;
  json_doc_t* doc;
  const json_node_t* root;
  version_t best_version;
  int found = 0;

  memset(u, 0, sizeof(*u));
  if(!repo_ok(repo) || !json || !(doc=json_parse(json, len, &err))) {
    return -1;
  }
  root = json_root(doc);
  if(root && root->type == JSON_ARRAY) {
    for(const json_node_t* r = root->child; r; r = r->next) {
      const json_node_t* draft = json_get(r, "draft");
      const char* tag = json_string(json_get(r, "tag_name"));
      version_t v;
      update_t one;

      if(r->type != JSON_OBJECT || !draft || draft->type != JSON_FALSE || !tag || tag[0] != 'v' || !version_read(tag + 1, &v)) {
        continue;
      }
      if(found && version_compare(&v, &best_version) <= 0) {
        continue;
      }
      memset(&one, 0, sizeof(one));
      memcpy(one.version, tag + 1, strlen(tag + 1) + 1);
      if(asset_take(r, repo, tag, &one)) {
        continue;
      }
      *u = one;
      best_version = v;
      found = 1;
    }
  }
  json_free(doc);
  if(!found) {
    memset(u, 0, sizeof(*u));
  }
  return found ? 0 : -1;
}
