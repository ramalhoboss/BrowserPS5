/* Copyright (C) 2026 BrowserPS5 contributors. GPL-3.0-or-later.

   share.c's checks, on the build machine:
     gcc -Wall -Werror -O2 -DSHARE_NO_CURL -o share_test share_test.c share.c && ./share_test */

#include <stdio.h>
#include <string.h>

#include "share.h"

static int failed;

static void
check(int ok, const char* what) {
  if(!ok) {
    printf("FAIL %s\n", what);
    failed++;
  }
}

int
main(void) {
  const char* token = "AbCdEfGhIjKlMnOpQrStUvWxYz0123456789-_AbCdEfGhIjKlMnOpQrStUvWxYz01";
  char url[600], out[256];

  snprintf(url, sizeof(url), "https://discord.com/api/webhooks/123456789012345678/%s", token);
  check(share_hook_ok(url, 0), "discord.com");
  snprintf(url, sizeof(url), "https://discordapp.com/api/webhooks/123456789012345678/%s", token);
  check(share_hook_ok(url, 0), "discordapp.com");
  snprintf(url, sizeof(url), "https://ptb.discord.com/api/webhooks/123456789012345678/%s", token);
  check(share_hook_ok(url, 0), "ptb");
  snprintf(url, sizeof(url), "https://canary.discord.com/api/webhooks/123456789012345678/%s", token);
  check(share_hook_ok(url, 0), "canary");
  snprintf(url, sizeof(url), "http://discord.com/api/webhooks/123456789012345678/%s", token);
  check(!share_hook_ok(url, 0), "http refused");
  snprintf(url, sizeof(url), "https://discord.com.evil.example/api/webhooks/123456789012345678/%s", token);
  check(!share_hook_ok(url, 0), "a host that only starts like Discord");
  snprintf(url, sizeof(url), "https://evil.example/https://discord.com/api/webhooks/123456789012345678/%s", token);
  check(!share_hook_ok(url, 0), "Discord's address inside another");
  snprintf(url, sizeof(url), "https://discord.com/api/webhooks/123456789012345678/%s?wait=true", token);
  check(!share_hook_ok(url, 0), "something after the token");
  snprintf(url, sizeof(url), "https://discord.com/api/webhooks/123456789012345678/%s/slack", token);
  check(!share_hook_ok(url, 0), "a path after the token");
  snprintf(url, sizeof(url), "https://discord.com/api/webhooks/12ab/%s", token);
  check(!share_hook_ok(url, 0), "an id with letters");
  check(!share_hook_ok("https://discord.com/api/webhooks/123456789012345678/short", 0), "a token too short");
  check(!share_hook_ok("https://discord.com/channels/1/2", 0), "a channel's address");
  check(!share_hook_ok("", 0) && !share_hook_ok(0, 0), "nothing");
  snprintf(url, sizeof(url), "http://127.0.0.1:9123/api/webhooks/123456789/%s", token);
  check(!share_hook_ok(url, 0), "the local test address, not allowed");
  check(share_hook_ok(url, 1), "the local test address, allowed for the test");
  snprintf(url, sizeof(url), "http://127.0.0.2:9123/api/webhooks/123456789/%s", token);
  check(!share_hook_ok(url, 1), "another local address");
  char longer[700];
  memset(longer, 'a', sizeof(longer) - 1);
  longer[sizeof(longer) - 1] = 0;
  snprintf(url, sizeof(url), "https://discord.com/api/webhooks/123456789012345678/%.500s", longer);
  check(!share_hook_ok(url, 0), "too long");

  snprintf(url, sizeof(url), "https://discord.com/api/webhooks/123456789012345678/%s", token);
  share_hook_short(url, out, sizeof(out));
  check(!strcmp(out, "discord.com/api/webhooks/1234..."), "the short form");
  check(!strstr(out, "AbCd"), "the short form has no token");
  share_hook_short("https://example.com/x", out, sizeof(out));
  check(!strcmp(out, "(not a webhook)"), "the short form of something else");

  check(share_payload("Wo Long", out, sizeof(out)) > 0 && !strcmp(out, "{\"content\":\"Wo Long\"}"), "payload");
  check(share_payload("a \"b\" \\ c\n", out, sizeof(out)) > 0 && !strcmp(out, "{\"content\":\"a \\\"b\\\" \\\\ c\\u000a\"}"),
        "payload escaped");
  check(share_payload("", out, sizeof(out)) == 14 && !strcmp(out, "{\"content\":\"\"}"), "empty payload");
  check(share_payload("\xc3\xa9", out, sizeof(out)) > 0 && !strcmp(out, "{\"content\":\"\xc3\xa9\"}"), "UTF-8 kept");
  check(share_payload("0123456789", out, 20) == -1, "payload too big");

  printf(failed ? "%d FAILED\n" : "all passed\n", failed);
  return failed != 0;
}
