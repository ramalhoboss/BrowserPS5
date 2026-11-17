/* Copyright (C) 2026 BrowserPS5 contributors. GPL-3.0-or-later.

   The controller's microphone, for BrowserPS5 RN's voice search: see mic.c. Built with TITLE_MIC=1. */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* A proof, once, when /app0/mic-probe is there: opens the audio input for the user, reads a few seconds, says
   in the log what came and writes it to /app0/mic-probe.wav. */
void mic_probe(int user);

#ifdef __cplusplus
}
#endif
