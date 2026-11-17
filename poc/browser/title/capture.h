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

/*
 * The browser's screen and sound, recorded to the console's disk for a video
 * of it (docs/09-video.md): what the title shows, the page with its video,
 * as YUV 4:2:0 at 30 frames a second, and the sound it plays, mixed, as
 * 48 kHz stereo. A capture is a folder under CAPTURE_DIR:
 *   video.yuv   1920x1080 I420 frames, BT.709 limited range: the frames the
 *               screen gave, at most one every 1/30 s of the wall clock;
 *   index.txt   a line per frame of video.yuv: the 1/30 s place it starts
 *               at (it fills the places up to the next frame's; the PC
 *               expands them, poc/video_edit/expand.c);
 *   sound.s16   signed 16-bit little-endian, 2 channels, 48000 Hz, from the
 *               same moment;
 *   info.txt    sizes, counts (places: the piece's 1/30 s steps in all),
 *               what was lost, the disk's speed.
 * The screen's thread only copies a frame into a free stage (about 1 ms) and
 * the sound's only copies its block. A thread converts each frame into a
 * RAM ring (direct memory) and another writes the ring out as fast as the
 * disk goes; both sleep when there is nothing to do, and no caller ever
 * waits for the disk.
 */
#ifndef BROWSERPS5_TITLE_CAPTURE_H
#define BROWSERPS5_TITLE_CAPTURE_H

#include <stdint.h>

#ifndef CAPTURE_DIR /* a test on the PC sets its own */
#define CAPTURE_DIR "/data/homebrew/PPSA97020/capture"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Starts a capture into CAPTURE_DIR/<name> (letters, digits, '-' and '_')
 * that ends by itself after seconds (at most 120). 0, or -1 with the reason
 * in the title's report (the piece before is still being written, no room
 * on the disk...). */
int capture_begin(const char *name, unsigned seconds);

/* Ends the capture running, if any, and returns at once: the files are
 * written after (capture_busy()). */
void capture_end(void);

/* Recording. */
int capture_running(void);

/* Recording, or writing what was recorded: no new piece can start. */
int capture_busy(void);

/* The pieces fully written since the title started. */
unsigned capture_done(void);

/* The screen's thread, every flip: the frame as shown (0xAARRGGBB pixels,
 * stride in pixels). Ends the capture when its time is up. */
void capture_frame(const uint32_t *pixels, int width, int height, int stride);

/* The sound's thread, every block it plays: frames x 2 samples. */
void capture_sound(const int16_t *samples, unsigned frames);

/* Where the report goes (title.cpp's report()). */
void capture_set_report(void (*say)(const char *line));

#ifdef __cplusplus
}
#endif

#endif
