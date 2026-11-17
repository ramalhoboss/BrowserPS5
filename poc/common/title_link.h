/*   Copyright (C) 2026 BrowserPS5 contributors
 *
 * This file is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * 2026-11-12 (BrowserPS5 RN). A title of
 * the media category (param.json applicationCategoryType 65536) may not
 * open a port: bind() is EACCES there, on 127.0.0.1 too (docs/24, 2026-11-08).
 * The browser's title has two services on 127.0.0.1 for the browser's own
 * payload processes: the GPU's owner (9040) and the WebProcess's host (9060).
 *
 * So, when the title cannot listen, it calls instead:
 *   - the browser's menu (a payload: it may listen) listens on the service's
 *     port itself, for the clients as they are (libvulkan.so.1, the engine's
 *     process launcher: nothing of theirs changes), and on the port + 100
 *     for the title (title_link_relay_start());
 *   - the title keeps one connection to the port + 100 ahead of time
 *     (title_link_dial()); when a client comes, the menu sends the title one
 *     byte on it and from then on passes the bytes between the two, both ways.
 * A title that can listen (category 0, as before) is there first: the menu's
 * bind() of the same port fails (in use) and nothing of this runs.
 *
 * Blocking calls only (poll, accept, read): no thread spins.
 */
#pragma once

#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#define TITLE_LINK_OFFSET 100
#define TITLE_LINK_HELLO 'T'

static inline int
title_link_listen(int port) {
  const int s = socket(AF_INET, SOCK_STREAM, 0);
  const int yes = 1;
  struct sockaddr_in a;

  if(s < 0) {
    return -1;
  }
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  memset(&a, 0, sizeof(a));
  a.sin_family = AF_INET;
  a.sin_port = htons((uint16_t)port);
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if(bind(s, (struct sockaddr*)&a, sizeof(a)) || listen(s, 8)) {
    const int err = errno;
    close(s);
    errno = err;
    return -1;
  }
  return s;
}

/* The title's side: a connection a client has taken, for the service of
 * port. Connects to the menu's port + 100 (again every 200 ms while nobody
 * listens there) and waits on it for the menu's byte: a client came. */
static inline int
title_link_dial(int port) {
  for(;;) {
    const int s = socket(AF_INET, SOCK_STREAM, 0);
    const int yes = 1;
    struct sockaddr_in a;
    char hello = 0;

    if(s < 0) {
      usleep(200000);
      continue;
    }
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)(port + TITLE_LINK_OFFSET));
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if(connect(s, (struct sockaddr*)&a, sizeof(a))) {
      close(s);
      usleep(200000);
      continue;
    }
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
    if(read(s, &hello, 1) == 1 && hello == TITLE_LINK_HELLO) {  /* sleeps here until a client comes */
      return s;
    }
    close(s);  /* the menu went away: again */
    usleep(200000);
  }
}

/* The menu's side. */
typedef struct title_link_pair {
  int a, b;
} title_link_pair_t;

static inline void*
title_link_pipe(void* arg) {
  title_link_pair_t* pair = (title_link_pair_t*)arg;
  static const size_t size = 256 * 1024;
  char* buffer = (char*)malloc(size);
  struct pollfd p[2] = {{pair->a, POLLIN, 0}, {pair->b, POLLIN, 0}};

  while(buffer) {
    if(poll(p, 2, -1) < 0) {
      if(errno == EINTR) {
        continue;
      }
      break;
    }
    int ended = 0;
    for(int i = 0; i < 2 && !ended; i++) {
      if(!(p[i].revents & (POLLIN | POLLHUP | POLLERR))) {
        continue;
      }
      const ssize_t n = read(p[i].fd, buffer, size);
      if(n <= 0) {
        ended = 1;
        break;
      }
      for(ssize_t done = 0; done < n;) {
        const ssize_t w = write(p[1 - i].fd, buffer + done, (size_t)(n - done));
        if(w <= 0) {
          ended = 1;
          break;
        }
        done += w;
      }
    }
    if(ended) {
      break;
    }
  }
  free(buffer);
  shutdown(pair->a, SHUT_RDWR);
  shutdown(pair->b, SHUT_RDWR);
  close(pair->a);
  close(pair->b);
  free(pair);
  return 0;
}

typedef struct title_link_service {
  int clients;  /* the listener on the service's port, for its clients */
  int title;    /* the one on the port + 100, where the title calls */
  int port;
} title_link_service_t;

static inline void*
title_link_serve(void* arg) {
  title_link_service_t* service = (title_link_service_t*)arg;
  const int yes = 1;

  for(;;) {
    const int client = accept(service->clients, 0, 0);  /* sleeps until a client comes */
    if(client < 0) {
      if(errno == EINTR || errno == ECONNABORTED) {
        continue;
      }
      break;
    }
    setsockopt(client, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
    /* The title's connection for it: the one waiting, or the next within ten seconds. */
    struct pollfd p = {service->title, POLLIN, 0};
    int title = -1;
    const char hello = TITLE_LINK_HELLO;
    while(title < 0 && poll(&p, 1, 10000) == 1) {
      title = accept(service->title, 0, 0);
      if(title >= 0 && write(title, &hello, 1) != 1) {  /* a connection of a title that is gone: the next */
        close(title);
        title = -1;
      }
    }
    title_link_pair_t* pair = title >= 0 ? (title_link_pair_t*)malloc(sizeof(*pair)) : 0;
    pthread_t thread;
    if(!pair) {
      close(client);
      if(title >= 0) {
        close(title);
      }
      continue;
    }
    setsockopt(title, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
    pair->a = client;
    pair->b = title;
    if(pthread_create(&thread, 0, title_link_pipe, pair)) {
      close(client);
      close(title);
      free(pair);
      continue;
    }
    pthread_detach(thread);
  }
  return 0;
}

/* The menu, before anything of it asks for the title: for each of the
 * title's ports that nobody listens on (the title could not), the two
 * listeners and their thread. Returns how many services it stands in for
 * (0: the title listens itself, as before). */
static inline int
title_link_relay_start(const int* ports, int count) {
  int relayed = 0;

  for(int i = 0; i < count; i++) {
    const int clients = title_link_listen(ports[i]);
    if(clients < 0) {  /* in use: the title has it */
      continue;
    }
    const int title = title_link_listen(ports[i] + TITLE_LINK_OFFSET);
    title_link_service_t* service = title >= 0 ? (title_link_service_t*)malloc(sizeof(*service)) : 0;
    pthread_t thread;
    if(!service) {
      close(clients);
      if(title >= 0) {
        close(title);
      }
      continue;
    }
    service->clients = clients;
    service->title = title;
    service->port = ports[i];
    if(pthread_create(&thread, 0, title_link_serve, service)) {
      close(clients);
      close(title);
      free(service);
      continue;
    }
    pthread_detach(thread);
    relayed++;
  }
  return relayed;
}
