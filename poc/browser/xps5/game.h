/* From XPS5 (github.com/ramalhoboss/XPS5, GPL-3.0-or-later), the same author's X client for the PS5: the part of
   src/game.h that goes with game.c here, taken on 2026-11-14 for BrowserPS5 RN.

   The game that runs beside the title: its name, from the PS5's own data about it (the param.json of a PS5
   game, the param.sfo of a PS4 one, under /user/appmeta/<title id>/), and a hashtag made of it. */

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GAME_ID   16  // "PPSA01325", with room
#define GAME_NAME 128

/**
 * whether id is a game's title id: four capitals and five digits, not the
 * system's (NPXS), not XPS5's and not this browser's own
 **/
int game_id_ok(const char *id);

/**
 * the name in a PS5 game's param.json (text, len bytes, changed in place):
 * its titleName in the language lang ("pt-PT"), else in its default one,
 * else in English; on one line, cut to fit size; 0, or -1
 **/
int game_name_json(char *text, size_t len, const char *lang, char *name, size_t size);

/**
 * the applicationCategoryType of a PS5 app's param.json (text, len bytes,
 * changed in place): 0 for a game, 65536 for a media app; -1 when it says
 * none or is not JSON
 **/
int game_category_json(char *text, size_t len);

/**
 * the TITLE of a PS4 game's param.sfo; 0, or -1
 **/
int game_name_sfo(const unsigned char *buf, size_t len, char *name, size_t size);

/**
 * name as a hashtag ("ASTRO BOT" gives "#ASTROBOT"): its letters (also the
 * accented Latin ones) and digits; 0, or -1 when no letter is left
 **/
int game_hashtag(const char *name, char *tag, size_t size);

#ifdef __cplusplus
}
#endif
