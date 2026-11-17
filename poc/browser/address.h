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

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Where a search goes until the settings say (address_set_search()): the
// words are appended, URL-encoded. Google's, the default engine since
// 2026-11-05. Its searches from the PS5 ended on its
// robot check once (/sorry, a CAPTCHA, 2026-11-03): the settings and the
// start page offer the others.
#ifndef ADDRESS_SEARCH_URL
#define ADDRESS_SEARCH_URL "https://www.google.com/search?q="
#endif

typedef enum address_kind {
  ADDRESS_NONE,    // nothing typed
  ADDRESS_URL,     // an address
  ADDRESS_SEARCH,  // words to search for
} address_kind_t;

/**
 * Searches go to prefix from now on (the words appended, URL-encoded); 0 or
 * "" for ADDRESS_SEARCH_URL. prefix must stay valid.
 **/
void address_set_search(const char* prefix);

/**
 * Turns what was typed in the address bar into the URL to load, in url
 * (size bytes): a URL as it is, an address without a scheme with https://
 * (http:// for localhost, an IP or a host with a port), anything else a
 * search.
 **/
address_kind_t address_to_url(const char* typed, char* url, size_t size);

/**
 * Decodes the %XX of encodeURIComponent() from in into out (size bytes).
 **/
void address_decode(const char* in, char* out, size_t size);

#ifdef __cplusplus
}
#endif
