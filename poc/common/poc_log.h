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

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

// The SDK's C headers have no extern "C" of their own (for C++ users such
// as poc/hello_page).
#ifdef __cplusplus
extern "C" {
#endif
#include <ps5/klog.h>
#ifdef __cplusplus
}
#endif


/**
 * Every line goes to stdout (the socket of whoever sent the payload) and to
 * klog (readable with a klog client on port 3232, or in etaHEN's log), so a
 * result survives even if the TCP connection to the PC drops.
 **/
// The arguments are evaluated twice (stdout, then the kernel log): no calls
// with side effects in them. hello_page typed every text twice that way.
#define POC_LOG(fmt, ...) do {						\
    printf("[%s] " fmt "\n", POC_NAME, ##__VA_ARGS__);			\
    fflush(stdout);							\
    klog_printf("[%s] " fmt "\n", POC_NAME, ##__VA_ARGS__);		\
  } while(0)

#define POC_PERROR(what) \
  POC_LOG("%s:%d: %s: %s", __FILE__, __LINE__, what, strerror(errno))

#define POC_RESULT(ok, fmt, ...) \
  POC_LOG("%s " fmt, (ok) ? "PASS" : "FAIL", ##__VA_ARGS__)

/**
 * Like POC_LOG, and also pops up a notification on the PS5, for when the
 * payload was sent with a tool that drops its output.
 **/
#define POC_NOTIFY(fmt, ...) do {					\
    POC_LOG(fmt, ##__VA_ARGS__);					\
    poc_notify("[%s] " fmt, POC_NAME, ##__VA_ARGS__);			\
  } while(0)


typedef struct poc_notify_request {
  char useless1[45];
  char message[3075];
} poc_notify_request_t;

#ifdef __cplusplus
extern "C"
#endif
int sceKernelSendNotificationRequest(int, poc_notify_request_t*, size_t, int);


__attribute__((format(printf, 1, 2))) static inline void
poc_notify(const char* fmt, ...) {
  poc_notify_request_t req;
  va_list args;

  memset(&req, 0, sizeof(req));
  va_start(args, fmt);
  vsnprintf(req.message, sizeof(req.message), fmt, args);
  va_end(args);
  sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}


static inline double
poc_now_ms(void) {
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}


static inline void
poc_hexdump(const void* data, size_t size) {
  const unsigned char* p = (const unsigned char*)data;
  char line[16*3 + 1];

  for(size_t off=0; off<size; off+=16) {
    size_t n = size - off < 16 ? size - off : 16;

    for(size_t i=0; i<n; i++) {
      snprintf(&line[i*3], 4, "%02x ", p[off + i]);
    }
    POC_LOG("  %04zx: %s", off, line);
  }
}
