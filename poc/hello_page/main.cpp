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

// hello_page: milestone M1 of docs/02-desenho-sem-gpu.md.
//
// WebKit's UIProcess, inside the app: loads a fixed HTML page (no network)
// and shows it on the TV. No compositing (the PlayStation port's default):
// the WebProcess paints with Cairo, WebKit calls setViewNeedsDisplay, and
// WKPagePaint copies the page into our ARGB32 buffer. A display thread
// hands that buffer to sceVideoOut.
//
// The WebProcess and NetworkProcess are started by the launcher payload
// (poc/launcher), through process-launcher.
//
// Milestone M2: built with PAGE_URL (make URL=https://...), it loads that
// page from the network instead, and logs why a navigation fails.
//
// Milestone M3: the controller (input.c) moves a cursor drawn over the
// page, clicks, scrolls and goes back and forward; options quits. A click
// on a text field (or triangle) opens the system keyboard (ime.c), and its
// text is typed into the field, a key per character.

#define POC_NAME "hello_page"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <WebKit/WKContext.h>
#include <WebKit/WKContextConfigurationPlayStation.h>
#include <WebKit/WKContextConfigurationRef.h>
#include <WebKit/WKErrorRef.h>
#include <WebKit/WKPage.h>
#include <WebKit/WKPageConfigurationRef.h>
#include <WebKit/WKPageNavigationClient.h>
#include <WebKit/WKPagePrivatePlayStation.h>
#include <WebKit/WKRunLoop.h>
#include <WebKit/WKString.h>
#include <WebKit/WKType.h>
#include <WebKit/WKURL.h>
#include <WebKit/WKView.h>
#include <WebKit/WKViewClient.h>

#include "poc_log.h"
#include "crash_log.h"
#include "display.h"
#include "ime.h"
#include "input.h"
#include "shot.h"

#define WEB_PROCESS_PATH     "/user/homebrew/browserps5/WebProcess.elf"
#define NETWORK_PROCESS_PATH "/user/homebrew/browserps5/NetworkProcess.elf"
// make RUN_SECONDS=n; options on the controller quits earlier.
#ifndef RUN_SECONDS
#define RUN_SECONDS          30
#endif

// The port's own user agent says "(PlayStation; ...)", and sites take it
// for a TV console: google.com sent its old basic HTML page (2026-11-03).
// This is desktop Safari's, the same engine.
#define USER_AGENT "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) " \
                   "AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.0 " \
                   "Safari/605.1.15"

// Text with accents (UTF-8 and the fonts), three colours to check the
// byte order on the TV, a script, and a counter that repaints every second.
[[maybe_unused]] static const char page_html[] = R"(<!doctype html>
<html><head><meta charset="utf-8"><style>
body { margin: 0; background: #101820; color: #f0f0f0; font-family: sans-serif; }
h1   { font-size: 96px; margin: 80px 100px 20px; }
p    { font-size: 40px; margin: 0 100px 40px; }
.row { display: flex; gap: 40px; margin: 40px 100px; }
.box { width: 320px; height: 200px; font-size: 44px; display: flex;
       align-items: center; justify-content: center; border-radius: 24px; }
</style></head><body>
<h1>BrowserPS5</h1>
<p>WebKit (port PlayStation) a desenhar em CPU com Cairo, sem GPU.</p>
<div class="row">
  <div class="box" style="background:#e03030">vermelho</div>
  <div class="box" style="background:#30c040">verde</div>
  <div class="box" style="background:#3060e0">azul</div>
</div>
<p id="js">JavaScript: à espera…</p>
<p id="t">0 s</p>
<script>
document.getElementById('js').textContent = 'JavaScript: a funcionar ✓';
var s = 0;
setInterval(function () { document.getElementById('t').textContent = ++s + ' s'; }, 1000);
</script>
</body></html>
)";


