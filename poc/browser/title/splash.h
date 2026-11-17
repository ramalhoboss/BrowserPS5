/* Copyright (C) 2026 BrowserPS5 contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 3, or (at your option) any
 * later version.
 *
 * 2026-11-09: the screen while the browser starts. As NK's (its
 * shell's native_splash_draw): a neon comet on a tilted orbit round a
 * wireframe globe in the icon's colours, "BrowserPS5 WK" under it and a
 * line that fills as the browser comes. Drawn by the title with PlutoVG
 * until the UIProcess's first picture.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// The font
// and a screen of the ground's colour. 0: ready.
int splash_open(int width, int height);

// The screen with the starting picture at t seconds and the line at
// progress (0..1): width x height, a row after the other. 0 when not open.
const uint32_t *splash_frame(double t, double progress);

void splash_close(void);

#ifdef __cplusplus
}
#endif
