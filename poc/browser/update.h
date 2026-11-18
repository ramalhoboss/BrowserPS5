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

// BrowserPS5's own updates (2026-11-17), after XPS5's (the author's other PS5 app: src/update.h there).
//
// A release published on the project's GitHub is the update: nothing else has to be made for one. The browser
// asks GitHub's public interface for the repository's releases (UPDATE_FEED), takes the newest that is not a
// draft and has the installer among its files (UPDATE_ASSET), with the size and the SHA-256 GitHub itself gives
// for that file ("digest"), and compares its version with the one installed. The file is then fetched from the
// release's own address, checked against that size and that SHA-256, and handed to the console's ELF loader once
// the browser has closed (../update_after): it is the installer anyone sends by hand, and does what it does.
//
// This file is the part with no network and no console in it: what a list of releases says, which version is
// newer, which address a file may be fetched from. It is tested on the PC (update_test.c).

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// The repository whose releases are the updates ("owner/name"); a fork names its own. Empty: none are looked for.
#ifndef UPDATE_REPO
#define UPDATE_REPO "ramalhoboss/BrowserPS5"
#endif
#define UPDATE_FEED  "https://api.github.com/repos/" UPDATE_REPO "/releases?per_page=10"
#define UPDATE_ASSET "BrowserPS5.elf"

#define UPDATE_VERSION 32            // "1.10.3-rc12", with room
#define UPDATE_URL     320
#define UPDATE_MAX     (512ull << 20)  // bytes of the installer, at most (1.0.0-rc13's has 116 MB)

typedef struct update {
  char version[UPDATE_VERSION];  // without the tag's "v"
  char url[UPDATE_URL];          // https://github.com/<UPDATE_REPO>/releases/download/<tag>/BrowserPS5.elf
  unsigned long long size;
  unsigned char sha256[32];
} update_t;

/**
 * Whether the version found is newer than have. A version is up to four numbers with dots, and may end in
 * "-rc<number>": a release candidate comes before its release ("1.0.0-rc13" < "1.0.0-rc14" < "1.0.0" <
 * "1.0.1-rc1"). 0 when either is not a version.
 **/
int update_newer(const char* have, const char* found);

/**
 * GitHub's list of a repository's releases (json, len bytes, changed in place) into u: the newest version among
 * the releases that are not drafts, are tagged "v<version>" and have a file UPDATE_ASSET that is uploaded, of 1
 * to UPDATE_MAX bytes, with a "sha256:" digest and the address its tag and repo make. 0, or -1 when there is none
 * (or the text is not that list).
 **/
int update_from_releases(char* json, size_t len, const char* repo, update_t* u);

#ifdef __cplusplus
}
#endif