typedef struct app {
  pthread_mutex_t lock;
  pthread_cond_t  cond;
  uint32_t* page;   // what WKPagePaint fills
  uint32_t* frame;  // the copy the display thread shows
  bool dirty;
  bool quit;
  WKViewRef view;
  WKRect damage;    // not painted yet
  bool paint_pending;
  bool hand_cursor; // over a link
  bool ime_replace; // the keyboard's text replaces the field's
  bool ime_enter;   // and is followed by Enter (search, URL)
  unsigned paints;
  unsigned frames;
  unsigned missed_flips;
  double start_ms;
  double first_paint_ms;
} app_t;

static app_t app = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER};


static WKRect
rect_union(WKRect a, WKRect b) {
  if(a.size.width <= 0 || a.size.height <= 0) {
    return b;
  }
  double x0 = a.origin.x < b.origin.x ? a.origin.x : b.origin.x;
  double y0 = a.origin.y < b.origin.y ? a.origin.y : b.origin.y;
  double x1 = a.origin.x + a.size.width;
  double y1 = a.origin.y + a.size.height;
  if(b.origin.x + b.size.width > x1) {
    x1 = b.origin.x + b.size.width;
  }
  if(b.origin.y + b.size.height > y1) {
    y1 = b.origin.y + b.size.height;
  }
  return WKRectMake(x0, y0, x1 - x0, y1 - y0);
}


/**
 * Runs on the main thread, after setViewNeedsDisplay has returned.
 **/
static void
paint(void*) {
  WKRect rect = app.damage;
  double t0 = poc_now_ms();

  app.damage = WKRectMake(0, 0, 0, 0);
  app.paint_pending = false;

  pthread_mutex_lock(&app.lock);
  WKPagePaint(WKViewGetPage(app.view), (unsigned char*)app.page,
              WKSizeMake(DISPLAY_WIDTH, DISPLAY_HEIGHT), rect);
  app.dirty = true;
  app.paints++;
  pthread_cond_signal(&app.cond);
  pthread_mutex_unlock(&app.lock);

  if(app.first_paint_ms < 0) {
    app.first_paint_ms = t0 - app.start_ms;
    POC_LOG("first paint %.0f ms after start (%.0fx%.0f at %.0f,%.0f, "
            "WKPagePaint %.2f ms)", app.first_paint_ms, rect.size.width,
            rect.size.height, rect.origin.x, rect.origin.y, poc_now_ms() - t0);
  }
}


static void
setViewNeedsDisplay(WKViewRef, WKRect rect, const void*) {
  app.damage = rect_union(app.damage, rect);
  if(!app.paint_pending) {
    app.paint_pending = true;
    WKRunLoopCallOnMainThread(paint, 0);
  }
}


// WebKit says which cursor the page wants (a hand over links); ours only
// changes colour.
static void
setCursor(WKViewRef, WKCursorType type, const void*) {
  pthread_mutex_lock(&app.lock);
  app.hand_cursor = type == kWKCursorTypeHand;
  app.dirty = true;
  pthread_cond_signal(&app.cond);
  pthread_mutex_unlock(&app.lock);
}


// An arrow, drawn at twice this size: X outline, . fill.
static const char* const cursor_shape[] = {
  "X",
  "XX",
  "X.X",
  "X..X",
  "X...X",
  "X....X",
  "X.....X",
  "X......X",
  "X.......X",
  "X........X",
  "X.....XXXXX",
  "X..X..X",
  "X.X X..X",
  "XX  X..X",
  "X    X..X",
  "     X..X",
  "      XX",
};


// Over the frame, not the page: the page buffer stays WebKit's.
static void
draw_cursor(uint32_t* frame, bool hand) {
  uint32_t fill = hand ? 0xff60b0ff : 0xffffffff;
  float fx, fy;

  input_cursor(&fx, &fy);
  int x0 = (int)fx;
  int y0 = (int)fy;
  for(int row = 0; row < (int)(sizeof(cursor_shape) / sizeof(*cursor_shape));
      row++) {
    for(int col = 0; cursor_shape[row][col]; col++) {
      char c = cursor_shape[row][col];
      if(c == ' ') {
        continue;
      }
      uint32_t color = c == 'X' ? 0xff000000 : fill;
      for(int dy = 0; dy < 2; dy++) {
        for(int dx = 0; dx < 2; dx++) {
          int x = x0 + col * 2 + dx;
          int y = y0 + row * 2 + dy;
          if(x >= 0 && x < DISPLAY_WIDTH && y >= 0 && y < DISPLAY_HEIGHT) {
            frame[(size_t)y * DISPLAY_WIDTH + x] = color;
          }
        }
      }
    }
  }
}


