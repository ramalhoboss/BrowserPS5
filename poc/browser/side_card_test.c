/*   Copyright (C) 2026 BrowserPS5 contributors. GPL-3.0-or-later.
 *
 * side_card.c on the build machine (make test): the deep link as the
 * user's TwitterPS5 sends it, only the characters the ShellUI takes, the
 * address back out of it whole, the limits, and a notification whose JSON
 * holds together.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "side_card.h"

static int failed;

#define CHECK(what, cond)                                   \
  do {                                                      \
    if(cond) {                                              \
      printf("PASS %s\n", what);                            \
    } else {                                                \
      printf("FAIL %s\n", what);                            \
      failed = 1;                                           \
    }                                                       \
  } while(0)

static int
link_chars_ok(const char* s) {
  for(; *s; s++) {
    if(!strchr(":/?#[]@!$&'()*+,;=-._~%", *s) && !((*s >= 'A' && *s <= 'z') || (*s >= '0' && *s <= '9'))) {
      return 0;
    }
  }
  return 1;
}

static void
percent_decode(const char* in, char* out) {
  while(*in) {
    if(*in == '%' && in[1] && in[2]) {
      char hex[3] = {in[1], in[2], 0};
      *out++ = (char)strtol(hex, 0, 16);
      in += 3;
    } else {
      *out++ = *in++;
    }
  }
  *out = 0;
}

/* Braces and brackets balanced outside strings, strings closed. */
static int
json_holds(const char* s) {
  int depth = 0, in_string = 0;
  for(; *s; s++) {
    if(in_string) {
      if(*s == '\\' && s[1]) {
        s++;
      } else if(*s == '"') {
        in_string = 0;
      } else if((unsigned char)*s < 0x20) {
        return 0;
      }
    } else if(*s == '"') {
      in_string = 1;
    } else if(*s == '{' || *s == '[') {
      depth++;
    } else if(*s == '}' || *s == ']') {
      if(--depth < 0) {
        return 0;
      }
    }
  }
  return depth == 0 && !in_string;
}

int
main(void) {
  static char link[16384], decoded[16384], toast[32768], url[4096];

  /* The link TwitterPS5 opens x.com with (src/toast.c's card_link()). */
  const char* expected =
    "pscontrolcenter:main?target=action-card&type=ActionCardWebBrowser&mode=selected"
    "&anchor=%7B%22vertical%22%3A%22center%22%2C%22horizontal%22%3A%22center%22%7D"
    "&param=%7B%22url%22%3A%22https%3A%2F%2Fx.com%2F%22%2C%22userAgent%22%3A%22%22%2C"
    "%22enableSplitScreen%22%3Atrue%2C%22enablePinP%22%3Atrue%7D";
  const int n = side_card_link("https://x.com/", link, sizeof(link));
  CHECK("the link for x.com is TwitterPS5's", n == (int)strlen(expected) && !strcmp(link, expected));

  /* An address with a query, quotes, a space and non-ASCII. */
  const char* odd = "https://example.com/a b?q=\"x\"&lang=pt#ç";
  CHECK("an odd address makes a link", side_card_link(odd, link, sizeof(link)) > 0);
  CHECK("the link has only the characters the ShellUI takes", link_chars_ok(link));
  const char* param = strstr(link, "&param=");
  CHECK("the link has its param", param != 0);
  if(param) {
    percent_decode(param + 7, decoded);
    CHECK("the param is JSON that holds", json_holds(decoded));
    CHECK("the address comes back out escaped",
          strstr(decoded, "\"url\":\"https://example.com/a b?q=\\\"x\\\"&lang=pt#ç\"") != 0);
    CHECK("split screen is on", strstr(decoded, "\"enableSplitScreen\":true") != 0);
  }

  /* Limits. */
  CHECK("no empty address", side_card_link("", link, sizeof(link)) == -1);
  memset(url, 'a', SIDE_CARD_URL_MAX + 1);
  memcpy(url, "https://", 8);
  url[SIDE_CARD_URL_MAX + 1] = 0;
  CHECK("no address over the limit", side_card_link(url, link, sizeof(link)) == -1);
  url[SIDE_CARD_URL_MAX] = 0;
  CHECK("the longest address goes", side_card_link(url, link, sizeof(link)) > 0);
  CHECK("a link that does not fit is refused", side_card_link("https://x.com/", link, 40) == -1);

  /* The notification. */
  const int t = side_card_toast(odd, "BrowserPS5", "Abrir ao lado do \"jogo\"", "Abrir", 1234, toast,
                                sizeof(toast));
  CHECK("the notification is made", t > 0);
  CHECK("its JSON holds", json_holds(toast));
  CHECK("its button is a deep link", strstr(toast, "\"actionType\":\"DeepLink\"") != 0 &&
                                         strstr(toast, "\"actionUrl\":\"pscontrolcenter:main?") != 0);
  CHECK("its number", strstr(toast, "\"localNotificationId\":\"1234\"") != 0);
  CHECK("its text escaped", strstr(toast, "Abrir ao lado do \\\"jogo\\\"") != 0);
  CHECK("the longest address fits the notification",
        side_card_toast(url, "BrowserPS5", "Abrir ao lado do jogo", "Abrir", 1, toast, sizeof(toast)) > 0 &&
            json_holds(toast));

  /* The PS5's own settings (docs/14-definicoes-da-ps5.md): four fixed
   * links, nothing else. */
  const char* net = side_card_settings_link("network");
  CHECK("network's link", net && !strcmp(net, "pssettings:play?mode=settings&function=network"));
  CHECK("video, storage, controller",
        side_card_settings_link("video") && side_card_settings_link("storage") &&
            side_card_settings_link("controller"));
  CHECK("no other name", !side_card_settings_link("delete") && !side_card_settings_link("") &&
                             !side_card_settings_link(0) && !side_card_settings_link("network "));
  static const char* const names[] = {"network", "video", "storage", "controller"};
  int all_ok = 1;
  for(size_t i = 0; i < sizeof(names) / sizeof(*names); i++) {
    all_ok = all_ok && link_chars_ok(side_card_settings_link(names[i]));
  }
  CHECK("the links are plain link characters", all_ok);
  const int s = side_card_toast_link(net, "BrowserPS5", "Rede da PS5", "Abrir", 99, toast, sizeof(toast));
  CHECK("the settings' notification is made", s > 0 && json_holds(toast));
  CHECK("its button opens the settings",
        strstr(toast, "\"actionUrl\":\"pssettings:play?mode=settings&function=network\"") != 0);
  CHECK("no notification without a link", side_card_toast_link("", "B", "t", "b", 1, toast, sizeof(toast)) == -1 &&
                                              side_card_toast_link(0, "B", "t", "b", 1, toast, sizeof(toast)) == -1);

  printf(failed ? "side_card_test: FAIL\n" : "side_card_test: all passed\n");
  return failed;
}
