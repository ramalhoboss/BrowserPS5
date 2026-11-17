/* Copyright (C) 2026 BrowserPS5 contributors. GPL-3.0-or-later.

   The phone: a small HTTP server on the local network, inside the browser
   (docs/07, "Telemóvel"). A phone opens http://<console>:<port>/ (the QR
   code on the TV), types the PIN the TV shows and gets a token. With it, it
   opens pages on the TV, types into the field in focus, presses keys,
   sends files and sees the console's downloads (cancel, try again, open on
   the TV, extract a ZIP; docs/17) and files (see, bring to the phone, open
   on the TV, remove; docs/18). Only addresses of a local network may talk to it, a PIN
   works for one phone, a few minutes and PHONE_TRIES wrong tries, and the
   tokens live only in memory: the browser forgets every phone when it ends.

   No cookies: the token goes in an X-Token header, which a page of another
   site cannot send here without asking first (CORS), and this server never
   answers that question. Tested on the PC by phone_test.c (make test). */

#ifndef PHONE_H
#define PHONE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PHONE_PORT 8090          // the first one tried, then up to 4 more
#define PHONE_PIN_SECONDS 300    // how long a PIN works after it is shown
#define PHONE_TRIES 5            // wrong PINs before it stops working
#define PHONE_MAX 4              // phones remembered at once
#define PHONE_TEXT_MAX 4096      // an address or a text, in bytes
#define PHONE_DOWNLOADS_MAX (64 * 1024)  // the downloads' JSON, in bytes
#define PHONE_PLACES 12          // folders a phone may see, and private ones
#define PHONE_SENDS 4            // files going to phones at once
#define PHONE_TICKETS 16         // files a phone may fetch without its token
#define PHONE_TICKET_SECONDS 600 // since the ticket was last used
#define PHONE_GAME_MAX (128 * 1024)  // a game's card (its notes and pages), in bytes

// A folder a phone may see (with what is in it), or one it never may.
typedef struct phone_place {
  char kind[12];    // "downloads", "media", "usb", "ext"; "private": never
  char path[1024];
} phone_place_t;

// What a phone asks. All are called on the server's thread, one at a time:
// they copy what they need and hand it to the app's main thread.
typedef struct phone_calls {
  void (*open)(const char* text, void* ctx);              // an address or words
  void (*type)(const char* text, int enter, void* ctx);   // into the focused field
  void (*key)(const char* name, void* ctx);               // phone_key_known()
  // Where a file goes: the whole path, new, into path. 0, or a PHONE_NO_*.
  int (*file_path)(const char* name, const char* where, long long size,
                   char* path, size_t path_size, void* ctx);
  // The file is whole (path) or was given up (path is 0).
  void (*file_done)(const char* path, long long size, void* ctx);
  void (*paired)(int count, void* ctx);                   // a phone came or went
  // What the TV shows, as a JSON object; its length (0: "{}").
  size_t (*state)(char* json, size_t size, void* ctx);
  // The downloads, as {"downloads": [...]} (Definições, Transferências);
  // its length (0: none).
  size_t (*downloads)(char* json, size_t size, void* ctx);
  // Something done to one (id): phone_download_action_known()'s.
  void (*download)(const char* action, int id, void* ctx);
  // The folders a phone may see, and the private ones; how many.
  int (*places)(phone_place_t* places, int max, void* ctx);
  // A video, sound or picture (its real path) to open on the TV.
  void (*show)(const char* path, void* ctx);
  // The first thing on each thread that sends a file to a phone.
  void (*sending)(void* ctx);
  void* ctx;
  // (2026-11-16, BrowserPS5 RN; after ctx, so a list of the ones above leaves them 0: the app of before has none,
  // and its phone page keeps the card hidden.) The card "O teu jogo": the games of the start page's row, and for
  // one of them (id, or "" for the first) its notes and pages kept, as a JSON object; its length (0: none).
  size_t (*game)(const char* id, char* json, size_t size, void* ctx);
  // Something written to a game (phone_game_action_known()): "note" (body: the notes, "" takes them away), "keep"
  // (body: "address<TAB>title", or "" for the page on the TV), "unpin" (body: the address).
  void (*game_do)(const char* action, const char* id, const char* body, void* ctx);
  // (2026-11-16, RN) The Discord webhook the game page's captures go to (share.h): {"set": 0|1, "where": "<its start
  // only, never the token>"}; its length (0: none).
  size_t (*discord)(char* json, size_t size, void* ctx);
  // A webhook's address to keep (the app checks it: 1 kept, 0 not one), or "" to forget it (1).
  int (*discord_set)(const char* url, void* ctx);
} phone_calls_t;

#define PHONE_HOOK_MAX 512  // a webhook's address the phone gives, at most (bytes, with its 0)

#define PHONE_NO_ROOM  -1   // not enough free space
#define PHONE_NO_USB   -2   // no USB drive
#define PHONE_NO_PLACE -3   // the folder cannot be made

// Starts the server; the port, or -1. page is the phone's page (phone.html).
int phone_start(const phone_calls_t* calls, const char* page);
// Stops it without waiting (the thread ends at its next request).
void phone_stop(void);
int phone_port(void);

// A new PIN of 6 digits (into pin, 7 bytes), for PHONE_PIN_SECONDS.
void phone_pin_open(char* pin);
void phone_pin_close(void);
int phone_pin_seconds(void);   // left; 0 when there is none
int phone_count(void);         // phones that have a token now
void phone_forget(void);       // all of them

// The console's address on the local network, as text; 0, or -1.
int phone_local_address(char* ip, size_t size);
// Whether an IPv4 address (host order) is of a local network.
int phone_address_allowed(unsigned long address);
// Whether a key name is one the phone may press.
int phone_key_known(const char* name);
// Whether it is something the phone may do to a download: "cancel",
// "retry", "open" (on the TV) or "unzip".
int phone_download_action_known(const char* name);
// Whether it is something the phone may write to a game: "note", "keep" or "unpin".
int phone_game_action_known(const char* name);
// Whether it is a game's id as the console has them: 4 capitals and 5 digits (PPSA01341, CUSA01765).
int phone_game_id_ok(const char* id);

#ifdef __cplusplus
}
#endif

#endif