static void*
display_thread(void*) {
  double tile_sum = 0;
  double tile_ms;
  double wait_ms;

  for(;;) {
    pthread_mutex_lock(&app.lock);
    while(!app.dirty && !app.quit) {
      pthread_cond_wait(&app.cond, &app.lock);
    }
    if(app.quit) {
      pthread_mutex_unlock(&app.lock);
      break;
    }
    memcpy(app.frame, app.page, DISPLAY_WIDTH * DISPLAY_HEIGHT * 4);
    app.dirty = false;
    bool hand = app.hand_cursor;
    pthread_mutex_unlock(&app.lock);
    draw_cursor(app.frame, hand);

    tile_ms = wait_ms = 0;  // vout_show adds to them
    int err = display_show(app.frame, &tile_ms, &wait_ms);
    if(err == 1) {
      app.missed_flips++;
    } else if(err) {
      POC_LOG("display_show: 0x%x", err);
      break;
    }
    app.frames++;
    tile_sum += tile_ms;
    if(app.frames == 1) {
      POC_RESULT(1, "first frame on the TV %.0f ms after start",
                 poc_now_ms() - app.start_ms);
    }
  }

  if(app.frames) {
    POC_LOG("%u frame(s) shown, %.2f ms per copy to video memory, %u missed "
            "flip(s)", app.frames, tile_sum / app.frames, app.missed_flips);
  }
  return 0;
}


// Copies a WebKit string into buf and releases it.
static const char*
take_string(WKStringRef s, char* buf, size_t size) {
  buf[0] = 0;
  if(s) {
    WKStringGetUTF8CString(s, buf, size);
    WKRelease(s);
  }
  return buf;
}


static void
didCommitNavigation(WKPageRef, WKNavigationRef, WKTypeRef, const void*) {
  POC_LOG("first bytes of the page in, %.0f ms after start",
          poc_now_ms() - app.start_ms);
}


#ifdef TYPE_TEST
static void type_test(void);
#endif

static void
didFinishNavigation(WKPageRef, WKNavigationRef, WKTypeRef, const void*) {
  POC_RESULT(1, "page loaded %.0f ms after start", poc_now_ms() - app.start_ms);
#ifdef TYPE_TEST
  type_test();
#endif
}


// Before the page commits (DNS, TLS, HTTP) or after (a subresource).
static void
didFailNavigation(WKPageRef, WKNavigationRef, WKErrorRef error, WKTypeRef,
                  const void*) {
  char domain[64];
  char description[256];
  char url[256];

  take_string(WKErrorCopyDomain(error), domain, sizeof(domain));
  take_string(WKErrorCopyLocalizedDescription(error), description,
              sizeof(description));
  WKURLRef failing = WKErrorCopyFailingURL(error);
  take_string(failing ? WKURLCopyString(failing) : 0, url, sizeof(url));
  if(failing) {
    WKRelease(failing);
  }
  POC_RESULT(0, "navigation failed %.0f ms after start: %s %d \"%s\" (%s)",
             poc_now_ms() - app.start_ms, domain, WKErrorGetErrorCode(error),
             description, url);
}


static void
webProcessDidCrash(WKPageRef, const void*) {
  POC_RESULT(0, "the WebProcess crashed");
}


// On the main thread, so no paint runs meanwhile: the page as it is now goes
// to the log as a PNG (poc/shot.ps1 gets it back).
static void
stop(void*) {
  static bool stopped;

  if(stopped) {  // options, then the timer
    return;
  }
  stopped = true;
  // The page buffer is not painted again after this, so the cursor can go
  // into it, for the screenshot.
  pthread_mutex_lock(&app.lock);
  draw_cursor(app.page, app.hand_cursor);
  pthread_mutex_unlock(&app.lock);
  unsigned long size = shot_print(app.page, DISPLAY_WIDTH, DISPLAY_HEIGHT);
  POC_RESULT(size != 0, "screenshot in the log: %lu bytes of PNG", size);
  WKRunLoopStopMain();
}


