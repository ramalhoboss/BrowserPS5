/* Copyright (C) 2026 BrowserPS5 contributors. GPL-3.0-or-later.

   The console's files for the phone (docs/18-ficheiros-no-telemovel.md):
   which paths a phone may see, a folder as JSON, the type and the name a file
   goes out with, the bytes a video player asks for, and removing. Only the
   places the app gives (the downloads' and media folders, USB drives), never
   a hidden name, never through a link that leads out of them, never into a
   private folder (the browser's own data: saved logins, the TorBox key).
   Plain C with no state; tested on the PC by files_test.c (make test). */

#ifndef FILES_H
#define FILES_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FILES_PATH_MAX 1024     // a whole path, in bytes, with its 0
#define FILES_LIST_MAX 2000     // entries of a folder shown; the rest are counted

// Which root the path is in by its text (the index), or -1: absolute, under
// 1024 bytes, no empty, "." or ".." part, no part starting with a dot (hidden
// names, which also covers "..") and no ending "/". A root itself counts.
int files_inside(const char* path, const char* const* roots, int count);

// The same for the real path (links followed, the roots' too): its root's
// index, with the real path in out, or -1 when it does not exist, leads out
// of every root or into one of the private folders.
int files_resolve(const char* path, const char* const* roots, int count,
                  const char* const* private_dirs, int private_count, char* out, size_t size);

// A folder as JSON, malloc'd: {"entries": [{"name", "dir", "size", "time",
// ("bad": true)}], "more": n}. Folders first, then by name (case ignored);
// hidden names, links and what is neither a file nor a folder are left out.
// A name that is not UTF-8 shows with U+FFFD and "bad": true (the phone can
// not ask for it back). The private folders (skip) are left out too. 0 when
// the folder cannot be read.
char* files_list_json(const char* dir, const char* const* skip, int skip_count);

// The Content-Type a name goes out with; anything not known is a download
// (application/octet-stream); never HTML or SVG (they would run as the
// phone's page).
const char* files_type(const char* name);

// Whether a name is a video, a sound or a picture (what the TV opens).
int files_media(const char* name);

// "attachment; filename=\"...\"; filename*=UTF-8''..." for the last part of
// path: the plain name keeps only safe ASCII (others are "_").
void files_disposition(const char* path, char* out, size_t size);

// A Range header against a file of size bytes: 0 with the first and last
// byte (inclusive), 1 when there is none or it is not one we take (send it
// all), -1 when it asks for nothing the file has (416).
int files_range(const char* header, long long size, long long* from, long long* to);

// Removes a file, or a folder with nothing in it (never a link's target):
// 0, or an errno (ENOTEMPTY for a folder with things in it).
int files_remove(const char* path);

#ifdef __cplusplus
}
#endif

#endif
