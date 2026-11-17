/* Copyright (C) 2026 BrowserPS5 contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING. If not, see
<http://www.gnu.org/licenses/>.  */

// BrowserPS5 RN, 2026-11-14: the games on this console, as the browser's start page shows them. A browser on a
// PC does not know what its user plays; this one reads it where the PS5 itself keeps it, and ONLY READS:
//
//   /user/appmeta/<title id>/param.json, param.sfo the game's name and kind (xps5/game.c, from XPS5)
//   /user/appmeta/<title id>/icon0.png, pic0.png...  its pictures: only whether they are there
//   /user/home/<user>/savedata_prospero/<title id>, /user/home/<user>/savedata/<title id>,
//   /user/av_contents/{photo,video}/NPXS40087/<title id>
//                                                     only the TIMES of what is in them: when it was last played
//
//   /user/appmeta/<title id>/trophy2/npbind.dat, /user/trophy2/nobackup/conf/<set>/TROPHY.UCP,
//   /user/home/<user>/trophy2/nobackup/data/<set>/TRPTITLE.DAT
//                                                     the trophies of a PS5 game and which the user has (xps5/trophy.c)
//
// and asks the system which of them is open now, behind the browser (sceLncUtilGetAppId()). No file of a save or
// of a capture is opened here.

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GAMES_MOST  8
#define GAMES_NOTE  400

typedef struct game_info {
  char      id[16];        // "PPSA01325"
  char      name[128];
  char      icon[96];      // its icon0.png's path, "" when it has none
  char      picture[96];   // its background's (pic0.png, else pic1.png), "" when it has none
  long long played;        // Unix time of the newest trace of it being played, 0 when there is none
  int       open;          // it is open now, behind the browser
  int       ps4;
  unsigned  colour;        // 0xRRGGBB of its icon, taken as a whole (the first GAMES_COLOURS ones), 0 when none
  int       trophies;      // how many trophies its set has (the first GAMES_COLOURS ones), 0 when it is not known
  int       trophies_have; // how many of them the user has
  char      version[24];   // the version of it the console has ("01.026.000"; a PS4 one's "01.27"), "" when not said
} game_info_t;

#define GAMES_COLOURS 4

/**
 * the games of this console, the one that is open first, then the ones played last; at most max. lang: the
 * language their names are wanted in, as param.json names it ("pt-PT"). note: what was found, for the log
 * (counts only). Returns how many, 0 when there are none or the folders cannot be read.
 **/
int games_recent(game_info_t* out, int max, const char* lang, char* note, size_t note_size);

#define GAMES_LIBRARY 256

/**
 * 2026-11-16 (docs/32, "the whole library in your games"): every game of this console, in the same order as
 * games_recent() (the open one, then the ones played last, then the ones installed last), with the same details;
 * the icon's colour only for the first GAMES_COLOURS, the trophies for every PS5 game. At most max (GAMES_LIBRARY
 * is enough).
 **/
int games_library(game_info_t* out, int max, const char* lang, char* note, size_t note_size);

/**
 * the name of the game with the title id, in the language lang; 1, or 0 when it is not a game with a name
 **/
int games_name(const char* id, const char* lang, char* name, size_t size);

// One trophy of a game's set (games_trophies()).
typedef struct game_trophy {
  int  id;
  int  grade;        // 0 bronze, 1 silver, 2 gold, 3 platinum
  int  have;         // the user has it
  int  hidden;       // the set says it is a hidden one
  char name[128];
  char detail[256];  // how it is won, as the set says it
} game_trophy_t;

#define GAME_TROPHIES 256

/**
 * the trophies of a PS5 game's set, in the set's order, with their names in the language lang and which of them
 * the user has (the record written last among the users); how many, 0 when the game has no set on this console
 * or it does not read
 **/
int games_trophies(const char* id, const char* lang, game_trophy_t* out, int max);

#ifdef __cplusplus
}
#endif