// libwpe's key codes are X11 keysyms: ASCII and Latin-1 are themselves, the
// rest of Unicode is 0x01000000 + the code point.
#define KEY_BACKSPACE 0xff08
#define KEY_RETURN    0xff0d

// Down and up. With no text, WebKit takes it from the key code (Enter: "\r",
// which submits a form).
static unsigned keys_sent;

static void
send_key(WKPageRef page, int32_t code, const char* text, uint32_t length) {
  keys_sent++;
  WKPageHandleKeyboardEvent(page, WKKeyboardEventMake(kWKEventKeyDown,
      kWKInputTypeNormal, text, length, "", code, 0, 0, 0));
  WKPageHandleKeyboardEvent(page, WKKeyboardEventMake(kWKEventKeyUp,
      kWKInputTypeNormal, text, length, "", code, 0, 0, 0));
}


// A key per character, as if typed. Returns how many.
static unsigned
type_text(WKPageRef page, const char* text) {
  const unsigned char* s = (const unsigned char*)text;
  unsigned count = 0;

  while(*s) {
    uint32_t length = *s < 0x80 ? 1 : (*s & 0xe0) == 0xc0 ? 2
                    : (*s & 0xf0) == 0xe0 ? 3 : 4;
    uint32_t cp = length == 1 ? *s : *s & (0x7f >> length);
    for(uint32_t i = 1; i < length; i++) {
      if((s[i] & 0xc0) != 0x80) {  // cut short: ime.c makes valid UTF-8
        return count;
      }
      cp = cp << 6 | (s[i] & 0x3f);
    }
    int32_t code = cp == '\n' ? KEY_RETURN
                 : (cp >= 0x20 && cp < 0x7f) || (cp >= 0xa0 && cp <= 0xff)
                 ? (int32_t)cp : (int32_t)(0x01000000 | cp);
    send_key(page, code, (const char*)s, length);
    s += length;
    count++;
  }
  return count;
}


static void
ignore_result(WKTypeRef, WKErrorRef, void*) {
}


static void
run_script(WKPageRef page, const char* script, void* ctx,
           WKPageEvaluateJavaScriptFunction done) {
  WKStringRef s = WKStringCreateWithUTF8CString(script);

  WKPageEvaluateJavaScriptInMainFrame(page, s, ctx, done);
  WKRelease(s);
}


// The element in focus, inside shadow roots too.
#define FOCUSED_ELEMENT                                         \
  "var e = document.activeElement;"                             \
  "while (e && e.shadowRoot && e.shadowRoot.activeElement)"     \
  "  e = e.shadowRoot.activeElement;"

// "<input type>\n<value>" for a text field, "edit\n" for other editable
// content (typed at the caret), null for anything else.
static const char field_script[] = "(function () {" FOCUSED_ELEMENT R"(
  if (!e) return null;
  if (e.tagName == 'TEXTAREA') return 'text\n' + e.value;
  if (e.tagName == 'INPUT' &&
      /^(text|search|url|email|tel|password|number)$/.test(e.type))
    return e.type + '\n' + e.value;
  if (e.isContentEditable) return 'edit\n';
  return null;
})())";

static const char select_script[] = "(function () {" FOCUSED_ELEMENT R"(
  if (e && e.select) e.select();
})())";


// On the main thread.
static void
keyboard_text(void* arg) {
  char* text = (char*)arg;
  WKPageRef page = WKViewGetPage(app.view);

  if(!text) {
    POC_LOG("keyboard cancelled");
    return;
  }
  // The script runs in the WebProcess before the keys that follow.
  if(app.ime_replace) {
    run_script(page, select_script, 0, ignore_result);
    if(!*text) {
      send_key(page, KEY_BACKSPACE, 0, 0);
    }
  }
  unsigned typed = type_text(page, text);  // not in POC_LOG: it runs twice
  // Only how much: it may be a password.
  POC_RESULT(1, "typed %u character(s) from the keyboard", typed);
  // The keyboard's own key said "Search" or "Go".
  if(app.ime_enter) {
    send_key(page, KEY_RETURN, 0, 0);
  }
  free(text);
}


