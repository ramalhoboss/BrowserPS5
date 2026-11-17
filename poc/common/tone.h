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

// tone.h: short beeps through sceAudioOut, to find out by ear which way of
// opening a sound port the console lets through (docs/03-video-e-gpu.md,
// "Som"). On the TV test of 2026-11-04 YouTube's sound was not heard,
// although the port took the samples at 1x. Used by ../audio_tone (a
// payload) and ../browser/title/title.cpp (the foreground title).
//
// Way N plays N beeps, so the person listening can tell the ways apart
// even when the console's notifications come late. Each port is closed
// before the next one opens, and sceAudioOutOutput() blocks until the
// console takes the block, so nothing here spins.

#pragma once

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif
int32_t sceAudioOutInit(void);
int32_t sceAudioOutOpen(int32_t user, int32_t type, int32_t index,
                        uint32_t frames, uint32_t rate, uint32_t format);
int32_t sceAudioOutOutput(int32_t handle, const void* samples);
int32_t sceAudioOutClose(int32_t handle);
int32_t sceAudioOutSetVolume(int32_t handle, int32_t flag, int32_t* volume);
#ifdef __cplusplus
}
#endif

#define TONE_USER_SYSTEM  0xff
#define TONE_PORT_MAIN    0
#define TONE_PORT_BGM     1
#define TONE_FRAMES       256
#define TONE_RATE         48000
#define TONE_S16_STEREO   1
#define TONE_VOLUME_0DB   32768
#define TONE_AMPLITUDE    6000  // of 32767: well below full scale

typedef struct tone_way {
  const char* name;  // for the screen and the log (ASCII)
  int32_t user;      // TONE_USER_SYSTEM or a user id
  int32_t type;      // TONE_PORT_*
  int set_volume;    // set both channels to 0 dB after opening
} tone_way_t;

// notify: also show it on the screen.
typedef void (*tone_say_t)(int notify, const char* text);


/**
 * beeps beeps of 0.3 s at hz through a port opened as way says. Returns
 * what sceAudioOutOpen() returned; *ms is the time the samples took (about
 * 0.5 s per beep plus 0.5 s when the console plays them at 1x).
 **/
static inline int32_t
tone_play(const tone_way_t* way, int beeps, unsigned hz, tone_say_t say,
          double (*now_ms)(void), double* ms) {
  static int16_t block[TONE_FRAMES * 2];
  char line[200];

  *ms = 0;
  int32_t port = sceAudioOutOpen(way->user, way->type, 0, TONE_FRAMES,
                                 TONE_RATE, TONE_S16_STEREO);
  snprintf(line, sizeof(line), "%s: sceAudioOutOpen(user 0x%x, type %d) = 0x%08x",
           way->name, (unsigned)way->user, (int)way->type, (unsigned)port);
  say(0, line);
  if(port < 0) {
    return port;
  }
  if(way->set_volume) {
    int32_t volume[8];
    for(int i = 0; i < 8; i++) {
      volume[i] = TONE_VOLUME_0DB;
    }
    // Flags: bit 0 the left channel, bit 1 the right one.
    int32_t ret = sceAudioOutSetVolume(port, 0x3, volume);
    snprintf(line, sizeof(line), "%s: sceAudioOutSetVolume(0 dB) = 0x%08x",
             way->name, (unsigned)ret);
    say(0, line);
  }

  // 0.3 s on, 0.2 s off per beep, then 0.5 s of silence so the last beep
  // is played before the port closes. A triangle wave (no libm), with 5 ms
  // ramps against clicks.
  const unsigned on = TONE_RATE * 3 / 10, cycle = TONE_RATE / 2;
  const unsigned sounding = (unsigned)beeps * cycle;
  const unsigned total = sounding + TONE_RATE / 2;
  const unsigned period = TONE_RATE / (hz ? hz : 1);
  const unsigned ramp = TONE_RATE / 200;
  unsigned frame = 0;
  double t0 = now_ms();
  while(frame < total) {
    for(unsigned i = 0; i < TONE_FRAMES; i++, frame++) {
      int sample = 0;
      unsigned at = frame % cycle;
      if(frame < sounding && at < on) {
        int v = (int)(4u * TONE_AMPLITUDE * (frame % period) / period);
        if(v > 2 * TONE_AMPLITUDE) {
          v = 4 * TONE_AMPLITUDE - v;
        }
        sample = v - TONE_AMPLITUDE;
        unsigned edge = at < on - at ? at : on - at;
        if(edge < ramp) {
          sample = sample * (int)edge / (int)ramp;
        }
      }
      block[2 * i] = block[2 * i + 1] = (int16_t)sample;
    }
    int32_t ret = sceAudioOutOutput(port, block);
    if(ret < 0) {
      snprintf(line, sizeof(line), "%s: sceAudioOutOutput = 0x%08x", way->name,
               (unsigned)ret);
      say(0, line);
      break;
    }
  }
  *ms = now_ms() - t0;
  int32_t ret = sceAudioOutClose(port);
  snprintf(line, sizeof(line), "%s: %u ms of samples took %.0f ms; sceAudioOutClose = 0x%08x",
           way->name, total * 1000u / TONE_RATE, *ms, (unsigned)ret);
  say(0, line);
  return port;
}
