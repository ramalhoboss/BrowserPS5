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

// What remote.c does beyond hello_page's display, input and ime: the pause
// (link.h, LINK_AWAY and LINK_ENDED; docs/03-video-e-gpu.md, "Pausa").

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// away: 1 when the system is over the app (its menu or home screen), 0 when
// the app is back. test: the title faked it (/app0/away_test). Called on
// remote.c's reader thread.
typedef void (*remote_away_t)(int away, int test);

void remote_on_away(remote_away_t handler);

// Called once, on the reader thread, when the title closes the link (closed
// from the system, or gone). display_show() also says so, but during a
// pause nothing calls it: no frames come (2026-11-04 16:22).
typedef void (*remote_closed_t)(void);

void remote_on_closed(remote_closed_t handler);

// Tell the title a WebProcess ended, so it frees that process's sound rings
// and, for_away, its GPU client too. 0 or -EPIPE.
int remote_ended(int32_t pid, int for_away);

// The quit (main.cpp stop()), before display_close(): gone when no
// WebProcess is left. Only then does display_close() have the system close
// the title (close_title()): the console went down when the title was
// closed with a GPU client's process still there (2026-11-04 15:53,
// 2026-11-05 16:51). Otherwise the title ends by itself.
void remote_web_gone(int gone);

// The quit, once no WebProcess is left (pid, or 0 when none ran): the title
// lets go of its GPU client, keeps showing the screen and says it may be
// closed (link.h, LINK_ENDED_QUIT, LINK_TITLE_READY). 0 or -EPIPE.
int remote_ended_for_quit(int32_t pid);

// An interface sound (LINK_SOUND_*, link.h) at percent of its loudness: the
// title makes it and mixes it into its sound port. A title from before
// ignores it (link_msg_t keeps its size).
void remote_sound(int which, int percent);

// The DualSense (title/feel.h): a short vibration (motors 0..255, 10..1000
// ms), the adaptive triggers (LINK_TRIGGERS_*, strength 1..8) and the light
// bar (on 0: the system's own). A title from before ignores them.
void remote_feel(int large, int small, int ms);
void remote_triggers(int mode, int strength);
void remote_light(int on, int r, int g, int b);

// The video of the browser (title/capture.h; docs/09-video.md): the title
// records what it shows and plays into the piece name (letters, digits, '-'
// and '_'), for up to seconds (at most 120), until remote_capture(0, ...).
// A title from before ignores it.
void remote_capture(int on, const char* name, int seconds);

// The title's capture (link_title_t): 1 while a piece is being recorded or
// written to its disk, 0 when not, -1 with no title; *done: the pieces it
// has fully written.
int remote_capture_status(unsigned* done);

// (2026-11-14) The largest free piece of the title's direct memory, in MiB; -1 with no title, or one
// that does not say (link_title_t, direct_free_mib).
int remote_title_direct_free(void);

// (2026-11-14) 1 when the title has the system keyboard's library loaded and bound (link_title_t, ime_ready:
// title/sysime.c), 0 when it has not, -1 with no title.
int remote_title_ime_ready(void);

// (2026-11-15, BrowserPS5 RN) Is the browser's title still running? (What the title says of itself.) A game is
// never asked for from the browser's processes: twice the console went down when it was (2026-11-16 14:48 and
// 14:59); the game hub's "Play" goes through ../play_after (main.cpp, game_play()).
int remote_title_running(void);

#ifdef __cplusplus
}
#endif