// On the keyboard's thread.
static void
keyboard_closed(const char* text, void*) {
  WKRunLoopCallOnMainThread(keyboard_text, text ? strdup(text) : 0);
}


// field_script's answer, on the main thread. ctx is 0 after a click, which
// opens the keyboard only for a text field.
static void
field_found(WKTypeRef result, WKErrorRef, void* ctx) {
  char kind[4096];

  if(!result || WKGetTypeID(result) != WKStringGetTypeID()) {
    if(ctx) {
      POC_LOG("triangle: no text field in focus");
    }
    return;
  }
  WKStringGetUTF8CString((WKStringRef)result, kind, sizeof(kind));
  char* value = strchr(kind, '\n');
  if(!value) {
    return;
  }
  *value++ = 0;
  ime_kind_t ime = !strcmp(kind, "url") ? IME_URL
                 : !strcmp(kind, "email") ? IME_MAIL
                 : !strcmp(kind, "number") ? IME_NUMBER : IME_TEXT;
  ime_enter_t enter = !strcmp(kind, "search") ? IME_ENTER_SEARCH
                    : ime == IME_URL ? IME_ENTER_GO : IME_ENTER_DEFAULT;
  app.ime_replace = strcmp(kind, "edit") != 0;
  app.ime_enter = enter != IME_ENTER_DEFAULT;
  POC_LOG("text field in focus: %s", kind);
  int err = ime_open(input_user(), ime, enter, value, keyboard_closed, 0);
  if(err && err != 1) {
    POC_RESULT(0, "the system keyboard did not open: 0x%x", err);
  }
}


#ifdef TYPE_TEST
// make TYPE_TEST=text: a page with a text field that records its events.
// Once it is loaded the text is typed into the field as the keyboard's
// would be, and 2 s later the field's value and the events go to the log.
// No one needs to be at the TV.
static const char type_test_html[] = R"(<!doctype html>
<html><head><meta charset="utf-8"><style>
body  { background: #fff; font: 32px sans-serif; margin: 80px; }
input { font: 60px sans-serif; width: 1200px; }
</style></head><body>
<input id="field" autofocus>
<script>
var events = [];
['keydown', 'keypress', 'beforeinput', 'textInput', 'input', 'keyup']
  .forEach(function (type) {
    field.addEventListener(type, function (e) {
      events.push(type + ' ' + (e.key || e.data || '') + ' -> ' + field.value);
    });
  });
</script></body></html>)";

static void
type_test_reported(WKTypeRef result, WKErrorRef, void*) {
  char report[8192];

  if(!result || WKGetTypeID(result) != WKStringGetTypeID()) {
    POC_RESULT(0, "type test: no report from the page");
    return;
  }
  WKStringGetUTF8CString((WKStringRef)result, report, sizeof(report));
  POC_LOG("type test: %u key(s) sent, the page says %s", keys_sent, report);
  POC_RESULT(strstr(report, "\"value\":\"" TYPE_TEST "\"") != 0,
             "type test: the field holds \"%s\" once", TYPE_TEST);
}


static void
type_test_report(void*) {
  run_script(WKViewGetPage(app.view),
             "JSON.stringify({ value: field.value, events: events })", 0,
             type_test_reported);
}


// The second half of the text, a second after the first: whether what is
// repeated is each batch of keys or all of them.
static void
type_test_second_half(void*) {
  const char* text = TYPE_TEST;
  unsigned typed = type_text(WKViewGetPage(app.view), text + strlen(text) / 2);

  POC_LOG("type test: typed %u more character(s)", typed);
}


static void*
type_test_thread(void*) {
  sleep(1);
  WKRunLoopCallOnMainThread(type_test_second_half, 0);
  sleep(2);  // the keys wait in the UIProcess's queue, the script would not
  WKRunLoopCallOnMainThread(type_test_report, 0);
  return 0;
}


