/*   Copyright (C) 2026 BrowserPS5 contributors
 *
 * This file is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * See side_card.h. The deep link and the notification's layout are the
 * ones TwitterPS5 (XPS5) found working on 13.60 (src/toast.c,
 * InteractiveToastTemplateB with a DeepLink action to
 * pscontrolcenter:main?target=action-card&type=ActionCardWebBrowser).
 * side_card_test.c checks the link and the JSON on the build machine.
 */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "side_card.h"

/* Every character but the URL's unreserved ones, as %XX. */
static int
percent(const char* in, char* out, size_t size) {
  static const char hex[] = "0123456789ABCDEF";
  size_t n = 0;

  for(; *in; in++) {
    const unsigned char c = (unsigned char)*in;
    if((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
       c == '-' || c == '_' || c == '.' || c == '~') {
      if(n + 1 >= size) {
        return -1;
      }
      out[n++] = (char)c;
    } else {
      if(n + 3 >= size) {
        return -1;
      }
      out[n++] = '%';
      out[n++] = hex[c >> 4];
      out[n++] = hex[c & 15];
    }
  }
  out[n] = 0;
  return (int)n;
}


/* A JSON string's inside: quotes, backslashes and control characters
 * escaped. */
static int
json_inside(const char* in, char* out, size_t size) {
  size_t n = 0;

  for(; *in; in++) {
    const unsigned char c = (unsigned char)*in;
    if(c == '"' || c == '\\') {
      if(n + 2 >= size) {
        return -1;
      }
      out[n++] = '\\';
      out[n++] = (char)c;
    } else if(c < 0x20) {
      if(n + 6 >= size) {
        return -1;
      }
      n += (size_t)snprintf(out + n, size - n, "\\u%04x", c);
    } else {
      if(n + 1 >= size) {
        return -1;
      }
      out[n++] = (char)c;
    }
  }
  out[n] = 0;
  return (int)n;
}


static char side_agent[256];

void
side_card_user_agent(const char* user_agent) {
  snprintf(side_agent, sizeof(side_agent), "%s", user_agent ? user_agent : "");
}


int
side_card_link(const char* url, char* out, size_t size) {
  static char url_json[SIDE_CARD_URL_MAX * 2];
  static char agent_json[sizeof(side_agent) * 2];
  static char param[SIDE_CARD_URL_MAX * 2 + sizeof(agent_json) + 128];
  static char param_encoded[sizeof(param) * 3];
  char anchor[128];

  if(!url || !*url || strlen(url) > SIDE_CARD_URL_MAX) {
    return -1;
  }
  if(json_inside(url, url_json, sizeof(url_json)) < 0 || json_inside(side_agent, agent_json, sizeof(agent_json)) < 0) {
    return -1;
  }
  snprintf(param, sizeof(param),
           "{\"url\":\"%s\",\"userAgent\":\"%s\",\"enableSplitScreen\":true,\"enablePinP\":true}", url_json, agent_json);
  if(percent("{\"vertical\":\"center\",\"horizontal\":\"center\"}", anchor, sizeof(anchor)) < 0 ||
     percent(param, param_encoded, sizeof(param_encoded)) < 0) {
    return -1;
  }
  const int n = snprintf(out, size,
                         "pscontrolcenter:main?target=action-card&type=ActionCardWebBrowser"
                         "&mode=selected&anchor=%s&param=%s",
                         anchor, param_encoded);
  return n > 0 && (size_t)n < size ? n : -1;
}


int
side_card_toast(const char* url, const char* title, const char* text, const char* button, unsigned id,
                char* out, size_t size) {
  // Static: ~8 KiB, and only the main thread asks (main.cpp).
  static char link[SIDE_CARD_URL_MAX * 8];

  if(side_card_link(url, link, sizeof(link)) < 0) {
    return -1;
  }
  return side_card_toast_link(link, title, text, button, id, out, size);
}


int
side_card_toast_link(const char* link, const char* title, const char* text, const char* button,
                     unsigned id, char* out, size_t size) {
  return side_card_toast_icon(link, "download", title, text, button, id, out, size);
}


// 2026-11-10 23:10 (BrowserPS5 RN's site notifications): icon is a picture's
// https address (TwitterPS5's avatars: "Url"), else one of the PS5's own
// ("messages", "download", "favorite", "ps_user").
int
side_card_toast_icon(const char* link, const char* icon, const char* title, const char* text,
                     const char* button, unsigned id, char* out, size_t size) {
  static char link_json[SIDE_CARD_URL_MAX * 8 + 64];
  static char icon_json[SIDE_CARD_URL_MAX + 160];
  char icon_value[SIDE_CARD_URL_MAX + 32];
  char title_json[256], text_json[512], button_json[128];
  char created[32];
  const time_t now = time(0);
  struct tm utc;

  if(!icon || !*icon) {
    icon = "messages";
  }
  if(!link || !*link || json_inside(link, link_json, sizeof(link_json)) < 0 ||
     json_inside(icon, icon_value, sizeof(icon_value)) < 0 ||
     json_inside(title, title_json, sizeof(title_json)) < 0 || json_inside(text, text_json, sizeof(text_json)) < 0 ||
     json_inside(button, button_json, sizeof(button_json)) < 0) {
    return -1;
  }
  if(!strncmp(icon, "https://", 8)) {
    snprintf(icon_json, sizeof(icon_json), "{\"type\":\"Url\",\"parameters\":{\"url\":\"%s\"}}", icon_value);
  } else {
    snprintf(icon_json, sizeof(icon_json), "{\"type\":\"Predefined\",\"parameters\":{\"icon\":\"%s\"}}",
             icon_value);
  }
  gmtime_r(&now, &utc);
  strftime(created, sizeof(created), "%Y-%m-%dT%H:%M:%S.000Z", &utc);
  const int n = snprintf(out, size,
    "{\"rawData\":{"
      "\"viewTemplateType\":\"InteractiveToastTemplateB\","
      "\"channelType\":\"Downloads\","
      "\"useCaseId\":\"IDC\","
      "\"toastOverwriteType\":\"No\","
      "\"isImmediate\":true,"
      "\"priority\":100,"
      "\"viewData\":{"
        "\"icon\":%s,"
        "\"message\":{\"body\":\"%s\"},"
        "\"subMessage\":{\"body\":\"%s\"},"
        "\"actions\":[{\"actionName\":\"%s\",\"actionType\":\"DeepLink\",\"defaultFocus\":true,"
                      "\"parameters\":{\"actionUrl\":\"%s\"}}]"
      "},"
      "\"platformViews\":{\"previewDisabled\":{\"viewData\":{"
        "\"icon\":%s,"
        "\"message\":{\"body\":\"%s: %s\"}}}}"
    "},"
    "\"createdDateTime\":\"%s\","
    "\"localNotificationId\":\"%u\"}",
    icon_json, title_json, text_json, button_json, link_json, icon_json, title_json, text_json, created, id);
  return n > 0 && (size_t)n < size ? n : -1;
}


#ifndef SIDE_CARD_HOST_TEST
#define NOTIFICATION_USER_SYSTEM 0xFE
int sceNotificationSend(int user_id, bool logged, const char* payload);
#endif

const char*
side_card_settings_link(const char* name) {
  static const char* const pages[][2] = {
    {"network", "pssettings:play?mode=settings&function=network"},
    {"video", "pssettings:play?mode=settings&function=video"},
    {"storage", "pssettings:play?mode=settings&function=storage&storage=internal&userid="},
    {"controller", "pssettings:play?mode=settings&function=controller"},
  };
  for(size_t i = 0; name && i < sizeof(pages) / sizeof(*pages); i++) {
    if(!strcmp(name, pages[i][0])) {
      return pages[i][1];
    }
  }
  return 0;
}


static char toast[SIDE_CARD_URL_MAX * 12 + 2048];

// A new number each time (two in the same second too: the system keeps
// only one notification per number).
static unsigned
next_id(void) {
  static unsigned last_id;
  unsigned id = (unsigned)time(0);
  if(id <= last_id) {
    id = last_id + 1;
  }
  last_id = id;
  return id;
}

static int
send_toast(void) {
#ifdef SIDE_CARD_HOST_TEST
  puts(toast);
  return 0;
#else
  return sceNotificationSend(NOTIFICATION_USER_SYSTEM, true, toast);
#endif
}

int
side_card_post(const char* url, const char* text, const char* button) {
  if(side_card_toast(url, "BrowserPS5", text, button, next_id(), toast, sizeof(toast)) < 0) {
    return -1;
  }
  return send_toast();
}

int
side_card_post_link(const char* link, const char* text, const char* button) {
  if(side_card_toast_link(link, "BrowserPS5", text, button, next_id(), toast, sizeof(toast)) < 0) {
    return -1;
  }
  return send_toast();
}


int
side_card_post_message(const char* link, const char* icon, const char* title, const char* text,
                       const char* button) {
  if(side_card_toast_icon(link, icon, title, text, button, next_id(), toast, sizeof(toast)) < 0) {
    return -1;
  }
  return send_toast();
}
