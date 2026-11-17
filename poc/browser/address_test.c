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

// address.c on the PC: make test.

#include <stdio.h>
#include <string.h>

#include "address.h"

#define SEARCH ADDRESS_SEARCH_URL

static const struct {
  const char* typed;
  const char* url;
} cases[] = {
  {"lisboa", SEARCH "lisboa"},
  {"rua augusta lisboa", SEARCH "rua+augusta+lisboa"},
  {"c++ tutorial", SEARCH "c%2B%2B+tutorial"},
  {"São Paulo", SEARCH "S%C3%A3o+Paulo"},
  {"1.5", SEARCH "1.5"},
  {"wiki/Lisboa", SEARCH "wiki%2FLisboa"},
  {"mailto:a@b.pt", SEARCH "mailto%3Aa%40b.pt"},
  {"javascript:alert(1)", SEARCH "javascript%3Aalert%281%29"},
  {"rtp.pt", "https://rtp.pt"},
  {"  RTP.pt/Noticias  ", "https://rtp.pt/Noticias"},
  {"pt.wikipedia.org/wiki/São_Paulo", "https://pt.wikipedia.org/wiki/São_Paulo"},
  {"https://www.wikipedia.org/", "https://www.wikipedia.org/"},
  {"HTTP://Example.com", "HTTP://Example.com"},
  {"about:blank", "about:blank"},
  {"localhost:8080/x", "http://localhost:8080/x"},
  {"192.168.1.51:9021", "http://192.168.1.51:9021"},
  {"[::1]:8080", "http://[::1]:8080"},
  {"nas:5000", "http://nas:5000"},
  {"example.com:8443", "https://example.com:8443"},
  {"exemplo.рф", "https://exemplo.рф"},
  {"example..com", SEARCH "example..com"},
};


int
main(void) {
  char url[1024];
  char decoded[256];
  int failed = 0;

  for(size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
    address_to_url(cases[i].typed, url, sizeof(url));
    if(strcmp(url, cases[i].url)) {
      printf("FAIL \"%s\": \"%s\", expected \"%s\"\n", cases[i].typed, url,
             cases[i].url);
      failed++;
    }
  }
  if(address_to_url(" \t", url, sizeof(url)) != ADDRESS_NONE) {
    printf("FAIL blank: not ADDRESS_NONE\n");
    failed++;
  }
  address_decode("S%C3%A3o%20Paulo%2B%zz", decoded, sizeof(decoded));
  if(strcmp(decoded, "São Paulo+%zz")) {
    printf("FAIL address_decode: \"%s\"\n", decoded);
    failed++;
  }
  printf("%s: %d failure(s)\n", failed ? "FAIL" : "PASS", failed);
  return failed != 0;
}