// On the main thread, once the page is in.
static void
type_test(void) {
  WKPageRef page = WKViewGetPage(app.view);
  char first_half[64];
  pthread_t thread;

  snprintf(first_half, sizeof(first_half), "%.*s",
           (int)(strlen(TYPE_TEST) / 2), TYPE_TEST);
  run_script(page, "field.focus()", 0, ignore_result);
  unsigned typed = type_text(page, first_half);
  POC_LOG("type test: typed %u character(s)", typed);
  pthread_create(&thread, 0, type_test_thread, 0);
  pthread_detach(thread);
}
#endif


// On the main thread: WebKit's API is not thread safe.
static void
dispatch(void* arg) {
  input_event_t* e = (input_event_t*)arg;
  WKPageRef page = WKViewGetPage(app.view);
  WKPoint at = WKPointMake(e->x, e->y);

  switch(e->kind) {
  case INPUT_MOVE:
    WKPageHandleMouseEvent(page, WKMouseEventMake(kWKEventMouseMove,
        e->pressed ? kWKEventMouseButtonLeftButton
                   : kWKEventMouseButtonNoButton, at, 0, 0));
    break;
  case INPUT_BUTTON_DOWN:
  case INPUT_BUTTON_UP:
    WKPageHandleMouseEvent(page, WKMouseEventMake(
        e->kind == INPUT_BUTTON_DOWN ? kWKEventMouseDown : kWKEventMouseUp,
        kWKEventMouseButtonLeftButton, at, 1, 0));
    if(e->kind == INPUT_BUTTON_UP) {  // after the click: a text field?
      run_script(page, field_script, 0, field_found);
    }
    break;
  case INPUT_SCROLL:
    // A wheel's delta is positive upwards, as in WebWheelEvent; a tick is
    // 40 pixels (Scrollbar::pixelsPerLineStep).
    WKPageHandleWheelEvent(page, WKWheelEventMake(kWKEventWheel, at,
        WKSizeMake(-e->dx, -e->dy), WKSizeMake(-e->dx / 40, -e->dy / 40),
        0));
    break;
  case INPUT_BACK:
    POC_LOG("back");
    WKPageGoBack(page);
    break;
  case INPUT_FORWARD:
    POC_LOG("forward");
    WKPageGoForward(page);
    break;
  case INPUT_KEYBOARD:
    run_script(page, field_script, (void*)1, field_found);
    break;
  case INPUT_ENTER:
    POC_LOG("enter");
    send_key(page, KEY_RETURN, 0, 0);
    break;
  case INPUT_QUIT:
    POC_LOG("options: quitting");
    stop(0);
    break;
  }
  free(e);
}


// On the input thread.
static void
on_input(const input_event_t* event) {
  input_event_t* copy = (input_event_t*)malloc(sizeof(*copy));

  if(event->kind == INPUT_MOVE) {  // the cursor moved: show it
    pthread_mutex_lock(&app.lock);
    app.dirty = true;
    pthread_cond_signal(&app.cond);
    pthread_mutex_unlock(&app.lock);
  }
  if(copy) {
    *copy = *event;
    WKRunLoopCallOnMainThread(dispatch, copy);
  }
}


static void*
timer_thread(void*) {
  sleep(RUN_SECONDS);
  WKRunLoopCallOnMainThread(stop, 0);
  return 0;
}


static WKStringRef
string(const char* s) {
  return WKStringCreateWithUTF8CString(s);
}


