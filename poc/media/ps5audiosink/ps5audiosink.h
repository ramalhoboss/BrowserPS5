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

// The PS5's sound as a GStreamer element (ps5audiosink.c). WebKit calls
// this from registerWebKitGStreamerElements(), gst_probe from main().

#pragma once

#include <glib.h>

G_BEGIN_DECLS

// Registers "ps5audiosink" (after gst_init), and "westerossink" with
// browserps5_video_sink_register(). Links with -lSceAudioOut and
// gstreamer-base.
gboolean browserps5_audio_sink_register(void);

// Registers "westerossink" (ps5videosink.c): the video, still compressed,
// to the browser's title (docs/08-4k.md, D2).
gboolean browserps5_video_sink_register(void);

G_END_DECLS
