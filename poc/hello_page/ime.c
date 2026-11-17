/* Copyright (C) 2026 BrowserPS5 contributors

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING. If not, see
<http://www.gnu.org/licenses/>.  */

// The system keyboard (libSceImeDialog), drawn by the system over the app.
// A thread waits for it to close and hands the text over.

#ifndef POC_NAME  // the browser builds these too, under its own name
#define POC_NAME "hello_page"
#endif

#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "ime.h"
#include "poc_log.h"

// SceImeDialogParam and SceImeDialogResult as in the public PS4 homebrew
// headers (OpenOrbis); the SDK has the stubs, not the headers.
typedef struct ime_param {
  int32_t user_id;
  int32_t type;              // 0 default, 2 URL, 3 mail, 4 number
  uint64_t languages;        // 0: the system's
  int32_t enter_label;       // ime_enter_t
  int32_t input_method;
  void* filter;
  uint32_t option;
  uint32_t max_text_length;
  uint16_t* text;            // UTF-16, the starting text in, the result out
  float x, y;                // on a 1920x1080 screen
  int32_t halign, valign;    // 1: centred
  const uint16_t* placeholder;
  const uint16_t* title;
  int8_t reserved[16];
} ime_param_t;

_Static_assert(sizeof(ime_param_t) == 96, "SceImeDialogParam is 96 bytes");

typedef struct ime_result {
  int32_t end_status;        // 0 OK, 1 cancelled, 2 aborted
  int8_t reserved[12];
} ime_result_t;

#ifdef TITLE_SYSIME
// 2026-11-14, BrowserPS5 RN's title (browser/title/sysime.c): the system does not load libSceImeDialog
// for a title on 13.60, so the four imports below stayed NULL there, and the first call through one ended
// the title, and with its GPU open the console (2026-11-04 18:19, 2026-11-11 01:31). The library is
// loaded and bound at run time instead (ime_load()), and the calls go through what was bound; with
// nothing bound ime_open() says so and opens nothing.
#include "sysime.h"

// SceImeParamExtended as in the public PS4 homebrew headers: 136 bytes.
typedef struct ime_extended {
  uint32_t option;           // 0x40: no touchpad or motion pointer over the keyboard
  uint8_t colors[9][4];
  int32_t priority;
  char* dictionary;
  void* filter;
  uint32_t disable_device;
  uint32_t keyboard_mode;
  int8_t reserved[60];
} ime_extended_t;

_Static_assert(sizeof(ime_extended_t) == 136, "SceImeParamExtended is 136 bytes");

static sysime_t sys;
static int sys_state;  // 0 not tried, 1 bound, -1 not there

int
ime_load(void) {
  if(!sys_state) {
    sys_state = sysime_load(&sys) ? -1 : 1;
    if(sys_state == 1 && sys.common_dialog_initialize) {
      POC_LOG("system keyboard: sceCommonDialogInitialize: 0x%x", (unsigned)sys.common_dialog_initialize());
    }
    POC_LOG("system keyboard: %s", sys_state == 1 ? "its library is loaded and bound" : "not there");
  }
  return sys_state == 1 ? 0 : -1;
}

int
ime_abort(void) {
  return sys_state == 1 && sys.abort ? sys.abort() : -1;
}

#define sceImeDialogInit(param, extended) sys.init(param, extended)
#define sceImeDialogGetStatus()           sys.get_status()
#define sceImeDialogGetResult(result)     sys.get_result(result)
#define sceImeDialogTerm()                sys.term()
#else
int sceImeDialogInit(const ime_param_t* param, void* extended);
int sceImeDialogGetStatus(void);
int sceImeDialogGetResult(ime_result_t* result);
int sceImeDialogTerm(void);
#endif

#define IME_STATUS_RUNNING  1
#define IME_STATUS_FINISHED 2
#define MAX_TEXT            1024  // UTF-16 units

static struct {
  pthread_mutex_t lock;
  int open;
  uint16_t text[MAX_TEXT + 1];
  ime_done_t done;
  void* ctx;
} ime = {.lock = PTHREAD_MUTEX_INITIALIZER};


// Bad bytes are skipped.
static void
utf8_to_utf16(const char* in, uint16_t* out, size_t max) {
  const unsigned char* s = (const unsigned char*)in;
  size_t n = 0;

  while(*s) {
    uint32_t cp;
    int extra;
    if(*s < 0x80) {
      cp = *s;
      extra = 0;
    } else if((*s & 0xe0) == 0xc0) {
      cp = *s & 0x1f;
      extra = 1;
    } else if((*s & 0xf0) == 0xe0) {
      cp = *s & 0x0f;
      extra = 2;
    } else if((*s & 0xf8) == 0xf0) {
      cp = *s & 0x07;
      extra = 3;
    } else {
      s++;
      continue;
    }
    s++;
    for(; extra && (*s & 0xc0) == 0x80; extra--) {
      cp = cp << 6 | (*s++ & 0x3f);
    }
    if(extra) {
      continue;
    }
    if(cp < 0x10000) {
      if(n + 1 > max) {
        break;
      }
      out[n++] = (uint16_t)cp;
    } else {
      if(n + 2 > max) {
        break;
      }
      cp -= 0x10000;
      out[n++] = (uint16_t)(0xd800 | cp >> 10);
      out[n++] = (uint16_t)(0xdc00 | (cp & 0x3ff));
    }
  }
  out[n] = 0;
}


