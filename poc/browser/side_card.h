/*   Copyright (C) 2026 BrowserPS5 contributors
 *
 * This file is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * "Abrir ao lado do jogo" (docs/10-achados-do-desktop.md, 1): the page in
 * the system's browser card, the one browser the PS5 docks next to a game
 * (split screen). BrowserPS5 is a title and the system docks only its own
 * cards, so this hands the address over: a notification with a button
 * whose deep link opens the card on it, as TwitterPS5 (XPS5) does
 * (src/toast.c). The card is the system's engine, with its own logins and
 * cookies; the button works while the notification is on the screen.
 */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The longest address handed over (the deep link stays under ~4 KiB). */
#define SIDE_CARD_URL_MAX 1024

/* 2026-11-13: the name the card's browser gives the sites, for the
 * links made from now on ("" or 0: its own, a desktop's). The card is the
 * system's own engine, slower than ours, and narrow: under a phone's name
 * the sites send their phone pages, lighter and made for that width. */
void side_card_user_agent(const char* user_agent);

/* The deep link that opens the card on url, into out (size bytes): every
 * JSON value percent-encoded (the ShellUI refuses links with other
 * characters than [:/?#[]@!$&'()*+,;=-._~%A-z0-9]); the card's header
 * stays (split screen needs it). Returns its length, or -1 when url is
 * empty, too long or the link does not fit. */
int side_card_link(const char* url, char* out, size_t size);

/* The notification's JSON (sceNotificationSend's), into out: title and
 * text in the toast, button the action's name, id its own number (a new
 * one each time, or the system takes it for the one already listed).
 * Returns its length, or -1. */
int side_card_toast(const char* url, const char* title, const char* text, const char* button,
                    unsigned id, char* out, size_t size);

/* The same notification for any deep link (as it is, already a link). */
int side_card_toast_link(const char* link, const char* title, const char* text, const char* button,
                         unsigned id, char* out, size_t size);

/* The deep link to one of the PS5's own settings pages, by its name:
 * "network", "video", "storage" or "controller" (psdevwiki's param.json
 * page, docs/10-achados-do-desktop.md 7). 0 for any other name: these are
 * the only links a page can have the browser post. */
const char* side_card_settings_link(const char* name);

/* Posts a notification whose button opens link (side_card_settings_link()'s). */
int side_card_post_link(const char* link, const char* text, const char* button);

/* The same with icon: a picture's https address, or one of the PS5's own
 * ("messages", "download", "favorite", "ps_user"; 0 = "messages"). */
int side_card_toast_icon(const char* link, const char* icon, const char* title, const char* text,
                         const char* button, unsigned id, char* out, size_t size);

/* Posts that one: a site's notification (BrowserPS5 RN), title and text its own. */
int side_card_post_message(const char* link, const char* icon, const char* title, const char* text,
                           const char* button);

/* Posts it (on the console: sceNotificationSend, as the system's user),
 * with text and button in the browser's language. Returns 0, -1 for an
 * address that cannot go, or the system's error. */
int side_card_post(const char* url, const char* text, const char* button);

#ifdef __cplusplus
}
#endif