int
main(void) {
  pthread_t display;
  pthread_t timer;
  int err;

  app.start_ms = poc_now_ms();
  app.first_paint_ms = -1;
  crash_log_install();
  // nm gives WKContextCreateWithConfiguration's offset in libWebKit: the
  // difference is the load address, for the crash log's addresses. (No
  // crash_log_watchdog: its ucontext registers read 0 here, and walking the
  // stack from rsp 0 crashed the app at 10 s, 2026-11-03. WebKit's own
  // crashes print file:line, see poc/webkit/crash-report.)
  uintptr_t wk = (uintptr_t)&WKContextCreateWithConfiguration;
  POC_LOG("pid %d, WKContextCreateWithConfiguration at 0x%lx", getpid(),
          (unsigned long)wk);

  if(!(app.page=(uint32_t*)calloc(DISPLAY_WIDTH * DISPLAY_HEIGHT, 4))
     || !(app.frame=(uint32_t*)calloc(DISPLAY_WIDTH * DISPLAY_HEIGHT, 4))) {
    POC_PERROR("calloc");
    return 1;
  }
  if((err=display_open())) {
    POC_RESULT(0, "display_open: 0x%x", err);
    return 1;
  }
  POC_RESULT(1, "video out open");

  WKRunLoopInitializeMain();
  POC_LOG("run loop ready");

  WKContextConfigurationRef contextConfig = WKContextConfigurationCreate();
  WKContextConfigurationSetWebProcessPath(contextConfig, string(WEB_PROCESS_PATH));
  WKContextConfigurationSetNetworkProcessPath(contextConfig, string(NETWORK_PROCESS_PATH));
  WKContextRef context = WKContextCreateWithConfiguration(contextConfig);
  POC_LOG("context created");

  WKPageConfigurationRef pageConfig = WKPageConfigurationCreate();
  WKPageConfigurationSetContext(pageConfig, context);

  app.view = WKViewCreate(pageConfig);
  WKPageRef page = WKViewGetPage(app.view);
  POC_LOG("view created");

  WKViewClientV0 viewClient;
  memset(&viewClient, 0, sizeof(viewClient));
  viewClient.base.version = 0;
  viewClient.setViewNeedsDisplay = setViewNeedsDisplay;
  viewClient.setCursor = setCursor;
  WKViewSetViewClient(app.view, &viewClient.base);

  WKPageNavigationClientV0 navigationClient;
  memset(&navigationClient, 0, sizeof(navigationClient));
  navigationClient.base.version = 0;
  navigationClient.didCommitNavigation = didCommitNavigation;
  navigationClient.didFinishNavigation = didFinishNavigation;
  navigationClient.didFailProvisionalNavigation = didFailNavigation;
  navigationClient.didFailNavigation = didFailNavigation;
  navigationClient.webProcessDidCrash = webProcessDidCrash;
  WKPageSetPageNavigationClient(page, &navigationClient.base);

  WKViewSetSize(app.view, WKSizeMake(DISPLAY_WIDTH, DISPLAY_HEIGHT));
  WKViewSetVisible(app.view, true);
  WKViewSetActive(app.view, true);
  WKViewSetFocus(app.view, true);

#if defined(TYPE_TEST)
  WKPageLoadHTMLString(page, string(type_test_html), 0);
  POC_LOG("loading the type test page, running for %d s", RUN_SECONDS);
#elif defined(PAGE_URL)
  char user_agent[512];
  take_string(WKPageCopyUserAgent(page), user_agent, sizeof(user_agent));
  POC_LOG("the port's user agent: %s", user_agent);
  WKPageSetCustomUserAgent(page, string(USER_AGENT));
  WKURLRef url = WKURLCreateWithUTF8CString(PAGE_URL);
  WKPageLoadURL(page, url);
  WKRelease(url);
  POC_LOG("loading %s, running for %d s", PAGE_URL, RUN_SECONDS);
#else
  WKPageLoadHTMLString(page, string(page_html), 0);
  POC_LOG("loading the page, running for %d s", RUN_SECONDS);
#endif

  if((err=input_start(on_input, DISPLAY_WIDTH, DISPLAY_HEIGHT))) {
    POC_RESULT(0, "no controller (0x%x): the page runs without input", err);
  }

  pthread_create(&display, 0, display_thread, 0);
  pthread_create(&timer, 0, timer_thread, 0);

  WKRunLoopRunMain();

  pthread_mutex_lock(&app.lock);
  app.quit = true;
  pthread_cond_signal(&app.cond);
  pthread_mutex_unlock(&app.lock);
  pthread_join(display, 0);
  display_close();

  POC_LOG("done: %u paint(s), first paint %.0f ms after start", app.paints,
          app.first_paint_ms);
  // Not return: WebKit's threads are still running, and the static
  // destructors under them crashed the app on its way out (2026-11-03).
  _exit(app.paints ? 0 : 1);
}
