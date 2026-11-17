/*   Copyright (C) 2026 BrowserPS5 contributors
 *
 * This file is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * How fast a download goes and how long it has left (Definições,
 * Transferências, and the dialog's bar; docs/15-transferencias.md). WebKit
 * says only how many bytes are in so far (didWriteData): this turns that
 * into a speed that does not jump at each piece, and notices when nothing
 * comes any more.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* A speed sample is taken once this much time has gone by (ms). */
#define DL_RATE_SAMPLE_MS 1000
/* Nothing in for this long (ms): the download is stalled, speed 0. */
#define DL_RATE_STALL_MS 4000
/* The longest time left it says (s): 99 hours. */
#define DL_RATE_LEFT_MAX (99 * 3600)

typedef struct dl_rate {
  long long sample_bytes;  // the bytes in at the start of this sample
  double sample_ms;        // when this sample started
  double last_ms;          // when bytes last came in
  long long last_bytes;
  double bps;              // smoothed bytes per second; 0 until known
  int started;
} dl_rate_t;

/* The download is at bytes (0 for a new one, more for a retry) at now_ms. */
void dl_rate_start(dl_rate_t* r, long long bytes, double now_ms);

/* It is at bytes now (the total so far, as WebKit says it). */
void dl_rate_add(dl_rate_t* r, long long bytes, double now_ms);

/* Bytes per second, 0 while unknown or stalled. */
long long dl_rate_speed(const dl_rate_t* r, double now_ms);

/* Seconds left for total (received so far), at the speed; -1 when it is
 * not known (no total, no speed, stalled). At most DL_RATE_LEFT_MAX. */
long long dl_rate_left(const dl_rate_t* r, long long received, long long total, double now_ms);

#ifdef __cplusplus
}
#endif
