/*   Copyright (C) 2026 BrowserPS5 contributors
 *
 * This file is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * The user's TorBox account (docs/11-torrents.md, settings "TorBox"): a
 * magnet, a .torrent or a file's link goes to TorBox, which fetches it on
 * its servers; the list of what is there comes to the settings page; a
 * file that is ready comes to the console by the browser's own downloads
 * (main.cpp: the address TorBox gives, as a download).
 *
 * The API key is the user's: read from and kept in one file of the
 * browser's data (0600), never in a log, a page or a URL that is shown
 * (requestdl wants it in its query: that request is made here, and only
 * the file's address it answers goes on). Each request runs on a thread of
 * its own (curl, torbox_http.c, through torbox_transport); what came of it
 * goes to a callback (main.cpp: back to the main thread).
 *
 * TorBox is not asked for YouTube's videos (the project's rule: no YouTube
 * downloads): such a link is refused here.
 */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TORBOX_API "https://api.torbox.app/v1/api"
#define TORBOX_KEY_MAX 128
#define TORBOX_BODY_MAX (8 << 20)  // an answer, at most

enum {
  TORBOX_ADD = 1,   // torbox_add*(): message is the bar's text
  TORBOX_LIST = 2,  // torbox_list(): torrents and web, TorBox's JSON
  TORBOX_LINK = 3,  // torbox_link(): url, the file's address
};

typedef struct torbox_result {
  int kind;
  int ok;
  char message[512];  // for the bar: what was added, or why not (Portuguese)
  char* torrents;     // TORBOX_LIST: /torrents/mylist's answer (malloc)
  char* web;          // TORBOX_LIST: /webdl/mylist's answer (malloc)
  char url[4096];     // TORBOX_LINK: https://, for a download
  void* user;
} torbox_result_t;

/* Called on the request's thread; the callback owns r (torbox_result_free). */
typedef void (*torbox_done_t)(torbox_result_t* r);
void torbox_result_free(torbox_result_t* r);

/* The key: from path (at the start), into path (the settings page), "" to
 * forget it. 0 when done. */
int torbox_key_load(const char* path);
int torbox_key_save(const char* path, const char* key);
/* 1 when there is a key; hint: its last 4 characters, for the page. */
int torbox_key_hint(char* hint, size_t size);

/* A magnet link, or an http(s) link to a file (a web download). */
int torbox_add_link(const char* link, torbox_done_t done, void* user);
/* A .torrent file: read, sent, then deleted. */
int torbox_add_file(const char* path, torbox_done_t done, void* user);
/* Both lists (torrents, web downloads), fresh (bypass_cache). */
int torbox_list(torbox_done_t done, void* user);
/* A ready file's address: web 0 for a torrent's (id: torrent_id), 1 for a
 * web download's (web_id); file is the file's id in it. */
int torbox_link(int web, long id, long file, torbox_done_t done, void* user);

/* The HTTP underneath (torbox_http.c on the console, a fake in the tests). */
typedef struct torbox_request {
  const char* method;        // "GET" or "POST"
  const char* url;           // whole, with its query
  const char* key;           // for "Authorization: Bearer", or 0
  const char* content_type;  // POST
  const char* body;          // POST
  size_t body_size;
} torbox_request_t;
/* status: the HTTP status (0: no answer); *body: malloc'd, NUL-ended. */
typedef int (*torbox_transport_t)(const torbox_request_t* req, int* status, char** body,
                                  size_t* size);
void torbox_set_transport(torbox_transport_t transport);
void torbox_set_api(const char* base);  // TORBOX_API, or the tests'
/* torbox_http.c: curl as the transport (main.cpp, at the start). */
void torbox_http_init(void);

/* For the tests: */
int torbox_link_allowed(const char* link);  // 0: YouTube's, or not magnet/http(s)
/* x-www-form-urlencoded value, into out (malloc). */
char* torbox_form_value(const char* value);
/* A multipart/form-data body with one field (name, and filename for a
 * file), the boundary chosen not in data. *size: its length. */
char* torbox_multipart(const char* name, const char* filename, const char* type, const char* data,
                       size_t data_size, char* boundary, size_t boundary_size, size_t* size);
/* What an add answered: 1 if success, message for the bar. */
int torbox_add_answer(int status, const char* body, char* message, size_t size);

#ifdef __cplusplus
}
#endif
