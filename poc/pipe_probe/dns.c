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

// dns_probe (2026-11-17): how long the console takes to resolve a few names at once, as a page's start does.
//
// The start page finished loading 0.4-1.0 s after its first bytes in the morning's runs; in the evening's, four
// in ten took 1.5 s or 5.5 s more, the same two values every time, with the console restarted too. A page that
// starts asks for several sites' icons at the same moment: several names resolved at once. This asks for
// DNS_NAMES names at once (a thread each, as libcurl's resolver has), DNS_ROUNDS times, DNS_PAUSE_MS apart, and
// says how long each took: a query that got no answer shows as the resolver's wait before it asks again.
//
// It reads and writes nothing on the console; the threads only wait for the resolver.

#include <netdb.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/socket.h>

#define POC_NAME "dns_probe"
#include "poc_log.h"

#define DNS_ROUNDS   6
#define DNS_PAUSE_MS 2500

static const char* const names[] = {"www.google.com", "www.youtube.com", "fmhy.net", "github.com",
                                    "en.wikipedia.org", "www.reddit.com"};
#define DNS_NAMES (sizeof(names) / sizeof(names[0]))

typedef struct lookup {
  const char* name;
  double took_ms;
  int error;
} lookup_t;

static double
now_ms(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static void*
lookup_thread(void* arg) {
  lookup_t* l = (lookup_t*)arg;
  struct addrinfo hints, *res = 0;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  const double began = now_ms();
  l->error = getaddrinfo(l->name, 0, &hints, &res);
  l->took_ms = now_ms() - began;
  if(res) {
    freeaddrinfo(res);
  }
  return 0;
}

int
main(void) {
  int slow = 0, failed = 0, all = 0;
  double worst = 0;

  for(int round = 0; round < DNS_ROUNDS; round++) {
    lookup_t l[DNS_NAMES];
    pthread_t t[DNS_NAMES];
    char line[512];
    int at = 0;

    memset(l, 0, sizeof(l));
    for(unsigned i = 0; i < DNS_NAMES; i++) {
      l[i].name = names[i];
      if(pthread_create(&t[i], 0, lookup_thread, &l[i])) {
        l[i].error = -1;
        t[i] = 0;
      }
    }
    for(unsigned i = 0; i < DNS_NAMES; i++) {
      if(t[i]) {
        pthread_join(t[i], 0);
      }
      all++;
      slow += l[i].took_ms >= 1000;
      failed += l[i].error != 0;
      worst = l[i].took_ms > worst ? l[i].took_ms : worst;
      at += snprintf(line + at, sizeof(line) - (size_t)at, "%s%s %.0f ms%s", i ? ", " : "", l[i].name, l[i].took_ms,
                     l[i].error ? " (FAILED)" : "");
    }
    POC_LOG("round %d: %s", round + 1, line);
    usleep(DNS_PAUSE_MS * 1000);
  }
  POC_RESULT(!slow && !failed, "%d lookups, %d at once: %d took a second or more, %d failed, the longest %.0f ms", all,
             (int)DNS_NAMES, slow, failed, worst);
  return 0;
}
