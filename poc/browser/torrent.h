/*   Copyright (C) 2026 BrowserPS5 contributors
 *
 * This file is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * The browser's torrents (docs/11-torrents.md): magnet links and .torrent
 * files go to Transmission's daemon (../torrent, torrentd.elf), a payload
 * of its own that keeps going with the browser closed. When it does not
 * answer on 127.0.0.1:TORRENT_RPC_PORT, it is sent to the payload loader
 * (as the title sends the browser, title.cpp start_browser()), and asked
 * once it answers. Each request runs on a thread of its own; what came of
 * it goes to a callback (main.cpp: back to the main thread, then the bar).
 */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TORRENT_RPC_PORT 9091
#define TORRENT_WEB_URL "http://127.0.0.1:9091/transmission/web/"
#define TORRENT_ELF "/data/homebrew/PPSA97020/torrentd.elf"

enum {
  TORRENT_ADDED = 0,      // name: the torrent's
  TORRENT_DUPLICATE = 1,  // it was there already
  TORRENT_FAILED = 2,     // name: why, in Portuguese for the bar
  TORRENT_RUNNING = 3,    // torrent_start(): the daemon answers
};

typedef void (*torrent_done_t)(int result, const char* name, void* user);

/* A magnet link (magnet:?xt=...). 0 when the request started. */
int torrent_magnet(const char* link, torrent_done_t done, void* user);

/* A .torrent file: read, sent, then deleted. 0 when the request started. */
int torrent_file(const char* path, torrent_done_t done, void* user);

/* The daemon running (started if it was not): TORRENT_RUNNING or
 * TORRENT_FAILED, for the menu's "Torrents" (TORRENT_WEB_URL after it). */
int torrent_start(torrent_done_t done, void* user);

/* For the tests and the requests: */
size_t torrent_base64(const unsigned char* in, size_t size, char* out, size_t out_size);
/* The JSON of a torrent-add for filename (a magnet) or metainfo (base64). */
char* torrent_add_json(const char* filename, const char* metainfo_base64);
/* What a torrent-add answered: TORRENT_ADDED, TORRENT_DUPLICATE or
 * TORRENT_FAILED, with the torrent's name (or the error) in name. */
int torrent_add_result(const char* body, char* name, size_t name_size);
/* A JSON string's value after "key": (the first one), unescaped (UTF-8)
 * into out: 0, or -1 when there is none (torbox.c reads TorBox's too). */
int torrent_json_string(const char* body, const char* key, char* out, size_t size);
/* The RPC host (127.0.0.1) and port: the tests point it at a fake one. */
void torrent_set_rpc(int port);

#ifdef __cplusplus
}
#endif
