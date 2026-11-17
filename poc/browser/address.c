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

// The rules follow Epiphany's ephy_embed_utils_normalize_address and
// ephy_embed_utils_address_is_valid (GNOME Web, embed/ephy-embed-utils.c,
// GPL-3.0-or-later), in plain C without GLib. Epiphany asks libsoup's
// public suffix list whether "x.y" is a real domain; here the last label
// only has to be 2 to 63 letters, so "node.js" passes for an address.

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "address.h"

// The schemes WebKit loads here; anything else is searched for (mailto:,
// javascript:, ...).
static const char* const schemes[] = {"http", "https", "file", "about", "data"};


// ASCII only: no locale.
static int
is_alpha(unsigned char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}


static int
is_digit(unsigned char c) {
  return c >= '0' && c <= '9';
}


static int
is_space(unsigned char c) {
  return c == ' ' || (c >= '\t' && c <= '\r');
}


static int
is_ipv4(const char* host, size_t len) {
  int parts = 0;
  int digits = 0;
  int value = 0;

  for(size_t i = 0; i < len; i++) {
    if(host[i] == '.') {
      if(!digits) {
        return 0;
      }
      parts++;
      digits = 0;
      value = 0;
    } else if(is_digit(host[i])) {
      value = value * 10 + host[i] - '0';
      if(++digits > 3 || value > 255) {
        return 0;
      }
    } else {
      return 0;
    }
  }
  return digits && parts == 3;
}


// Letters only, 2 to 63; a byte >= 0x80 counts as one (a TLD in UTF-8).
static int
is_tld(const char* label, size_t len) {
  if(len < 2 || len > 63) {
    return 0;
  }
  for(size_t i = 0; i < len; i++) {
    if(!is_alpha(label[i]) && (unsigned char)label[i] < 0x80) {
      return 0;
    }
  }
  return 1;
}


// The scheme to put in front of host[:port], or 0 if that is no address.
static const char*
scheme_for_host(const char* host, size_t len) {
  if(host[0] == '[') {  // IPv6
    return memchr(host, ']', len) ? "http://" : 0;
  }

  const char* colon = (const char*)memchr(host, ':', len);
  size_t name_len = colon ? (size_t)(colon - host) : len;
  if(colon) {
    size_t port_len = len - name_len - 1;
    if(!port_len || port_len > 5) {
      return 0;
    }
    for(size_t i = name_len + 1; i < len; i++) {
      if(!is_digit(host[i])) {
        return 0;
      }
    }
  }
  if(!name_len) {
    return 0;
  }
  if(name_len == 9 && !strncasecmp(host, "localhost", 9)) {
    return "http://";
  }
  if(is_ipv4(host, name_len)) {
    return "http://";
  }

  // Labels of letters, digits and -, none empty; the last one a TLD.
  size_t label = 0;
  int dots = 0;
  for(size_t i = 0; i < name_len; i++) {
    unsigned char c = host[i];
    if(c == '.') {
      if(i == label) {
        return 0;
      }
      dots++;
      label = i + 1;
    } else if(!is_alpha(c) && !is_digit(c) && c != '-' && c < 0x80) {
      return 0;
    }
  }
  if(!dots) {
    return colon ? "http://" : 0;  // a machine on the network, "nas:8080"
  }
  return is_tld(host + label, name_len - label) ? "https://" : 0;
}


// Where searches go now (address_set_search()).
static const char* search_url = ADDRESS_SEARCH_URL;


void
address_set_search(const char* prefix) {
  search_url = prefix && *prefix ? prefix : ADDRESS_SEARCH_URL;
}


address_kind_t
address_to_url(const char* typed, char* url, size_t size) {
  static const char hex[] = "0123456789ABCDEF";
  size_t len;
  size_t n;

  while(is_space(*typed)) {
    typed++;
  }
  len = strlen(typed);
  while(len && is_space(typed[len - 1])) {
    len--;
  }
  if(!len) {
    url[0] = 0;
    return ADDRESS_NONE;
  }

  if(!memchr(typed, ' ', len)) {
    // A scheme: a letter, then letters, digits, + - ., then ':'.
    size_t i = 0;
    if(is_alpha(typed[0])) {
      for(i = 1; i < len && (is_alpha(typed[i]) || is_digit(typed[i])
                             || strchr("+-.", typed[i])); i++) {
      }
    }
    if(i && i < len && typed[i] == ':') {
      for(size_t k = 0; k < sizeof(schemes) / sizeof(*schemes); k++) {
        if(strlen(schemes[k]) == i && !strncasecmp(typed, schemes[k], i)) {
          snprintf(url, size, "%.*s", (int)len, typed);
          return ADDRESS_URL;
        }
      }
    }

    // No scheme: the host is what comes before / ? or #.
    size_t host_len = strcspn(typed, "/?#");
    if(host_len > len) {
      host_len = len;
    }
    const char* scheme = scheme_for_host(typed, host_len);
    if(scheme) {
      n = (size_t)snprintf(url, size, "%s", scheme);
      for(i = 0; i < host_len && n + 1 < size; i++) {  // lower case, as Epiphany
        unsigned char c = typed[i];
        url[n++] = (char)(c >= 'A' && c <= 'Z' ? c + 'a' - 'A' : c);
      }
      if(n < size) {
        snprintf(url + n, size - n, "%.*s", (int)(len - host_len),
                 typed + host_len);
      }
      return ADDRESS_URL;
    }
  }

  // Words: a search, encoded as a form would (space as +).
  n = (size_t)snprintf(url, size, "%s", search_url);
  for(size_t i = 0; i < len && n + 4 < size; i++) {
    unsigned char c = typed[i];
    if(is_alpha(c) || is_digit(c) || c == '-' || c == '_' || c == '.'
       || c == '~') {
      url[n++] = (char)c;
    } else if(c == ' ') {
      url[n++] = '+';
    } else {
      url[n++] = '%';
      url[n++] = hex[c >> 4];
      url[n++] = hex[c & 15];
    }
  }
  url[n] = 0;
  return ADDRESS_SEARCH;
}


static int
hex_value(unsigned char c) {
  return is_digit(c) ? c - '0'
       : c >= 'a' && c <= 'f' ? c - 'a' + 10
       : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}


void
address_decode(const char* in, char* out, size_t size) {
  size_t n = 0;

  while(*in && n + 1 < size) {
    int high;
    int low;
    if(in[0] == '%' && (high=hex_value(in[1])) >= 0
       && (low=hex_value(in[2])) >= 0) {
      out[n++] = (char)(high << 4 | low);
      in += 3;
    } else {
      out[n++] = *in++;
    }
  }
  out[n] = 0;
}
