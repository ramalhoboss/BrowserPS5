/* Copyright (C) 2026 BrowserPS5 contributors. GPL-3.0-or-later. */

// The start page's "Play" in BrowserPS5 RN (2026-11-16): what the browser leaves for play_after.elf (main.c)
// before it leaves. One line, "<title id> <pid of the browser's app> <pid of its launcher> <en|pt>", written by
// main.cpp's game_play() just before it sends play_after.elf to the payload loader; play_after removes it as it
// reads it. Under DATA_DIR of RN (main.cpp).

#pragma once

#define PLAY_AFTER_ASK "/user/homebrew/browserps5-rn/data/play-next.txt"
#define PLAY_AFTER_LOG "/user/homebrew/browserps5-rn/data/play-log.txt"