static void
utf16_to_utf8(const uint16_t* in, char* out, size_t size) {
  size_t n = 0;

  for(; *in; in++) {
    uint32_t cp = *in;
    if(cp >= 0xd800 && cp < 0xdc00 && in[1] >= 0xdc00 && in[1] < 0xe000) {
      cp = 0x10000 + ((cp - 0xd800) << 10 | (in[1] - 0xdc00));
      in++;
    }
    if(n + 5 > size) {
      break;
    }
    if(cp < 0x80) {
      out[n++] = (char)cp;
    } else if(cp < 0x800) {
      out[n++] = (char)(0xc0 | cp >> 6);
      out[n++] = (char)(0x80 | (cp & 0x3f));
    } else if(cp < 0x10000) {
      out[n++] = (char)(0xe0 | cp >> 12);
      out[n++] = (char)(0x80 | (cp >> 6 & 0x3f));
      out[n++] = (char)(0x80 | (cp & 0x3f));
    } else {
      out[n++] = (char)(0xf0 | cp >> 18);
      out[n++] = (char)(0x80 | (cp >> 12 & 0x3f));
      out[n++] = (char)(0x80 | (cp >> 6 & 0x3f));
      out[n++] = (char)(0x80 | (cp & 0x3f));
    }
  }
  out[n] = 0;
}


static void*
ime_thread(void* arg) {
  static char text[MAX_TEXT * 3 + 1];  // a UTF-16 unit is at most 3 bytes
  ime_result_t result;
  int status;
  int err = 0;

  (void)arg;
  while((status=sceImeDialogGetStatus()) == IME_STATUS_RUNNING) {
    usleep(50000);
  }
  memset(&result, 0, sizeof(result));
  if(status == IME_STATUS_FINISHED) {
    err = sceImeDialogGetResult(&result);
  }
  POC_LOG("keyboard closed: status 0x%x, result 0x%x, end status %d", status,
          err, result.end_status);
  int ok = status == IME_STATUS_FINISHED && !err && result.end_status == 0;
  if(ok) {
    utf16_to_utf8(ime.text, text, sizeof(text));
  }
  sceImeDialogTerm();

  pthread_mutex_lock(&ime.lock);
  ime.open = 0;
  pthread_mutex_unlock(&ime.lock);
  ime.done(ok ? text : 0, ime.ctx);
  return 0;
}


int
ime_open(int user_id, ime_kind_t kind, ime_enter_t enter, const char* text,
         ime_done_t done, void* ctx) {
  static const int32_t types[] = {
    [IME_TEXT] = 0, [IME_URL] = 2, [IME_MAIL] = 3, [IME_NUMBER] = 4,
  };
  ime_param_t param;
  pthread_t thread;
  int err;

#ifdef TITLE_SYSIME
  if(sys_state != 1) {
    POC_LOG("system keyboard: asked for, and its library is not loaded: nothing opens");
    return -1;
  }
#endif
  pthread_mutex_lock(&ime.lock);
  if(ime.open) {
    pthread_mutex_unlock(&ime.lock);
    return 1;
  }
  ime.open = 1;
  pthread_mutex_unlock(&ime.lock);

  utf8_to_utf16(text ? text : "", ime.text, MAX_TEXT);
  ime.done = done;
  ime.ctx = ctx;

  memset(&param, 0, sizeof(param));
  param.user_id = user_id;
  param.type = types[kind];
  param.enter_label = enter;
  param.max_text_length = MAX_TEXT;
  param.text = ime.text;
  param.x = 1920 / 2;
  param.y = 1080 / 2;
  param.halign = 1;
  param.valign = 1;
  param.title = (const uint16_t*)u"BrowserPS5";

#ifdef TITLE_SYSIME
  // As Parallax Browser opens it on 13.60: along the bottom of the screen, and with no pointer of the
  // controller's over it (the extended parameters); without those if the system refuses them.
  ime_extended_t extended;
  memset(&extended, 0, sizeof(extended));
  extended.option = 0x40;
  param.y = 1080 - 16;
  param.valign = 2;
  err = sceImeDialogInit(&param, &extended);
  if(err) {
    POC_LOG("sceImeDialogInit with the pointer off: 0x%x; again without", err);
    err = sceImeDialogInit(&param, 0);
  }
  if(err) {
    POC_LOG("sceImeDialogInit: 0x%x", err);
#else
  if((err=sceImeDialogInit(&param, 0))) {
    POC_LOG("sceImeDialogInit: 0x%x", err);
#endif
  } else if((err=pthread_create(&thread, 0, ime_thread, 0))) {
    POC_LOG("pthread_create: %d", err);
    sceImeDialogTerm();
  } else {
    pthread_detach(thread);
    POC_LOG("keyboard open (kind %d, enter %d, starting with %zu bytes of "
            "text)", kind, enter, text ? strlen(text) : 0);
    return 0;
  }

  pthread_mutex_lock(&ime.lock);
  ime.open = 0;
  pthread_mutex_unlock(&ime.lock);
  return err;
}


int
ime_is_open(void) {
  pthread_mutex_lock(&ime.lock);
  int open = ime.open;
  pthread_mutex_unlock(&ime.lock);
  return open;
}
