/* Copyright (C) 2026 BrowserPS5 contributors. GPL-3.0-or-later. */

// share.c (BrowserPS5 RN, 2026-11-16; docs/32, idea 4: "captures to Discord in one press"): a capture of the PS5
// (or any file the app allows) sent to a Discord channel through the channel's webhook, an address the server's
// owner makes in Discord (Server settings -> Integrations -> Webhooks -> Copy webhook URL). No sign-in: whoever
// has the address can post to that one channel, so it is kept like a password (never in the logs, only its
// start: share_hook_short()).

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SHARE_HOOK_MAX     512
#define SHARE_DISCORD_MOST (10L * 1024 * 1024)  // a server without boosts takes files of up to 10 MiB

// 1 when url is a Discord webhook's address: https://discord.com/api/webhooks/<digits>/<token> (or discordapp.com,
// ptb.discord.com, canary.discord.com), the token of letters, digits, '-' and '_', nothing after it. local: also
// http://127.0.0.1:<port>/api/webhooks/<digits>/<token> (the app's own test, SHOW_PROBE).
int share_hook_ok(const char* url, int local);

// For the logs and the screen: the address up to its id's first 4 digits ("discord.com/api/webhooks/1234..."),
// never the token.
void share_hook_short(const char* url, char* out, size_t size);

// Discord's payload_json, {"content":"<text>"} with text escaped as JSON; its length, or -1 when it does not fit.
int share_payload(const char* text, char* out, size_t size);

// The file at path sent to the webhook, with text as the message (none when empty). 0 when Discord took it
// (HTTP 2xx); -1 otherwise, *status the HTTP code (0: no answer, *curl_err says why). Blocks: call it on a
// thread of its own.
int share_discord(const char* hook, const char* path, const char* text, int* status, int* curl_err);

#ifdef __cplusplus
}
#endif
