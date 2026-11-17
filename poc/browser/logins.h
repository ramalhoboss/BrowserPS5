/*   Copyright (C) 2026 BrowserPS5 contributors
 *
 * This file is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * The saved logins (docs/12-contas.md): a site's user name and password,
 * kept for its origin (scheme://host[:port]) after the user said "Guardar"
 * in the dialog, and offered back on the browser's keyboard ("Entrar como
 * ...") in a field of the same origin. Sites where the user said "Nunca"
 * are kept too, with no password.
 *
 * One file of the browser's data, 0600, a line each (fields escaped):
 *   L <tab> origin <tab> user <tab> password <tab> time
 *   N <tab> origin <tab> <tab> <tab> time
 * The passwords are not encrypted: the console has no keystore for a
 * payload; the file stays on the console, never in a log or a page (the
 * settings page gets the origins, user names and times only). All on the
 * main thread.
 */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LOGINS_MAX 512
#define LOGIN_FIELD 256    // origin, user and password: at most 255 bytes
#define LOGINS_OFFERED 3   // user names on the keyboard, at most

/* The origin of an http(s) URL, lower case, without a default port: 0, or
 * -1 for any other kind of address. */
int logins_origin(const char* url, char* out, size_t size);

int logins_load(const char* path);  // the number of entries, -1 if none
int logins_save(const char* path);  // 0: written whole (a new file renamed over it)
void logins_clear(void);            // all forgotten (the tests; "clear")

/* What the dialog does: 1 a new one, 2 a new password for that user, 0 the
 * same was there, -1 not kept (too long, empty password, full). */
int logins_put(const char* origin, const char* user, const char* pass, long now);
int logins_known(const char* origin, const char* user, const char* pass);
int logins_has_user(const char* origin, const char* user);
int logins_set_never(const char* origin, long now);
int logins_never(const char* origin);
/* user 0: the "never" of that origin. 0 when one went. */
int logins_remove(const char* origin, const char* user);

/* The keyboard: how many for origin, their user names as a JSON array (at
 * most LOGINS_OFFERED, the latest first), and the index-th of that array. */
int logins_count(const char* origin);
int logins_users_json(const char* origin, char* out, size_t size);
int logins_get(const char* origin, int index, char* user, size_t user_size, char* pass,
               size_t pass_size);

/* The settings page: [{"origin", "user", "time", "never"}], no password
 * (malloc). */
char* logins_list_json(void);

#ifdef __cplusplus
}
#endif
