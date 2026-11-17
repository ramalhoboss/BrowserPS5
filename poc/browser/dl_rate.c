/*   Copyright (C) 2026 BrowserPS5 contributors
 *
 * This file is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * A download's speed and time left (dl_rate.h).
 */
#include "dl_rate.h"

#include <string.h>

/* How much a new sample counts against the speed so far. */
#define DL_RATE_WEIGHT 0.3

void
dl_rate_start(dl_rate_t* r, long long bytes, double now_ms) {
  memset(r, 0, sizeof(*r));
  r->sample_bytes = r->last_bytes = bytes;
  r->sample_ms = r->last_ms = now_ms;
  r->started = 1;
}

void
dl_rate_add(dl_rate_t* r, long long bytes, double now_ms) {
  if(!r->started) {
    dl_rate_start(r, bytes, now_ms);
    return;
  }
  if(bytes < r->sample_bytes) {  // WebKit started it over
    dl_rate_start(r, bytes, now_ms);
    return;
  }
  if(bytes > r->last_bytes) {
    // After a stall, the time with nothing in is not part of the speed.
    if(now_ms - r->last_ms > DL_RATE_STALL_MS) {
      r->sample_bytes = r->last_bytes;
      r->sample_ms = r->last_ms = now_ms - DL_RATE_SAMPLE_MS / 4;
    }
    r->last_bytes = bytes;
    r->last_ms = now_ms;
  }
  const double elapsed = now_ms - r->sample_ms;
  if(elapsed >= DL_RATE_SAMPLE_MS) {
    const double bps = (double)(bytes - r->sample_bytes) * 1000.0 / elapsed;
    r->bps = r->bps > 0 ? r->bps * (1 - DL_RATE_WEIGHT) + bps * DL_RATE_WEIGHT : bps;
    r->sample_bytes = bytes;
    r->sample_ms = now_ms;
  }
}

long long
dl_rate_speed(const dl_rate_t* r, double now_ms) {
  if(!r->started || r->bps <= 0 || now_ms - r->last_ms > DL_RATE_STALL_MS) {
    return 0;
  }
  return (long long)(r->bps + 0.5);
}

long long
dl_rate_left(const dl_rate_t* r, long long received, long long total, double now_ms) {
  const long long speed = dl_rate_speed(r, now_ms);
  if(total <= 0 || speed <= 0) {
    return -1;
  }
  if(received >= total) {
    return 0;
  }
  const long long left = (total - received + speed - 1) / speed;
  return left > DL_RATE_LEFT_MAX ? DL_RATE_LEFT_MAX : left;
}
