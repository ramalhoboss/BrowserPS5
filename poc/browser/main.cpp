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

// browser: the BrowserPS5 app, after milestones M1-M3 (poc/hello_page).
//
// Two WebKit views on one screen: the bar (chrome.html, a page of its own)
// over the top BAR_HEIGHT pixels, the page below. Both paint with Cairo
// into one ARGB32 buffer, each into its own rows (WKPagePaint), and a
// display thread hands that to sceVideoOut with the cursor drawn over it.
//
// The bar sends its commands as navigations to browserps5:<command>, which
// the app stops and carries out on the page; a timer gives the bar the
// page's state (URL, history, progress). What is typed in the bar goes
// through address.c: an address, or a search.
//
// The bar also keeps the favorites (a row under the address, ☆ to add the
// page); the app saves them in DATA_DIR and writes the page of favorites
// (favorites.html, opened by ⌂) next to them.
//
// With REMOTE_UI (the title's build) a third view, the keyboard
// (keyboard.html), is drawn over the bottom KEYS_HEIGHT pixels while a text
// field wants text: the system keyboard took the console down with the
// title drawing on the GPU. Its text goes into the field when it closes, as
// the system keyboard's did.
//
// The controller and the system keyboard are hello_page's (input.c,
// ime.c): left stick moves the cursor, right stick and D-pad scroll, cross
// clicks, circle or L1 goes back, R1 forward, triangle opens the keyboard
// (always for the address bar; a page's field opens it when clicked),
// square is Enter, options quits.
//
// Built with GPU_COMPOSITING (browser_gpu.elf; docs/03-video-e-gpu.md, G2),
// WebKit composites both views on the GPU instead, in the WebProcess, and
// hands each frame over in shared memory (poc/webkit/gpu/bps5_layer.h); the
// display thread copies the newest one of each view into its rows.
//
// The look and the smart parts (docs/07-navegacao-e-definicoes.md): two more
// views drawn as panels over the page, the menu (⋮, menu.html) and a dialog
// (dialog.html: where to keep a download, a folder for a setting); the
// browser's own pages in the page's view, the start page (start.html) and
// the settings (settings.html), written into DATA_DIR with their data in
// them; the settings (settings.c), applied to WebKit as they change; the
// history; the extensions, user scripts and the built-in ones (ext/); the
// downloads; the D-pad's way from element to element (nav.js) with a ring
// around the one it chose; the Windows cursors (cursors_win.h, made from the
// build machine's own) and the full screen for videos.

#define POC_NAME "browser"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/statvfs.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#ifdef GPU_COMPOSITING
#include <stddef.h>
#include <sys/mman.h>
#include "bps5_layer.h"
#include "video_ring.h"
#endif

#include <WebKit/WKBackForwardListItemRef.h>
#include <WebKit/WKBackForwardListRef.h>
#include <WebKit/WKContext.h>
#include <WebKit/WKContextPrivate.h>
#include <WebKit/WKContextConfigurationPlayStation.h>
#include <WebKit/WKContextConfigurationRef.h>
#include <WebKit/WKCookieManager.h>
#include <WebKit/WKDownloadClient.h>
#include <WebKit/WKDownloadRef.h>
#include <WebKit/WKErrorRef.h>
#include <WebKit/WKFrameInfoRef.h>
#include <WebKit/WKFramePolicyListener.h>
#include <WebKit/WKHTTPCookieStoreRef.h>
#include <WebKit/WKNavigationActionRef.h>
#include <WebKit/WKNavigationResponseRef.h>
#include <WebKit/WKNumber.h>
#ifdef APP_RN  // a site's own Notification, asked for and shown by the app (notify_native_*)
#include <WebKit/WKDictionary.h>
#include <WebKit/WKMutableDictionary.h>
#include <WebKit/WKNotification.h>
#include <WebKit/WKNotificationManager.h>
#include <WebKit/WKNotificationPermissionRequest.h>
#include <WebKit/WKNotificationProvider.h>
#include <WebKit/WKSecurityOriginRef.h>
#endif
#include <WebKit/WKPage.h>
#ifdef APP_RN  // the payload sender's socket (download_run_payload()), the services' (local_port_answers())
#include <arpa/inet.h>
#include <poll.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif
#ifdef APP_RN  // a page's file chooser (runOpenPanel)
#include <WebKit/WKArray.h>
#include <WebKit/WKOpenPanelParametersRef.h>
#include <WebKit/WKOpenPanelResultListener.h>
#include <WebKit/WKPageUIClient.h>
#endif
#ifdef ENGINE_PLUS
#include <WebKit/WKPagePrivate.h>
#include <WebKit/WKUserContentExtensionStoreRef.h>
#endif
#include <WebKit/WKPageConfigurationRef.h>
#include <WebKit/WKPageNavigationClient.h>
#include <WebKit/WKPagePrivate.h>
#include <WebKit/WKPagePrivatePlayStation.h>
#include <WebKit/WKPreferencesRef.h>
#include <WebKit/WKPreferencesRefPrivate.h>
#include <WebKit/WKRunLoop.h>
#include <WebKit/WKString.h>
#include <WebKit/WKType.h>
#include <WebKit/WKURL.h>
#include <WebKit/WKURLRequest.h>
#include <WebKit/WKURLResponse.h>
#include <WebKit/WKUserContentControllerRef.h>
#include <WebKit/WKUserScriptRef.h>
#include <WebKit/WKView.h>
#include <WebKit/WKViewClient.h>
#include <WebKit/WKWebsiteDataStoreConfigurationRef.h>
#include <WebKit/WKWebsiteDataStoreRef.h>

// libWebKit's process launcher (poc/webkit/process-launcher, exported as
// PlayStation::*): the title's pid while the WebProcess runs in it (2c-2).
namespace PlayStation {
#ifdef WEBKIT_BEFORE_2C2  // make WEBKIT_BEFORE_2C2=1: for a WebKit without it
inline int32_t titleWebProcessId() { return -1; }
#else
int32_t titleWebProcessId();
#endif
}

#include "poc_log.h"
#include "crash_log.h"
#include "address.h"
#include "display.h"
#include "ime.h"
#include "input.h"
#include "overlay.h"
#ifdef APP_RN
#include "title_link.h"  // the title's ports, listened for here when the title may not (a media title)
#include "ask_box.h"  // the window of a page's alert(), confirm() or prompt(), drawn here
// 2026-11-11 21:00: /data/homebrew/PPSA97024/bisect.txt, a few letters, each
// one thing of the day left out for that run (the pages had gone slow: which
// one did it?): p the three functions turned on by name, u WebGL and Web
// Audio's keys, l the languages, f the window's frame. No file: nothing off.
static bool
bisect_off(char letter) {
  static char letters[32];
  static bool read;
  if(!read) {
    read = true;
    FILE* f = fopen("/data/homebrew/" TITLE_ID "/bisect.txt", "r");
    if(f) {
      if(!fgets(letters, sizeof(letters), f)) {
        letters[0] = 0;
      }
      fclose(f);
    }
  }
  return strchr(letters, letter) != 0;
}
#endif
#include "settings.h"
#include "dl_rate.h"
#include "phone.h"
#include "files.h"
#include "session.h"
#include "system.h"
#include "shot.h"
// The build machine's Windows cursors, as C (Makefile, cur2c.c): kept out of
// the project, made again at each build. Without them, overlay.c's own.
#include "cursors_win.h"
#ifdef REMOTE_UI
#include "link.h"
#include "remote.h"
#include "side_card.h"
#ifdef APP_RN
#include "games.h"      // the games on the console, for the start page's "your game" row (2026-11-14)
#include "health.h"     // the console's temperatures, power, fan, drives (2026-11-15)
#include "share.h"      // a capture to a Discord channel by its webhook (2026-11-16)
#include "xps5/captures.h"  // the PS5's captures, the newest first (from XPS5)
#include "xps5/game.h"  // a game's hashtag (from XPS5)
#endif
#include "torrent.h"
#include "torbox.h"
#include "logins.h"
#include "unzip.h"
#else
enum { LINK_SOUND_MOVE, LINK_SOUND_OPEN, LINK_SOUND_CLOSE, LINK_SOUND_SELECT };
#endif

#if defined(ENGINE_PLUS) && !defined(ENGINE_DIR)
#define ENGINE_DIR "plus"
#endif
#ifdef ENGINE_DIR
// An engine variant's own processes, next to the usual ones (2026-11-11,
// browser-rn/pack-lib-rn.sh; make ENGINE_DIR=skia, or ENGINE_PLUS=1 for
// "plus"): its switches change the messages between the processes, so the
// UI's library (libWebKitPlus.so.SOVERSION, libWebKitSkia.so.SOVERSION), the
// network process and the WebProcesses must all be of that build, and v1's
// (and the normal RN's) stay where they are.
#define WEB_PROCESS_PATH    "/user/homebrew/browserps5-rn/" ENGINE_DIR "/WebProcess.elf"
#define NETWORK_PROCESS_PATH "/user/homebrew/browserps5-rn/" ENGINE_DIR "/NetworkProcess.elf"
#else
#define WEB_PROCESS_PATH    "/user/homebrew/browserps5/WebProcess.elf"
#define NETWORK_PROCESS_PATH "/user/homebrew/browserps5/NetworkProcess.elf"
#endif
#ifdef APP_RN  // 2026-11-10 22:50: with the tabs' row at the top (chrome.html body.strip)
#define BAR_HEIGHT           204  // chrome.html's #chrome
#define BAR_HEIGHT_NOFAV     148  // and without the favorites (setFavbar)
#else
#define BAR_HEIGHT           152  // chrome.html's #chrome
#define BAR_HEIGHT_NOFAV     100  // and without the favorites (setFavbar)
#endif
#define KEYS_HEIGHT          440  // keyboard.html's --height
#ifdef APP_RN  // keyboard-rn.css: the board in the middle, its width and its top corners
#define KEYS_BOARD_WIDTH     1180
#define KEYS_BOARD_LEFT      ((DISPLAY_WIDTH - KEYS_BOARD_WIDTH) / 2)
#define KEYS_BOARD_RADIUS    28
#endif
// The menu, under ⋮ at the bar's right end, and the dialog in the middle.
#define MENU_WIDTH           560
#define MENU_HEIGHT          966  // menu.html is 948 high (with "Abrir ao lado do jogo"); + 18, top 92: 1058, under 1080
#define MENU_LEFT            (DISPLAY_WIDTH - MENU_WIDTH - 18)
#define MENU_TOP             92
#define DIALOG_WIDTH         1240
#define DIALOG_HEIGHT        720
#define DIALOG_LEFT          ((DISPLAY_WIDTH - DIALOG_WIDTH) / 2)
#define DIALOG_TOP           150
#define PANEL_RADIUS         26
#define BAR_UPDATE_US        250000
// The magnet: this long after the stick stops, the cursor is drawn to the
// clickable thing nearest it (nav.js "near"; settings.magnet).
#define MAGNET_IDLE_MS       160.0
#define CLOSE_WAIT_US        1500000  // pages closed, before the app exits
// The NetworkProcess writes cookies and storage here (WebKit makes the
// directories); the app its favorites, and the page that shows them.
#ifdef DEMO  // the video's tour (demo_thread()): its own, none of the user's
#define DATA_DIR             "/user/homebrew/browserps5/demo-data"
#elif defined(APP_RN)  // BrowserPS5 RN (PPSA97024): its own logins, cookies, settings
#define DATA_DIR             "/user/homebrew/browserps5-rn/data"
#else
#define DATA_DIR             "/user/homebrew/browserps5/data"
#endif
#define TABS_DIR             DATA_DIR "/tabs"  // the tabs' pictures
#define FAVORITES_FILE       DATA_DIR "/favorites.json"
#define FAVORITES_MAX_BYTES  (256 * 1024)
#define SETTINGS_FILE        DATA_DIR "/settings.txt"
#define LOGINS_FILE          DATA_DIR "/logins.txt"  // the saved logins (logins.h), 0600
#define TORBOX_KEY_FILE      DATA_DIR "/torbox-key"  // the user's TorBox key (torbox.h), 0600
#define HISTORY_FILE         DATA_DIR "/history.tsv"
#define SESSION_FILE         DATA_DIR "/last-tabs.txt"  // the tabs of the last time (session.h)
#define SESSION_OPEN_FILE    DATA_DIR "/last-tabs.open"  // there while those reopened at the start are new
#define SESSION_OPEN_MS      90000  // how long they must last to be reopened by themselves again
#define EXTENSIONS_DIR       DATA_DIR "/extensions"
#define WEBKIT_TEMP_DIR      "/user/homebrew/browserps5/data/tmp"  // WTF's (webkit/storage)
#define CACHE_DIR            DATA_DIR "/cache"  // the pages' HTTP cache (docs/21), WebKit's own files
#define PARTIAL_DIR          DATA_DIR "/partial"  // downloads on their way
#define PARTIAL_NEAR         ".parcial"  // RN: the same inside a download's own folder, when that is on another drive
#define START_PAGE           DATA_DIR "/start.html"
#define START_URL            "file://" START_PAGE
#define SETTINGS_PAGE        DATA_DIR "/settings.html"
#define SETTINGS_URL         "file://" SETTINGS_PAGE

// make HOME_URL=https://... starts there whatever the settings say (the
// tests); otherwise the settings' home page, or the last page.
#ifdef HOME_URL
#define HOME_URL_GIVEN       1
#else
#define HOME_URL_GIVEN       0
#define HOME_URL             "https://www.wikipedia.org/"
#endif
#define BROWSER_VERSION      "0.7 (" __DATE__ ")"
// make RUN_SECONDS=n; the menu's Sair (options, then the last row) quits earlier.
#ifndef RUN_SECONDS
#if defined(APP_RN) && !defined(SPEED_TEST)
// BrowserPS5 RN (2026-11-11): no limit. The tests' 600 s closed the browser
// under the user after 10 minutes (the app left by itself, then the system's
// error dialog): found with RN left open at the home page, 00:26.
#define RUN_SECONDS          0
#else
#define RUN_SECONDS          600
#endif
#endif
// make MOUSE_MOVE_MS=n: the page gets a mouse move at most every n ms (the
// cursor on the screen still moves at every poll). Every move runs the
// page's hover handlers: on the TV test of 2026-11-04 the YouTube video
// stuttered while the cursor moved. 0: every move.
#ifndef MOUSE_MOVE_MS
#define MOUSE_MOVE_MS        0
#endif
// make CURSOR_TEST=1 (with MEDIA_PROBE): from CURSOR_TEST_START s on, the
// cursor goes round over the page for CURSOR_TEST_SECONDS, then stays still
// for CURSOR_STILL_SECONDS, CURSOR_TEST_ROUNDS times, with the times in the
// log, to compare the video's frames while it moves and while it does not.
// YouTube's player gave up 47 s into the video in three runs (16:35, 16:44,
// V3), and its video is nearly still until ~35 s after the start, so the
// windows are short. YouTube shows its controls until ~3 s after the last
// move: the still windows are longer than that.
#ifndef CURSOR_TEST_START
#define CURSOR_TEST_START    35
#endif
#ifndef CURSOR_TEST_SECONDS
#define CURSOR_TEST_SECONDS  4
#endif
#ifndef CURSOR_STILL_SECONDS
#define CURSOR_STILL_SECONDS 8
#endif
#define CURSOR_TEST_ROUNDS   2

// make VIDEO_TEST=<clip URL> (docs/03-video-e-gpu.md, V2; WebKit built with
// MEDIA=1): instead of HOME_URL, a page with only a muted <video> of that
// clip, over and over, which writes into its title how far it got, which
// of YouTube's types MediaSource takes and its last few events (with the time
// at each); LOG_TITLE logs every title. At 2.5 s it seeks back to 1 s once,
// as someone would. Muted, so nothing is heard on the TV.
#ifdef VIDEO_TEST
#define LOG_TITLE
#ifndef VIDEO_TEST_STALL_MS
static const char video_test_html[] = R"(<!doctype html>
<meta charset="utf-8">
<body style="margin:0;background:#000">
<video id="v" src="%s" autoplay muted playsinline
       style="width:100vw;height:100vh"></video>
<script>
const v = document.getElementById('v');
const t0 = performance.now();
const types = {avc1: 'video/mp4; codecs="avc1.4d401f"',
               mp4a: 'audio/mp4; codecs="mp4a.40.2"',
               vp9: 'video/webm; codecs="vp9"',
               opus: 'audio/webm; codecs="opus"',
               av01: 'video/mp4; codecs="av01.0.05M.08"'};
const mse = window.MediaSource ? Object.keys(types).map(
    k => k + '=' + (MediaSource.isTypeSupported(types[k]) ? 1 : 0)).join(',')
    : 'none';
const trail = [];
let ends = 0, seekedOnce = false;
function show(what) {
  const q = v.getVideoPlaybackQuality ? v.getVideoPlaybackQuality() : {};
  document.title = 'video ' + what + ': t=' + v.currentTime.toFixed(2) +
      ' ready=' + v.readyState + ' net=' + v.networkState + ' ' +
      v.videoWidth + 'x' + v.videoHeight + ' frames=' + q.totalVideoFrames +
      ' dropped=' + q.droppedVideoFrames + ' error=' +
      (v.error ? v.error.code + ' ' + v.error.message : 'none') +
      ' mse=' + mse + ' ends=' + ends + ' [' + trail.join(' ') + '] at ' +
      Math.round(performance.now() - t0) + ' ms';
}
for (const e of ['loadedmetadata', 'canplay', 'playing', 'ended', 'error',
                 'stalled', 'waiting', 'seeking', 'seeked', 'play', 'pause'])
  v.addEventListener(e, () => {
    trail.push(e + '@' + v.currentTime.toFixed(2) + (v.seeking ? '*' : ''));
    if (trail.length > 10)
      trail.shift();
    show(e);
  });
v.addEventListener('timeupdate', () => {
  if (!seekedOnce && v.currentTime > 2.5) {
    seekedOnce = true;
    v.currentTime = 1;
  }
});
v.addEventListener('ended', () => { ends++; v.currentTime = 0; v.play(); });
setInterval(() => show('tick'), 1000);
show('start');
</script>
)";
#endif

// make VIDEO_TEST=<clip URL> VIDEO_TEST_STALL_MS=<ms> (docs/08-4k.md, "O
// vídeo pára com o scroll"): the clip through MediaSource, as YouTube's
// player does (appended whole, in 2 MiB pieces), and every STALL_EVERY_MS
// the page's main thread busy for VIDEO_TEST_STALL_MS, as scrolling under
// a video keeps it busy on ~2 cores. The title (in the log) says how
// many pictures came late; so does dropped= here. On the loop at the end it
// starts again at 0. Muted. No "%" in the page but these three (snprintf).
#ifdef VIDEO_TEST_STALL_MS
#define STALL_EVERY_MS 8000
// make VIDEO_TEST_AUDIO=<sound clip URL> too (2026-11-05 15:44): a second
// SourceBuffer with the sound, as YouTube has, then the end of the stream
// after both: does "ended" come (the next video never started).
#ifndef VIDEO_TEST_AUDIO
#define VIDEO_TEST_AUDIO ""
#endif
static const char video_test_mse_html[] = R"(<!doctype html>
<meta charset="utf-8">
<body style="margin:0;background:#000">
<video id="v" muted playsinline style="width:100vw;height:100vh"></video>
<script>
const CLIP = '%s', SOUND = '%s', STALL = %d, EVERY = %d;
const v = document.getElementById('v');
const t0 = performance.now();
const trail = [];
let ends = 0, stalls = 0, appended = 0, phase = 'start', stalling = false, waiting = SOUND ? 2 : 1;
function show(what) {
  const q = v.getVideoPlaybackQuality ? v.getVideoPlaybackQuality() : {};
  document.title = 'video ' + what + ': t=' + v.currentTime.toFixed(2) +
      ' dur=' + v.duration.toFixed(2) +
      ' ready=' + v.readyState + ' ' + v.videoWidth + 'x' + v.videoHeight +
      ' frames=' + q.totalVideoFrames + ' dropped=' + q.droppedVideoFrames +
      ' error=' + (v.error ? v.error.code + ' ' + v.error.message : 'none') +
      ' mse=' + phase + ' appended=' + appended + ' stalls=' + stalls +
      ' ends=' + ends + ' [' + trail.join(' ') + '] at ' +
      Math.round(performance.now() - t0) + ' ms';
}
for (const e of ['loadedmetadata', 'canplay', 'playing', 'ended', 'error',
                 'stalled', 'waiting', 'seeking', 'seeked', 'play', 'pause'])
  v.addEventListener(e, () => {
    trail.push(e + '@' + v.currentTime.toFixed(2) + (v.seeking ? '*' : ''));
    if (trail.length > 10)
      trail.shift();
    show(e);
  });
v.addEventListener('ended', () => { ends++; v.currentTime = 0; v.play(); });
v.addEventListener('playing', () => {
  if (stalling) return;
  stalling = true;
  setInterval(() => {
    const before = v.getVideoPlaybackQuality().droppedVideoFrames;
    const start = performance.now();
    while (performance.now() - start < STALL) {}
    stalls++;
    show('stall' + stalls);
    setTimeout(() => show('after-stall' + stalls + ' (+' +
        (v.getVideoPlaybackQuality().droppedVideoFrames - before) + ' dropped)'), 1500);
  }, EVERY);
});
const ms = new MediaSource();
v.src = URL.createObjectURL(ms);
// Both appended (the sound's, with SOUND, as YouTube's player has two):
// the end of the stream, then play.
function appendedAll() {
  if (--waiting) return;
  if (ms.readyState === 'open') ms.endOfStream();
  phase = 'appended';
  show('appended');
  v.play();
}
ms.addEventListener('sourceopen', () => {
  phase = 'open';
  const sb = ms.addSourceBuffer('video/webm; codecs="vp9"');
  if (SOUND) {
    const ab = ms.addSourceBuffer('audio/webm; codecs="opus"');
    const y = new XMLHttpRequest();
    y.open('GET', SOUND);
    y.responseType = 'arraybuffer';
    y.onload = () => {
      ab.addEventListener('updateend', appendedAll, {once: true});
      ab.addEventListener('error', () => { phase = 'sound-error'; show('sound-error'); });
      ab.appendBuffer(new Uint8Array(y.response || new ArrayBuffer(0)));
    };
    y.onerror = () => { phase = 'sound-xhr-error'; show('sound-xhr-error'); };
    y.send();
  }
  const x = new XMLHttpRequest();
  x.open('GET', CLIP);
  x.responseType = 'arraybuffer';
  x.onload = () => {
    const data = new Uint8Array(x.response || new ArrayBuffer(0));
    phase = 'fetched' + data.length;
    let at = 0;
    const next = () => {
      if (at >= data.length) {
        sb.removeEventListener('updateend', next);
        appendedAll();
        return;
      }
      const end = Math.min(at + 2 * 1024 * 1024, data.length);
      sb.appendBuffer(data.subarray(at, end));
      appended = end;
      at = end;
    };
    sb.addEventListener('updateend', next);
    sb.addEventListener('error', () => { phase = 'append-error'; show('append-error'); });
    next();
  };
  x.onerror = () => { phase = 'xhr-error'; show('xhr-error'); };
  x.send();
});
setInterval(() => show('tick'), 1000);
show('start');
</script>
)";
#endif
#endif

// Desktop Safari's: the port's own "(PlayStation; ...)" gets the basic
// pages made for TV consoles (hello_page, M2). "Versão para computador" off:
// an iPad's, which gets the sites' pages for touch screens.
#define USER_AGENT "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) " \
                   "AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.0 " \
                   "Safari/605.1.15"
#define USER_AGENT_MOBILE "Mozilla/5.0 (iPad; CPU OS 18_0 like Mac OS X) " \
                   "AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.0 " \
                   "Mobile/15E148 Safari/604.1"

// The browser's own pages and scripts, by pages.S (the pages with theme.css
// and icons.svg put in, include.awk).
extern "C" const char chrome_html[];
extern "C" const char keyboard_html[];
extern "C" const char menu_html[];
extern "C" const char dialog_html[];
extern "C" const char radial_html[];
extern "C" const char tabs_html[];
extern "C" const char start_html[];
extern "C" const char settings_html[];
extern "C" const char phone_html[];
static settings_t settings;
static void phone_command(const char* argument);
static void phone_setting(void);
static void phone_page_changed(void);
static void phone_state_note(void);
static void phone_downloads_note(void);
static void system_tell(void);
static void game_offer(void);
#ifdef APP_RN
static void padkeys_toggle(void);  // the menu's "Comando como teclado" (2026-11-15)
#endif
static bool media_shown(WKPageRef p);
static bool url_is_web(const char* url);
static void devtools_toggle(void);
#ifdef APP_RN
static bool settings_tab_open(const char* url);
static bool settings_tab_back(void);
#endif
static void game_set(bool on);
static bool game_input(const input_event_t* e);
static bool game_wanted;  // the page uses the Gamepad API
static void vibrate_command(const char* argument);
static void ad_allow_command(const char* argument);
static void bar_notify(const char* text, const char* icon);
#ifdef APP_RN
static bool url_this_console(const char* url);  // (an address of this console's own services: site_may_open())
#endif
extern "C" const char nav_js[];
extern "C" const char ext_gm_js[];
extern "C" const char ext_adblock_js[];
extern "C" const char ext_dark_js[];
extern "C" const char ext_fullscreen_js[];
extern "C" const char ext_reader_js[];  // BrowserPS5 RN: the reading mode (ui_command "reader")
extern "C" const char ext_linkmenu_js[];  // BrowserPS5 RN: cross held on a link or a picture (extensions_apply())
extern "C" const char ext_gamepad_js[];
extern "C" const char ext_devtools_js[];
extern "C" const char ext_manager_js[];
extern "C" const char ext_logins_js[];
extern "C" const char ext_adhosts_txt[];

// MENU and DIALOG: panels over the page, shown only while open
// (app.menu_open, app.dialog_open); KEYS: the keyboard, over everything
// while it is open (app.keys_open); RADIAL: L2's and R2's round menus, in
// the middle of the screen while open (app.radial_open); SWITCHER: the tabs'
// carousel, over the whole screen while open (app.switcher_open).
enum { BAR, PAGE, MENU, DIALOG, KEYS, RADIAL, SWITCHER, PANES };

typedef struct pane {
  const char* name;
  WKViewRef view;
  int left;
  int top;
  int width;
  int height;
  uint32_t* pixels;   // where its rows go: the screen's, or its own buffer
  int stride;         // pixels from a row to the next
  WKRect damage;      // not painted yet
  bool paint_pending;
  bool redraw;        // its last frame goes into its rows again (layout())
  // GPU_COMPOSITING: the page's ID in the WebProcess and that process's
  // pid (set on the main thread), and on the display thread the frames
  // that ID names.
  uint64_t page_id;
  int32_t web_pid;
  int32_t last_web_pid;  // the last one that was not 0, for after a crash
  uint64_t layer_id;
  void* layer;        // bps5_layer_t
  size_t layer_bytes; // mapped: its size when it was opened
  unsigned front;
  double next_open_ms;
  // Until then the layer is looked for every 50 ms, not 500: a tab just
  // brought to the front, whose WebProcess gave its layer back while it was
  // hidden and makes it again at its first frame (bps5_layer.h).
  double open_soon_ms;
  // The frames taken from the layer (under app.lock): how many, when the
  // last came, and the longest wait between two since the media probe last
  // read it. What the TV shows of the page, unlike the <video>'s count.
  unsigned taken;
  double taken_ms;
  double max_gap_ms;
} pane_t;

typedef struct app {
  pthread_mutex_t lock;
  pthread_cond_t  cond;
  uint32_t* screen;   // what WKPagePaint fills: the bar's rows, the page's
  uint32_t* frame;    // the copy the display thread shows
  // The keyboard's rows, which the display thread draws over the page's
  // while it is open and has drawn itself since it opened (under the lock).
  uint32_t* keys;
  bool keys_open;
  bool keys_fresh;
  // The menu's and the dialog's own pixels, as the keyboard's.
  uint32_t* menu;
  uint32_t* dialog;
  bool menu_open, menu_fresh;
  bool dialog_open, dialog_fresh;
  // The radial menus' (radial.html): open; armed once its show() has run,
  // fresh from its next frame on (no frame of the ring before), which
  // starts the fade in at radial_shown_ms.
  uint32_t* radial;
  bool radial_open, radial_armed, radial_fresh;
  double radial_shown_ms;
  // The tabs' switcher's (tabs.html), the same way, over the whole screen.
  uint32_t* switcher_px;
  bool switcher_open, switcher_armed, switcher_fresh;
  double switcher_shown_ms;
  // The element the D-pad chose (nav.js): its box on the screen, and the
  // pane it is in. on: the ring shows (under the lock, as all of this).
  // The ring glides there from where it was (from_*, since start_ms), or
  // closes in on it from a little larger when it appears.
  struct {
    bool on;
    int pane;
    int x, y, w, h;
    float from_x, from_y, from_w, from_h;
    float shown_x, shown_y, shown_w, shown_h;
    double start_ms;
    bool appearing;
  } ring;
  uint32_t accent;    // the theme's colour, the ring's
  int cursor_type;    // WebKit's, from setCursor
  bool editable;      // and with an I-beam, a text field under it
  bool fullscreen;    // a video covers the screen, no bar (ext/fullscreen.js)
  bool native_fullscreen;  // WebKit's own full screen holds it (view_enter_fullscreen)
  bool game;          // a web game has the controller (docs/07, "Modo jogo")
  bool aim_shown;     // RN: in it, the right stick moved the pointer (aim_step()): it is drawn
  bool cursor_off;    // not drawn: a game, the cinema's rest, or the D-pad driving the page (display_thread)
  bool keys_drive;    // the D-pad's arrow keys drive the page (dpad_keys_now()) and the stick has not moved since
  double input_ms;    // the controller last did something (not a game's)
  int bar_height;     // BAR_HEIGHT, BAR_HEIGHT_NOFAV or 0 (full screen)
  bool dirty;
  bool quit;
  bool frozen;        // the screenshot is being taken from screen
  pane_t panes[PANES];
  int focus;          // the pane with the keyboard focus
  int pressed;        // the pane cross went down on, or -1
  int hover;          // the pane the cursor is over
  int ime_pane;       // where the keyboard's text goes
  bool system_ime;    // RN: the system's keyboard was asked for ime_pane (open_keyboard())
  bool ime_held;      // its text came while the pages were paused: it goes in when they are back
  char* ime_held_text;
  bool ime_replace;   // the keyboard's text replaces the field's
  bool ime_enter;     // and is followed by Enter
  bool hand_cursor;   // over a link
  bool swallow_up;    // the press closed the menu: its release goes nowhere
  bool nav_fresh;     // the cursor moved since the D-pad last chose
  double stick_ms;    // the stick last moved the cursor (the magnet)
  double zoom;        // the page's, 1 = 100%
  bool loading;
  char bar_state[16384];  // the last update() the bar got
  unsigned paints;
  unsigned frames;
  unsigned missed_flips;
  double start_ms;
  double first_paint_ms;
} app_t;

static app_t app = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER};


// A frame of the radial menus' view came (app.lock held): once their show()
// has run, the ring is in it, and it fades in from now.
static void
radial_painted(void) {
  if(app.radial_open && app.radial_armed && !app.radial_fresh) {
    app.radial_fresh = true;
    app.radial_shown_ms = poc_now_ms();
  }
}


static void
switcher_painted(void) {
  if(app.switcher_open && app.switcher_armed && !app.switcher_fresh) {
    app.switcher_fresh = true;
    app.switcher_shown_ms = poc_now_ms();
  }
}


static WKPageRef
page(int pane) {
  return WKViewGetPage(app.panes[pane].view);
}


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
 * Runs on the main thread, after setViewNeedsDisplay has returned. A pane's
 * rows are a surface of their own: the stride is the screen's width either
 * way.
 **/
static void
paint(void* arg) {
  pane_t* pane = (pane_t*)arg;
  WKRect rect = pane->damage;
  double t0 = poc_now_ms();

  pane->damage = WKRectMake(0, 0, 0, 0);
  pane->paint_pending = false;

  pthread_mutex_lock(&app.lock);
  if(pane == &app.panes[BAR] && app.fullscreen) {  // the page has its rows
    pthread_mutex_unlock(&app.lock);
    return;
  }
  WKPagePaint(WKViewGetPage(pane->view), (unsigned char*)pane->pixels,
              WKSizeMake(pane->width, pane->height), rect);
  if(pane == &app.panes[KEYS]) {
    app.keys_fresh = true;
  } else if(pane == &app.panes[MENU]) {
    app.menu_fresh = true;
  } else if(pane == &app.panes[DIALOG]) {
    app.dialog_fresh = true;
  } else if(pane == &app.panes[RADIAL]) {
    radial_painted();
  } else if(pane == &app.panes[SWITCHER]) {
    switcher_painted();
  }
  app.dirty = true;
  app.paints++;
  pthread_cond_signal(&app.cond);
  pthread_mutex_unlock(&app.lock);

  if(app.first_paint_ms < 0) {
    app.first_paint_ms = t0 - app.start_ms;
    POC_LOG("first paint (%s) %.0f ms after start, WKPagePaint %.2f ms",
            pane->name, app.first_paint_ms, poc_now_ms() - t0);
  }
}


[[maybe_unused]] static void
setViewNeedsDisplay(WKViewRef, WKRect rect, const void* info) {
  pane_t* pane = (pane_t*)info;

  pane->damage = rect_union(pane->damage, rect);
  if(!pane->paint_pending) {
    pane->paint_pending = true;
    WKRunLoopCallOnMainThread(paint, pane);
  }
}


// WebKit says which cursor the page wants: a hand over links, an I-beam
// over text. Ours is the hand over a text field too (editable_check()), and
// while the D-pad's ring shows, the arrow everywhere else.
#define CURSOR_TYPE_IBEAM 3  // WebCore's Cursor::Type: Pointer, Cross, Hand, IBeam

static void editable_check(void);

static void
setCursor(WKViewRef, WKCursorType type, const void*) {
  pthread_mutex_lock(&app.lock);
  bool ibeam = type == CURSOR_TYPE_IBEAM && app.cursor_type != CURSOR_TYPE_IBEAM;
  app.cursor_type = type;
  app.hand_cursor = type == kWKCursorTypeHand;
  if(type != CURSOR_TYPE_IBEAM) {
    app.editable = false;
  }
  app.dirty = true;
  pthread_cond_signal(&app.cond);
  pthread_mutex_unlock(&app.lock);
  if(ibeam) {
    editable_check();
  }
}


// The arrow and the hand: the build machine's Windows cursors with a soft
// shadow, or overlay.c's own.
static cursor_image_t cursor_images[2];

static void
cursors_init(void) {
#if defined(CURSORS_WIN) && !defined(DEMO)  // the video is published: only ours there
  if(!cursor_with_shadow(&cursor_images[0], cursor_arrow_width, cursor_arrow_height,
                         cursor_arrow_hot_x, cursor_arrow_hot_y, cursor_arrow_pixels)
     && !cursor_with_shadow(&cursor_images[1], cursor_hand_width, cursor_hand_height,
                            cursor_hand_hot_x, cursor_hand_hot_y, cursor_hand_pixels)) {
    POC_LOG("cursors: Windows's, %dx%d", cursor_arrow_width, cursor_arrow_height);
    return;
  }
#endif
  cursor_make(&cursor_images[0], CURSOR_ARROW, 0xffffff);
  cursor_make(&cursor_images[1], CURSOR_HAND, 0xffffff);
  POC_LOG("cursors: overlay.c's own");
}


#ifdef APP_RN
/* BrowserPS5 RN (2026-11-11; BrowserPS5 NK's, shell.c 2026-11-09): the neon
 * ball that chases the cursor. A soft light in the theme's colour, not stuck
 * to the cursor but pulled by a spring: after a fast move it lags behind,
 * catches up a little past and settles. Under the cursor; brighter the
 * faster it goes. Still, it costs nothing: only while it moves does it ask
 * for another picture. Settings, "Luz néon do cursor" (cursor_glow). */
#define GLOW_R 46  // the light's radius in pixels
#define GLOW_D (2 * GLOW_R + 1)
#define GLOW_K 170.0     // the spring (about 13 rad/s)
#define GLOW_DAMP 0.62   // under 1: a little past, then back
#define GLOW_OX 5        // its middle a little into the arrow, not on its tip
#define GLOW_OY 8
static double glow_x = -1, glow_y, glow_vx, glow_vy, glow_ms;
static uint32_t glow_accent = 0xffffffffu;  // the colour the light was made in (none yet)
static uint8_t glow_a[GLOW_D * GLOW_D];
static uint32_t glow_c[GLOW_D * GLOW_D];

static int
glow_round(double v) {
  return v < 0 ? -(int)(0.5 - v) : (int)(v + 0.5);
}

// The light in the accent: whiter in the middle, a wide soft glow around.
static void
glow_make(uint32_t accent) {
  for(int y = 0; y < GLOW_D; y++) {
    for(int x = 0; x < GLOW_D; x++) {
      const float dx = (float)(x - GLOW_R), dy = (float)(y - GLOW_R), r2 = dx * dx + dy * dy;
      const float halo = 0.55f * __builtin_expf(-r2 / (2.0f * 17.0f * 17.0f));
      const float mid = 0.60f * __builtin_expf(-r2 / (2.0f * 8.0f * 8.0f));
      const float hot = 0.90f * __builtin_expf(-r2 / (2.0f * 3.6f * 3.6f));
      float a = halo + mid * (1 - halo);
      a = a + hot * (1 - a);
      const float edge = r2 >= (float)(GLOW_R * GLOW_R) ? 0.0f : 1.0f - r2 / (float)(GLOW_R * GLOW_R);
      a *= edge < 0.25f ? edge * 4.0f : 1.0f;  // nothing cut at the square's edge
      float w = (hot + 0.5f * mid) / (halo + mid + hot + 1e-6f);
      w = w > 1 ? 1 : w;
      uint32_t rgb = 0;
      for(int k = 0; k < 24; k += 8) {
        const float ca = (float)((accent >> k) & 0xff), cc = ca + (255 - ca) * 0.85f;  // the core: nearly white
        rgb |= (uint32_t)(ca + (cc - ca) * w + 0.5f) << k;
      }
      glow_a[y * GLOW_D + x] = (uint8_t)(a * 255.0f + 0.5f);
      glow_c[y * GLOW_D + x] = rgb;
    }
  }
  glow_accent = accent;
}

// One step of the chase and the light on the frame, under the cursor still
// to be drawn. True while it is on its way: another picture.
static bool
glow_draw(uint32_t* frame, uint32_t accent) {
  float cx, cy;

  if(!settings.cursor_glow) {
    glow_x = -1;  // starts again at the cursor when turned back on
    return false;
  }
  input_cursor(&cx, &cy);
  const double now = poc_now_ms();
  bool moving = false;
  if(glow_x < 0) {
    glow_x = cx;
    glow_y = cy;
    glow_vx = glow_vy = 0;
  } else {
    double dt = (now - glow_ms) / 1000.0;
    dt = dt > 0.05 ? 0.05 : dt < 0 ? 0 : dt;
    const double c = 2.0 * GLOW_DAMP * __builtin_sqrt(GLOW_K), h = dt / 4;
    for(int i = 0; i < 4; i++) {  // small steps: the same at any frame time
      glow_vx += (GLOW_K * (cx - glow_x) - c * glow_vx) * h;
      glow_vy += (GLOW_K * (cy - glow_y) - c * glow_vy) * h;
      glow_x += glow_vx * h;
      glow_y += glow_vy * h;
    }
    const double ex = cx - glow_x, ey = cy - glow_y;
    if(ex * ex + ey * ey < 0.09 && glow_vx * glow_vx + glow_vy * glow_vy < 25.0) {  // there: still
      glow_x = cx;
      glow_y = cy;
      glow_vx = glow_vy = 0;
    } else {
      moving = true;
    }
  }
  glow_ms = now;
  if(accent != glow_accent) {
    glow_make(accent);
  }
  const double v2 = glow_vx * glow_vx + glow_vy * glow_vy;  // brighter the faster it goes (80% at rest)
  const unsigned strength = v2 >= 1500.0 * 1500.0 ? 256 : (unsigned)(205.0 + 51.0 * __builtin_sqrt(v2) / 1500.0);
  light_draw(frame, DISPLAY_WIDTH, DISPLAY_HEIGHT, glow_a, glow_c, GLOW_D,
             glow_round(glow_x) + GLOW_OX - GLOW_R, glow_round(glow_y) + GLOW_OY - GLOW_R, strength);
  return moving;
}
#endif


// Over the frame, not the screen buffer: that one is WebKit's.
static void
draw_cursor(uint32_t* frame, bool hand) {
  float fx, fy;

  input_cursor(&fx, &fy);
  cursor_draw(frame, DISPLAY_WIDTH, DISPLAY_HEIGHT, &cursor_images[hand ? 1 : 0],
              (int)fx, (int)fy);
}


static bool
cursor_is_hand(void) {  // app.lock held
  return app.hand_cursor || app.editable || app.ring.on;
}


// The panels' rounded corners and shadows, made once (main()); the radial
// menus' is round.
static panel_t menu_panel, dialog_panel, radial_panel;

// The radial menus: a disc of RADIAL_SIZE in the middle of the screen, a
// little of the page through it (RADIAL_GLASS of 255), the screen under it
// darker (RADIAL_DIM); both come in over RADIAL_FADE_MS.
#define RADIAL_SIZE      720
#define RADIAL_LEFT      ((DISPLAY_WIDTH - RADIAL_SIZE) / 2)
#define RADIAL_TOP       ((DISPLAY_HEIGHT - RADIAL_SIZE) / 2)
#define RADIAL_GLASS     238
#define RADIAL_DIM       120
#define RADIAL_FADE_MS   130.0
#define SWITCHER_FADE_MS 150.0  // the tabs' switcher comes in over the screen


// The ring's glide from one element to the next and its close-in when it
// appears (ms); while one runs the display thread makes a frame each flip.
#define RING_GLIDE_MS   140.0
#define RING_APPEAR_MS  180.0
#define RING_APPEAR_PX  14.0f

// Where the ring is drawn now, and how strong; true while it still moves.
// app.lock held.
static bool
ring_now(float* x, float* y, float* w, float* h, float* strength) {
  double span = app.ring.appearing ? RING_APPEAR_MS : RING_GLIDE_MS;
  double t = (poc_now_ms() - app.ring.start_ms) / span;
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  float e = (float)(1 - (1 - t) * (1 - t) * (1 - t));  // ease out
  *x = app.ring.from_x + (app.ring.x - app.ring.from_x) * e;
  *y = app.ring.from_y + (app.ring.y - app.ring.from_y) * e;
  *w = app.ring.from_w + (app.ring.w - app.ring.from_w) * e;
  *h = app.ring.from_h + (app.ring.h - app.ring.from_h) * e;
  *strength = app.ring.appearing ? e : 1;
  app.ring.shown_x = *x;
  app.ring.shown_y = *y;
  app.ring.shown_w = *w;
  app.ring.shown_h = *h;
  return t < 1;
}


#ifdef GPU_COMPOSITING
// The title's video (docs/08-4k.md, D3): WebKit leaves the page transparent
// where a video goes, and says where in the page's pixels; the title needs
// the page's place on the screen too, which this gives it with each frame
// (app.lock held). Its ring is opened when the title has made it (once a
// second until then), and again if it made it anew.
static video_ring_t* video_ring;
static double video_ring_next_ms;
// 2026-11-14: what
// this process maps of the TITLE's memory. The title is the application: what it makes with shm_open (its
// video ring; the frames of the WebProcess that runs in it, /bps5layer-N) is its memory, and the system
// takes it all back when it closes the title. This process is a payload, which the system does not close,
// and it had those mapped and read them at every frame. The history fits that: a close from the system
// went well twice on 2026-11-04, when the only thing of the title's a payload had mapped was ended by the
// pause first; since the title has the video ring, which this process maps for the page's place, every
// close from outside took the console down (docs/08-4k.md), and so did the title's own exit after its
// WebProcess crashed, when it left before this process did. So while the system is over the title
// (pause_pages()), and as soon as the title's link closes, none of it is mapped here (title_memory_let_go());
// back, it is opened again as at the start. With app.lock held.
static bool title_memory_off;
// (2026-11-14 18:3x) Set by main() at its end, before it shuts the title's link itself: the link's close is
// then this process's own doing (on_title_closed()).
static int main_ending;

static void
video_page_publish(void) {
  const size_t header = offsetof(video_ring_t, slots);

  if(title_memory_off) {
    return;
  }
  if(video_ring && !video_ring_ready(video_ring)) {
    munmap(video_ring, header);
    video_ring = 0;
  }
  if(!video_ring) {
    double now = poc_now_ms();
    if(now < video_ring_next_ms) {
      return;
    }
    video_ring_next_ms = now + 1000;
    int fd = shm_open(VIDEO_RING_SHM, O_RDWR, 0);
    if(fd < 0) {
      return;
    }
    void* map = mmap(0, header, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if(map == MAP_FAILED) {
      return;
    }
    if(!video_ring_ready((video_ring_t*)map)) {
      munmap(map, header);
      return;
    }
    video_ring = (video_ring_t*)map;
    POC_LOG("video: the title's video ring is there (up to %ux%u@%u)", video_ring->max_width,
            video_ring->max_height, video_ring->max_fps);
  }
  const pane_t* p = &app.panes[PAGE];
  const int32_t place[4] = {p->left, p->top, p->width, p->height};
  int32_t was[4];
  video_ring_get4(&video_ring->page_seq, video_ring->page, was);
  if(memcmp(was, place, sizeof(place))) {
    video_ring_put4(&video_ring->page_seq, video_ring->page, place);
  }
}
#endif


// The screen as the TV shows it but for the cursor, into out, with app.lock
// held (the display thread changes the panes' pixels under it): the views,
// then what is over them. True while something on it moves by itself (the
// ring gliding), so the next flip needs a frame too. The page's pixels keep
// their alpha (transparent where the title puts a video), and what goes
// over them keeps it as it should (overlay.c).
#ifdef APP_RN
// BrowserPS5 RN (2026-11-11): the window of a page's alert(), confirm() or
// prompt() as a picture of the app's own (ask_box.h), over everything, the
// frame dimmed under it. With app.lock.
static struct {
  bool open;
  ask_box_t box;
  double shown_ms;
  bool has_value;      // prompt(): OK gives value
  char value[1024];
} ask_native;
#endif

static bool
compose(uint32_t* out) {
  bool moving = false;
#ifdef GPU_COMPOSITING
  video_page_publish();
#endif
  memcpy(out, app.screen, DISPLAY_WIDTH * DISPLAY_HEIGHT * 4);
  bool dialog = app.dialog_open && app.dialog_fresh;
  if(dialog) {
    frame_dim(out, (size_t)DISPLAY_WIDTH * DISPLAY_HEIGHT);
  }
  if(app.menu_open && app.menu_fresh) {
    const pane_t* p = &app.panes[MENU];
    panel_draw(out, DISPLAY_WIDTH, DISPLAY_HEIGHT, &menu_panel, p->left, p->top,
               p->pixels, p->stride);
  }
  if(dialog) {
    const pane_t* p = &app.panes[DIALOG];
    panel_draw(out, DISPLAY_WIDTH, DISPLAY_HEIGHT, &dialog_panel, p->left, p->top,
               p->pixels, p->stride);
  }
  if(app.ring.on) {
    const pane_t* p = &app.panes[app.ring.pane];
    float x, y, w, h, strength;
    moving = ring_now(&x, &y, &w, &h, &strength);
    ring_draw(out, DISPLAY_WIDTH, DISPLAY_HEIGHT, x, y, w, h, p->top, p->top + p->height,
              app.accent, strength);
  }
  if(app.radial_open && !app.radial_fresh) {
    // Its frame is late, or did not change: after 300 ms, as it is.
    if(app.radial_armed && poc_now_ms() - app.radial_shown_ms > 300) {
      app.radial_fresh = true;
      app.radial_shown_ms = poc_now_ms();
    }
    moving = true;
  }
  if(app.radial_open && app.radial_fresh) {
    const pane_t* p = &app.panes[RADIAL];
    double t = (poc_now_ms() - app.radial_shown_ms) / RADIAL_FADE_MS;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    float e = (float)(1 - (1 - t) * (1 - t));  // ease out
    frame_darken(out, (size_t)DISPLAY_WIDTH * DISPLAY_HEIGHT, (unsigned)(RADIAL_DIM * e));
    panel_draw_glass(out, DISPLAY_WIDTH, DISPLAY_HEIGHT, &radial_panel, p->left, p->top,
                     p->pixels, p->stride, (unsigned)(RADIAL_GLASS * e));
    moving |= t < 1;
  }
  if(app.switcher_open && !app.switcher_fresh) {  // as the radial menus
    if(app.switcher_armed && poc_now_ms() - app.switcher_shown_ms > 300) {
      app.switcher_fresh = true;
      app.switcher_shown_ms = poc_now_ms();
    }
    moving = true;
  }
  if(app.switcher_open && app.switcher_fresh) {
    double t = (poc_now_ms() - app.switcher_shown_ms) / SWITCHER_FADE_MS;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    float e = (float)(1 - (1 - t) * (1 - t));
    frame_blend(out, app.switcher_px, (size_t)DISPLAY_WIDTH * DISPLAY_HEIGHT,
                (unsigned)(255 * e));
    moving |= t < 1;
  }
  if(app.keys_open && app.keys_fresh) {  // opaque, over a video too
    uint32_t* to = out + (size_t)app.panes[KEYS].top * DISPLAY_WIDTH;
#ifdef APP_RN
    // BrowserPS5 RN (2026-11-11): the keyboard as the PS5's own, a board in
    // the middle with round top corners (keyboard-rn.css), not the whole
    // width of the screen: the page stays in view at its sides.
    for(int y = 0; y < KEYS_HEIGHT; y++) {
      int inset = 0;  // the round corners: columns left out at each end of this row
      if(y < KEYS_BOARD_RADIUS) {
        const int dy = KEYS_BOARD_RADIUS - y;
        int dx = 0;
        while(dx < KEYS_BOARD_RADIUS && (KEYS_BOARD_RADIUS - dx) * (KEYS_BOARD_RADIUS - dx) + dy * dy
                                        > KEYS_BOARD_RADIUS * KEYS_BOARD_RADIUS) {
          dx++;
        }
        inset = dx;
      }
      const uint32_t* from = app.keys + (size_t)y * DISPLAY_WIDTH;
      uint32_t* row = to + (size_t)y * DISPLAY_WIDTH;
      for(int x = KEYS_BOARD_LEFT + inset; x < KEYS_BOARD_LEFT + KEYS_BOARD_WIDTH - inset; x++) {
        row[x] = from[x] | 0xff000000;
      }
    }
#else
    for(size_t i = 0; i < (size_t)DISPLAY_WIDTH * KEYS_HEIGHT; i++) {
      to[i] = app.keys[i] | 0xff000000;
    }
#endif
  }
#ifdef APP_RN
  if(ask_native.open && ask_native.box.pixels) {
    frame_dim(out, (size_t)DISPLAY_WIDTH * DISPLAY_HEIGHT);
    bitmap_draw(out, DISPLAY_WIDTH, DISPLAY_HEIGHT, ask_native.box.pixels, ask_native.box.width, ask_native.box.height,
                (DISPLAY_WIDTH - ask_native.box.width) / 2, (DISPLAY_HEIGHT - ask_native.box.height) / 2);
  }
#endif
  return moving;
}


#ifdef GPU_COMPOSITING
// On the main thread (and the bar's timer): a view's ID changes when it
// gets another WebProcess.
static void
layer_ids_update(void) {
  for(int i = 0; i < PANES; i++) {
    __atomic_store_n(&app.panes[i].page_id,
                     WKPageGetWebPageIDForBrowserPS5(page(i)),
                     __ATOMIC_RELAXED);
    int32_t pid = (int32_t)WKPageGetProcessIdentifier(page(i));
    __atomic_store_n(&app.panes[i].web_pid, pid, __ATOMIC_RELAXED);
    if(pid > 0) {
      app.panes[i].last_web_pid = pid;
    }
  }
}


// Frames another WebProcess wrote under this name: one that crashed or
// ended without taking them down, in this run or an earlier one (G2,
// 2026-11-04: the bar showed what pid 156 had left).
static bool
layer_stale(const bps5_layer_t* layer, int32_t web_pid) {
  return web_pid > 0 && layer->pid != web_pid;
}


static bps5_layer_t*
layer_open(uint64_t page_id, int32_t web_pid, size_t* mapped) {
  char name[48];

  bps5_layer_name(name, sizeof(name), page_id);
  int fd = shm_open(name, O_RDWR, 0);
  if(fd < 0) {
    return 0;
  }
  // Its own size (version 2 layers are as big as their view), mapped as a
  // whole, and only one whose buffers lie inside it: a WebProcess whose
  // ftruncate() failed leaves it with 0 bytes, and reading its header beyond
  // the end faults (2026-11-06, the 2c2l and 2c2m runs: the WebProcess in
  // the title ran out of memory for /bps5layer-11, and the next UIProcess
  // died on it at start). One being made has no size yet either: the next
  // try takes it.
  struct stat st = {};
  if(fstat(fd, &st) || st.st_size < (off_t)BPS5_LAYER_HEADER_BYTES ||
     (uint64_t)st.st_size > BPS5_LAYER_BYTES + BPS5_LAYER_BUFFERS * BPS5_LAYER_PAGE) {
    static uint64_t told;
    if(told != page_id) {
      told = page_id;
      POC_LOG("%s: %lld bytes, not a layer: not opened", name, (long long)st.st_size);
    }
    close(fd);
    return 0;
  }
  size_t bytes = (size_t)st.st_size;
  void* memory = mmap(0, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  close(fd);
  if(memory == MAP_FAILED) {
    POC_PERROR("mmap");
    return 0;
  }
  bps5_layer_t* layer = (bps5_layer_t*)memory;
  if(!bps5_layer_ready(layer) || layer_stale(layer, web_pid)) {
    munmap(memory, bytes);
    return 0;
  }
  if(!bps5_layer_fits(layer, bytes)) {
    static uint64_t told_fit;
    if(told_fit != page_id) {
      told_fit = page_id;
      POC_LOG("%s: %zu bytes do not hold its frames: not opened", name, bytes);
    }
    munmap(memory, bytes);
    return 0;
  }
  uint32_t width, height;
  bps5_layer_capacity(layer, &width, &height);
  POC_LOG("%s: the frames of WebProcess %d (version %u, %ux%u, %.1f MiB)", name,
          layer->pid, layer->version, width, height, bytes / 1048576.0);
  *mapped = bytes;
  return layer;
}


// On the display thread, with app.lock held: each view's newest frame into
// its rows of the screen. True if there was one.
static bool
layers_take(void) {
  bool any = false;

  if(title_memory_off) {  // (2026-11-14: the title may be closed by the system now: none of its memory is read)
    return false;
  }
  for(int i = 0; i < PANES; i++) {
    pane_t* pane = &app.panes[i];
    bps5_layer_t* layer = (bps5_layer_t*)pane->layer;
    uint64_t id = __atomic_load_n(&pane->page_id, __ATOMIC_RELAXED);
    int32_t web_pid = __atomic_load_n(&pane->web_pid, __ATOMIC_RELAXED);

    // A WebProcess that closed them, another page ID, or another
    // WebProcess: open the new ones.
    if(layer && (!bps5_layer_ready(layer) || id != pane->layer_id ||
                 layer_stale(layer, web_pid))) {
      munmap(layer, pane->layer_bytes);
      pane->layer = layer = 0;
    }
    if(!layer) {
      // Twice a second until the WebProcess has made them (or every 50 ms
      // for a tab just brought to the front).
      double now = poc_now_ms();
      if(!id || now < pane->next_open_ms) {
        continue;
      }
      pane->next_open_ms = now + (now < pane->open_soon_ms ? 50 : 500);
      if(!(pane->layer = layer = layer_open(id, web_pid, &pane->layer_bytes))) {
        continue;
      }
      pane->layer_id = id;
      pane->front = 2;
    }

    int fresh;
    pane->front = bps5_layer_take(layer, pane->front, &fresh);
    // In full screen the page has the bar's rows too.
    if(i == BAR && app.fullscreen) {
      continue;
    }
    // Or its last frame again, where its rows were the page's.
    if(!fresh && !(pane->redraw && pane->taken)) {
      continue;
    }
    pane->redraw = false;
    const bps5_layer_frame_t* frame = &layer->frames[pane->front];
    if(!bps5_layer_frame_fits(layer, frame)) {
      continue;  // a frame bigger than its buffer: not one to copy
    }
    const uint32_t* pixels = bps5_layer_pixels(layer, pane->front);
    unsigned width = frame->width < (unsigned)pane->width ? frame->width : pane->width;
    unsigned rows = frame->height < (unsigned)pane->height ? frame->height
                                                           : pane->height;
    for(unsigned y = 0; y < rows; y++) {
      unsigned from = frame->flags & BPS5_LAYER_BOTTOM_UP
                      ? frame->height - 1 - y : y;
      memcpy(pane->pixels + (size_t)y * pane->stride,
             pixels + (size_t)from * frame->width, width * 4);
    }
    if(i == KEYS) {
      app.keys_fresh = true;
    } else if(i == MENU) {
      app.menu_fresh = true;
    } else if(i == DIALOG) {
      app.dialog_fresh = true;
    } else if(i == RADIAL) {
      radial_painted();
    } else if(i == SWITCHER) {
      switcher_painted();
    }
    app.paints++;
    any = true;
    double taken_ms = poc_now_ms();
    if(pane->taken && taken_ms - pane->taken_ms > pane->max_gap_ms) {
      pane->max_gap_ms = taken_ms - pane->taken_ms;
    }
    pane->taken++;
    pane->taken_ms = taken_ms;
    if(app.first_paint_ms < 0) {
      app.first_paint_ms = poc_now_ms() - app.start_ms;
      POC_LOG("first frame from the GPU (%s, %ux%u) %.0f ms after start",
              pane->name, frame->width, frame->height, app.first_paint_ms);
    }
  }
  return any;
}
#endif


static void stop(void*);


// While a title is in front the payloads share about 2 cores, all their
// threads at priority 700. A moving cursor over YouTube's player kept the
// WebProcess's script busy and the TV lost 15-35% of its frames (2026-11-04
// 17:51-17:58). This thread copies a frame in a few ms and otherwise waits
// (4 ms polls, the title's vsync), so it goes a little ahead of the page, as
// the WebProcess's compositor does (BROWSERPS5_COMPOSITOR_PRIORITY).
// DISPLAY_PRIORITY=0 leaves it as it is; a smaller number is a higher
// priority.
#ifndef DISPLAY_PRIORITY
#define DISPLAY_PRIORITY 690
#endif

static void
raise_display_priority(void) {
  struct sched_param param;
  int policy = 0;

  memset(&param, 0, sizeof(param));
  if(DISPLAY_PRIORITY <= 0 || pthread_getschedparam(pthread_self(), &policy, &param)) {
    return;
  }
  int before = param.sched_priority;
  param.sched_priority = DISPLAY_PRIORITY;
  int err = pthread_setschedparam(pthread_self(), policy, &param);
  pthread_getschedparam(pthread_self(), &policy, &param);
  POC_LOG("display thread priority %d -> %d (%s)", before, param.sched_priority,
          err ? strerror(err) : "done");
}


// Modo cinema: the cursor goes after this long with the controller at rest.
#define CINEMA_CURSOR_MS 2500

static void*
display_thread(void*) {
  double tile_sum = 0;
  double tile_ms;
  double wait_ms;

  raise_display_priority();
  for(;;) {
    pthread_mutex_lock(&app.lock);
    {  // the cursor out of the way: a game has the controller, or a video
       // covers the screen and the controller rested (docs/07, "Modo cinema")
      bool off = (app.game && !app.aim_shown) || (app.fullscreen && settings.cinema
                              && poc_now_ms() - app.input_ms > CINEMA_CURSOR_MS)
                 // 2026-11-11: a page driven by the D-pad's arrow keys (YouTube na TV) has its own highlight:
                 // the cursor is in the way until the stick moves it again
                 || (app.keys_drive && !app.menu_open && !app.dialog_open && !app.keys_open);
      if(off != app.cursor_off) {
        app.cursor_off = off;
        app.dirty = true;
      }
    }
#ifdef GPU_COMPOSITING
    if(!app.frozen && layers_take()) {
      app.dirty = true;
    }
    if(!app.dirty && !app.quit) {
      // Nothing new: look again in 4 ms (the WebProcess does not say when
      // it publishes a frame).
      struct timespec until;
      clock_gettime(CLOCK_REALTIME, &until);
      until.tv_nsec += 4000000;
      if(until.tv_nsec >= 1000000000) {
        until.tv_sec++;
        until.tv_nsec -= 1000000000;
      }
      pthread_cond_timedwait(&app.cond, &app.lock, &until);
      pthread_mutex_unlock(&app.lock);
      continue;
    }
#else
    while(!app.dirty && !app.quit) {
      pthread_cond_wait(&app.cond, &app.lock);
    }
#endif
    if(app.quit) {
      pthread_mutex_unlock(&app.lock);
      break;
    }
    app.dirty = compose(app.frame);  // the ring still gliding: again next flip
    bool hand = cursor_is_hand();
    bool cursor = !app.radial_open && !app.switcher_open && !app.cursor_off;  // the stick points there
#ifdef APP_RN
    const uint32_t glow_colour = app.accent;
#endif
    pthread_mutex_unlock(&app.lock);
#ifdef APP_RN
    if(!cursor) {
      glow_x = -1;  // gone with the cursor: back at it when it shows again
    } else if(glow_draw(app.frame, glow_colour)) {  // the neon light still on its way to the cursor
      pthread_mutex_lock(&app.lock);
      app.dirty = true;
      pthread_mutex_unlock(&app.lock);
    }
#endif
    if(cursor) {
      draw_cursor(app.frame, hand);
    }

    tile_ms = wait_ms = 0;  // vout_show adds to them
    int err = display_show(app.frame, &tile_ms, &wait_ms);
    if(err == 1) {
      app.missed_flips++;
    } else if(err) {
      POC_LOG("display_show: 0x%x", err);
      // The screen is gone (with GPU_COMPOSITING: the title was closed,
      // maybe from the system): close the pages as options does, so the
      // WebProcess lets go of its sound port before the launcher kills it.
      WKRunLoopCallOnMainThread(stop, (void*)1);  // no screenshot: fast
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


static WKStringRef
string(const char* s) {
  return WKStringCreateWithUTF8CString(s);
}


#ifdef APP_RN
// 2026-11-14 (read in the engine's own source, Shared/API/c/WKString.cpp and wtf/unicode/UTF8Conversion.cpp):
// WKStringGetUTF8CString() answers 0 and writes NO ending 0 when the text has half of a letter of two halves by
// itself (a lone surrogate). A page's title cut in the middle of an emoji is enough (title.substring(0, 40)):
// the place was left with the letters before that half and, after them, whatever it had before: on the stack,
// anything at all, read on to the first 0 byte, shown in the bar, written into the history. The engine's other
// form puts U+FFFD for such a half and always ends the text; and here the place is ended whatever is answered.
// Every text taken from the engine in this file goes through here (the name below is this one from now on).
static size_t
wk_utf8(WKStringRef s, char* buf, size_t size) {
  if(!size) {
    return 0;
  }
  const size_t n = WKStringGetUTF8CStringNonStrict(s, buf, size);
  if(!n) {
    buf[0] = 0;
  }
  return n;
}
#define WKStringGetUTF8CString(s, buf, size) wk_utf8(s, buf, size)
#endif

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


// The same for a URL.
static const char*
take_url(WKURLRef url, char* buf, size_t size) {
  buf[0] = 0;
  if(url) {
    take_string(WKURLCopyString(url), buf, size);
    WKRelease(url);
  }
  return buf;
}


#ifdef APP_RN
// The version the settings' "Acerca" shows (2026-11-14): the release that is installed, as the installer wrote
// it (poc/release-rn/install.c: the first line of /user/homebrew/browserps5-rn/release.txt), and the day the
// project began. With no such file (a build sent by hand): "dev" and that day. (2026-11-17: it was the day this
// browser was built, which a release whose menu is an earlier release's showed as a day that was not its own.)
#define PROJECT_BEGAN "Nov 2 2026"
static const char*
browser_version(void) {
  static char text[96];
  char release[40] = "";

  if(text[0]) {
    return text;
  }
  if(FILE* f = fopen("/user/homebrew/browserps5-rn/release.txt", "r")) {
    if(!fgets(release, sizeof(release), f)) {
      release[0] = 0;
    }
    fclose(f);
  }
  release[strcspn(release, "\r\n")] = 0;
  for(const char* c = release; *c; c++) {
    if(*c < 0x20 || *c > 0x7e) {
      release[0] = 0;
      break;
    }
  }
  snprintf(text, sizeof(text), "%s (" PROJECT_BEGAN ")", release[0] ? release : "dev");
  return text;
}
#else
static const char*
browser_version(void) {
  return BROWSER_VERSION;
}
#endif

#ifdef APP_RN
// 2026-11-14: a part of a download in several parts is asked for as a navigation of its tab (multi_pump()), and
// until the engine has made it a download it gives that part's address ("...#bps5range=<first>-<last>") as the
// page's own. It never is: not in the bar, not in the tabs' strip, not in the tabs kept for the next time
// (which would have asked for that part again as a file). The page's committed address stands in for it.
static WKURLRef
page_active_url(WKPageRef p) {
  WKURLRef url = WKPageCopyActiveURL(p);
  static char text[8192];

  if(!url) {
    return 0;
  }
  WKStringRef string = WKURLCopyString(url);
  text[0] = 0;
  if(string) {
    WKStringGetUTF8CString(string, text, sizeof(text));
    WKRelease(string);
  }
  if(strstr(text, "#bps5range=")) {
    WKRelease(url);
    return WKPageCopyCommittedURL(p);
  }
  return url;
}
#define WKPageCopyActiveURL(p) page_active_url(p)
#endif

static void
ignore_result(WKTypeRef, WKErrorRef, void*) {
}


#ifdef APP_RN
// 2026-11-14 (found on the PC, tests-host/files-fuzz.c): a script must be UTF-8 or the engine takes NOTHING of it
// (WKStringCreateWithUTF8CString() gives an empty string for a text with one byte that is not). One title cut in
// the middle of a letter, one file on a drive named in another encoding, one line of the history broken by a
// power cut, and the panel that was to be told of it (the tabs, the downloads, the address's suggestions) was
// told nothing more, with no line in the log. Every script goes through run_script(): there, what is not part
// of a whole character becomes U+FFFD, and the rest arrives.
// The bytes of the whole character at s (as the engine reads UTF-8: no overlong form, no surrogate, nothing over
// U+10FFFF), 0 if there is none there.
static size_t
utf8_char(const unsigned char* s) {
  if(s[0] < 0x80) {
    return 1;
  }
  if(s[0] >= 0xc2 && s[0] <= 0xdf) {
    return (s[1] & 0xc0) == 0x80 ? 2 : 0;
  }
  if(s[0] >= 0xe0 && s[0] <= 0xef) {
    const unsigned char low = s[0] == 0xe0 ? 0xa0 : 0x80, high = s[0] == 0xed ? 0x9f : 0xbf;
    return s[1] >= low && s[1] <= high && (s[2] & 0xc0) == 0x80 ? 3 : 0;
  }
  if(s[0] >= 0xf0 && s[0] <= 0xf4) {
    const unsigned char low = s[0] == 0xf0 ? 0x90 : 0x80, high = s[0] == 0xf4 ? 0x8f : 0xbf;
    return s[1] >= low && s[1] <= high && (s[2] & 0xc0) == 0x80 && (s[3] & 0xc0) == 0x80 ? 4 : 0;
  }
  return 0;
}

// 0 for a text that is UTF-8 (nearly always: nothing is copied); or a copy of it, malloc'd, with every byte that
// is not part of a whole character as U+FFFD.
static char*
utf8_mended(const char* text) {
  const unsigned char* s = (const unsigned char*)text;
  size_t bad = 0, length = 0;

  for(const unsigned char* p = s; *p;) {
    const size_t k = utf8_char(p);
    bad += !k;
    p += k ? k : 1;
    length = (size_t)(p - s);
  }
  char* out = bad ? (char*)malloc(length + 2 * bad + 1) : 0;
  if(!out) {
    return 0;
  }
  size_t n = 0;
  for(const unsigned char* p = s; *p;) {
    const size_t k = utf8_char(p);
    if(k) {
      memcpy(out + n, p, k);
      n += k;
      p += k;
    } else {
      memcpy(out + n, "\xef\xbf\xbd", 3);
      n += 3;
      p++;
    }
  }
  out[n] = 0;
  return out;
}
#endif

static void
run_script(int pane, const char* script, void* ctx,
           WKPageEvaluateJavaScriptFunction done) {
#ifdef APP_RN
  char* mended = utf8_mended(script);
  WKStringRef s = string(mended ? mended : script);
  free(mended);
#else
  WKStringRef s = string(script);
#endif

  WKPageEvaluateJavaScriptInMainFrame(page(pane), s, ctx, done);
  WKRelease(s);
}


// Which view gets the keys (and shows a caret).
static void
set_focus(int pane) {
  for(int i = 0; i < PANES; i++) {
    WKViewSetFocus(app.panes[i].view, i == pane);
  }
  app.focus = pane;
}


// s as a JSON string, at out; returns its length.
static size_t
json_string(char* out, size_t size, const char* s) {
  static const char hex[] = "0123456789abcdef";
  size_t n = 0;

  if(size < 3) {
    return 0;
  }
  out[n++] = '"';
  for(; *s && n + 8 < size; s++) {
    unsigned char c = *s;
    if(c == '"' || c == '\\') {
      out[n++] = '\\';
      out[n++] = (char)c;
    } else if(c < 0x20) {
      n += (size_t)snprintf(out + n, size - n, "\\u00%c%c", hex[c >> 4],
                            hex[c & 15]);
    } else {
      out[n++] = (char)c;
    }
  }
  out[n++] = '"';
  out[n] = 0;
  return n;
}


#ifdef APP_RN
static void bar_tabs(void);
static char bar_tabs_sent[8192];  // the strip's last setTabs(): sent again only when it changes
#endif

/**
 * Tells the bar what the page is doing, on the main thread, if that
 * changed since last time.
 **/
#ifdef APP_RN
static bool error_page_of(WKPageRef p);
static void error_page_note(WKPageRef p, bool shown);
static bool download_of_address(const char* url);
#endif

static void
bar_update(void*) {
  static char script[sizeof(app.bar_state)];
  char url[4096];
  char title[1024];
  size_t n = 0;

  take_url(WKPageCopyActiveURL(page(PAGE)), url, sizeof(url));
  take_string(WKPageCopyTitle(page(PAGE)), title, sizeof(title));
#ifdef APP_RN
  // An address that became a download stays the page's "active" one in the engine (2026-11-16: over the start
  // page the bar said the address of the cheat file just saved): the page that is shown is the one to say.
  if(!app.loading && url[0] && download_of_address(url)) {
    char shown[sizeof(url)];
    take_url(WKPageCopyCommittedURL(page(PAGE)), shown, sizeof(shown));
    if(strcmp(url, shown)) {
      memcpy(url, shown, sizeof(url));
    }
  }
  const bool failed = error_page_of(page(PAGE));  // (its title is the site's name: the address did not come)
#else
  const bool failed = false;
#endif
  n += (size_t)snprintf(script + n, sizeof(script) - n, "update({url: ");
  n += json_string(script + n, sizeof(script) - n, url);
  n += (size_t)snprintf(script + n, sizeof(script) - n, ", title: ");
  n += json_string(script + n, sizeof(script) - n, title);
  snprintf(script + n, sizeof(script) - n,
           ", canGoBack: %s, canGoForward: %s, loading: %s, progress: %.2f, "
           "zoom: %d, media: %s, failed: %s})",
           WKPageCanGoBack(page(PAGE)) ? "true" : "false",
           WKPageCanGoForward(page(PAGE)) ? "true" : "false",
           app.loading ? "true" : "false",
           WKPageGetEstimatedProgress(page(PAGE)), (int)(app.zoom * 100 + 0.5),
           media_shown(page(PAGE)) && url_is_web(url) ? "true" : "false", failed ? "true" : "false");
  if(strcmp(script, app.bar_state)) {
    strcpy(app.bar_state, script);
    run_script(BAR, script, 0, ignore_result);
#ifdef APP_RN
    bar_tabs();  // the tab strip: the title and host of the tab in front changed too
#endif
    if(phone_port() > 0) {  // what the phone shows as on the TV
      phone_state_note();
    }
    // What the bar was told about loading (J3: it stayed on "loading, 48%").
    static int last_loading = -1;
    static int last_percent = -1;
    int percent = (int)(WKPageGetEstimatedProgress(page(PAGE)) * 100);
    if(app.loading != last_loading || percent != last_percent) {
      last_loading = app.loading;
      last_percent = percent;
      POC_LOG("bar told at %.0f ms: loading %d, progress %d%%",
              poc_now_ms() - app.start_ms, app.loading, percent);
    }
  }

#ifdef LOG_TITLE
  static char last_title[sizeof(title)];
  if(strcmp(title, last_title)) {
    strcpy(last_title, title);
    POC_LOG("title at %.0f ms: %s", poc_now_ms() - app.start_ms, title);
  }
#endif
}


#ifdef MEDIA_PROBE
// make MEDIA_PROBE=1 (docs/03-video-e-gpu.md, V3): every second, the state
// of the page's first <video> goes to the log when it changed, for sites
// whose page is not ours (YouTube). A blob: source is MediaSource's.
// The first run also starts counting what the page's own script runs into
// (V3: YouTube's player gave up after ~45 s with nothing wrong in the
// <video>, the network or the log): script errors and rejected promises,
// console.error(), and the SourceBuffer calls, their exceptions and 'error'
// events; and says whether there is WebAssembly.
static const char media_probe_js[] = R"((() => {
  const w = window;
  if (!w.__bps5) {
    const s = w.__bps5 = {errors: 0, lastError: '', consoleErrors: 0,
                          lastConsole: '', appends: 0, removes: 0,
                          exceptions: 0, lastException: '', sbErrors: 0,
                          seen: [], fetchFails: 0, lastFetch: ''};
    const short = (m) => String(m).replace(/\s+/g, ' ').slice(0, 140);
    // 2026-11-10 (WhatsApp stuck on "loading your chats", Telegram stuck in
    // one chat): the last 3 different errors, failed fetches, and what the
    // page may want that this WebKit lacks. Also run at each page's start.
    const note = (m) => {
      if (s.seen.indexOf(m) < 0) {
        s.seen.push(m);
        if (s.seen.length > 3)
          s.seen.shift();
      }
    };
    s.missing = ['DecompressionStream', 'CompressionStream', 'OffscreenCanvas',
                 'ResizeObserver', 'IntersectionObserver', 'BroadcastChannel',
                 'SharedWorker', 'Notification', 'indexedDB', 'caches',
                 'structuredClone', 'requestIdleCallback', 'WebAssembly',
                 'AudioContext', 'MediaRecorder', 'VideoDecoder', 'ReadableStream',
                 'TransformStream', 'WritableStream']
        .filter((n) => !(n in w)).join(',') +
        (navigator.locks ? '' : ',locks') + (navigator.storage ? '' : ',storage') +
        (navigator.serviceWorker ? '' : ',serviceWorker');
    const where = (u) => {
      try {
        const x = new URL(String(u), location.href);
        return x.host + x.pathname.slice(0, 40);
      } catch (e) {
        return short(u).slice(0, 60);
      }
    };
    if (w.fetch) {
      const f = w.fetch;
      w.fetch = function(input, init) {
        const u = input && typeof input === 'object' && 'url' in input ? input.url : input;
        return f.call(this, input, init).then((r) => {
          if (!r.ok && r.type !== 'opaque' && r.type !== 'opaqueredirect') {
            s.fetchFails++;
            s.lastFetch = r.status + ' ' + where(u);
          }
          return r;
        }, (x) => {
          s.fetchFails++;
          s.lastFetch = short(x && x.name ? x.name + ': ' + x.message : x).slice(0, 50) + ' ' + where(u);
          throw x;
        });
      };
    }
    w.addEventListener('error', (e) => {
      if (!(e instanceof ErrorEvent))
        return;
      s.errors++;
      s.lastError = short(e.message) + ' @' +
          String(e.filename || '').split('?')[0].split('/').pop() + ':' + e.lineno;
      note(s.lastError);
    }, true);
    w.addEventListener('unhandledrejection', (e) => {
      s.errors++;
      const r = e.reason;
      s.lastError = 'rejection ' + short(r && r.name ? r.name + ': ' + r.message : r);
      note(s.lastError);
    });
    const error = console.error;
    console.error = function(...a) {
      s.consoleErrors++;
      s.lastConsole = short(a.map(String).join(' '));
      note('console ' + s.lastConsole);
      return error.apply(this, a);
    };
    if (w.SourceBuffer) {
      const p = SourceBuffer.prototype;
      for (const name of ['appendBuffer', 'remove', 'abort']) {
        const f = p[name];
        p[name] = function(...a) {
          if (!this.__bps5) {
            this.__bps5 = true;
            this.addEventListener('error', () => { s.sbErrors++; });
          }
          if (name === 'appendBuffer')
            s.appends++;
          else if (name === 'remove')
            s.removes++;
          try {
            return f.apply(this, a);
          } catch (x) {
            s.exceptions++;
            s.lastException = name + ' ' + short(x.name + ': ' + x.message);
            throw x;
          }
        };
      }
    }
  }
  const s = w.__bps5;
  const page = ' js=' + s.errors + (s.lastError ? ' [' + s.lastError + ']' : '') +
      ' console=' + s.consoleErrors + (s.lastConsole ? ' [' + s.lastConsole + ']' : '') +
      ' sb=' + s.appends + '/' + s.removes + ' exc=' + s.exceptions +
      (s.lastException ? ' [' + s.lastException + ']' : '') + ' sberr=' + s.sbErrors +
      ' wasm=' + typeof WebAssembly +
      ' fetch=' + s.fetchFails + (s.lastFetch ? ' [' + s.lastFetch + ']' : '') +
      (s.missing ? ' missing=' + s.missing : '') +
      ' title=[' + String(document.title).slice(0, 30) + ']' +
      (s.seen.length ? ' recent={' + s.seen.join(' | ').slice(0, 420) + '}' : '');
  const v = document.querySelector('video');
  if (!v)
    return 'no <video> in ' + location.href.slice(0, 100) + page;
  const q = v.getVideoPlaybackQuality ? v.getVideoPlaybackQuality() : {};
  const b = [];
  for (let i = 0; i < v.buffered.length; i++)
    b.push(v.buffered.start(i).toFixed(1) + '-' + v.buffered.end(i).toFixed(1));
  return 't=' + v.currentTime.toFixed(1) + ' paused=' + v.paused +
      ' ready=' + v.readyState + ' net=' + v.networkState + ' ' +
      v.videoWidth + 'x' + v.videoHeight + ' frames=' + q.totalVideoFrames +
      ' dropped=' + q.droppedVideoFrames + ' buffered=[' + b.join(' ') +
      '] src=' + (v.currentSrc || '').slice(0, 24) + ' error=' +
      (v.error ? v.error.code + ' ' + v.error.message : 'none') + page;
})())";

// Seconds the last probe has gone unanswered: a WebProcess whose main thread
// is stuck never answers (V3, 2026-11-04).
static int media_probe_waiting = -1;


static void
media_probe_result(WKTypeRef result, WKErrorRef, void*) {
  static char last[2048];
  char now[2048] = "?";

  media_probe_waiting = -1;
  if(result && WKGetTypeID(result) == WKStringGetTypeID()) {
    WKStringGetUTF8CString((WKStringRef)result, now, sizeof(now));
  }
  if(strcmp(now, last)) {
    strcpy(last, now);
#ifdef GPU_COMPOSITING
    // The page's frames on the TV so far, and the longest gap between two
    // since the last line (a stutter shows there, not in the <video>'s).
    pthread_mutex_lock(&app.lock);
    unsigned shown = app.panes[PAGE].taken;
    double gap = app.panes[PAGE].max_gap_ms;
    app.panes[PAGE].max_gap_ms = 0;
    pthread_mutex_unlock(&app.lock);
    POC_LOG("video at %.0f ms: %s shown=%u gap=%.0f",
            poc_now_ms() - app.start_ms, now, shown, gap);
#else
    POC_LOG("video at %.0f ms: %s", poc_now_ms() - app.start_ms, now);
#endif
  }
}


// The UIProcess's share of the payloads' cores, a line a second: payloads
// may run on 5 of the 16 hardware threads (mask 0xea00) and get ~4.4 of
// them when nothing else runs (poc/cpu_probe, 2026-11-04 17:16), while the
// WebProcess never got more than 1.9 in a YouTube run. Like the WebProcess's
// BROWSERPS5_LOAD_PROBE (crash-report.c): the CPU time of the process and
// how late a thread that sleeps 5 ms wakes up.
static double
process_cpu_ms(void) {
  struct rusage usage;
  if(getrusage(RUSAGE_SELF, &usage)) {
    return -1;
  }
  return usage.ru_utime.tv_sec * 1e3 + usage.ru_utime.tv_usec / 1e3
         + usage.ru_stime.tv_sec * 1e3 + usage.ru_stime.tv_usec / 1e3;
}


static void*
load_probe_thread(void*) {
  double second = poc_now_ms();
  double cpu = process_cpu_ms();
  double late_max = 0;
  double late_sum = 0;
  unsigned wakes = 0;

  while(!__atomic_load_n(&app.quit, __ATOMIC_RELAXED)) {
    double before = poc_now_ms();
    usleep(5000);
    double now = poc_now_ms();
    double late = now - before - 5.0;
    late = late < 0 ? 0 : late;
    late_max = late > late_max ? late : late_max;
    late_sum += late;
    wakes++;
    if(now - second >= 1000) {
      double cpu_now = process_cpu_ms();
#ifdef GPU_COMPOSITING
      // The frames taken from each view so far: the bar stayed on "loading,
      // 48%" for 80 s after YouTube had loaded (J3 TV test, 2026-11-04
      // 21:21), so does it still send any?
      pthread_mutex_lock(&app.lock);
      unsigned bar_frames = app.panes[BAR].taken;
      unsigned page_frames = app.panes[PAGE].taken;
      pthread_mutex_unlock(&app.lock);
      POC_LOG("ui load at %.0f ms: %.2f cores, woke up late by %.0f ms max, "
              "%.1f ms mean; frames taken: bar %u, page %u",
              now - app.start_ms,
              cpu < 0 || cpu_now < 0 ? -1.0 : (cpu_now - cpu) / (now - second),
              late_max, late_sum / wakes, bar_frames, page_frames);
#else
      POC_LOG("ui load at %.0f ms: %.2f cores, woke up late by %.0f ms max, "
              "%.1f ms mean", now - app.start_ms,
              cpu < 0 || cpu_now < 0 ? -1.0 : (cpu_now - cpu) / (now - second),
              late_max, late_sum / wakes);
#endif
      second = now;
      cpu = cpu_now;
      late_max = late_sum = 0;
      wakes = 0;
    }
  }
  return 0;
}
#endif


// libwpe's key codes are X11 keysyms: ASCII and Latin-1 are themselves, the
// rest of Unicode is 0x01000000 + the code point.
#define KEY_BACKSPACE 0xff08
#define KEY_RETURN    0xff0d

// Down and up. With no text, WebKit takes it from the key code (Enter: "\r",
// which submits a form).
static void
send_key(int pane, int32_t code, const char* text, uint32_t length) {
  WKPageHandleKeyboardEvent(page(pane), WKKeyboardEventMake(kWKEventKeyDown,
      kWKInputTypeNormal, text, length, "", code, 0, 0, 0));
  WKPageHandleKeyboardEvent(page(pane), WKKeyboardEventMake(kWKEventKeyUp,
      kWKInputTypeNormal, text, length, "", code, 0, 0, 0));
}


// A key per character, as if typed. Returns how many. Not inside POC_LOG,
// which evaluates its arguments twice.
static unsigned
type_text(int pane, const char* text) {
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
    send_key(pane, code, (const char*)s, length);
    s += length;
    count++;
  }
  return count;
}


// The element in focus, inside shadow roots too.
#define FOCUSED_ELEMENT                                         \
  "var e = document.activeElement;"                             \
  "while (e && e.shadowRoot && e.shadowRoot.activeElement)"     \
  "  e = e.shadowRoot.activeElement;"

// "<input type>\n<enterkeyhint>\n<value>" for a text field, "edit\n<hint>\n"
// for other editable content (typed at the caret), null for anything else.
// This WebKit has no enterKeyHint property, only the attribute (M4).
static const char field_script[] = "(function () {" FOCUSED_ELEMENT R"(
  if (!e) return null;
  var hint = (e.enterKeyHint || e.getAttribute('enterkeyhint') || '')
    .toLowerCase() + '\n';
  if (e.tagName == 'TEXTAREA') return 'textarea\n' + hint + e.value;
  if (e.tagName == 'INPUT' &&
      /^(text|search|url|email|tel|password|number)$/.test(e.type))
    return e.type + '\n' + hint + e.value;
  if (e.isContentEditable) return 'edit\n' + hint;
  return null;
})())";

static const char select_script[] = "(function () {" FOCUSED_ELEMENT R"(
  if (e && e.select) e.select();
})())";

#ifdef APP_RN
// 2026-11-17 (RN): the keyboard for a page's field only when the press gave that field the focus, or was on the
// field itself (or its label). A page that keeps a field in focus by itself (a game's name box: slither.io) had
// the keyboard open over the game at every press anywhere on it. field_mark_js runs before the press reaches
// the page and remembers what had the focus; field_at_js is field_script for a press at (x, y) of the page.
static const char field_mark_js[] = "(function () {" FOCUSED_ELEMENT " window.__bps5was = e || null; })()";
static const char field_at_js[] = "(function (x, y) {" FOCUSED_ELEMENT R"(
  var was = window.__bps5was;
  window.__bps5was = undefined;
  if (!e) return null;
  var hint = (e.enterKeyHint || e.getAttribute('enterkeyhint') || '')
    .toLowerCase() + '\n', found = null;
  if (e.tagName == 'TEXTAREA') found = 'textarea\n' + hint + e.value;
  else if (e.tagName == 'INPUT' &&
           /^(text|search|url|email|tel|password|number)$/.test(e.type))
    found = e.type + '\n' + hint + e.value;
  else if (e.isContentEditable) found = 'edit\n' + hint;
  if (!found || e !== was) return found;
  var t = document.elementFromPoint(x, y);
  while (t && t.shadowRoot) {
    var inner = t.shadowRoot.elementFromPoint(x, y);
    if (!inner || inner === t) break;
    t = inner;
  }
  for (var n = t; n; n = n.parentNode || n.host) {
    if (n === e || (n.tagName == 'LABEL' && n.control === e)) return found;
  }
  return null;
}))";
#endif

// field_found's context: the pane.
#define FIELD_CTX(pane) ((void*)(uintptr_t)(pane))


// Back to the page, and the bar shows its URL again (update() leaves the
// field alone while it has the focus).
static void
leave_bar(void) {
  run_script(BAR, "address.blur()", 0, ignore_result);
  set_focus(PAGE);
  app.bar_state[0] = 0;
  bar_update(0);
}


#if defined(REMOTE_UI) && defined(APP_RN)
static bool pages_away(void);
static bool find_typing;  // the keyboard is open for find_open()
static void find_start(const char* text);
#endif

// On the main thread.
static void
keyboard_text(void* arg) {
  char* text = (char*)arg;
  int pane = app.ime_pane;

#if defined(REMOTE_UI) && defined(APP_RN)
  // The system's keyboard (open_keyboard()): the title paused the pages
  // for it, and they are back half a second after it closes. Its text goes
  // in then (away_main()), into views that are shown, as our keyboard's.
  if(app.system_ime && pages_away()) {
    free(app.ime_held_text);
    app.ime_held_text = text;
    app.ime_held = true;
    POC_LOG("the system keyboard closed (%s): waiting for the pages to be back", text ? "text" : "cancelled");
    return;
  }
  app.system_ime = false;
  if(find_typing) {  // "Procurar na página": the text to look for, not text for a field
    find_typing = false;
    if(text) {
      find_start(text);
      free(text);
    } else {
      POC_LOG("find: cancelled");
    }
    return;
  }
#endif

  if(!text) {
    POC_LOG("keyboard cancelled");
    if(pane == BAR) {
      leave_bar();
    }
    return;
  }
  // The script runs in the WebProcess before the keys that follow.
  if(app.ime_replace) {
    run_script(pane, select_script, 0, ignore_result);
    if(!*text) {
      send_key(pane, KEY_BACKSPACE, 0, 0);
    }
  }
  unsigned typed = type_text(pane, text);
  // Only how much: it may be a password.
  POC_LOG("typed %u character(s) into the %s", typed, app.panes[pane].name);
  if(app.ime_enter) {  // the keyboard's own key said "Go" or "Search"
    send_key(pane, KEY_RETURN, 0, 0);
  }
  free(text);
}


// On the keyboard's thread.
static void
keyboard_closed(const char* text, void*) {
  WKRunLoopCallOnMainThread(keyboard_text, text ? strdup(text) : 0);
}


// The saved logins the keyboard offers ("Entrar como ...", logins.h): the
// user names for the field's origin, set by logins_for_keys() before it
// opens, and the origin they are for (login_fill() checks it again).
static char keys_logins[LOGINS_OFFERED * LOGIN_FIELD * 6 + 16] = "[]";
static char keys_login_origin[LOGIN_FIELD];
static void logins_for_keys(int pane, const char* kind);
static void login_fill(int index);

// The browser's own keyboard (KEYS, keyboard.html), on the main thread.
// Opens it for a field: kind and hint are field_script's (text, search,
// url, email, tel, password, number, textarea, edit; or bar, the address)
// and the field's enterkeyhint; text is what the field has. It never takes
// the focus: the field keeps it, for the text that comes when it closes.
#ifdef APP_RN
static const char* favorites_for_keys(void);  // the favorites' JSON, or 0 (after favorites_json)
#endif

[[maybe_unused]] static void
keys_show(const char* kind, const char* hint, const char* text) {
  static char script[65536];  // text is at most 8 KiB, 6 bytes a character escaped
  size_t n = (size_t)snprintf(script, sizeof(script), "kb.open({kind: ");
  n += json_string(script + n, sizeof(script) - n, kind);
  n += (size_t)snprintf(script + n, sizeof(script) - n, ", enter: ");
  n += json_string(script + n, sizeof(script) - n, hint);
  n += (size_t)snprintf(script + n, sizeof(script) - n, ", text: ");
  n += json_string(script + n, sizeof(script) - n, text);
#ifdef APP_RN
  // RN's keyboard suggests the favorites' sites for the address.
  const char* favs = !strcmp(kind, "bar") ? favorites_for_keys() : 0;
  if(favs && n + strlen(favs) + 2048 < sizeof(script)) {
    n += (size_t)snprintf(script + n, sizeof(script) - n, ", favs: %s", favs);
  }
  // And the sites opened lately (the history's, newest first).
  char* recent = !strcmp(kind, "bar") ? history_recent_json(HISTORY_FILE, 40) : 0;
  if(recent && n + strlen(recent) + 2048 < sizeof(script)) {
    n += (size_t)snprintf(script + n, sizeof(script) - n, ", recent: %s", recent);
  }
  free(recent);
#endif
  snprintf(script + n, sizeof(script) - n, ", logins: %s})", keys_logins);
  run_script(KEYS, script, 0, ignore_result);
  if(!strcmp(keys_logins, "[]")) {
    keys_login_origin[0] = 0;
  }
  snprintf(keys_logins, sizeof(keys_logins), "[]");  // the next opening asks again

  // Shown once it has drawn itself for this field (the display thread).
  pthread_mutex_lock(&app.lock);
  app.keys_open = true;
  app.keys_fresh = false;
  app.dirty = true;
  pthread_cond_signal(&app.cond);
  pthread_mutex_unlock(&app.lock);
  POC_LOG("keyboard open for a %s field", kind);
}


static void
keys_hide(void) {
  pthread_mutex_lock(&app.lock);
  app.keys_open = false;
  app.dirty = true;
  pthread_cond_signal(&app.cond);
  pthread_mutex_unlock(&app.lock);
}


// The page's field in focus above the keyboard, if the page can scroll
// that far.
[[maybe_unused]] static void
keys_scroll_field(int pane) {
  char script[512];

  snprintf(script, sizeof(script), "(function () {" FOCUSED_ELEMENT
           "if (!e || !e.getBoundingClientRect) return;"
           "var r = e.getBoundingClientRect(), room = innerHeight - %d;"
           "if (r.bottom > room) scrollBy(0, r.bottom - room);"
           "})()", KEYS_HEIGHT + 24);
  run_script(pane, script, 0, ignore_result);
}


// What the keyboard sent (keyboard.html's send()): done?<text> or cancel.
// The text is never logged: it may be a password.
static void
keys_command(const char* command) {
  const char* argument = strchr(command, '?');

  if(!app.keys_open) {  // one that came after it was closed
    return;
  }
  keys_hide();
  if(!strncmp(command, "login?", 6)) {  // "Entrar como ...": the index in its list
    keyboard_text(0);  // as a cancel: the field keeps what it had
    login_fill(atoi(command + 6));
    return;
  }
  if(!strncmp(command, "done", 4) && (command[4] == '?' || !command[4])) {
    const char* encoded = argument ? argument + 1 : "";
    size_t size = strlen(encoded) + 1;
    char* text = (char*)malloc(size);
    if(text) {
      address_decode(encoded, text, size);
    }
    keyboard_text(text);  // frees it; 0 is a cancel
  } else {
    keyboard_text(0);
  }
}


// Closes the keyboard as its own "fechar" would.
static void
keys_cancel(void) {
  if(app.keys_open) {
    run_script(KEYS, "kb.cancel()", 0, ignore_result);
  }
}


// The keyboard for a field, kind and hint as in keys_show(), ime and enter
// the same for the system keyboard. With the title (REMOTE_UI) the
// browser's own: the system's, opened while the title drew with the GPU,
// took the console down (TV test, 2026-11-04 18:19, a click on YouTube's
// search; docs/03-video-e-gpu.md). Returns what ime_open() would.
static int
open_keyboard(const char* kind, const char* hint, ime_kind_t ime,
              ime_enter_t enter, const char* text) {
#ifdef REMOTE_UI
#ifdef APP_RN
  // BrowserPS5 RN (2026-11-11): the system's keyboard when the setting says
  // so. What took the console down on 2026-11-04 was the GPU drawing under
  // it; RN's title pauses the pages first, as for the PS button, opens the
  // keyboard, and brings them back when it closes (title.cpp, "The
  // keyboard's pause"). Its text goes into the field then (keyboard_text()).
  // 2026-11-11 01:31: the first run of this on the console (ime_plan.h, the
  // title's pause first) took the console down. Until that is understood
  // only the test builds (SPEED_IME) can open the system's keyboard: a
  // normal build takes no notice of the setting.
  // 2026-11-14: what was not understood then: the system does not load the keyboard's library for a
  // title, and the call went through a NULL import. The title now loads it itself (title/sysime.c) and
  // says so (remote_title_ime_ready()). SYSIME_UI: a normal build opens the system's keyboard when the
  // setting says so and the title has it; else the browser's own, as before. Off until the console's
  // tests of the title's part pass (make SYSIME_UI=1).
  // 2026-11-14 16:47: with the title's part the keyboard opened and closed twice on the console (ime_plan.h),
  // so every build of RN does it now: the system's keyboard when the setting says so (on unless the user
  // turns it off) and the title has it. Not for a password: the system's keyboard shows what is typed in
  // its own line, on a TV; the browser's own hides it.
#if defined(SPEED_IME) || defined(SYSIME_UI)  // the keyboard's own test (ime_plan.h, make SYSIME_UI=1): the title closes it
  if(settings.system_keyboard && remote_title_ime_ready() == 1) {
#elif defined(SPEED_TEST) || defined(SHOW) || defined(DEMO)
  if(false) {  // the other tests type on the browser's own keyboard: nobody is there to press the system's
#else
  if(settings.system_keyboard && strcmp(kind, "password") && remote_title_ime_ready() == 1) {
#endif
    app.system_ime = true;
    const int err = ime_open(input_user(), ime, enter, text, keyboard_closed, 0);
    POC_LOG("the system keyboard asked of the title for a %s field: %d", kind, err);
    if(!err) {
      return 0;
    }
    app.system_ime = false;  // not asked (no title, or one open already): the browser's own
  }
#endif
  (void)ime;
  (void)enter;
  (void)keyboard_closed;
  if(app.ime_pane == PAGE) {
    keys_scroll_field(PAGE);
  }
  keys_show(kind, hint, text);
  return 0;
#else
  (void)kind;
  (void)hint;
  return ime_open(input_user(), ime, enter, text, keyboard_closed, 0);
#endif
}


// Triangle, or a click on the address: the keyboard for the bar, starting
// empty (a new address or a search is what one types most), its "Go" is
// Enter. Not the page's field in focus: Wikipedia focuses its search box
// by itself, and "rtp.pt" went there (TV test, 2026-11-03).
static void
keyboard_for_bar(void) {
  set_focus(BAR);
  run_script(BAR, "address.focus()", 0, ignore_result);
  app.ime_pane = BAR;
  app.ime_replace = true;
  app.ime_enter = true;
  POC_LOG("keyboard for the bar");
  int err = open_keyboard("bar", "go", IME_TEXT, IME_ENTER_GO, "");
  if(err && err != 1) {  // 1: the system keyboard is open already
    POC_RESULT(0, "the system keyboard did not open: 0x%x", err);
  }
}


// field_script's answer, on the main thread.
static void
field_found(WKTypeRef result, WKErrorRef, void* ctx) {
  int pane = (int)(uintptr_t)ctx;
  char kind[8192];

  if(!result || WKGetTypeID(result) != WKStringGetTypeID()) {
    // A click on anything but a text field closes the keyboard.
    keys_cancel();
    return;
  }
  if(pane == BAR) {  // its only field
    keyboard_for_bar();
    return;
  }
  WKStringGetUTF8CString((WKStringRef)result, kind, sizeof(kind));
  char* hint = strchr(kind, '\n');
  char* value = hint ? strchr(hint + 1, '\n') : 0;
  if(!value) {
    return;
  }
  *hint++ = 0;
  *value++ = 0;

  ime_kind_t ime = !strcmp(kind, "url") ? IME_URL
                 : !strcmp(kind, "email") ? IME_MAIL
                 : !strcmp(kind, "number") ? IME_NUMBER : IME_TEXT;
  ime_enter_t enter = !strcmp(hint, "go") || (!*hint && ime == IME_URL)
                    ? IME_ENTER_GO
                    : !strcmp(hint, "search") || !strcmp(kind, "search")
                    ? IME_ENTER_SEARCH : IME_ENTER_DEFAULT;
  app.ime_pane = pane;
  app.ime_replace = strcmp(kind, "edit") != 0;
  app.ime_enter = enter != IME_ENTER_DEFAULT || !strcmp(hint, "send");
  POC_LOG("text field in focus in the %s: %s (enterkeyhint \"%s\")",
          app.panes[pane].name, kind, hint);
  // The keyboard starts with what the field has, except where its text
  // goes in at the caret.
  logins_for_keys(pane, kind);
  int err = open_keyboard(kind, hint, ime, enter, app.ime_replace ? value : "");
  if(err && err != 1) {  // 1: the system keyboard is open already
    POC_RESULT(0, "the system keyboard did not open: 0x%x", err);
  }
}


// The favorites (main thread): the bar keeps the list and hands it over as
// JSON after each change; here it is saved in FAVORITES_FILE, given back
// to the bar when its page loads, and written into the page of favorites.
static char* favorites_json;  // malloc'd, or 0 for none
#ifdef APP_RN
static const char*
favorites_for_keys(void) {
  return favorites_json;
}
#endif


// path, through a new file renamed over it: a crash in between leaves the
// old one whole.
static bool
write_file(const char* path, const char* part1, size_t length1,
           const char* part2, const char* part3) {
  char temporary[256];

  snprintf(temporary, sizeof(temporary), "%s.new", path);
  FILE* f = fopen(temporary, "w");
  if(!f) {
    POC_PERROR(temporary);
    return false;
  }
  bool ok = fwrite(part1, 1, length1, f) == length1
            && fputs(part2, f) >= 0 && fputs(part3, f) >= 0;
  ok = !fclose(f) && ok;
  if(!ok || rename(temporary, path)) {
    POC_PERROR(path);
    unlink(temporary);
    return false;
  }
  return true;
}


// s inside a JavaScript string between double quotes in a <script>: no ",
// \ or control characters, and no < either (a "</script>" in a title) nor
// U+2028/U+2029. malloc'd.
static char*
script_string(const char* s) {
  static const char hex[] = "0123456789abcdef";
  char* out = (char*)malloc(strlen(s) * 6 + 1);
  size_t n = 0;

  if(!out) {
    return 0;
  }
  for(const unsigned char* p = (const unsigned char*)s; *p; p++) {
    if(*p == 0xe2 && p[1] == 0x80 && (p[2] == 0xa8 || p[2] == 0xa9)) {
      n += (size_t)sprintf(out + n, "\\u202%c", p[2] == 0xa8 ? '8' : '9');
      p += 2;
    } else if(*p < 0x20 || *p == '"' || *p == '\\' || *p == '<' || *p == '>'
              || *p == '&') {
      n += (size_t)sprintf(out + n, "\\u00%c%c", hex[*p >> 4], hex[*p & 15]);
    } else {
      out[n++] = (char)*p;
    }
  }
  out[n] = 0;
  return out;
}


// One of the browser's own pages (start.html, settings.html) as path, with
// json in place of its mark ("@START@", "@SETTINGS@", which stand between
// quotes in the page: the JSON goes there, escaped for a <script>). True if
// it was written. Until 2026-11-05 13:50 the mark's two @ stayed around the
// JSON ("@{...}@"): JSON.parse() failed, and both pages showed none of the
// app's data (no favorites, no recent sites, the settings' defaults).
static bool
write_page(const char* path, const char* page_html, const char* mark,
           const char* json) {
  const char* at = strstr(page_html, mark);
  char* text = script_string(json);
  bool ok = at && text
            && write_file(path, page_html, (size_t)(at - page_html), text,
                          at + strlen(mark));
  if(!ok) {
    POC_LOG("%s was not written", path);
  }
  free(text);
  return ok;
}


// At the start: the saved list.
static void
favorites_load(void) {
  mkdir(DATA_DIR, 0755);  // made by the NetworkProcess, after this
  FILE* f = fopen(FAVORITES_FILE, "r");
  if(f) {
    char* json = (char*)malloc(FAVORITES_MAX_BYTES + 1);
    size_t length = json ? fread(json, 1, FAVORITES_MAX_BYTES, f) : 0;
    fclose(f);
    if(json) {
      json[length] = 0;
      favorites_json = json;
    }
  }
  POC_LOG("favorites: %zu bytes saved", favorites_json ? strlen(favorites_json) : 0);
}


// The bar's page is in: its favorites.
static void
favorites_to_bar(void) {
  if(!favorites_json) {
    return;
  }
  size_t size = strlen(favorites_json) * 6 + 32;
  char* script = (char*)malloc(size);
  if(script) {
    size_t n = (size_t)snprintf(script, size, "setFavorites(");
    n += json_string(script + n, size - n, favorites_json);
    snprintf(script + n, size - n, ")");
    run_script(BAR, script, 0, ignore_result);
    free(script);
  }
}


// JSON.stringify(favorites), from the bar after a change.
static void
favorites_got(WKTypeRef result, WKErrorRef, void*) {
  if(!result || WKGetTypeID(result) != WKStringGetTypeID()) {
    POC_LOG("favorites: the bar gave no list");
    return;
  }
  size_t size = WKStringGetMaximumUTF8CStringSize((WKStringRef)result);
  if(size > FAVORITES_MAX_BYTES) {
    POC_LOG("favorites: %zu bytes is too many, not saved", size);
    return;
  }
  char* json = (char*)malloc(size);
  if(!json) {
    return;
  }
  WKStringGetUTF8CString((WKStringRef)result, json, size);
  free(favorites_json);
  favorites_json = json;
  bool saved = write_file(FAVORITES_FILE, json, strlen(json), "", "");
  POC_LOG("favorites: %zu bytes %s", strlen(json), saved ? "saved" : "NOT saved");
}


// A string that grows: the JSON and the scripts for the browser's own pages
// and panels. failed: out of memory, s is not to be used.
typedef struct text {
  char* s;
  size_t n, size;
  bool failed;
} text_t;

#ifdef APP_RN
static bool notify_sites_json(text_t* t);                 // the sites answered for (notify_native_*), for the settings
static void notify_forget_command(const char* argument);  // the settings page: notify-forget?<origin>
#endif

static void
text_reserve(text_t* t, size_t more) {
  if(t->failed || t->n + more + 1 <= t->size) {
    return;
  }
  size_t size = t->size ? t->size : 1024;
  while(size < t->n + more + 1) {
    size *= 2;
  }
  char* s = (char*)realloc(t->s, size);
  if(!s) {
    t->failed = true;
    return;
  }
  if(!t->s) {
    s[0] = 0;
  }
  t->s = s;
  t->size = size;
}


static void
text_add(text_t* t, const char* s) {
  size_t n = strlen(s);
  text_reserve(t, n);
  if(!t->failed) {
    memcpy(t->s + t->n, s, n + 1);
    t->n += n;
  }
}


static void __attribute__((format(printf, 2, 3)))
text_printf(text_t* t, const char* format, ...) {
  va_list args;
  va_start(args, format);
  int n = vsnprintf(0, 0, format, args);
  va_end(args);
  if(n < 0) {
    t->failed = true;
    return;
  }
  text_reserve(t, (size_t)n);
  if(t->failed) {
    return;
  }
  va_start(args, format);
  vsnprintf(t->s + t->n, t->size - t->n, format, args);
  va_end(args);
  t->n += (size_t)n;
}


// s as a JSON string, with its quotes.
static void
text_json(text_t* t, const char* s) {
  text_reserve(t, strlen(s) * 6 + 16);
  if(!t->failed) {
    t->n += json_string(t->s + t->n, t->size - t->n, s);
  }
}


// Runs t in a pane, and frees it.
static void
text_run(int pane, text_t* t) {
  if(!t->failed && t->s) {
    run_script(pane, t->s, 0, ignore_result);
  }
  free(t->s);
  *t = text_t{};
}


// ---------------------------------------------------------------------------
// The settings (settings.c, settings.txt) and what each one changes. (The
// settings themselves are declared at the top: the display thread reads
// one.)

static ext_list_t extensions;
static WKWebsiteDataStoreRef data_store;
static WKPreferencesRef page_preferences;      // the page's only
static WKUserContentControllerRef page_content;  // the scripts in each page
static double last_gesture_ms = -1e9;  // cross or square last went down
#ifdef APP_RN
static double last_input_ms;  // on_input(): any button or stick (quit_request_check())
#endif
static unsigned page_serial;  // ?n after our own pages: a new document each time

// The interface's sounds (LINK_SOUND_*): the title makes them, quietly,
// with the setting on (docs/07, "Navegação magnética").
#define UI_SOUND_PERCENT 100

// With each sound, a vibration as short (the setting "Vibração"; the title
// stops it at its end, title/feel.c): the small motor ticks, the large one
// adds weight to opening and choosing. Motors 0..255, milliseconds.
static const struct {
  uint8_t large, small;
  uint8_t ms;
} ui_feels[LINK_SOUNDS] = {
  /* MOVE */ {0, 70, 16},
  /* OPEN */ {40, 120, 32},
  /* CLOSE */ {20, 90, 24},
  /* SELECT */ {90, 160, 40},
};

// Modo cinema (docs/07): a video covers the screen. No sounds, no
// vibrations, the light bar low, the cursor gone after CINEMA_CURSOR_MS.
#define CINEMA_ON (app.fullscreen && settings.cinema)

static void
ui_sound(int which) {
#ifdef REMOTE_UI
  if(CINEMA_ON) {
    return;
  }
  if(settings.sounds) {
    remote_sound(which, UI_SOUND_PERCENT);
  }
  if(settings.haptics && which >= 0 && which < LINK_SOUNDS) {
    remote_feel(ui_feels[which].large, ui_feels[which].small, ui_feels[which].ms);
  }
#else
  (void)which;
#endif
}

// The theme shown now: the setting, or for "estacao" the season's by the
// console's date (settings_theme_for(), docs/19-temas-da-estacao.md).
static const char*
theme_now(void) {
  time_t now = time(0);
  struct tm day;
  if(!localtime_r(&now, &day)) {
    return settings_theme_for(settings.theme, 1, 1);
  }
  return settings_theme_for(settings.theme, day.tm_mon + 1, day.tm_mday);
}

// The season's theme today, whatever the setting (the settings page's card).
static const char*
theme_season(void) {
  time_t now = time(0);
  struct tm day;
  if(!localtime_r(&now, &day)) {
    return settings_theme_for("estacao", 1, 1);
  }
  return settings_theme_for("estacao", day.tm_mon + 1, day.tm_mday);
}

// The triggers and the light bar as the settings have them (docs/07, "O
// comando"): at the start, when a setting changes and with the theme.
#define TRIGGER_STRENGTH 3

static void
feel_apply(void) {
#ifdef REMOTE_UI
  // A game's triggers are its own: no click.
  remote_triggers(settings.triggers && !app.game ? LINK_TRIGGERS_CLICK : LINK_TRIGGERS_OFF,
                  TRIGGER_STRENGTH);
  uint32_t accent = settings_accent(theme_now());
  int r = (accent >> 16) & 0xff, g = (accent >> 8) & 0xff, b = accent & 0xff;
  if(CINEMA_ON) {  // low, as a cinema's lights
    r /= 10;
    g /= 10;
    b /= 10;
  }
  remote_light(settings.lightbar, r, g, b);
#endif
}

static void
settings_store(void) {
  if(settings_save(&settings, SETTINGS_FILE)) {
    POC_LOG("settings: NOT saved");
  }
}


static bool
url_is_web(const char* url) {
  return !strncmp(url, "http://", 7) || !strncmp(url, "https://", 8);
}


// The host of an http(s) URL in lower case, "" for any other.
static const char*
url_host(const char* url, char* out, size_t size) {
  out[0] = 0;
  if(!url_is_web(url) || size < 2) {
    return out;
  }
  const char* p = strstr(url, "://") + 3;
  size_t end = strcspn(p, "/?#");
  for(size_t i = end; i > 0; i--) {  // user:password@host
    if(p[i - 1] == '@') {
      p += i;
      end -= i;
      break;
    }
  }
  size_t n = 0;
  for(size_t i = 0; i < end && p[i] != ':' && n + 1 < size; i++) {
    char c = p[i];
    out[n++] = c >= 'A' && c <= 'Z' ? (char)(c + 32) : c;
  }
  out[n] = 0;
  return out;
}


// What the log shows of a URL: not its query or fragment (the words of a
// search, a token).
static const char*
url_for_log(const char* url, char* out, size_t size) {
  size_t n = strcspn(url, "?#");
  snprintf(out, size, "%.*s%s", (int)n, url, url[n] ? " (...)" : "");
  return out;
}


// One of the browser's own pages (START_URL, SETTINGS_URL), with or without
// what follows: ?n, #section.
static bool
own_url(const char* url, const char* page_url) {
  size_t n = strlen(page_url);
  return !strncmp(url, page_url, n) && (!url[n] || url[n] == '?' || url[n] == '#');
}


// Is the page showing page_url, or with 0 either of our pages?
static bool
own_page_shown(const char* page_url) {
  char shown[2048];
  take_url(WKPageCopyCommittedURL(page(PAGE)), shown, sizeof(shown));
  return page_url ? own_url(shown, page_url)
                  : own_url(shown, START_URL) || own_url(shown, SETTINGS_URL);
}


#ifdef APP_RN
// (2026-11-14) The tab the browser itself last sent to an address (typed, a favorite, a tile, a tab reopened),
// until that load is in or has failed: what comes meanwhile in that tab is the user's own going, not a page's
// (decidePageNavigation(), the strict blocking). Main thread.
static WKPageRef app_nav_page;
static double app_nav_ms;
// 2026-11-14: and asked for again when the page BEFORE cancels it. The page shown is still alive while the
// next one's first bytes come, and a navigation of its own inside itself (Telegram Web moves its "#..." when
// a message comes) cancels the load that was on its way: the user asked for an address and stayed where they
// were (Gmail after Telegram Web, twice in show-sites: WebKitErrorDomain 302 a third of a second after the
// start). app_nav_url: the address; app_nav_started: its load has begun (a cancel that comes before that is
// the one of the load this one took the place of); app_nav_tries: how many times it was asked for again.
#define APP_NAV_TRIES 3
static char app_nav_url[4096];
static bool app_nav_started;
static int app_nav_tries;

static void
app_nav_note(WKPageRef p) {
  app_nav_page = p;
  app_nav_ms = poc_now_ms();
  app_nav_started = false;
}
#endif

static void
load_url(const char* url) {
  WKURLRef u = WKURLCreateWithUTF8CString(url);
#ifdef APP_RN
  app_nav_note(page(PAGE));
  if(url != app_nav_url) {  // (not the same address asked for again: app_nav_again())
    snprintf(app_nav_url, sizeof(app_nav_url), "%s", url);
    app_nav_tries = 0;
  }
#endif
  WKPageLoadURL(page(PAGE), u);
  WKRelease(u);
}

#ifdef APP_RN
// From didFailProvisionalNavigation(): true if the load that failed is the browser's own, cancelled by another
// than the browser, and it was asked for again.
static bool
app_nav_again(WKPageRef p, const char* domain, int code) {
  if(p != app_nav_page || p != page(PAGE) || !app_nav_started || code != 302 || strcmp(domain, "WebKitErrorDomain")
     || !app_nav_url[0] || app_nav_tries >= APP_NAV_TRIES || poc_now_ms() - app_nav_ms > 20000) {
    return false;
  }
  app_nav_tries++;
  POC_LOG("the address asked for was cancelled by the page before, %.0f ms after it began: asked for again (%d of %d)",
          poc_now_ms() - app_nav_ms, app_nav_tries, APP_NAV_TRIES);
  load_url(app_nav_url);
  return true;
}
#endif


// A web page loads again (a setting that changes how it is shown).
static void
reload_web_page(void) {
  char url[2048];
  take_url(WKPageCopyCommittedURL(page(PAGE)), url, sizeof(url));
  if(url_is_web(url)) {
    WKPageReload(page(PAGE));
  }
}


static void
ring_off(void) {
  if(!app.ring.on) {
    return;
  }
  pthread_mutex_lock(&app.lock);
  app.ring.on = false;
  app.dirty = true;
  pthread_cond_signal(&app.cond);
  pthread_mutex_unlock(&app.lock);
}


// The language of the browser's own pages and messages (lang.js,
// docs/13-idioma.md): the panels get setLang(), the start and settings
// pages it too when shown (and in their data when written), the phone's
// page in its state. The pages translate the app's messages to the bar
// themselves; what reaches no page asks lang_en().
static bool
lang_en(void) {
#ifdef APP_RN
  return strcmp(settings.lang, "pt") != 0;  // (the languages of the pages' tables: what the app itself says, in English)
#else
  return !strcmp(settings.lang, "en");
#endif
}

// The pages' language: "pt", "en", or (RN) one of the tables' (browser-rn/pages/lang-more.js).
static const char*
lang_code(void) {
#ifdef APP_RN
  static const char* const more[] = {"en", "es", "fr", "de", "it", "nl"};
  for(const char* code : more) {
    if(!strcmp(settings.lang, code)) {
      return code;
    }
  }
  return "pt";
#else
  return lang_en() ? "en" : "pt";
#endif
}

#ifdef APP_RN
// The browser's language for the console's (sceSystemServiceParamGetInt(1), the system's own list): one of those
// there is a table for, English for every other (2026-11-14; tests-host/make-names-test.py).
extern "C" int sceSystemServiceParamGetInt(int id, int* value);

static const char*
lang_of_console(int console) {
  switch(console) {
  case 7: case 17: return "pt";  // Portugal, Brazil
  case 3: case 20: return "es";  // Spain, Latin America
  case 2: case 22: return "fr";  // France, Canada
  case 4: return "de";
  case 5: return "it";
  case 6: return "nl";
  default: return "en";
  }
}

// 2026-11-14: "pages do not connect to this console's own services" (poc/webkit/security) is on only with
// BROWSERPS5_NO_LOCAL=1 in the engine's settings file, which comes with the engine (browser-rn/pack-lib-rn.sh).
// With no file, or a file without that line, the engine lets them connect, and nothing says so. At the start,
// before the engine's network process is there to read it: a file that does not say, either way, gets the line;
// one that says "0" is left as it is (someone's test); the log says how it stands.
#define ENGINE_ENV_FILE "/user/homebrew/browserps5-rn/engine-env.txt"

static void
engine_env_check(void) {
  // The engine reads these three, in this order, a later one over an earlier one (webkit/crash-report,
  // browserps5_debug_env()): the first and the last are tests' files, and no release has them.
  static const char* const files[] = {"/user/homebrew/browserps5/data/env.txt", ENGINE_ENV_FILE,
                                      "/user/homebrew/browserps5-rn/data/env.txt"};
  bool says = false, on = false, own_file = false, own_ends = true;

  for(int k = 0; k < 3; k++) {
    size_t length = 0;
    char* text = read_whole(files[k], 64 * 1024, &length);
    if(!text) {
      continue;
    }
    for(const char* line = text; line && *line;) {
      if(!strncmp(line, "BROWSERPS5_NO_LOCAL=", 20)) {
        says = true;
        on = line[20] == '1';
      }
      const char* next = strchr(line, '\n');
      line = next ? next + 1 : 0;
    }
    if(k == 1) {
      own_file = true;
      own_ends = !length || text[length - 1] == '\n';
    } else {
      POC_LOG("engine settings: a TEST's file is there, and the engine reads it: %s (%zu bytes)", files[k], length);
    }
    free(text);
  }
  if(says) {
    POC_LOG("engine settings: pages %s connect to this console's own services (BROWSERPS5_NO_LOCAL is %s)", on ? "do not" : "CAN",
            on ? "1" : "not 1");
  } else {
    FILE* f = fopen(ENGINE_ENV_FILE, "a");
    bool added = false;
    if(f) {
      added = fprintf(f, "%sBROWSERPS5_NO_LOCAL=1\n", own_ends ? "" : "\n") > 0;
      added = !fclose(f) && added;
    }
    POC_LOG("engine settings: %s BROWSERPS5_NO_LOCAL line: %s", own_file ? "the file had no" : "no file, so no",
            added ? "added (pages do not connect to this console's own services)"
                  : "it could NOT be added: pages CAN connect to this console's own services");
  }
}
#endif

#ifdef APP_RN
#include "lang_c.h"

// What the app itself says, in the pages' other languages (browser-rn/pages/lang_c.h): the English text's row
// there, or the English text. In Portuguese and in English nothing changes: LM() is only asked where the app
// already chose the English.
static const char*
lang_more(const char* en) {
  static const char* const codes[] = {"es", "fr", "de", "it", "nl"};
  const char* code = lang_code();

  for(int column = 0; column < 5; column++) {
    if(strcmp(code, codes[column])) {
      continue;
    }
    for(const lang_c_row& row : lang_c) {
      if(!strcmp(row.en, en)) {
        return row.to[column];
      }
    }
    break;
  }
  return en;
}
#define LM(en) lang_more(en)
#else
#define LM(en) (en)
#endif

static void
lang_apply(void) {
  char script[32];
  snprintf(script, sizeof(script), "setLang('%s')", lang_code());
  static const int panes[] = {BAR, MENU, DIALOG, KEYS, RADIAL, SWITCHER};
  for(int pane : panes) {
    run_script(pane, script, 0, ignore_result);
  }
  if(own_page_shown(START_URL) || own_page_shown(SETTINGS_URL)) {
    run_script(PAGE, script, 0, ignore_result);
  }
  POC_LOG("lang: %s", lang_en() ? "en" : "pt");
}


// The theme (theme.css) in the browser's panels, the ring's colour. Our
// pages take it when they are written.
static char theme_shown[32];  // what theme_apply() last put on (the tick's check)

static void
theme_apply(void) {
  char script[96];
  // settings_set() takes only the themes' names: no quotes in it.
  snprintf(theme_shown, sizeof(theme_shown), "%s", theme_now());
  snprintf(script, sizeof(script), "setTheme('%s')", theme_shown);
  static const int panes[] = {BAR, MENU, DIALOG, KEYS, RADIAL, SWITCHER};
  for(int pane : panes) {
    run_script(pane, script, 0, ignore_result);
  }
  pthread_mutex_lock(&app.lock);
  app.accent = settings_accent(theme_now());
  app.dirty = true;
  pthread_cond_signal(&app.cond);
  pthread_mutex_unlock(&app.lock);
  feel_apply();
}


// own: the page is going to one of ours, which need JavaScript whatever the
// setting says (or nothing could turn it on again).
static void
preferences_apply(bool own) {
  WKPreferencesSetJavaScriptEnabled(page_preferences, settings.javascript || own);
  WKPreferencesSetLoadsImagesAutomatically(page_preferences, settings.images || own);
  WKPreferencesSetJavaScriptCanOpenWindowsAutomatically(page_preferences, settings.popups);
  WKPreferencesSetMediaPlaybackRequiresUserGesture(page_preferences, !settings.autoplay);
}


#ifdef APP_RN
// BrowserPS5 RN (2026-11-11 22:00): the name this browser gives YouTube is its own, a PlayStation 5's WebKit
// (the form of the console's own browser), not "Safari on a Mac". As Safari, YouTube's check of the browser refused the film a minute in, in half
// of the runs (HTML5_SPS_UMP_STATUS_REJECTED: 2 of 4 on the full engine, with WebGL and Web Audio there, the
// same as before them); as what it is, in none of four (rn-feel-20261111-2136.. to 2145..-show-yt-ps5ua). Nothing
// of YouTube's check is touched: it runs as it is and gets the truth. The other sites keep the Safari name
// (WhatsApp Web and others refuse a name they do not know). Only with the desktop name chosen in the settings:
// the mobile one still gives YouTube's mobile page. /data/homebrew/PPSA97024/ua-safari-youtube: Safari there too.
#define USER_AGENT_PS5 "Mozilla/5.0 (PlayStation; PlayStation 5/13.60) AppleWebKit/605.1.15 (KHTML, like Gecko) " \
                       "Version/18.0 Safari/605.1.15"

static const char*
user_agent_for(const char* url) {
  static int safari_there = -1;
  char host[256];

  if(!settings.desktop) {
    return USER_AGENT_MOBILE;
  }
  if(safari_there < 0) {
    safari_there = !access("/data/homebrew/" TITLE_ID "/ua-safari-youtube", F_OK);
  }
  url_host(url ? url : "", host, sizeof(host));
  const size_t n = strlen(host);
  static const char* const sites[] = {"youtube.com", "youtube-nocookie.com", "youtu.be"};
  for(const char* site : sites) {
    const size_t m = strlen(site);
    if(!safari_there && ((n == m && !strcmp(host, site)) || (n > m && host[n - m - 1] == '.' && !strcmp(host + n - m, site)))) {
      return USER_AGENT_PS5;
    }
  }
  return USER_AGENT;
}

// A tab's page is about to go to url (its main frame): the name it gives there. Before the request leaves.
static void
user_agent_page(WKPageRef p, const char* url) {
  static char now[512];
  const char* want = user_agent_for(url);

  take_string(WKPageCopyCustomUserAgent(p), now, sizeof(now));
  if(strcmp(now, want)) {
    WKStringRef user_agent = string(want);
    WKPageSetCustomUserAgent(p, user_agent);
    WKRelease(user_agent);
    POC_LOG("user agent: %s for %.60s", !strcmp(want, USER_AGENT_PS5) ? "the PlayStation 5's" : "the usual one", url ? url : "");
  }
}
#endif

static void
user_agent_apply(void) {
#if defined(SHOW_YT_PS5UA) || defined(SHOW_YT_TV)
  // make SHOW=1 SHOW_YT=1 SHOW_YT_PS5UA=1 (2026-11-11 21:40): the YouTube trace saying what this browser is, a
  // WebKit on a PlayStation 5 (the console's own browser's name), not Safari on a Mac: does YouTube's check of the
  // browser still refuse half of the films after a minute (it does with WebGL and Web Audio there: 2 of 4)?
  // (Since 22:00 the normal build does this by itself for YouTube: user_agent_for().)
  WKStringRef user_agent = string("Mozilla/5.0 (PlayStation; PlayStation 5/13.60) AppleWebKit/605.1.15 (KHTML, like Gecko) "
                                  "Version/18.0 Safari/605.1.15");
#elif defined(APP_RN)
  char url[4096];
  take_url(WKPageCopyActiveURL(page(PAGE)), url, sizeof(url));
  WKStringRef user_agent = string(user_agent_for(url));  // the page in front keeps its site's name
#else
  WKStringRef user_agent = string(settings.desktop ? USER_AGENT : USER_AGENT_MOBILE);
#endif
  WKPageSetCustomUserAgent(page(PAGE), user_agent);
  WKRelease(user_agent);
}


static void
data_removed(void*) {
}


static void
cookies_apply(void) {
  WKHTTPCookieStoreSetHTTPCookieAcceptPolicy(
      WKWebsiteDataStoreGetHTTPCookieStore(data_store),
      settings.third_party_cookies ? kWKHTTPCookieAcceptPolicyAlways
                                   : kWKHTTPCookieAcceptPolicyOnlyFromMainDocumentDomain,
      0, data_removed);
}


static void
zoom_apply(void) {
  char script[48];

  app.zoom = settings.zoom / 100.0;
  WKPageSetPageZoomFactor(page(PAGE), app.zoom);
  snprintf(script, sizeof(script), "setZoom(%d)", settings.zoom);
  run_script(MENU, script, 0, ignore_result);
  ring_off();
}


// zoom?+, zoom?- (the menu) and zoom?100 (the bar's 125%, to go back).
static void
zoom_step(const char* how) {
  static const int zooms[] = {50, 67, 80, 90, 100, 110, 125, 150, 175, 200, 250, 300};
  int z = settings.zoom;

  if(!strcmp(how, "+")) {
    for(int step : zooms) {
      if(step > settings.zoom) {
        z = step;
        break;
      }
    }
  } else if(!strcmp(how, "-")) {
    for(int step : zooms) {
      if(step < settings.zoom) {
        z = step;
      }
    }
  } else {
    z = 100;
  }
  char value[16];
  snprintf(value, sizeof(value), "%d", z);
  if(settings_set(&settings, "zoom", value) == 1) {
    settings_store();
    zoom_apply();
    bar_update(0);
    POC_LOG("zoom %d%%", z);
  }
}


// The bar's height (the favorites under the address, or not; none in full
// screen) and so the page's place.
static void
layout(void) {
  int bar = app.fullscreen ? 0 : settings.favbar ? BAR_HEIGHT : BAR_HEIGHT_NOFAV;

  run_script(BAR, settings.favbar ? "setFavbar(true)" : "setFavbar(false)", 0,
             ignore_result);
  if(bar == app.bar_height) {
    return;
  }
  pthread_mutex_lock(&app.lock);
  app.bar_height = bar;
  if(bar) {  // in full screen it keeps its size, unseen
    app.panes[BAR].height = bar;
    app.panes[BAR].redraw = true;
  }
  pane_t* p = &app.panes[PAGE];
  p->top = bar;
  p->height = DISPLAY_HEIGHT - bar;
  p->pixels = app.screen + (size_t)bar * DISPLAY_WIDTH;
  app.ring.on = false;
  app.dirty = true;
  pthread_cond_signal(&app.cond);
  pthread_mutex_unlock(&app.lock);
  if(bar) {
    WKViewSetSize(app.panes[BAR].view, WKSizeMake(DISPLAY_WIDTH, bar));
  }
  WKViewSetSize(p->view, WKSizeMake(DISPLAY_WIDTH, DISPLAY_HEIGHT - bar));
#ifndef GPU_COMPOSITING
  if(bar) {  // its rows had the page in them
    app.panes[BAR].damage = WKRectMake(0, 0, DISPLAY_WIDTH, bar);
    paint(&app.panes[BAR]);
  }
#endif
  POC_LOG("layout: the bar is %d px, the page %d px", bar, DISPLAY_HEIGHT - bar);
}


static void menu_hide(void);
static void radial_hide(bool sound);
static void switcher_hide(void);
static void switcher_show(void);
static void switcher_toggle(void);
static bool tab_new(const char* url);
#ifdef APP_RN
// BrowserPS5 RN (2026-11-11): what the menu's second row of tiles does.
static bool tab_is_private(void);
static void find_open(void);
static void reader_said(WKTypeRef result, WKErrorRef error, void* ctx);
static void translate_page(void);
static void find_client_install(WKPageRef p);
static bool find_input(const input_event_t* e);
static bool tab_private_next;  // the next tab_new() makes a private one
// The tab whose page has something of ours open over it (the link menu, the
// reading mode: browserps5:overlay?1), or -1: circle closes that first.
static int overlay_tab = -1;
static bool overlay_input(const input_event_t* e);
// The tabs closed in this run, the last one last (tab_close()): the strip's
// "reopen" brings the last back (tab_reopen()). Never a private one.
static char closed_tabs[8][2048];
static int closed_count;
static void tab_reopen(void);
// A page's file chooser (<input type="file">): the dialog's files mode.
static void upload_client_install(WKPageRef p);
static void upload_end(bool cancel);
static bool upload_command(const char* command, const char* text);
static bool upload_pending(void);
// A page's alert(), confirm() and prompt(): the dialog's ask mode (ui_split only).
static bool ask_command(const char* command, const char* text);
static void ask_abandon(void);
static bool ask_native_input(const input_event_t* e);
#endif
#ifdef APP_RN
static void tab_switch(int i);
static void tab_close(int i);
static int tab_with_host(const char* url);
static int tab_in_front(void);
static int tab_front_id(void);  // the id of the tab in front, or -1
static void favorite_open(const char* encoded);
#endif
static void bar_tabs(void);

// BrowserPS5 RN (2026-11-11): is the page in front YouTube's page for televisions and consoles
// (https://www.youtube.com/tv, which this browser is given under its own name: user_agent_for())? It is driven
// by the D-pad (dpad_keys_now()) and has the whole screen by itself, as on a television (tv_page_fullscreen()).
#ifdef APP_RN
static bool
tv_url(const char* url) {
  static const char tv[] = "https://www.youtube.com/tv";
  const char after = strncmp(url, tv, sizeof(tv) - 1) ? 'x' : url[sizeof(tv) - 1];
  return after == 0 || after == '#' || after == '/' || after == '?';
}
#endif

static bool
tv_page_now(void) {
#ifdef APP_RN
  char url[4096];
  take_url(WKPageCopyActiveURL(page(PAGE)), url, sizeof(url));
  return tv_url(url);
#else
  return false;
#endif
}

// A video asked for the whole screen (ext/fullscreen.js): no bar. Only right
// after a press: a site with no bar to show its address could pass for
// another.
static void
fullscreen_set(bool on) {
  if(on == app.fullscreen) {
    return;
  }
  if(on && poc_now_ms() - last_gesture_ms > 2500) {
    POC_LOG("full screen asked for with no press: not taken");
    return;
  }
  menu_hide();
  pthread_mutex_lock(&app.lock);
  app.fullscreen = on;
  pthread_mutex_unlock(&app.lock);
  layout();
  if(!on) {
    app.bar_state[0] = 0;
    bar_update(0);
  }
  feel_apply();  // the cinema's light, or the theme's again
  POC_LOG("full screen %s%s", on ? "on" : "off", on && settings.cinema ? " (cinema)" : "");
}

// A page came in, or another tab came in front: the bar is back, but for YouTube's page for televisions, which
// takes the whole screen (the menu's "Ecrã inteiro" brings the bar back; circle is the page's own "back").
static void
tv_page_fullscreen(void) {
  if(tv_page_now()) {
    last_gesture_ms = poc_now_ms();  // the app's own choice for this one page, not a page's request
    fullscreen_set(true);
  } else {
    fullscreen_set(false);
  }
}

// The answer of __bps5enterFullscreen(): no video in the page, so the page
// itself gets the whole screen (a video's player asks for it by itself).
static void
fullscreen_video_found(WKTypeRef result, WKErrorRef error, void* context) {
  (void)error;
  (void)context;
  if(!(result && WKGetTypeID(result) == WKBooleanGetTypeID()
       && WKBooleanGetValue((WKBooleanRef)result))) {
    fullscreen_set(true);
  }
}

static bool native_fullscreen_exit(void);

// The menu's "Ecrã inteiro" and the radial's: the video that plays, with its
// player, covers the screen (ext/fullscreen.js); with no video, the page
// does. Again: back to the bar, as circle does.
static void
fullscreen_toggle(void) {
  if(app.fullscreen) {
    if(!native_fullscreen_exit()) {
      run_script(PAGE, "window.__bps5exitFullscreen && __bps5exitFullscreen()", 0,
                 ignore_result);
      fullscreen_set(false);
    }
    return;
  }
  last_gesture_ms = poc_now_ms();  // the press that chose it
  run_script(PAGE, "!!(window.__bps5enterFullscreen && __bps5enterFullscreen())", 0,
             fullscreen_video_found);
}


// ---------------------------------------------------------------------------
// The ad blocker's hosts (ext/adhosts.txt).

#define AD_HOSTS_MAX 1024
static char* ad_text;
static const char* ad_list[AD_HOSTS_MAX];
static int ad_count;
static char* ad_json;          // ["host", ...], for ext/adblock.js
static unsigned ads_blocked;   // frames refused

static void
ad_hosts_init(void) {
  text_t json = {};

  ad_text = strdup(ext_adhosts_txt);
  text_add(&json, "[");
  for(char* line = ad_text; line && *line && ad_count < AD_HOSTS_MAX;) {
    char* next = strchr(line, '\n');
    if(next) {
      *next++ = 0;
    }
    while(*line == ' ' || *line == '\t') {
      line++;
    }
    size_t n = strlen(line);
    while(n && (line[n - 1] == ' ' || line[n - 1] == '\t' || line[n - 1] == '\r')) {
      line[--n] = 0;
    }
    if(n && line[0] != '#' && line[0] != '[') {
      if(ad_count) {
        text_add(&json, ", ");
      }
      text_json(&json, line);
      ad_list[ad_count++] = line;
    }
    line = next;
  }
  text_add(&json, "]");
  ad_json = json.failed ? strdup("[]") : json.s;
  POC_LOG("ad blocker: %d hosts", ad_count);
}


// The host or one it is under is in the list.
static bool
ad_host(const char* host) {
  for(const char* h = host; *h;) {
    for(int i = 0; i < ad_count; i++) {
      if(!strcmp(h, ad_list[i])) {
        return true;
      }
    }
    const char* dot = strchr(h, '.');
    if(!dot) {
      break;
    }
    h = dot + 1;
  }
  return false;
}


// The page's site is one where the ad blocker does nothing (settings
// "ad_allow": the radial's "Anúncios aqui", the settings' list).
static bool
ad_site_allowed(WKPageRef p) {
  char url[4096], host[256];
  if(!settings.ad_allow[0]) {
    return false;
  }
  take_url(WKPageCopyCommittedURL(p), url, sizeof(url));
  return url_is_web(url) && settings_ad_allowed(&settings, url_host(url, host, sizeof(host)));
}


// ---------------------------------------------------------------------------
// The scripts in each page: the built-in ones (ext/) and the extensions the
// user installed (user scripts, settings.c).

// No new windows: a link for one opens in a new tab (browserps5:newtab, from
// a frame of its own as ext/fullscreen.js does; the app takes it right after
// a press only), or in this one when the page handles the click itself or
// stops it on its way up; window.open() opens in this one when it follows a
// press (or always, with "Janelas pop-up" on).
//
// 2026-11-09 17:0x: the screen is SDR (matchMedia('(dynamic-range: high)')
// says false), but WebKit took 10-bit VP9 as playable (FFmpeg decodes it), so
// YouTube sent 4K60 as HDR VP9.2 (itag 337): ~40 Mbps, the buffer ~6 s ahead,
// on the CPU (the PS5's decoder takes 8-bit only), and PQ colours shown as
// SDR ones (no tone mapping: washed out). Pages see the 10-bit and HDR types
// as not playable while the screen is SDR: YouTube sends 8-bit SDR at the
// same sizes and rates (4K60 too: no size limit), half the bits, on the
// PS5's decoder (title/video_hw.h).
#ifdef SHOW_YT
static const char show_diag_js[] = R"(function () {
  var d = window.__bps5diag = {errors: [], res: {}, order: []};
  function rec(e) {
    var u; try { u = new URL(e.name); } catch (x) { return; }
    if (/[.](png|jpe?g|webp|gif|svg|woff2?|css|ico)$/.test(u.pathname) || /^(i[0-9]*[.]ytimg|yt3[.]ggpht|fonts[.]g|lh3[.]google)/.test(u.hostname)) return;
    var key = u.hostname + u.pathname.replace(/[A-Za-z0-9_-]{14,}/g, '*').slice(0, 56);
    var b = d.res[key];
    if (!b) { b = d.res[key] = {n: 0, st: {}, first: e.startTime, last: 0, slow: 0, bytes: 0}; d.order.push(key); }
    b.n++; b.last = e.startTime; b.slow = Math.max(b.slow, e.duration); b.bytes += e.transferSize || 0;
    var st = e.responseStatus === undefined ? '?' : e.responseStatus; b.st[st] = (b.st[st] || 0) + 1;
  }
  try { new PerformanceObserver(function (l) { l.getEntries().forEach(rec); }).observe({type: 'resource', buffered: true}); }
  catch (e) { d.errors.push('no observer: ' + e); }
  addEventListener('error', function (e) {
    if (d.errors.length >= 16) return;
    var at = Math.round(performance.now() / 1000) + ' s ';
    if (e.message) d.errors.push(at + 'error: ' + String(e.message).slice(0, 120) + ' @' + String(e.filename || '').slice(-44) + ':' + e.lineno);
    else if (e.target && e.target.tagName) d.errors.push(at + 'load failed: ' + e.target.tagName + ' ' + String(e.target.src || e.target.href || '').slice(0, 100));
  }, true);
  addEventListener('unhandledrejection', function (e) {
    if (d.errors.length < 16) d.errors.push(Math.round(performance.now() / 1000) + ' s rejection: ' + String(e.reason && (e.reason.message || e.reason)).slice(0, 150));
  });
  if (location.pathname.indexOf('/pdfjs/') === 0) ['error', 'warn'].forEach(function (k) { var o = console[k]; console[k] = function () {
    if (d.errors.length < 16) d.errors.push(k + ': ' + Array.prototype.map.call(arguments, String).join(' ').slice(0, 170)); return o.apply(console, arguments); }; });
})";
#endif
static const char sdr_js[] = R"(function () {
  'use strict';
  try { if (matchMedia('(dynamic-range: high)').matches) return; } catch (e) {}
  const deep = /vp0?9\.0[23]|vp9\.2|av01\.\d\.\d+[mh]\.1[02]|hev1\.2|hvc1\.2|dvh[e1]|dav1|eotf=(smpte2084|arib-std-b67)/i;
  for (const name of ['MediaSource', 'ManagedMediaSource', 'WebKitMediaSource']) {
    const MS = window[name];
    if (MS && typeof MS.isTypeSupported === 'function') {
      const orig = MS.isTypeSupported.bind(MS);
      MS.isTypeSupported = function (t) { return deep.test(String(t)) ? false : orig(t); };
    }
  }
  const cpt = HTMLMediaElement.prototype.canPlayType;
  HTMLMediaElement.prototype.canPlayType = function (t) { return deep.test(String(t)) ? '' : cpt.call(this, t); };
  const mc = navigator.mediaCapabilities;
  if (mc && typeof mc.decodingInfo === 'function') {
    const di = mc.decodingInfo.bind(mc);
    mc.decodingInfo = function (c) {
      const v = c && c.video;
      if (v && (deep.test(String(v.contentType)) || v.hdrMetadataType ||
                /pq|hlg/i.test(String(v.transferFunction || ''))))
        return Promise.resolve({supported: false, smooth: false, powerEfficient: false, configuration: c});
      return di(c);
    };
  }
}
)";

#ifdef APP_RN
// BrowserPS5 RN (2026-11-10 23:10): the sites' own notifications as the
// PS5's (TwitterPS5 (XPS5) posts them from X's API; here any of these
// sites, by itself, with no login of ours). This WebKit has no Notification
// (and no script message handlers on PlayStation): notify_js gives these
// sites one, granted, whose notices come to the app as a main-frame
// navigation to browserps5:notify?<title>&<text>&<icon> (an iframe's would
// fall to their CSP's frame-src); notify_take() posts the toast and marks
// the tab behind, whose last notice is "clicked" when it comes in front
// (WhatsApp and Telegram open that chat).
static const char* const notify_sites[][2] = {
  {"whatsapp.com", "WhatsApp"}, {"telegram.org", "Telegram"}, {"discord.com", "Discord"},
  {"messenger.com", "Messenger"}, {"facebook.com", "Facebook"}, {"instagram.com", "Instagram"},
  {"x.com", "X"}, {"twitter.com", "X"}, {"mail.google.com", "Gmail"}, {"chat.google.com", "Google Chat"},
  {"outlook.live.com", "Outlook"}, {"outlook.office.com", "Outlook"}, {"outlook.office365.com", "Outlook"},
  {"teams.microsoft.com", "Teams"}, {"teams.live.com", "Teams"}, {"app.slack.com", "Slack"},
  {"web.skype.com", "Skype"},
};

// 2026-11-10 22:45: service workers only for WhatsApp Web, the one site
// whose workers run in the title's WebProcess (BROWSERPS5_SW_TITLE_DOMAIN):
// any other site's go to a payload WebProcess, which the PS5 stopped 6 s in
// (Telegram Web's, 22:35), and the browser then took ~35 s for a page in
// another tab. Other sites see none (navigator.serviceWorker), as in
// Safari's private windows of old, and work without (Telegram falls back
// to its own); their registrations of before go once at the start
// (sw_only_once()).
static const char sw_only_js[] = R"(function (keep) {
  'use strict';
  const host = location.hostname;
  if (keep.some((d) => host === d || host.endsWith('.' + d))) return;
  try { delete Navigator.prototype.serviceWorker; } catch (e) {}
}
)";

static const char notify_js[] = R"(function (hosts) {
  'use strict';
  // 2026-11-11 21:45: the "full" engine has a Notification of its own, which says "denied" (nobody
  // answers its question yet): these sites keep this one, on every engine. Once per page.
  if (window !== window.top || window.__bps5_notify_click) return;
  const host = location.hostname;
  if (!hosts.some((d) => host === d || host.endsWith('.' + d))) return;
  const permission = 'granted';
  const cut = (v, n) => Array.from(String(v === undefined || v === null ? '' : v).replace(/\s+/g, ' ').trim()).slice(0, n).join('');
  const queue = [];
  let busy = false;
  const flush = () => {
    if (!queue.length) { busy = false; return; }
    if (document.readyState !== 'complete') { setTimeout(flush, 500); return; }
    try { location.href = 'browserps5:notify?' + queue.shift(); } catch (e) {}
    setTimeout(flush, 400);
  };
  const send = (n) => {
    try {
      queue.push([cut(n.title, 120), cut(n.body, 220), /^https:/.test(n.icon) && n.icon.length <= 900 ? n.icon : '']
        .map(encodeURIComponent).join('&'));
    } catch (e) { return; }
    if (queue.length > 4) queue.shift();
    if (!busy) { busy = true; setTimeout(flush, 0); }
  };
  const fire = (n, type) => {
    const e = new Event(type, {cancelable: true});
    const f = n['on' + type];
    if (typeof f === 'function') {
      try { f.call(n, e); } catch (x) { setTimeout(() => { throw x; }); }
    }
    n.dispatchEvent(e);
  };
  const closed = new WeakSet();
  let last = null;
  class Notification extends EventTarget {
    constructor(title, options) {
      super();
      if (!arguments.length)
        throw new TypeError("Failed to construct 'Notification': 1 argument required, but only 0 present.");
      const o = options || {};
      let icon = '';
      try { icon = o.icon ? new URL(String(o.icon), location.href).href : ''; } catch (e) {}
      Object.assign(this, {title: String(title), body: o.body === undefined ? '' : String(o.body),
        tag: o.tag === undefined ? '' : String(o.tag), icon, data: o.data === undefined ? null : o.data,
        dir: o.dir || 'auto', lang: o.lang || '', badge: o.badge || '', image: o.image || '',
        silent: o.silent === undefined ? null : !!o.silent, renotify: !!o.renotify,
        requireInteraction: !!o.requireInteraction, timestamp: o.timestamp || Date.now(),
        vibrate: [], actions: [], onclick: null, onshow: null, onerror: null, onclose: null});
      send(this);
      last = this;
      setTimeout(() => fire(this, 'show'), 0);
    }
    close() {
      if (closed.has(this)) return;
      closed.add(this);
      setTimeout(() => fire(this, 'close'), 0);
    }
    static get permission() { return permission; }
    static requestPermission(done) {
      if (typeof done === 'function') setTimeout(() => done(permission), 0);
      return Promise.resolve(permission);
    }
    static get maxActions() { return 0; }
  }
  Object.defineProperty(window, 'Notification', {value: Notification, writable: true, configurable: true});
  // The app, when this tab comes in front with a notice unread: its click.
  Object.defineProperty(window, '__bps5_notify_click', {value() {
    const n = last;
    last = null;
    if (n) fire(n, 'click');
  }});
  try {
    const P = navigator.permissions;
    if (P && P.query) {
      const query = P.query.bind(P);
      P.query = (d) => d && d.name === 'notifications'
        ? Promise.resolve(Object.assign(new EventTarget(), {name: 'notifications', state: permission, onchange: null}))
        : query(d);
    }
  } catch (e) {}
  const R = window.ServiceWorkerRegistration;
  if (R) {
    R.prototype.showNotification = function (title, options) {
      new Notification(title, options);
      return Promise.resolve();
    };
    R.prototype.getNotifications = function () { return Promise.resolve([]); };
  }
  // Telegram Web shows its notices from its service worker when push is
  // there (none here): in the page instead.
  if ('PushManager' in window) {
    try { delete window.PushManager; } catch (e) {}
  }
}
)";
#endif

#ifdef APP_RN
// 2026-11-14: AWAY_PAUSE_MEDIA and AWAY_PLAY_MEDIA (further down) reach the
// page's own frames and those of its origin. A film site's player is a frame of another site: its video
// went on playing behind the PS5's menu (muted), its frames still coming to the title's decoder while the
// system closed the title. This one is in every frame from its start (extensions_apply()): the page's
// frame is told (away_js's __bps5away), stops or starts its media and passes the word to its frames, each
// of which does the same (postMessage: it crosses origins). A message of ours never reaches the page's own
// listeners, and only a frame's parent, or the frame itself, is listened to.
static const char away_js[] = R"(function () {
  'use strict';
  if (window.__bps5away) return;
  const act = (play) => {
    try {
      document.querySelectorAll('video,audio').forEach((m) => {
        if (!play) {
          if (!m.paused) { m.setAttribute('data-bps5-away', '1'); m.pause(); }
        } else if (m.hasAttribute('data-bps5-away')) {
          m.removeAttribute('data-bps5-away');
          const r = m.play();
          if (r && r.catch) r.catch(() => {});
        }
      });
    } catch (e) { }
    for (let i = 0; i < frames.length; i++) {
      try { frames[i].postMessage({__bps5away: play ? 'play' : 'pause'}, '*'); } catch (e) { }
    }
  };
  addEventListener('message', (e) => {
    const d = e.data;
    if (!d || (d.__bps5away !== 'pause' && d.__bps5away !== 'play')) return;
    e.stopImmediatePropagation();
    if (e.source === parent || e.source === window) act(d.__bps5away === 'play');
  }, true);
  window.__bps5away = act;
}
)";
#define AWAY_PAUSE_FRAMES "window.__bps5away && __bps5away(false)"
#define AWAY_PLAY_FRAMES  "window.__bps5away && __bps5away(true)"
#endif

// 2026-11-14, BrowserPS5 RN (rn): a window a page's script asks for used to take the tab's own place (top.location = its address),
// so the first press on a film's page replaced the film with an advertisement. With rn the app decides
// (browserps5:popup, popup_asked()): the same site's in a new tab, another site's not at all. Asked for by
// window.open(), by a click the page made itself on a link for a new window (not the user's: isTrusted), or
// by the user's press on a link for a new window to another site that lies over most of the page (the
// invisible link such pages put over everything).
static const char tabs_js[] = R"(function (popups, rn) {
  'use strict';
  if (window.__bps5tabs) return;
  window.__bps5tabs = true;
  let link = null, made = false;
  const ask = (what, to) => {
    const f = document.createElement('iframe');
    f.style.cssText = 'display: none';
    f.src = 'browserps5:' + what + '?' + encodeURIComponent(to);
    (document.body || document.documentElement).appendChild(f);
    setTimeout(() => f.remove(), 1000);
  };
  const site = (host) => {
    const p = String(host).toLowerCase().split('.');
    const n = p.length > 2 && p[p.length - 1].length === 2 && /^(com?|org|net|gov|edu|ac)$/.test(p[p.length - 2]) ? 3 : 2;
    return p.slice(-n).join('.');
  };
  document.addEventListener('click', (e) => {
    const a = e.target && e.target.closest ? e.target.closest('a[target]') : null;
    link = null;
    made = false;
    if (a && !/^(_self|_parent|_top)?$/i.test(a.target) && !a.hasAttribute('download')) {
      a.target = '_top';
      if (/^https?:/i.test(a.href)) {
        link = a;
        if (rn) {
          let over = false;
          try {
            const r = a.getBoundingClientRect();
            over = site(new URL(a.href).hostname) !== site(location.hostname)
              && r.width * r.height > 0.5 * innerWidth * innerHeight;
          } catch (x) { }
          made = !e.isTrusted || over;
        }
      }
    }
  }, true);
  addEventListener('click', (e) => {
    if (!link || e.defaultPrevented) return;
    e.preventDefault();
    ask(made ? 'popup' : 'newtab', link.href);
    link = null;
  });
  let pressed = 0;
  for (const type of ['mousedown', 'keydown', 'pointerdown'])
    addEventListener(type, () => { pressed = Date.now(); }, true);
  window.open = function (url) {
    if (url === undefined || url === null || url === '') return null;
    if (!popups && Date.now() - pressed > 1500) return null;
    let to;
    try { to = new URL(String(url), location.href).href; } catch (e) { return null; }
    if (!/^https?:/i.test(to)) return null;
    if (rn) {
      ask('popup', to);
      return null;
    }
    try { top.location.href = to; } catch (e) { location.href = to; }
    return null;
  };
}
)";


#ifdef SPEED_TEST
#ifdef SPEED_PLAN_FILE  // make SPEED_PLAN=worker_plan.h: another plan for the same code
#include SPEED_PLAN_FILE
#else
#include "speed_test.h"  // SPEED_START_JS (pragma once: the include further down adds nothing)
#endif
#endif

static void
add_script(const char* source, bool at_start, bool main_frame_only) {
  WKStringRef s = string(source);
  WKUserScriptRef script = WKUserScriptCreateWithSource(
      s, at_start ? kWKInjectAtDocumentStart : kWKInjectAtDocumentEnd, main_frame_only);
  WKUserContentControllerAddUserScript(page_content, script);
  WKRelease(script);
  WKRelease(s);
}


// (function)(argument);
static void
add_builtin(const char* function, const char* argument, bool at_start,
            bool main_frame_only) {
  text_t t = {};
  text_printf(&t, "(%s\n)(%s);\n", function, argument);
  if(!t.failed) {
    add_script(t.s, at_start, main_frame_only);
  }
  free(t.s);
}


// After any change: the scripts for the pages to come (one that is open
// keeps those it had until it loads again).
#ifdef ENGINE_PLUS
// ---------------------------------------------------------------------------
// BrowserPS5 RN with the "plus" WebKit (make ENGINE_PLUS=1, 2026-11-11).
//
// Content rules: the ad blocker's hosts (ext/adhosts.txt) as WebKit's own
// content rule list, the kind Safari's content blockers are. WebKit then
// refuses every load from those hosts in the network layer (scripts,
// pictures, frames, requests from workers too), where ext/adblock.js can
// only refuse what a page's own script asks for. A second list comes from
// RULES_FILE when it is there: a content blocker's JSON (e.g. a filter list
// converted for WebKit), the user's to put there. Each is compiled once and
// kept in RULES_DIR under a name with its size in it; they are in the pages'
// content controller while the ad blocker is on.
//
// Dark sites: with the setting on the pages are told the system is dark
// (prefers-color-scheme), so a site with a dark theme of its own shows it;
// ext/dark.js still turns round the ones that stay light.
#define RULES_DIR  DATA_DIR "/content-rules"
#define RULES_FILE DATA_DIR "/content-rules.json"
static WKUserContentExtensionStoreRef rules_store;
// 2026-11-14, BrowserPS5 RN: a third list, the
// browser's own: uBlock Origin Lite's network rules as WebKit content rules (browser-rn/adblock/make-rules.js
// makes the file on the PC, the release installs it; ~59 000 rules, 13 MB). Compiled by WebKit on the
// console the first time it is seen (its name has its size), then only loaded. WebKit takes each list on
// its own, so the sites where the user lets the ads be cannot be an exception inside another list: this one
// comes off the pages while such a site is shown (rules_site_tick()).
// 2026-11-14, the second part: lists 3 and 4, what uBO Lite hides in the pages
// (make-cosmetic.js: ~27 800 rules with the sites as domains, ~1 100 with them as expressions over the page's
// address, which one list cannot mix). They go with the browser's own list: off on a site where the user lets
// the ads be. Not read when the file no-hide-rules is in the data folder.
#ifdef APP_RN
#define RULES_LISTS 5
#else
#define RULES_LISTS 3
#endif
#define RULES_OWN "/user/homebrew/browserps5-rn/adblock/content-rules.json"
#define RULES_HIDE "/user/homebrew/browserps5-rn/adblock/cosmetic-rules.json"
#define RULES_HIDE_ENTITIES "/user/homebrew/browserps5-rn/adblock/cosmetic-entities.json"
static WKUserContentFilterRef rules_filter[RULES_LISTS];  // the hosts', the user's file's, the browser's own
static char* rules_source[RULES_LISTS];                   // their JSON, until compiled
static char rules_name[RULES_LISTS][64];
static double rules_asked_ms[RULES_LISTS];
static bool rules_own_off;  // the site shown is one where the user lets the ads be
static bool rules_reload;  // the page loads again once the hosts' list is made again (ad_allow_changed())

static void
rules_apply(void) {
  WKUserContentControllerRemoveAllUserContentFilters(page_content);
  int n = 0;
  for(int i = 0; settings.adblock && i < RULES_LISTS; i++) {
    if(rules_filter[i] && !(i >= 2 && rules_own_off)) {
      WKUserContentControllerAddUserContentFilter(page_content, rules_filter[i]);
      n++;
    }
  }
  POC_LOG("content rules: %d list(s) in the pages%s", n, rules_own_off && rules_filter[2] ? " (the browser's own is off: ads are let be on this site)" : "");
}

#ifdef APP_RN
// With the bar's updates: the browser's own list off while a site where the user lets the ads be is shown.
static void
rules_site_tick(void) {
  bool any = false;
  for(int i = 2; i < RULES_LISTS; i++) {
    any = any || rules_filter[i];
  }
  if(!any || !settings.adblock || !app.panes[PAGE].view) {
    return;
  }
  const bool off = ad_site_allowed(page(PAGE));
  if(off != rules_own_off) {
    rules_own_off = off;
    rules_apply();
  }
}
#endif

static void rules_compiled(WKUserContentFilterRef filter, WKUserContentExtensionStoreResult result, void* ctx);

// The user's list: compiled in a run before (its name has its size), or now.
static void
rules_looked_up(WKUserContentFilterRef filter, WKUserContentExtensionStoreResult result, void* ctx) {
  const int i = (int)(intptr_t)ctx;
  if(result == kWKUserContentExtensionStoreSuccess && filter) {
    free(rules_source[i]);
    rules_source[i] = 0;
    rules_compiled(filter, result, ctx);
    return;
  }
  WKStringRef name = string(rules_name[i]);
  WKStringRef json = string(rules_source[i] ? rules_source[i] : "[]");
  rules_asked_ms[i] = poc_now_ms();
  POC_LOG("content rules: list %d (%s) is new: WebKit compiles it (%zu bytes)", i, rules_name[i],
          rules_source[i] ? strlen(rules_source[i]) : 0);
  WKUserContentExtensionStoreCompile(rules_store, name, json, ctx, rules_compiled);
  WKRelease(json);
  WKRelease(name);
  free(rules_source[i]);  // WebKit has its own copy
  rules_source[i] = 0;
}

static void
rules_compiled(WKUserContentFilterRef filter, WKUserContentExtensionStoreResult result, void* ctx) {
  const int i = (int)(intptr_t)ctx;
  const bool reload = i == 0 && rules_reload;
  if(reload) {
    rules_reload = false;
  }
  if(result != kWKUserContentExtensionStoreSuccess || !filter) {
    POC_LOG("content rules: list %d (%s) not taken: result %u", i, rules_name[i], (unsigned)result);
  } else {
    if(rules_filter[i]) {  // the hosts' list made again (the exceptions changed)
      WKRelease(rules_filter[i]);
    }
    rules_filter[i] = (WKUserContentFilterRef)WKRetain(filter);
    if(rules_asked_ms[i] > 0) {
      POC_LOG("content rules: list %d (%s) ready, compiled in %.1f s", i, rules_name[i], (poc_now_ms() - rules_asked_ms[i]) / 1e3);
      rules_asked_ms[i] = 0;
    } else {
      POC_LOG("content rules: list %d (%s) ready", i, rules_name[i]);
    }
    rules_apply();
  }
  if(reload) {
    reload_web_page();
  }
}

// A host for a rule: its letters, digits, '-', and the dots as the regular
// expression (dots true) or the JSON (false) wants them.
static void
rules_host(text_t* t, const char* host, size_t length, bool dots) {
  for(size_t k = 0; k < length; k++) {
    const char c = (char)tolower((unsigned char)host[k]);
    char one[8];
    if(c == '.') {
      snprintf(one, sizeof(one), "%s", dots ? "\\\\." : ".");
    } else if(isalnum((unsigned char)c) || c == '-') {
      snprintf(one, sizeof(one), "%c", c);
    } else {
      one[0] = 0;
    }
    text_add(t, one);
  }
}

// The hosts' list, at the start and whenever the exceptions change
// (ad_allow_changed()): each load from one of the hosts, or from a host under
// it, by a page of another site (a page of the host itself still opens); and
// at the end the sites where the user lets the ads be ("Anúncios aqui"),
// for which every rule before is forgotten. A few hundred rules: compiled
// each time, under one name.
static void
rules_hosts_compile(void) {
  if(!rules_store || !ad_count) {
    return;
  }
  text_t t = {};
  text_add(&t, "[");
  for(int i = 0; i < ad_count; i++) {
    text_add(&t, i ? ", " : "");
    text_add(&t, "{\"trigger\": {\"url-filter\": \"^https?://([^/:]*\\\\.)?");
    rules_host(&t, ad_list[i], strlen(ad_list[i]), true);
    text_add(&t, "[/:]\", \"load-type\": [\"third-party\"]}, \"action\": {\"type\": \"block\"}}");
  }
  int allowed = 0;
  for(const char* p = settings.ad_allow; *p;) {
    while(*p == ' ') {
      p++;
    }
    size_t n = strcspn(p, " ");
    if(n) {
      text_add(&t, allowed++ ? ", \"*" : ", {\"trigger\": {\"url-filter\": \".*\", \"if-domain\": [\"*");
      rules_host(&t, p, n, false);
      text_add(&t, "\"");
    }
    p += n;
  }
  if(allowed) {
    text_add(&t, "]}, \"action\": {\"type\": \"ignore-previous-rules\"}}");
  }
  text_add(&t, "]");
  if(!t.failed) {
    snprintf(rules_name[0], sizeof(rules_name[0]), "bps5-hosts");
    WKStringRef name = string(rules_name[0]);
    WKStringRef json = string(t.s);
    WKUserContentExtensionStoreCompile(rules_store, name, json, (void*)(intptr_t)0, rules_compiled);
    WKRelease(json);
    WKRelease(name);
    POC_LOG("content rules: %d hosts, %d site(s) let be, %zu bytes of rules to compile", ad_count, allowed,
            strlen(t.s));
  }
  free(t.s);
}

// At the start, once the ad blocker's hosts are read (ad_hosts_init()).
static void
rules_start(void) {
  mkdir(RULES_DIR, 0755);
  WKStringRef dir = string(RULES_DIR);
  rules_store = WKUserContentExtensionStoreCreate(dir);
  WKRelease(dir);
  rules_hosts_compile();

  // The user's list, when there is one (up to 24 MiB of JSON).
  FILE* f = fopen(RULES_FILE, "rb");
  if(f) {
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if(size > 2 && size < 24 * 1024 * 1024 && (rules_source[1] = (char*)malloc((size_t)size + 1))) {
      size_t got = fread(rules_source[1], 1, (size_t)size, f);
      rules_source[1][got] = 0;
      snprintf(rules_name[1], sizeof(rules_name[1]), "bps5-user-%ld", size);
      WKStringRef name = string(rules_name[1]);
      WKUserContentExtensionStoreLookup(rules_store, name, (void*)(intptr_t)1, rules_looked_up);
      WKRelease(name);
      POC_LOG("content rules: %s, %ld bytes", RULES_FILE, size);
    }
    fclose(f);
  }
#ifdef APP_RN
  // The browser's own list (RULES_OWN), unless the file no-own-rules is in the data folder.
  f = access(DATA_DIR "/no-own-rules", F_OK) ? fopen(RULES_OWN, "rb") : 0;
  if(f) {
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if(size > 2 && size < 64 * 1024 * 1024 && (rules_source[2] = (char*)malloc((size_t)size + 1))) {
      size_t got = fread(rules_source[2], 1, (size_t)size, f);
      rules_source[2][got] = 0;
      snprintf(rules_name[2], sizeof(rules_name[2]), "bps5-own-%ld", size);
      WKStringRef name = string(rules_name[2]);
      WKUserContentExtensionStoreLookup(rules_store, name, (void*)(intptr_t)2, rules_looked_up);
      WKRelease(name);
      POC_LOG("content rules: the browser's own list, %s, %ld bytes", RULES_OWN, size);
    }
    fclose(f);
  }
  // What is hidden in the pages (RULES_HIDE, RULES_HIDE_ENTITIES), unless the file no-hide-rules is there.
  static const struct {
    int i;
    const char* path;
    const char* name;
  } hide[] = {{3, RULES_HIDE, "bps5-hide"}, {4, RULES_HIDE_ENTITIES, "bps5-hide-ent"}};
  for(const auto& h : hide) {
    f = access(DATA_DIR "/no-hide-rules", F_OK) ? fopen(h.path, "rb") : 0;
    if(!f) {
      continue;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if(size > 2 && size < 32 * 1024 * 1024 && (rules_source[h.i] = (char*)malloc((size_t)size + 1))) {
      size_t got = fread(rules_source[h.i], 1, (size_t)size, f);
      rules_source[h.i][got] = 0;
      snprintf(rules_name[h.i], sizeof(rules_name[h.i]), "%s-%ld", h.name, size);
      WKStringRef name = string(rules_name[h.i]);
      WKUserContentExtensionStoreLookup(rules_store, name, (void*)(intptr_t)h.i, rules_looked_up);
      WKRelease(name);
      POC_LOG("content rules: what is hidden in the pages, %s, %ld bytes", h.path, size);
    }
    fclose(f);
  }
#endif
}

#ifdef APP_RN
// 2026-11-14. uBO Lite's
// strict blocking: the hosts whose pages are advertisements or worse (adblock/strict-hosts.txt, ~46 000, one
// per line, sorted as strcmp() sorts; make-cosmetic.js). A film site's page takes the whole tab there at a
// press: decidePageNavigation() does not let a page of ANOTHER site do that, the tab stays where it is and
// the bar says so. An address the user asks for (app_nav_note()), going back, and a site where the user
// lets the ads be are not looked at. Not read when the file no-strict is in the data folder.
#define STRICT_FILE "/user/homebrew/browserps5-rn/adblock/strict-hosts.txt"
static char* strict_text;
static const char** strict_hosts;
static size_t strict_count;
static unsigned strict_blocked;

static int
strict_order(const void* a, const void* b) {
  return strcmp(*(const char* const*)a, *(const char* const*)b);
}

static int
strict_find(const void* key, const void* item) {
  return strcmp((const char*)key, *(const char* const*)item);
}

static void
strict_load(void) {
  FILE* f = access(DATA_DIR "/no-strict", F_OK) ? fopen(STRICT_FILE, "rb") : 0;
  if(!f) {
    return;
  }
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  if(size > 0 && size < 8 * 1024 * 1024 && (strict_text = (char*)malloc((size_t)size + 1))) {
    size_t got = fread(strict_text, 1, (size_t)size, f);
    strict_text[got] = 0;
    size_t lines = 1;
    for(size_t k = 0; k < got; k++) {
      lines += strict_text[k] == '\n';
    }
    strict_hosts = (const char**)malloc(lines * sizeof(char*));
    bool sorted = true;
    for(char* at = strict_text; strict_hosts && *at;) {
      char* end = strchr(at, '\n');
      if(end) {
        *end = 0;
      }
      size_t length = strlen(at);
      if(length && at[length - 1] == '\r') {
        at[--length] = 0;
      }
      if(length) {
        sorted = sorted && (!strict_count || strcmp(strict_hosts[strict_count - 1], at) < 0);
        strict_hosts[strict_count++] = at;
      }
      if(!end) {
        break;
      }
      at = end + 1;
    }
    if(!sorted && strict_count) {
      qsort(strict_hosts, strict_count, sizeof(char*), strict_order);
    }
    POC_LOG("strict blocking: %zu host(s) from %s (%ld bytes%s)", strict_count, STRICT_FILE, size, sorted ? "" : ", sorted here");
  }
  fclose(f);
}

// Is the address's host, or a host it is under, one of them? host: the one found.
static bool
strict_host(const char* url, char* host, size_t size) {
  char name[256];
  if(!strict_count || !url_host(url, name, sizeof(name)) || !name[0]) {
    return false;
  }
  for(char* c = name; *c; c++) {
    *c = (char)tolower((unsigned char)*c);
  }
  for(const char* h = name; h && strchr(h, '.'); h = strchr(h, '.') + 1) {
    if(bsearch(h, strict_hosts, strict_count, sizeof(char*), strict_find)) {
      snprintf(host, size, "%s", h);
      return true;
    }
  }
  return false;
}
#endif

// The sites' own dark theme, for every tab (and each new one: tab_new()).
static void dark_appearance_apply(void);
#endif

static void
extensions_apply(void) {
  WKUserContentControllerRemoveAllUserScripts(page_content);
#ifdef SHOW_YT_BARE  // make SHOW_YT_BARE=1: no script of the browser's in the pages (does one of them stop YouTube's film?)
  POC_LOG("page scripts: none (SHOW_YT_BARE)");
  return;
#endif
#ifdef ENGINE_PLUS
  rules_apply();
  dark_appearance_apply();
#endif
#ifdef APP_RN
  add_builtin(sw_only_js, "[\"whatsapp.com\"]", true, false);  // before any of the site's
#endif
#ifdef MEDIA_PROBE
  // From each page's start, so the errors of its first seconds count too.
  add_script(media_probe_js, true, true);
#endif
#if defined(SHOW_YT_DIAG) || defined(SHOW_SITE_ERRORS)
  // make SHOW_YT_DIAG=1 or SHOW_SITE_ERRORS=1 (a test's): what a page's scripts complain of, kept from its start for the test to read
  // (window.__bps5log: console.error and console.warn, errors and rejected promises nobody caught).
  add_script("(function () { if (window.top !== window || window.__bps5log) return; var L = window.__bps5log = [];"
             " function add(k, a) { try { L.push(k + ': ' + Array.prototype.map.call(a, function (x) {"
             " return x && x.stack ? String(x.message || x) + ' @ ' + String(x.stack).slice(0, 160) : typeof x === 'object' ? JSON.stringify(x).slice(0, 160) : String(x); }).join(' ').slice(0, 320));"
             " if (L.length > 40) L.shift(); } catch (e) {} }"
             " ['error', 'warn'].forEach(function (k) { var o = console[k]; console[k] = function () { add(k, arguments); return o.apply(console, arguments); }; });"
             " window.addEventListener('error', function (e) { add('onerror', [e.message, String(e.filename).slice(-60), e.lineno]); }, true);"
             " window.addEventListener('unhandledrejection', function (e) { add('rejection', [e.reason]); }); })();",
             true, true);
#endif
#ifdef APP_RN
  add_builtin(away_js, "", true, false);  // every frame: the pause reaches the players of other sites
  add_builtin(tabs_js, settings.popups ? "true, true" : "false, true", true, false);
#else
  add_builtin(tabs_js, settings.popups ? "true" : "false", true, false);
#endif
  add_builtin(sdr_js, "", true, false);  // 8-bit SDR video (the PS5's decoder), as the screen
#ifdef APP_RN
  {
    text_t hosts = {};
    text_add(&hosts, "[");
    for(size_t i = 0; i < sizeof(notify_sites) / sizeof(*notify_sites); i++) {
      if(i) {
        text_add(&hosts, ", ");
      }
      text_json(&hosts, notify_sites[i][0]);
    }
    text_add(&hosts, "]");
    if(!hosts.failed) {
      add_builtin(notify_js, hosts.s, true, true);
    }
    free(hosts.s);
  }
  // Cross held on a link or a picture: its menu (ext/linkmenu.js).
  {
    char lang_argument[16];
    snprintf(lang_argument, sizeof(lang_argument), "\"%s\"", lang_code());
    add_builtin(ext_linkmenu_js, lang_argument, true, true);
  }
#endif
#ifdef SPEED_TEST
  if(SPEED_START_JS[0]) {  // 2026-11-10: the test's own script from each page's start
    add_script(SPEED_START_JS, true, true);
  }
#endif
  if(settings.fullscreen) {
    add_builtin(ext_fullscreen_js, "", true, false);
  }
  if(settings.gamepad) {
    add_builtin(ext_gamepad_js, "", true, false);
  }
  if(settings.devtools) {
    add_builtin(ext_devtools_js, "", true, true);
  }
  if(settings.logins) {  // the saved logins (logins.h): main frames only
    add_builtin(ext_logins_js, "", true, true);
#ifdef SHOW_YT
    add_builtin(show_diag_js, "", true, true);
#endif
  }
  // The script sites see a manager (ext/manager.js): their "Install" goes
  // to the .user.js instead of the "get Tampermonkey" help.
  {
    text_t list = {};
    text_add(&list, "[");
    for(int i = 0; i < extensions.count; i++) {
      text_add(&list, i ? ", {name: " : "{name: ");
      text_json(&list, extensions.items[i].meta.name);
      text_add(&list, ", version: ");
      text_json(&list, extensions.items[i].meta.version);
      text_add(&list, "}");
    }
    text_add(&list, "]");
    if(!list.failed) {
      add_builtin(ext_manager_js, list.s, true, true);
    }
    free(list.s);
  }
  if(settings.adblock) {
    text_t options = {};
    text_add(&options, "{hosts: ");
    text_add(&options, ad_json);
    text_add(&options, ", allow: [");
    bool first = true;
    for(const char* p = settings.ad_allow; *p;) {
      while(*p == ' ') {
        p++;
      }
      size_t n = strcspn(p, " ");
      if(n) {
        char site[256];
        snprintf(site, sizeof(site), "%.*s", (int)(n < 255 ? n : 255), p);
        text_add(&options, first ? "" : ", ");
        text_json(&options, site);
        first = false;
      }
      p += n;
    }
    text_add(&options, "]}");
    if(!options.failed) {
      add_builtin(ext_adblock_js, options.s, true, false);
    }
    free(options.s);
  }
  if(settings.dark_sites) {
    add_builtin(ext_dark_js, "", true, true);
  }
  int on = 0;
  for(int i = 0; i < extensions.count; i++) {
    const ext_t* x = &extensions.items[i];
    if(!x->on) {
      continue;
    }
    char* source = ext_source(&extensions, x->id);
    char* wrapped = source ? ext_wrap(x, source, ext_gm_js) : 0;
    if(wrapped) {
      add_script(wrapped, x->meta.run_at_start, x->meta.noframes);
      on++;
    } else {
      POC_LOG("extension %d: its script could not be read", x->id);
    }
    free(wrapped);
    free(source);
  }
  POC_LOG("page scripts: full screen %s, ad blocker %s, dark sites %s; %d of %d "
          "extension(s) on", settings.fullscreen ? "on" : "off",
          settings.adblock ? "on" : "off", settings.dark_sites ? "on" : "off", on,
          extensions.count);
}


// A user script that came as a download, waiting for the settings page's
// "Instalar" (ext?install) or "Cancelar".
static char* pending_source;
static ext_meta_t pending_meta;

// "a\nb" as ["a", "b"].
static void
lines_json(text_t* t, const char* lines) {
  bool first = true;

  text_add(t, "[");
  for(const char* p = lines; *p;) {
    size_t n = strcspn(p, "\n");
    if(n) {
      char one[1024];
      snprintf(one, sizeof(one), "%.*s", (int)n, p);
      if(!first) {
        text_add(t, ", ");
      }
      text_json(t, one);
      first = false;
    }
    p += n;
    if(*p) {
      p++;
    }
  }
  text_add(t, "]");
}


static void
pending_json(text_t* t) {
  if(!pending_source) {
    text_add(t, "null");
    return;
  }
  text_add(t, "{\"name\": ");
  text_json(t, pending_meta.name);
  text_add(t, ", \"version\": ");
  text_json(t, pending_meta.version);
  text_add(t, ", \"description\": ");
  text_json(t, pending_meta.description);
  text_add(t, ", \"matches\": ");
  lines_json(t, pending_meta.matches);
  text_add(t, "}");
}


// ---------------------------------------------------------------------------
// Downloads. WebKit writes each one into PARTIAL_DIR; it goes to its folder
// when it is all in and the folder is known (the "Guardar como" dialog, or
// the setting for its kind of file: the packages, the videos and music, the
// rest), renamed or, from another drive, copied by a thread of its own.

enum { DL_RUNNING, DL_WAITING, DL_MOVING, DL_DONE, DL_FAILED, DL_CANCELLED };
static const char* const download_states[] = {
  "running", "waiting", "running", "done", "failed", "cancelled",
};
#define DOWNLOADS_MAX 16

typedef struct download {
  int id;               // 0: a free slot
  WKDownloadRef ref;    // kept until the slot is used again
  int state;
  bool chosen;          // where it goes is known (dir, name)
  bool written;         // all of it is in partial
  bool cancelling;
  double cancel_ms;     // RN: when its cancel was asked of the engine (not answered in ten seconds: multi_tick())
  bool extension;       // a user script, offered as an extension
  bool torrent;         // a .torrent, handed to the torrents (torrent.h)
  bool view;            // RN: a PDF a page went to: shown in the reader once it is in (pdf_open())
  bool cheat;           // RN: a cheat file of etaHEN's, asked for from the start page (cheat_ask())
  char kind[8];         // pkg, media, ext, torrent, file
  char name[256];
  char host[256];
  char dir[512];
  char partial[600];
  char path[1024];      // where it ended up
  long long received, total;
  dl_rate_t rate;       // its speed and time left (dl_rate.h)
  long long speed_told; // the speed the pages were last told
  bool can_retry;       // a GET of a web address: it can be asked again
  char url[4096];       // that address, for "Voltar a tentar": in memory
                        // only, never in the log nor in a page
  int unzip;            // UZ_*: "Extrair" for a .zip (unzip.h)
  char unzip_dir[1024]; // the folder it went into
  long long own;        // its bytes counted here, for an engine that tells no totals (download_progress())
  int status;           // RN: its response's HTTP status
  bool multi_tried;     // RN: whether it goes in several parts was decided (multi_begin())
  char final_url[4096]; // RN: the address its response came from (after the redirects): the other parts ask it
  bool kept;            // RN: left unfinished by a session before, with its file and its map: "Continuar" (multi_resume())
} download_t;

enum { UZ_NONE, UZ_RUNNING, UZ_DONE, UZ_FAILED };
static const char* const unzip_states[] = {"none", "running", "done", "failed"};
static volatile int unzip_percent = -1;  // the running extraction's, -1: none

static download_t downloads[DOWNLOADS_MAX];
static int download_ids;
static bool downloads_changed;  // the bar and the settings page are told (tick)

static download_t*
download_by_id(int id) {
  for(download_t& d : downloads) {
    if(id && d.id == id) {
      return &d;
    }
  }
  return 0;
}


// The same address, but for its query (WebKit may write it otherwise).
static bool
same_address(const char* a, const char* b) {
  const size_t n = strcspn(a, "?#");
  return n == strcspn(b, "?#") && !strncmp(a, b, n);
}

#ifdef APP_RN
// A download was made from this address (the one asked for, or the one its answer came from): bar_update(). The
// last two are kept apart from the list, which the user may clear while the page still has the address.
static char download_last_url[2][1024];

static bool
download_of_address(const char* url) {
  for(const char* last : download_last_url) {
    if(last[0] && same_address(last, url)) {
      return true;
    }
  }
  for(const download_t& d : downloads) {
    if(d.id && ((d.url[0] && same_address(d.url, url)) || (d.final_url[0] && same_address(d.final_url, url)))) {
      return true;
    }
  }
  return false;
}
#endif


#ifdef APP_RN
// 2026-11-14/15. etaHEN, the
// console's own homebrew enabler, has a cheats menu that reads cheat files by the game's id and version; the
// scene keeps them in one public repository (github.com/etaHEN/PS5_Cheats, GPL-3.0: json/, shn/, mc4/ and an
// index of each). The start page's game row lists the ones of the user's game (start-rn.js reads the indexes
// itself) and "Save" asks for one here (browserps5:cheat?<file name>): the browser downloads THAT file from THAT
// repository and puts it where etaHEN looks (CHEATS_DIR/<kind>/). Nothing else comes this way: the name must
// be a cheat file's (cheat_name_ok()), the asking page must be the browser's own start page, and the address is
// made here, never taken from the page. Without etaHEN's folder on the console the file goes to the downloads.
#define CHEATS_REPO "https://raw.githubusercontent.com/etaHEN/PS5_Cheats/main/"
#define CHEATS_HOME "/data/etaHEN"
#define CHEATS_DIR  CHEATS_HOME "/cheats"
#define CHEAT_MOST  (8 << 20)  // bytes; they are a few KiB
static struct {
  char url[200];
  char name[72];
  char dir[600];
  double ms;
} cheat_want;
static char cheat_last_path[700];  // where the last one was put (the probe looks)
static int cheat_last_id;
static unsigned cheats_saved;

// "<4 capitals><5 digits>_<version>.<json|shn|mc4>", and its kind into ext.
static bool
cheat_name_ok(const char* name, char ext[8]) {
  size_t n = strlen(name);
  if(n < 16 || n > 60) {
    return false;
  }
  for(int i = 0; i < 9; i++) {
    if(i < 4 ? !(name[i] >= 'A' && name[i] <= 'Z') : !isdigit((unsigned char)name[i])) {
      return false;
    }
  }
  const char* dot = strrchr(name, '.');
  if(name[9] != '_' || !dot || dot - name < 11 || (strcmp(dot, ".json") && strcmp(dot, ".shn") && strcmp(dot, ".mc4"))) {
    return false;
  }
  for(const char* c = name + 10; c < dot; c++) {
    if(!isalnum((unsigned char)*c) && *c != '.') {
      return false;
    }
  }
  if(strstr(name, "..")) {
    return false;
  }
  snprintf(ext, 8, "%s", dot + 1);
  return true;
}

// 2: etaHEN's cheats folder is there; 1: etaHEN's folder is, without it; 0: no etaHEN folder on this console.
static int
cheats_state(void) {
  struct stat st;
  if(!stat(CHEATS_DIR, &st) && S_ISDIR(st.st_mode)) {
    return 2;
  }
  return !stat(CHEATS_HOME, &st) && S_ISDIR(st.st_mode) ? 1 : 0;
}

static void load_url(const char* url);
static bool own_page_shown(const char* page_url);

// browserps5:cheat?<file name>, from the start page's game row.
static void
cheat_ask(const char* arg) {
  char ext[8];

  if(!own_page_shown(START_URL)) {
    POC_LOG("cheats: asked for by another page than the browser's start page: not taken");
    return;
  }
  if(!cheat_name_ok(arg, ext)) {
    POC_LOG("cheats: what was asked for is not a cheat file's name: not taken");
    return;
  }
  const int state = cheats_state();
  snprintf(cheat_want.name, sizeof(cheat_want.name), "%s", arg);
  snprintf(cheat_want.url, sizeof(cheat_want.url), CHEATS_REPO "%s/%s", ext, arg);
  if(state) {
    snprintf(cheat_want.dir, sizeof(cheat_want.dir), CHEATS_DIR "/%s", ext);
  } else {
    snprintf(cheat_want.dir, sizeof(cheat_want.dir), "%s", settings.downloads_dir);
  }
  cheat_want.ms = poc_now_ms();
  POC_LOG("cheats: %s asked for from the start page; it goes to %s (etaHEN's folder: %s)", arg, cheat_want.dir,
          state == 2 ? "with its cheats folder" : state == 1 ? "there, the cheats folder is made" : "NOT on this console");
  load_url(cheat_want.url);
}

static bool
cheat_wanted(const char* url) {
  return cheat_want.url[0] && poc_now_ms() - cheat_want.ms < 30000 && same_address(url, cheat_want.url);
}

// 2026-11-15 (docs/32, "60 fps patches beside the cheats"). etaHEN's game patches plugin
// (github.com/etaHEN/etaHEN-Plugins, Plugin_samples/itemzflow_xml_patches) reads, when a game starts,
// PATCHES_DIR/xml/<id>.xml for a PS4 game and PATCHES_DIR/xml_prospero/<id>.xml for a PS5 one, and applies the
// patches of that file whose switch is on: PATCHES_SETTINGS/0x<16 hex>.txt, "1" or "0", the number being its own
// sum of the patch's title, name and version, the file's path and the game's executable (start-rn.js
// patchHash() is that sum; patch_sum() here is the plugin's, for the probe to hold the page's against). The
// patch files are the scene's (github.com/illusionyy/PS-Game-Patch, patches/xml and xml_prospero: a file by
// game). The start page lists the patches of the user's game with their switches (patchesPanel()); here the
// file is fetched from THAT repository under the name the plugin reads (browserps5:patch?<id>=<kind>/<file>), a
// switch is written (browserps5:patch-set?<id>=<kind>=<number>=<0|1>) and the page is told what the console has
// (browserps5:patches?<id>=<kind>=<number>,<number>...). Only for the browser's own start page, every name
// checked, the address made here; nothing else is written, and no game is touched by the browser.
#define PATCHES_REPO     "https://raw.githubusercontent.com/illusionyy/PS-Game-Patch/main/patches/"
#define PATCHES_DIR      CHEATS_HOME "/patches"
#define PATCHES_SETTINGS PATCHES_DIR "/settings"
static unsigned patches_set_count;

static __attribute__((unused)) uint64_t
patch_sum(const char* s) {
  uint64_t hash = 5381;
  uint32_t c = 0;
  while((c = (uint32_t)(int32_t)(signed char)*s++)) {
    hash = hash * 33 ^ c;
  }
  return hash;
}

// "<4 capitals><5 digits>" in the n characters at s.
static bool
patch_id_ok(const char* s, size_t n) {
  if(n != 9) {
    return false;
  }
  for(int i = 0; i < 9; i++) {
    if(i < 4 ? !(s[i] >= 'A' && s[i] <= 'Z') : !isdigit((unsigned char)s[i])) {
      return false;
    }
  }
  return true;
}

static bool
patch_kind_ok(const char* s, size_t n) {
  return (n == 3 && !strncmp(s, "xml", 3)) || (n == 12 && !strncmp(s, "xml_prospero", 12));
}

// 16 digits of base 16, the letters small, as the plugin writes them.
static bool
patch_number_ok(const char* s, size_t n) {
  if(n != 16) {
    return false;
  }
  for(int i = 0; i < 16; i++) {
    if(!isdigit((unsigned char)s[i]) && !(s[i] >= 'a' && s[i] <= 'f')) {
      return false;
    }
  }
  return true;
}

// browserps5:patch?<id>=<kind>/<file of the repository>: the game's patch file, for the plugin.
static void
patch_ask(const char* arg) {
  const char* eq = strchr(arg, '=');
  const char* slash = eq ? strchr(eq + 1, '/') : 0;

  if(!own_page_shown(START_URL)) {
    POC_LOG("patches: a file asked for by another page than the browser's start page: not taken");
    return;
  }
  bool ok = eq && slash && patch_id_ok(arg, (size_t)(eq - arg)) && patch_kind_ok(eq + 1, (size_t)(slash - eq - 1));
  const char* file = ok ? slash + 1 : "";
  const size_t n = strlen(file);
  ok = ok && n > 4 && n <= 100 && !strcmp(file + n - 4, ".xml") && file[0] != '.' && !strstr(file, "..");
  for(const char* c = file; ok && *c; c++) {
    ok = isalnum((unsigned char)*c) || *c == '_' || *c == '-' || *c == '.';
  }
  if(!ok) {
    POC_LOG("patches: what was asked for is not a game's id and a patch file's name: not taken");
    return;
  }
  if(!cheats_state()) {
    POC_LOG("patches: no etaHEN folder on this console: nothing is fetched");
    return;
  }
  snprintf(cheat_want.name, sizeof(cheat_want.name), "%.9s.xml", arg);
  snprintf(cheat_want.url, sizeof(cheat_want.url), PATCHES_REPO "%.*s/%s", (int)(slash - eq - 1), eq + 1, file);
  snprintf(cheat_want.dir, sizeof(cheat_want.dir), PATCHES_DIR "/%.*s", (int)(slash - eq - 1), eq + 1);
  cheat_want.ms = poc_now_ms();
  POC_LOG("patches: %s asked for from the start page; it goes to %s/%s", file, cheat_want.dir, cheat_want.name);
  load_url(cheat_want.url);
}

// The parts of "<id>=<kind>=<rest>": false when the first two are not a game's id and a kind.
static bool
patch_parts(const char* arg, char id[12], char kind[16], const char** rest) {
  const char* a = strchr(arg, '=');
  const char* b = a ? strchr(a + 1, '=') : 0;
  if(!b || !patch_id_ok(arg, (size_t)(a - arg)) || !patch_kind_ok(a + 1, (size_t)(b - a - 1))) {
    return false;
  }
  snprintf(id, 12, "%.9s", arg);
  snprintf(kind, 16, "%.*s", (int)(b - a - 1), a + 1);
  *rest = b + 1;
  return true;
}

static bool
patch_file_there(const char* kind, const char* id) {
  char path[160];
  struct stat st;
  snprintf(path, sizeof(path), PATCHES_DIR "/%s/%s.xml", kind, id);
  return !stat(path, &st) && S_ISREG(st.st_mode);
}

// browserps5:patch-set?<id>=<kind>=<number>=<0|1>: one patch's switch, as the plugin keeps it.
static void
patch_set(const char* arg) {
  char id[12], kind[16], path[160];
  const char* rest = 0;

  if(!own_page_shown(START_URL) || !patch_parts(arg, id, kind, &rest) || !patch_number_ok(rest, strcspn(rest, "=")) ||
     (strcmp(rest + 16, "=0") && strcmp(rest + 16, "=1"))) {
    POC_LOG("patches: a switch asked for by another page, or not a switch's words: not taken");
    return;
  }
  const bool on = rest[17] == '1';
  bool written = false;
  if(cheats_state()) {
    if(!mkdir(PATCHES_DIR, 0777)) {  // (made here: open to etaHEN and its plugin, as etaHEN makes its own)
      chmod(PATCHES_DIR, 0777);
    }
    if(!mkdir(PATCHES_SETTINGS, 0777)) {
      chmod(PATCHES_SETTINGS, 0777);
    }
    snprintf(path, sizeof(path), PATCHES_SETTINGS "/0x%.16s.txt", rest);
    FILE* f = fopen(path, "wb");
    if(f) {
      written = fputs(on ? "1\n" : "0\n", f) >= 0;
      written = !fclose(f) && written;
      chmod(path, 0666);
    }
  }
  patches_set_count += written ? 1 : 0;
  POC_LOG("patches: %s: the switch 0x%.16s is %s", id, rest, !written ? "NOT written" : on ? "on" : "off");
  text_t t = {};
  text_add(&t, "window.bps5PatchSet && bps5PatchSet(");
  text_json(&t, id);
  text_printf(&t, ", \"%.16s\", %s, %s)", rest, written && on ? "true" : "false", patch_file_there(kind, id) ? "true" : "false");
  text_run(PAGE, &t);
}

// 2026-11-16: without the plugin
// the switches do nothing, and this console never had its folders. etaHEN loads plugins from /data/etaHEN/plugins
// and from a USB drive's etaHEN/plugins (first); the plugin is itemzflow_xml_patches, title id XMLS00001
// (etaHEN-Plugins, CMakeLists.txt). Found: the list it writes when it runs (/data/game_patch_fliprate_list.xml), a
// file there named after it, or one with its title id inside (the first 16 MiB). Only read; the panel says when it
// is not found (bps5Patches(): plugin).
#define PATCHES_PLUGIN_ID "XMLS00001"
#define PATCHES_PLUGIN_RAN "/data/game_patch_fliprate_list.xml"

static bool
patches_plugin_found(void) {
  struct stat st;
  if(!stat(PATCHES_PLUGIN_RAN, &st)) {
    return true;
  }
  char dirs[9][48];
  int n = 0;
  for(int i = 0; i < 8; i++) {
    snprintf(dirs[n++], sizeof(dirs[0]), "/mnt/usb%d/etaHEN/plugins", i);
  }
  snprintf(dirs[n++], sizeof(dirs[0]), "%s", CHEATS_HOME "/plugins");
  const size_t chunk = 64 * 1024, keep = sizeof(PATCHES_PLUGIN_ID) - 2;  // 8 bytes carried over between reads
  char* head = (char*)malloc(chunk + keep);
  ssize_t budget = 32 * 1024 * 1024;
  bool found = false;
  for(int d = 0; d < n && !found && head; d++) {
    DIR* dir = opendir(dirs[d]);
    int files = 0;
    for(struct dirent* e; dir && !found && files < 64 && (e = readdir(dir));) {
      if(e->d_name[0] == '.') {
        continue;
      }
      files++;
      char low[256];
      size_t k = 0;
      for(; e->d_name[k] && k < sizeof(low) - 1; k++) {
        low[k] = (char)tolower((unsigned char)e->d_name[k]);
      }
      low[k] = 0;
      if(strstr(low, "xml_patch") || strstr(low, "itemzflow_xml")) {
        found = true;
        break;
      }
      char path[320];
      snprintf(path, sizeof(path), "%s/%s", dirs[d], e->d_name);
      const int fd = budget > 0 ? open(path, O_RDONLY | O_NONBLOCK) : -1;  // runs on the main thread: 32 MiB in all
      if(fd < 0) {
        continue;
      }
      size_t have = 0, total = 0;
      for(ssize_t got; !found && total < 16 * 1024 * 1024 && budget > 0 && (got = read(fd, head + have, chunk)) > 0;) {
        total += (size_t)got;
        budget -= got;
        const size_t len = have + (size_t)got;
        for(size_t i = 0; i + keep + 1 <= len && !found; i++) {
          found = head[i] == 'X' && !memcmp(head + i, PATCHES_PLUGIN_ID, keep + 1);
        }
        have = len < keep ? len : keep;
        memmove(head, head + len - have, have);
      }
      close(fd);
    }
    if(dir) {
      closedir(dir);
    }
  }
  free(head);
  return found;
}

// browserps5:patches?<id>=<kind>=<number>,<number>...: what the console has for the game (its file, each switch).
static void
patches_tell(const char* arg) {
  char id[12], kind[16], path[160];
  const char* rest = 0;
  int count = 0, on = 0;

  if(!own_page_shown(START_URL) || !patch_parts(arg, id, kind, &rest)) {
    return;
  }
  text_t t = {};
  text_add(&t, "window.bps5Patches && bps5Patches(");
  text_json(&t, id);
  const bool plugin = patches_plugin_found();
  text_printf(&t, ", {saved: %s, etahen: %d, plugin: %d, on: \"", patch_file_there(kind, id) ? "true" : "false", cheats_state(), plugin ? 1 : 0);
  for(const char* c = rest; count < 400 && patch_number_ok(c, strcspn(c, ",")); c += c[16] ? 17 : 16) {
    char first = 0;
    snprintf(path, sizeof(path), PATCHES_SETTINGS "/0x%.16s.txt", c);
    FILE* f = fopen(path, "rb");
    if(f) {
      if(fread(&first, 1, 1, f) != 1) {
        first = 0;
      }
      fclose(f);
    }
    text_add(&t, first == '1' ? "1" : "0");
    on += first == '1';
    count++;
  }
  text_add(&t, "\"})");
  POC_LOG("patches: %s (%s): its file is %s the console, %d of %d switch(es) on; etaHEN's patches plugin %s", id, kind,
          patch_file_there(kind, id) ? "on" : "NOT on", on, count, plugin ? "found" : "NOT found");
  text_run(PAGE, &t);
}

static void games_stale(void);
#endif

static bool
name_ends_with(const char* name, const char* end) {
  size_t a = strlen(name), b = strlen(end);
  return a >= b && !strcasecmp(name + a - b, end);
}


static const char*
download_kind(const char* name, const char* mime) {
  static const char* const media[] = {
    ".mp4", ".m4v", ".mkv", ".webm", ".mov", ".avi", ".ts", ".mp3", ".m4a", ".aac",
    ".flac", ".wav", ".ogg", ".opus", ".jpg", ".jpeg", ".png", ".gif", ".webp",
  };
  if(name_ends_with(name, ".pkg")) {
    return "pkg";
  }
  if(name_ends_with(name, ".user.js")) {
    return "ext";
  }
  if(name_ends_with(name, ".torrent") || !strcmp(mime, "application/x-bittorrent")) {
    return "torrent";
  }
  for(const char* m : media) {
    if(name_ends_with(name, m)) {
      return "media";
    }
  }
  if(!strncmp(mime, "video/", 6) || !strncmp(mime, "audio/", 6) || !strncmp(mime, "image/", 6)) {
    return "media";
  }
  return "file";
}


static const char*
download_dir(const char* kind) {
  return !strcmp(kind, "pkg") ? settings.pkg_dir
       : !strcmp(kind, "media") ? settings.media_dir : settings.downloads_dir;
}


// A file name that is only a name: its last part, with nothing a folder or
// the system would take otherwise, not starting with a dot, UTF-8 cut whole.
static void
clean_name(const char* in, char* out, size_t size) {
  const char* slash = strrchr(in, '/');
  const char* back = strrchr(slash ? slash : in, '\\');
  const unsigned char* p = (const unsigned char*)(back ? back + 1 : slash ? slash + 1 : in);
  size_t n = 0;

  while(*p == '.' || *p == ' ') {
    p++;
  }
#ifdef APP_RN
  // 2026-11-14 (found on the PC, tests-host/make-names-test.py): a name that ended in a letter of more than one
  // byte lost it ("resumé" was saved as "resum", a name in Japanese with no ".ext" lost its last letter): the
  // cut "of a character in two" was made of every name, cut or not. And a name too long lost its end, ".mp4"
  // with it: the file was no longer a video for the list, nor for whoever opened it. Now: all of the name is
  // cleaned first; one too long keeps its end (".mp4"; of ".tar.gz" the ".gz") and loses the letters before it,
  // whole ones.
  char all[1024];
  const size_t most = size > 6 ? size - 6 : 0;
  for(; *p && n + 1 < sizeof(all); p++) {
    all[n++] = *p < 0x20 || *p == 0x7f || strchr("/\\:*?\"<>|", *p) ? '_' : (char)*p;
  }
  while(n && (all[n - 1] == ' ' || all[n - 1] == '.')) {
    n--;
  }
  all[n] = 0;
  if(n > most) {
    const char* dot = strrchr(all, '.');
    size_t end = dot && dot != all && n - (size_t)(dot - all) <= 16 ? n - (size_t)(dot - all) : 0;
    if(end >= most) {
      end = 0;
    }
    size_t stem = most - end;
    while(stem && ((unsigned char)all[stem] & 0xc0) == 0x80) {  // (the first byte left out is inside a letter: all of it out)
      stem--;
    }
    while(stem && (all[stem - 1] == ' ' || all[stem - 1] == '.')) {
      stem--;
    }
    if(!stem) {
      end = 0;
    }
    memmove(all + stem, all + n - end, end);
    n = stem + end;
    while(n && (all[n - 1] == ' ' || all[n - 1] == '.')) {
      n--;
    }
  }
  if(size) {
    memcpy(out, all, n < size ? n : size - 1);
    out[n < size ? n : size - 1] = 0;
  }
  return;
#endif
  for(; *p && n + 5 < size; p++) {
    if(*p < 0x20 || *p == 0x7f || strchr("/\\:*?\"<>|", *p)) {
      out[n++] = '_';
    } else {
      out[n++] = (char)*p;
    }
  }
  while(n && (out[n - 1] & 0xc0) == 0x80) {  // a character cut in two
    n--;
  }
  if(n && (unsigned char)out[n - 1] >= 0xc0) {
    n--;
  }
  while(n && (out[n - 1] == ' ' || out[n - 1] == '.')) {
    n--;
  }
  out[n] = 0;
}


static int
mkdirs(const char* dir) {
  char path[600];

  snprintf(path, sizeof(path), "%s", dir);
  for(char* p = path + 1; *p; p++) {
    if(*p == '/') {
      *p = 0;
      mkdir(path, 0777);
      *p = '/';
    }
  }
  return mkdir(path, 0777) && errno != EEXIST ? -1 : 0;
}


// dir/name, or dir/name (2).ext and on if that is taken.
static void
unique_path(const char* dir, const char* name, char* out, size_t size) {
  struct stat st;

  snprintf(out, size, "%s/%s", dir, name);
  if(stat(out, &st)) {
    return;
  }
  const char* dot = name_ends_with(name, ".user.js") ? name + strlen(name) - 8
                                                     : strrchr(name, '.');
  if(!dot || dot == name) {
    dot = name + strlen(name);
  }
  for(int i = 2; i < 1000; i++) {
    snprintf(out, size, "%s/%.*s (%d)%s", dir, (int)(dot - name), name, i, dot);
    if(stat(out, &st)) {
      return;
    }
  }
}


typedef struct move_job {
  int id;
  char from[600];
  char to[1024];
  bool ok;
} move_job_t;


static void
move_done(void* arg) {
  move_job_t* job = (move_job_t*)arg;
  download_t* d = download_by_id(job->id);

  if(d) {
    d->state = job->ok ? DL_DONE : DL_FAILED;
    if(!job->ok) {
      d->path[0] = 0;
    }
    downloads_changed = true;
  }
  POC_LOG("download %d: %s", job->id, job->ok ? "copied into its folder"
                                               : "could NOT be copied into its folder");
  free(job);
}


// From another drive (a USB one): a copy a MiB at a time, with a little
// sleep after each, below the other threads: what is read may come from the
// cache at once, and a copy that never waited took a core from the
// WebProcess (memory: no spinning threads).
static void*
move_thread(void* arg) {
  move_job_t* job = (move_job_t*)arg;
  struct sched_param param;
  int policy;

  if(!pthread_getschedparam(pthread_self(), &policy, &param)) {
    param.sched_priority = 760;  // FIFO: a bigger number is a lower priority
    pthread_setschedparam(pthread_self(), policy, &param);
  }
  const size_t chunk = 1 << 20;
  char* buffer = (char*)malloc(chunk);
  int in = open(job->from, O_RDONLY);
  int out = open(job->to, O_WRONLY | O_CREAT | O_EXCL, 0666);
  bool ok = buffer && in >= 0 && out >= 0;
  while(ok) {
    ssize_t n = read(in, buffer, chunk);
    if(n <= 0) {
      ok = n == 0;
      break;
    }
    for(ssize_t done = 0; ok && done < n;) {
      ssize_t w = write(out, buffer + done, (size_t)(n - done));
      ok = w > 0;
      done += w > 0 ? w : 0;
    }
    usleep(2000);
  }
  if(in >= 0) {
    close(in);
  }
  if(out >= 0) {
    ok = !close(out) && ok;
  }
  if(ok) {
    unlink(job->from);
  } else if(out >= 0) {
    unlink(job->to);
  }
  free(buffer);
  job->ok = ok;
  WKRunLoopCallOnMainThread(move_done, job);
  return 0;
}


// It is all in and its folder known: there it goes.
#ifdef APP_RN
static void download_view(download_t* d);  // pdf_open(), when the reader is there (below)
// The address of the PDF a page last went to, and when: its download is the one to show.
static char pdf_wanted[4096];
static double pdf_wanted_ms;
#endif

static void
download_move(download_t* d) {
  downloads_changed = true;
  if(!path_allowed(d->dir) || mkdirs(d->dir)) {
    POC_LOG("download %d: its folder %s could not be made", d->id, d->dir);
    d->state = DL_FAILED;
    unlink(d->partial);
    return;
  }
#ifdef APP_RN
  if(d->cheat) {  // a cheat file takes the place of the one of its name (etaHEN looks for that very name)
    char old[sizeof(d->dir) + sizeof(d->name) + 2];
    snprintf(old, sizeof(old), "%s/%s", d->dir, d->name);
    unlink(old);
    if(!strncmp(d->dir, CHEATS_DIR "/", sizeof(CHEATS_DIR))) {
      // etaHEN makes these folders itself with mkdir(..., 0777) (its Source Code/util/source/CheatManager.cpp:
      // /data/etaHEN/cheats and json, shn, mc4 in it, which it reads at its start): the same here.
      chmod(CHEATS_DIR, 0777);
      chmod(d->dir, 0777);
    }
    if(!strncmp(d->dir, PATCHES_DIR "/", sizeof(PATCHES_DIR))) {  // a game's patch file (patch_ask()): the same
      chmod(PATCHES_DIR, 0777);
      chmod(d->dir, 0777);
    }
  }
#endif
  unique_path(d->dir, d->name, d->path, sizeof(d->path));
  if(!rename(d->partial, d->path)) {
    d->state = DL_DONE;
#ifdef APP_RN
    if(d->cheat) {
      cheats_saved++;
      cheat_last_id = d->id;
      snprintf(cheat_last_path, sizeof(cheat_last_path), "%s", d->path);
      games_stale();  // the start page's row says it is on the console
      if(!strncmp(d->dir, PATCHES_DIR "/", sizeof(PATCHES_DIR))) {
        bar_notify(lang_en() ? LM("Patch file saved for etaHEN") : "Ficheiro de patches guardado para o etaHEN", "games");
      } else {
        bar_notify(lang_en() ? LM("Cheat file saved for etaHEN") : "Ficheiro de cheats guardado para o etaHEN", "games");
      }
    }
#endif
#ifdef APP_RN
    {  // its hidden folder goes when this was the last file in it (download_destination())
      char near[600];
      snprintf(near, sizeof(near), "%s", d->partial);
      char* slash = strrchr(near, '/');
      if(slash && slash - near >= (long)sizeof(PARTIAL_NEAR) - 1
         && !strncmp(slash - (sizeof(PARTIAL_NEAR) - 1), PARTIAL_NEAR, sizeof(PARTIAL_NEAR) - 1)) {
        *slash = 0;
        rmdir(near);
      }
    }
#endif
    POC_LOG("download %d: %lld bytes in %s", d->id, d->received, d->dir);
#ifdef APP_RN
    if(d->view) {
      d->view = false;
      download_view(d);
    }
#endif
    return;
  }
  if(errno != EXDEV) {
    POC_LOG("download %d: not moved into %s (%s)", d->id, d->dir, strerror(errno));
    d->state = DL_FAILED;
    d->path[0] = 0;
    unlink(d->partial);
    return;
  }
  move_job_t* job = (move_job_t*)calloc(1, sizeof(*job));
  pthread_t thread;
  if(!job) {
    d->state = DL_FAILED;
    return;
  }
  job->id = d->id;
  snprintf(job->from, sizeof(job->from), "%s", d->partial);
  snprintf(job->to, sizeof(job->to), "%s", d->path);
  d->state = DL_MOVING;
  if(pthread_create(&thread, 0, move_thread, job)) {
    free(job);
    d->state = DL_FAILED;
    return;
  }
  pthread_detach(thread);
  POC_LOG("download %d: copying it to another drive", d->id);
}


static void settings_open(const char* section);

#ifdef REMOTE_UI
// The torrents (torrent.h, docs/11-torrents.md): magnet links and .torrent
// files to Transmission's daemon, started when it does not answer; what
// came of it in the bar. Settings, Transferencias, "Torrents" opens its page
// in a tab.
typedef struct torrent_note {
  int result;
  bool open_page;
  char name[512];
} torrent_note_t;

static void
torrent_main_note(void* arg) {
  torrent_note_t* n = (torrent_note_t*)arg;
  char text[640];
  switch(n->result) {
  case TORRENT_ADDED:
    snprintf(text, sizeof(text), "Torrent a transferir: %s", n->name);
    bar_notify(text, "arrow-download");
    break;
  case TORRENT_DUPLICATE:
    snprintf(text, sizeof(text), "Este torrent já está na lista: %s", n->name);
    bar_notify(text, "arrow-download");
    break;
  case TORRENT_RUNNING:
    if(n->open_page) {
      tab_new(TORRENT_WEB_URL);
    }
    break;
  default:
    bar_notify(n->name, "dismiss");
  }
  free(n);
}

// On the request's thread: to the main thread.
static void
torrent_done(int result, const char* name, void* user) {
  torrent_note_t* n = (torrent_note_t*)calloc(1, sizeof(torrent_note_t));
  if(!n) {
    return;
  }
  n->result = result;
  n->open_page = user != 0;
  snprintf(n->name, sizeof(n->name), "%s", name);
  WKRunLoopCallOnMainThread(torrent_main_note, n);
}

static void
torrent_open_magnet(const char* link) {
  // The log keeps no more of it than the start (a magnet's name and
  // trackers are what was asked).
  POC_LOG("torrents: a magnet link (%.24s...)", link);
  if(torrent_magnet(link, torrent_done, 0)) {
    bar_notify("Não deu para pedir o torrent", "dismiss");
  } else {
    bar_notify("A entregar o torrent ao Transmission...", "arrow-download");
  }
}

static void
torrent_page(void) {
  bar_notify("A abrir os torrents...", "arrow-download");
  if(torrent_start(torrent_done, (void*)1)) {
    bar_notify("Não deu para abrir os torrents", "dismiss");
  }
}

static void torbox_done(torbox_result_t* r);

// A .torrent downloaded (in partial): to the daemon, which keeps its copy,
// or to TorBox (settings "TorBox": torbox_magnets).
static void
torrent_offer(download_t* d) {
  d->state = DL_DONE;
  if(settings.torbox_magnets) {
    snprintf(d->path, sizeof(d->path), "TorBox");
    if(torbox_add_file(d->partial, torbox_done, 0)) {
      unlink(d->partial);
      d->state = DL_FAILED;
      bar_notify("Não deu para pedir ao TorBox", "dismiss");
      return;
    }
    POC_LOG("download %d: a .torrent, handed to TorBox", d->id);
    return;
  }
  snprintf(d->path, sizeof(d->path), "Torrents");
  if(torrent_file(d->partial, torrent_done, 0)) {
    unlink(d->partial);
    d->state = DL_FAILED;
    bar_notify("Não deu para pedir o torrent", "dismiss");
    return;
  }
  POC_LOG("download %d: a .torrent, handed to the torrents", d->id);
}


// TorBox (torbox.h, docs/11-torrents.md, settings "TorBox"): the user's
// account. Its answers come back here, on the main thread: the bar, the
// settings page if it is open, and a ready file's address as a download
// (decidePageNavigation: the next navigation to it is one).
static char torbox_download_url[4096];

static void torbox_list_to_page(void);

static void
torbox_main(void* arg) {
  torbox_result_t* r = (torbox_result_t*)arg;
  const bool shown = own_page_shown(SETTINGS_URL);

  if(r->kind == TORBOX_ADD) {
    bar_notify(r->message, r->ok ? "arrow-download" : "dismiss");
    if(r->ok && shown) {
      torbox_list_to_page();
    }
  } else if(r->kind == TORBOX_LIST) {
    if(shown) {
      text_t t = {};
      text_add(&t, "window.setTorbox && setTorbox({ok: ");
      text_add(&t, r->ok ? "true" : "false");
      text_add(&t, ", message: ");
      text_json(&t, r->message);
      text_add(&t, ", torrents: ");
      text_json(&t, r->torrents ? r->torrents : "");
      text_add(&t, ", web: ");
      text_json(&t, r->web ? r->web : "");
      text_add(&t, "})");
      text_run(PAGE, &t);
    }
#ifdef APP_RN
  } else if(r->ok && url_this_console(r->url)) {  // (an answer from the Internet does not send the app to this console)
    POC_LOG("torbox: a link to this console's own services: not opened");
#endif
  } else if(r->ok) {  // TORBOX_LINK: the file, by the downloads
    snprintf(torbox_download_url, sizeof(torbox_download_url), "%s", r->url);
    last_gesture_ms = poc_now_ms();
    bar_notify(r->message, "arrow-download");
    POC_LOG("torbox: a ready file, as a download");
    load_url(torbox_download_url);
  } else {
    bar_notify(r->message, "dismiss");
  }
  torbox_result_free(r);
}

// On the request's thread: to the main thread.
static void
torbox_done(torbox_result_t* r) {
  WKRunLoopCallOnMainThread(torbox_main, r);
}

static void
torbox_list_to_page(void) {
  if(torbox_list(torbox_done, 0)) {
    bar_notify("Não deu para pedir a lista ao TorBox", "dismiss");
  }
}

// A magnet or a file's link to TorBox.
static void
torbox_take(const char* link) {
  char shown[256];
  if(!strncasecmp(link, "magnet:", 7)) {
    POC_LOG("torbox: a magnet link (%.24s...)", link);
  } else {
    POC_LOG("torbox: a link, %s", url_for_log(link, shown, sizeof(shown)));
  }
  const int err = torbox_add_link(link, torbox_done, 0);
  if(err == -2) {
    bar_notify("O TorBox não é usado para isto (YouTube, ou não é um link)", "dismiss");
  } else if(err) {
    bar_notify("Não deu para pedir ao TorBox", "dismiss");
  } else {
    bar_notify("A entregar ao TorBox...", "arrow-download");
  }
}

// The settings page: is there a key (and its last 4 characters).
static void
torbox_key_to_page(void) {
  char hint[8];
  const bool has = torbox_key_hint(hint, sizeof(hint));
  text_t t = {};
  text_add(&t, "window.setTorboxKey && setTorboxKey({key: ");
  text_add(&t, has ? "true" : "false");
  text_add(&t, ", hint: ");
  text_json(&t, has ? hint : "");
  text_add(&t, "})");
  text_run(PAGE, &t);
}

// settings.html's TorBox section (page_command): key, add, list, get.
static void
torbox_command(const char* name, const char* arg) {
  static char value[8192];

  address_decode(arg, value, sizeof(value));
  if(!strcmp(name, "key")) {
    const bool forget = !value[0];
    if(torbox_key_save(TORBOX_KEY_FILE, value)) {
      bar_notify("Esta chave não parece do TorBox: copia-a de torbox.app, Settings, API", "dismiss");
    } else {
      bar_notify(forget ? "A chave do TorBox foi apagada" : "Chave do TorBox guardada",
                 "shield-checkmark");
      torbox_key_to_page();
      if(!forget) {
        torbox_list_to_page();
      }
    }
  } else if(!strcmp(name, "add")) {
    torbox_take(value);
  } else if(!strcmp(name, "list")) {
    torbox_list_to_page();
  } else if(!strcmp(name, "get")) {  // "t:<torrent id>:<file id>" or "w:<web id>:<file id>"
    char kind = 0;
    long id = -1, file = -1;
    if(sscanf(value, "%c:%ld:%ld", &kind, &id, &file) == 3 && (kind == 't' || kind == 'w') &&
       !torbox_link(kind == 'w', id, file, torbox_done, 0)) {
      bar_notify("A pedir o ficheiro ao TorBox...", "arrow-download");
    } else {
      bar_notify("Não deu para pedir o ficheiro ao TorBox", "dismiss");
    }
  }
  memset(value, 0, sizeof(value));  // it may have been a key
}
#endif


// The saved logins (logins.h, ext/logins.js, docs/12-contas.md). A page's
// form sent with a password offers it (login-offer); the dialog asks
// whether to keep it for the page's origin; the keyboard offers it back in
// a field of the same origin. Neither the passwords nor the user names go
// into the log.
static void dialog_show(text_t* t, int download);
static void dialog_hide(void);

static struct {
  bool open;  // the dialog asks about it
  char origin[LOGIN_FIELD];
  char user[LOGIN_FIELD];
  char pass[LOGIN_FIELD];
} login_pending;
static double login_offer_ms;

static void
logins_store(void) {
  if(logins_save(LOGINS_FILE)) {
    POC_LOG("logins: %s was not written", LOGINS_FILE);
  }
}

// The origin of the page shown (http and https only).
static bool
page_origin(char* origin, size_t size) {
  char shown[4096];
  take_url(WKPageCopyCommittedURL(page(PAGE)), shown, sizeof(shown));
  return !logins_origin(shown, origin, size);
}

static void
logins_to_page(void) {
  if(!own_page_shown(SETTINGS_URL)) {
    return;
  }
  char* list = logins_list_json();
  text_t t = {};
  text_add(&t, "window.setLogins && setLogins(");
  text_add(&t, list ? list : "[]");
  text_add(&t, ")");
  text_run(PAGE, &t);
  free(list);
}

// login-offer?<user>&<password>, each URL-encoded (ext/logins.js, from a
// site: the origin is the page's, never the message's).
static void
login_offer(const char* arg) {
  char origin[LOGIN_FIELD];
  static char user[LOGIN_FIELD * 3], pass[LOGIN_FIELD * 3], part[LOGIN_FIELD * 9];

#ifdef APP_RN
  if(tab_is_private()) {  // a private tab's logins are not offered to be kept
    return;
  }
#endif
  if(!settings.logins || app.dialog_open || poc_now_ms() - login_offer_ms < 3000 ||
     !page_origin(origin, sizeof(origin)) || logins_never(origin)) {
    return;
  }
  const char* amp = strchr(arg, '&');
  const size_t user_length = amp ? (size_t)(amp - arg) : 0;
  if(!amp || user_length >= sizeof(part) || strlen(amp + 1) >= sizeof(part)) {
    return;
  }
  snprintf(part, sizeof(part), "%.*s", (int)user_length, arg);
  address_decode(part, user, sizeof(user));
  address_decode(amp + 1, pass, sizeof(pass));
  memset(part, 0, sizeof(part));
  if(pass[0] && strlen(pass) < LOGIN_FIELD && strlen(user) < LOGIN_FIELD &&
     !logins_known(origin, user, pass)) {
    login_pending.open = true;
    snprintf(login_pending.origin, sizeof(login_pending.origin), "%s", origin);
    snprintf(login_pending.user, sizeof(login_pending.user), "%s", user);
    snprintf(login_pending.pass, sizeof(login_pending.pass), "%s", pass);
    login_offer_ms = poc_now_ms();
    const char* host = strstr(origin, "://") + 3;
    text_t t = {};
    text_add(&t, "dialog.login({host: ");
    text_json(&t, host);
    text_add(&t, ", user: ");
    text_json(&t, user);
    text_printf(&t, ", update: %s, secure: %s, theme: '%s'})",
                logins_has_user(origin, user) ? "true" : "false",
                !strncmp(origin, "https:", 6) ? "true" : "false", theme_now());
    dialog_show(&t, 0);
    POC_LOG("logins: the dialog asks whether to keep a login for %s", origin);
  }
  memset(user, 0, sizeof(user));
  memset(pass, 0, sizeof(pass));
}

// The dialog's answer: 1 keep it, 2 never for this site, 0 not now.
static void
login_answer(int answer) {
  if(login_pending.open) {
    char text[LOGIN_FIELD + 64];
    const char* host = strstr(login_pending.origin, "://") + 3;
    if(answer == 1) {
      const int r = logins_put(login_pending.origin, login_pending.user, login_pending.pass,
                               (long)time(0));
      if(r >= 0) {
        logins_store();
        snprintf(text, sizeof(text), r == 2 ? "Palavra-passe atualizada: %s" : "Palavra-passe guardada: %s",
                 host);
        bar_notify(text, "lock-closed");
        POC_LOG("logins: kept one for %s", login_pending.origin);
      } else {
        bar_notify("Não deu para guardar a palavra-passe", "dismiss");
      }
    } else if(answer == 2) {
      logins_set_never(login_pending.origin, (long)time(0));
      logins_store();
      snprintf(text, sizeof(text), "Não volto a perguntar em %s", host);
      bar_notify(text, "lock-closed");
      POC_LOG("logins: never for %s", login_pending.origin);
    }
    logins_to_page();
  }
  memset(&login_pending, 0, sizeof(login_pending));
  dialog_hide();
}

// The keyboard opens for a field of the page: its origin's user names.
static void
logins_for_keys(int pane, const char* kind) {
  char origin[LOGIN_FIELD];

  snprintf(keys_logins, sizeof(keys_logins), "[]");
  keys_login_origin[0] = 0;
  if(pane != PAGE || !settings.logins ||
     (strcmp(kind, "text") && strcmp(kind, "email") && strcmp(kind, "tel") && strcmp(kind, "password")) ||
     !page_origin(origin, sizeof(origin))) {
    return;
  }
  const int n = logins_users_json(origin, keys_logins, sizeof(keys_logins));
  if(n <= 0) {
    snprintf(keys_logins, sizeof(keys_logins), "[]");
    return;
  }
  snprintf(keys_login_origin, sizeof(keys_login_origin), "%s", origin);
  POC_LOG("logins: the keyboard offers %d for %s", n, origin);
}

// In the page's own document (not a frame's): the field in focus, the name
// before the password or the password after the name, set as typed.
static const char login_fill_script[] = R"((function (u, p) {
  var e = document.activeElement;
  if (!e || e.tagName !== 'INPUT') return '';
  var all = Array.prototype.slice.call((e.form || document).querySelectorAll('input'));
  function shown(f) {
    return !f.disabled && !f.readOnly && f.type !== 'hidden' && (f.offsetWidth > 0 || f.offsetHeight > 0);
  }
  var pass = e.type === 'password' ? e
    : all.filter(function (f) { return f.type === 'password' && shown(f); })[0] || null;
  var user = null;
  if (e.type !== 'password') {
    user = e;
  } else {
    for (var i = all.indexOf(e) - 1; i >= 0; i--) {
      if (/^(text|email|tel)$/.test(all[i].type) && shown(all[i])) { user = all[i]; break; }
    }
  }
  var set = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value').set;
  function put(f, v) {
    f.focus();
    set.call(f, v);
    f.dispatchEvent(new Event('input', {bubbles: true}));
    f.dispatchEvent(new Event('change', {bubbles: true}));
  }
  var done = '';
  if (user && u) { put(user, u); done += 'u'; }
  if (pass) { put(pass, p); done += 'p'; }
  return done;
})()";

#ifdef APP_RN
/* BrowserPS5 RN (2026-11-11; BrowserPS5 NK's LOGIN_SEND_JS, 2026-11-09): the
 * login the user chose on the keyboard is sent by the browser itself, half a
 * second after it is filled, as a person would. Only when nothing else of
 * the form is left empty (a code, a captcha: the user's to fill); a name
 * alone (the first step of a login in two, Google's) only on a login page;
 * and not again on that site within two minutes (a wrong password: no loop).
 * The button is the form's submit, or a shown one that says so. The argument:
 * 1 the name step may go, 2 the password. The answer: which went, or 0. */
static const char login_send_script[] = R"((function (k) {
  var f = document.activeElement;
  if (!f || f.tagName !== 'INPUT' || !f.value) return '0';
  var pw = f.type === 'password', au = (f.autocomplete || '').toLowerCase();
  var go = pw ? (k & 2) : ((k & 1) && (au.indexOf('username') >= 0 ||
    /login|signin|sign-in|logon|auth|identifier/.test((location.hostname + location.pathname).toLowerCase())));
  if (!go) return '0';
  var r = f.form || document, q = r.querySelectorAll('input');
  for (var j = 0; j < q.length; j++) {
    var x = q[j], y = (x.type || 'text').toLowerCase();
    if (x.offsetParent !== null && !x.value && !x.disabled && !x.readOnly &&
        /^(text|email|tel|password|number)$/.test(y)) return '0';
  }
  var b = r.querySelector('button[type=submit],input[type=submit]');
  if (b && (b.offsetParent === null || b.disabled)) b = null;
  if (!b) {
    var c = r.querySelectorAll('button,[role=button],input[type=button]');
    for (var i = 0; i < c.length; i++) {
      var v = ((c[i].innerText || c[i].value || '') + '').trim().toLowerCase();
      if (c[i].offsetParent !== null && !c[i].disabled &&
          /^(sign ?in|log ?in|next|continue|submit|entrar|iniciar|seguinte|continuar|avan|pr.ximo)/.test(v)) { b = c[i]; break; }
    }
  }
  if (!b && !f.form) return '0';
  setTimeout(function () {
    if (b) b.click(); else if (f.form.requestSubmit) f.form.requestSubmit(); else f.form.submit();
  }, 500);
  return pw ? '2' : '1';
})()";
#define LOGIN_SEND_GAP_MS 120000.0
static char login_send_origin[LOGIN_FIELD];  // where the last fill was
static char login_sent_origin[LOGIN_FIELD];  // where something was last sent, and when each step
static double login_sent_name_ms, login_sent_pass_ms;

static void
login_sent(WKTypeRef result, WKErrorRef, void*) {
  char what[8] = "";
  if(result && WKGetTypeID(result) == WKStringGetTypeID()) {
    WKStringGetUTF8CString((WKStringRef)result, what, sizeof(what));
  }
  if(what[0] == '1' || what[0] == '2') {
    if(strcmp(login_sent_origin, login_send_origin)) {
      snprintf(login_sent_origin, sizeof(login_sent_origin), "%s", login_send_origin);
      login_sent_name_ms = login_sent_pass_ms = 0;
    }
    *(what[0] == '1' ? &login_sent_name_ms : &login_sent_pass_ms) = poc_now_ms();
  }
  POC_LOG("logins: %s", what[0] == '2' ? "sent by the browser (the password's step)"
                        : what[0] == '1' ? "sent by the browser (the name's step)"
                        : "not sent by the browser (something else to fill, no button, or sent just before)");
}
#endif

static void
login_filled(WKTypeRef result, WKErrorRef, void*) {
  char what[8] = "";
  if(result && WKGetTypeID(result) == WKStringGetTypeID()) {
    WKStringGetUTF8CString((WKStringRef)result, what, sizeof(what));
  }
  POC_LOG("logins: filled %s", !strcmp(what, "up") ? "the name and the password"
                               : !strcmp(what, "p") ? "the password"
                               : !strcmp(what, "u") ? "the name" : "nothing (no field in the page)");
#ifdef APP_RN
  if(what[0]) {
    const bool same = !strcmp(login_sent_origin, login_send_origin);
    const double now = poc_now_ms();
    char script[sizeof(login_send_script) + 8];
    snprintf(script, sizeof(script), "%s%d)", login_send_script,
             (!same || !login_sent_name_ms || now - login_sent_name_ms > LOGIN_SEND_GAP_MS ? 1 : 0)
             | (!same || !login_sent_pass_ms || now - login_sent_pass_ms > LOGIN_SEND_GAP_MS ? 2 : 0));
    run_script(PAGE, script, 0, login_sent);
  }
#endif
}

// "Entrar como ..." on the keyboard: the index-th of its list, if the page
// is still at the origin it was offered for.
static void
login_fill(int index) {
  char origin[LOGIN_FIELD], user[LOGIN_FIELD], pass[LOGIN_FIELD];
  static char script[sizeof(login_fill_script) + LOGIN_FIELD * 12 + 16];

  if(!keys_login_origin[0] || !page_origin(origin, sizeof(origin)) || strcmp(origin, keys_login_origin) ||
     logins_get(origin, index, user, sizeof(user), pass, sizeof(pass))) {
    POC_LOG("logins: not filled (the page is not where it was)");
    keys_login_origin[0] = 0;
    return;
  }
#ifdef APP_RN
  snprintf(login_send_origin, sizeof(login_send_origin), "%s", origin);  // login_filled(): then sent
#endif
  size_t n = (size_t)snprintf(script, sizeof(script), "%s", login_fill_script);
  n += json_string(script + n, sizeof(script) - n, user);
  n += (size_t)snprintf(script + n, sizeof(script) - n, ", ");
  n += json_string(script + n, sizeof(script) - n, pass);
  snprintf(script + n, sizeof(script) - n, ")");
  run_script(PAGE, script, 0, login_filled);
  memset(script, 0, sizeof(script));
  memset(user, 0, sizeof(user));
  memset(pass, 0, sizeof(pass));
  keys_login_origin[0] = 0;
}

// The settings page: login-remove?<origin>\n<user> (no user: the "never").
static void
login_remove_command(const char* arg) {
  static char text[LOGIN_FIELD * 2 + 8];
  address_decode(arg, text, sizeof(text));
  char* user = strchr(text, '\n');
  if(user) {
    *user++ = 0;
  }
  if(!logins_remove(text, user && *user ? user : 0)) {
    logins_store();
    POC_LOG("logins: one removed for %s", text);
  }
  logins_to_page();
}


// A .user.js: read, and offered on the settings page.
static void
extension_offer(download_t* d) {
  size_t length = 0;
  char* source = read_whole(d->partial, 1 << 20, &length);
  ext_meta_t meta;

  unlink(d->partial);
  if(!source || ext_parse(source, &meta)) {
    POC_LOG("download %d: not a user script", d->id);
    d->state = DL_FAILED;
    free(source);
    return;
  }
  free(pending_source);
  pending_source = source;
  pending_meta = meta;
  d->state = DL_DONE;
  snprintf(d->path, sizeof(d->path), "Extensões");
  POC_LOG("extension offered: \"%s\" %s, %zu bytes", meta.name, meta.version, length);
  settings_open("extensoes");
}


static void dialog_for_download(const download_t* d);

#ifdef APP_RN
// ---------------------------------------------------------------------------
// Downloads in several parts (2026-11-12). One connection takes ~9 MB/s on
// the console whatever the line gives (the TCP window stops at the 512 KiB
// the system allows a socket: poc/net_window); the console's line (Wi-Fi
// that night: ~30 MB/s with 4 connections or 32; the PC, on a cable, took
// 84 with 16) gives several times that. A download of 200 MiB or more whose
// server gives parts is brought by up to MULTI_WORKERS connections at once.
//   The file is one from the start: every connection writes its bytes at
// their place in it (no parts to join at the end: for 50 GB that was minutes
// and twice the room). The download itself is one of the connections and
// goes on as it started (nothing is asked twice, so a link that works once
// still works); the others are downloads of the same address with
// "#bps5range=<first>-<last>" after it, which the engine turns into a
// request for those bytes on a connection of its own, written into the file
// it is told at their offset, never truncated and never removed
// (webkit/security/CurlRequest.cpp.patch, network/NetworkDataTaskCurl.cpp
// .patch). They are asked for as navigations of the tab the download came
// from, made downloads at once (decidePageNavigation(): the page shown
// stays as it is), so they carry the site's cookies as the download did.
//   The file's bytes are pieces (multi_seg_t), each brought by one
// connection. A connection with nothing left takes a piece nobody has, or
// half of what is left of the piece with the most left; the connection that
// had that one stops at the new end (it is cancelled there, the download
// itself included: the engine removes a cancelled download's file by its
// name, so the file has another name while it is in parts; link() is
// refused on the console, errno 2). A connection that fails leaves what it
// brought (it is in the file) and the rest of its piece for another.
//   Parts must never make a download slower (one night's run: OVH's test
// file at 1.5 MB/s in four parts, and at 9 alone). So the download runs
// alone for three seconds first, and its speed is kept; five seconds after
// three other connections are bringing bytes, what all take together is
// compared with it: under 1.3 times as much, or a server that gives no
// part (no 206), and the others are dropped: the download goes on alone, as
// before. Its own piece is never ended before that comparison.
//   2026-11-14: the line failing under a large download must not lose it (the console is on Wi-Fi; 50 GB are
// half an hour). Once the parts are judged worth it, a connection that fails, or that brings nothing for
// MULTI_STALL_MS (a line gone quiet gives no error for a long time), leaves what it brought and the rest of its
// piece to another, however many times. While no byte comes after a failure, one connection at a time is
// asked for, with waits (1 s, then 3, 10, 30), for as long as it takes; with the first bytes the others
// return. A part is never asked for while its tab is opening a page of the user's (the part's navigation
// would stop that one); with its tab closed, the tab shown asks for the parts.
//   A download by one connection is kept the same way ("solo": 32 MiB or more that is not for parts, or
// whose server gives no part, or whose parts were not worth it): its file has the other name and its set is
// one piece, its own. If its connection fails, or brings nothing for a minute, the rest is asked for from
// where it stopped, as a part (a server that gives none: the download fails, as it did before).
//   Kept from one session to the next (2026-11-14): what a set's file has is written down, as the ranges
// that are missing, in a small file of the app's own folder (PARTIAL_DIR/<the partial's name>.map, with the
// download's name, folder and addresses): every 15 s, the note of 45 s before (what it says is in the file has
// reached the disk by then, with no fsync of a file being written at full speed), and at the moment the
// browser is closed, what there is then. The next session finds the map and its file and shows the download
// as "Interrompida"; "Continuar" asks for the missing ranges as parts (multi_resume()), "Tirar da lista"
// removes both. Kept for 14 days. A download with no map (its first 45 s, in a session that fell) is not kept.
//   Not for private tabs (their tab's data is another), nor user scripts or
// torrents. Off with the settings' "Transferências rápidas" (settings.multipart), and never with the
// marker file /data/homebrew/<the title>/no-multipart (the tests').
#define MULTI_WORKERS 8
#define MULTI_SEGS 64
#define MULTI_MIN (200LL << 20)
#define MULTI_SOLO_MIN (32LL << 20)  // from this size a download by one connection can go on after a failure
#define MULTI_STALL_SOLO_MS 60000    // (its own connection, with no other: dropped after this long with nothing)
#define MULTI_SPLIT_MIN (48LL << 20)  // a piece with less than twice this left is not split for an idle connection
#define MULTI_ALONE_MS 3000
#define MULTI_JUDGE_MS 5000
#define MULTI_STALL_MS 20000  // a connection that brings nothing for this long is dropped (multi_drop())
#define MULTI_OFF_FILE "/data/homebrew/" TITLE_ID "/no-multipart"
enum { MS_FREE, MS_ASKED, MS_RUNNING, MS_FLOWING, MS_STOPPING, MS_DONE };

typedef struct multi_seg {
  long long from, to;    // its bytes of the file: [from, to); "to" comes down when half of it goes to another
  long long got;         // how many of them are in the file
  long long asked_to;    // where the range asked of the server ends
  int state;             // MS_*: nobody's, asked for, begun, its bytes coming, being stopped at its end, all in
  bool main;             // brought by the download itself
  WKDownloadRef ref;     // by this download otherwise
  double fresh_ms;       // when its bytes last came (or it began, or was told to stop)
  bool dropping;         // being stopped because it brought nothing: the rest of its piece is another's
} multi_seg_t;

typedef struct multi {
  int id;                // its download's, 0: free
  multi_seg_t seg[MULTI_SEGS];
  int segs;
  long long total;
  WKPageRef page;        // the tab's page that asks for the parts (checked before each use: multi_page_fine())
  double asked_ms;       // when the part being asked for was asked
  double began_ms;       // when it was taken for several parts, and its bytes then:
  long long began_bytes; //   MULTI_ALONE_MS later its speed alone is known
  double alone;          // that speed, bytes a second (0: not yet: nothing else is asked for yet)
  double flow_ms;        // when three other connections were bringing bytes, and the bytes in then:
  long long flow_bytes;  //   MULTI_JUDGE_MS later the parts are judged
  bool judged;           // they were, and stay
  bool main_gone;        // the download itself is stopped (its piece is in) or failed: the others bring the rest
  bool solo;             // by one connection: the download's own, or after it failed one for the rest (multi_alone())
  long long own_written; // what the download's own connection wrote so far
  long long banked;      // bytes in the file from connections that ended before their piece did (multi_rest())
  int failures;
  bool trouble;          // a connection failed and no byte came since: one at a time, with waits (multi_trouble())
  double trouble_ms;     // since when
  int troubles;          // connections that failed since then
  double bytes_ms;       // when bytes last came, by any connection
  double pump_ms;        // when multi_tick() last looked for a connection to ask for
  double map_ms;         // when what its file has was last noted (multi_map_note())
  char* maps[3];         // those notes, the newest first: the one that goes to the disk is 45 s old
  char map[700];         // its map's file: with it another session goes on with the download (multi_resume())
  int most;              // the most connections it had at once
  int limit;             // how many the server takes at once (MULTI_WORKERS until it refuses one: HTTP 429 and the like)
  int helped;            // other connections that were given a part so far
  int refused;           // refusals in a row since the last one given
  double hold_ms;        // no new connection is asked for before this (after a refusal)
  char file[640];        // the file's name while it is in parts
  char url[4096];
} multi_t;

static multi_t multis[4];
static bool multi_test_off;  // (the tests': as with the settings' "Transferências rápidas" off)
static bool multi_test_fall; // (the tests': the browser closes as if it fell: nothing is written down at the end)
static void download_all_in(download_t* d);
static bool multi_page_fine(WKPageRef p);
static void multi_pump(multi_t* m);
static void multi_check(multi_t* m);
static void multi_rest(multi_t* m, multi_seg_t* s);

static multi_t*
multi_of(int id) {
  for(multi_t& m : multis) {
    if(id && m.id == id) {
      return &m;
    }
  }
  return 0;
}

// How many connections bring it now (for the list and the log).
static int
multi_workers(const multi_t* m) {
  int n = 0;
  for(int i = 0; i < m->segs; i++) {
    n += m->seg[i].state >= MS_ASKED && m->seg[i].state <= MS_STOPPING;
  }
  return n;
}

// What is in so far, of every piece: the download's count and speed.
static void
multi_account(multi_t* m) {
  download_t* d = download_by_id(m->id);
  long long got = 0;

  if(!d) {
    return;
  }
  for(int i = 0; i < m->segs; i++) {
    const long long size = m->seg[i].to - m->seg[i].from;
    got += m->seg[i].got < size ? m->seg[i].got : size;
  }
  got += m->banked;
  d->received = got;
  dl_rate_add(&d->rate, got, poc_now_ms());
  downloads_changed = true;
}

// A connection failed: the next is asked for after a wait, longer the longer no byte has come, and one at a
// time until one brings bytes (multi_bytes()). There is no giving up: the file and what is in it stay.
static void
multi_trouble(multi_t* m) {
  const double now = poc_now_ms();
  const double quiet = now - m->bytes_ms;
  const double wait = quiet < 10000 ? 1000 : quiet < 30000 ? 3000 : quiet < 120000 ? 10000 : 30000;

  if(!m->trouble) {
    m->trouble = true;
    m->trouble_ms = now;
    m->troubles = 0;
  }
  m->troubles++;
  if(now + wait > m->hold_ms) {
    m->hold_ms = now + wait;
  }
}

// Bytes came, by any connection. True: there was trouble and it is over (the caller asks for the others again).
static bool
multi_bytes(multi_t* m) {
  m->bytes_ms = poc_now_ms();
  if(!m->trouble) {
    return false;
  }
  m->trouble = false;
  POC_LOG("download %d: bytes again, %.0f s and %d failed connection(s) later", m->id,
          (m->bytes_ms - m->trouble_ms) / 1e3, m->troubles);
  return true;
}

// Is the tab opening a page (a link the user pressed, an address)? A part asked for now would stop that.
static bool
multi_page_busy(WKPageRef p) {
  static char text[8192];
  bool busy = false;

  for(int k = 0; k < 2 && !busy; k++) {
    WKURLRef url = k ? WKPageCopyPendingAPIRequestURL(p) : WKPageCopyProvisionalURL(p);
    if(url) {
      take_url(url, text, sizeof(text));
      busy = text[0] && !strstr(text, "#bps5range=");
    }
  }
  // 2026-11-14: a press just made in the tab shown may be on a link, and for some milliseconds its navigation is
  // not the tab's yet (the page's process asks, the app answers): a piece asked for in between would take its
  // place, as one piece took another's (multi_page_asked()), and the press would do nothing. A piece waits 0.6 s
  // after a press there (multi_tick() asks again).
  if(!busy && p == page(PAGE) && poc_now_ms() - last_gesture_ms < 600) {
    busy = true;
  }
  return busy;
}

// 2026-11-14 (found on the PC, tests-host/multi-sim.in.cpp run_duo(): two downloads at once): ONE navigation not
// begun yet in a tab, whoever asks. Two downloads in parts begun from the same tab each asked for their pieces by
// themselves (each waits for its own piece to begin before the next: multi_pump()), and a navigation asked of a
// tab takes the place of the one the tab had not begun yet: the piece asked for first never came, and nobody was
// told. Eight seconds later it counted as a failure of its download; before that download's speed alone was
// known, it went on by one connection for good. Is a piece of any set still to begin in this tab?
static bool
multi_page_asked(WKPageRef p) {
  for(const multi_t& m : multis) {
    for(int i = 0; m.id && m.page == p && i < m.segs; i++) {
      if(m.seg[i].state == MS_ASKED) {
        return true;
      }
    }
  }
  return false;
}

static void
multi_cancel_ignored(WKDataRef, const void*) {
}

#define MULTI_MAP_MS 15000
#define MULTI_KEEP_DAYS 14
static volatile int multi_map_busy;     // a map is being written by its thread
static volatile int multi_maps_written;
static int multi_gen[4];                // a slot's changes when its set ends: a map written then is not kept

// A partial file's map ("<folder>/N-T.part" or "...part.multi" -> PARTIAL_DIR "/N-T.part.map").
static void
multi_map_path(const char* file, char* out, size_t size) {
  const char* base = strrchr(file, '/');
  char name[160];

  snprintf(name, sizeof(name), "%s", base ? base + 1 : file);
  char* multi = strstr(name, ".multi");
  if(multi) {
    *multi = 0;
  }
  snprintf(out, size, PARTIAL_DIR "/%s.map", name);
}

// What a set's file has now, as the map's text (malloc'd): the download's own facts and the ranges missing.
static char*
multi_map_text(const multi_t* m, const download_t* d) {
  const size_t size = sizeof(m->url) + sizeof(d->url) + 4096 + MULTI_SEGS * 48;
  char* text = (char*)malloc(size);
  long long have = m->total;

  if(!text) {
    return 0;
  }
  size_t n = (size_t)snprintf(text, size, "bps5map 1\ntotal %lld\nfile %s\nname %s\nkind %s\ndir %s\nhost %s\nurl %s\nask %s\n",
                              m->total, m->file, d->name, d->kind, d->chosen ? d->dir : "", d->host, m->url, d->url);
  for(int i = 0; i < m->segs && n + 64 < size; i++) {
    const multi_seg_t* s = &m->seg[i];
    const long long all = s->to - s->from;
    const long long got = s->state == MS_DONE || s->got > all ? all : s->got;
    if(got < all) {
      n += (size_t)snprintf(text + n, size - n, "miss %lld %lld\n", s->from + got, s->to);
      have -= all - got;
    }
  }
  snprintf(text + n, size - n, "have %lld\nend\n", have);
  return text;
}

// The map's file, written whole under another name first. Only if its slot's set is still the same one.
static void
multi_map_put(const char* map, const char* text, int slot, int gen, const char* suffix) {
  char other[760];
  const size_t n = strlen(text);

  snprintf(other, sizeof(other), "%s%s", map, suffix);
  const int out = open(other, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  const bool fine = out >= 0 && write(out, text, n) == (ssize_t)n;
  if(out >= 0) {
    close(out);
  }
  if(fine && multi_gen[slot] == gen && !rename(other, map)) {
    __sync_fetch_and_add(&multi_maps_written, 1);
  } else {
    unlink(other);
  }
}

typedef struct multi_map_job {
  char map[700];
  char* text;
  int slot, gen;
} multi_map_job_t;

static void*
multi_map_thread(void* arg) {
  multi_map_job_t* job = (multi_map_job_t*)arg;
  multi_map_put(job->map, job->text, job->slot, job->gen, ".tmp");
  free(job->text);
  free(job);
  multi_map_busy = 0;
  return 0;
}

// Every MULTI_MAP_MS (multi_tick()): what the file has now is noted, and the note of three times before goes to
// the disk, on a thread of its own (the main thread writes no file here).
static void
multi_map_note(multi_t* m) {
  const download_t* d = download_by_id(m->id);
  pthread_t thread;

  if(!d || d->state != DL_RUNNING || !m->map[0]) {
    return;
  }
  char* due = m->maps[2];
  m->maps[2] = m->maps[1];
  m->maps[1] = m->maps[0];
  m->maps[0] = multi_map_text(m, d);
  if(!due) {
    return;
  }
  if(__sync_lock_test_and_set(&multi_map_busy, 1)) {  // the one before is still being written: the next will do
    free(due);
    return;
  }
  multi_map_job_t* job = (multi_map_job_t*)calloc(1, sizeof(*job));
  if(job) {
    snprintf(job->map, sizeof(job->map), "%s", m->map);
    job->text = due;
    job->slot = (int)(m - multis);
    job->gen = multi_gen[job->slot];
  }
  if(!job || pthread_create(&thread, 0, multi_map_thread, job)) {
    free(due);
    free(job);
    multi_map_busy = 0;
    return;
  }
  pthread_detach(thread);
}

// A download left by a session before is given up: its file and its map go (the list's "Tirar da lista").
static void
multi_kept_forget(download_t* d) {
  char path[760];

  snprintf(path, sizeof(path), "%s.multi", d->partial);
  unlink(path);
  multi_map_path(d->partial, path, sizeof(path));
  unlink(path);
  POC_LOG("download %d: what a session before left of it is removed", d->id);
  d->kept = false;
  d->received = 0;
  d->state = DL_CANCELLED;
  downloads_changed = true;
}

// The parts end: the other connections are cancelled; the file stays (under the name it has) or goes.
static void
multi_end(multi_t* m, bool keep_file) {
  for(int i = 0; i < m->segs; i++) {
    multi_seg_t* s = &m->seg[i];
    if(s->ref) {
      if(s->state >= MS_RUNNING && s->state <= MS_FLOWING) {
        WKDownloadCancel(s->ref, 0, multi_cancel_ignored);
      }
      WKRelease(s->ref);
    }
  }
  if(!keep_file && m->file[0]) {
    unlink(m->file);
  }
  multi_gen[m - multis]++;  // (a map on its way to the disk is not put)
  if(m->map[0]) {  // however it ended, there is nothing to go on with
    unlink(m->map);
  }
  for(char* note : m->maps) {
    free(note);
  }
  memset(m, 0, sizeof(*m));
}

// Something went wrong with the parts: the download goes on alone, if it is still there.
static void
multi_abandon(multi_t* m, const char* why) {
  download_t* d = download_by_id(m->id);
  const bool gone = m->main_gone;

  POC_LOG("download %d: %s (%s)", m->id,
          gone ? "FAILED: its own connection was stopped already" : "in one part after all", why);
  if(!gone && d && m->file[0]) {
    rename(m->file, d->partial);  // its own name again: the download writes on into it
    m->file[0] = 0;
  }
  multi_end(m, false);
  if(d && gone && d->state == DL_RUNNING) {
    d->state = DL_FAILED;
    unlink(d->partial);
    downloads_changed = true;
  }
}

// The parts are not worth it, or the server gives none: the download goes on by its own connection, as it
// began. Its set stays, as one piece that is all its own ("solo"): if that connection fails or brings nothing,
// the rest is asked for from where it stopped (multi_main_failed(), multi_drop()).
static void
multi_alone(multi_t* m, const char* why) {
  download_t* d = download_by_id(m->id);

  if(m->main_gone || !d || !m->file[0] || !m->seg[0].main || m->seg[0].state != MS_FLOWING) {
    multi_abandon(m, why);
    return;
  }
  POC_LOG("download %d: in one part after all (%s); after a failure it goes on from where it was", m->id, why);
  for(int i = 0; i < m->segs; i++) {
    multi_seg_t* s = &m->seg[i];
    if(s->ref) {
      if(s->state >= MS_RUNNING && s->state <= MS_FLOWING) {
        WKDownloadCancel(s->ref, 0, multi_cancel_ignored);
      }
      WKRelease(s->ref);
    }
  }
  memset(m->seg, 0, sizeof(m->seg));
  m->segs = 1;
  m->seg[0].to = m->seg[0].asked_to = m->total;
  m->seg[0].got = m->own_written;
  m->seg[0].main = true;
  m->seg[0].state = MS_FLOWING;
  m->seg[0].fresh_ms = m->bytes_ms = poc_now_ms();
  m->banked = 0;
  m->solo = m->judged = true;
  m->trouble = false;
  m->hold_ms = 0;
  m->refused = 0;
  multi_account(m);
}

// The piece an address asks for ("...#bps5range=<first>-<last>"), if the app asked for it.
static multi_seg_t*
multi_asked(const char* url, multi_t** owner) {
  const char* mark = strstr(url, "#bps5range=");
  long long from = -1, last = -1;

  if(!mark || sscanf(mark + 11, "%lld-%lld", &from, &last) != 2) {
    return 0;
  }
  const size_t n = (size_t)(mark - url);
  for(multi_t& m : multis) {
    if(!m.id || strlen(m.url) != n || strncmp(url, m.url, n)) {
      continue;
    }
    for(int i = 0; i < m.segs; i++) {
      multi_seg_t* s = &m.seg[i];
      if(s->state == MS_ASKED && s->from == from && s->asked_to - 1 == last) {
        *owner = &m;
        return s;
      }
    }
  }
  return 0;
}

// A connection for a piece nobody brings: one is asked for at a time (the tab's page makes one navigation at a
// time), while fewer than MULTI_WORKERS bring bytes. With no such piece, half of what is left of the piece with
// the most left becomes one.
static void
multi_pump(multi_t* m) {
  static char url[sizeof(m->url) + 64];
  multi_seg_t* pick = 0;
  multi_seg_t* most = 0;
  long long most_left = 0;
  int busy = 0;

  if(!m->alone || poc_now_ms() < m->hold_ms || (m->solo && !m->main_gone)) {  // (solo: no other while its own brings it)
    return;
  }
  for(int i = 0; i < m->segs; i++) {
    multi_seg_t* s = &m->seg[i];
    if(s->state == MS_ASKED) {
      return;
    }
    busy += s->state >= MS_RUNNING && s->state <= MS_STOPPING;
    if(s->state == MS_FREE && !pick) {
      pick = s;
    }
    const long long left = s->to - s->from - s->got;
    if((s->state == MS_FLOWING || s->state == MS_RUNNING) && left > most_left) {
      most = s;
      most_left = left;
    }
  }
  if(busy > m->most) {
    m->most = busy;
  }
  if(busy >= (m->trouble || m->solo ? 1 : m->limit)) {  // after a failure: one at a time until bytes come (multi_trouble())
    return;
  }
  if(!pick && most && most_left >= 2 * MULTI_SPLIT_MIN && m->segs < MULTI_SEGS && m->judged && !m->solo) {
    const long long middle = (most->from + most->got + most_left / 2) & ~((1LL << 20) - 1);
    pick = &m->seg[m->segs++];
    memset(pick, 0, sizeof(*pick));
    pick->from = middle;
    pick->to = most->to;
    most->to = middle;  // its connection stops there (multi_progress(), multi_progress_of())
  }
  if(!pick) {
    return;
  }
  if(!multi_page_fine(m->page)) {  // its tab was closed: the tab shown asks for the parts from now on
    WKPageRef shown = page(PAGE);
    if(!multi_page_fine(shown)) {  // (a private one does not: later, with another tab shown)
      if(!m->main_gone) {
        multi_abandon(m, "its tab is gone");
      }
      return;
    }
    m->page = shown;
  }
  if(multi_page_busy(m->page)) {  // asked for in a moment (multi_tick())
    return;
  }
  if(multi_page_asked(m->page)) {  // another download's piece is to begin there first (multi_claim() says when it has)
    return;
  }
  pick->state = MS_ASKED;
  pick->asked_to = pick->to;
  m->asked_ms = poc_now_ms();
  snprintf(url, sizeof(url), "%s#bps5range=%lld-%lld", m->url, pick->from, pick->to - 1);
  WKURLRef address = WKURLCreateWithUTF8CString(url);
  WKPageLoadURL(m->page, address);
  WKRelease(address);
}

static multi_seg_t*
multi_seg_of(WKDownloadRef download, const void* info, multi_t** owner) {
  const int n = (int)(intptr_t)info - 1;
  if(n < 0 || n >= (int)(sizeof(multis) / sizeof(*multis)) * MULTI_SEGS) {
    return 0;
  }
  multi_t* m = &multis[n / MULTI_SEGS];
  multi_seg_t* s = &m->seg[n % MULTI_SEGS];
  if(!m->id || s->ref != download) {  // of a set of parts that ended
    return 0;
  }
  *owner = m;
  return s;
}

// Is every piece in? Then the file has its name again and the download ends as any other.
static void
multi_check(multi_t* m) {
  download_t* d = download_by_id(m->id);
  struct stat st;

  for(int i = 0; i < m->segs; i++) {
    if(m->seg[i].state != MS_DONE) {
      return;
    }
  }
  if(!d || d->state != DL_RUNNING) {
    multi_end(m, false);
    return;
  }
  const long long total = m->total;
  const int most = m->most;
  const bool fine = !stat(m->file, &st) && st.st_size == total && !rename(m->file, d->partial);
  if(!fine) {
    POC_LOG("download %d: FAILED: its file is not what its parts make (%lld bytes wanted)", d->id, total);
    multi_end(m, false);
    d->state = DL_FAILED;
    unlink(d->partial);
    downloads_changed = true;
    return;
  }
  multi_end(m, true);
  d->received = d->total = total;
  POC_LOG("download %d: every piece is in (%lld bytes, up to %d connections at once)", d->id, total, most);
  download_all_in(d);
}

// A connection told to stop has stopped (its cancel was answered, or was not in ten seconds: multi_tick()).
static void
multi_stopped(multi_t* m, multi_seg_t* s) {
  if(s->dropping) {  // it brought nothing (multi_drop()): what it had is in the file, the rest is another's
    if(s->main) {
      m->main_gone = true;
    }
    multi_rest(m, s);
  } else {
    s->state = MS_DONE;
    s->got = s->to - s->from;
    if(s->main) {
      m->main_gone = true;
    } else if(s->ref) {
      WKRelease(s->ref);
      s->ref = 0;
    }
  }
  multi_account(m);
  multi_pump(m);
  multi_check(m);
}

// A connection is told to stop at its piece's end (its range went on: the piece was halved, or it is the
// download itself), or because it brings nothing (multi_drop()). Not from inside its own callback: at the main
// thread's next turn. info: as its client's.
static void
multi_stop(multi_t* m, multi_seg_t* s) {
  s->state = MS_STOPPING;
  s->fresh_ms = poc_now_ms();  // (with no answer to its cancel in ten seconds it counts as stopped: multi_tick())
  WKRunLoopCallOnMainThread([](void* info) {
    const int n = (int)(intptr_t)info - 1;
    multi_t* m = &multis[n / MULTI_SEGS];
    multi_seg_t* s = &m->seg[n % MULTI_SEGS];
    download_t* d = m->id ? download_by_id(m->id) : 0;
    WKDownloadRef ref = s->main ? (d ? d->ref : 0) : s->ref;
    if(!d || s->state != MS_STOPPING || !ref) {
      return;
    }
    WKDownloadCancel(ref, info, [](WKDataRef, const void* info) {
      const int n = (int)(intptr_t)info - 1;
      multi_t* m = &multis[n / MULTI_SEGS];
      multi_seg_t* s = &m->seg[n % MULTI_SEGS];
      if(!m->id || s->state != MS_STOPPING) {
        return;
      }
      multi_stopped(m, s);
    });
  }, (void*)(intptr_t)((m - multis) * MULTI_SEGS + (s - m->seg) + 1));
}

// A connection ended before its piece did: what it brought is in the file; the rest is a piece for another.
static void
multi_rest(multi_t* m, multi_seg_t* s) {
  const long long size = s->to - s->from;
  const long long in = s->got < size ? s->got : size;
  m->banked += in;  // counted as in from now on (multi_account())
  s->from += in;
  s->got = 0;
  s->main = false;
  s->dropping = false;
  s->state = s->from < s->to ? MS_FREE : MS_DONE;
  if(s->ref) {
    WKRelease(s->ref);
    s->ref = 0;
  }
}

// A part's download: where it goes (the download's own file, at its place: only a part of the file is taken,
// 206), its bytes, its end.
static WKStringRef
multi_destination(WKDownloadRef download, WKURLResponseRef response, WKStringRef, const void* info) {
  multi_t* m = 0;
  multi_seg_t* s = multi_seg_of(download, info, &m);
  download_t* d = s ? download_by_id(m->id) : 0;
  const int status = response ? WKURLResponseHTTPStatusCode(response) : 0;

  if(!s) {
    return string("");
  }
  if(status != 206 || !d) {
    // No part given. A server that takes only so many connections at once says so (429, 503: OVH's test
    // server at the eighth, 2026-11-12): the ones it gave stay, no more are asked for than that, and the next
    // is asked a while later. A server that gave none at all gives no parts: the download goes on alone.
    char why[64];
    int others = 0;
    snprintf(why, sizeof(why), "the server gave no part: HTTP %d", status);
    WKRelease(s->ref);  // an empty destination ends this one: not cancelled twice
    s->ref = 0;
    s->state = MS_FREE;
    for(int i = 0; i < m->segs; i++) {
      others += m->seg[i].state >= MS_RUNNING && m->seg[i].state <= MS_STOPPING;
    }
    m->refused++;
    // With no part ever given by this server, only the rest of a download whose own connection is gone is asked
    // for again, and only of a server that says "later" (429, 503): one that gives the whole file (200) or
    // refuses (403: the link ran out) gives no parts.
    const bool later = status == 429 || status == 503;
    // 2026-11-14 (found on the PC, tests-host/make-multi-sim.py, seed 2565): a server that gives ONE connection,
    // a download gone on with after a pause: its one connection brought the file, a second was asked for every
    // 15 s and refused each time, and at the 21st refusal the download was given up and its file removed, with
    // 600 MB in and coming. Two things were wrong: the fewest connections "it gives" was taken to be 2, so one
    // more than it gave was asked for, for ever; and refusals were counted to 20 with no part given in between,
    // though bytes were coming. Now: what it had when it refused one more is what it gives (1 too); and a server
    // that says "later" to the rest of a download whose own connection is gone is asked again for as long as it
    // takes (there is nothing else to go on with, and what came stays), with a minute between askings.
    const bool patient = later && m->main_gone;
    if(!d || (!patient && (m->refused > 20 || !m->helped))) {
      if(d && !m->main_gone) {
        multi_alone(m, why);
      } else {
        multi_abandon(m, why);
      }
      return string("");
    }
    if(others < m->limit) {
      m->limit = others < 1 ? 1 : others;
      POC_LOG("download %d: %d connection(s) at once and no more (%s)", m->id, m->limit, why);
    }
    m->hold_ms = poc_now_ms() + (m->refused < 5 ? 3000.0 * m->refused : m->refused < 20 ? 15000.0 : 60000.0);
    if(!m->flow_ms) {  // those there are are judged (multi_tick())
      m->flow_ms = poc_now_ms();
      m->flow_bytes = d->received;
    }
    return string("");
  }
  s->state = MS_FLOWING;
  s->fresh_ms = poc_now_ms();
  m->helped++;
  m->refused = 0;
  int flowing = 0;
  for(int i = 0; i < m->segs; i++) {
    flowing += !m->seg[i].main && (m->seg[i].state == MS_FLOWING || m->seg[i].state == MS_DONE);
  }
  if(flowing >= 3 && !m->flow_ms) {  // judged in a while (multi_tick())
    m->flow_ms = poc_now_ms();
    m->flow_bytes = d->received;
  }
  return string(m->file);
}

static void
multi_progress(WKDownloadRef download, long long now, long long written, long long, const void* info) {
  multi_t* m = 0;
  multi_seg_t* s = multi_seg_of(download, info, &m);

  if(!s) {
    return;
  }
  const long long before = s->got;
  s->got = written > 0 ? written : s->got + now;  // (an engine that tells no totals: counted here)
  if(s->got > before) {
    s->fresh_ms = poc_now_ms();
    if(multi_bytes(m)) {
      multi_pump(m);
    }
  }
  multi_account(m);
  if(s->state == MS_FLOWING && s->to < s->asked_to && s->got >= s->to - s->from) {  // its piece was halved: it ends here
    multi_stop(m, s);
  }
}

static void
multi_finished(WKDownloadRef download, const void* info) {
  multi_t* m = 0;
  multi_seg_t* s = multi_seg_of(download, info, &m);

  if(!s) {
    return;
  }
  s->got = s->to - s->from;  // its whole range came (as far as it was asked: the piece may have been halved since)
  s->state = MS_DONE;
  WKRelease(s->ref);
  s->ref = 0;
  multi_account(m);
  multi_pump(m);
  multi_check(m);
}

static void
multi_failed(WKDownloadRef download, WKErrorRef error, WKDataRef, const void* info) {
  multi_t* m = 0;
  multi_seg_t* s = multi_seg_of(download, info, &m);
  char why[160], description[120];

  if(!s || s->state == MS_STOPPING) {  // (stopped at its end: its cancel's answer does the rest)
    return;
  }
  take_string(error ? WKErrorCopyLocalizedDescription(error) : 0, description, sizeof(description));
  snprintf(why, sizeof(why), "a connection failed %lld bytes into its piece: %s", s->got, description);
  m->failures++;
  if(!m->judged) {  // before the parts are known to be worth it: the download goes on alone, as it began
    s->state = MS_FREE;
    WKRelease(s->ref);
    s->ref = 0;
    multi_alone(m, why);
    return;
  }
  if(!m->trouble) {  // (the first of a row is told; the row's end is: multi_bytes())
    POC_LOG("download %d: %s (another takes the rest)", m->id, why);
  }
  multi_rest(m, s);
  multi_trouble(m);
  multi_pump(m);
  multi_check(m);
}

// A download the app asked for as a part (download_begin()): taken as one, with its own callbacks.
static bool
multi_claim(WKDownloadRef download) {
  static char url[sizeof(multis[0].url) + 64];
  multi_t* m = 0;

  url[0] = 0;
  WKURLRequestRef request = WKDownloadCopyRequest(download);
  if(request) {
    take_url(WKURLRequestCopyURL(request), url, sizeof(url));
    WKRelease(request);
  }
  multi_seg_t* s = multi_asked(url, &m);
  if(!s) {
    if(strstr(url, "#bps5range=")) {  // its set ended between the navigation and here: no download of the user's
      POC_LOG("download: a part nobody waits for any more: cancelled");
      WKDownloadCancel(download, 0, multi_cancel_ignored);
      return true;
    }
    return false;
  }
  s->ref = (WKDownloadRef)WKRetain(download);
  s->state = MS_RUNNING;
  s->fresh_ms = poc_now_ms();
  WKDownloadClientV0 client;
  memset(&client, 0, sizeof(client));
  client.base.version = 0;
  client.base.clientInfo = (const void*)(intptr_t)((m - multis) * MULTI_SEGS + (s - m->seg) + 1);
  client.decideDestinationWithResponse = multi_destination;
  client.didWriteData = multi_progress;
  client.didFinish = multi_finished;
  client.didFailWithError = multi_failed;
  WKDownloadSetClient(download, &client.base);
  multi_pump(m);  // the next connection, if one is wanted
  for(multi_t& other : multis) {  // and those of the other downloads that waited for this one to begin (multi_page_asked())
    if(other.id && &other != m) {
      multi_pump(&other);
    }
  }
  return true;
}

// The download's own bytes came (download_progress()). The first time its size is known: is it one for several
// parts? Then: its count is the pieces', and at the end of its own piece it stops. True if the count was taken here.
static bool
multi_progress_of(download_t* d, long long written, long long total) {
  multi_t* m = multi_of(d->id);

  if(!m && !d->multi_tried && total > 0) {
    d->multi_tried = true;
    WKPageRef page = d->ref ? WKDownloadGetOriginatingPage(d->ref) : 0;
    // Its rest can be asked for again (after a failure, or by others at once): 32 MiB or more of a web address
    // that answered 200, from a tab that is not private. In several parts: 200 MiB or more, with the settings'
    // "Transferências rápidas"; otherwise by its own connection, kept all the same ("solo").
    const bool can = total >= MULTI_SOLO_MIN && d->status == 200 && d->can_retry && !d->extension && !d->torrent
                     && url_is_web(d->final_url) && strlen(d->final_url) + 1 < sizeof(m->url) && page
                     && multi_page_fine(page) && access(MULTI_OFF_FILE, F_OK);
    const bool parts = can && total >= MULTI_MIN && settings.multipart && !multi_test_off;
    if(can) {
      for(multi_t& slot : multis) {
        if(!slot.id) {
          m = &slot;
          break;
        }
      }
    }
    if(m) {
      memset(m, 0, sizeof(*m));
      m->id = d->id;
      m->page = page;
      m->total = total;
      snprintf(m->url, sizeof(m->url), "%s", d->final_url);
      m->url[strcspn(m->url, "#")] = 0;
      m->limit = MULTI_WORKERS;
      m->segs = 1;  // all of it is the download's own until its speed alone is known
      m->seg[0].to = m->seg[0].asked_to = total;
      m->seg[0].main = true;
      m->seg[0].state = MS_FLOWING;
      m->began_ms = m->bytes_ms = m->seg[0].fresh_ms = poc_now_ms();
      m->began_bytes = written;
      if(!parts) {  // solo from the start: the file under its other name at once (see MULTI_ALONE_MS below)
        snprintf(m->file, sizeof(m->file), "%s.multi", d->partial);
        unlink(m->file);
        if(rename(d->partial, m->file)) {
          memset(m, 0, sizeof(*m));
          m = 0;
        } else {
          multi_map_path(m->file, m->map, sizeof(m->map));
          m->solo = m->judged = true;
          m->alone = 1;
          POC_LOG("download %d: %lld bytes by one connection; after a failure it goes on from where it was", d->id, total);
        }
      }
    }
  }
  if(!m) {
    return false;
  }
  multi_seg_t* own = &m->seg[0];
  const double now = poc_now_ms();
  m->own_written = written;
  if(!m->alone && now - m->began_ms >= MULTI_ALONE_MS) {  // its speed alone is known: now the others
    m->alone = (written - m->began_bytes) / ((now - m->began_ms) / 1e3);
    if(m->alone < 1) {
      m->alone = 1;
    }
    // The file under the name it has while in parts (the engine goes on writing into it; a cancelled
    // download's file is removed by the name the engine knows, which is no longer there).
    snprintf(m->file, sizeof(m->file), "%s.multi", d->partial);
    unlink(m->file);
    if(rename(d->partial, m->file)) {
      m->file[0] = 0;
      multi_abandon(m, "its file took no other name");
      return false;
    }
    multi_map_path(m->file, m->map, sizeof(m->map));
    // MULTI_WORKERS pieces of the same size, the first its own (what is left of it will be halved as others end).
    const long long each = (total / MULTI_WORKERS) & ~((1LL << 20) - 1);
    own->to = each;
    for(int k = 1; k < MULTI_WORKERS; k++) {
      multi_seg_t* s = &m->seg[m->segs++];
      memset(s, 0, sizeof(*s));
      s->from = k * each;
      s->to = k == MULTI_WORKERS - 1 ? total : (k + 1) * each;
    }
    POC_LOG("download %d: %lld bytes, %.2f MB/s alone: up to %d connections, pieces of %lld", d->id, total,
            m->alone / 1e6, MULTI_WORKERS, each);
    multi_pump(m);
  }
  if(own->main && own->state == MS_FLOWING) {
    const long long size = own->to - own->from;
    const long long before = own->got;
    own->got = written < size ? written : size;
    if(own->got > before) {
      own->fresh_ms = now;
      if(multi_bytes(m)) {
        multi_pump(m);
      }
    }
    multi_account(m);
    if(m->judged && written >= size && size < total) {  // its piece is in: it stops, the others bring the rest
      multi_stop(m, own);
    }
  }
  return true;
}

// The download itself failed while in parts. True: the others bring the rest (it stays in the list as it was).
static bool
multi_main_failed(download_t* d) {
  multi_t* m = multi_of(d->id);

  if(!m) {
    return false;
  }
  multi_seg_t* own = &m->seg[0];
  if(m->main_gone || own->state == MS_STOPPING || own->state == MS_DONE) {  // its own cancel (its piece's end, or dropped)
    return true;
  }
  if(!m->judged) {
    if(!m->file[0]) {  // in its first seconds: nothing is kept yet
      multi_abandon(m, "the download itself failed");
      return false;
    }
    m->judged = true;  // the others bring the rest, whatever they are worth
  }
  m->failures++;
  m->main_gone = true;
  POC_LOG("download %d: its own connection failed %lld bytes in: the others bring the rest", d->id, own->got);
  multi_rest(m, own);
  multi_trouble(m);
  multi_pump(m);
  multi_check(m);
  return true;
}

// A connection that brings nothing (the line went quiet under it: no error comes for a long time). It is told
// to stop; what it brought is in the file, and the rest of its piece is another's (multi_stopped()).
static void
multi_drop(multi_t* m, multi_seg_t* s) {
  if(!m->trouble) {
    POC_LOG("download %d: a connection brought nothing for %.0f s, %lld bytes into its piece: dropped", m->id,
            (poc_now_ms() - s->fresh_ms) / 1e3, s->got);
  }
  m->failures++;
  s->dropping = true;
  multi_stop(m, s);
  multi_trouble(m);
}

// A part asked for and not begun in eight seconds (its navigation was lost); and, five seconds after the others
// began to bring bytes, whether they are worth it.
static void
multi_tick(void) {
  const double now = poc_now_ms();
  static bool told;
  if(!told && now - app.start_ms > 7000) {  // once, with the bar there: downloads a session before left unfinished
    int kept = 0;
    told = true;
    for(const download_t& d : downloads) {
      kept += d.id && d.kept && d.state == DL_FAILED;
    }
    if(kept) {
      bar_notify(lang_en() ? (kept > 1 ? LM("Downloads left unfinished: go on with them in Downloads")
                                       : LM("A download was left unfinished: go on with it in Downloads"))
                           : (kept > 1 ? "Ficaram transferências por acabar: continua-as em Transferências"
                                       : "Ficou uma transferência por acabar: continua-a em Transferências"),
                 "arrow-download");
    }
  }
  // 2026-11-14 (found on the PC, tests-host/make-multi-sim.py, seed 516658): a download the user cancels is
  // "cancelled" when the engine answers its cancel (or tells of it as a failure). With neither (the network
  // process busy with something else, or gone) it stayed "running" in the list for as long as the browser was
  // open, with the user's cancel done. Ten seconds with no answer: cancelled all the same, as a part's is.
  for(download_t& d : downloads) {
    if(d.id && d.state == DL_RUNNING && d.cancelling && now - d.cancel_ms > 10000) {
      d.state = DL_CANCELLED;
      unlink(d.partial);
      downloads_changed = true;
      POC_LOG("download %d cancelled (its cancel was not answered)", d.id);
    }
  }
  for(multi_t& m : multis) {
    for(int i = 0; m.id && i < m.segs; i++) {
      if(m.seg[i].state == MS_ASKED && now - m.asked_ms > 8000) {
        m.seg[i].state = MS_FREE;
        m.failures++;
        if(!m.judged) {
          multi_alone(&m, "a part was not begun");
        } else {
          multi_trouble(&m);
          multi_pump(&m);
        }
        break;
      }
    }
    for(int i = 0; m.id && m.judged && i < m.segs; i++) {
      multi_seg_t* s = &m.seg[i];
      if((s->state == MS_RUNNING || s->state == MS_FLOWING)
         && now - s->fresh_ms > (m.solo && s->main ? MULTI_STALL_SOLO_MS : MULTI_STALL_MS)) {
        multi_drop(&m, s);
      } else if(s->state == MS_STOPPING && now - s->fresh_ms > 10000) {  // its cancel was never answered
        multi_stopped(&m, s);
      }
    }
    if(m.id && m.alone && now - m.pump_ms > 1000) {  // (a part not asked for because its tab was busy)
      m.pump_ms = now;
      multi_pump(&m);
    }
    if(m.id && m.file[0] && now - m.map_ms >= MULTI_MAP_MS) {  // for another session, if this one ends
      m.map_ms = now;
      multi_map_note(&m);
    }
    if(m.id && !m.judged && m.flow_ms && now - m.flow_ms >= MULTI_JUDGE_MS) {
      download_t* d = download_by_id(m.id);
      const double together = d ? (d->received - m.flow_bytes) / ((now - m.flow_ms) / 1e3) : 0;
      m.judged = true;
      if(together < m.alone * 1.3) {
        char why[120];
        snprintf(why, sizeof(why), "its parts take %.2f MB/s together, and it took %.2f alone", together / 1e6, m.alone / 1e6);
        multi_alone(&m, why);
      } else {
        POC_LOG("download %d: %.2f MB/s in parts (%.2f alone): they stay", m.id, together / 1e6, m.alone / 1e6);
        multi_pump(&m);
      }
    }
    if(m.id && m.hold_ms && now >= m.hold_ms) {  // after a refusal's wait: the next connection
      m.hold_ms = 0;
      multi_pump(&m);
    }
  }
}
#endif

#ifdef SHOW_DL_DROP
// make SHOW=1 SHOW_YT=1 SHOW_DL=1 SHOW_DL_DROP=1 (2026-11-14): what a download in parts does when the line fails
// under it, with a server of the test's own on this console (127.0.0.1:DROP_PORT: the browser's own navigation
// may reach it). It serves one file made by a formula (DROP_SIZE bytes: the byte at each place is known, so the
// file that arrives is checked against it), whole (200) or a range of it (206), DROP_RATE bytes a second for
// each connection (its thread sleeps between blocks). drop_mode: 0 serving; 1 the line is CUT: every connection
// ends with a reset where it is, and so do new ones; 2 the line is QUIET: the connections stay and bring
// nothing, new ones get no answer.
#define DROP_PORT 8111
#define DROP_SIZE (640LL << 20)
static long long drop_rate = 3LL << 20;
static bool drop_one;  // SHOW_DL_DROP_SOLO: a range is refused (429) while another connection is served, as OVH's server does
static volatile int drop_mode;
static volatile int drop_connections, drop_ranges, drop_cut, drop_active, drop_refused;

static void
drop_fill(unsigned char* out, long long at, size_t n) {
  for(size_t i = 0; i < n; i++, at++) {
    const unsigned long long w = (unsigned long long)(at >> 3) * 0x9e3779b97f4a7c15ull;
    out[i] = (unsigned char)((w ^ (w >> 29)) >> ((at & 7) * 8));
  }
}

static void*
drop_serve(void* arg) {
  const int c = (int)(intptr_t)arg;
  const size_t block = 256 << 10;
  unsigned char* buffer = (unsigned char*)malloc(block);
  char head[2048], answer[512];
  size_t have = 0;
  long long from = 0, last = DROP_SIZE - 1;
  bool range = false, cut = false;

  head[0] = 0;
  while(buffer && have + 1 < sizeof(head) && !strstr(head, "\r\n\r\n")) {
    const ssize_t r = recv(c, head + have, sizeof(head) - 1 - have, 0);
    if(r <= 0) {
      break;
    }
    have += (size_t)r;
    head[have] = 0;
  }
  if(buffer && strstr(head, "\r\n\r\n") && !strncmp(head, "GET /bps5-test.bin", 18)) {
    const char* r = strcasestr(head, "\r\nrange: bytes=");
    if(r && sscanf(r + 15, "%lld-%lld", &from, &last) >= 1) {
      range = true;
      __sync_fetch_and_add(&drop_ranges, 1);
    }
    if(last > DROP_SIZE - 1) {
      last = DROP_SIZE - 1;
    }
    while(drop_mode == 2) {  // quiet: no answer for now
      usleep(200000);
    }
    cut = drop_mode == 1;
    if(!cut && drop_one && range && drop_active > 0) {
      static const char busy[] = "HTTP/1.1 429 Too Many Requests\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
      send(c, busy, sizeof(busy) - 1, 0);
      __sync_fetch_and_add(&drop_refused, 1);
    } else if(!cut && from >= 0 && from <= last) {
      __sync_fetch_and_add(&drop_active, 1);
      int n = snprintf(answer, sizeof(answer),
                       "HTTP/1.1 %s\r\nContent-Type: application/octet-stream\r\n"
                       "Content-Disposition: attachment; filename=\"bps5-test.bin\"\r\nAccept-Ranges: bytes\r\n"
                       "Content-Length: %lld\r\n", range ? "206 Partial Content" : "200 OK", last - from + 1);
      if(range) {
        n += snprintf(answer + n, sizeof(answer) - (size_t)n, "Content-Range: bytes %lld-%lld/%lld\r\n", from, last,
                      (long long)DROP_SIZE);
      }
      n += snprintf(answer + n, sizeof(answer) - (size_t)n, "Connection: close\r\n\r\n");
      bool fine = send(c, answer, (size_t)n, 0) == n;
      for(long long at = from; fine && at <= last;) {
        if(drop_mode == 1) {
          cut = true;
          break;
        }
        if(drop_mode == 2) {
          usleep(200000);
          continue;
        }
        const size_t want = last - at + 1 < (long long)block ? (size_t)(last - at + 1) : block;
        drop_fill(buffer, at, want);
        for(size_t sent = 0; fine && sent < want;) {
          const ssize_t w = send(c, buffer + sent, want - sent, 0);
          if(w <= 0) {
            fine = false;
          } else {
            sent += (size_t)w;
          }
        }
        at += (long long)want;
        usleep((useconds_t)(1000000LL * (long long)block / drop_rate));
      }
      __sync_fetch_and_sub(&drop_active, 1);
    }
  }
  if(cut) {  // the line is cut: the connection ends with a reset, not as one the server closed
    const struct linger at_once = {1, 0};
    setsockopt(c, SOL_SOCKET, SO_LINGER, &at_once, sizeof(at_once));
    __sync_fetch_and_add(&drop_cut, 1);
  }
  free(buffer);
  close(c);
  return 0;
}

static void*
drop_server_thread(void* arg) {
  const int s = (int)(intptr_t)arg;
  for(;;) {
    const int c = accept(s, 0, 0);  // sleeps until a connection comes
    if(c < 0) {
      if(errno == EINTR) {
        continue;
      }
      break;
    }
    const int on = 1;
    pthread_t thread;
    setsockopt(c, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
    __sync_fetch_and_add(&drop_connections, 1);
    if(pthread_create(&thread, 0, drop_serve, (void*)(intptr_t)c)) {
      close(c);
    } else {
      pthread_detach(thread);
    }
  }
  return 0;
}

static bool
drop_server(void) {
  const int s = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in at = {};
  const int on = 1;
  pthread_t thread;

  if(s < 0) {
    return false;
  }
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
  at.sin_family = AF_INET;
  at.sin_port = htons(DROP_PORT);
  at.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if(bind(s, (struct sockaddr*)&at, sizeof(at)) || listen(s, 32)
     || pthread_create(&thread, 0, drop_server_thread, (void*)(intptr_t)s)) {
    close(s);
    return false;
  }
  pthread_detach(thread);
  return true;
}

// Is a file what the server makes, byte for byte? Its bytes read; *wrong: where the first other byte is, or -1.
static long long
drop_check(const char* path, long long* wrong) {
  static unsigned char got[1 << 20], want[1 << 20];
  const int in = open(path, O_RDONLY);
  long long at = 0;

  *wrong = -1;
  for(ssize_t n; in >= 0 && *wrong < 0 && (n = read(in, got, sizeof(got))) > 0; at += n) {
    drop_fill(want, at, (size_t)n);
    if(memcmp(got, want, (size_t)n)) {
      for(ssize_t k = 0; k < n && *wrong < 0; k++) {
        if(got[k] != want[k]) {
          *wrong = at + k;
        }
      }
    }
    usleep(300);
  }
  if(in >= 0) {
    close(in);
  }
  return at;
}

// The test's own files in the downloads' folder (bps5-test*.bin): none stays, whatever went wrong in a run.
static void
drop_clean(void) {
  if(DIR* dir = opendir(settings.downloads_dir)) {
    for(struct dirent* e; (e = readdir(dir));) {
      const size_t n = strlen(e->d_name);
      if(!strncmp(e->d_name, "bps5-test", 9) && n > 4 && !strcmp(e->d_name + n - 4, ".bin")) {
        char path[900];
        snprintf(path, sizeof(path), "%s/%s", settings.downloads_dir, e->d_name);
        POC_LOG("show: dl: drop: a test file removed: %s (%s)", path, unlink(path) ? "FAILED" : "gone");
      }
    }
    closedir(dir);
  }
}
#endif

static WKStringRef
download_destination(WKDownloadRef, WKURLResponseRef response, WKStringRef suggested,
                     const void* info) {
  download_t* d = download_by_id((int)(intptr_t)info);
  char text[1024];
  char mime[128];

  if(!d) {
    return string("");
  }
  text[0] = 0;
  if(suggested) {
    WKStringGetUTF8CString(suggested, text, sizeof(text));
  }
  clean_name(text, d->name, sizeof(d->name));
  if(!d->name[0]) {
    snprintf(d->name, sizeof(d->name), "transferencia-%d", d->id);
  }
  take_string(response ? WKURLResponseCopyMIMEType(response) : 0, mime, sizeof(mime));
  take_url(response ? WKURLResponseCopyURL(response) : 0, text, sizeof(text));
  url_host(text, d->host, sizeof(d->host));
  d->status = response ? (int)WKURLResponseHTTPStatusCode(response) : 0;
  snprintf(d->final_url, sizeof(d->final_url), "%s", text);
#ifdef APP_RN
  snprintf(download_last_url[0], sizeof(download_last_url[0]), "%s", d->url);
  snprintf(download_last_url[1], sizeof(download_last_url[1]), "%s", text);
  bar_update(0);  // (the bar said the address: the page it stays on is said now)
#endif
  uint32_t length = response ? WKURLResponseGetExpectedContentLength(response) : 0;
  d->total = length && length != 0xffffffffu ? length : 0;
  snprintf(d->kind, sizeof(d->kind), "%s", download_kind(d->name, mime));
  d->extension = !strcmp(d->kind, "ext");
#ifdef APP_RN
  d->view = pdf_wanted[0] && poc_now_ms() - pdf_wanted_ms < 15000 && !strncmp(text, pdf_wanted, strlen(pdf_wanted));
  pdf_wanted[0] = 0;
#endif
#ifdef REMOTE_UI
  d->torrent = !strcmp(d->kind, "torrent");
#endif
  mkdirs(PARTIAL_DIR);
  snprintf(d->partial, sizeof(d->partial), PARTIAL_DIR "/%d-%ld.part", d->id,
           (long)time(0));
  POC_LOG("download %d: a %s file (%s), %lld bytes, from %s", d->id, d->kind, mime,
          d->total, d->host);
#ifdef APP_RN
  // A cheat file the start page asked for (cheat_ask()): its name and its folder are the ones made there.
  if(cheat_wanted(text) && (!d->total || d->total <= CHEAT_MOST)) {
    d->cheat = true;
    d->extension = d->torrent = d->view = false;
    snprintf(d->name, sizeof(d->name), "%s", cheat_want.name);
    snprintf(d->dir, sizeof(d->dir), "%s", cheat_want.dir);
    d->chosen = true;
    cheat_want.url[0] = 0;
    POC_LOG("download %d: a cheat file for etaHEN, into %s", d->id, d->dir);
  }
  if(d->cheat) {
  } else
#endif
  if(d->extension || d->torrent || !settings.ask_downloads) {
    snprintf(d->dir, sizeof(d->dir), "%s", download_dir(d->kind));
    d->chosen = true;
  } else if(!app.dialog_open) {
    dialog_for_download(d);
  }
#ifdef APP_RN
  // 2026-11-12: with its folder known and on another drive than PARTIAL_DIR (the downloads' folder is: a file
  // of 1 GiB took 12 s more at its end, copied from one to the other, and twice its room meanwhile), the file
  // is written in a hidden folder of that folder from the start, and ends with a rename.
  if(d->chosen && !d->extension && !d->torrent) {
    struct stat here, there;
    char near[560];
    if(path_allowed(d->dir) && !mkdirs(d->dir) && !stat(d->dir, &there) && !stat(PARTIAL_DIR, &here)
       && there.st_dev != here.st_dev) {
      snprintf(near, sizeof(near), "%s/" PARTIAL_NEAR, d->dir);
      if(!mkdirs(near)) {
        snprintf(d->partial, sizeof(d->partial), "%s/%d-%ld.part", near, d->id, (long)time(0));
      }
    }
  }
#endif
  downloads_changed = true;
  return string(d->partial);  // WebKit takes it
}


static void
download_progress(WKDownloadRef, long long now, long long written, long long expected,
                  const void* info) {
  download_t* d = download_by_id((int)(intptr_t)info);

#ifdef APP_RN
  if(d && d->kept) {  // paused (download_pause()): its own connection's last bytes are not the download's count
    return;
  }
#endif
  if(d) {
    // 2026-11-12: the engine's curl port told no totals (0 written so far, 0 expected, with every block):
    // the list showed "0 %" and no speed until a download was done. The engine tells them since that day
    // (webkit/build-webkit.sh, NetworkDataTaskCurl); with one that does not, they are counted here.
    if(written <= 0 && now > 0) {
      d->own += now;
      written = d->own;
    }
    const bool told = expected > 0;  // by the engine itself, in 64 bits
    if(expected <= 0) {
      expected = d->total;
    }
    if(expected > 0) {
      d->total = expected;
    }
#ifdef APP_RN
    // (2026-11-14: a size the engine does not tell is the response's, which the C API gives in 32 bits
    // (download_destination()): of a file of 4 GiB or more it is NOT its size, and pieces cut by it would end the
    // download short. The engines of this app tell it; with one that does not, no pieces.)
    if(multi_progress_of(d, written, told ? expected : 0)) {  // in several parts: the count is theirs
      return;
    }
#endif
    d->received = written;
    dl_rate_add(&d->rate, written, poc_now_ms());
    downloads_changed = true;
  }
}


static void
download_finished(WKDownloadRef, const void* info) {
  download_t* d = download_by_id((int)(intptr_t)info);

  if(!d || d->state != DL_RUNNING) {
    return;
  }
#ifdef APP_RN
  if(d->cancelling) {  // the user cancelled it as its last bytes came: cancelled is what was asked for
    d->state = DL_CANCELLED;
    unlink(d->partial);
    downloads_changed = true;
    POC_LOG("download %d cancelled (as it ended)", d->id);
    return;
  }
  if(multi_t* m = multi_of(d->id)) {  // it came whole by itself (its own connection was never stopped)
    if(m->solo && !m->main_gone && m->file[0]) {  // as a download by one connection does: its own name again
      rename(m->file, d->partial);
      m->file[0] = 0;
      multi_end(m, false);
    } else {
      m->main_gone = false;
      multi_abandon(m, "the download itself had it all");  // (the file has its own name again)
    }
    d->received = d->total;
  }
#endif
  download_all_in(d);
}

// All of a download is in its partial file: what happens to it now.
static void
download_all_in(download_t* d) {
  d->written = true;
  if(d->total < d->received) {
    d->total = d->received;
  }
  downloads_changed = true;
  if(d->extension) {
    extension_offer(d);
#ifdef REMOTE_UI
  } else if(d->torrent) {
    torrent_offer(d);
#endif
  } else if(d->chosen) {
    download_move(d);
  } else {
    d->state = DL_WAITING;  // the dialog is still asking
    POC_LOG("download %d: all in, waiting for its folder", d->id);
  }
}


static void dialog_hide(void);
static int dialog_download;  // the download the dialog asks about, or 0

static void
download_failed(WKDownloadRef, WKErrorRef error, WKDataRef, const void* info) {
  download_t* d = download_by_id((int)(intptr_t)info);
  char description[256];

  if(!d || (d->state != DL_RUNNING && d->state != DL_WAITING)) {
    return;
  }
#ifdef APP_RN
  if(multi_main_failed(d)) {  // in several parts: its own cancel at its piece's end, or the others bring the rest
    return;
  }
#endif
  take_string(error ? WKErrorCopyLocalizedDescription(error) : 0, description,
              sizeof(description));
  d->state = d->cancelling ? DL_CANCELLED : DL_FAILED;
  unlink(d->partial);
  downloads_changed = true;
  POC_LOG("download %d %s: %s", d->id, d->cancelling ? "cancelled" : "FAILED",
          description);
  if(dialog_download == d->id) {
    dialog_hide();
  }
}


static void
download_cancelled(WKDataRef, const void* info) {
  download_t* d = download_by_id((int)(intptr_t)info);

  if(d && d->state == DL_RUNNING) {
    d->state = DL_CANCELLED;
    unlink(d->partial);
    downloads_changed = true;
    POC_LOG("download %d cancelled", d->id);
  }
}


static void
download_cancel(download_t* d) {
#ifdef APP_RN
  if(d->kept && d->state == DL_FAILED) {  // one a session before left: what there is of it goes
    multi_kept_forget(d);
    return;
  }
  if(multi_t* m = multi_of(d->id)) {  // in several parts: they go too, and the file
    const bool stopped = m->main_gone;
    multi_end(m, false);
    if(stopped && d->state == DL_RUNNING) {  // its own download is cancelled already
      d->state = DL_CANCELLED;
      unlink(d->partial);
      downloads_changed = true;
      POC_LOG("download %d cancelled (with its parts)", d->id);
      return;
    }
  }
#endif
  if(d->state == DL_RUNNING && !d->written && d->ref) {
    d->cancelling = true;
    d->cancel_ms = poc_now_ms();
    WKDownloadCancel(d->ref, (const void*)(intptr_t)d->id, download_cancelled);
  } else if(d->state == DL_RUNNING || d->state == DL_WAITING) {
    unlink(d->partial);
    d->state = DL_CANCELLED;
    POC_LOG("download %d cancelled", d->id);
  }
  downloads_changed = true;
}


#ifdef APP_RN
// A map read: the set's facts and missing ranges into m; the download's own into d (if given). False: not one
// that can be gone on with (its file is gone, or the map is not whole).
static bool
multi_map_load(const char* map, download_t* d, multi_t* m) {
  size_t length = 0;
  char* text = read_whole(map, 64 << 10, &length);
  bool ended = false, fine = true;
  long long missing = 0;
  struct stat st;
  // No file is this large (a petabyte), and with every number under it the sums below cannot overflow: a broken
  // map with huge ranges did (2026-11-14, found on the PC: browser-rn/tests-host, the sanitizers).
  const long long most = 1LL << 50;

  if(!text) {
    return false;
  }
  memset(m, 0, sizeof(*m));
  for(char* line = text; line && *line;) {
    char* next = strchr(line, '\n');
    if(next) {
      *next++ = 0;
    }
    char* value = strchr(line, ' ');
    if(value) {
      *value++ = 0;
    } else {
      value = line + strlen(line);
    }
    if(!strcmp(line, "total")) {
      m->total = atoll(value);
    } else if(!strcmp(line, "file")) {
      snprintf(m->file, sizeof(m->file), "%s", value);
    } else if(!strcmp(line, "url")) {
      snprintf(m->url, sizeof(m->url), "%s", value);
    } else if(!strcmp(line, "miss")) {
      multi_seg_t* s = &m->seg[m->segs < MULTI_SEGS ? m->segs : 0];
      if(m->segs < MULTI_SEGS && sscanf(value, "%lld %lld", &s->from, &s->to) == 2 && s->from >= 0 && s->from < s->to
         && s->to <= most) {
        s->asked_to = s->to;
        missing += s->to - s->from;
        m->segs++;
      } else {
        fine = false;
      }
    } else if(!strcmp(line, "end")) {
      ended = true;
    } else if(d && !strcmp(line, "name")) {
      snprintf(d->name, sizeof(d->name), "%s", value);
    } else if(d && !strcmp(line, "kind")) {
      snprintf(d->kind, sizeof(d->kind), "%s", value);
    } else if(d && !strcmp(line, "dir")) {
      snprintf(d->dir, sizeof(d->dir), "%s", value);
    } else if(d && !strcmp(line, "host")) {
      snprintf(d->host, sizeof(d->host), "%s", value);
    } else if(d && !strcmp(line, "ask")) {
      snprintf(d->url, sizeof(d->url), "%s", value);
    }
    line = next;
  }
  free(text);
  for(int i = 0; i < m->segs; i++) {
    fine = fine && m->seg[i].to <= m->total;
  }
  // Its file: a partial of this browser's ("...part.multi" in PARTIAL_DIR or in a downloads folder's hidden one).
  fine = fine && ended && m->total > 0 && m->total <= most && missing <= m->total && url_is_web(m->url)
         && name_ends_with(m->file, ".part.multi")
         && (!strncmp(m->file, PARTIAL_DIR "/", sizeof(PARTIAL_DIR)) || strstr(m->file, "/" PARTIAL_NEAR "/"))
         && !strstr(m->file, "/../") && !stat(m->file, &st) && S_ISREG(st.st_mode);
  if(!fine) {
    return false;
  }
  m->banked = m->total - missing;
  snprintf(m->map, sizeof(m->map), "%s", map);
  if(d) {
    d->total = m->total;
    d->received = m->banked;
    snprintf(d->partial, sizeof(d->partial), "%s", m->file);
    d->partial[strlen(d->partial) - 6] = 0;  // (without its ".multi")
    snprintf(d->final_url, sizeof(d->final_url), "%s", m->url);
  }
  return true;
}

// At the start (partial_clean()): the downloads a session before left unfinished, each with its map and its
// file, are lines of the list ("Interrompida"); their files are the ones partial_clean_in() leaves.
static char multi_kept_files[4][640];

static bool
multi_kept_file(const char* path) {
  for(const char* kept : multi_kept_files) {
    if(kept[0] && !strcmp(kept, path)) {
      return true;
    }
  }
  return false;
}

static void
multi_kept_scan(void) {
  static multi_t probe;
  DIR* dir = opendir(PARTIAL_DIR);
  int kept = 0;

  memset(multi_kept_files, 0, sizeof(multi_kept_files));
  if(!dir) {
    return;
  }
  for(struct dirent* e; (e = readdir(dir));) {
    const size_t n = strlen(e->d_name);
    char map[700];
    struct stat st;
    download_t* d = 0;
    snprintf(map, sizeof(map), PARTIAL_DIR "/%s", e->d_name);
    if(n > 8 && (!strcmp(e->d_name + n - 8, ".map.tmp") || !strcmp(e->d_name + n - 8, ".map.now"))) {
      unlink(map);
      continue;
    }
    if(n < 5 || strcmp(e->d_name + n - 4, ".map")) {
      continue;
    }
    for(download_t& slot : downloads) {
      if(!slot.id && !d) {
        d = &slot;
      }
    }
    const bool recent = !stat(map, &st) && time(0) - st.st_mtime < MULTI_KEEP_DAYS * 86400;
    if(d) {
      memset(d, 0, sizeof(*d));
    }
    if(!d || kept >= 4 || !recent || !multi_map_load(map, d, &probe)) {  // nothing to go on with: its file goes too (partial_clean_in())
      if(d) {
        memset(d, 0, sizeof(*d));
      }
      unlink(map);
      continue;
    }
    d->id = ++download_ids;
    d->state = DL_FAILED;
    d->kept = d->can_retry = d->multi_tried = true;
    d->status = 200;
    d->chosen = d->dir[0] != 0;
    if(!d->name[0]) {
      snprintf(d->name, sizeof(d->name), "transferencia-%d", d->id);
    }
    if(!d->kind[0]) {
      snprintf(d->kind, sizeof(d->kind), "file");
    }
    snprintf(multi_kept_files[kept++], sizeof(multi_kept_files[0]), "%s", probe.file);
    POC_LOG("download %d: left unfinished by a session before: %lld of %lld bytes are in (%s)", d->id, d->received, d->total,
            d->host);
  }
  closedir(dir);
  memset(&probe, 0, sizeof(probe));
}

// "Continuar" on such a download: its missing ranges are asked for as parts, by the tab shown. True: taken (or
// said why not); false: there is nothing to go on with, and it is asked for from the start.
static bool
multi_resume(download_t* d) {
  char map[700];
  multi_t* m = 0;
  WKPageRef shown = page(PAGE);

  if(!multi_page_fine(shown)) {
    bar_notify(lang_en() ? LM("Show a tab that is not private to go on with this download")
                         : "Mostra um separador que não seja privado para continuar esta transferência", "dismiss");
    return true;
  }
  for(multi_t& slot : multis) {
    if(!slot.id && !m) {
      m = &slot;
    }
  }
  if(!m) {
    bar_notify(lang_en() ? LM("Too many downloads at once: try again when one ends")
                         : "Demasiadas transferências ao mesmo tempo: tenta quando uma acabar", "dismiss");
    return true;
  }
  multi_map_path(d->partial, map, sizeof(map));
  if(!multi_map_load(map, 0, m)) {
    memset(m, 0, sizeof(*m));
    multi_kept_forget(d);
    d->state = DL_FAILED;
    return false;
  }
  m->id = d->id;
  m->page = shown;
  m->main_gone = m->judged = true;  // no connection of its own: every range is a part's
  m->alone = 1;
  m->limit = MULTI_WORKERS;
  m->solo = !(m->total >= MULTI_MIN && settings.multipart && !multi_test_off);
  m->began_ms = m->bytes_ms = poc_now_ms();
  if(!d->chosen) {
    snprintf(d->dir, sizeof(d->dir), "%s", download_dir(d->kind));
    d->chosen = true;
  }
  d->kept = d->written = d->cancelling = false;
  d->state = DL_RUNNING;
  d->received = m->banked;
  memset(&d->rate, 0, sizeof(d->rate));
  last_gesture_ms = poc_now_ms();
  POC_LOG("download %d: goes on from where it was: %lld of %lld bytes are in, %d range(s) missing, %s", d->id, m->banked,
          m->total, m->segs, m->solo ? "by one connection" : "in parts");
  bar_notify(lang_en() ? LM("Going on with the download") : "A continuar a transferência", "arrow-download");
  downloads_changed = true;
  multi_pump(m);
  multi_check(m);  // (with nothing missing: it is all in)
  return true;
}

// "Pausar" (2026-11-14) on a download whose set has its file: what the file has is written down, its
// connections end, and it is a download left unfinished, as one of a session before: "Continuar" goes on with
// it (multi_resume()), in this session or another.
static void
download_pause(download_t* d) {
  multi_t* m = multi_of(d->id);
  char* text = m && d->state == DL_RUNNING && !d->written && m->file[0] && m->map[0] ? multi_map_text(m, d) : 0;
  struct stat st;

  if(!text) {
    return;
  }
  const int slot = (int)(m - multis);
  multi_gen[slot]++;  // (a map on its way to the disk, older, does not take this one's place)
  multi_map_put(m->map, text, slot, multi_gen[slot], ".now");
  free(text);
  if(stat(m->map, &st)) {
    POC_LOG("download %d: not paused: its map could not be written", d->id);
    return;
  }
  POC_LOG("download %d: paused with %lld of %lld bytes in", d->id, d->received, m->total);
  if(!m->main_gone && d->ref) {  // its own connection (the others: multi_end())
    WKDownloadCancel(d->ref, 0, multi_cancel_ignored);
  }
  m->map[0] = 0;  // (multi_end() leaves the map, and the file)
  multi_end(m, true);
  d->state = DL_FAILED;
  d->kept = true;
  d->cancelling = false;
  downloads_changed = true;
}

// The browser is closing (stop()): what every unfinished download's file has now is written down, for the
// next session to go on from.
static void
multi_keep_all(void) {
  if(multi_test_fall) {
    POC_LOG("downloads: (the test's) closed as a fall would: nothing written down now");
    return;
  }
  for(multi_t& m : multis) {
    const download_t* d = m.id ? download_by_id(m.id) : 0;
    char* text = d && d->state == DL_RUNNING && m.file[0] && m.map[0] ? multi_map_text(&m, d) : 0;
    if(text) {
      const int slot = (int)(&m - multis);
      multi_gen[slot]++;  // (one on its way to the disk, older, does not take this one's place)
      multi_map_put(m.map, text, slot, multi_gen[slot], ".now");
      POC_LOG("download %d: kept for the next time: %lld of %lld bytes are in", d->id, d->received, m.total);
      free(text);
    }
  }
}
#endif

// Asks for a download that failed or was cancelled once more (Definições,
// Transferências, "Voltar a tentar"): the tab shown loads its address
// again, and decidePageNavigation makes that a download. From the start:
// WebKit's curl downloads cannot go on from where they stopped. A link
// that has run out comes back as a page, and the tab shows it.
static char download_retry_url[4096];

static void
download_retry(download_t* d) {
  if(!d || (d->state != DL_FAILED && d->state != DL_CANCELLED) || !d->can_retry) {
    return;
  }
#ifdef APP_RN
  if(d->kept && multi_resume(d)) {  // left by a session before: from where it was
    return;
  }
#endif
#ifdef APP_RN
  // (2026-11-14) "Try again" is the app's own load: the kind that may connect to this console (site_may_open()).
  // A download a site began on the console's own services was refused by the engine and stayed in the list as
  // failed, with this button. Only one that had its answer from there (the user's own, from the service's page).
  if(url_this_console(d->url) && d->status != 200) {
    POC_LOG("download %d: not tried again: a site began it on this console's own services", d->id);
    bar_notify(lang_en() ? LM("A site tried to open a service of this console: not opened")
                         : "Um site tentou abrir um serviço desta consola: não foi aberto", "shield-checkmark");
    return;
  }
#endif
  snprintf(download_retry_url, sizeof(download_retry_url), "%s", d->url);
  POC_LOG("download %d: tried again (%s)", d->id, d->host[0] ? d->host : "its address");
  if(d->ref) {  // its line goes; the new download takes a slot
    WKRelease(d->ref);
  }
  memset(d, 0, sizeof(*d));
  downloads_changed = true;
  last_gesture_ms = poc_now_ms();
  bar_notify("A transferir de novo", "arrow-download");
  load_url(download_retry_url);
}


// A path's file:// address, its bytes but the plain ones as %XX.
static void
file_url(const char* path, char* url, size_t size) {
  size_t n = (size_t)snprintf(url, size, "file://");
  for(const unsigned char* p = (const unsigned char*)path; *p && n + 4 < size; p++) {
    if(isalnum(*p) || strchr("/._~-", *p)) {
      url[n++] = (char)*p;
    } else {
      n += (size_t)snprintf(url + n, size - n, "%%%02X", *p);
    }
  }
  url[n] = 0;
}

#ifdef APP_RN
/* BrowserPS5 RN (2026-11-11): PDFs in the browser. The engine shows none by
 * itself (off Apple's ports WebKit has no PDF view): a PDF a page goes to is
 * a download, as before, and once it is in it opens in PDF.js (Mozilla's
 * reader, Apache 2.0, the copy WebKit itself carries: Source/ThirdParty/pdfjs,
 * installed as files in PDF_DIR by browser-rn/pack-pdfjs-rn.sh), in the tab
 * that asked for it; a finished one opens from the downloads' list too
 * ("Abrir"). The reader is a page of the browser's own (file://) reading a
 * file of the downloads: no site sees it, and nothing is fetched again. With
 * no reader installed everything stays as it was. */
#define PDF_DIR "/user/homebrew/browserps5-rn/pdfjs"
#define PDF_VIEWER PDF_DIR "/web/viewer.html"

static bool
pdf_reader(void) {
  return !access(PDF_VIEWER, R_OK);
}

static bool
pdf_name(const char* name) {
  return name_ends_with(name, ".pdf");
}

static void file_url(const char* path, char* url, size_t size);

/* The reader is served by the app itself, to itself: the engine refuses a
 * module script that a file:// page asks for (PDF.js is modules), whatever
 * the file is called (2026-11-11 20:35). A small HTTP server on 127.0.0.1
 * only (port 8098, or one of the four after it), one thread that sleeps in
 * accept(): /pdfjs/... are the reader's files, /doc?p=<path> is a PDF of the
 * downloads (path_allowed(), ".pdf"), whole or a range of it. No site's page
 * reaches it: the engine lets only the user's own navigation and the page so
 * opened connect to the console (webkit/security, BROWSERPS5_NO_LOCAL). */
static int pdf_port;

static const char*
pdf_type(const char* path) {
  static const char* const types[][2] = {
    {".html", "text/html; charset=utf-8"}, {".js", "text/javascript"}, {".mjs", "text/javascript"},
    {".css", "text/css"}, {".json", "application/json"}, {".wasm", "application/wasm"},
    {".svg", "image/svg+xml"}, {".png", "image/png"}, {".gif", "image/gif"}, {".pdf", "application/pdf"},
    {".ftl", "text/plain; charset=utf-8"},
  };
  for(const auto& t : types) {
    if(name_ends_with(path, t[0])) {
      return t[1];
    }
  }
  return "application/octet-stream";
}

static void
pdf_serve(int c) {
  static char request[4096];
  static char path[2048];
  static char buffer[65536];
  struct timeval wait = {3, 0};
  size_t got = 0;

  setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &wait, sizeof(wait));
  setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, &wait, sizeof(wait));
  while(got < sizeof(request) - 1) {
    const ssize_t n = recv(c, request + got, sizeof(request) - 1 - got, 0);
    if(n <= 0) {
      return;
    }
    got += (size_t)n;
    request[got] = 0;
    if(strstr(request, "\r\n\r\n")) {
      break;
    }
  }
  char* target = !strncmp(request, "GET ", 4) ? request + 4 : !strncmp(request, "HEAD ", 5) ? request + 5 : 0;
  const bool head = target == request + 5;
  char* end = target ? strchr(target, ' ') : 0;
  long long from = 0, to = -1;
  const char* range = strcasestr(request, "\r\nRange: bytes=");
  if(range) {
    sscanf(range + 15, "%lld-%lld", &from, &to);
  }
  path[0] = 0;
  if(end) {
    *end = 0;
    if(!strncmp(target, "/pdfjs/", 7)) {
      target[strcspn(target, "?#")] = 0;
      if(!strstr(target, "..")) {
        snprintf(path, sizeof(path), "%s/%s", PDF_DIR, target + 7);
      }
    } else if(!strncmp(target, "/doc?p=", 7)) {
      address_decode(target + 7, path, sizeof(path));
      if(!pdf_name(path) || strstr(path, "..") || !path_allowed(path)) {
        path[0] = 0;
      }
    }
  }
  struct stat st;
  const int f = path[0] ? open(path, O_RDONLY) : -1;
  if(f < 0 || fstat(f, &st) || !S_ISREG(st.st_mode)) {
    static const char none[] = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    (void)!send(c, none, sizeof(none) - 1, 0);
    if(f >= 0) {
      close(f);
    }
    return;
  }
  const long long size = st.st_size;
  const bool part = range && from >= 0 && from < size;
  if(!part) {
    from = 0;
    to = size - 1;
  } else if(to < 0 || to >= size) {
    to = size - 1;
  }
  int n = snprintf(buffer, sizeof(buffer),
                   "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %lld\r\nAccept-Ranges: bytes\r\n"
                   "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n",
                   part ? "206 Partial Content" : "200 OK", pdf_type(path), to - from + 1);
  if(part) {
    n += snprintf(buffer + n, sizeof(buffer) - (size_t)n, "Content-Range: bytes %lld-%lld/%lld\r\n", from, to, size);
  }
  n += snprintf(buffer + n, sizeof(buffer) - (size_t)n, "\r\n");
  if(send(c, buffer, (size_t)n, 0) == n && !head) {
    long long at = from;
    while(at <= to) {
      const long long want = to - at + 1 < (long long)sizeof(buffer) ? to - at + 1 : (long long)sizeof(buffer);
      const ssize_t r = pread(f, buffer, (size_t)want, at);
      if(r <= 0) {
        break;
      }
      ssize_t sent = 0;
      while(sent < r) {
        const ssize_t w = send(c, buffer + sent, (size_t)(r - sent), 0);
        if(w <= 0) {
          close(f);
          return;
        }
        sent += w;
      }
      at += r;
    }
  }
  close(f);
}

static void*
pdf_server_thread(void* arg) {
  const int s = (int)(intptr_t)arg;
  for(;;) {
    const int c = accept(s, 0, 0);  // sleeps until the reader asks
    if(c < 0) {
      if(errno == EINTR) {
        continue;
      }
      break;
    }
    const int on = 1;
    setsockopt(c, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
    pdf_serve(c);
    close(c);
  }
  return 0;
}

// The server, started once (when a reader is installed). Its port, or 0.
static int
pdf_server(void) {
  if(pdf_port || !pdf_reader()) {
    return pdf_port;
  }
  const int s = socket(AF_INET, SOCK_STREAM, 0);
  if(s < 0) {
    return 0;
  }
  const int on = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
  for(int port = 8098; port < 8103 && !pdf_port; port++) {
    struct sockaddr_in at = {};
    at.sin_family = AF_INET;
    at.sin_port = htons((uint16_t)port);
    at.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if(!bind(s, (struct sockaddr*)&at, sizeof(at)) && !listen(s, 16)) {
      pdf_port = port;
    }
  }
  pthread_t thread;
  if(!pdf_port || pthread_create(&thread, 0, pdf_server_thread, (void*)(intptr_t)s)) {
    POC_LOG("pdf: the reader's server did not start (errno %d)", errno);
    close(s);
    pdf_port = 0;
    return 0;
  }
  pthread_detach(thread);
  POC_LOG("pdf: the reader is served on 127.0.0.1:%d", pdf_port);
  return pdf_port;
}

// The reader's address for a file: viewer.html?file=/doc?p=<path>, each a query's value.
static void
pdf_open_path(const char* path) {
  static char doc[3 * 1024 + 16];
  static char url[sizeof(doc) * 3 + 96];
  static const char keep[] = "._~-/";

  if(!pdf_server()) {
    bar_notify(lang_en() ? LM("The PDF reader could not start") : "O leitor de PDF não arrancou", "dismiss");
    return;
  }
  size_t n = (size_t)snprintf(doc, sizeof(doc), "/doc?p=");
  for(const unsigned char* c = (const unsigned char*)path; *c && n + 4 < sizeof(doc); c++) {
    if(isalnum(*c) || strchr(keep, *c)) {
      doc[n++] = (char)*c;
    } else {
      n += (size_t)snprintf(doc + n, sizeof(doc) - n, "%%%02X", *c);
    }
  }
  doc[n] = 0;
  n = (size_t)snprintf(url, sizeof(url), "http://127.0.0.1:%d/pdfjs/web/viewer.html?file=", pdf_port);
  for(const unsigned char* c = (const unsigned char*)doc; *c && n + 4 < sizeof(url); c++) {
    if(isalnum(*c) || strchr("._~-", *c)) {
      url[n++] = (char)*c;
    } else {
      n += (size_t)snprintf(url + n, sizeof(url) - n, "%%%02X", *c);
    }
  }
  url[n] = 0;
  last_gesture_ms = poc_now_ms();
  load_url(url);
}

static void
pdf_open(download_t* d) {
  POC_LOG("download %d: a PDF, opened in the reader", d->id);
  pdf_open_path(d->path);
}

#endif

#ifdef APP_RN
static void
download_view(download_t* d) {
  if(pdf_reader() && path_allowed(d->path)) {
    pdf_open(d);
  }
}
#endif

// A finished video, sound or picture opens in the tab shown (Definições,
// Transferências, "Abrir"; docs/16-abrir-e-extrair.md): its file:// address,
// WebKit's own player or picture. Only the downloads' own files.
static void
download_open(download_t* d) {
  struct stat st;
#ifdef APP_RN
  const bool pdf = d && pdf_name(d->name) && pdf_reader();
#else
  const bool pdf = false;
#endif
  if(!d || d->state != DL_DONE || (strcmp(d->kind, "media") && !pdf) || !path_allowed(d->path) ||
     stat(d->path, &st) || !S_ISREG(st.st_mode)) {
    bar_notify("O ficheiro já não está lá", "dismiss");
    return;
  }
#ifdef APP_RN
  if(pdf) {
    pdf_open(d);
    return;
  }
#endif
  static char url[3 * sizeof(d->path) + 8];
  file_url(d->path, url, sizeof(url));
  POC_LOG("download %d: opened in the tab", d->id);
  last_gesture_ms = poc_now_ms();
  load_url(url);
}


#ifdef REMOTE_UI
// "Extrair" for a finished .zip (unzip.h, docs/16-abrir-e-extrair.md): into
// a new folder beside it, named after it. One at a time, on a thread below
// the others (760) that waits a little after each MiB (memory: no spinning
// threads); what came of it in the bar and in the list.
typedef struct unzip_job {
  int id;  // the download
  char zip[1024];
  char dest[1100];
  long long free_bytes;
  int err;
  unzip_result_t result;
} unzip_job_t;

static int unzip_running_id;

static int
unzip_progress(void*, long long done, long long total) {
  unzip_percent = total > 0 ? (int)(done * 100 / total) : 0;
  usleep(2000);
  return 0;
}

static void
unzip_main(void* arg) {
  unzip_job_t* job = (unzip_job_t*)arg;
  download_t* d = download_by_id(job->id);
  char text[1400];

  POC_LOG("unzip %d: %d: %d files, %d folders, %d skipped, %lld of %lld bytes", job->id, job->err,
          job->result.files, job->result.dirs, job->result.skipped, job->result.bytes,
          job->result.total);
  if(job->err == UNZIP_ERR_OPEN || job->err == UNZIP_ERR_FORMAT || job->err == UNZIP_ERR_SPACE) {
    rmdir(job->dest);  // nothing was written
  }
  switch(job->err) {
  case UNZIP_OK:
    if(job->result.skipped) {
      snprintf(text, sizeof(text), "Extraído para %s; %d ficaram de fora", job->dest,
               job->result.skipped);
    } else {
      snprintf(text, sizeof(text), "Extraído para %s", job->dest);
    }
    break;
  case UNZIP_ERR_FORMAT:
    snprintf(text, sizeof(text), "Este ZIP está estragado ou tem várias partes");
    break;
  case UNZIP_ERR_SPACE:
    snprintf(text, sizeof(text), "Não há espaço para extrair este ZIP");
    break;
  case UNZIP_ERR_WRITE:
    snprintf(text, sizeof(text), "Não foi possível escrever os ficheiros do ZIP");
    break;
  case UNZIP_ERR_DATA:
    snprintf(text, sizeof(text), "Um ficheiro do ZIP está estragado: %s", job->result.entry);
    break;
  default:
    snprintf(text, sizeof(text), "Não foi possível abrir o ZIP");
    break;
  }
  bar_notify(text, job->err ? "dismiss" : "folder-open");
  if(d) {
    d->unzip = job->err ? UZ_FAILED : UZ_DONE;
    snprintf(d->unzip_dir, sizeof(d->unzip_dir), "%s", job->err ? "" : job->dest);
  }
  unzip_running_id = 0;
  unzip_percent = -1;
  downloads_changed = true;
  free(job);
}

static void*
unzip_thread(void* arg) {
  unzip_job_t* job = (unzip_job_t*)arg;
  struct sched_param param;
  int policy;

  if(!pthread_getschedparam(pthread_self(), &policy, &param)) {
    param.sched_priority = 760;  // FIFO: a bigger number is a lower priority
    pthread_setschedparam(pthread_self(), policy, &param);
  }
  job->err = unzip_extract(job->zip, job->dest, job->free_bytes, unzip_progress, 0, &job->result);
  WKRunLoopCallOnMainThread(unzip_main, job);
  return 0;
}

static void
download_unzip(download_t* d) {
  struct stat st;
  if(!d || d->state != DL_DONE || !name_ends_with(d->name, ".zip") || !path_allowed(d->path) ||
     d->unzip == UZ_RUNNING) {
    return;
  }
  if(unzip_running_id) {
    bar_notify("Já está a extrair um ZIP", "dismiss");
    return;
  }
  if(stat(d->path, &st) || !S_ISREG(st.st_mode)) {
    bar_notify("O ficheiro já não está lá", "dismiss");
    return;
  }
  unzip_job_t* job = (unzip_job_t*)calloc(1, sizeof(unzip_job_t));
  if(!job) {
    return;
  }
  char dir[1024], folder[256];
  snprintf(dir, sizeof(dir), "%s", d->path);
  char* slash = strrchr(dir, '/');
  if(slash && slash != dir) {
    *slash = 0;
  }
  unzip_folder_name(d->name, folder, sizeof(folder));
  snprintf(job->dest, sizeof(job->dest), "%s/%s", dir, folder);
  for(int i = 2; !stat(job->dest, &st) && i < 1000; i++) {
    snprintf(job->dest, sizeof(job->dest), "%s/%s (%d)", dir, folder, i);
  }
  long long total = 0;
  job->free_bytes = -1;
  if(system_disk(dir, &total, &job->free_bytes)) {
    job->free_bytes = -1;
  }
  if(!path_allowed(job->dest) || mkdir(job->dest, 0777)) {
    bar_notify("Não foi possível criar a pasta para o ZIP", "dismiss");
    free(job);
    return;
  }
  job->id = d->id;
  snprintf(job->zip, sizeof(job->zip), "%s", d->path);

  pthread_attr_t attr;
  pthread_t thread;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 256 << 10);  // the PS5 gives 64 KiB
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  const int err = pthread_create(&thread, &attr, unzip_thread, job);
  pthread_attr_destroy(&attr);
  if(err) {
    rmdir(job->dest);
    free(job);
    bar_notify("Não foi possível abrir o ZIP", "dismiss");
    return;
  }
  unzip_running_id = d->id;
  unzip_percent = 0;
  d->unzip = UZ_RUNNING;
  d->unzip_dir[0] = 0;
  downloads_changed = true;
  bar_notify("A extrair o ZIP", "folder-open");
  POC_LOG("unzip %d: into a new folder (%lld bytes free)", d->id, job->free_bytes);
}
#endif


// The Transferências page's "Apagar" (2026-11-10, BrowserPS5 RN: "a secção
// de downloads precisa de um botão para apagar os arquivos baixados"): a
// finished download's file goes from the disk, only inside the download
// folders; one that failed or was cancelled only leaves the list.
static void
download_delete(download_t* d) {
  if(d->state < DL_DONE || d->unzip == UZ_RUNNING) {
    return;  // still running, waiting, being copied or extracted
  }
#ifdef APP_RN
  if(d->kept) {  // "Tirar da lista" on one a session before left: its file and its map go with its line
    multi_kept_forget(d);
  }
#endif
  if(d->state == DL_DONE && d->path[0]) {
    const char* const dirs[] = {settings.downloads_dir, settings.media_dir, settings.pkg_dir};
    bool inside = false;
    for(const char* dir : dirs) {
      const size_t n = strlen(dir);
      if(n && !strncmp(d->path, dir, n) && d->path[n] == '/' && !strstr(d->path + n, "/..")) {
        inside = true;
      }
    }
    if(!inside) {
      POC_LOG("download %d: not in a download folder, its file is kept", d->id);
      return;
    }
    if(unlink(d->path) && errno != ENOENT) {
      POC_LOG("download %d: its file could not be deleted (%s)", d->id, strerror(errno));
      return;
    }
    POC_LOG("download %d: its file deleted", d->id);
  }
  if(d->ref) {
    WKRelease(d->ref);
  }
  memset(d, 0, sizeof(*d));
  downloads_changed = true;
}


#ifdef APP_RN
// "Executar" on a finished .elf (2026-11-11, the settings' downloads; what
// the scene's payload senders do from a PC): the file goes to the console's
// own payload loader (elfldr, 127.0.0.1:9021), which runs it. Asked for by
// the user with two presses on that file; only a file in the folders of the
// downloads, at most 64 MiB. On a thread of its own, which sleeps in the
// socket's calls.
#define PAYLOAD_PORT 9021
#define PAYLOAD_MAX  (64 * 1024 * 1024)

static void
payload_said(void* arg) {
  char* text = (char*)arg;
  bar_notify(text, "developer-board");
  free(text);
}

static void*
payload_thread(void* arg) {
  char* path = (char*)arg;
  const char* slash = strrchr(path, '/');
  const char* name = slash ? slash + 1 : path;
  char said[400];
  bool ok = false;
  long long sent = 0;
  int fd = open(path, O_RDONLY);
  int s = -1;

  if(fd >= 0 && (s = socket(AF_INET, SOCK_STREAM, 0)) >= 0) {
    struct sockaddr_in to;
    struct timeval wait = {5, 0};
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(PAYLOAD_PORT);
    to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &wait, sizeof(wait));
    if(!connect(s, (struct sockaddr*)&to, sizeof(to))) {
      static const size_t CHUNK = 65536;
      char* buffer = (char*)malloc(CHUNK);
      ok = buffer != 0;
      for(ssize_t n; ok && (n = read(fd, buffer, CHUNK)) > 0;) {
        for(ssize_t done = 0; done < n;) {
          ssize_t w = send(s, buffer + done, (size_t)(n - done), 0);
          if(w <= 0) {
            ok = false;
            break;
          }
          done += w;
          sent += w;
        }
      }
      free(buffer);
      if(ok) {
        snprintf(said, sizeof(said), lang_en() ? LM("%s sent to the payload loader") : "%s enviado para o carregador de payloads", name);
      } else {
        snprintf(said, sizeof(said), lang_en() ? LM("%s: the sending stopped half way") : "%s: o envio parou a meio", name);
      }
    } else {
      snprintf(said, sizeof(said), lang_en() ? LM("No payload loader answers on port %d (elfldr)")
                                             : "Nenhum carregador de payloads responde na porta %d (elfldr)", PAYLOAD_PORT);
    }
  } else {
    snprintf(said, sizeof(said), lang_en() ? LM("%s could not be opened") : "Não consegui abrir %s", name);
  }
  if(s >= 0) {
    close(s);
  }
  if(fd >= 0) {
    close(fd);
  }
  POC_LOG("payload: %s, %lld bytes sent to port %d", ok ? "sent" : "NOT sent", sent, PAYLOAD_PORT);
  free(path);
  WKRunLoopCallOnMainThread(payload_said, strdup(said));
  return 0;
}

static void
download_run_payload(const download_t* d) {
  struct stat st;
  pthread_t thread;
  if(d->state != DL_DONE || !name_ends_with(d->path, ".elf") || !path_allowed(d->dir)
     || stat(d->path, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 || st.st_size > PAYLOAD_MAX) {
    POC_LOG("payload: download %d is not a finished .elf of up to %d MiB", d->id, PAYLOAD_MAX >> 20);
    return;
  }
  char* path = strdup(d->path);
  if(path && !pthread_create(&thread, 0, payload_thread, path)) {
    pthread_detach(thread);
  } else {
    free(path);
  }
}
#endif

// What the settings page or the phone does to a download (by its id):
// cancel, retry, open (in the tab shown), unzip (the menu's browser),
// delete (download_delete()).
static void
download_do(const char* action, int id) {
  download_t* d = download_by_id(id);
  if(!d) {
    return;
  }
  if(!strcmp(action, "delete")) {
    if(d->id == dialog_download) {
      dialog_hide();
    }
    download_delete(d);
  }
  if(!strcmp(action, "cancel")) {
    download_cancel(d);
    if(d->id == dialog_download) {
      dialog_hide();
    }
  } else if(!strcmp(action, "retry")) {
    download_retry(d);
#ifdef APP_RN
  } else if(!strcmp(action, "pause")) {
    download_pause(d);
#endif
  } else if(!strcmp(action, "open")) {
    download_open(d);
#ifdef APP_RN
  } else if(!strcmp(action, "run")) {
    download_run_payload(d);
#endif
#ifdef REMOTE_UI
  } else if(!strcmp(action, "unzip")) {
    download_unzip(d);
#endif
  }
}


// A navigation became a download: a slot for it, and its client.
static void
download_begin(WKDownloadRef download) {
  download_t* d = 0;

#ifdef APP_RN
  if(multi_claim(download)) {  // a part of a download in several parts: no line of its own
    return;
  }
#endif
  for(download_t& slot : downloads) {
    if(!slot.id) {
      d = &slot;
      break;
    }
  }
  for(download_t& slot : downloads) {  // or the oldest that ended
    if(!d && slot.state >= DL_DONE && (!d || slot.id < d->id)) {
      d = &slot;
    }
  }
  if(!d) {
    POC_LOG("download: %d running already, this one is not taken", DOWNLOADS_MAX);
    WKDownloadCancel(download, 0, download_cancelled);
    return;
  }
  if(d->ref) {
    WKRelease(d->ref);
  }
  memset(d, 0, sizeof(*d));
  d->id = ++download_ids;
  d->ref = (WKDownloadRef)WKRetain(download);
  d->state = DL_RUNNING;
  dl_rate_start(&d->rate, 0, poc_now_ms());
  WKURLRequestRef request = WKDownloadCopyRequest(download);
  if(request) {
    char method[16] = "";
    take_string(WKURLRequestCopyHTTPMethod(request), method, sizeof(method));
    take_url(WKURLRequestCopyURL(request), d->url, sizeof(d->url));
    d->can_retry = url_is_web(d->url) && strlen(d->url) + 1 < sizeof(d->url) &&
                   (!method[0] || !strcasecmp(method, "GET"));
    WKRelease(request);
  }

  WKDownloadClientV0 client;
  memset(&client, 0, sizeof(client));
  client.base.version = 0;
  client.base.clientInfo = (const void*)(intptr_t)d->id;
  client.decideDestinationWithResponse = download_destination;
  client.didWriteData = download_progress;
  client.didFinish = download_finished;
  client.didFailWithError = download_failed;
  WKDownloadSetClient(download, &client.base);
  downloads_changed = true;
}


static void
navigationActionDidBecomeDownload(WKPageRef, WKNavigationActionRef, WKDownloadRef download,
                                  const void*) {
  download_begin(download);
}


static void
navigationResponseDidBecomeDownload(WKPageRef, WKNavigationResponseRef,
                                    WKDownloadRef download, const void*) {
  download_begin(download);
}


// [{"id", "name", "path", "received", "total", "state", "speed" (bytes/s),
// "left" (s, -1 not known), "stalled", "retry"}], the oldest first.
static void
downloads_json(text_t* t) {
  const download_t* order[DOWNLOADS_MAX];
  int count = 0;
  const double now = poc_now_ms();

  for(const download_t& d : downloads) {
    if(d.id) {
      int i = count++;
      for(; i > 0 && order[i - 1]->id > d.id; i--) {
        order[i] = order[i - 1];
      }
      order[i] = &d;
    }
  }
  text_add(t, "[");
  for(int i = 0; i < count; i++) {
    const download_t* d = order[i];
    text_printf(t, "%s{\"id\": %d, \"name\": ", i ? ", " : "", d->id);
    text_json(t, d->name);
    text_add(t, ", \"path\": ");
    text_json(t, d->path);
    const bool running = d->state == DL_RUNNING && !d->written;
    const long long speed = running ? dl_rate_speed(&d->rate, now) : 0;
    text_printf(t, ", \"received\": %lld, \"total\": %lld, \"state\": \"%s\"",
                d->state == DL_MOVING ? d->total : d->received, d->total,
                download_states[d->state]);
#ifdef APP_RN
    text_printf(t, ", \"view\": %s", pdf_name(d->name) && pdf_reader() ? "true" : "false");
    {  // in parts, with the line failing under it: it waits and goes on by itself (multi_trouble())
      const multi_t* m = multi_of(d->id);
      text_printf(t, ", \"waiting\": %s", running && m && m->trouble && now - m->bytes_ms > 3000 ? "true" : "false");
      text_printf(t, ", \"resume\": %s", d->kept && d->state == DL_FAILED ? "true" : "false");
      text_printf(t, ", \"pause\": %s", running && m && m->file[0] && m->map[0] ? "true" : "false");
    }
#endif
    text_printf(t, ", \"kind\": \"%s\", \"unzip\": \"%s\", \"unzip_percent\": %d, \"unzip_dir\": ",
                d->kind, unzip_states[d->unzip], d->unzip == UZ_RUNNING ? unzip_percent : -1);
    text_json(t, d->unzip_dir);
    text_printf(t, ", \"speed\": %lld, \"left\": %lld, \"stalled\": %s, \"retry\": %s}", speed,
                running ? dl_rate_left(&d->rate, d->received, d->total, now) : -1LL,
                running && !speed && d->rate.bps > 0 ? "true" : "false",
                (d->state == DL_FAILED || d->state == DL_CANCELLED) && d->can_retry ? "true" : "false");
  }
  text_add(t, "]");
}


#ifdef APP_RN
// BrowserPS5 RN (2026-11-10 21:10): the Transferências page shows from the
// start what is already in the download folders (Telegram's APKs were
// in /data/downloads, the page of a new start showed nothing): the newest 12
// files, as finished downloads.
typedef struct found_file {
  char path[1024];
  char name[256];
  const char* dir;
  long long size;
  time_t when;
} found_file_t;

static int
found_older_first(const void* a, const void* b) {
  const time_t x = ((const found_file_t*)a)->when, y = ((const found_file_t*)b)->when;
  return x < y ? -1 : x > y;
}

static void
downloads_from_folders(void) {
  static found_file_t files[64];
  const char* const dirs[] = {settings.downloads_dir, settings.media_dir, settings.pkg_dir};
  int count = 0;

  for(const char* dir_path : dirs) {
    DIR* dir = dir_path[0] ? opendir(dir_path) : 0;
    struct dirent* e;
    if(!dir) {
      continue;
    }
    while((e = readdir(dir))) {
      struct stat st;
      found_file_t f = {};
      if(e->d_name[0] == '.' || name_ends_with(e->d_name, ".part")) {
        continue;
      }
      snprintf(f.path, sizeof(f.path), "%s/%s", dir_path, e->d_name);
      if(stat(f.path, &st) || !S_ISREG(st.st_mode)) {
        continue;
      }
      snprintf(f.name, sizeof(f.name), "%s", e->d_name);
      f.dir = dir_path;
      f.size = st.st_size;
      f.when = st.st_mtime;
      if(count < 64) {
        files[count++] = f;
        continue;
      }
      int oldest = 0;  // full: the newest 64 are kept
      for(int i = 1; i < count; i++) {
        if(files[i].when < files[oldest].when) {
          oldest = i;
        }
      }
      if(files[oldest].when < f.when) {
        files[oldest] = f;
      }
    }
    closedir(dir);
  }
  qsort(files, (size_t)count, sizeof(files[0]), found_older_first);
  int shown = 0;
  for(int i = count > 12 ? count - 12 : 0; i < count; i++) {
    download_t* d = 0;
    for(download_t& slot : downloads) {
      if(!slot.id) {
        d = &slot;
        break;
      }
    }
    if(!d) {
      break;
    }
    memset(d, 0, sizeof(*d));
    d->id = ++download_ids;
    d->state = DL_DONE;
    d->chosen = true;
    d->written = true;
    snprintf(d->name, sizeof(d->name), "%s", files[i].name);
    snprintf(d->kind, sizeof(d->kind), "%s", download_kind(files[i].name, ""));
    snprintf(d->dir, sizeof(d->dir), "%s", files[i].dir);
    snprintf(d->path, sizeof(d->path), "%s", files[i].path);
    d->received = d->total = files[i].size;
    shown++;
  }
  POC_LOG("downloads: %d file(s) in the folders, the newest %d shown", count, shown);
}
#endif


// The tick: what changed, to the bar's button, the settings page if it is
// open, and the dialog's bar.
static void
downloads_tell(void) {
  const double now = poc_now_ms();
  for(const download_t& d : downloads) {  // one that stalls says so: WebKit
    if(d.id && d.state == DL_RUNNING && d.speed_told && !dl_rate_speed(&d.rate, now)) {
      downloads_changed = true;           // says nothing while nothing comes
    }
  }
  static int unzip_percent_told = -1;
  if(unzip_percent != unzip_percent_told) {  // an extraction goes on
    unzip_percent_told = unzip_percent;
    downloads_changed = true;
  }
  if(!downloads_changed) {
    return;
  }
  downloads_changed = false;
  for(download_t& d : downloads) {
    d.speed_told = d.id && d.state == DL_RUNNING ? dl_rate_speed(&d.rate, now) : 0;
  }
  phone_downloads_note();
  int count = 0, active = 0;
  long long got = 0, total = 0;
  for(const download_t& d : downloads) {
    if(!d.id) {
      continue;
    }
    count++;
    if(d.state == DL_RUNNING || d.state == DL_MOVING) {
      active++;
      got += d.state == DL_MOVING ? d.total : d.received;
      total += d.total > 0 ? d.total : d.received;
    }
  }
  char script[160];
  snprintf(script, sizeof(script), "setDownloads({count: %d, active: %d, percent: %d})",
           count, active, total > 0 ? (int)(got * 100 / total) : 0);
  run_script(BAR, script, 0, ignore_result);
  if(own_page_shown(SETTINGS_URL)) {
    text_t t = {};
    text_add(&t, "setDownloads(");
    downloads_json(&t);
    text_add(&t, ")");
    text_run(PAGE, &t);
  }
  const download_t* d = app.dialog_open ? download_by_id(dialog_download) : 0;
  if(d) {
    snprintf(script, sizeof(script), "dialog.progress(%lld, %lld, %lld)", d->received, d->total,
             d->state == DL_RUNNING && !d->written ? dl_rate_speed(&d->rate, now) : 0LL);
    run_script(DIALOG, script, 0, ignore_result);
  }
}


// What was left in PARTIAL_DIR by a run that ended in the middle; and (RN, 2026-11-12) in the hidden folders
// of the downloads' own folders (download_destination()), with the file of a download in several parts (multi_*).
static int
partial_clean_in(const char* where) {
  DIR* dir = opendir(where);
  int removed = 0;

  if(!dir) {
    return 0;
  }
  for(struct dirent* e; (e = readdir(dir));) {
    const char* part = strstr(e->d_name, ".part");
    if(part && (!part[5] || !strncmp(part + 5, ".p", 2) || !strcmp(part + 5, ".multi"))) {  // "N-T.part" and "N-T.part.multi"
      char path[700];
      snprintf(path, sizeof(path), "%s/%s", where, e->d_name);
#ifdef APP_RN
      if(multi_kept_file(path)) {  // an unfinished download's, to go on with (multi_kept_scan())
        continue;
      }
#endif
      removed += !unlink(path);
    }
  }
  closedir(dir);
  return removed;
}

static void
partial_clean(void) {
#ifdef APP_RN
  multi_kept_scan();
#endif
  int removed = partial_clean_in(PARTIAL_DIR);
#ifdef APP_RN
  const char* const dirs[] = {settings.downloads_dir, settings.pkg_dir, settings.media_dir};
  for(const char* dir : dirs) {
    char near[600];
    snprintf(near, sizeof(near), "%s/" PARTIAL_NEAR, dir);
    removed += partial_clean_in(near);
    rmdir(near);
  }
#endif
  if(removed) {
    POC_LOG("downloads: %d left from before removed", removed);
  }
}


// ---------------------------------------------------------------------------
// The dialog (dialog.html): where a download goes, or a folder for a setting.

static char* dialog_script;  // what opened it, again if its page loads again
static void keys_cancel(void);

// Shows it with what script opens in it; takes t.
static void
dialog_show(text_t* t, int download) {
  if(t->failed || !t->s) {
    free(t->s);
    return;
  }
  free(dialog_script);
  dialog_script = t->s;
  *t = text_t{};
  dialog_download = download;
  menu_hide();
  keys_cancel();
  run_script(DIALOG, dialog_script, 0, ignore_result);
  ui_sound(LINK_SOUND_OPEN);
  pthread_mutex_lock(&app.lock);
  app.dialog_open = true;
  app.ring.on = false;
  app.dirty = true;
  pthread_cond_signal(&app.cond);
  pthread_mutex_unlock(&app.lock);
  app.nav_fresh = true;
}


static void
dialog_next(void) {
  const download_t* next = 0;

  if(app.dialog_open) {
    return;
  }
  for(const download_t& d : downloads) {
    if(d.id && !d.chosen && !d.extension && (d.state == DL_RUNNING || d.state == DL_WAITING)
       && (!next || d.id < next->id)) {
      next = &d;
    }
  }
  if(next) {
    dialog_for_download(next);
  }
}


static void
dialog_hide(void) {
  if(!app.dialog_open) {
    return;
  }
  ui_sound(LINK_SOUND_CLOSE);
  pthread_mutex_lock(&app.lock);
  app.dialog_open = false;
  if(app.ring.pane == DIALOG) {
    app.ring.on = false;
  }
  app.dirty = true;
  pthread_cond_signal(&app.cond);
  pthread_mutex_unlock(&app.lock);
  free(dialog_script);
  dialog_script = 0;
  dialog_download = 0;
  if(app.keys_open && app.ime_pane == DIALOG) {
    keys_cancel();
  }
  if(app.focus == DIALOG) {
    set_focus(PAGE);
  }
  dialog_next();  // another download that waits for its folder
}


static long long
free_space(const char* path) {
  char probe[600];
  struct statvfs v;

  snprintf(probe, sizeof(probe), "%s", path);
  for(;;) {
    if(!statvfs(probe, &v)) {
      return (long long)v.f_bavail * (long long)v.f_frsize;
    }
    char* slash = strrchr(probe, '/');
    if(!slash || slash == probe) {
      return -1;
    }
    *slash = 0;
  }
}


// A drive is in: its folder is another file system than /mnt's.
static bool
mounted(const char* path) {
  struct stat a, b;
  return !stat(path, &a) && S_ISDIR(a.st_mode) && !stat("/mnt", &b) && a.st_dev != b.st_dev;
}


static void
place_json(text_t* t, bool* first, const char* label, const char* path, const char* icon) {
  text_add(t, *first ? "{label: " : ", {label: ");
  *first = false;
  text_json(t, label);
  text_add(t, ", path: ");
  text_json(t, path);
  text_printf(t, ", icon: '%s', free: %lld}", icon, free_space(path));
}


// The dialog's places down its side: the folders of the settings, the
// console's storage and the drives that are in.
static void
places_json(text_t* t) {
  bool first = true;
  char path[32];
  char label[32];

  text_add(t, "[");
  place_json(t, &first, "Transferências", settings.downloads_dir, "arrow-download");
  place_json(t, &first, "Pacotes (PKG)", settings.pkg_dir, "box");
  place_json(t, &first, "Multimédia", settings.media_dir, "video-clip");
  place_json(t, &first, "Armazenamento interno", "/data", "storage");
  for(int i = 0; i < 8; i++) {
    snprintf(path, sizeof(path), "/mnt/usb%d", i);
    if(mounted(path)) {
      snprintf(label, sizeof(label), "USB %d", i + 1);
      place_json(t, &first, label, path, "usb-stick");
    }
  }
  for(int i = 0; i < 2; i++) {
    snprintf(path, sizeof(path), "/mnt/ext%d", i);
    if(mounted(path)) {
      snprintf(label, sizeof(label), "Disco externo %d", i + 1);
      place_json(t, &first, label, path, "storage");
    }
  }
  text_add(t, "]");
}


static void
dialog_for_download(const download_t* d) {
  text_t t = {};

  text_printf(&t, "dialog.save({id: %d, name: ", d->id);
  text_json(&t, d->name);
  text_printf(&t, ", size: %lld, host: ", d->total);
  text_json(&t, d->host);
  text_printf(&t, ", kind: '%s', dir: ", d->kind);
  text_json(&t, download_dir(d->kind));
  text_add(&t, ", places: ");
  places_json(&t);
  text_printf(&t, ", theme: '%s'})", theme_now());
  dialog_show(&t, d->id);
  POC_LOG("download %d: the dialog asks where it goes", d->id);
}


static bool
folder_key(const char* key) {
  return !strcmp(key, "downloads_dir") || !strcmp(key, "pkg_dir") || !strcmp(key, "media_dir");
}


// choose-folder?<key>, from the settings page.
static void
dialog_for_folder(const char* key) {
  const settings_t* s = &settings;
  const bool pkg = !strcmp(key, "pkg_dir");
  const bool media = !strcmp(key, "media_dir");
  text_t t = {};

  text_printf(&t, "dialog.folder({key: '%s', title: ", key);
  text_json(&t, pkg ? "Pasta dos pacotes (PKG)" : media ? "Pasta da multimédia"
                                                        : "Pasta das transferências");
  text_add(&t, ", dir: ");
  text_json(&t, pkg ? s->pkg_dir : media ? s->media_dir : s->downloads_dir);
  text_add(&t, ", note: ");
  text_json(&t, pkg ? "Os ficheiros .pkg transferidos ficam aqui."
              : media ? "Vídeos, música e imagens transferidos ficam aqui."
                      : "Os outros ficheiros transferidos ficam aqui.");
  text_add(&t, ", places: ");
  places_json(&t);
  text_add(&t, "})");
  dialog_show(&t, 0);
}


static int
name_order(const void* a, const void* b) {
  return strcasecmp(*(char* const*)a, *(char* const*)b);
}


// browse?<dir>: its folders, for the dialog's list.
static void
dialog_browse(const char* dir) {
  enum { MAX = 400 };
  char* names[MAX];
  int count = 0;
  text_t t = {};

  if(path_allowed(dir)) {
    DIR* d = opendir(dir);
    for(struct dirent* e; d && count < MAX && (e = readdir(d));) {
      if(e->d_name[0] == '.') {
        continue;
      }
      bool folder = e->d_type == DT_DIR;
      if(e->d_type == DT_UNKNOWN || e->d_type == DT_LNK) {
        char full[1024];
        struct stat st;
        snprintf(full, sizeof(full), "%s/%s", dir, e->d_name);
        folder = !stat(full, &st) && S_ISDIR(st.st_mode);
      }
      if(folder && (names[count] = strdup(e->d_name))) {
        count++;
      }
    }
    if(d) {
      closedir(d);
    }
  }
  qsort(names, (size_t)count, sizeof(*names), name_order);
  text_add(&t, "dialog.folders(");
  text_json(&t, dir);
  text_add(&t, ", [");
  for(int i = 0; i < count; i++) {
    if(i) {
      text_add(&t, ", ");
    }
    text_json(&t, names[i]);
    free(names[i]);
  }
  text_add(&t, "])");
  text_run(DIALOG, &t);
}


// What the dialog sent (dialog.html's send()): save?<id>\n<dir>\n<name>,
// cancel?<id>, pick?<key>\n<dir>, close, browse?<dir>, mkdir?<dir>\n<name>.
static void
dialog_command(const char* command) {
  const char* argument = strchr(command, '?');
  size_t name_length = argument ? (size_t)(argument - command) : strlen(command);
  static char text[4096];

  address_decode(argument ? argument + 1 : "", text, sizeof(text));
#ifdef APP_RN
  if(upload_command(command, text)) {  // entries?<dir>, upload?<path>\n<path>..., upload-cancel
    return;
  }
  if(ask_command(command, text)) {  // ask-ok?<text>, ask-cancel
    return;
  }
#endif
  char* line2 = strchr(text, '\n');
  if(line2) {
    *line2++ = 0;
  }
  char* line3 = line2 ? strchr(line2, '\n') : 0;
  if(line3) {
    *line3++ = 0;
  }
#define IS(name) (name_length == strlen(name) && !strncmp(command, name, name_length))
  if(IS("save") && line3) {
    download_t* d = download_by_id(atoi(text));
    char name[256];
    clean_name(line3, name, sizeof(name));
    if(!d || d->chosen || !path_allowed(line2) || !name[0]) {
      POC_LOG("dialog: a place or name that is not taken");
      return;
    }
    snprintf(d->dir, sizeof(d->dir), "%s", line2);
    snprintf(d->name, sizeof(d->name), "%s", name);
    d->chosen = true;
    POC_LOG("download %d goes to %s", d->id, d->dir);
    dialog_hide();
    if(d->written && (d->state == DL_RUNNING || d->state == DL_WAITING)) {
      download_move(d);
    }
    downloads_changed = true;
  } else if(IS("login-save") || IS("login-never") || IS("login-skip")) {
    login_answer(IS("login-save") ? 1 : IS("login-never") ? 2 : 0);
  } else if(IS("cancel")) {
    download_t* d = download_by_id(atoi(text));
    if(d && d->id == dialog_download) {
      download_cancel(d);
    }
    dialog_hide();
  } else if(IS("pick") && line2) {
    if(folder_key(text) && settings_set(&settings, text, line2) >= 0) {
      settings_store();
      POC_LOG("setting %s: a folder was chosen", text);
      if(own_page_shown(SETTINGS_URL)) {
        text_t t = {};
        text_printf(&t, "setSetting('%s', ", text);
        text_json(&t, line2);
        text_add(&t, ")");
        text_run(PAGE, &t);
      }
    } else {
      POC_LOG("dialog: that folder is not taken");
    }
    dialog_hide();
  } else if(IS("close")) {
    dialog_hide();
  } else if(IS("browse")) {
    dialog_browse(text);
  } else if(IS("mkdir") && line2) {
    char name[256];
    char path[1024];
    clean_name(line2, name, sizeof(name));
    snprintf(path, sizeof(path), "%s/%s", text, name);
    if(name[0] && path_allowed(path) && mkdir(path, 0777)) {
      POC_LOG("dialog: the folder was not made (%s)", strerror(errno));
    }
    dialog_browse(text);
  } else {
    POC_LOG("the dialog sent an unknown command: %.*s", (int)name_length, command);
  }
#undef IS
}


// ---------------------------------------------------------------------------
// The menu (menu.html), under ⋮.

static void
menu_hide(void) {
  if(!app.menu_open) {
    return;
  }
  ui_sound(LINK_SOUND_CLOSE);
  pthread_mutex_lock(&app.lock);
  app.menu_open = false;
  if(app.ring.pane == MENU) {
    app.ring.on = false;
  }
  app.dirty = true;
  pthread_cond_signal(&app.cond);
  pthread_mutex_unlock(&app.lock);
  run_script(BAR, "setMenuOpen(false)", 0, ignore_result);
  if(app.keys_open && app.ime_pane == MENU) {
    keys_cancel();
  }
  if(app.focus == MENU) {
    set_focus(PAGE);
  }
}


static void
menu_show(bool favorite) {
  char url[4096];
  char script[512];

  ui_sound(LINK_SOUND_OPEN);
  take_url(WKPageCopyActiveURL(page(PAGE)), url, sizeof(url));
#define B(x) ((x) ? "true" : "false")
  snprintf(script, sizeof(script),
           "show({theme: '%s', zoom: %d, desktop: %s, dark_sites: %s, adblock: %s, "
           "favorite: %s, canFavorite: %s, fullscreen: %s})", theme_now(), settings.zoom,
           B(settings.desktop), B(settings.dark_sites), B(settings.adblock), B(favorite),
           B(url_is_web(url)), B(app.fullscreen));
#undef B
  run_script(MENU, script, 0, ignore_result);
#ifdef APP_RN
  {
    text_t t = {};  // the page's address, for the menu's QR code
    text_add(&t, "show({url: ");
    text_json(&t, url_is_web(url) ? url : "");
    text_add(&t, "})");
    text_run(MENU, &t);
  }
#endif
  keys_cancel();
  pthread_mutex_lock(&app.lock);
  app.menu_open = true;
  app.ring.on = false;
  app.dirty = true;
  pthread_cond_signal(&app.cond);
  pthread_mutex_unlock(&app.lock);
  app.nav_fresh = true;
  run_script(BAR, "setMenuOpen(true)", 0, ignore_result);
  POC_LOG("menu open");
}


static void
menu_favorite_known(WKTypeRef result, WKErrorRef, void*) {
  menu_show(result && WKGetTypeID(result) == WKBooleanGetTypeID()
            && WKBooleanGetValue((WKBooleanRef)result));
}


static void
menu_toggle(void) {
  if(app.menu_open) {
    menu_hide();
  } else if(!app.dialog_open) {
    // The bar knows the favorites: is this page one?
    run_script(BAR, "favoriteIndex(state.url) >= 0", 0, menu_favorite_known);
  }
}


// ---------------------------------------------------------------------------
// The tabs of the last time (session.h, docs/20): read at the start, before
// this run writes its own (session_ready); with "Ao ligar: continuar onde
// fiquei" they come back then, otherwise the start page offers them until
// they are reopened.
static char session_prev[SESSION_TABS_MAX][SESSION_URL_MAX];
static int session_prev_count, session_prev_current;
static bool session_ready, session_restored;
static void session_note(void);
static void session_forget(void);
static int session_offer(void);
static bool tabs_restore(void);


// The browser's own pages, written into DATA_DIR with their data in them.

#ifdef APP_RN
// Does something on the console itself listen on port (the scene's websrv
// on 8080, with its homebrew launcher)? The loopback answers at once, yes
// or no; the socket never blocks all the same (this is the main thread): at
// most 150 ms for the answer, and no answer is a no.
static bool
local_port_answers(int port) {
  int s = socket(AF_INET, SOCK_STREAM, 0);
  if(s < 0) {
    return false;
  }
  struct sockaddr_in to;
  bool yes = false;
  memset(&to, 0, sizeof(to));
  to.sin_family = AF_INET;
  to.sin_port = htons((uint16_t)port);
  to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  const int flags = fcntl(s, F_GETFL, 0);
  if(flags >= 0 && !fcntl(s, F_SETFL, flags | O_NONBLOCK)) {
    if(!connect(s, (struct sockaddr*)&to, sizeof(to))) {
      yes = true;
    } else if(errno == EINPROGRESS) {
      struct pollfd wait = {s, POLLOUT, 0};
      int error = 0;
      socklen_t size = sizeof(error);
      yes = poll(&wait, 1, 150) == 1 && !getsockopt(s, SOL_SOCKET, SO_ERROR, &error, &size) && !error;
    }
  }
  close(s);
  return yes;
}
#endif

#ifdef APP_RN
// 2026-11-14: the games on this console for the start page (start-rn.js, "your game"): the one open
// behind the browser first, then the ones played last (games.c: read from the PS5's own data, only read). Looked
// for again when the start page opens and the last look is GAMES_FRESH_MS old.
#define GAMES_FRESH_MS 30000
static game_info_t games_list[GAMES_MOST];
static int games_count;
static double games_ms = -1e9;
static char games_note[GAMES_NOTE];
// The cheat files of etaHEN's the console already has (CHEATS_DIR/<kind>/<id>_...), CHEATS_THERE at most, read
// with the games; each game is given its own, GAME_CHEATS at most (game_json_one()).
#define GAME_CHEATS  12
#define CHEATS_THERE 512
static char cheats_there[CHEATS_THERE][72];
static int cheats_there_count;
// 2026-11-16 (docs/32, "the whole library in your games"): every game of the console, for the start page's "Your
// games" (library_tell()); made when it is first asked for.
static game_info_t* library_list;
static int library_count;

static void
games_stale(void) {
  games_ms = -1e9;
}

static const char*
games_lang(void) {
  static const struct {
    const char* code;
    const char* locale;
  } map[] = {{"pt", "pt-PT"}, {"en", "en-US"}, {"es", "es-ES"}, {"fr", "fr-FR"}, {"de", "de-DE"}, {"it", "it-IT"}, {"nl", "nl-NL"}};
  for(const auto& m : map) {
    if(!strcmp(lang_code(), m.code)) {
      return m.locale;
    }
  }
  return "en-US";
}

static void
cheats_there_read(void) {
  cheats_there_count = 0;
  static const char* const kinds[] = {"json", "shn", "mc4"};
  for(const char* kind : kinds) {
    char dir[96], ext[8];
    snprintf(dir, sizeof(dir), CHEATS_DIR "/%s", kind);
    DIR* d = opendir(dir);
    for(struct dirent* e; d && cheats_there_count < CHEATS_THERE && (e = readdir(d));) {
      if(cheat_name_ok(e->d_name, ext)) {
        snprintf(cheats_there[cheats_there_count++], sizeof(cheats_there[0]), "%s", e->d_name);
      }
    }
    if(d) {
      closedir(d);
    }
  }
}

static void phone_games_note(void);  // (the phone's card: the row for the server's thread)

static void
games_refresh(void) {
  const double start = poc_now_ms();
  if(start - games_ms < GAMES_FRESH_MS) {
    return;
  }
  games_count = games_recent(games_list, GAMES_MOST, games_lang(), games_note, sizeof(games_note));
  cheats_there_read();
  games_ms = poc_now_ms();
  phone_games_note();
  POC_LOG("games: %s, in %.0f ms%s%s", games_note, games_ms - start, games_count ? "; the first: " : "",
          games_count ? games_list[0].name : "");
}

static void
game_json_one(text_t* t, const game_info_t* g) {
  char tag[96] = "", url[128];
  game_hashtag(g->name, tag, sizeof(tag));
  text_add(t, "{\"id\": ");
  text_json(t, g->id);
  text_add(t, ", \"name\": ");
  text_json(t, g->name);
  text_add(t, ", \"tag\": ");
  text_json(t, tag);
  snprintf(url, sizeof(url), "%s%s", g->icon[0] ? "file://" : "", g->icon);
  text_add(t, ", \"icon\": ");
  text_json(t, url);
  snprintf(url, sizeof(url), "%s%s", g->picture[0] ? "file://" : "", g->picture);
  text_add(t, ", \"picture\": ");
  text_json(t, url);
  text_printf(t, ", \"open\": %d, \"ps4\": %d, \"played\": %lld, \"colour\": %u, \"trophies\": [%d, %d]", g->open, g->ps4,
              g->played, g->colour, g->trophies_have, g->trophies);
  text_add(t, ", \"version\": ");
  text_json(t, g->version);
  text_add(t, ", \"cheats\": [");
  for(int k = 0, n = 0; k < cheats_there_count && n < GAME_CHEATS; k++) {
    if(!strncmp(cheats_there[k], g->id, 9)) {
      text_add(t, n++ ? ", " : "");
      text_json(t, cheats_there[k]);
    }
  }
  text_add(t, "]}");
}

static void
games_json(text_t* t) {
  games_refresh();
  text_add(t, ", \"games\": [");
  for(int i = 0; i < games_count; i++) {
    text_add(t, i ? ", " : "");
    game_json_one(t, &games_list[i]);
  }
  text_printf(t, "], \"now\": %lld, \"etahen\": %d", (long long)time(0), cheats_state());
}

// 2026-11-16 (docs/32, the start page's "Homebrew"): which of the scene's tools listen on the console now, for the
// panel (start-rn.js): browserps5:services -> bps5Services({ports: {"9021": 1, ...}, ip: "..."}). The loopback
// answers at once (local_port_answers()); the console's own address is for what the panel says to use from a PC
// (ftp://<it>:2121). Only the browser's own start page is answered.
static void
services_tell(void) {
  if(!own_page_shown(START_URL)) {
    POC_LOG("homebrew: the services asked for by another page: not answered");
    return;
  }
  static const int ports[] = {9021, 8080, 2121, 2323, 3232};
  char ip[64] = "", said[160] = "";
  text_t t = {};
  text_add(&t, "window.bps5Services && bps5Services({ports: {");
  for(size_t i = 0; i < sizeof(ports) / sizeof(ports[0]); i++) {
    const bool on = local_port_answers(ports[i]);
    text_printf(&t, "%s\"%d\": %d", i ? ", " : "", ports[i], on ? 1 : 0);
    snprintf(said + strlen(said), sizeof(said) - strlen(said), "%s%d %s", i ? ", " : "", ports[i], on ? "on" : "off");
  }
  text_add(&t, "}, ip: ");
  if(phone_local_address(ip, sizeof(ip))) {
    ip[0] = 0;
  }
  text_json(&t, ip);
  text_add(&t, "})");
  POC_LOG("homebrew: the console's services: %s; its address %s", said, ip[0] ? "known" : "NOT known");
  text_run(PAGE, &t);
}

// browserps5:library: every game of the console for the start page's "Your games" (start-rn.js, gameLibrary()),
// given back as bps5Library([...], now), each game as the row has it (game_json_one()). Read again at each ask
// (a few milliseconds a game); only the browser's own start page is answered.
static void
library_tell(void) {
  if(!own_page_shown(START_URL)) {
    POC_LOG("games: the library asked for by another page: not answered");
    return;
  }
  if(!library_list && !(library_list = (game_info_t*)calloc(GAMES_LIBRARY, sizeof(*library_list)))) {
    return;
  }
  const double start = poc_now_ms();
  char note[GAMES_NOTE];
  library_count = games_library(library_list, GAMES_LIBRARY, games_lang(), note, sizeof(note));
  cheats_there_read();
  text_t t = {};
  text_add(&t, "window.bps5Library && bps5Library([");
  for(int i = 0; i < library_count; i++) {
    text_add(&t, i ? ", " : "");
    game_json_one(&t, &library_list[i]);
  }
  text_printf(&t, "], %lld)", (long long)time(0));
  POC_LOG("games: the library for the start page: %s; %d cheat file(s) of etaHEN's; %zu bytes, in %.0f ms", note,
          cheats_there_count, t.n, poc_now_ms() - start);
  text_run(PAGE, &t);
}
#endif

#ifdef APP_RN
// 2026-11-15 (docs/32, "a page for each game"). The start page's game hub (start-rn.js, gameHub()) asks for a
// game's trophies with browserps5:game?<id>: all of the set, one by one, with which the user has (games.c,
// games_trophies(): the PS5's own files, only read), given back to the page as bps5Game(id, [...]). Only the
// browser's own start page is answered, and only for a game of the row.
#define GAME_NOTES     DATA_DIR "/notes"
#define GAME_NOTE_MOST 6000
#define GAME_SHOTS     48  // a game's captures listed for its page, the newest
static int game_pages_json(text_t* t, const char* id);  // (the pages kept with a game, below)

static const game_info_t*
game_of_row(const char* id) {  // (2026-11-16: or of the library, "Your games", once it was asked for)
  for(int i = 0; i < games_count; i++) {
    if(!strcmp(games_list[i].id, id)) {
      return &games_list[i];
    }
  }
  for(int i = 0; library_list && i < library_count; i++) {
    if(!strcmp(library_list[i].id, id)) {
      return &library_list[i];
    }
  }
  return 0;
}

static void
game_hub(const char* id) {
  if(!own_page_shown(START_URL) || !game_of_row(id)) {
    POC_LOG("games: a game's trophies asked for by another page, or for a game that is not in the row: not answered");
    return;
  }
  game_trophy_t* list = (game_trophy_t*)calloc(GAME_TROPHIES, sizeof(*list));
  const double start = poc_now_ms();
  const int count = list ? games_trophies(id, games_lang(), list, GAME_TROPHIES) : 0;
  int have = 0;
  text_t t = {};
  text_add(&t, "window.bps5Game && bps5Game(");
  text_json(&t, id);
  text_add(&t, ", [");
  for(int i = 0; i < count; i++) {
    have += list[i].have;
    text_printf(&t, "%s{i: %d, g: %d, h: %d, s: %d, n: ", i ? ", " : "", list[i].id, list[i].grade, list[i].have, list[i].hidden);
    text_json(&t, list[i].name);
    text_add(&t, ", d: ");
    text_json(&t, list[i].detail);
    text_add(&t, "}");
  }
  text_add(&t, "], ");
  {  // the user's notes of this game (game_note_save()), with the trophies
    char path[160];
    static char note[GAME_NOTE_MOST + 1];
    snprintf(path, sizeof(path), GAME_NOTES "/%s.txt", id);
    FILE* f = fopen(path, "rb");
    const size_t got = f ? fread(note, 1, GAME_NOTE_MOST, f) : 0;
    note[got] = 0;
    if(f) {
      fclose(f);
    }
    text_json(&t, note);
  }
  int shots = 0, shots_hdr = 0;
  {  // 2026-11-15 (docs/32, "the captures of each game"): the game's own screenshots and clips, the newest first
     // (xps5/captures.c lists the PS5's, only reads: .../NPXS40087/<the game's id>/...), for its page's gallery.
     // A screenshot in HDR (.jxr) is one the engine does not show: counted, not listed.
    capture_t* all = (capture_t*)malloc(CAPTURES_ROOM * sizeof(*all));
    const int n = all ? captures_list(all, CAPTURES_ROOM) : 0;
    char mark[40];
    snprintf(mark, sizeof(mark), "/" CAPTURES_APP "/%s/", id);
    text_add(&t, ", [");
    for(int i = 0; i < n; i++) {
      if(!strstr(all[i].path, mark)) {
        continue;
      }
      if(all[i].kind == CAPTURE_JXR) {
        shots_hdr++;
        continue;
      }
      if(shots >= GAME_SHOTS) {
        continue;
      }
      char url[CAPTURE_PATH + 8];
      snprintf(url, sizeof(url), "file://%s", all[i].path);
      text_add(&t, shots ? ", {u: " : "{u: ");
      text_json(&t, url);
      text_printf(&t, ", v: %d, t: %lld}", all[i].kind == CAPTURE_MP4 || all[i].kind == CAPTURE_WEBM ? 1 : 0, all[i].time);
      shots++;
    }
    text_printf(&t, "], %d", shots_hdr);
    free(all);
  }
  text_add(&t, ", ");
  const int pins = game_pages_json(&t, id);  // the pages kept with it (game_pin_current())
  text_add(&t, ")");
  POC_LOG("games: %s: %d trophy(ies) of its set for the page, %d of them the user's, in %.0f ms; %d capture(s) of the game for its page, %d in HDR left out; %d page(s) kept with it",
          id, count, have, poc_now_ms() - start, shots, shots_hdr, pins);
  free(list);
  text_run(PAGE, &t);
}

// A game's notes written (a file of text by game in the browser's data; empty takes the file away): from its
// hub (game_note_save()) or from the phone (phone_main_game()).
static void
game_note_put(const char* id, const char* text) {
  char path[160];
  mkdir(GAME_NOTES, 0755);
  snprintf(path, sizeof(path), GAME_NOTES "/%s.txt", id);
  if(!text[0]) {
    unlink(path);
    POC_LOG("games: %s: its notes are empty: the file goes", id);
    return;
  }
  char fresh[176];
  snprintf(fresh, sizeof(fresh), "%s.new", path);
  FILE* f = fopen(fresh, "wb");
  if(f) {
    const size_t n = strlen(text);
    const bool ok = fwrite(text, 1, n, f) == n;
    fclose(f);
    if(ok) {
      rename(fresh, path);
    }
    POC_LOG("games: %s: its notes %s (%zu bytes)", id, ok ? "kept" : "NOT kept", n);
  }
}

// browserps5:note-save?<id>=<the text, percent-encoded>: the user's notes of a game, from its hub.
static void
game_note_save(const char* arg) {
  char id[16];
  static char text[GAME_NOTE_MOST + 1];
  const char* eq = strchr(arg, '=');

  if(!eq || eq - arg != 9 || !own_page_shown(START_URL)) {
    return;
  }
  snprintf(id, sizeof(id), "%.9s", arg);
  if(!game_of_row(id)) {
    return;
  }
  address_decode(eq + 1, text, sizeof(text));
  game_note_put(id, text);
}

// 2026-11-16 (docs/32, "pages kept with a game"): the menu's "Guardar no jogo" keeps the page shown (a guide, a
// map) with the game being played, or the one played last (games_list[0]); the game's page lists them
// (game_hub(): its sixth answer, [{u, t}]), the newest first, and takes one away (browserps5:game-unpin). A file of
// lines "address<TAB>title" by game in the browser's data, written only when the user keeps or takes one.
#define GAME_PAGES      DATA_DIR "/game-pages"
#define GAME_PAGES_MOST 30
#define GAME_PAGES_ROOM (GAME_PAGES_MOST * 2400)

static char*
game_pages_read(const char* id) {  // the game's file, whole (or 0), for game_pages_json() and the others
  char path[160];
  snprintf(path, sizeof(path), GAME_PAGES "/%s.txt", id);
  FILE* f = fopen(path, "rb");
  if(!f) {
    return 0;
  }
  char* all = (char*)malloc(GAME_PAGES_ROOM + 1);
  const size_t got = all ? fread(all, 1, GAME_PAGES_ROOM, f) : 0;
  fclose(f);
  if(all) {
    all[got] = 0;
  }
  return all;
}

static int
game_pages_json(text_t* t, const char* id) {  // [{u: "...", t: "..."}, ...]
  char* all = game_pages_read(id);
  int n = 0;
  text_add(t, "[");
  for(char* line = all; line && *line && n < GAME_PAGES_MOST;) {
    char* end = strchr(line, '\n');
    if(end) {
      *end = 0;
    }
    char* tab = strchr(line, '\t');
    if(tab) {
      *tab = 0;
    }
    if(!strncmp(line, "http://", 7) || !strncmp(line, "https://", 8)) {
      text_add(t, n ? ", {\"u\": " : "{\"u\": ");
      text_json(t, line);
      text_add(t, ", \"t\": ");
      text_json(t, tab ? tab + 1 : "");
      text_add(t, "}");
      n++;
    }
    line = end ? end + 1 : 0;
  }
  text_add(t, "]");
  free(all);
  return n;
}

// The game's file again: url first (when given, with its title), then the ones there were but drop.
static bool
game_pages_write(const char* id, const char* url, const char* title, const char* drop) {
  char path[160], fresh[176];
  char* all = game_pages_read(id);
  text_t t = {};
  int n = 0;
  if(url) {
    text_printf(&t, "%s\t", url);
    for(const char* c = title; *c; c++) {  // (one line: its tabs and line ends as spaces)
      text_printf(&t, "%c", (unsigned char)*c < ' ' ? ' ' : *c);
    }
    text_add(&t, "\n");
    n++;
  }
  for(char* line = all; line && *line && n < GAME_PAGES_MOST;) {
    char* end = strchr(line, '\n');
    if(end) {
      *end = 0;
    }
    const char* tab = strchr(line, '\t');
    const size_t ulen = tab ? (size_t)(tab - line) : strlen(line);
    const bool same = (url && strlen(url) == ulen && !strncmp(line, url, ulen)) || (drop && strlen(drop) == ulen && !strncmp(line, drop, ulen));
    if(ulen && !same) {
      text_printf(&t, "%s\n", line);
      n++;
    }
    line = end ? end + 1 : 0;
  }
  free(all);
  mkdir(GAME_PAGES, 0755);
  snprintf(path, sizeof(path), GAME_PAGES "/%s.txt", id);
  bool ok = true;
  if(!n || t.failed) {
    ok = !t.failed;
    if(!n) {
      unlink(path);
    }
  } else {
    snprintf(fresh, sizeof(fresh), "%s.new", path);
    FILE* f = fopen(fresh, "wb");
    ok = f && fwrite(t.s, 1, t.n, f) == t.n;
    if(f) {
      fclose(f);
    }
    ok = ok && !rename(fresh, path);
  }
  POC_LOG("games: %s: %s; %d page(s) kept with it now", id, ok ? "its pages written" : "its pages NOT written", n);
  free(t.s);
  return ok;
}

// The page on the TV kept with a game: the menu's "Guardar no jogo" (game_pin_current()), or the phone's card.
static bool
game_pin_shown(const game_info_t* g) {
  static char url[2048];
  char title[256], said[320];
  take_url(WKPageCopyCommittedURL(page(PAGE)), url, sizeof(url));
  if(strncmp(url, "http://", 7) && strncmp(url, "https://", 8)) {
    bar_notify(lang_en() ? LM("Only web pages can be kept with a game") : "Só as páginas da web se guardam num jogo", "dismiss");
    return false;
  }
  if(tab_is_private()) {
    bar_notify(lang_en() ? LM("A private tab's page is not kept with a game") : "A página de um separador privado não se guarda num jogo", "dismiss");
    return false;
  }
  take_string(WKPageCopyTitle(page(PAGE)), title, sizeof(title));
  if(!game_pages_write(g->id, url, title, 0)) {
    return false;
  }
  snprintf(said, sizeof(said), lang_en() ? LM("Kept with %s: it is on the game's page") : "Guardada com %s: está na página do jogo", g->name);
  bar_notify(said, "games");
  POC_LOG("games: the page shown kept with %s (%s)", g->id, g->open ? "the game open" : "the game played last");
  return true;
}

// The menu's "Guardar no jogo": with the game open, or the one played last.
static void
game_pin_current(void) {
  games_refresh();
  if(games_count <= 0) {
    bar_notify(lang_en() ? LM("No game on this console to keep this page with") : "Nenhum jogo nesta consola para guardar esta página", "dismiss");
    return;
  }
  game_pin_shown(&games_list[0]);
}

// browserps5:game-unpin?<id>=<the address, percent-encoded>: the game's page takes one away.
static void
game_unpin(const char* arg) {
  static char url[2048];
  char id[16];
  const char* eq = strchr(arg, '=');
  if(!eq || eq - arg != 9 || !own_page_shown(START_URL)) {
    return;
  }
  snprintf(id, sizeof(id), "%.9s", arg);
  if(!game_of_row(id)) {
    return;
  }
  address_decode(eq + 1, url, sizeof(url));
  if(url[0]) {
    game_pages_write(id, 0, 0, url);
  }
}

// 2026-11-16 (docs/32, idea 4: "captures to Discord in one press"): the capture shown whole in a game's page goes
// to a Discord channel by the channel's webhook (share.h), its address kept in the browser's data (DISCORD_HOOK,
// read and written by the app only, never in the logs). browserps5:shot-share?<id>=<the capture's file:// address,
// percent-encoded>[&hook=<a webhook's address, percent-encoded>]: with no webhook kept (or one Discord no longer
// knows) the page is asked for one (bps5HookAsk(id, address)), and sends it back with the capture. Only the
// browser's own start page; only a capture of the PS5's own folder (CAPTURES_ROOT), of up to SHARE_DISCORD_MOST;
// the game's name is the message. On a thread of its own; the outcome is a notice.
#define DISCORD_HOOK DATA_DIR "/discord-webhook.txt"
#ifdef SHOW_PROBE
#define SHARE_LOCAL 1  // the probe's own listener on 127.0.0.1 counts as a webhook
#define PROBE_SHOT  DATA_DIR "/probe-shot.png"
#else
#define SHARE_LOCAL 0
#endif

static bool
discord_hook_read(char* hook, size_t size) {
  FILE* f = fopen(DISCORD_HOOK, "rb");
  const size_t got = f ? fread(hook, 1, size - 1, f) : 0;
  if(f) {
    fclose(f);
  }
  hook[got] = 0;
  hook[strcspn(hook, "\r\n")] = 0;
  return share_hook_ok(hook, SHARE_LOCAL);
}

static void
discord_hook_write(const char* hook) {  // "" forgets it
  if(!hook[0]) {
    unlink(DISCORD_HOOK);
    POC_LOG("share: the Discord webhook is forgotten");
    return;
  }
  const int fd = open(DISCORD_HOOK ".new", O_WRONLY | O_CREAT | O_TRUNC, 0600);
  const size_t n = strlen(hook);
  const bool ok = fd >= 0 && write(fd, hook, n) == (ssize_t)n;
  if(fd >= 0) {
    close(fd);
  }
  if(ok) {
    rename(DISCORD_HOOK ".new", DISCORD_HOOK);
  }
  char where[96];
  share_hook_short(hook, where, sizeof(where));
  POC_LOG("share: a Discord webhook %s (%s)", ok ? "kept" : "NOT kept", where);
}

typedef struct share_job {
  char hook[SHARE_HOOK_MAX];
  char path[CAPTURE_PATH];
  char text[160];
} share_job_t;

static int share_busy;  // a send under way (main thread)

static void
share_said(void* arg) {
  char* text = (char*)arg;
  share_busy = 0;
  bar_notify(text, "send");
  free(text);
}

static void*
share_thread(void* arg) {
  share_job_t* job = (share_job_t*)arg;
  const double start = poc_now_ms();
  int status = 0, err = 0;
  const int r = share_discord(job->hook, job->path, job->text, &status, &err);
  const char* slash = strrchr(job->path, '/');
  const char* name = slash ? slash + 1 : job->path;
  char said[400];
  if(!r) {
    snprintf(said, sizeof(said), lang_en() ? LM("%s sent to Discord") : "%s enviado para o Discord", name);
  } else if(status == 401 || status == 404) {  // the webhook was deleted in Discord: forgotten, the next send asks
    unlink(DISCORD_HOOK);
    snprintf(said, sizeof(said), "%s", lang_en() ? LM("Discord no longer knows that webhook: it was forgotten, the next send asks for one")
                                                 : "O Discord já não conhece esse webhook: foi esquecido, o próximo envio pede outro");
  } else if(status == 413) {
    snprintf(said, sizeof(said), "%s", lang_en() ? LM("Too big for that Discord server (10 MB without boosts)")
                                                 : "Grande demais para esse servidor do Discord (10 MB sem boosts)");
  } else if(status) {
    snprintf(said, sizeof(said), lang_en() ? LM("Discord did not take it (HTTP %d)") : "O Discord não o aceitou (HTTP %d)", status);
  } else {
    snprintf(said, sizeof(said), "%s", lang_en() ? LM("Discord did not answer: is the console online?")
                                                 : "O Discord não respondeu: a consola tem rede?");
  }
  POC_LOG("share: %s to Discord: %s, HTTP %d, curl %d, in %.0f ms", name, r ? "NOT sent" : "sent", status, err,
          poc_now_ms() - start);
  memset(job->hook, 0, sizeof(job->hook));
  free(job);
  WKRunLoopCallOnMainThread(share_said, strdup(said));
  return 0;
}

static void
shot_share(const char* arg) {
  static char url[CAPTURE_PATH + 64];
  static char hook[SHARE_HOOK_MAX + 8];
  static char part[3 * (CAPTURE_PATH + 64)];
  char id[16];
  const char* eq = strchr(arg, '=');
  if(!eq || eq - arg != 9 || !own_page_shown(START_URL)) {
    return;
  }
  snprintf(id, sizeof(id), "%.9s", arg);
  const game_info_t* g = game_of_row(id);
  if(!g) {
    return;
  }
  const char* given = strstr(eq + 1, "&hook=");
  snprintf(part, sizeof(part), "%.*s", (int)(given ? given - eq - 1 : (long)strlen(eq + 1)), eq + 1);
  address_decode(part, url, sizeof(url));
  hook[0] = 0;
  if(given) {
    address_decode(given + 6, hook, sizeof(hook));
    hook[strcspn(hook, "\r\n")] = 0;
    for(char* h = hook; *h == ' '; ) {  // (a pasted address may come with spaces around it)
      memmove(h, h + 1, strlen(h));
    }
    for(size_t n = strlen(hook); n && hook[n - 1] == ' '; n--) {
      hook[n - 1] = 0;
    }
  }
  const char* path = !strncmp(url, "file://", 7) ? url + 7 : "";
  bool allowed = !strncmp(path, CAPTURES_ROOT "/", strlen(CAPTURES_ROOT) + 1) && !strstr(path, "/..") && !strstr(path, "/./");
#ifdef SHOW_PROBE
  allowed = allowed || !strcmp(path, PROBE_SHOT);
#endif
  struct stat st;
  if(!allowed || strlen(path) >= CAPTURE_PATH || stat(path, &st) || !S_ISREG(st.st_mode)) {
    POC_LOG("share: %s: not a capture of the PS5's that can be read: not sent", id);
    bar_notify(lang_en() ? LM("That capture could not be read") : "Não consegui ler essa captura", "dismiss");
    return;
  }
  if(st.st_size > SHARE_DISCORD_MOST) {
    POC_LOG("share: %s: %lld bytes, more than Discord takes without boosts: not sent", id, (long long)st.st_size);
    bar_notify(lang_en() ? LM("Too big for that Discord server (10 MB without boosts)")
                         : "Grande demais para esse servidor do Discord (10 MB sem boosts)", "dismiss");
    return;
  }
  if(share_busy) {
    bar_notify(lang_en() ? LM("Still sending the one before") : "Ainda a enviar a anterior", "send");
    return;
  }
  if(hook[0]) {
    if(!share_hook_ok(hook, SHARE_LOCAL)) {
      POC_LOG("share: what the page gave is not a Discord webhook's address: asked again");
      bar_notify(lang_en() ? LM("That is not a Discord webhook address") : "Isso não é o endereço de um webhook do Discord", "dismiss");
      hook[0] = 0;
    } else {
      discord_hook_write(hook);
    }
  } else {
    discord_hook_read(hook, sizeof(hook));
  }
  if(!share_hook_ok(hook, SHARE_LOCAL)) {
    memset(hook, 0, sizeof(hook));
    text_t t = {};
    text_add(&t, "window.bps5HookAsk && bps5HookAsk(");
    text_json(&t, id);
    text_add(&t, ", ");
    text_json(&t, url);
    text_add(&t, ")");
    text_run(PAGE, &t);
    return;
  }
  share_job_t* job = (share_job_t*)calloc(1, sizeof(*job));
  pthread_t thread;
  if(!job) {
    return;
  }
  snprintf(job->hook, sizeof(job->hook), "%s", hook);
  snprintf(job->path, sizeof(job->path), "%s", path);
  snprintf(job->text, sizeof(job->text), "%s", g->name);
  memset(hook, 0, sizeof(hook));
  if(pthread_create(&thread, 0, share_thread, job)) {
    free(job);
    return;
  }
  pthread_detach(thread);
  share_busy = 1;
  POC_LOG("share: %s: %lld bytes of %s on their way to Discord", id, (long long)st.st_size, g->name);
  bar_notify(lang_en() ? LM("Sending to Discord…") : "A enviar para o Discord…", "send");
}

// browserps5:discord-forget, from the page's webhook box: the webhook kept is forgotten.
static void
discord_forget(void) {
  if(own_page_shown(START_URL)) {
    discord_hook_write("");
    bar_notify(lang_en() ? LM("The Discord webhook was forgotten") : "O webhook do Discord foi esquecido", "send");
  }
}

#ifdef SHOW_PROBE
// The probe's stand-in for Discord (the test build listens itself): one POST on 127.0.0.1, what came in it, and
// Discord's 204 back. Sleeps in poll() and recv().
static int probe_hook_port;
static char probe_hook_saw[320];
static int probe_hook_done;

static void*
probe_hook_thread(void* arg) {
  const int s = (int)(intptr_t)arg;
  struct pollfd p = {s, POLLIN, 0};
  const int c = poll(&p, 1, 30000) == 1 ? accept(s, 0, 0) : -1;
  char* got = (char*)malloc(65536);
  size_t n = 0;
  if(c >= 0 && got) {
    struct timeval wait = {10, 0};
    long want = -1;
    setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &wait, sizeof(wait));
    while(n < 65535) {
      const ssize_t r = recv(c, got + n, 65535 - n, 0);
      if(r <= 0) {
        break;
      }
      n += (size_t)r;
      got[n] = 0;
      const char* end = strstr(got, "\r\n\r\n");
      if(end && want < 0) {
        const char* length = strstr(got, "Content-Length:");
        want = (long)(end + 4 - got) + (length && length < end ? atol(length + 15) : 0);
      }
      if(want >= 0 && (long)n >= want) {
        break;
      }
    }
    got[n] = 0;
    bool png = false;
    for(size_t i = 0; i + 4 <= n && !png; i++) {
      png = !memcmp(got + i, "\x89PNG", 4);
    }
    snprintf(probe_hook_saw, sizeof(probe_hook_saw), "%zu bytes: a POST to the webhook's path %s, payload_json %s, the message %s, the file %s, its PNG %s",
             n, strncmp(got, "POST /api/webhooks/123456789/", 29) ? "NO" : "yes", strstr(got, "name=\"payload_json\"") ? "there" : "MISSING",
             strstr(got, "{\"content\":\"") ? "there" : "MISSING", strstr(got, "name=\"files[0]\"; filename=\"probe-shot.png\"") ? "there" : "MISSING",
             png ? "there" : "MISSING");
    static const char answer[] = "HTTP/1.1 204 No Content\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    send(c, answer, sizeof(answer) - 1, 0);
  } else {
    snprintf(probe_hook_saw, sizeof(probe_hook_saw), "nothing came in 30 s");
  }
  if(c >= 0) {
    close(c);
  }
  free(got);
  close(s);
  __atomic_store_n(&probe_hook_done, 1, __ATOMIC_RELEASE);
  return 0;
}

static bool
probe_hook_listen(void) {
  struct sockaddr_in at;
  socklen_t size = sizeof(at);
  pthread_t thread;
  const int s = socket(AF_INET, SOCK_STREAM, 0);
  memset(&at, 0, sizeof(at));
  at.sin_family = AF_INET;
  at.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if(s < 0 || bind(s, (struct sockaddr*)&at, sizeof(at)) || listen(s, 1) || getsockname(s, (struct sockaddr*)&at, &size)
     || pthread_create(&thread, 0, probe_hook_thread, (void*)(intptr_t)s)) {
    if(s >= 0) {
      close(s);
    }
    return false;
  }
  pthread_detach(thread);
  probe_hook_port = ntohs(at.sin_port);
  return true;
}
#endif

// 2026-11-15 (docs/32, "the console's health"): the start page's panel asks with browserps5:health, about every two seconds while it is open, and is
// answered with bps5Health({...}): health.c reads the sensors, only reads. Only the browser's own start page.
static console_health_t health_last;

static void
health_json(text_t* t, const console_health_t* h) {
  text_printf(t, "{cpu: %d, soc: %d, gpu_mw: %d, cpu_mw: %d, mem_mw: %d, mhz: %d, fan: %d, up: %lld, ip: ", h->cpu_c, h->soc_c,
              h->gpu_mw, h->cpu_mw, h->mem_mw, h->cpu_mhz, h->fan_c, h->uptime_s);
  text_json(t, h->ip);
  text_add(t, ", net: ");
  text_json(t, h->net);
  text_add(t, ", disks: [");
  for(int i = 0; i < h->disk_count; i++) {
    text_add(t, i ? ", {p: " : "{p: ");
    text_json(t, h->disks[i].path);
    text_printf(t, ", t: %lld, f: %lld}", h->disks[i].total, h->disks[i].free);
  }
  text_printf(t, "], etahen: %d}", cheats_state());
}

static void
health_tell(void) {
  if(!own_page_shown(START_URL)) {
    return;
  }
  console_health_read(&health_last);
  text_t t = {};
  text_add(&t, "window.bps5Health && bps5Health(");
  health_json(&t, &health_last);
  text_add(&t, ")");
  text_run(PAGE, &t);
}

// "Play", from the game hub. Asked for from the browser's own process once its title had left, the game took the
// console down twice (2026-11-16 14:48 and 14:59;
// docs/28). The user's own way never did: "Sair", then the game from the home screen. So the browser leaves
// exactly as "Sair" does, and play_after.elf (../play_after/main.c), sent just before to the console's payload
// loader (elfldr, 127.0.0.1:9021), asks for the game once the browser's processes are gone and the system has no
// app in front. Without a payload loader nothing is left: the user is told, and the browser stays.
#include "../play_after/play_after.h"

extern "C" const char play_after_elf[], play_after_elf_end[];
static char play_after[16];

static void stop(void* fast);

// All of data to the payload loader; true when it took all of it.
static bool
loader_send(const char* data, size_t size) {
  struct sockaddr_in to;
  struct timeval wait = {5, 0};
  const int s = socket(AF_INET, SOCK_STREAM, 0);
  if(s < 0) {
    return false;
  }
  memset(&to, 0, sizeof(to));
  to.sin_family = AF_INET;
  to.sin_port = htons(PAYLOAD_PORT);
  to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &wait, sizeof(wait));
  bool ok = !connect(s, (struct sockaddr*)&to, sizeof(to));
  for(size_t done = 0; ok && done < size;) {
    const ssize_t w = send(s, data + done, size - done, 0);
    ok = w > 0;
    done += ok ? (size_t)w : 0;
  }
  close(s);
  return ok;
}

static void
game_play(const char* id) {
  if(!own_page_shown(START_URL) || !game_of_row(id)) {
    POC_LOG("games: a game asked to be played by another page, or one that is not in the row: not done");
    return;
  }
  char ask[80];
  const int n = snprintf(ask, sizeof(ask), "%s %d %d %s\n", id, (int)getpid(), (int)getppid(), lang_en() ? "en" : "pt");
  const int fd = open(PLAY_AFTER_ASK ".new", O_WRONLY | O_CREAT | O_TRUNC, 0644);
  bool ok = fd >= 0 && write(fd, ask, (size_t)n) == n;
  if(fd >= 0) {
    close(fd);
  }
  ok = ok && !rename(PLAY_AFTER_ASK ".new", PLAY_AFTER_ASK);
  const size_t size = (size_t)(play_after_elf_end - play_after_elf);
  ok = ok && loader_send(play_after_elf, size);
  if(!ok) {
    unlink(PLAY_AFTER_ASK ".new");
    unlink(PLAY_AFTER_ASK);
    POC_LOG("games: %s not played: the request could not be written, or no payload loader took play_after.elf", id);
    char said[300];
    snprintf(said, sizeof(said), lang_en() ? LM("Play needs the payload loader (elfldr, port %d): start the game from the home screen")
                                           : "O Jogar precisa do carregador de payloads (elfldr, porta %d): abre o jogo no ecrã inicial",
             PAYLOAD_PORT);
    bar_notify(said, "developer-board");
    return;
  }
  snprintf(play_after, sizeof(play_after), "%s", id);
  POC_LOG("games: %s is to be played: play_after.elf (%zu bytes) is with the payload loader, the browser leaves as \"Sair\" does",
          id, size);
  stop((void*)1);
}

// The browser's own updates (2026-11-17; update.h says what an update is). Nobody is asked anything:
//   - some seconds after the first page of a session, on a thread of its own (it sleeps, and its priority is
//     under the browser's others): GitHub's list of the project's releases, UPDATE_EVERY_S apart at most; a newer
//     release's installer is fetched into UPDATE_DIR while the browser is used, checked against the size and the
//     SHA-256 the list gave, and left there with a line that says what it is (UPDATE_READY);
//   - the next time the browser is opened, at its first page: with such an installer waiting it says so, sends
//     update_after.elf to the console's payload loader and leaves as "Sair" does; update_after hands the installer
//     over once the browser is gone and opens the browser again (../update_after/main.c). A session is never cut
//     short for an update, and the installer is the one anybody sends by hand.
// A build with no release file (sent by hand: "dev"), a console with the file UPDATE_OFF_FILE, and a test build
// (but SHOW_UPDATE's) look for nothing. A release whose installer ran and did not install (UPDATE_FAILED) is not
// fetched again. And the browser leaves for one release's installer UPDATE_TRIES_MAX times at most (UPDATE_TRIES,
// counted before it leaves; update_after removes the count once the release is installed): on a console where
// the rest never ends well, whatever the reason, it would otherwise close itself at every start. After that the
// release is as one that failed: it is installed by hand, as before there were updates.
#include "update.h"
#include "update_net.h"
#include "../update_after/update_after.h"

extern "C" const char update_after_elf[], update_after_elf_end[];

#define UPDATE_OFF_FILE     DATA_DIR "/no-updates"
#define UPDATE_CHECKED_FILE UPDATE_DIR "/checked.txt"
#define UPDATE_EVERY_S      (6 * 3600)
#define UPDATE_TRIES_MAX    2
#ifdef SHOW_UPDATE
#define UPDATE_FIRST_S 3
#else
#define UPDATE_FIRST_S 20
#endif
#if (!defined(SHOW) && !defined(DEMO) && !defined(SPEED_TEST)) || defined(SHOW_UPDATE)
#define UPDATES_ON 1
#endif

enum { UPDATE_IDLE, UPDATE_LOOKING, UPDATE_NONE, UPDATE_FETCHING, UPDATE_READY_NOW, UPDATE_NOT_NOW, UPDATE_LEAVING };
static volatile int update_state;
static volatile unsigned long long update_got;
static volatile int update_stop;

// The release that is installed (release.txt's first line); false when there is none, or it is not a version.
static bool
update_installed(char* out, size_t size) {
  out[0] = 0;
  if(FILE* f = fopen(UPDATE_RELEASE, "r")) {
    if(!fgets(out, (int)size, f)) {
      out[0] = 0;
    }
    fclose(f);
  }
  out[strcspn(out, "\r\n")] = 0;
  return out[0] && update_newer("0", out);
}

// The installer that waits in UPDATE_DIR, as UPDATE_READY says it (and its file is of that size).
static bool
update_waiting(char* version, size_t vsize, unsigned long long* bytes, char* sha, size_t ssize) {
  char line[160] = "", v[40] = "", h[80] = "";
  struct stat st;
  FILE* f = fopen(UPDATE_READY, "r");

  if(!f) {
    return false;
  }
  const bool read = fgets(line, sizeof(line), f) && sscanf(line, "%39s %llu %79s", v, bytes, h) == 3;
  fclose(f);
  if(!read || strlen(h) != 64 || !update_newer("0", v) || stat(UPDATE_ELF, &st) || (unsigned long long)st.st_size != *bytes) {
    return false;
  }
  snprintf(version, vsize, "%s", v);
  snprintf(sha, ssize, "%s", h);
  return true;
}

static bool
update_failed_before(const char* version) {
  char line[64] = "";
  if(FILE* f = fopen(UPDATE_FAILED, "r")) {
    if(!fgets(line, sizeof(line), f)) {
      line[0] = 0;
    }
    fclose(f);
  }
  line[strcspn(line, "\r\n")] = 0;
  return line[0] && !strcmp(line, version);
}

// The times the browser has left for this release's installer (UPDATE_TRIES); 0 when the count is another's.
static int
update_tries(const char* version) {
  char v[40] = "";
  int count = 0;
  if(FILE* f = fopen(UPDATE_TRIES, "r")) {
    if(fscanf(f, "%39s %d", v, &count) != 2) {
      count = 0;
    }
    fclose(f);
  }
  return count > 0 && !strcmp(v, version) ? count : 0;
}

static bool
update_write(const char* path, const char* text) {
  char fresh[200];
  snprintf(fresh, sizeof(fresh), "%s.new", path);
  const int fd = open(fresh, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  const size_t n = strlen(text);
  bool ok = fd >= 0 && write(fd, text, n) == (ssize_t)n;
  if(fd >= 0) {
    close(fd);
  }
  ok = ok && !rename(fresh, path);
  if(!ok) {
    unlink(fresh);
  }
  return ok;
}

static void*
update_leave_thread(void*) {
  sleep(5);  // (the words on the bar read)
  WKRunLoopCallOnMainThread(stop, (void*)1);
  return 0;
}

// Main thread, at the first page: an installer of a newer release waiting? Then it is handed to update_after,
// and the browser leaves. True when it is leaving.
static bool
update_apply(void) {
  char have[40], version[40], sha[80], ask[240];
  unsigned long long bytes = 0;

  if(!update_installed(have, sizeof(have)) || !update_waiting(version, sizeof(version), &bytes, sha, sizeof(sha))) {
    return false;
  }
  if(!update_newer(have, version) || update_failed_before(version)) {
    POC_LOG("update: the installer that waits is of %s, the installed release is %s: removed", version, have);
    unlink(UPDATE_ELF);
    unlink(UPDATE_READY);
    return false;
  }
  const int tries = update_tries(version);
  if(tries >= UPDATE_TRIES_MAX) {
    char line[48];
    snprintf(line, sizeof(line), "%s\n", version);
    update_write(UPDATE_FAILED, line);
    unlink(UPDATE_ELF);
    unlink(UPDATE_READY);
    unlink(UPDATE_TRIES);
    POC_LOG("update: the browser left %d times for release %s and %s is still the installed one: not tried again by "
            "itself (its installer can be sent by hand)", tries, version, have);
    return false;
  }
  snprintf(ask, sizeof(ask), "%s %d\n", version, tries + 1);
  bool ok = update_write(UPDATE_TRIES, ask);  // counted first: with no count there is no leaving
  snprintf(ask, sizeof(ask), "%s %llu %s %d %d\n", version, bytes, sha, (int)getpid(), (int)getppid());
  ok = ok && update_write(UPDATE_ASK, ask);
  const size_t size = (size_t)(update_after_elf_end - update_after_elf);
  ok = ok && loader_send(update_after_elf, size);
  if(!ok) {
    unlink(UPDATE_ASK);
    POC_LOG("update: release %s waits, but the request could not be written or no payload loader took update_after.elf: "
            "not installed this time", version);
    return false;
  }
  POC_LOG("update: release %s (%llu bytes) is to be installed over %s (try %d of %d): update_after.elf (%zu bytes) is "
          "with the payload loader, the browser leaves as \"Sair\" does", version, bytes, have, tries + 1, UPDATE_TRIES_MAX, size);
  update_state = UPDATE_LEAVING;
  char said[200];
  snprintf(said, sizeof(said), lang_en() ? LM("Installing BrowserPS5 RN %s. The browser closes and opens again by itself.")
                                         : "A instalar o BrowserPS5 RN %s. O browser fecha e volta a abrir sozinho.", version);
  bar_notify(said, "arrow-download");
  pthread_t leave;
  if(pthread_create(&leave, 0, update_leave_thread, 0)) {
    stop((void*)1);
  } else {
    pthread_detach(leave);
  }
  return true;
}

// Its own thread: GitHub asked, and a newer release's installer fetched and checked.
static void*
update_thread(void*) {
  struct sched_param param;
  int policy;
  if(!pthread_getschedparam(pthread_self(), &policy, &param)) {
    param.sched_priority = 760;  // under the browser's own threads (700): a larger number is a lower priority
    pthread_setschedparam(pthread_self(), policy, &param);
  }
  for(int s = 0; s < UPDATE_FIRST_S && !update_stop; s++) {
    sleep(1);
  }
  char have[40], agent[80], waiting[40], sha[80];
  unsigned long long bytes = 0;
  struct stat st;
  (void)st;
  if(update_stop || !update_installed(have, sizeof(have))) {
    POC_LOG("update: no release file (a build sent by hand): no updates are looked for");
    update_state = UPDATE_NONE;
    return 0;
  }
  if(update_waiting(waiting, sizeof(waiting), &bytes, sha, sizeof(sha)) && update_newer(have, waiting)) {
    POC_LOG("update: release %s waits (%llu bytes): it is installed the next time the browser is opened", waiting, bytes);
    update_state = UPDATE_READY_NOW;
    return 0;
  }
#ifndef SHOW_UPDATE
  if(!stat(UPDATE_CHECKED_FILE, &st) && time(0) - st.st_mtime < UPDATE_EVERY_S && time(0) >= st.st_mtime) {
    POC_LOG("update: looked %ld min ago: not again yet", (long)(time(0) - st.st_mtime) / 60);
    update_state = UPDATE_NONE;
    return 0;
  }
#endif
  update_state = UPDATE_LOOKING;
  mkdir(UPDATE_DIR, 0755);
  snprintf(agent, sizeof(agent), "BrowserPS5/%s", have);
  char* text = 0;
  size_t length = 0;
  int status = 0, err = 0;
  update_t u;
  const double began = poc_now_ms();
  if(update_net_feed(UPDATE_FEED, agent, &text, &length, &status, &err)) {
    POC_LOG("update: the list of releases did not come (HTTP %d, curl %d) in %.0f ms: another time", status, err,
            poc_now_ms() - began);
    update_state = UPDATE_NOT_NOW;
    return 0;
  }
  const int found = update_from_releases(text, length, UPDATE_REPO, &u);
  free(text);
  if(const int fd = open(UPDATE_CHECKED_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644); fd >= 0) {
    close(fd);
  }
  if(found || !update_newer(have, u.version)) {
    POC_LOG("update: %s is installed and the newest release is %s (%zu bytes of list in %.0f ms): nothing to do", have,
            found ? "none with an installer" : u.version, length, poc_now_ms() - began);
    update_state = UPDATE_NONE;
    return 0;
  }
  if(update_failed_before(u.version)) {
    POC_LOG("update: release %s is newer than %s, but it is one that did not install by itself before (%s): not fetched again",
            u.version, have, UPDATE_FAILED);
    update_state = UPDATE_NONE;
    return 0;
  }
  POC_LOG("update: release %s is newer than %s: its installer (%llu bytes) is fetched", u.version, have, u.size);
  update_state = UPDATE_FETCHING;
  unlink(UPDATE_READY);
  const double fetch = poc_now_ms();
  const int got = update_net_file(u.url, agent, UPDATE_ELF ".part", u.size, u.sha256, &update_got, &update_stop, &status, &err);
  if(got) {
    POC_LOG("update: the installer of %s %s (HTTP %d, curl %d, %llu of %llu bytes): another time", u.version,
            got == -2 ? "came and is NOT the release's file (its size or its SHA-256): removed" : "did not come", status, err,
            (unsigned long long)update_got, u.size);
    update_state = UPDATE_NOT_NOW;
    return 0;
  }
  char hex[65], line[160];
  for(int i = 0; i < 32; i++) {
    snprintf(hex + 2 * i, 3, "%02x", u.sha256[i]);
  }
  const int n = snprintf(line, sizeof(line), "%s %llu %s\n", u.version, u.size, hex);
  const int fd = open(UPDATE_READY ".new", O_WRONLY | O_CREAT | O_TRUNC, 0644);
  bool ok = fd >= 0 && write(fd, line, (size_t)n) == n;
  if(fd >= 0) {
    close(fd);
  }
  ok = ok && !rename(UPDATE_ELF ".part", UPDATE_ELF) && !rename(UPDATE_READY ".new", UPDATE_READY);
  if(!ok) {
    POC_LOG("update: the installer of %s came whole, but could not be kept (%s): another time", u.version, strerror(errno));
    unlink(UPDATE_ELF ".part");
    unlink(UPDATE_ELF);
    unlink(UPDATE_READY ".new");
    update_state = UPDATE_NOT_NOW;
    return 0;
  }
  POC_LOG("update: release %s is ready (%llu bytes in %.1f s, its size and its SHA-256 the release's): it is installed the "
          "next time the browser is opened", u.version, u.size, (poc_now_ms() - fetch) / 1e3);
  update_state = UPDATE_READY_NOW;
  return 0;
}

// Main thread: the first page of the session has loaded.
static void
update_first_page(void) {
  static bool once;
  if(once) {
    return;
  }
  once = true;
#ifdef UPDATES_ON
  if(!access(UPDATE_OFF_FILE, F_OK)) {
    POC_LOG("update: %s is there: no updates are looked for", UPDATE_OFF_FILE);
    update_state = UPDATE_NONE;
    return;
  }
  if(update_apply()) {
    return;
  }
  pthread_t thread;
  if(!pthread_create(&thread, 0, update_thread, 0)) {
    pthread_detach(thread);
  }
#else
  update_state = UPDATE_NONE;  // a test build: nothing is looked for
#endif
}
#endif

static void
start_open(void) {
  char* recent = history_recent_json(HISTORY_FILE, 10);
  text_t t = {};

  text_add(&t, "{\"favorites\": ");
  text_add(&t, favorites_json && favorites_json[0] == '[' ? favorites_json : "[]");
  text_add(&t, ", \"recent\": ");
  text_add(&t, recent ? recent : "[]");
  text_add(&t, ", \"theme\": ");
  text_json(&t, theme_now());
  text_printf(&t, ", \"lang\": \"%s\"", lang_code());
  text_add(&t, ", \"engine\": ");
  text_json(&t, settings_engine(&settings)->name);
  text_add(&t, ", \"search\": ");
  text_json(&t, settings_engine(&settings)->id);
  text_printf(&t, ", \"welcomed\": %d", settings.welcomed ? 1 : 0);
  text_printf(&t, ", \"restore\": %d", session_offer());
#ifdef APP_RN
  // The console's own services, as tiles after the favorites (start-rn.js).
  static bool websrv_told;
  const bool websrv = local_port_answers(8080);
  if(websrv != websrv_told) {
    websrv_told = websrv;
    POC_LOG("start page: websrv (127.0.0.1:8080) %s", websrv ? "answers: its tile is shown" : "does not answer");
  }
  text_add(&t, ", \"services\": [");
  if(websrv) {
    text_add(&t, "{\"name\": \"Homebrew\", \"url\": \"http://127.0.0.1:8080/\", \"note\": \"websrv \xc2\xb7 PS5\"}");
  }
  text_add(&t, "]");
  games_json(&t);
#endif
  text_add(&t, "}");
  free(recent);
  if(!t.failed && write_page(START_PAGE, start_html, "@START@", t.s)) {
    char url[256];
    snprintf(url, sizeof(url), "%s?%u", START_URL, ++page_serial);
    preferences_apply(true);
    load_url(url);
  }
  free(t.s);
  leave_bar();
}


static void
settings_open(const char* section) {
  static char settings_text[16384];
  char* history = history_json(HISTORY_FILE, 500);
  char* list = ext_list_json(&extensions);
  text_t t = {};

  // Only the sections' names (they go into the URL too).
  bool known = *section && strlen(section) < 20;
  for(const char* p = section; *p; p++) {
    known &= *p >= 'a' && *p <= 'z';
  }
  if(!known) {
    section = "geral";
  }
  if(!settings_json(&settings, settings_text, sizeof(settings_text))) {
    snprintf(settings_text, sizeof(settings_text), "{}");
  }
  text_add(&t, "{\"settings\": ");
  text_add(&t, settings_text);
  text_add(&t, ", \"season\": ");
  text_json(&t, theme_season());
  text_add(&t, ", \"history\": ");
  text_add(&t, history ? history : "[]");
  text_add(&t, ", \"favorites\": ");
  text_add(&t, favorites_json && favorites_json[0] == '[' ? favorites_json : "[]");
  text_add(&t, ", \"extensions\": ");
  text_add(&t, list ? list : "[]");
  text_add(&t, ", \"pending\": ");
  pending_json(&t);
  text_add(&t, ", \"about\": {\"version\": ");
  text_json(&t, browser_version());
#ifdef APP_RN
  text_printf(&t, ", \"engine\": \"%s\"}, \"section\": ",
              lang_en() ? LM("WebKit 627.1.7, ported to the PS5") : "WebKit 627.1.7, portado para a PS5");
#else
  text_add(&t, ", \"engine\": \"WebKit, porta PlayStation\"}, \"section\": ");
#endif
  text_json(&t, section);
  text_add(&t, ", \"downloads\": ");
  downloads_json(&t);
#ifdef APP_RN
  text_add(&t, ", \"delete_files\": true");  // "Apagar" on each (download_delete())
  text_add(&t, ", \"run_payloads\": true");  // "Executar" on a finished .elf (download_run_payload())
#endif
#ifdef APP_RN
  {  // the sites that asked to show notifications, and the user's answer: only an engine with Notification asks
    text_t sites = {};
    if(notify_sites_json(&sites) && !sites.failed && sites.s) {
      text_add(&t, ", \"notify\": ");
      text_add(&t, sites.s);
    }
    free(sites.s);
  }
#endif
  {
    char* logins = logins_list_json();  // origins and user names, no password
    text_add(&t, ", \"logins\": ");
    text_add(&t, logins ? logins : "[]");
    free(logins);
  }
#ifdef REMOTE_UI
  {  // the torrents' card, and TorBox's section (only in this build)
    char hint[8];
    const bool has = torbox_key_hint(hint, sizeof(hint));
    text_add(&t, ", \"torrents\": true, \"ps5settings\": true, \"unzip\": true, \"torbox\": {\"key\": ");
    text_add(&t, has ? "true" : "false");
    text_add(&t, ", \"hint\": ");
    text_json(&t, has ? hint : "");
    text_add(&t, "}");
#ifndef WEBKIT_BEFORE_2C2
    // The "pages inside the browser app" switch: only with a WebKit that
    // can run the WebProcess in the title.
    text_add(&t, ", \"web_title\": true");
#endif
  }
#endif
  text_add(&t, "}");
  free(history);
  free(list);
  if(!t.failed && write_page(SETTINGS_PAGE, settings_html, "@SETTINGS@", t.s)) {
    char url[256];
    snprintf(url, sizeof(url), "%s?%u#%s", SETTINGS_URL, ++page_serial, section);
    preferences_apply(true);
#ifdef APP_RN
    if(!settings_tab_open(url))  // over a web page: a tab of their own
#endif
    load_url(url);
    POC_LOG("settings open at %s", section);
  }
  free(t.s);
  menu_hide();
  leave_bar();
}


// ⌂ and the start: the home page of the settings.
static void
home_open(void) {
  if(!strcmp(settings.home_kind, "url") && url_is_web(settings.home_url)) {
    load_url(settings.home_url);
    leave_bar();
  } else {
    start_open();
  }
}


// ---------------------------------------------------------------------------
// The commands. From the bar and the menu (ui_command), our own pages and
// any page (page_command), each from its own pages only.

static void
setting_apply(const char* key) {
#define KEY(k) !strcmp(key, k)
  if(KEY("theme")) {
    theme_apply();
  } else if(KEY("lang")) {
    lang_apply();
  } else if(KEY("javascript") || KEY("images") || KEY("autoplay")) {
    preferences_apply(own_page_shown(0));
    reload_web_page();
  } else if(KEY("popups")) {
    preferences_apply(own_page_shown(0));
    extensions_apply();
  } else if(KEY("desktop")) {
    user_agent_apply();
    reload_web_page();
  } else if(KEY("search")) {
    address_set_search(settings_engine(&settings)->prefix);
  } else if(KEY("third_party_cookies")) {
    cookies_apply();
  } else if(KEY("cursor_speed") || KEY("scroll_speed")) {
    input_set_speed(settings.cursor_speed, settings.scroll_speed);
  } else if(KEY("favbar")) {
    layout();
  } else if(KEY("zoom")) {
    zoom_apply();
    bar_update(0);
  } else if(KEY("phone")) {
    phone_setting();
  } else if(KEY("triggers") || KEY("lightbar") || KEY("cinema")) {
    feel_apply();
  } else if(KEY("devtools")) {
    extensions_apply();
    reload_web_page();
  } else if(KEY("gamepad")) {
    if(!settings.gamepad) {
      game_set(false);
      game_wanted = false;
    }
    extensions_apply();
    reload_web_page();
  } else if(KEY("adblock") || KEY("dark_sites") || KEY("fullscreen") || KEY("logins")) {
    extensions_apply();
    reload_web_page();
  }
#undef KEY
}


// set?<key>=<value>
static void
setting_command(const char* argument) {
  static char text[2048];

  address_decode(argument, text, sizeof(text));
  char* value = strchr(text, '=');
  if(!value) {
    return;
  }
  *value++ = 0;
  if(!strcmp(text, "last_url") || !strcmp(text, "ad_allow")) {  // the app's own
    return;
  }
  int changed = settings_set(&settings, text, value);
  if(changed < 0) {
    POC_LOG("setting %s: that value is not taken", text);
    return;
  }
  // An engine picked (the settings or the start page), even the one there
  // was: kept from now on, whatever the default (settings_load()).
  if(!strcmp(text, "search") && settings_set(&settings, "search_chosen", "1") == 1) {
    changed = 1;
  }
  if(changed) {
    settings_store();
    POC_LOG("setting %s changed", text);
    setting_apply(text);
  }
}


// The ad blocker's exceptions changed: kept, and the pages get them on
// their next load (this one now).
static void
ad_allow_changed(const char* what, const char* site) {
  settings_store();
  POC_LOG("ad blocker: %s %s (%d bytes of exceptions)", what, site, (int)strlen(settings.ad_allow));
  extensions_apply();
#ifdef ENGINE_PLUS
  // WebKit's own rules have the exceptions in them: the page loads again
  // once they are made again (rules_compiled()).
  if(rules_store && ad_count) {
    rules_reload = true;
    rules_hosts_compile();
    return;
  }
#endif
  reload_web_page();
}


// The radial's "Anúncios aqui": this site's ads allowed, or blocked again
// (the blocker on, if it was off: blocked here is what was asked).
static void
ad_site_toggle(void) {
  char url[4096], host[256], site[256];
  take_url(WKPageCopyCommittedURL(page(PAGE)), url, sizeof(url));
  if(!url_is_web(url) || settings_site(url_host(url, host, sizeof(host)), site, sizeof(site))) {
    return;
  }
  bool blocked_here = settings.adblock && !settings_ad_allowed(&settings, site);
  if(blocked_here) {
    if(settings_ad_allow(&settings, site, 1) < 0) {
      bar_notify("Há sites demais na lista de exceções", "shield-checkmark");
      return;
    }
    bar_notify("Anúncios permitidos neste site", "shield-checkmark");
    ad_allow_changed("allowed on", site);
    return;
  }
  settings_ad_allow(&settings, site, 0);
  if(!settings.adblock) {
    settings.adblock = 1;
  }
  bar_notify("Anúncios bloqueados neste site", "shield-checkmark");
  ad_allow_changed("blocking again on", site);
}


// adallow?remove:<site> (the settings' list).
static void
ad_allow_command(const char* argument) {
  char text[300], site[256];
  address_decode(argument, text, sizeof(text));
  if(strncmp(text, "remove:", 7) || settings_site(text + 7, site, sizeof(site))) {
    return;
  }
  if(settings_ad_allow(&settings, site, 0) > 0) {
    ad_allow_changed("blocking again on", site);
  }
}


// clear?history,cookies,cache
static void
clear_command(const char* argument) {
  char what[256];
  WKHTTPCookieStoreRef cookies = WKWebsiteDataStoreGetHTTPCookieStore(data_store);

  address_decode(argument, what, sizeof(what));
  for(char* item = strtok(what, ","); item; item = strtok(0, ",")) {
    if(!strcmp(item, "history")) {
      history_clear(HISTORY_FILE);
      settings.last_url[0] = 0;
      settings_store();
      session_forget();  // the tabs of the last time go with it
    } else if(!strcmp(item, "cookies")) {
      WKHTTPCookieStoreDeleteAllCookies(cookies, 0, data_removed);
      WKWebsiteDataStoreRemoveLocalStorage(data_store, 0, data_removed);
      WKWebsiteDataStoreRemoveAllIndexedDatabases(data_store, 0, data_removed);
      WKWebsiteDataStoreRemoveAllServiceWorkerRegistrations(data_store, 0, data_removed);
    } else if(!strcmp(item, "cache")) {
      WKWebsiteDataStoreRemoveNetworkCache(data_store, 0, data_removed);
      WKWebsiteDataStoreRemoveMemoryCaches(data_store, 0, data_removed);
      WKWebsiteDataStoreRemoveAllFetchCaches(data_store, 0, data_removed);
    } else if(!strcmp(item, "favorites")) {
      // "Apagar todos" (start page, settings): the bar keeps the list and
      // sends the empty one back to be saved (favorites-changed).
      run_script(BAR, "removeAllFavorites()", 0, ignore_result);
    } else {
      continue;
    }
    POC_LOG("browsing data cleared: %s", item);
  }
}


// ext?install, ext?cancel, ext?on:<id>, ext?off:<id>, ext?remove:<id>
static void
ext_command(const char* argument) {
  char text[64];

  address_decode(argument, text, sizeof(text));
  if(!strcmp(text, "install")) {
    if(pending_source) {
      int id = ext_install(&extensions, pending_source, &pending_meta);
      POC_LOG("extension \"%s\" %s", pending_meta.name,
              id > 0 ? "installed" : "NOT installed");
      free(pending_source);
      pending_source = 0;
      extensions_apply();
      settings_open("extensoes");
    }
    return;
  }
  if(!strcmp(text, "cancel")) {
    free(pending_source);
    pending_source = 0;
    return;
  }
  const char* colon = strchr(text, ':');
  int id = colon ? atoi(colon + 1) : 0;
  int err = !id ? -1
          : !strncmp(text, "on:", 3) ? ext_set_on(&extensions, id, 1)
          : !strncmp(text, "off:", 4) ? ext_set_on(&extensions, id, 0)
          : !strncmp(text, "remove:", 7) ? ext_remove(&extensions, id) : -1;
  if(!err) {
    POC_LOG("extension %d: %.*s", id, (int)(colon - text), text);
    extensions_apply();
  }
}


static void
go_to(const char* encoded) {
  static char typed[4096];
  static char url[8192];
  char shown[512];

  address_decode(encoded, typed, sizeof(typed));
  address_kind_t kind = address_to_url(typed, url, sizeof(url));
  if(kind != ADDRESS_NONE) {
    POC_LOG("go at %.0f ms: %s %s", poc_now_ms() - app.start_ms,
            kind == ADDRESS_SEARCH ? "search" : "address",
            url_for_log(url, shown, sizeof(shown)));
#ifdef APP_RN
    // BrowserPS5 RN (2026-11-10 22:20): its site open in another tab: that
    // tab, as it is (WhatsApp Web twice is two sessions fighting).
    const int there = tab_with_host(url);
    if(there >= 0 && there != tab_in_front()) {
      tab_switch(there);
      leave_bar();
      return;
    }
#endif
    load_url(url);
  }
  leave_bar();
}


// remove?<url>: a favorite taken away on our pages; the bar keeps the list
// and sends it back.
static void
favorite_remove(const char* encoded) {
  char removed[4096];
  text_t t = {};

  address_decode(encoded, removed, sizeof(removed));
  text_add(&t, "removeFavorite(");
  text_json(&t, removed);
  text_add(&t, ")");
  text_run(BAR, &t);
}


#ifdef REMOTE_UI
// The menu's "Abrir ao lado do jogo" (side_card.h, docs/10): the page's
// address to the system's browser card, the one browser the system docks
// next to a game, through a notification whose button opens it. Pages of
// the web only (ours are not there). Not tried on the console yet.
static void side_card_open(void);

// One of the PS5's own settings pages (Definições, Sistema;
// docs/14-definicoes-da-ps5.md): the same notification, its button a
// pssettings: link. The page names the page; the links are side_card.c's
// four. Opening one puts the title in the background, with the PS button's
// pause. Not tried on the console yet.
static void
ps5_settings_open(const char* name) {
  const char* link = side_card_settings_link(name);
  if(!link) {
    POC_LOG("ps5 settings: no such page");
    return;
  }
  const int err = lang_en() ? side_card_post_link(link, LM("Open the PS5's settings"), LM("Open"))
                            : side_card_post_link(link, "Abrir as definições da PS5", "Abrir");
  POC_LOG("ps5 settings: %s: 0x%x", name, (unsigned)err);
  if(err) {
    bar_notify("O sistema não mostrou a notificação", "dismiss");
  } else {
    bar_notify("Carrega em «Abrir» na notificação para ir às definições da PS5", "settings");
  }
}

static void
side_card_open(void) {
  char url[4096];
  char shown[512];

  take_url(WKPageCopyActiveURL(page(PAGE)), url, sizeof(url));
  if(!url_is_web(url)) {
    bar_notify("Só as páginas da web abrem ao lado do jogo", "dismiss");
    return;
  }
#ifdef APP_RN
  // 2026-11-13: the card's browser under a phone's name (settings.side_mobile, on unless turned off): the
  // sites' phone pages are lighter and fit the card's width. The system's engine itself cannot be made faster.
  side_card_user_agent(settings.side_mobile
                       ? "Mozilla/5.0 (iPhone; CPU iPhone OS 17_0 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) "
                         "Version/17.0 Mobile/15E148 Safari/604.1"
                       : "");
#endif
  const int err = lang_en() ? side_card_post(url, LM("Open next to the game"), LM("Open"))
                             : side_card_post(url, "Abrir ao lado do jogo", "Abrir");
  POC_LOG("side card: %s: 0x%x", url_for_log(url, shown, sizeof(shown)), (unsigned)err);
  if(err == -1) {
    bar_notify("Endereço longo demais para o cartão do sistema", "dismiss");
  } else if(err) {
    bar_notify("O sistema não mostrou a notificação", "dismiss");
  } else {
    bar_notify("Carrega em «Abrir» na notificação: a página abre no cartão do sistema, ao lado do jogo",
               "games");
  }
}
#endif


// What the bar or the menu asked for (their send()), on the main thread.
static void
ui_command(const char* command) {
  const char* argument = strchr(command, '?');
  size_t name_length = argument ? (size_t)(argument - command) : strlen(command);
  const char* arg = argument ? argument + 1 : "";

#define IS(name) (name_length == strlen(name) && !strncmp(command, name, name_length))
  if(IS("go")) {
    menu_hide();
    go_to(arg);
  } else if(IS("back")) {
#ifdef APP_RN
    if(!settings_tab_back())  // the settings' own tab: closed
#endif
    WKPageGoBack(page(PAGE));
  } else if(IS("forward")) {
    WKPageGoForward(page(PAGE));
  } else if(IS("reload")) {
    WKPageReload(page(PAGE));
  } else if(IS("stop")) {
#ifdef APP_RN
    app_nav_page = 0;  // the user's own stop: not asked for again
#endif
    WKPageStopLoading(page(PAGE));
  } else if(IS("favorites-changed")) {  // the list is read back (no size limit)
    run_script(BAR, "JSON.stringify(favorites)", 0, favorites_got);
  } else if(IS("start") || IS("favorites-page")) {
    menu_hide();
    start_open();
  } else if(IS("home")) {
    menu_hide();
    home_open();
  } else if(IS("settings")) {
    char section[32];
    address_decode(arg, section, sizeof(section));
    settings_open(section);
  } else if(IS("menu")) {
    menu_toggle();
  } else if(IS("menu-do")) {  // a menu row that closes it: the command in it
    static char inner[1024];
    address_decode(arg, inner, sizeof(inner));
    menu_hide();
    if(strncmp(inner, "menu", 4)) {
      ui_command(inner);
    }
  } else if(IS("savemedia")) {  // the bar's "Guardar": a video or a sound by itself
    if(media_shown(page(PAGE))) {
      // A link to itself with download: WebKit asks for the download
      // (decidePageNavigation) and the downloads ask where to keep it.
      last_gesture_ms = poc_now_ms();
      run_script(PAGE,
                 "(function(){var a=document.createElement('a');a.href=location.href;"
                 "a.download='';(document.body||document.documentElement).appendChild(a);"
                 "a.click();a.remove();})()",
                 0, ignore_result);
      POC_LOG("keeping the video or sound shown");
    }
  } else if(IS("zoom")) {
    char how[8];
    address_decode(arg, how, sizeof(how));
    zoom_step(how);
  } else if(IS("set")) {
    setting_command(arg);
  } else if(IS("favorite")) {  // the menu's row: as the star
    run_script(BAR, "star.click()", 0, ignore_result);
  } else if(IS("tabs")) {
    switcher_toggle();
  } else if(IS("newtab")) {
    menu_hide();
    tab_new(0);
#ifdef APP_RN
  } else if(IS("find")) {  // the menu's second row of tiles (menu.html, data-rn)
    find_open();
  } else if(IS("reader")) {
    run_script(PAGE, ext_reader_js, 0, reader_said);
  } else if(IS("translate")) {
    translate_page();
  } else if(IS("reopen")) {  // the strip's button: the tab closed last, again
    tab_reopen();
  } else if(IS("newprivate")) {
    tab_private_next = true;
    if(!tab_new(0)) {
      tab_private_next = false;
      bar_notify(lang_en() ? LM("No room for another tab") : "Não cabem mais separadores", "window-new");
    }
  } else if(IS("tab")) {  // the bar's tab strip (chrome.html): one in front
    menu_hide();
    tab_switch(atoi(arg));
  } else if(IS("tab-close")) {
    tab_close(atoi(arg));
  } else if(IS("fav")) {  // a favorite of the bar: its tab, or a tab of its own
    menu_hide();
    favorite_open(arg);
#endif
  } else if(IS("fullscreen")) {  // the menu's "Ecrã inteiro"
    fullscreen_toggle();
#ifdef REMOTE_UI
  } else if(IS("sidecard")) {  // the menu's "Abrir ao lado do jogo"
    side_card_open();
#endif
#ifdef APP_RN
  } else if(IS("padkeys")) {  // the menu's "Comando como teclado" (padkeys_toggle())
    menu_hide();
    padkeys_toggle();
  } else if(IS("gamepin")) {  // the menu's "Guardar no jogo" (game_pin_current())
    menu_hide();
    game_pin_current();
#endif
  } else if(IS("quit")) {
    POC_LOG("the menu: quitting");
    stop(0);
  } else {
    POC_LOG("the bar sent an unknown command: %.*s", (int)name_length, command);
  }
#undef IS
}


// style?cinema|ciborgue|skip: the look chosen at the first start (the start
// page's welcome): a theme and how the controller sounds; skip keeps them.
// Asked once (settings.welcomed).
static void
style_command(const char* argument) {
  char style[16];

  address_decode(argument, style, sizeof(style));
  if(!strcmp(style, "cinema")) {
    setting_command("theme=champanhe");
    setting_command("sounds=0");
  } else if(!strcmp(style, "ciborgue")) {
    setting_command("theme=neon");
    setting_command("sounds=1");
    setting_command("magnet=1");
  }
  setting_command("welcomed=1");
  POC_LOG("first start: style %s", style);
  start_open();
}


#ifdef APP_RN
// 2026-11-14 (the engine's patches read again, poc/webkit/security): is this address this console's own? A
// jailbroken PS5 has services that take commands from whoever connects, with no password (websrv on 8080 starts a
// program with one GET). As the engine reads the address: 2130706433, 127.1 and 0x7f.0.0.1 are 127.0.0.1 for it.
static bool
url_this_console(const char* url) {
  char host[256];
  WKURLRef address = WKURLCreateWithUTF8CString(url);

  host[0] = 0;
  if(address) {
    take_string(WKURLCopyHostName(address), host, sizeof(host));
    WKRelease(address);
  }
  return !strcmp(host, "127.0.0.1") || !strcasecmp(host, "localhost") || !strcmp(host, "[::1]") || !strcmp(host, "::1");
}

// A tab a SITE asks for (window.open(), a link for a new window, the link menu's "new tab") is opened by the app,
// and what the app opens is "the user's own navigation" for the engine (Sec-Fetch-Site "none":
// CachedResourceLoader, isRequestFromClientOrUserInput()): the one kind of request the engine's patch lets
// connect to this console (CurlRequest.cpp). So a page could reach the console's services after all, with one
// press anywhere on it: window.open('http://127.0.0.1:8080/...'). A site's tab is not opened there; a page of the
// console's own service, which the user opened, may ask for one.
static bool
site_may_open(const char* to) {
  char shown[2048];

  if(!url_this_console(to)) {
    return true;
  }
  take_url(WKPageCopyCommittedURL(page(PAGE)), shown, sizeof(shown));
  if(url_this_console(shown)) {
    return true;
  }
  POC_LOG("a site asked for a tab on this console's own services: not opened");
  bar_notify(lang_en() ? LM("A site tried to open a service of this console: not opened")
                       : "Um site tentou abrir um serviço desta consola: não foi aberto", "shield-checkmark");
  return false;
}
#endif

#ifdef APP_RN
// 2026-11-14: a window a page asked for by itself (tabs_js, browserps5:popup). The site of an address: its
// host's last two names, three under a country's com/co/org/net/gov/edu/ac (bbc.co.uk, globo.com.br).
static void
url_site(const char* url, char* site, size_t size) {
  char host[256] = "";
  site[0] = 0;
  WKURLRef u = WKURLCreateWithUTF8CString(url);
  if(u) {
    WKStringRef h = WKURLCopyHostName(u);
    if(h) {
      WKStringGetUTF8CString(h, host, sizeof(host));
      WKRelease(h);
    }
    WKRelease(u);
  }
  for(char* c = host; *c; c++) {
    *c = (char)tolower((unsigned char)*c);
  }
  const char* labels[64];
  int n = 0;
  labels[n++] = host;
  for(char* c = host; *c && n < 64; c++) {
    if(*c == '.' && c[1]) {
      labels[n++] = c + 1;
    }
  }
  int keep = 2;
  if(n > 2 && strlen(labels[n - 1]) == 2) {
    static const char* const second[] = {"com.", "co.", "org.", "net.", "gov.", "edu.", "ac."};
    for(const char* s : second) {
      if(!strncmp(labels[n - 2], s, strlen(s))) {
        keep = 3;
      }
    }
  }
  snprintf(site, size, "%s", labels[n > keep ? n - keep : 0]);
}

// The sign-in pages sites open in a window of their own: another site's, but asked for by the user.
static bool
url_signs_in(const char* url) {
  static const char* const hosts[] = {
    "accounts.google.com", "appleid.apple.com", "login.microsoftonline.com", "login.live.com", "github.com",
    "www.facebook.com", "m.facebook.com", "api.twitter.com", "twitter.com", "x.com", "discord.com",
    "id.twitch.tv", "steamcommunity.com", "www.paypal.com", "auth.riotgames.com", "my.account.sony.com",
    "ca.account.sony.com", "www.amazon.com", "oauth.telegram.org", "www.linkedin.com", "accounts.spotify.com",
  };
  char host[256] = "";
  WKURLRef u = WKURLCreateWithUTF8CString(url);
  if(u) {
    WKStringRef h = WKURLCopyHostName(u);
    if(h) {
      WKStringGetUTF8CString(h, host, sizeof(host));
      WKRelease(h);
    }
    WKRelease(u);
  }
  for(const char* h : hosts) {
    if(!strcasecmp(host, h)) {
      return true;
    }
  }
  return false;
}

static unsigned popups_blocked;

static void
popup_asked(const char* to) {
  char shown[2048], from_site[256], to_site[256];
  static double told_ms = -1e9;

  if(!url_is_web(to)) {
    return;
  }
  if(!settings.popups && poc_now_ms() - last_gesture_ms >= 1500) {
    POC_LOG("a window a page asked for with no press: not opened");
    return;
  }
  if(!site_may_open(to)) {
    return;
  }
  take_url(WKPageCopyCommittedURL(page(PAGE)), shown, sizeof(shown));
  url_site(shown, from_site, sizeof(from_site));
  url_site(to, to_site, sizeof(to_site));
  const bool same = from_site[0] && !strcmp(from_site, to_site);
  if(same || settings.popups || url_signs_in(to)) {
    POC_LOG("a window a page asked for (%s): in a new tab", same ? "its own site's" : settings.popups ? "the setting lets sites open windows" : "a sign-in page");
    if(!tab_new(to)) {
      load_url(to);  // no room: in this one
    }
    return;
  }
  popups_blocked++;
  POC_LOG("a window of another site (%s, asked for by %s): not opened (%u so far)", to_site, from_site, popups_blocked);
  if(poc_now_ms() - told_ms > 4000) {  // (such pages ask again at every press)
    told_ms = poc_now_ms();
    bar_notify(lang_en() ? LM("A window of another site was blocked") : "Foi bloqueada uma janela de outro site",
               "shield-checkmark");
  }
}
#endif

// A browserps5: navigation in the page. Only fullscreen comes from any page
// (ext/fullscreen.js runs in all of them); the rest only from our own, in
// their main frame: any site could send them.
static void
page_command(const char* command, bool main_frame) {
  const char* argument = strchr(command, '?');
  size_t name_length = argument ? (size_t)(argument - command) : strlen(command);
  const char* arg = argument ? argument + 1 : "";

#define IS(name) (name_length == strlen(name) && !strncmp(command, name, name_length))
  if(IS("fullscreen")) {
    fullscreen_set(arg[0] == '1');
    return;
  }
#ifdef APP_RN
  if(IS("health")) {  // the start page's console health panel (health_tell())
    if(main_frame) {
      health_tell();
    }
    return;
  }
  if(IS("services")) {  // the start page's "Homebrew": which of the scene's tools listen now (services_tell())
    if(main_frame) {
      services_tell();
    }
    return;
  }
  if(IS("library")) {  // the start page's "Your games": every game of the console (library_tell())
    if(main_frame) {
      library_tell();
    }
    return;
  }
  if(IS("game")) {  // the start page's game hub: the game's trophies, one by one (game_hub())
    if(main_frame) {
      game_hub(arg);
    }
    return;
  }
  if(IS("note-save")) {  // the start page's game hub: the user's notes of the game (game_note_save())
    if(main_frame) {
      game_note_save(arg);
    }
    return;
  }
  if(IS("shot-share")) {  // the start page's game hub: a capture to Discord (shot_share())
    if(main_frame) {
      shot_share(arg);
    }
    return;
  }
  if(IS("discord-forget")) {
    if(main_frame) {
      discord_forget();
    }
    return;
  }
  if(IS("game-unpin")) {  // the start page's game hub: a page kept with the game taken away (game_unpin())
    if(main_frame) {
      game_unpin(arg);
    }
    return;
  }
  if(IS("play")) {  // the start page's game hub: "Play" (game_play())
    if(main_frame) {
      game_play(arg);
    }
    return;
  }
  if(IS("cheat")) {  // the start page's game row: one of etaHEN's cheat files (cheat_ask())
    if(main_frame) {
      cheat_ask(arg);
    }
    return;
  }
  if(IS("patch") || IS("patch-set") || IS("patches")) {  // the start page's patches panel (patch_ask() and the others)
    if(main_frame) {
      IS("patch") ? patch_ask(arg) : IS("patch-set") ? patch_set(arg) : patches_tell(arg);
    }
    return;
  }
#endif
  if(IS("gamepad")) {  // ext/gamepad.js: the page uses the Gamepad API
    game_offer();
    return;
  }
  if(IS("vibrate")) {  // ext/gamepad.js: navigator.vibrate(), the actuator
    vibrate_command(arg);
    return;
  }
  if(IS("newtab")) {  // a link for a new window (tabs_js), right after a press
    static char to[4096];
    address_decode(arg, to, sizeof(to));
    if(url_is_web(to) && poc_now_ms() - last_gesture_ms < 1500) {
#ifdef APP_RN
      if(!site_may_open(to)) {
        return;
      }
#endif
      if(!tab_new(to)) {
        load_url(to);  // no room: in this one
      }
    } else {
      POC_LOG("a new tab with no press, or not for a web page: not taken");
    }
    return;
  }
#ifdef APP_RN
  if(IS("popup")) {  // tabs_js: a window the page asked for by itself
    static char to[4096];
    address_decode(arg, to, sizeof(to));
    popup_asked(to);
    return;
  }
#endif
  if(IS("login-offer")) {  // ext/logins.js, from a site's page (never logged)
    login_offer(arg);
    return;
  }
#ifdef APP_RN
  if(IS("newprivate")) {  // ext/linkmenu.js: the link in a private tab, right after a press
    static char to[4096];
    address_decode(arg, to, sizeof(to));
    if(url_is_web(to) && poc_now_ms() - last_gesture_ms < 1500) {
      if(!site_may_open(to)) {
        return;
      }
      tab_private_next = true;
      if(!tab_new(to)) {
        tab_private_next = false;
      }
    } else {
      POC_LOG("a private tab with no press, or not for a web page: not taken");
    }
    return;
  }
  if(IS("overlay")) {  // ext/linkmenu.js, ext/reader.js: open (1) or closed (0) over the page
    overlay_tab = arg[0] == '1' ? tab_front_id() : -1;
    POC_LOG("overlay: %s over the page (tab %d)", arg[0] == '1' ? "open" : "closed", tab_front_id());
    return;
  }
#endif
  if(!main_frame || !own_page_shown(0)) {
    POC_LOG("a browserps5: navigation from a site, not taken");
    return;
  }
  if(IS("go")) {
    go_to(arg);
  } else if(IS("remove")) {
    favorite_remove(arg);
  } else if(IS("set")) {
    setting_command(arg);
  } else if(IS("phone")) {
    phone_command(arg);
  } else if(IS("system")) {
    system_tell();
#ifdef REMOTE_UI
  } else if(IS("ps5settings")) {
    ps5_settings_open(arg);
#endif
  } else if(IS("adallow")) {
    ad_allow_command(arg);
  } else if(IS("style")) {
    style_command(arg);
  } else if(IS("clear")) {
    clear_command(arg);
  } else if(IS("ext")) {
    ext_command(arg);
  } else if(IS("download-cancel")) {
    download_do("cancel", atoi(arg));
  } else if(IS("download-retry")) {
    download_do("retry", atoi(arg));
  } else if(IS("download-pause")) {
    download_do("pause", atoi(arg));
  } else if(IS("download-open")) {
    download_do("open", atoi(arg));
  } else if(IS("download-unzip")) {
    download_do("unzip", atoi(arg));
#ifdef APP_RN
  } else if(IS("download-run")) {  // "Executar" on a finished .elf, pressed twice
    download_do("run", atoi(arg));
#endif
  } else if(IS("download-delete")) {
    download_do("delete", atoi(arg));
  } else if(IS("choose-folder")) {
    char key[32];
    address_decode(arg, key, sizeof(key));
    if(folder_key(key)) {
      dialog_for_folder(key);
    }
  } else if(IS("start")) {
    start_open();
  } else if(IS("tabs-restore")) {  // start.html: "Reabrir os separadores da última vez"
    if(!tabs_restore()) {
      start_open();  // nothing to reopen any more: the page without the button
    }
#ifdef APP_RN
  } else if(IS("notify-forget")) {
    notify_forget_command(arg);
#endif
  } else if(IS("login-remove")) {
    login_remove_command(arg);
  } else if(IS("settings")) {
    char section[32];
    address_decode(arg, section, sizeof(section));
    settings_open(section);
#ifdef REMOTE_UI
  } else if(IS("torrents")) {  // settings.html, Transferencias: "Torrents"
    torrent_page();
  } else if(IS("torbox-key")) {  // settings.html, TorBox (never logged: a key)
    torbox_command("key", arg);
  } else if(IS("torbox-add")) {
    torbox_command("add", arg);
  } else if(IS("torbox-list")) {
    torbox_command("list", arg);
  } else if(IS("torbox-get")) {
    torbox_command("get", arg);
#endif
  } else {
    POC_LOG("our page sent an unknown command: %.*s", (int)name_length, command);
  }
#undef IS
}


// The navigations of the bar, the menu, the dialog and the keyboard: their
// own pages go, their commands do not (info: the pane).
static void
decideBarNavigation(WKPageRef, WKNavigationActionRef action,
                    WKFramePolicyListenerRef listener, WKTypeRef,
                    const void* info) {
  static char url[65536];  // the keyboard's text is in it
  WKURLRequestRef request = WKNavigationActionCopyRequest(action);

  url[0] = 0;
  if(request) {
    take_url(WKURLRequestCopyURL(request), url, sizeof(url));
    WKRelease(request);
  }
  if(strncmp(url, "browserps5:", 11)) {
    WKFramePolicyListenerUse(listener);
    return;
  }
  WKFramePolicyListenerIgnore(listener);
  if(info == &app.panes[KEYS]) {
    keys_command(url + 11);
  } else if(info == &app.panes[DIALOG]) {
    dialog_command(url + 11);
  } else {
    ui_command(url + 11);
  }
}


#ifdef APP_RN
static void notify_take(WKPageRef p, const char* arg);
#endif

// The page's navigations: browserps5: ones are commands (page_command());
// frames from the ad hosts do not load while the ad blocker is on; <a
// download> and the like become downloads.
static void
decidePageNavigation(WKPageRef p, WKNavigationActionRef action,
                     WKFramePolicyListenerRef listener, WKTypeRef,
                     const void*) {
  static char url[8192];
  WKURLRequestRef request = WKNavigationActionCopyRequest(action);

  url[0] = 0;
  if(request) {
    take_url(WKURLRequestCopyURL(request), url, sizeof(url));
    WKRelease(request);
  }
  WKFrameInfoRef target = WKNavigationActionCopyTargetFrameInfo(action);
  bool main_frame = !target || WKFrameInfoGetIsMainFrame(target);
  if(target) {
    WKRelease(target);
  }
  if(!strncmp(url, "browserps5:", 11)) {
    WKFramePolicyListenerIgnore(listener);
#ifdef APP_RN
    if(!strncmp(url + 11, "notify?", 7)) {  // notify_js: from any tab, the behind ones most
      if(main_frame) {
        notify_take(p, url + 18);
      }
      return;
    }
#endif
    if(p == page(PAGE)) {  // a tab behind asks nothing
      page_command(url + 11, main_frame);
    }
    return;
  }
#ifdef APP_RN
  {  // a part of a download in several parts, asked for by the app itself (multi_ask()): a download at once
    multi_t* owner = 0;
    if(main_frame && multi_asked(url, &owner)) {
      WKFramePolicyListenerDownload(listener);
      return;
    }
    // 2026-11-14: one whose set ended meanwhile (the parts were given up between its asking and here) went on
    // as a navigation of the user's, and became a second download of the file. It is nothing.
    if(strstr(url, "#bps5range=")) {
      POC_LOG("a part nobody waits for any more: not asked for");
      WKFramePolicyListenerIgnore(listener);
      return;
    }
  }
#endif
#ifdef APP_RN
  if(p == page(PAGE) && main_frame && cheat_wanted(url)) {  // cheat_ask(): a download, not a page
    WKFramePolicyListenerDownload(listener);
    return;
  }
#endif
  if(download_retry_url[0] && p == page(PAGE) && main_frame &&
     same_address(url, download_retry_url)) {  // download_retry()
    download_retry_url[0] = 0;
    WKFramePolicyListenerDownload(listener);
    return;
  }
#ifdef REMOTE_UI
  if(!strncasecmp(url, "magnet:", 7)) {  // a torrent's link (torrent.h, torbox.h)
    WKFramePolicyListenerIgnore(listener);
    if(p == page(PAGE) && main_frame) {
      if(settings.torbox_magnets) {
        torbox_take(url);
      } else {
        torrent_open_magnet(url);
      }
    }
    return;
  }
  if(torbox_download_url[0] && p == page(PAGE) && main_frame &&
     same_address(url, torbox_download_url)) {  // torbox_main(): a ready file
    torbox_download_url[0] = 0;
    WKFramePolicyListenerDownload(listener);
    return;
  }
#endif
#ifdef APP_RN
  // The strict blocking (strict_load()): a page of another site takes the tab to an advertising page.
  if(main_frame && strict_count && settings.adblock && url_is_web(url)) {
    const WKFrameNavigationType type = WKNavigationActionGetNavigationType(action);
    const bool by_user = p == app_nav_page && poc_now_ms() - app_nav_ms < 20000;
    char host[256];
    if(!by_user && type != kWKFrameNavigationTypeBackForward && type != kWKFrameNavigationTypeReload
       && strict_host(url, host, sizeof(host)) && !ad_site_allowed(p)) {
      static char from[2048];
      char from_site[256], to_site[256];
      static double told_ms = -1e9;
      take_url(WKPageCopyCommittedURL(p), from, sizeof(from));
      url_site(from, from_site, sizeof(from_site));
      url_site(url, to_site, sizeof(to_site));
      if(strcmp(from_site, to_site)) {
        WKFramePolicyListenerIgnore(listener);
        strict_blocked++;
        POC_LOG("an advertising page (%s, asked for by a page of %s): the tab stays where it is (%u so far)", host,
                from_site[0] ? from_site : "no site", strict_blocked);
        if(p == page(PAGE) && poc_now_ms() - told_ms > 4000) {
          told_ms = poc_now_ms();
          bar_notify(lang_en() ? LM("An advertising page was blocked") : "Foi bloqueada uma página de publicidade",
                     "shield-checkmark");
        }
        return;
      }
    }
  }
#endif
  if(!main_frame && settings.adblock && !ad_site_allowed(p)) {
    char host[256];
    if(ad_host(url_host(url, host, sizeof(host)))) {
      WKFramePolicyListenerIgnore(listener);
      ads_blocked++;
      return;
    }
  }
  if(main_frame) {
    if(p == page(PAGE)) {  // the preferences are all the tabs'
      preferences_apply(own_url(url, START_URL) || own_url(url, SETTINGS_URL));
    }
#if defined(APP_RN) && !defined(SHOW_YT_PS5UA) && !defined(SHOW_YT_TV)
    if(url_is_web(url)) {
      user_agent_page(p, url);  // the name this browser gives that site (YouTube: its own)
    }
#endif
    if(WKNavigationActionShouldPerformDownload(action)) {
      WKFramePolicyListenerDownload(listener);
      return;
    }
  }
  WKFramePolicyListenerUse(listener);
}


// The pages whose main frame shows a video or a sound by itself (its own
// address, WebKit's media document): the bar offers to keep the file
// ("Guardar", docs/07, "Transferências").
static WKPageRef media_pages[8];

static void
media_mark(WKPageRef p, bool on) {
  int free_slot = -1;
  for(int i = 0; i < (int)(sizeof(media_pages) / sizeof(media_pages[0])); i++) {
    if(media_pages[i] == p) {
      if(!on) {
        media_pages[i] = 0;
      }
      return;
    }
    if(!media_pages[i] && free_slot < 0) {
      free_slot = i;
    }
  }
  if(on && free_slot >= 0) {
    media_pages[free_slot] = p;
  }
}

static bool
media_shown(WKPageRef p) {
  for(WKPageRef m : media_pages) {
    if(m && m == p) {
      return true;
    }
  }
  return false;
}


// What came back: shown, or a download (an attachment, a type WebKit does
// not show, a user script to install).
static void
decidePageResponse(WKPageRef p, WKNavigationResponseRef navigation,
                   WKFramePolicyListenerRef listener, WKTypeRef, const void*) {
  WKURLResponseRef response = WKNavigationResponseCopyResponse(navigation);
  WKFrameInfoRef frame = WKNavigationResponseCopyFrameInfo(navigation);
  bool main_frame = !frame || WKFrameInfoGetIsMainFrame(frame);
  char url[4096];
  char mime[128] = "";

  if(frame) {
    WKRelease(frame);
  }
  bool attachment = response && WKURLResponseIsAttachment(response);
  take_url(response ? WKURLResponseCopyURL(response) : 0, url, sizeof(url));
  if(response && main_frame) {
    take_string(WKURLResponseCopyMIMEType(response), mime, sizeof(mime));
  }
  if(response) {
    WKRelease(response);
  }
  url[strcspn(url, "?#")] = 0;
  bool user_script = main_frame && url_is_web(url) && name_ends_with(url, ".user.js");
  if(attachment || user_script || !WKNavigationResponseCanShowMIMEType(navigation)) {
#ifdef APP_RN
    if(main_frame && !attachment && !strcmp(mime, "application/pdf")) {  // shown in the reader once it is in
      snprintf(pdf_wanted, sizeof(pdf_wanted), "%s", url);
      pdf_wanted_ms = poc_now_ms();
    }
#endif
    WKFramePolicyListenerDownload(listener);
  } else {
    if(main_frame) {
      bool media = url_is_web(url) && (!strncmp(mime, "video/", 6) || !strncmp(mime, "audio/", 6));
      media_mark(p, media);
      if(media) {
        POC_LOG("a %s file by itself: the bar offers to keep it", mime);
      }
    }
    WKFramePolicyListenerUse(listener);
  }
}


// ---------------------------------------------------------------------------
// The D-pad: from element to element (nav.js), a ring around the one it
// chose and the cursor on it. Each view keeps nav.js as __bps5navfn once it
// has had it.

static void move_to(const input_event_t* e);

typedef struct nav_job {
  int pane;
  char cmd[8];
  float x, y;
  bool fresh;
  bool full;
} nav_job_t;

static void nav_done(WKTypeRef result, WKErrorRef, void* ctx);

// (x, y): the cursor on the screen.
static void
nav_run(int pane, const char* cmd, float x, float y, bool fresh, bool full) {
  const pane_t* p = &app.panes[pane];
  double z = pane == PAGE ? app.zoom : 1;
  nav_job_t* job = (nav_job_t*)calloc(1, sizeof(*job));
  text_t t = {};

  if(!job) {
    return;
  }
  job->pane = pane;
  snprintf(job->cmd, sizeof(job->cmd), "%s", cmd);
  job->x = x;
  job->y = y;
  job->fresh = fresh;
  job->full = full;
  double cx = (x - p->left) / z;
  double cy = (y - p->top) / z;
  if(full) {
    text_add(&t, "(window.__bps5navfn = (");
    text_add(&t, nav_js);
    text_printf(&t, "\n))('%s', %.1f, %.1f, %d)", cmd, cx, cy, fresh ? 1 : 0);
  } else {
    text_printf(&t, "typeof __bps5navfn == 'function' ? __bps5navfn('%s', %.1f, %.1f, %d) "
                    ": 'missing'", cmd, cx, cy, fresh ? 1 : 0);
  }
  if(t.failed) {
    free(job);
  } else {
    run_script(pane, t.s, job, nav_done);
  }
  free(t.s);
}


// The element chosen, its box in the view's CSS pixels.
static void
ring_set(int pane, double x, double y, double w, double h, bool move) {
  const pane_t* p = &app.panes[pane];
  double z = pane == PAGE ? app.zoom : 1;
  int rx = p->left + (int)(x * z);
  int ry = p->top + (int)(y * z);
  int rw = (int)(w * z + 0.5);
  int rh = (int)(h * z + 0.5);

  pthread_mutex_lock(&app.lock);
  bool same = app.ring.x == rx && app.ring.y == ry && app.ring.w == rw && app.ring.h == rh;
  if(app.ring.on && app.ring.pane == pane) {
    if(!same) {  // glide from where it is drawn now
      app.ring.from_x = app.ring.shown_x;
      app.ring.from_y = app.ring.shown_y;
      app.ring.from_w = app.ring.shown_w;
      app.ring.from_h = app.ring.shown_h;
      app.ring.appearing = false;
      app.ring.start_ms = poc_now_ms();
    }
  } else {  // close in on it
    app.ring.from_x = rx - RING_APPEAR_PX;
    app.ring.from_y = ry - RING_APPEAR_PX;
    app.ring.from_w = rw + 2 * RING_APPEAR_PX;
    app.ring.from_h = rh + 2 * RING_APPEAR_PX;
    app.ring.shown_x = app.ring.from_x;
    app.ring.shown_y = app.ring.from_y;
    app.ring.shown_w = app.ring.from_w;
    app.ring.shown_h = app.ring.from_h;
    app.ring.appearing = true;
    app.ring.start_ms = poc_now_ms();
  }
  bool moved = !app.ring.on || app.ring.pane != pane || !same;
  app.ring.on = true;
  app.ring.pane = pane;
  app.ring.x = rx;
  app.ring.y = ry;
  app.ring.w = rw;
  app.ring.h = rh;
  app.dirty = true;
  pthread_cond_signal(&app.cond);
  pthread_mutex_unlock(&app.lock);
  app.nav_fresh = false;
  if(!move) {
    return;
  }
  if(moved) {
    ui_sound(LINK_SOUND_MOVE);
  }
  // The cursor in the middle of what shows of it: a click there clicks it.
  float top = ry > p->top ? ry : p->top;
  float bottom = ry + rh < p->top + p->height ? ry + rh : p->top + p->height;
  float cx = rx + rw / 2.0f;
  float cy = (top + bottom) / 2;
  cx = cx < 0 ? 0 : cx > DISPLAY_WIDTH - 1 ? DISPLAY_WIDTH - 1 : cx;
  cy = cy < 0 ? 0 : cy > DISPLAY_HEIGHT - 1 ? DISPLAY_HEIGHT - 1 : cy;
  input_set_cursor(cx, cy);
  input_event_t e = {INPUT_MOVE, cx, cy, 0, 0, 0};
  move_to(&e);
}


static void wheel(float x, float y, float dx, float dy);

static void
nav_done(WKTypeRef result, WKErrorRef, void* ctx) {
  nav_job_t* job = (nav_job_t*)ctx;
  char answer[160] = "";

  if(result && WKGetTypeID(result) == WKStringGetTypeID()) {
    WKStringGetUTF8CString((WKStringRef)result, answer, sizeof(answer));
  }
  if(!strcmp(answer, "missing") && !job->full) {  // a new document
    nav_run(job->pane, job->cmd, job->x, job->y, job->fresh, true);
    free(job);
    return;
  }
  bool refresh = !strcmp(job->cmd, "refresh");
  bool near = !strcmp(job->cmd, "near");
  double x, y, w, h;
  char kind[16];
  if(near) {
    // The magnet: only if the stick is still at rest and nothing took the
    // ring meanwhile; the cursor moves only when it is not on it already.
    if(sscanf(answer, "sel %lf %lf %lf %lf %15s", &x, &y, &w, &h, kind) == 5
       && poc_now_ms() - app.stick_ms >= MAGNET_IDLE_MS && !app.ring.on) {
      const pane_t* p = &app.panes[job->pane];
      double z = job->pane == PAGE ? app.zoom : 1;
      double cx = (job->x - p->left) / z, cy = (job->y - p->top) / z;
      bool inside = cx >= x && cx < x + w && cy >= y && cy < y + h;
      ring_set(job->pane, x, y, w, h, !inside);
    }
  } else if(sscanf(answer, "sel %lf %lf %lf %lf %15s", &x, &y, &w, &h, kind) == 5) {
    if(!refresh || app.ring.on) {
      ring_set(job->pane, x, y, w, h, !refresh);
    }
  } else if(!strncmp(answer, "edge ", 5)) {
    // Up from the top of the page to the bar, down from the bar to the page.
    if(job->pane == PAGE && !strcmp(answer + 5, "up") && !app.fullscreen) {
      nav_run(BAR, "up", job->x, (float)app.bar_height, true, false);
    } else if(job->pane == BAR && !strcmp(answer + 5, "down")) {
      nav_run(PAGE, "down", job->x, (float)app.panes[PAGE].top, true, false);
    }
  } else if(!answer[0] && !refresh) {
    // No answer (JavaScript off, a page still loading): the D-pad scrolls.
    int dx = !strcmp(job->cmd, "left") ? -1 : !strcmp(job->cmd, "right") ? 1 : 0;
    int dy = !strcmp(job->cmd, "up") ? -1 : !strcmp(job->cmd, "down") ? 1 : 0;
    wheel(job->x, job->y, dx * 120.0f, dy * 120.0f);
  } else if(refresh && strcmp(answer, "scroll")) {
    ring_off();  // what it chose is gone
  }
  free(job);
}


// The D-pad's step from an INPUT_SCROLL: -1, 0 or 1 each way, or false for
// the right stick. A title from before the D-pad had its own events
// (pressed 1) sends it as a scroll of 120, which the stick reaches only
// above 150% (settings).
#define DPAD_AT_LEAST 100

static bool
dpad_step(const input_event_t* e, int* dx, int* dy) {
  bool dpad = e->pressed == 1
              || (settings.scroll_speed <= 150
                  && (fabsf(e->dx) >= DPAD_AT_LEAST || fabsf(e->dy) >= DPAD_AT_LEAST));
  float least = e->pressed == 1 ? 1 : DPAD_AT_LEAST;

  if(!dpad) {
    return false;
  }
  *dx = e->dx >= least ? 1 : e->dx <= -least ? -1 : 0;
  *dy = e->dy >= least ? 1 : e->dy <= -least ? -1 : 0;
  return *dx || *dy;
}


// Over a text field, whose cursor is an I-beam, the hand shows too
// (setCursor()): WebKit gives the I-beam over any text.
static const char editable_js[] = R"((function (x, y) {
  let e = document.elementFromPoint(x, y);
  while (e && e.shadowRoot) {
    const inner = e.shadowRoot.elementFromPoint(x, y);
    if (!inner || inner === e) break;
    e = inner;
  }
  if (!e) return false;
  if (e.isContentEditable || e.tagName === 'TEXTAREA') return true;
  return e.tagName === 'INPUT' &&
      /^(text|search|email|url|password|number|tel)$/i.test(e.type || 'text');
}))";

static double editable_checked_ms;

static void
editable_found(WKTypeRef result, WKErrorRef, void*) {
  bool yes = result && WKGetTypeID(result) == WKBooleanGetTypeID()
             && WKBooleanGetValue((WKBooleanRef)result);
  pthread_mutex_lock(&app.lock);
  if(app.cursor_type == CURSOR_TYPE_IBEAM && app.editable != yes) {
    app.editable = yes;
    app.dirty = true;
    pthread_cond_signal(&app.cond);
  }
  pthread_mutex_unlock(&app.lock);
}


static void
editable_check(void) {
  int pane = app.hover;
  float x, y;
  char script[sizeof(editable_js) + 64];

  if(pane < 0 || pane >= PANES || pane == KEYS) {
    return;
  }
  input_cursor(&x, &y);
  const pane_t* p = &app.panes[pane];
  double z = pane == PAGE ? app.zoom : 1;
  snprintf(script, sizeof(script), "%s(%.1f, %.1f)", editable_js, (x - p->left) / z,
           (y - p->top) / z);
  run_script(pane, script, 0, editable_found);
  editable_checked_ms = poc_now_ms();
}


#ifdef GO_TEST
static void go_test(void);
#endif
#ifdef NAV_TEST
static void nav_test(void);
#endif
#ifdef KEYS_TEST
static void keys_test(void);
#endif
#ifdef OPTIONS_TEST
static void options_test(void);
#endif
#ifdef UI_TEST
static void ui_test(void);
#endif
#ifdef SPEED_TEST
static void speed_test(void);
static void speed_guard(void);
#endif
#if defined(DEMO) || defined(SHOW)
static void demo(void);
#endif
#ifdef H2_TEST
static void h2_test(void);
#endif

static void
didStartProvisionalNavigation(WKPageRef p, WKNavigationRef, WKTypeRef,
                              const void*) {
#ifdef APP_RN
  if(p == app_nav_page) {
    app_nav_started = true;
  }
#endif
  if(p != page(PAGE)) {  // a tab behind
    return;
  }
  POC_LOG("navigation started %.0f ms after start",
          poc_now_ms() - app.start_ms);
  app.loading = true;
  ring_off();
  bar_update(0);
}


static void
didCommitNavigation(WKPageRef p, WKNavigationRef, WKTypeRef, const void*) {
#ifdef APP_RN
  if(p == app_nav_page) {  // the address the browser asked for is in: what follows is the page's doing
    app_nav_page = 0;
  }
  {  // a web page came into a tab that showed the page of a site that did not load: it shows it no more
    char committed[64];
    take_url(WKPageCopyCommittedURL(p), committed, sizeof(committed));
    if(url_is_web(committed)) {
      error_page_note(p, false);
    }
  }
#endif
  if(p != page(PAGE)) {  // a tab behind
    return;
  }
  POC_LOG("first bytes of the page in, %.0f ms after start",
          poc_now_ms() - app.start_ms);
  ring_off();
  tv_page_fullscreen();  // a new page starts with the bar (YouTube's page for televisions: with the whole screen)
  game_set(false);
  game_wanted = false;
#ifdef APP_RN
  overlay_tab = -1;  // the page that had the link menu or the reading mode is gone
  if(upload_pending()) {  // and so is the one that asked for a file
    upload_end(true);
    dialog_hide();
  }
#endif
  phone_page_changed();
  bar_update(0);
}


// A web page that came in: into the history, and the last page (the
// setting "Ao iniciar: a última página").
static void
history_note(void) {
  char url[4096];
  char title[1024];

#if defined(H2_TEST) || defined(SHOW_YT) || defined(SPEED_TEST)
  // The test's pages stay out of the user's history and last page (2026-11-11: the traces' and probes' too;
  // 2026-11-12: every test plan's: 258 of the history's 403 lines were tests', taken out by hand that day).
  return;
#endif
  take_url(WKPageCopyCommittedURL(page(PAGE)), url, sizeof(url));
  if(!url_is_web(url)) {
    return;
  }
#ifdef APP_RN
  if(tab_is_private()) {  // neither the history nor the last page
    return;
  }
  if(pdf_port) {  // the PDF reader's own address (pdf_server()) is no page to come back to: "PDF.js viewer" was in the history
    char reader[64];
    const int n = snprintf(reader, sizeof(reader), "http://127.0.0.1:%d/pdfjs/", pdf_port);
    if(!strncmp(url, reader, (size_t)n)) {
      return;
    }
  }
#endif
  take_string(WKPageCopyTitle(page(PAGE)), title, sizeof(title));
  history_add(HISTORY_FILE, (long)time(0), url, title);
  if(strcmp(settings.last_url, url) && settings_set(&settings, "last_url", url) == 1) {
    settings_store();
  }
}


// 2026-11-09 19:0x: the page crashed on YouTube's full screen button. WebKit
// keeps a WebProcess's "shared preferences" from the first page put in it
// (WebProcessProxy::initializePreferencesForGPUAndNetworkProcesses): the bar,
// with full screen off (we have one WebProcess for every pane). The page's
// EnterFullScreen then counts as a message the process may not send, and
// WebKit ends the process (log: "WebKit refused a message ...
// WebFullScreenManagerProxy_EnterFullScreen"). A change of the page's
// preferences adds its full screen to the process's (WebPageProxy::
// preferencesDidChange, updateSharedPreferences): done once for each
// WebProcess, a crash's new one too, as the page's first load ends.
static WKProcessID fullscreen_pid;

static void
fullscreen_prefs_sync(WKPageRef p) {
  const WKProcessID pid = WKPageGetProcessIdentifier(p);
  if(pid <= 0 || pid == fullscreen_pid) {
    return;
  }
  fullscreen_pid = pid;
  WKPreferencesStartBatchingUpdates(page_preferences);
  WKPreferencesSetFullScreenEnabled(page_preferences, false);
  WKPreferencesSetFullScreenEnabled(page_preferences, true);
  WKPreferencesEndBatchingUpdates(page_preferences);
  POC_LOG("full screen: in the WebProcess's preferences (pid %d)", (int)pid);
}

static void
didFinishNavigation(WKPageRef p, WKNavigationRef, WKTypeRef, const void*) {
  char url[1024];
  char shown[512];

  if(p != page(PAGE)) {  // a tab behind
    return;
  }
  fullscreen_prefs_sync(p);
  app.loading = false;
  bar_update(0);
  history_note();
  session_note();
  take_url(WKPageCopyActiveURL(page(PAGE)), url, sizeof(url));
  POC_RESULT(1, "page loaded %.0f ms after start: %s",
             poc_now_ms() - app.start_ms, url_for_log(url, shown, sizeof(shown)));
#ifdef APP_RN
  update_first_page();  // the browser's own updates: once a session
#endif
#ifdef H2_TEST
  h2_test();
#endif
#ifdef GO_TEST
  go_test();
#endif
#ifdef NAV_TEST
  nav_test();
#endif
#ifdef KEYS_TEST
  keys_test();
#endif
#ifdef OPTIONS_TEST
  options_test();
#endif
#ifdef UI_TEST
  ui_test();
#endif
#ifdef SPEED_TEST
  speed_test();
#endif
#if defined(DEMO) || defined(SHOW)
  demo();
#endif
}


// WebKit's: the load stopped because its response became a download.
#define FRAME_LOAD_INTERRUPTED_BY_POLICY_CHANGE 102

// Before the page commits (DNS, TLS, HTTP) or after (a subresource).
static void
didFailNavigation(WKPageRef p, WKNavigationRef, WKErrorRef error, WKTypeRef,
                  const void*) {
  char domain[64];
  char description[256];
  char url[1024];
  char shown[512];

  if(p != page(PAGE)) {  // a tab behind
    return;
  }
  app.loading = false;
  bar_update(0);
  take_string(WKErrorCopyDomain(error), domain, sizeof(domain));
  take_string(WKErrorCopyLocalizedDescription(error), description,
              sizeof(description));
  take_url(WKErrorCopyFailingURL(error), url, sizeof(url));
  if(WKErrorGetErrorCode(error) == FRAME_LOAD_INTERRUPTED_BY_POLICY_CHANGE
     && !strcmp(domain, "WebKitErrorDomain")) {
    POC_LOG("the navigation became a download: %s", url_for_log(url, shown, sizeof(shown)));
    return;
  }
  POC_RESULT(0, "navigation failed %.0f ms after start: %s %d \"%s\" (%s)",
             poc_now_ms() - app.start_ms, domain, WKErrorGetErrorCode(error),
             description, url_for_log(url, shown, sizeof(shown)));
}


#ifdef APP_RN
// The page a site that does not load gets (2026-11-14). Until now a navigation that failed before its page
// came (no such site, no connection, a certificate that cannot be verified, a server that never answers) left
// the page of before on the screen, or an empty one, and said nothing. Now the tab shows what happened, in
// the user's language, with the site's name and a button that asks for the address again. The address stays
// in the bar (it is the page's "unreachable URL" for the engine), so the bar's reload asks for it again too,
// and nothing of it goes to the history (history_note() reads the committed address, which this page has not).
//   Which page: by curl's error code (CurlErrorDomain). There is no way past a certificate that fails.
static bool tab_page(WKPageRef p);

// The pages that show it now (the bar gives such a tab no padlock: bar_update()). One is such a page from the
// moment it is shown until a web address commits in it.
static WKPageRef error_pages[8];

static bool
error_page_of(WKPageRef p) {
  char committed[64];
  for(WKPageRef shown : error_pages) {
    if(shown == p) {
      // (2026-11-14, read again on the PC: from such a page to the start page or the settings, a tab went on
      // counting as one "that did not load", and the bar was told so of the browser's own page. The page of a
      // site that did not load is committed as about:blank, the browser's own pages as files.)
      take_url(WKPageCopyCommittedURL(p), committed, sizeof(committed));
      return !url_is_web(committed) && strncmp(committed, "file:", 5) != 0;
    }
  }
  return false;
}

static void
error_page_note(WKPageRef p, bool shown) {
  WKPageRef* slot = 0;
  for(WKPageRef& known : error_pages) {
    if(known == p) {
      slot = &known;
    }
  }
  for(WKPageRef& known : error_pages) {
    if(!slot && !known && shown) {
      slot = &known;
    }
  }
  if(slot) {
    *slot = shown ? p : 0;
  }
}

static void
error_html_text(const char* in, char* out, size_t size) {
  size_t n = 0;
  for(; *in && n + 7 < size; in++) {
    const char* as = *in == '&' ? "&amp;" : *in == '<' ? "&lt;" : *in == '>' ? "&gt;" : *in == '"' ? "&quot;" : *in == '\'' ? "&#39;" : 0;
    if(as) {
      n += (size_t)snprintf(out + n, size - n, "%s", as);
    } else {
      out[n++] = *in;
    }
  }
  out[n] = 0;
}

static void
error_page_show(WKPageRef p, const char* url, const char* domain, int code, const char* description) {
  static const char style[] =
    "html,body{height:100%;margin:0}"
    "body{display:flex;align-items:center;justify-content:center;color:#e9ebf0;font:26px/1.45 'DejaVu Sans',sans-serif;"
    "background:radial-gradient(1200px 700px at 50% 28%,#1b1e28 0,#0c0e13 72%)}"
    "main{max-width:1040px;padding:0 80px;text-align:center}"
    ".ic{width:124px;height:124px;margin:0 auto 34px;border-radius:34px;background:rgba(255,255,255,.06);display:flex;"
    "align-items:center;justify-content:center}"
    ".ic svg{width:66px;height:66px;fill:none;stroke:#ff9d4d;stroke-width:1.6;stroke-linecap:round;stroke-linejoin:round}"
    "h1{font-size:46px;font-weight:700;margin:0 0 14px}"
    "p{margin:0 0 10px;color:#b9bdc9}"
    "p.host{color:#e9ebf0;font-weight:700;word-break:break-all}"
    "a.again{display:inline-block;margin:34px 0 26px;padding:16px 46px;border-radius:999px;background:#ff9d4d;color:#14161c;"
    "font-weight:700;text-decoration:none}"
    "a.again:focus{outline:4px solid #fff;outline-offset:4px}"
    "small{display:block;color:#6f7482;font-size:18px}";
  static const char icon_off[] = "<circle cx='12' cy='12' r='9'/><path d='M3 12h18M12 3c3 3.2 3 14.8 0 18M12 3c-3 3.2-3 14.8 0 18M4.5 4.5l15 15'/>";
  static const char icon_clock[] = "<circle cx='12' cy='12' r='9'/><path d='M12 7v5l3.2 2'/>";
  static const char icon_shield[] = "<path d='M12 3l7 3v5c0 4.6-3 8.4-7 10-4-1.6-7-5.4-7-10V6z'/><path d='M12 8.5v4.5M12 16.2v.1'/>";
  static const char icon_warn[] = "<path d='M12 4l9 16H3z'/><path d='M12 10v4.5M12 17.4v.1'/>";
  static char html[16000], address[6200], host_text[800], said[900];
  const bool en = lang_en();
  const char* title;
  const char* text;
  const char* icon = icon_off;
  char host[256];

  url_host(url, host, sizeof(host));
  switch(!strcmp(domain, "CurlErrorDomain") ? code : -1) {
  case 5: case 6:  // the name has no address
    title = en ? LM("Site not found") : "Site não encontrado";
    text = en ? LM("No site answers at this address. Check that it is written correctly, and that the console has internet.")
              : "Nenhum site responde neste endereço. Vê se está bem escrito, e se a consola tem internet.";
    break;
  case 7:  // nobody answers there
    title = en ? LM("Could not connect to this site") : "Não foi possível ligar a este site";
    text = en ? LM("The site may be down, or the console may be without internet. Check the PS5's network connection.")
              : "O site pode estar em baixo, ou a consola sem internet. Vê a ligação de rede da PS5.";
    break;
  case 28:
    title = en ? LM("The site took too long to answer") : "O site demorou demasiado a responder";
    text = en ? LM("It may be overloaded, or the connection slow. Try again in a moment.")
              : "Pode estar sobrecarregado, ou a ligação lenta. Tenta outra vez daqui a pouco.";
    icon = icon_clock;
    break;
  case 35: case 51: case 53: case 54: case 58: case 59: case 60: case 64: case 66: case 77: case 80: case 82: case 83:
  case 90: case 91: case 98:  // TLS: the handshake, the certificate, its issuer
    title = en ? LM("This connection is not secure") : "Esta ligação não é segura";
    text = en ? LM("The site's security certificate could not be verified, so the browser did not open it and sent it nothing. "
                "It may be a problem of the site's, or the console's date and time may be wrong.")
              : "O certificado de segurança do site não pôde ser verificado, por isso o browser não o abriu nem lhe enviou nada. "
                "Pode ser um problema do site, ou a data e a hora da consola estarem erradas.";
    icon = icon_shield;
    break;
  case 47:
    title = en ? LM("Too many redirects") : "Demasiados redirecionamentos";
    text = en ? LM("The site keeps sending the browser from one address to another. Clearing its cookies may help (Settings, Privacy).")
              : "O site está sempre a mandar o browser de um endereço para outro. Limpar os cookies pode ajudar (Definições, Privacidade).";
    icon = icon_warn;
    break;
  case 1: case 3:
    title = en ? LM("This address is not valid") : "Este endereço não é válido";
    text = en ? LM("Check how it is written.") : "Vê como está escrito.";
    icon = icon_warn;
    break;
  case 16: case 18: case 52: case 55: case 56: case 92: case 95:
    title = en ? LM("The connection was interrupted") : "A ligação foi interrompida";
    text = en ? LM("The site stopped answering while the page was coming. Try again.")
              : "O site deixou de responder enquanto a página vinha. Tenta outra vez.";
    break;
  default:
    title = en ? LM("This page did not open") : "Esta página não abriu";
    text = en ? LM("Something went wrong while the page was coming. Try again.")
              : "Algo correu mal enquanto a página vinha. Tenta outra vez.";
    icon = icon_warn;
    break;
  }
  // 2026-11-14: an address of this console's own services. A site's navigation there is refused by the engine
  // (curl's code 42: its request was stopped before a byte went out), and this page then had that address as
  // its own: the bar's reload, the tab closed and opened again, the tabs of the last time would each ask for it
  // again as the app's own load, which may connect there (site_may_open()). Such a page has no address and no
  // "Try again": whoever wants to go there writes the address.
  static char again[6400];
  const bool console = url_this_console(url);
  if(console && !strcmp(domain, "CurlErrorDomain") && code == 42) {
    title = en ? LM("Sites cannot open this console's services") : "Os sites não podem abrir os serviços desta consola";
    text = en ? LM("A page tried to open an address of this console itself, and the browser did not go there. "
                   "If you want to open it, type the address yourself.")
              : "Uma página tentou abrir um endereço da própria consola, e o browser não foi lá. "
                "Se o queres abrir, escreve tu o endereço.";
    icon = icon_shield;
  }
  error_html_text(url, address, sizeof(address));
  error_html_text(host[0] ? host : url, host_text, sizeof(host_text));
  error_html_text(description, said, sizeof(said));
  again[0] = 0;
  if(!console) {
    snprintf(again, sizeof(again), "<a class=\"again\" href=\"%s\">%s</a>", address, en ? LM("Try again") : "Tentar outra vez");
  }
  snprintf(html, sizeof(html),
           "<!doctype html><html lang=\"%s\"><meta charset=\"utf-8\"><title>%s</title><style>%s</style>"
           "<main><div class=\"ic\"><svg viewBox=\"0 0 24 24\">%s</svg></div><h1>%s</h1><p class=\"host\">%s</p><p>%s</p>"
           "%s<small>%s %d%s%s</small></main>",
           lang_code(), host_text, style, icon, title, host_text, text, again,
           domain, code, said[0] ? " \u00b7 " : "", said);
  char* mended = utf8_mended(html);  // (one byte that is not UTF-8 in it and the engine would get an empty page: run_script())
  WKStringRef page_html = string(mended ? mended : html);
  free(mended);
  WKURLRef address_url = console ? 0 : WKURLCreateWithUTF8CString(url);
  WKPageLoadAlternateHTMLString(p, page_html, 0, address_url);
  if(address_url) {
    WKRelease(address_url);
  }
  WKRelease(page_html);
  error_page_note(p, true);
  POC_LOG("the page of a site that did not load is shown (%s %d)", domain, code);
}

// Before the page commits: what the user is told (the other failures, after it, are only written down).
static void
didFailProvisionalNavigation(WKPageRef p, WKNavigationRef navigation, WKErrorRef error, WKTypeRef data, const void* info) {
  static char url[4200];
  char domain[64], description[256];
  const int code = WKErrorGetErrorCode(error);

  take_string(WKErrorCopyDomain(error), domain, sizeof(domain));
  take_string(WKErrorCopyLocalizedDescription(error), description, sizeof(description));
  take_url(WKErrorCopyFailingURL(error), url, sizeof(url));
#ifdef APP_RN
  if(app_nav_again(p, domain, code)) {
    return;
  }
  if(p == app_nav_page && app_nav_started) {
    app_nav_page = 0;
  }
#endif
  didFailNavigation(p, navigation, error, data, info);
  // Not: what became a download (102), what the user or the browser stopped (302), a part of a download, the
  // browser's own pages, another view than a tab's.
  if((!strcmp(domain, "WebKitErrorDomain") && (code == FRAME_LOAD_INTERRUPTED_BY_POLICY_CHANGE || code == 302))
     || !url_is_web(url) || strstr(url, "#bps5range=") || !tab_page(p)) {
    return;
  }
  error_page_show(p, url, domain, code, description);
}
#endif

// The bar's page is in: it gets the whole state (what was sent before it
// had update() went nowhere).
static void
barLoaded(WKPageRef, WKNavigationRef, WKTypeRef, const void*) {
  char script[96];

  snprintf(script, sizeof(script), "setTheme('%s')", theme_now());
  run_script(BAR, script, 0, ignore_result);
  {
    char lang_script[32];
    snprintf(lang_script, sizeof(lang_script), "setLang('%s')", lang_code());
    run_script(BAR, lang_script, 0, ignore_result);
  }
  run_script(BAR, settings.favbar ? "setFavbar(true)" : "setFavbar(false)", 0,
             ignore_result);
  app.bar_state[0] = 0;
#ifdef APP_RN
  bar_tabs_sent[0] = 0;
#endif
  bar_update(0);
  favorites_to_bar();
  bar_tabs();
  downloads_changed = true;  // the button, on the tick
}


// The menu's, the dialog's and the keyboard's pages are in: the theme, and
// the dialog what it was showing (after a crash, or the pause).
static void
uiLoaded(WKPageRef, WKNavigationRef, WKTypeRef, const void* info) {
  const pane_t* pane = (const pane_t*)info;
  int i = (int)(pane - app.panes);
  char script[96];

  snprintf(script, sizeof(script), "setTheme('%s')", theme_now());
  run_script(i, script, 0, ignore_result);
  {
    char lang_script[32];
    snprintf(lang_script, sizeof(lang_script), "setLang('%s')", lang_code());
    run_script(i, lang_script, 0, ignore_result);
  }
  if(i == MENU) {
    snprintf(script, sizeof(script), "setZoom(%d)", settings.zoom);
    run_script(MENU, script, 0, ignore_result);
  } else if(i == DIALOG && app.dialog_open && dialog_script) {
    run_script(DIALOG, dialog_script, 0, ignore_result);
  }
}


// The keyboard's page, again (after a crash, or the pause): closed if it
// was open, as if cancelled.
static void
keys_reload(void) {
  if(app.keys_open) {
    keys_hide();
    keyboard_text(0);
  }
  WKStringRef html = string(keyboard_html);
  WKPageLoadHTMLString(page(KEYS), html, 0);
  WKRelease(html);
}


// A page of the app's, its links and pictures from base (a URL) if not 0.
static void
load_html_at(int pane, const char* html, const char* base) {
  WKStringRef s = string(html);
  WKURLRef u = base ? WKURLCreateWithUTF8CString(base) : 0;
  WKPageLoadHTMLString(page(pane), s, u);
  if(u) {
    WKRelease(u);
  }
  WKRelease(s);
}


// The bar's, the menu's or the dialog's page, again.
static void
ui_reload(int pane) {
  const char* html = pane == BAR ? chrome_html : pane == MENU ? menu_html
                   : pane == RADIAL ? radial_html : pane == SWITCHER ? tabs_html
                   : dialog_html;
  if(pane == RADIAL) {
    radial_hide(false);
  }
  if(pane == SWITCHER) {
    switcher_hide();
  }
  if(pane == MENU) {
    menu_hide();
  }
  load_html_at(pane, html, pane == SWITCHER ? "file://" TABS_DIR "/" : 0);
}


// Instead of the page that crashed: loading that again would likely crash
// again.
static const char crashed_html[] = R"(<!doctype html><meta charset="utf-8">
<body style="margin: 160px; background: #1b1f24; color: #e8eaed;
             font: 32px 'DejaVu Sans', sans-serif">
<h1 style="font-size: 48px">Esta página fez o browser falhar</h1>
<p>Carrega em &#x21bb; na barra para tentar outra vez.</p>)";
static const char crashed_html_en[] = R"(<!doctype html><meta charset="utf-8">
<body style="margin: 160px; background: #1b1f24; color: #e8eaed;
             font: 32px 'DejaVu Sans', sans-serif">
<h1 style="font-size: 48px">This page made the browser fail</h1>
<p>Press &#x21bb; in the bar to try again.</p>)";

// The two views share the WebProcess, so both come here. Each gets a new
// one (a load starts it): the bar its own page, the page itself again or,
// when it crashed less than a minute before, crashed_html with the address
// kept for the bar and ↻. Before, the app stayed with nothing
// to paint (TV test 3, YouTube, 2026-11-03).
static void
webProcessDidCrash(WKPageRef p, const void* info) {
  pane_t* pane = (pane_t*)info;

  POC_RESULT(0, "the %s's WebProcess crashed", pane->name);
#ifdef APP_RN
  if(pane == &app.panes[PAGE]) {
    ask_abandon();  // the page that asked something (alert, confirm, prompt) is gone
  }
#endif
  if(pane == &app.panes[PAGE] && p != page(PAGE)) {
    POC_LOG("a tab behind: it loads again when it comes to the front");
    return;
  }
#if defined(REMOTE_UI) && defined(GPU_COMPOSITING)
  // Its sound rings, which nothing will write again (both views come here
  // for the same WebProcess: the title frees them once).
  if(pane->last_web_pid > 0) {
    remote_ended(pane->last_web_pid, 0);
  }
#endif
  if(pane == &app.panes[BAR] || pane == &app.panes[MENU] || pane == &app.panes[DIALOG]
     || pane == &app.panes[RADIAL] || pane == &app.panes[SWITCHER]) {
    ui_reload((int)(pane - app.panes));
  } else if(pane == &app.panes[KEYS]) {
    keys_reload();
  } else {
    // The first crash in a minute loads the page again by itself: once the
    // compositor crashed 2 s after the start, with no one doing anything
    // (2026-11-04 16:21). Another one that soon is the page's doing.
    static double last_crash_ms = -1e9;
    double now = poc_now_ms();
    bool again = now - last_crash_ms > 60000;
    last_crash_ms = now;
    WKBackForwardListRef list = WKPageGetBackForwardList(page(PAGE));
    if(again && list && WKBackForwardListGetCurrentItem(list)) {
      POC_LOG("the page loads again after the crash");
      WKPageReload(page(PAGE));
      return;
    }
    WKURLRef url = WKPageCopyActiveURL(page(PAGE));
    if(again && url) {
      POC_LOG("the page loads its address again after the crash");
#ifdef APP_RN
      app_nav_note(page(PAGE));
#endif
      WKPageLoadURL(page(PAGE), url);
      WKRelease(url);
      return;
    }
#ifdef APP_RN
    static char crashed_more[1000];  // in the pages' other languages (lang_c.h)
    const bool more = lang_en() && strcmp(lang_code(), "en") != 0;
    if(more) {
      snprintf(crashed_more, sizeof(crashed_more),
               "<!doctype html><meta charset=\"utf-8\"><body style=\"margin: 160px; background: #1b1f24; color: #e8eaed;"
               " font: 32px 'DejaVu Sans', sans-serif\"><h1 style=\"font-size: 48px\">%s</h1><p>%s</p>",
               LM("This page made the browser fail"), LM("Press \xe2\x86\xbb in the bar to try again."));
    }
    WKStringRef html = string(more ? crashed_more : lang_en() ? crashed_html_en : crashed_html);
#else
    WKStringRef html = string(lang_en() ? crashed_html_en : crashed_html);
#endif
    WKPageLoadAlternateHTMLString(page(PAGE), html, 0, url);
    WKRelease(html);
    if(url) {
      WKRelease(url);
    }
    app.loading = false;
  }
}


static void
finish(void*) {
  WKRunLoopStopMain();
}


// (2026-11-14, RN) Leaving at the PS5's home screen (away_main(), "leave"): LEAVE_WAIT_US. The 1.5 s are for
// a WebProcess of its own to let go of its sound port; RN's runs in the title, which closes its own, and until
// the title has left, a close from its tile can still find it (17:33: the console went down).
#define LEAVE_WAIT_US 250000
static unsigned close_wait_us = CLOSE_WAIT_US;

static void*
finish_thread(void*) {
  usleep(close_wait_us);
  WKRunLoopCallOnMainThread(finish, 0);
  return 0;
}


// On the main thread, so no paint runs meanwhile: the screen as it is now
// goes to the log as a PNG (poc/shot.ps1 gets it back). Then the pages
// close, and the run loop goes on a little longer for the WebProcess to let
// go of what it holds before the launcher kills it: a <video> its sound port
// (V2: after WebProcesses were killed with one open, the PS5's sound and
// then its app service got stuck until a restart). fast (the screen is
// gone): the pages close at once, with no screenshot.
static bool stopped;  // main thread

// The screen as the TV shows it, cursor and all, into the log as a PNG
// (poc/shot.ps1 gets each one back). On the main thread.
static void
screenshot(const char* why) {
  uint32_t* shot = (uint32_t*)malloc((size_t)DISPLAY_WIDTH * DISPLAY_HEIGHT * 4);

  if(!shot) {
    return;
  }
  pthread_mutex_lock(&app.lock);
  compose(shot);
  bool hand = cursor_is_hand();
  pthread_mutex_unlock(&app.lock);
  draw_cursor(shot, hand);
  POC_LOG("screenshot: %s", why);
  unsigned long size = shot_print(shot, DISPLAY_WIDTH, DISPLAY_HEIGHT);
  POC_RESULT(size != 0, "screenshot in the log: %lu bytes of PNG", size);
  free(shot);
}


static void quit_ends_web_processes(void);  // below, with the pause


static void
stop(void* fast) {
  if(stopped) {  // options, then the timer
    return;
  }
  stopped = true;
#ifdef APP_RN
  multi_keep_all();
  update_stop = 1;  // an installer being fetched is given up: fetched again another time
#endif
  phone_stop();
  unlink(SESSION_OPEN_FILE);  // a quit, not a fall: the tabs may come back by themselves
  pthread_mutex_lock(&app.lock);
  app.frozen = true;
  pthread_mutex_unlock(&app.lock);
#ifdef NO_SHOT
  const bool shot = false;
#else
  const bool shot = true;
#endif
  if(fast || !shot) {
    POC_LOG("no screenshot: closing the pages");
  } else {
    screenshot("at the end");
  }

  // The WebProcess ends now, as for the pause: after this run loop stops,
  // remote.c's display_close() has the system close the title, which no
  // GPU client's process may outlive (end_web_processes()).
  quit_ends_web_processes();
  for(int i = 0; i < PANES; i++) {
    WKPageClose(page(i));
  }
  pthread_t finisher;
  if(pthread_create(&finisher, 0, finish_thread, 0)) {
    WKRunLoopStopMain();
    return;
  }
  pthread_detach(finisher);
}


#ifdef REMOTE_UI
// The pause (docs/03-video-e-gpu.md, "Pausa"). Closing the title from the
// system while the WebProcess drew through its GPU took the console down
// (2026-11-04, 15:16 and 15:53); closing it with no GPU client did not. A
// close always comes after the system's menu or home screen is over the
// app; the title sees that in its status within 0.1 s and says so
// (LINK_AWAY). With GPU_COMPOSITING the WebProcess is then ended
// (WKPageTerminate, which WebKit does not take for a crash) and the title
// lets go of its GPU client (LINK_ENDED), so a close finds none; when the
// app is back, the pages load again (the history stays in this process).
// make AWAY_HIDE=1, or without GPU_COMPOSITING: the views are only hidden
// and the WebProcess stays.
#if defined(GPU_COMPOSITING) && !defined(AWAY_HIDE)
#define AWAY_ENDS_WEB_PROCESS 1
#else
#define AWAY_ENDS_WEB_PROCESS 0
#endif

static struct {
  bool away;   // main thread, as all of this
  bool ended;  // the WebProcess was ended for this pause
  bool media;  // the page's media was paused and muted for it (2c-2)
  bool ui_ended;  // RN: the interface's own WebProcess was ended for it (ui_split)
  int pauses;
} pause_state;

#ifdef APP_RN
static bool
pages_away(void) {
  return pause_state.away;
}
#endif

// 2c-2 (docs/05): a WebProcess in the title stays through a pause
// (end_web_processes()), so what plays in the page pauses instead, marked
// to play again on the way back, and the page is muted; nothing plays
// behind the home screen, as when the WebProcess ended. Frames of the same
// origin too (tab_front()'s way).
#define AWAY_PAUSE_MEDIA                                                         \
  "(function p(d){try{d.querySelectorAll('video,audio').forEach(function(m){"   \
  "if(!m.paused){m.setAttribute('data-bps5-away','1');m.pause();}});"            \
  "d.querySelectorAll('iframe').forEach(function(f){try{p(f.contentDocument);}" \
  "catch(e){}});}catch(e){}})(document)"
#define AWAY_PLAY_MEDIA                                                          \
  "(function p(d){try{d.querySelectorAll('[data-bps5-away]').forEach(function(m){" \
  "m.removeAttribute('data-bps5-away');var r=m.play();if(r&&r.catch)"           \
  "r.catch(function(){});});d.querySelectorAll('iframe').forEach(function(f){" \
  "try{p(f.contentDocument);}catch(e){}});}catch(e){}})(document)"


static void
views_visible(bool visible) {
  for(int i = 0; i < PANES; i++) {
    WKViewSetVisible(app.panes[i].view, visible);
  }
}


#if AWAY_ENDS_WEB_PROCESS
// Ends the WebProcess (WKPageTerminate) and tells the title, which lets go
// of its GPU client for it (LINK_ENDED). The pause does it before the
// system's close; the quit before remote.c's close_title() has the system
// close the title (stop(), quit_ends_web_processes()). Closing the title
// while a GPU client's process still lived took the console down
// (2026-11-04 15:53, and 2026-11-05 16:51: the quit's close came while the
// WebProcess, its pages closed, was still there).
// for_away: as remote_ended(); below 0, the title is not told (the quit
// tells it once it has checked, quit_ends_web_processes()).
static int
end_web_processes(const char* why, double start, int32_t ended[PANES], int for_away) {
  // Both views share one WebProcess (main()); ending it for one ends it
  // for the other, whose pid then reads 0.
  int n = 0;
  for(int i = 0; i < PANES; i++) {
    int32_t pid = (int32_t)WKPageGetProcessIdentifier(page(i));
    bool seen = false;
    for(int j = 0; j < n; j++) {
      seen |= ended[j] == pid;
    }
    if(pid <= 0 || seen) {
      continue;
    }
    if(pid == PlayStation::titleWebProcessId()) {
      // 2c-2: a thread of the title, with no GPU client of its own. Ending
      // it (WebKit closes its connection) would end the whole title.
      if(i == 0 || pid != (int32_t)WKPageGetProcessIdentifier(page(0))) {
        POC_LOG("%s: WebProcess %d runs in the title: it stays", why, pid);
      }
      continue;
    }
    WKPageTerminate(page(i));
    ended[n++] = pid;
  }
  for(int j = 0; j < n; j++) {
    if(for_away < 0) {
      POC_LOG("%s: WebProcess %d ended in %.0f ms", why, ended[j], poc_now_ms() - start);
      continue;
    }
    int err = remote_ended(ended[j], for_away);
    POC_LOG("%s: WebProcess %d ended in %.0f ms; the title %s", why, ended[j],
            poc_now_ms() - start, err ? "could not be told" : "lets go of its GPU client");
  }
  if(!n) {
    POC_LOG("%s: no WebProcess was running", why);
  }
  return n;
}
#endif


// stop()'s part. The launcher answers WKPageTerminate's kill once the
// process is gone (launcher.c kill_and_reap()), so kill(pid, 0) says ESRCH
// here; only then may remote.c have the system close the title
// (remote_web_gone()). Otherwise the title ends by itself, as before 16:50
// (the system's "Erro no jogo", but the launcher kills the WebProcess as
// soon as this process ends).
static void
quit_ends_web_processes(void) {
#ifdef REMOTE_UI
#if AWAY_ENDS_WEB_PROCESS
  int32_t ended[PANES];
  int n = end_web_processes("quit", poc_now_ms(), ended, -1);
  bool gone = true;
  for(int j = 0; j < n; j++) {
    errno = 0;
    if(!(kill(ended[j], 0) && errno == ESRCH)) {
      POC_LOG("quit: WebProcess %d is still there (%s)", ended[j], errno ? strerror(errno) : "kill 0");
      gone = false;
    }
  }
  // A new WebProcess since? After WKPageTerminate the pages still give the
  // old pid (17:08: all said 117, gone 118 ms before), so a pid counts
  // only if that process is there.
  for(int i = 0; i < PANES; i++) {
    int32_t pid = (int32_t)WKPageGetProcessIdentifier(page(i));
    errno = 0;
    if(pid > 0 && !(kill(pid, 0) && errno == ESRCH)) {
      POC_LOG("quit: the %s has WebProcess %d, still there (%s)", app.panes[i].name, pid,
              errno ? strerror(errno) : "kill 0");
      gone = false;
    }
  }
  // Only now the title lets go of its GPU client, keeps the screen and says
  // it may be closed (link.h, LINK_TITLE_READY); otherwise LINK_QUIT
  // closes it all and it ends by itself.
  if(gone) {
    for(int j = 0; j < (n ? n : 1); j++) {
      remote_ended_for_quit(n ? ended[j] : 0);
    }
  }
  POC_LOG("quit: %s", gone ? "no WebProcess left: the title lets go of the GPU and may be closed by the system"
                           : "the title is left to end by itself");
  remote_web_gone(gone);
  pause_state.ended = true;
#elif defined(GPU_COMPOSITING)
  remote_web_gone(0);  // AWAY_HIDE: the WebProcess, a GPU client, stays
#else
  remote_web_gone(1);  // no GPU client but the title's own
#endif
#endif
}


#ifdef GPU_COMPOSITING
// See title_memory_off: every mapping of the title's memory goes (off), or may come back.
static void
title_memory_let_go(bool off, const char* why) {
  int layers = 0;
  bool ring = false;
  pthread_mutex_lock(&app.lock);
  if(off) {
    for(int i = 0; i < PANES; i++) {
      pane_t* pane = &app.panes[i];
      if(pane->layer) {
        munmap(pane->layer, pane->layer_bytes);
        pane->layer = 0;
        layers++;
      }
    }
    if(video_ring) {
      munmap(video_ring, offsetof(video_ring_t, slots));
      video_ring = 0;
      ring = true;
    }
  } else {
    for(int i = 0; i < PANES; i++) {
      app.panes[i].next_open_ms = 0;  // at the next frame
      app.panes[i].redraw = true;
    }
    video_ring_next_ms = 0;
  }
  const bool was = title_memory_off;
  title_memory_off = off;
  pthread_mutex_unlock(&app.lock);
  if(was != off) {
    if(off) {
      POC_LOG("%s: none of the title's memory is mapped here now (%d layer(s)%s let go)", why, layers,
              ring ? ", the video ring" : "");
    } else {
      POC_LOG("%s: the title's memory may be mapped again", why);
    }
  }
}
#else
static void title_memory_let_go(bool, const char*) { }
#endif

static void
pause_pages(void) {
  double start = poc_now_ms();

  pause_state.pauses++;
#ifdef APP_RN
  title_memory_let_go(true, "pause");  // first: the system may close the title from now on
#endif
#ifdef APP_RN
  ask_abandon();  // a page's question open: answered "no", so the page goes on
#endif
  if(app.keys_open) {  // closed, as if cancelled
    keys_hide();
    keyboard_text(0);
  }
  radial_hide(false);
  switcher_hide();
  views_visible(false);
#if AWAY_ENDS_WEB_PROCESS
  char why[32];
  int32_t ended[PANES];
  snprintf(why, sizeof(why), "pause %d", pause_state.pauses);
  // The panes share one WebProcess (main()): in the title, it stays.
  const int32_t pid = (int32_t)WKPageGetProcessIdentifier(page(PAGE));
  const bool in_title = pid > 0 && pid == PlayStation::titleWebProcessId();
  const int ended_now = end_web_processes(why, start, ended, 1);
  pause_state.ended = !in_title;
  pause_state.ui_ended = in_title && ended_now > 0;  // RN's interface in a WebProcess of its own
  if(in_title) {
    run_script(PAGE, AWAY_PAUSE_MEDIA, 0, ignore_result);
#ifdef APP_RN
    run_script(PAGE, AWAY_PAUSE_FRAMES, 0, ignore_result);  // (2026-11-14: and in the frames of other sites)
#endif
    WKPageSetMuted(page(PAGE), kWKMediaAudioMuted);
    pause_state.media = true;
    POC_LOG("%s: the page's media paused and muted; its WebProcess stays in the title", why);
  }
#else
  POC_LOG("pause %d: views hidden, the WebProcess stays", pause_state.pauses);
#endif
}


static void
resume_pages(void) {
#ifdef APP_RN
  title_memory_let_go(false, "back");
#endif
  views_visible(true);
  if(!pause_state.ended) {
    if(pause_state.ui_ended) {  // the pages stayed (the title's WebProcess), the interface's did not
      pause_state.ui_ended = false;
      ui_reload(BAR);
      ui_reload(MENU);
      ui_reload(DIALOG);
      ui_reload(RADIAL);
      ui_reload(SWITCHER);
      keys_reload();
      POC_LOG("back: the interface's pages load again (its WebProcess was ended for the pause)");
    }
    if(pause_state.media) {  // 2c-2: the same WebProcess, the same page
      pause_state.media = false;
      WKPageSetMuted(page(PAGE), kWKMediaNoneMuted);
      run_script(PAGE, AWAY_PLAY_MEDIA, 0, ignore_result);
#ifdef APP_RN
      run_script(PAGE, AWAY_PLAY_FRAMES, 0, ignore_result);
#endif
      POC_LOG("back: views shown, the page's media plays again (full screen: app %d, WebKit's %d, view %d)",
              app.fullscreen, app.native_fullscreen,
              app.panes[PAGE].view ? (int)WKViewIsFullScreen(app.panes[PAGE].view) : -1);
      return;
    }
    POC_LOG("back: views shown");
    return;
  }
  pause_state.ended = false;

  // As after a crash (webProcessDidCrash): a load starts a new WebProcess.
  ui_reload(BAR);
  ui_reload(MENU);
  ui_reload(DIALOG);  // what it showed comes back (uiLoaded())
  ui_reload(RADIAL);
  ui_reload(SWITCHER);
  keys_reload();
  WKBackForwardListRef list = WKPageGetBackForwardList(page(PAGE));
  if(list && WKBackForwardListGetCurrentItem(list)) {
    WKPageReload(page(PAGE));
    POC_LOG("back: the page loads again");
  } else {  // ended before the first page came in
    WKURLRef url = WKPageCopyActiveURL(page(PAGE));
    if(!url) {
      url = WKURLCreateWithUTF8CString(HOME_URL);
    }
#ifdef APP_RN
    app_nav_note(page(PAGE));
#endif
    WKPageLoadURL(page(PAGE), url);
    WKRelease(url);
    POC_LOG("back: the page loads from its address again");
  }
}


static void
away_main(void* arg) {
  intptr_t v = (intptr_t)arg;
  bool away = v & 1;
  bool test = v & 2;

#ifdef APP_RN
  // 2026-11-14: the home screen has been over the app for a moment (the title says so: link.h, leave). The
  // pages are paused already; the browser quits now as its menu's "Sair" does, so that a close from the
  // system, or another game being started, finds no title to end.
  if(v & 4) {
    if(!stopped) {
      POC_LOG("the PS5's home screen is over the app: quitting the clean way (%.0f ms after start)", poc_now_ms() - app.start_ms);
      close_wait_us = LEAVE_WAIT_US;
      stop((void*)1);
    }
    return;
  }
#endif
  if(stopped || away == pause_state.away) {
    return;
  }
  pause_state.away = away;
  POC_LOG("%s%s at %.0f ms", away ? "away: pausing the pages"
                                  : "back: resuming the pages",
          test ? " (test)" : "", poc_now_ms() - app.start_ms);
  if(away) {
    pause_pages();
  } else {
    resume_pages();
#ifdef APP_RN
    if(app.ime_held) {  // the system keyboard's text, now that the views are shown
      char* text = app.ime_held_text;
      app.ime_held = false;
      app.ime_held_text = 0;
      keyboard_text(text);
    }
#endif
  }
}


// On remote.c's reader thread: the title is gone (closed from the system).
// The display thread finds that out only when it shows a frame, and during
// a pause none come.
#ifdef APP_RN
// 2026-11-14 18:3x. The title is gone and this process did not ask for it: the system closed it (the user,
// from its tile: "fechou bem", and the console went down "algum tempo depois"), or it left by itself after
// its WebProcess crashed (13:14, the console went down too). Both times this process was left running
// without its title, closing pages of a WebProcess that no longer exists for 1.5 s and more, and what it
// said went to the title, so nothing of it was ever read. Now it leaves at once: what it wrote goes to a
// file of its own from here on (EXIT_LOG, each line synced: read it after a console that went down), the
// downloads' maps are kept and the session is marked as closed, on the main thread, and _exit; a thread
// that sleeps makes sure of it after ALONE_MS whatever the main thread is doing.
#define EXIT_LOG DATA_DIR "/last-exit.txt"
#define ALONE_MS 1000
static int exit_log_fd = -1;

static void
exit_note(const char* what) {
  char line[256];
  char clock[16] = "";
  time_t now = time(0);
  struct tm tm;

  if(exit_log_fd < 0) {
    return;
  }
  if(localtime_r(&now, &tm)) {
    strftime(clock, sizeof(clock), "%H:%M:%S", &tm);
  }
  int n = snprintf(line, sizeof(line), "[exit] %s, %.0f ms after start: %s\n", clock, poc_now_ms() - app.start_ms, what);
  if(n > 0) {
    if(write(exit_log_fd, line, (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1) < 0) {
      return;
    }
    fsync(exit_log_fd);
  }
}

static void*
alone_thread(void*) {
  usleep(ALONE_MS * 1000);
  exit_note("still here after the wait: _exit from the waiting thread");
  _exit(0);
  return 0;
}

static void
alone_main(void*) {  // main thread
  exit_note(stopped ? "main thread: a quit was under way already: _exit" : "main thread: keeping the downloads' maps");
  if(!stopped) {
    stopped = true;
    multi_keep_all();
    unlink(SESSION_OPEN_FILE);  // a close, not a fall
    exit_note("main thread: done: _exit");
  }
  _exit(0);
}
#endif

static void
on_title_closed(void) {
#ifdef APP_RN
  if(__atomic_load_n(&main_ending, __ATOMIC_ACQUIRE)) {  // main() shut the link itself: it is at its _exit
    return;
  }
  rename(EXIT_LOG, EXIT_LOG ".1");
  exit_log_fd = open(EXIT_LOG, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if(exit_log_fd >= 0) {  // what this process says no longer goes to the title, which is gone
    dup2(exit_log_fd, 1);
    dup2(exit_log_fd, 2);
  }
  exit_note(pause_state.away ? "the title's link closed while the system was over the app: leaving"
                             : "the title's link closed with the app in front: leaving");
  // Its memory goes with it: none of it is mapped here from now on.
  title_memory_let_go(true, "the title closed the link");
  exit_note("none of the title's memory is mapped");
  pthread_t waiting;
  if(!pthread_create(&waiting, 0, alone_thread, 0)) {
    pthread_detach(waiting);
  }
  WKRunLoopCallOnMainThread(alone_main, 0);
#else
  WKRunLoopCallOnMainThread(stop, (void*)1);  // no screenshot: fast
#endif
}


// On remote.c's reader thread.
static void
on_away(int away, int test) {  // test: bit 0 a test's away, bit 1 "leave" (link.h)
  WKRunLoopCallOnMainThread(away_main,
                            (void*)(intptr_t)((away ? 1 : 0) | (test & 1 ? 2 : 0) | (test & 2 ? 4 : 0)));
}
#endif


#ifdef SPEED_TEST
// make SPEED_TEST=1 (2026-11-09): speed_test.h, the same test as NK's
// (Desktop/BrowserPS5, shell.c SPEED_TEST): five sites twice with their
// timings, a scroll and the JavaScript bench, all measured in the page.
#ifdef SPEED_PLAN_FILE
#include SPEED_PLAN_FILE
#else
#include "speed_test.h"
#endif
#define SP_ROW(at, what, url) {at, what, url},
static const struct { int at, what, url; } speed_plan[] = { SPEED_PLAN(SP_ROW) };

static void
speed_said(WKTypeRef result, WKErrorRef error, void*) {
  char text[2048];

  text[0] = 0;
  if(result && WKGetTypeID(result) == WKStringGetTypeID()) {
    take_string((WKStringRef)WKRetain(result), text, sizeof(text));
  } else if(error) {
    take_string(WKErrorCopyLocalizedDescription(error), text, sizeof(text));
  }
  POC_LOG("speed: %s", text[0] ? text : "(no answer)");
}


#ifdef SPEED_TOUR
static void dispatch(void* arg);
#endif

static void
speed_step(void* arg) {
  const int i = (int)(uintptr_t)arg;

  switch(speed_plan[i].what) {
  case SP_LOAD:
    POC_LOG("speed: at %d s, loading %s", speed_plan[i].at, SPEED_URLS[speed_plan[i].url]);
    load_url(SPEED_URLS[speed_plan[i].url]);
    break;
  case SP_TIMING:
    run_script(PAGE, SPEED_TIMING_JS, 0, speed_said);
    break;
  case SP_SCROLL:
    run_script(PAGE, SPEED_SCROLL_JS, 0, speed_said);
    break;
  case SP_SCROLL_READ:
    run_script(PAGE, SPEED_SCROLL_READ_JS, 0, speed_said);
    break;
  case SP_BENCH_SET:
    run_script(PAGE, SPEED_BENCH_JS, 0, ignore_result);
    break;
  case SP_BENCH_RUN:
    run_script(PAGE, SPEED_BENCH_RUN_JS, 0, speed_said);
    break;
  case SP_SHOT:  // 2026-11-10: the screen into the log (poc/shot.ps1)
    screenshot("speed");
    break;
#if defined(SPEED_TABS) && defined(APP_RN)  // feel_plan.h (2026-11-11): tabs, and what the page felt
  case SP_TAB_NEW:
    POC_LOG("speed: at %d s, a new tab with %s", speed_plan[i].at, SPEED_URLS[speed_plan[i].url]);
    tab_new(SPEED_URLS[speed_plan[i].url]);
    break;
  case SP_TAB_GO:
    POC_LOG("speed: at %d s, tab %d in front", speed_plan[i].at, speed_plan[i].url + 1);
    tab_switch(speed_plan[i].url);
    break;
  case SP_FEEL:
    run_script(PAGE, SPEED_FEEL_JS, 0, speed_said);
    break;
  case SP_BAR_FEEL:  // the same for the bar's page: does the interface wait for the pages?
    run_script(BAR, SPEED_BAR_FEEL_JS, 0, speed_said);
    break;
#ifdef SPEED_IME  // ime_plan.h: the system keyboard for the address, with no person (the title closes it)
  case SP_IME:
    POC_LOG("speed: at %d s, the system keyboard for the bar", speed_plan[i].at);
    settings.system_keyboard = 1;
    keyboard_for_bar();
    break;
#endif
#ifdef SPEED_TOUR  // tour_plan.h (2026-11-11): the new functions opened as a person would, with no person
  case SP_UI:  // what a tile of the menu asks for
    POC_LOG("speed: at %d s, the command \"%s\"", speed_plan[i].at, SPEED_UI[speed_plan[i].url]);
    ui_command(SPEED_UI[speed_plan[i].url]);
    break;
  case SP_PAD: {  // the controller, as the title's input.c sends it
    input_event_t* e = (input_event_t*)malloc(sizeof(*e));
    if(e) {
      *e = SPEED_PADS[speed_plan[i].url];
      POC_LOG("speed: at %d s, the controller: event %d (dx %.0f, pressed %d)", speed_plan[i].at,
              (int)e->kind, e->dx, e->pressed);
      dispatch(e);  // frees it
    }
    break;
  }
  case SP_KEYS_JS:
    run_script(KEYS, SPEED_KEYS_JS[speed_plan[i].url], 0, speed_said);
    break;
  case SP_PAGE_JS:
#ifdef LOCAL_PLAN
    if(speed_plan[i].url == J_LOCAL_OPEN) {  // as right after a press on the page (what a new tab asks for)
      last_gesture_ms = poc_now_ms();
      POC_LOG("local test: a site asks the app for a tab on the console itself (right: \"not opened\" next, and nothing at the service)");
    }
#endif
    run_script(PAGE, SPEED_PAGE_JS[speed_plan[i].url], 0, speed_said);
    break;
  case SP_MENU_JS:
    run_script(MENU, SPEED_MENU_JS[speed_plan[i].url], 0, speed_said);
    break;
  case SP_DIALOG_JS:
    run_script(DIALOG, SPEED_DIALOG_JS[speed_plan[i].url], 0, speed_said);
    break;
  case SP_BAR_KEYS:
    POC_LOG("speed: at %d s, the keyboard for the address", speed_plan[i].at);
    keyboard_for_bar();
    break;
  case SP_MENU:
    POC_LOG("speed: at %d s, the menu", speed_plan[i].at);
    menu_toggle();
    break;
#endif
#endif
  default:
    POC_RESULT(1, "speed: done");
    stop(0);
  }
}


static void*
speed_thread(void*) {
  const double start = poc_now_ms() + 3000;

  for(uintptr_t i = 0; i < sizeof(speed_plan) / sizeof(speed_plan[0]); i++) {
    while(poc_now_ms() < start + speed_plan[i].at * 1000.0) {
      usleep(100000);  // sleeps: no spinning (FIFO 700)
    }
    WKRunLoopCallOnMainThread(speed_step, (void*)i);
  }
  return 0;
}


static bool speed_begun;  // the first page is in: the plan runs

static void
speed_test(void) {
  pthread_t thread;

  if(speed_begun) {
    return;
  }
  speed_begun = true;
  POC_LOG("speed test: %zu steps", sizeof(speed_plan) / sizeof(speed_plan[0]));
  pthread_create(&thread, 0, speed_thread, 0);
  pthread_detach(thread);
}


// 2026-11-11 12:40: a test build whose first page never comes stayed open
// with nobody at the controller (the "plus" engine's first run started the
// wrong NetworkProcess: no page at all, and the plan starts with the first
// page). After a minute and a half without one, the test ends by itself.
static void
speed_never(void*) {
  if(!speed_begun) {
    POC_RESULT(0, "speed: no first page in 90 s, the test ends here");
    stop(0);
  }
}

static void*
speed_guard_thread(void*) {
  for(int i = 0; i < 900 && !speed_begun; i++) {
    usleep(100000);  // sleeps: no spinning (FIFO 700)
  }
  if(!speed_begun) {
    WKRunLoopCallOnMainThread(speed_never, 0);
  }
  return 0;
}

#if defined(LOCAL_PLAN) || defined(DL_PLAN)
// dl_plan.h: /big is 256 MiB of nothing, as fast as the page takes it (how
// much the engine can receive when the network is not what limits it).
// local_plan.h (2026-11-11, webkit/security): a service of the console for
// the test, on 127.0.0.1:8099 and the console's own address. It says what each
// connection brought: a page's must bring nothing (given up once made, before
// a byte), the user's own navigation its request, answered with a page.
static void*
local_test_thread(void*) {
  const int s = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in at = {};
  const int on = 1;

  at.sin_family = AF_INET;
  at.sin_port = htons(8099);
  at.sin_addr.s_addr = htonl(INADDR_ANY);
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
  if(s < 0 || bind(s, (struct sockaddr*)&at, sizeof(at)) || listen(s, 8)) {
    POC_LOG("local test: no service (errno %d)", errno);
    return 0;
  }
  POC_LOG("local test: the service listens on 8099");
  for(;;) {
    const int c = accept(s, 0, 0);  // sleeps until someone connects
    if(c < 0) {
      break;
    }
    char request[512];
    struct timeval wait = {2, 0};
    setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &wait, sizeof(wait));
    const ssize_t n = recv(c, request, sizeof(request) - 1, 0);
    if(n > 0) {
      static const char answer[] = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nAccess-Control-Allow-Origin: *\r\n"
                                   "Connection: close\r\nContent-Length: 53\r\n\r\n"
                                   "<title>local service</title>the console's own service";
      request[n] = 0;
      request[strcspn(request, "\r\n")] = 0;
      if(!strncmp(request, "GET /big", 8)) {
        static const char head[] = "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n"
                                   "Connection: close\r\nContent-Length: 268435456\r\n\r\n";
        static char nothing[65536];
        const double t0 = poc_now_ms();
        size_t sent = 0;
        setsockopt(c, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
        if(send(c, head, sizeof(head) - 1, 0) > 0) {
          while(sent < 268435456) {  // send() sleeps while the other end does not take it
            const ssize_t m = send(c, nothing, sizeof(nothing), 0);
            if(m <= 0) {
              break;
            }
            sent += (size_t)m;
          }
        }
        const double s = (poc_now_ms() - t0) / 1000;
        POC_LOG("local test: /big: %.0f MiB taken in %.1f s (%.1f MB/s)", (double)sent / 1048576, s,
                s > 0 ? (double)sent / 1048576 / s : 0);
        close(c);
        continue;
      }
      POC_LOG("local test: a connection brought %d bytes (%s): answered", (int)n, request);
      (void)!send(c, answer, sizeof(answer) - 1, 0);
    } else {
      POC_LOG("local test: a connection brought nothing and went (given up before a byte)");
    }
    close(c);
  }
  return 0;
}
#endif

static void
speed_guard(void) {
  pthread_t thread;
#if defined(LOCAL_PLAN) || defined(DL_PLAN)
  if(!pthread_create(&thread, 0, local_test_thread, 0)) {
    pthread_detach(thread);
  }
#endif
  if(!pthread_create(&thread, 0, speed_guard_thread, 0)) {
    pthread_detach(thread);
  }
}
#endif


#ifdef GO_TEST
// make GO_TEST=lisboa: once the first page is in, the app does what a user
// would in the bar (focus the address, type the text, Enter) and checks
// where the page went; then what the user did when the app crashed at the
// TV (2026-11-03): another site from the bar, back twice, forward. No one
// needs to be at the TV.
static void
go_type(const char* text) {
  set_focus(BAR);
  run_script(BAR, "address.focus(); address.select()", 0, ignore_result);
  unsigned typed = type_text(BAR, text);
  send_key(BAR, KEY_RETURN, 0, 0);
  POC_LOG("go test: typed %u character(s) into the bar, then Enter", typed);
}


static void
go_test_step(void* arg) {
  int step = (int)(uintptr_t)arg;
  char url[4096];

  take_url(WKPageCopyActiveURL(page(PAGE)), url, sizeof(url));
  POC_LOG("go test: step %d, the page is at %s", step, url);
  switch(step) {
  case 0: {
    char expected[1024];
    address_to_url(GO_TEST, expected, sizeof(expected));
    POC_RESULT(!strncmp(url, expected, strlen(expected)),
               "go test: \"%s\" went to %s", GO_TEST, expected);
    go_type("rtp.pt");
    break;
  }
  case 1:
  case 2:
    WKPageGoBack(page(PAGE));
    break;
  case 3:
    WKPageGoForward(page(PAGE));
    break;
  default:
    POC_RESULT(1, "go test: still running after back, back, forward");
    stop(0);
  }
}


static void*
go_test_thread(void*) {
  for(uintptr_t step = 0; step < 5; step++) {
    sleep(10);
    WKRunLoopCallOnMainThread(go_test_step, (void*)step);
  }
  return 0;
}


static void
go_test(void) {
  static bool started;
  pthread_t thread;

  if(started) {
    return;
  }
  started = true;
  go_type(GO_TEST);
  pthread_create(&thread, 0, go_test_thread, 0);
  pthread_detach(thread);
}
#endif


#ifdef NAV_TEST
// make NAV_TEST=<rounds>: the way to the console's freezes of 2026-11-05
// (docs/08-4k.md, Estado): once the home page (the start page) is in, the
// first favorite is clicked, on the start page and (every other round) in
// the bar's favorites, as the user did at 13:15, then ⌂, as at 12:30; a
// step every NAV_TEST_EVERY s. The spin guard (crash-report.c) ends a
// WebProcess that gets stuck; the log says how far the test got.
#define NAV_TEST_EVERY 15

static void
nav_test_step(void* arg) {
  int step = (int)(uintptr_t)arg;
  char url[4096];
  char shown[512];

  take_url(WKPageCopyActiveURL(page(PAGE)), url, sizeof(url));
  POC_LOG("nav test: step %d, the page is at %s", step, url_for_log(url, shown, sizeof(shown)));
  if(step >= 2 * NAV_TEST) {
    POC_RESULT(1, "nav test: %d rounds done", NAV_TEST);
    stop(0);
  } else if(step % 4 == 0) {
    POC_LOG("nav test: the start page's first favorite, clicked");
    run_script(PAGE, "document.querySelector('#grid a.tile').click()", 0, ignore_result);
  } else if(step % 4 == 2) {
    POC_LOG("nav test: the bar's first favorite, clicked");
    run_script(BAR, "document.querySelector('#favbar .fav:not(#all)').click()", 0,
               ignore_result);
  } else {
    POC_LOG("nav test: home");
    home_open();
  }
}


static void*
nav_test_thread(void*) {
  for(uintptr_t step = 0; step <= 2 * NAV_TEST; step++) {
    sleep(NAV_TEST_EVERY);
    WKRunLoopCallOnMainThread(nav_test_step, (void*)step);
  }
  return 0;
}


static void
nav_test(void) {
  static bool started;
  pthread_t thread;

  if(started) {
    return;
  }
  started = true;
  pthread_create(&thread, 0, nav_test_thread, 0);
  pthread_detach(thread);
}
#endif


#ifdef H2_TEST
// make H2_TEST=1 HOME_URL=https://www.google.com/ (docs/22-http2.md): the
// three pages twice each, H2_TEST_EVERY s apart (the first is the HOME_URL).
// After each load, one line from the page's own Navigation and Resource
// Timing: the protocol of the page and of what it loaded ("h2" is HTTP/2,
// "http/1.1" not), its times, and what came without the network
// (transferSize 0: a cache; cross-origin entries without Timing-Allow-Origin
// show nothing, "hidden"). At the end, what the disk cache (docs/21) holds.
// The test writes neither the history nor the last page (history_note()),
// nor the tabs (HOME_URL).
#define H2_TEST_EVERY 25
static const char* const h2_test_urls[] = {
  "https://www.google.com/",
  "https://pt.wikipedia.org/wiki/Lisboa",
  "https://github.com/",
};
#define H2_TEST_SITES ((int)(sizeof(h2_test_urls) / sizeof(h2_test_urls[0])))
#define H2_TEST_VISITS (2 * H2_TEST_SITES)
static double h2_test_asked_ms;           // when the page was asked for
static int h2_test_visit;                 // the page asked for: 0 .. VISITS - 1
static double h2_test_ms[H2_TEST_VISITS]; // asked -> loaded, ms; 0: never
static int h2_test_h2[H2_TEST_VISITS];    // 1: the page came over HTTP/2

static const char h2_test_script[] =
  "(() => {"
  " const n = performance.getEntriesByType('navigation')[0];"
  " if (!n) return 'no navigation entry';"
  " const by = {}; let net = 0, cache = 0, hidden = 0, bytes = 0;"
  " for (const e of performance.getEntriesByType('resource')) {"
  "  const p = e.nextHopProtocol || '-'; by[p] = (by[p] || 0) + 1;"
  "  if (!e.decodedBodySize && !e.transferSize) hidden++;"
  "  else if (!e.transferSize) cache++;"
  "  else { net++; bytes += e.transferSize; }"
  " }"
  " const t = (v) => Math.round(v || 0);"
  " return 'page=' + (n.nextHopProtocol || '-') + ' page_bytes=' + t(n.transferSize) +"
  "  ' ttfb=' + t(n.responseStart - n.startTime) + ' dom=' + t(n.domContentLoadedEventEnd) +"
  "  ' load=' + t(n.loadEventEnd) + ' resources=' + JSON.stringify(by) +"
  "  ' net=' + net + ' cache=' + cache + ' hidden=' + hidden + ' net_kib=' + t(bytes / 1024);"
  "})()";


static void
h2_test_result(WKTypeRef result, WKErrorRef, void* ctx) {
  const int visit = (int)(uintptr_t)ctx;
  char text[1024] = "no answer";

  if(result && WKGetTypeID(result) == WKStringGetTypeID()) {
    WKStringGetUTF8CString((WKStringRef)result, text, sizeof(text));
  }
  h2_test_h2[visit] = !strncmp(text, "page=h2 ", 8);
  POC_LOG("h2 test: visit %d (%s, %s): loaded %.0f ms after asked; %s", visit + 1,
          h2_test_urls[visit % H2_TEST_SITES], visit < H2_TEST_SITES ? "first" : "again",
          h2_test_ms[visit], text);
}


// Files and bytes under dir (the disk cache's folders, a few deep).
static void
h2_test_walk(const char* dir, int depth, long long* files, long long* bytes) {
  DIR* d = opendir(dir);

  for(struct dirent* e; d && (e = readdir(d));) {
    char full[1024];
    struct stat st;

    if(!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) {
      continue;
    }
    snprintf(full, sizeof(full), "%s/%s", dir, e->d_name);
    if(lstat(full, &st)) {
      continue;
    }
    if(S_ISDIR(st.st_mode) && depth < 8) {
      h2_test_walk(full, depth + 1, files, bytes);
    } else if(S_ISREG(st.st_mode)) {
      (*files)++;
      *bytes += st.st_size;
    }
  }
  if(d) {
    closedir(d);
  }
}


static void
h2_test_step(void* arg) {
  const int visit = (int)(uintptr_t)arg;

  if(visit < H2_TEST_VISITS) {
    h2_test_visit = visit;
    h2_test_asked_ms = poc_now_ms();
    POC_LOG("h2 test: visit %d, %s", visit + 1, h2_test_urls[visit % H2_TEST_SITES]);
    load_url(h2_test_urls[visit % H2_TEST_SITES]);
    return;
  }
  int loaded = 0, over_h2 = 0;
  for(int i = 0; i < H2_TEST_VISITS; i++) {
    loaded += h2_test_ms[i] > 0;
    over_h2 += h2_test_h2[i];
  }
  long long files = 0, bytes = 0;
  h2_test_walk(CACHE_DIR, 0, &files, &bytes);
  POC_LOG("h2 test: the disk cache (%s) holds %lld files, %lld KiB", CACHE_DIR, files,
          bytes / 1024);
  POC_RESULT(loaded == H2_TEST_VISITS, "h2 test: %d of %d pages loaded, %d of them over HTTP/2",
             loaded, H2_TEST_VISITS, over_h2);
  stop(0);
}


static void*
h2_test_thread(void*) {
  for(uintptr_t visit = 1; visit <= H2_TEST_VISITS; visit++) {
    sleep(H2_TEST_EVERY);
    WKRunLoopCallOnMainThread(h2_test_step, (void*)visit);
  }
  return 0;
}


// Each load of the page: the first one is the HOME_URL's, which starts the
// steps; only the first finish after a page was asked for counts.
static void
h2_test(void) {
  static bool started;
  static int measured = -1;
  pthread_t thread;

  if(!started) {
    started = true;
    h2_test_asked_ms = app.start_ms;
    pthread_create(&thread, 0, h2_test_thread, 0);
    pthread_detach(thread);
  }
  if(measured == h2_test_visit) {
    return;
  }
  measured = h2_test_visit;
  h2_test_ms[h2_test_visit] = poc_now_ms() - h2_test_asked_ms;
  run_script(PAGE, h2_test_script, (void*)(uintptr_t)h2_test_visit, h2_test_result);
}
#endif


// The pane under (x, y) on the screen: the keyboard, then the panels, then
// the bar or the page. -1: outside the dialog, which is the only one then.
static int
pane_at(float x, float y) {
#ifdef APP_RN  // the board only (KEYS_BOARD_LEFT, KEYS_BOARD_WIDTH): the page is in view at its sides
  if(app.keys_open && y >= app.panes[KEYS].top && x >= KEYS_BOARD_LEFT && x < KEYS_BOARD_LEFT + KEYS_BOARD_WIDTH) {
    return KEYS;
  }
#else
  if(app.keys_open && y >= app.panes[KEYS].top) {
    return KEYS;
  }
#endif
  static const int panels[] = {DIALOG, MENU};
  for(int i : panels) {
    const pane_t* p = &app.panes[i];
    bool open = i == DIALOG ? app.dialog_open : app.menu_open;
    if(open && x >= p->left && x < p->left + p->width && y >= p->top
       && y < p->top + p->height) {
      return i;
    }
  }
  if(app.dialog_open) {
    return -1;
  }
  return !app.fullscreen && y < app.bar_height ? BAR : PAGE;
}


// While the keyboard is open, the buttons work it (keyboard.html): the
// D-pad moves its highlight, square presses that key, circle deletes,
// triangle closes it and R1 is its Enter. Cross presses the key under the
// cursor, but the highlighted one once the D-pad has moved the highlight
// (until the stick moves the cursor again), and always when the cursor is
// off the keyboard. Until 2026-11-05 14:20 cross clicked where the cursor
// was, still on the page's field: the field took the click, the keyboard
// opened again empty, and nothing was ever typed (the TV, the start page's
// search, five times in a row).
static bool keys_dpad;   // the D-pad moved the highlight since the stick last moved
static bool keys_cross;  // cross went down as a key: its up is ours too

static bool
keys_input(const input_event_t* e) {
  char script[64];

  if(e->kind == INPUT_BUTTON_UP && keys_cross) {  // the keyboard may have closed since
    keys_cross = false;
#ifdef APP_RN
    if(app.keys_open) {  // RN's keyboard: a held cross opens a letter's accents (kb.down, kb.up)
      run_script(KEYS, "kb.up()", 0, ignore_result);
    }
#endif
    return true;
  }
#ifdef APP_RN
  // Square held (RN's title, input.c INPUT_SQUARE_REPEAT: dx 1) deletes on
  // in the keyboard, and is nothing anywhere else.
  if(e->kind == INPUT_ENTER && e->dx > 0 && !app.keys_open) {
    return true;
  }
#endif
  if(e->kind == INPUT_MOVE) {
    keys_dpad = false;
  }
  if(!app.keys_open) {
    keys_dpad = false;
    return false;
  }
  switch(e->kind) {
  case INPUT_SCROLL: {
    int dx, dy;
    if(!dpad_step(e, &dx, &dy)) {
      return false;  // the right stick still scrolls the page
    }
    keys_dpad = true;
    snprintf(script, sizeof(script), "kb.move(%d, %d)", dx, dy);
    break;
  }
  case INPUT_BUTTON_DOWN:
    if(!keys_dpad && pane_at(e->x, e->y) == KEYS) {
      return false;  // a click on the key under the cursor
    }
    last_gesture_ms = poc_now_ms();
    keys_cross = true;
#ifdef APP_RN
    snprintf(script, sizeof(script), "kb.down()");
#else
    snprintf(script, sizeof(script), "kb.pressHighlighted()");
#endif
    break;
#ifdef APP_RN
  // BrowserPS5 RN (2026-11-11; the system's took the console down twice): the
  // buttons of the PS5's own keyboard (pages/keyboard.html). Square deletes
  // (held: on and on, RN's title sends it again with dx 1), triangle is the
  // space, circle closes (a letter's accents first), L1 and R1 move the
  // caret (L1 is circle's event with dx -1 from RN's title; from a title
  // before that it closes, as circle), L2 is the shift, L3 the symbols, R3
  // the accents' page, R2 confirms.
  case INPUT_ENTER:
    snprintf(script, sizeof(script), "kb.backspace()");
    break;
  case INPUT_BACK:
    snprintf(script, sizeof(script), e->dx < 0 ? "kb.caret(-1)" : "kb.back()");
    break;
  case INPUT_KEYBOARD:
    snprintf(script, sizeof(script), "kb.space()");
    break;
  case INPUT_FORWARD:
    snprintf(script, sizeof(script), "kb.caret(1)");
    break;
  case INPUT_RADIAL:
    if(!e->pressed) {
      return true;  // the trigger coming back up: no radial menu under the keyboard
    }
    snprintf(script, sizeof(script), e->dx < 0 ? "kb.shift()" : "kb.done()");
    break;
  case INPUT_STICK:
    snprintf(script, sizeof(script), e->dx < 0 ? "kb.layer('sym')" : "kb.layer('acc')");
    break;
#else
  case INPUT_ENTER:
    snprintf(script, sizeof(script), "kb.pressHighlighted()");
    break;
  case INPUT_BACK:
    snprintf(script, sizeof(script), "kb.backspace()");
    break;
  case INPUT_KEYBOARD:
    snprintf(script, sizeof(script), "kb.cancel()");
    break;
  case INPUT_FORWARD:
    snprintf(script, sizeof(script), "kb.done()");
    break;
#endif
  default:
    return false;
  }
  run_script(KEYS, script, 0, ignore_result);
  return true;
}


// (x, y) on the screen; the pane gets them in its own pixels.
static void
mouse(int pane, WKEventType type, WKEventMouseButton button, float x,
      float y, int clicks) {
  const pane_t* p = &app.panes[pane];
  WKPageHandleMouseEvent(page(pane), WKMouseEventMake(type, button,
      WKPointMake(x - p->left, y - p->top), clicks, 0));
}


// With MOUSE_MOVE_MS, the newest move the page has not had yet (main thread
// only).
static struct {
  bool pending;
  input_event_t event;
  double sent_ms;
} moves;

static void
move_to(const input_event_t* e) {
  int pane = app.pressed >= 0 ? app.pressed : pane_at(e->x, e->y);

  moves.pending = false;
  moves.sent_ms = poc_now_ms();
  if(pane < 0) {  // outside the dialog
    return;
  }
  if(pane != app.hover && app.pressed < 0) {
    // Off the pane it was over, for its mouseleave and :hover.
    if(app.hover >= 0) {
      const pane_t* h = &app.panes[app.hover];
      mouse(app.hover, kWKEventMouseMove, kWKEventMouseButtonNoButton, h->left - 1,
            h->top - 1, 0);
    }
    app.hover = pane;
  }
  mouse(pane, kWKEventMouseMove, e->pressed ? kWKEventMouseButtonLeftButton
                                            : kWKEventMouseButtonNoButton,
        e->x, e->y, 0);
  if(app.cursor_type == CURSOR_TYPE_IBEAM && poc_now_ms() - editable_checked_ms > 120) {
    editable_check();
  }
}


// The move held back, if any (the timer's tick, a click).
static void
moves_flush(bool now) {
  if(moves.pending && (now || poc_now_ms() - moves.sent_ms >= MOUSE_MOVE_MS)) {
    move_to(&moves.event);
  }
}


// The right stick, or the D-pad with "scroll" in the settings: a wheel at
// the cursor, to the panel under it or the page.
static void
wheel(float x, float y, float dx, float dy) {
  int pane = pane_at(x, y);

  if(pane < 0 || (app.dialog_open && pane != DIALOG)) {
    return;
  }
  if(pane != MENU && pane != DIALOG) {
    pane = PAGE;
  }
  const pane_t* p = &app.panes[pane];
  float px = x - p->left;
  float py = y < p->top ? 0 : y - p->top;
  // A wheel's delta is positive upwards; a tick is 40 pixels.
  WKPageHandleWheelEvent(page(pane), WKWheelEventMake(kWKEventWheel,
      WKPointMake(px, py), WKSizeMake(-dx, -dy), WKSizeMake(-dx / 40, -dy / 40), 0));
}


// 2026-11-11: the D-pad as the arrow keys, to the page: what a page made for a keyboard or a television's
// remote listens to (a game, a slide show, a video player's seek and volume). Chosen in the settings
// (settings.dpad "keys"), and by itself on YouTube's page for televisions (youtube.com/tv, which this browser
// is given under its own name, 22:27: the film starts by itself in 2160p). While the cursor stays where it was
// at the last arrow, cross is Enter (the page's own highlight is what gets pressed, not what is under the cursor).
static struct {
  double ms;   // when the last arrow went, 0: none yet
  float x, y;  // where the cursor was
} dpad_keys;

static bool
dpad_keys_now(void) {
  if(!strcmp(settings.dpad, "keys")) {
    return true;
  }
  return tv_page_now();
}

// Cross, pressed over the page: Enter instead of a click? (The D-pad drove last, and the cursor has not moved.)
static bool
dpad_keys_enter(float x, float y) {
  return dpad_keys.ms > 0 && fabsf(x - dpad_keys.x) < 3 && fabsf(y - dpad_keys.y) < 3 && dpad_keys_now();
}

static void
scroll(const input_event_t* e) {
  int dx, dy;
  bool by_element = strcmp(settings.dpad, "scroll") || app.menu_open || app.dialog_open;

  // X11 key codes, as send_key() takes them. The bar, the menu and the windows keep the jump between their elements.
  if(!app.menu_open && !app.dialog_open && dpad_step(e, &dx, &dy) && dpad_keys_now()) {
    float x, y;
    input_cursor(&x, &y);
    if(pane_at(x, y) != BAR) {
      ring_off();
      set_focus(PAGE);
      send_key(PAGE, dy < 0 ? 0xff52 : dy > 0 ? 0xff54 : dx < 0 ? 0xff51 : 0xff53, "", 0);  // Up, Down, Left, Right
      app.keys_drive = true;  // the cursor goes (display_thread) until the stick moves
      dpad_keys.ms = poc_now_ms();
      dpad_keys.x = x;
      dpad_keys.y = y;
      return;
    }
  }
  if(dpad_step(e, &dx, &dy) && by_element) {
    float x, y;
    input_cursor(&x, &y);
    int pane;
    if(app.dialog_open) {
      pane = DIALOG;
    } else if(app.menu_open) {
      pane = MENU;
    } else if(app.ring.on && !app.nav_fresh) {
      pane = app.ring.pane;
    } else {
      pane = pane_at(x, y);
      if(pane != BAR) {
        pane = PAGE;
      }
    }
    const char* cmd = dy < 0 ? "up" : dy > 0 ? "down" : dx < 0 ? "left" : "right";
    nav_run(pane, cmd, x, y, app.nav_fresh || !app.ring.on, false);
    return;
  }
  ring_off();
  app.nav_fresh = true;
  wheel(e->x, e->y, e->dx, e->dy);
}


#ifdef APP_RN
// 2026-11-17 (reported with 1.0.0-rc13): circle on YouTube's page for televisions (tv_page_now()) went back a
// page of the browser, and from a film that was the browser's start page: the page keeps its own screens (its
// home, a film, the search) out of the browser's history. Now circle is the page's own "back", the Escape key,
// as a television's remote sends it: a film goes back to the screen it was opened from. On the page's first
// screen (its address has nothing after "#/": its home, or its welcome with no account) there is nothing of the
// page to go back to: there circle leaves it, to the last page of this tab that is not this one (the page may
// have left entries of its own in the list), or to the start page when the tab began with it.
static bool
tv_url_first(const char* url) {
  const char* at = strchr(url, '#');

  if(!at) {
    return true;
  }
  at += at[1] == '/' ? 2 : 1;
  return *at == 0 || *at == '?';
}

static void
tv_page_back(void) {
  char url[4096];

  take_url(WKPageCopyActiveURL(page(PAGE)), url, sizeof(url));
  if(!tv_url_first(url)) {
    set_focus(PAGE);
    send_key(PAGE, 0xff1b, 0, 0);
    POC_LOG("tv: circle: the page's own back (Escape)");
    return;
  }
  WKBackForwardListRef list = WKPageGetBackForwardList(page(PAGE));
  const unsigned count = list ? WKBackForwardListGetBackListCount(list) : 0;
  for(unsigned i = 1; i <= count; i++) {
    WKBackForwardListItemRef item = WKBackForwardListGetItemAtIndex(list, -(int)i);
    if(!item) {
      break;
    }
    take_url(WKBackForwardListItemCopyURL(item), url, sizeof(url));
    if(!tv_url(url)) {
      POC_LOG("tv: circle on the page's first screen: out of it, %u back in this tab's list", i);
      WKPageGoToBackForwardListItem(page(PAGE), item);
      return;
    }
  }
  POC_LOG("tv: circle on the page's first screen, and the tab began with it: the start page");
  home_open();
}
#endif

// Circle: what is open closes first, then the full screen; then back.
static void
go_back(void) {
  if(app.dialog_open) {
    run_script(DIALOG, "dialog.cancel()", 0, ignore_result);
  } else if(app.menu_open) {
    menu_hide();
  } else if(app.fullscreen && !tv_page_now()) {  // (YouTube's page for televisions: circle is its own "back")
    if(!native_fullscreen_exit()) {
      run_script(PAGE, "window.__bps5exitFullscreen && __bps5exitFullscreen()", 0,
                 ignore_result);
      fullscreen_set(false);
    }
#ifdef APP_RN
  } else if(settings_tab_back()) {  // the settings' own tab: closed
  } else if(tv_page_now()) {
    tv_page_back();
#endif
  } else {
    WKPageGoBack(page(PAGE));
  }
}


// The magnet (settings.magnet): MAGNET_IDLE_MS after the stick stops, the
// ring goes round what is under the cursor, or the cursor jumps to the
// clickable thing within 40 pixels of it (nav.js "near"). A thread that
// sleeps until then (never spins: docs, "no spinning threads") asks the
// main thread.
static pthread_mutex_t magnet_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t magnet_cond = PTHREAD_COND_INITIALIZER;
static bool magnet_armed;

static void
magnet_fire(void*) {
  float x, y;

  if(!settings.magnet || app.ring.on || app.menu_open || app.keys_open || app.radial_open
     || app.switcher_open
     || app.fullscreen || app.pressed >= 0 || poc_now_ms() - app.stick_ms < MAGNET_IDLE_MS) {
    return;
  }
  input_cursor(&x, &y);
  int pane = pane_at(x, y);
  if(pane == PAGE || pane == BAR || pane == DIALOG) {
    nav_run(pane, "near", x, y, true, false);
  }
}

static void*
magnet_thread(void*) {
  pthread_mutex_lock(&magnet_lock);
  while(!__atomic_load_n(&app.quit, __ATOMIC_RELAXED)) {
    if(!magnet_armed) {
      pthread_cond_wait(&magnet_cond, &magnet_lock);
      continue;
    }
    double due = app.stick_ms + MAGNET_IDLE_MS, now = poc_now_ms();
    if(now < due) {  // asleep until then; a later move pushes it back
      struct timespec until;
      clock_gettime(CLOCK_REALTIME, &until);
      long ns = until.tv_nsec + (long)((due - now) * 1e6) + 1000000;
      until.tv_sec += ns / 1000000000;
      until.tv_nsec = ns % 1000000000;
      pthread_cond_timedwait(&magnet_cond, &magnet_lock, &until);
      continue;
    }
    magnet_armed = false;
    pthread_mutex_unlock(&magnet_lock);
    WKRunLoopCallOnMainThread(magnet_fire, 0);
    pthread_mutex_lock(&magnet_lock);
  }
  pthread_mutex_unlock(&magnet_lock);
  return 0;
}

static void
magnet_arm(void) {
  app.stick_ms = poc_now_ms();
  if(!settings.magnet) {
    return;
  }
  pthread_mutex_lock(&magnet_lock);
  if(!magnet_armed) {
    magnet_armed = true;
    pthread_cond_signal(&magnet_cond);
  }
  pthread_mutex_unlock(&magnet_lock);
}


// ---------------------------------------------------------------------------
// The tabs (docs/07 "Separadores"): up to TABS_MAX pages, each in a view of
// its own; the PAGE pane shows the one in front (app.panes[PAGE].view = its
// view), the others are hidden. Their switcher (tabs.html, the SWITCHER
// pane) is a carousel over the whole screen, from the touch pad, L2's
// radial menu or the bar's button; each tab shows its last picture, a BMP
// the app writes into TABS_DIR when the tab goes behind or the switcher
// opens. Events of a page behind are mostly dropped (p != page(PAGE)).

#define TABS_MAX     6
#define THUMB_SCALE  3     // a picture is the page a third as big

typedef struct tab {
  WKViewRef view;
  int id;          // its picture's name; never reused in a run
  unsigned shots;  // pictures taken (in its URL, so none is cached)
  char* pending;   // a tab of the last time: its address, loaded when it comes in front
  bool unread;     // RN: its site sent a notification while it was behind (notify_take())
  uint64_t notice; // RN: the engine's number of that notification, 0 when it was notify_js's (notify_native_show())
  bool priv;       // RN: a private tab (tab_new_private()): its own data, nothing kept
} tab_t;

static tab_t tabs[TABS_MAX];
static int tab_count, tab_current, tab_next_id = 1;
#ifdef APP_RN
static WKNotificationManagerRef notify_manager;  // the engine's, once the app shows its notifications (notify_native_install())
#endif
static WKContextRef ui_context;

static struct {
  int sel;              // the card chosen: a tab, or tab_count (a new one)
  float vx;             // the stick's pull, as in the radial menus
  float last_x;
  double stepped_ms;    // when the stick last moved the choice
  float saved_x, saved_y;  // the cursor before it opened
} switcher;


// 24 bits, bottom up).
static void
tab_shoot(int i) {
  char path[256];
  pane_t* p = &app.panes[PAGE];
  int w = p->width / THUMB_SCALE, h = p->height / THUMB_SCALE;
  size_t row = ((size_t)w * 3 + 3) & ~(size_t)3;
  size_t size = 54 + row * h;
  uint8_t* bmp = (uint8_t*)calloc(1, size);

  if(!bmp || w <= 0 || h <= 0) {
    free(bmp);
    return;
  }
  pthread_mutex_lock(&app.lock);
  for(int y = 0; y < h; y++) {
    uint8_t* out = bmp + 54 + row * (size_t)(h - 1 - y);
    for(int x = 0; x < w; x++) {
      unsigned r = 0, g = 0, b = 0;
      for(int dy = 0; dy < THUMB_SCALE; dy++) {
        const uint32_t* src = p->pixels + (size_t)(y * THUMB_SCALE + dy) * p->stride
                              + x * THUMB_SCALE;
        for(int dx = 0; dx < THUMB_SCALE; dx++) {
          r += (src[dx] >> 16) & 0xff;
          g += (src[dx] >> 8) & 0xff;
          b += src[dx] & 0xff;
        }
      }
      unsigned n = THUMB_SCALE * THUMB_SCALE;
      out[x * 3] = (uint8_t)(b / n);
      out[x * 3 + 1] = (uint8_t)(g / n);
      out[x * 3 + 2] = (uint8_t)(r / n);
    }
  }
  pthread_mutex_unlock(&app.lock);

  // The headers, little endian.
  uint32_t fields[] = {(uint32_t)size, 0, 54, 40, (uint32_t)w, (uint32_t)h};
  bmp[0] = 'B';
  bmp[1] = 'M';
  memcpy(bmp + 2, &fields[0], 4);
  memcpy(bmp + 10, &fields[2], 4);
  memcpy(bmp + 14, &fields[3], 4);
  memcpy(bmp + 18, &fields[4], 4);
  memcpy(bmp + 22, &fields[5], 4);
  bmp[26] = 1;   // planes
  bmp[28] = 24;  // bits per pixel
  uint32_t image = (uint32_t)(row * h);
  memcpy(bmp + 34, &image, 4);

  snprintf(path, sizeof(path), TABS_DIR "/%d.bmp", tabs[i].id);
  FILE* f = fopen(path, "wb");
  if(f) {
    if(fwrite(bmp, 1, size, f) == size) {
      tabs[i].shots++;
    }
    fclose(f);
  } else {
    POC_PERROR(path);
  }
  free(bmp);
}


static void
bar_tabs(void) {
#ifdef APP_RN
  // BrowserPS5 RN (2026-11-10 22:20): the open tabs under the address
  // (chrome.html's strip), each one's title and host, and the one in front:
  // the user went from site to site with the favorites, each load anew.
  text_t t = {};
  char url[4096];
  char title[512];
  char host[256];

  text_printf(&t, "setTabs && setTabs(%d, [", tab_count);
  for(int i = 0; i < tab_count; i++) {
    WKPageRef p = WKViewGetPage(tabs[i].view);
    if(tabs[i].pending) {
      snprintf(url, sizeof(url), "%s", tabs[i].pending);
    } else {
      take_url(WKPageCopyActiveURL(p), url, sizeof(url));
    }
    take_string(WKPageCopyTitle(p), title, sizeof(title));
    const bool web = url_is_web(url);
    text_printf(&t, "%s{title: ", i ? ", " : "");
    text_json(&t, title[0] ? title : web ? url_host(url, host, sizeof(host)) : "Início");
    text_add(&t, ", host: ");
    text_json(&t, web ? url_host(url, host, sizeof(host)) : "");
    if(tabs[i].unread) {
      text_add(&t, ", n: 1");
    }
    if(tabs[i].priv) {
      text_add(&t, ", p: 1");
    }
    text_add(&t, "}");
  }
  text_printf(&t, "], %d, %d)", tab_current, closed_count);
  if(t.failed || !t.s || strlen(t.s) >= sizeof(bar_tabs_sent) || !strcmp(t.s, bar_tabs_sent)) {
    free(t.s);
    return;
  }
  strcpy(bar_tabs_sent, t.s);
  text_run(BAR, &t);
#else
  char script[32];
  snprintf(script, sizeof(script), "setTabs && setTabs(%d)", tab_count);
  run_script(BAR, script, 0, ignore_result);
#endif
}


// The PAGE pane has another view: what goes with the page in front.
static void
tab_front(void) {
  pane_t* p = &app.panes[PAGE];

  WKViewSetSize(p->view, WKSizeMake(p->width, p->height));
  WKViewSetVisible(p->view, true);
  WKViewSetActive(p->view, true);
  WKPageSetMuted(page(PAGE), kWKMediaNoneMuted);
  WKPageSetPageZoomFactor(page(PAGE), app.zoom);
  user_agent_apply();
  preferences_apply(own_page_shown(0));  // shared by the tabs
  tv_page_fullscreen();
  ring_off();
  app.nav_fresh = true;
  app.loading = false;
#ifdef GPU_COMPOSITING
  p->next_open_ms = 0;
  p->open_soon_ms = poc_now_ms() + 3000;
  layer_ids_update();
#endif
  set_focus(PAGE);
  app.bar_state[0] = 0;
  bar_update(0);
  bar_tabs();
  pthread_mutex_lock(&app.lock);
  app.dirty = true;
  pthread_cond_signal(&app.cond);
  pthread_mutex_unlock(&app.lock);
}


// The tab in front goes behind: its picture, hidden.
static void
tab_behind(void) {
  game_set(false);  // the game stays behind; its tab asks again when it comes back
  game_wanted = false;
  tab_shoot(tab_current);
  if(app.fullscreen) {  // the page behind is told, as circle does
    run_script(PAGE, "window.__bps5exitFullscreen && __bps5exitFullscreen()", 0,
               ignore_result);
  }
  // Nothing plays behind: its videos pause (the decoder is the costly part)
  // and the whole page is muted, frames of other sites included.
  run_script(PAGE, "(function p(d){try{d.querySelectorAll('video,audio')"
             ".forEach(function(m){if(!m.paused)m.pause();});"
             "d.querySelectorAll('iframe').forEach(function(f){try{p(f.contentDocument);}"
             "catch(e){}});}catch(e){}})(document)", 0, ignore_result);
  WKPageSetMuted(page(PAGE), kWKMediaAudioMuted);
  WKViewSetVisible(tabs[tab_current].view, false);
  WKViewSetActive(tabs[tab_current].view, false);
}


static void
tab_switch(int i) {
  if(i == tab_current || i < 0 || i >= tab_count) {
    return;
  }
  tab_behind();
  tab_current = i;
  app.panes[PAGE].view = tabs[i].view;
  tab_front();
  if(tabs[i].pending) {  // a tab of the last time, in front for the first time
    char* url = tabs[i].pending;
    tabs[i].pending = 0;
    load_url(url);
    free(url);
  } else if(WKPageGetProcessIdentifier(page(PAGE)) <= 0) {
    // Its WebProcess ended while it was behind (a crash, the pause): again.
    WKBackForwardListRef list = WKPageGetBackForwardList(page(PAGE));
    if(list && WKBackForwardListGetCurrentItem(list)) {
      WKPageReload(page(PAGE));
    } else {
      home_open();
    }
  }
#ifdef APP_RN
  if(tabs[i].unread) {  // notify_take(): its site's last notice, "clicked"
    tabs[i].unread = false;
    run_script(PAGE, "window.__bps5_notify_click && window.__bps5_notify_click()", 0, ignore_result);
    if(tabs[i].notice && notify_manager) {  // the engine's own: its click event
      WKNotificationManagerProviderDidClickNotification(notify_manager, tabs[i].notice);
    }
    tabs[i].notice = 0;
    bar_tabs();
  }
#endif
  POC_LOG("tabs: %d of %d in front", i + 1, tab_count);
  session_note();
}


static void page_client_install(WKPageRef p);
static WKViewRef view_create(pane_t* pane, int width, int height, WKContextRef context,
                             WKWebsiteDataStoreRef data, WKPreferencesRef preferences,
                             WKUserContentControllerRef content);

#ifdef APP_RN
static WKWebsiteDataStoreRef private_store;  // the private tabs', made with the first one
#endif

// A new tab in front, with url (http or https) or the home page.
static bool
tab_new(const char* url) {
  pane_t* p = &app.panes[PAGE];

  if(tab_count >= TABS_MAX) {
    POC_LOG("tabs: already %d, none more", TABS_MAX);
    return false;
  }
#ifdef APP_RN
  // A private tab (tab_new_private()): a data store of its own that is never
  // written (cookies, storage, cache), shared by the private tabs of this run.
  const bool priv = tab_private_next;
  tab_private_next = false;
  if(priv && !private_store) {
    private_store = WKWebsiteDataStoreCreateNonPersistentDataStore();
  }
  WKViewRef view = view_create(p, p->width, p->height, ui_context, priv ? private_store : data_store,
                               page_preferences, page_content);
#else
  const bool priv = false;
  WKViewRef view = view_create(p, p->width, p->height, ui_context, data_store,
                               page_preferences, page_content);
#endif
  page_client_install(WKViewGetPage(view));
  tab_behind();
  int i = tab_count++;
  tabs[i].view = view;
  tabs[i].id = tab_next_id++;
  tabs[i].shots = 0;
  tabs[i].pending = 0;
  tabs[i].unread = false;
  tabs[i].notice = 0;
  tabs[i].priv = priv;
#ifdef ENGINE_PLUS
  WKPageSetUseDarkAppearanceForTesting(WKViewGetPage(view), settings.dark_sites);
#endif
  tab_current = i;
  p->view = view;
  tab_front();
  if(url) {
    load_url(url);
  } else {
    home_open();
  }
  POC_LOG("tabs: a new one, %d of %d", i + 1, tab_count);
  session_note();
  return true;
}


// The last one is not closed: it goes to the home page.
static void
tab_close(int i) {
  char path[256];

  if(i < 0 || i >= tab_count) {
    return;
  }
  if(tab_count == 1) {
    home_open();
    return;
  }
  if(i == tab_current) {
    tab_switch(i + 1 < tab_count ? i + 1 : i - 1);
  }
  WKViewRef view = tabs[i].view;
#ifdef APP_RN
  {  // its address, for the strip's "reopen" (not a private tab's, not one of the browser's own pages)
    char url[sizeof(closed_tabs[0])];
    if(tabs[i].pending) {
      snprintf(url, sizeof(url), "%s", tabs[i].pending);
    } else {
      take_url(WKPageCopyActiveURL(WKViewGetPage(view)), url, sizeof(url));
    }
    if(!tabs[i].priv && url_is_web(url)) {
      const int max = (int)(sizeof(closed_tabs) / sizeof(*closed_tabs));
      if(closed_count == max) {
        memmove(closed_tabs[0], closed_tabs[1], sizeof(closed_tabs[0]) * (size_t)(max - 1));
        closed_count--;
      }
      snprintf(closed_tabs[closed_count++], sizeof(closed_tabs[0]), "%s", url);
    }
  }
#endif
  snprintf(path, sizeof(path), TABS_DIR "/%d.bmp", tabs[i].id);
  unlink(path);
  free(tabs[i].pending);
  memmove(&tabs[i], &tabs[i + 1], sizeof(tab_t) * (tab_count - i - 1));
  tab_count--;
  if(tab_current > i) {
    tab_current--;
  }
  WKPageClose(WKViewGetPage(view));
  WKRelease(view);
  bar_tabs();
  POC_LOG("tabs: one closed, %d left", tab_count);
  session_note();
}


#ifdef ENGINE_PLUS
static void
dark_appearance_apply(void) {
  for(int i = 0; i < tab_count; i++) {
    if(tabs[i].view) {
      WKPageSetUseDarkAppearanceForTesting(WKViewGetPage(tabs[i].view), settings.dark_sites);
    }
  }
}
#endif

#ifdef APP_RN
// notify_js's notices (browserps5:notify?<title>&<text>&<icon>, each
// URL-encoded), from the main frame of any tab whose committed address is
// an https one of notify_sites: the PS5's toast (its "Abrir" opens the app)
// and, for a tab behind, the strip's dot. Toasts: one each 2 s, 8 a minute.
static time_t notify_last, notify_minute;
static int notify_in_minute;

static const char*
notify_site(const char* host) {
  const size_t n = strlen(host);
  for(size_t i = 0; i < sizeof(notify_sites) / sizeof(*notify_sites); i++) {
    const char* d = notify_sites[i][0];
    const size_t m = strlen(d);
    if((n == m && !strcmp(host, d)) || (n > m && host[n - m - 1] == '.' && !strcmp(host + n - m, d))) {
      return notify_sites[i][1];
    }
  }
  return 0;
}

// Control characters to spaces, and at most max bytes (not inside a UTF-8
// character): the toast's JSON has room for title and text escaped.
static void
notify_clean(char* s, size_t max) {
  for(char* c = s; *c; c++) {
    if((unsigned char)*c < 0x20) {
      *c = ' ';
    }
  }
  size_t n = strlen(s);
  if(n > max) {
    n = max;
    while(n > 0 && ((unsigned char)s[n] & 0xc0) == 0x80) {
      n--;
    }
    s[n] = 0;
  }
}

static void notify_toast(int i, const char* site, const char* title_given, const char* body, const char* icon);

static void
notify_take(WKPageRef p, const char* arg) {
  static char part[3][1200];
  char url[2048], host[256];
  int i = 0;

  while(i < tab_count && WKViewGetPage(tabs[i].view) != p) {
    i++;
  }
  if(i == tab_count) {
    return;
  }
  take_url(WKPageCopyCommittedURL(p), url, sizeof(url));
  const char* site = strncmp(url, "https://", 8) ? 0 : notify_site(url_host(url, host, sizeof(host)));
  if(!site) {
    POC_LOG("notify: a notice from a page not on the list, not taken");
    return;
  }
  for(int k = 0; k < 3; k++) {
    char encoded[3 * sizeof(part[0])];
    const char* end = strchr(arg, '&');
    size_t n = end ? (size_t)(end - arg) : strlen(arg);
    if(n >= sizeof(encoded)) {
      n = sizeof(encoded) - 1;
    }
    memcpy(encoded, arg, n);
    encoded[n] = 0;
    address_decode(encoded, part[k], sizeof(part[k]));
    arg = end ? end + 1 : arg + strlen(arg);
  }
  notify_clean(part[0], 120);
  notify_clean(part[1], 200);
  notify_clean(part[2], 1000);
  notify_toast(i, site, part[0], part[1], part[2]);
}

// The PS5's toast for a site's notice (title, text, picture's address: cleaned), and the strip's dot for the tab
// it came from when that tab is behind (i: the tab, -1: none of them, a worker's).
static void
notify_toast(int i, const char* site, const char* title_given, const char* body, const char* icon) {
  static char text[600];

  if(i >= 0 && i != tab_current && !tabs[i].unread) {
    tabs[i].unread = true;
    bar_tabs();
  }
  const time_t now = time(0);
  if(now - notify_minute >= 60) {
    notify_minute = now;
    notify_in_minute = 0;
  }
  if(now - notify_last < 2 || notify_in_minute >= 8) {
    POC_LOG("notify: %s, tab %d: too soon after the last, no toast", site, i + 1);
    return;
  }
  notify_last = now;
  notify_in_minute++;
  if(body[0]) {
    snprintf(text, sizeof(text), "%s \xc2\xb7 %s", site, body);
  } else {
    snprintf(text, sizeof(text), "%s", site);
  }
  const bool picture = !strncmp(icon, "https://", 8);
  const char* button = lang_en() ? LM("Open") : "Abrir";
  const char* title = title_given[0] ? title_given : site;
  int err = side_card_post_message("psgm:play?id=" TITLE_ID, picture ? icon : "messages", title, text,
                                   button);
  if(err && picture) {  // the picture's address refused: the PS5's own
    err = side_card_post_message("psgm:play?id=" TITLE_ID, "messages", title, text, button);
  }
  POC_LOG("notify: %s, tab %d%s: toast 0x%x (title %zu, text %zu bytes, %s)", site, i + 1,
          i != tab_current ? " behind" : "", (unsigned)err, strlen(title_given), strlen(body),
          picture ? "picture" : "no picture");
}


// BrowserPS5 RN (2026-11-10 21:20): the settings in a tab of their own when
// they open over a web page. In its place, Telegram Web's 318 MB download
// failed ("FetchEvent.respondWith received an error": its service worker
// lost the page that feeds it) and each way back loaded Telegram again.
// Back in that tab closes it and brings back the tab it was opened over.
static int settings_tab = -1;   // the settings tab's id, -1: none
static int settings_from = -1;  // the id of the tab it was opened over

static int
tab_index(int id) {
  for(int i = 0; i < tab_count; i++) {
    if(tabs[i].id == id) {
      return i;
    }
  }
  return -1;
}

static bool
settings_tab_open(const char* url) {
  const int there = tab_index(settings_tab);
  if(there >= 0) {
    if(there != tab_current) {
      settings_from = tabs[tab_current].id;
      tab_switch(there);
    }
    load_url(url);
    return true;
  }
  char now[4096];
  take_url(WKPageCopyActiveURL(page(PAGE)), now, sizeof(now));
  if(!url_is_web(now)) {
    return false;  // over the browser's own pages: in their place, as before
  }
  const int from = tabs[tab_current].id;
  if(!tab_new(url)) {
    return false;
  }
  settings_tab = tabs[tab_current].id;
  settings_from = from;
  POC_LOG("settings: in a tab of their own, over tab %d", from);
  return true;
}

static bool
settings_tab_back(void) {
  const int i = tab_index(settings_tab);
  if(i < 0 || i != tab_current) {
    return false;
  }
  const int from = tab_index(settings_from);
  settings_tab = -1;
  tab_switch(from >= 0 ? from : (i > 0 ? i - 1 : i + 1));
  tab_close(i);
  return true;
}

static int
tab_in_front(void) {
  return tab_current;
}

static int
tab_front_id(void) {
  return tab_current >= 0 && tab_current < tab_count ? tabs[tab_current].id : -1;
}

// The tab that shows url's site (a tab of the last time not shown yet, by
// its address), the one in front first; -1: none.
static int
tab_with_host(const char* url) {
  char host[256];
  char other[4096];
  char other_host[256];

  if(!url_host(url, host, sizeof(host))[0]) {
    return -1;
  }
  for(int k = -1; k < tab_count; k++) {
    const int i = k < 0 ? tab_current : k;
    if(k >= 0 && i == tab_current) {
      continue;
    }
    if(tabs[i].pending) {
      snprintf(other, sizeof(other), "%s", tabs[i].pending);
    } else {
      take_url(WKPageCopyActiveURL(WKViewGetPage(tabs[i].view)), other, sizeof(other));
    }
    if(!strcmp(url_host(other, other_host, sizeof(other_host)), host)) {
      return i;
    }
  }
  return -1;
}

// A favorite of the bar (2026-11-10 22:20): its site's tab as it is, else a
// new tab when the one in front shows another site, else in front.
static void
favorite_open(const char* encoded) {
  static char url[4096];
  char now[4096];

  address_decode(encoded, url, sizeof(url));
  const int there = tab_with_host(url);
  if(there >= 0) {
    if(there != tab_current) {
      tab_switch(there);
    }
    POC_LOG("favorite: its tab, %d of %d", there + 1, tab_count);
  } else {
    take_url(WKPageCopyActiveURL(page(PAGE)), now, sizeof(now));
    if(url_is_web(now) && tab_new(url)) {
      POC_LOG("favorite: in a tab of its own, %d of %d", tab_current + 1, tab_count);
    } else {
      load_url(url);
    }
  }
  leave_bar();
}
#endif


// ----- the tabs of the last time (session.h, docs/20)

// A tab behind at index at that loads url when it first comes in front: a
// view with nothing in it yet, so no page loads (no WebProcess works) for it
// until then.
static bool
tab_waiting(int at, const char* url) {
  pane_t* p = &app.panes[PAGE];

  if(tab_count >= TABS_MAX || at < 0 || at > tab_count) {
    return false;
  }
  char* copy = strdup(url);
  if(!copy) {
    return false;
  }
  WKViewRef view = view_create(p, p->width, p->height, ui_context, data_store,
                               page_preferences, page_content);
  page_client_install(WKViewGetPage(view));
  WKViewSetVisible(view, false);
  WKViewSetActive(view, false);
  WKPageSetMuted(WKViewGetPage(view), kWKMediaAudioMuted);
  memmove(&tabs[at + 1], &tabs[at], sizeof(tab_t) * (tab_count - at));
  tabs[at].view = view;
  tabs[at].id = tab_next_id++;
  tabs[at].shots = 0;
  tabs[at].pending = copy;
  tabs[at].unread = false;
  tabs[at].notice = 0;
  tabs[at].priv = false;
  tab_count++;
  if(tab_current >= at) {  // the one in front moved along (its view is the same)
    tab_current++;
  }
  return true;
}


static char session_written[TABS_MAX * SESSION_URL_MAX + 32];  // the file's text, as last written

// The tabs of now into SESSION_FILE when they changed: the web address each
// one has (the one it waits for, for a tab of the last time not shown yet)
// and which one is in front. Nothing is logged but a failure.
static void
session_note(void) {
  static char urls[TABS_MAX][SESSION_URL_MAX];
  static char text[sizeof(session_written)];
  const char* list[TABS_MAX];

  if(!session_ready) {  // a test run (HOME_URL), or before the last time's were read
    return;
  }
#if defined(SHOW_YT) || defined(SPEED_TEST)
  return;  // 2026-11-12: a test build's tabs are not the user's ("reopen the tabs from last time" offered a test's film)
#endif
  int kept = 0, front = 0;  // the private tabs are not written (RN)
  for(int i = 0; i < tab_count; i++) {
    if(tabs[i].priv) {
      continue;
    }
    if(tabs[i].pending) {
      snprintf(urls[kept], sizeof(urls[kept]), "%s", tabs[i].pending);
    } else {
      take_url(WKPageCopyCommittedURL(WKViewGetPage(tabs[i].view)), urls[kept], sizeof(urls[kept]));
    }
    if(i == tab_current) {
      front = kept;
    }
    list[kept] = urls[kept];
    kept++;
  }
  size_t n = session_text(list, kept, front, text, sizeof(text));
  if(!n || !strcmp(text, session_written)) {
    return;
  }
  if(session_write(SESSION_FILE, text)) {
    POC_PERROR(SESSION_FILE);
    return;
  }
  memcpy(session_written, text, n + 1);
}


// "Limpar o histórico": the file goes, and the offer with it; the tabs of
// now are written again at their next change.
static void
session_forget(void) {
  session_prev_count = 0;
  session_written[0] = 0;
  unlink(SESSION_FILE);
  POC_LOG("tabs: the last time's forgotten");
}


// How many the start page offers to reopen (0: no button).
static int
session_offer(void) {
  if(!session_ready || session_restored || session_prev_count <= 0) {
    return 0;
  }
  int room = TABS_MAX - tab_count + 1;  // the tab in front is one of them
  return session_prev_count < room ? session_prev_count : room;
}


// The tabs of the last time back: the one that was in front into the tab in
// front (at the start the empty one, from the start page that one), the
// others around it in their order, waiting. Once a run.
static bool
tabs_restore(void) {
  int n = session_offer();
  int front = session_prev_current;

  if(n <= 0) {
    return false;
  }
  session_restored = true;
  if(front >= n) {
    front = 0;
  }
  int back = 0;
  for(int k = 0; k < front; k++) {  // before it: each right before the one in front
    back += tab_waiting(tab_current, session_prev[k]);
  }
  for(int k = front + 1; k < n; k++) {  // after it, in order
    back += tab_waiting(tab_current + (k - front), session_prev[k]);
  }
  load_url(session_prev[front]);
  bar_tabs();
  POC_LOG("tabs: %d of the last time back, %d of %d in front", back + 1, tab_current + 1,
          tab_count);
  session_note();
  return true;
}


// At the start, before anything is written: the tabs of the last time.
static void
session_start(void) {
  session_prev_count = session_read(SESSION_FILE, session_prev, SESSION_TABS_MAX,
                                    &session_prev_current);
  session_ready = true;
  POC_LOG("tabs: %d of the last time", session_prev_count);
}


// A page that takes the browser (or the console) down must not open again
// by itself at every start: tabs reopened at a start that did not last
// SESSION_OPEN_MS, without a quit, leave SESSION_OPEN_FILE behind, and the
// next start shows the start page, which offers them instead.
static double session_open_ms;

static bool
session_safe(void) {
  if(access(SESSION_OPEN_FILE, F_OK)) {
    return true;
  }
  unlink(SESSION_OPEN_FILE);
  POC_LOG("tabs: those reopened at the last start did not last: the start page offers them");
  return false;
}


static void
session_opened(void) {
  FILE* f = fopen(SESSION_OPEN_FILE, "w");
  if(f) {
    fclose(f);
  } else {
    POC_PERROR(SESSION_OPEN_FILE);
  }
  session_open_ms = poc_now_ms();
}


static void
session_tick(void) {  // tick()
  if(session_open_ms > 0 && poc_now_ms() - session_open_ms > SESSION_OPEN_MS) {
    session_open_ms = 0;
    unlink(SESSION_OPEN_FILE);
    POC_LOG("tabs: the reopened ones lasted %d s", SESSION_OPEN_MS / 1000);
  }
}


// ----- the switcher

static int
switcher_last(void) {  // the last card: the new one, when there is room
  return tab_count < TABS_MAX ? tab_count : tab_count - 1;
}


static void
switcher_render(void) {
  text_t t = {};
  char url[4096];
  char title[512];
  char host[256];

  text_add(&t, "show({theme: ");
  text_json(&t, theme_now());
  text_printf(&t, ", current: %d, sel: %d, max: %d, tabs: [", tab_current, switcher.sel,
              TABS_MAX);
  for(int i = 0; i < tab_count; i++) {
    WKPageRef p = WKViewGetPage(tabs[i].view);
    if(tabs[i].pending) {  // a tab of the last time not shown yet: its host
      snprintf(url, sizeof(url), "%s", tabs[i].pending);
    } else {
      take_url(WKPageCopyActiveURL(p), url, sizeof(url));
    }
    take_string(WKPageCopyTitle(p), title, sizeof(title));
    bool web = url_is_web(url);
    text_printf(&t, "%s{title: ", i ? ", " : "");
    text_json(&t, title[0] ? title : web ? url_host(url, host, sizeof(host)) : "Início");
    text_add(&t, ", host: ");
#ifdef APP_RN
    text_json(&t, web ? url_host(url, host, sizeof(host)) : "BrowserPS5 RN");
#else
    text_json(&t, web ? url_host(url, host, sizeof(host)) : "BrowserPS5");
#endif
    if(tabs[i].shots) {
      text_printf(&t, ", picture: '%d.bmp?%u'", tabs[i].id, tabs[i].shots);
    }
    text_add(&t, "}");
  }
  text_add(&t, "]})");
  text_run(SWITCHER, &t);
}


// Its show() ran: the view's next frame has the cards (switcher_painted()).
static void
switcher_shown(WKTypeRef, WKErrorRef, void*) {
  pthread_mutex_lock(&app.lock);
  if(app.switcher_open) {
    app.switcher_armed = true;
  }
  pthread_mutex_unlock(&app.lock);
}


static void
switcher_show(void) {
  if(app.switcher_open || app.keys_open) {
    return;
  }
  menu_hide();
  radial_hide(false);
  if(app.dialog_open) {
    return;
  }
  ring_off();
  tab_shoot(tab_current);
  switcher.sel = tab_current;
  switcher.vx = 0;
  input_cursor(&switcher.saved_x, &switcher.saved_y);
  switcher.last_x = DISPLAY_WIDTH / 2.0f;
  input_set_cursor(DISPLAY_WIDTH / 2.0f, DISPLAY_HEIGHT / 2.0f);
  switcher_render();
  run_script(SWITCHER, "0", 0, switcher_shown);  // after show()
  ui_sound(LINK_SOUND_OPEN);
  pthread_mutex_lock(&app.lock);
  app.switcher_open = true;
  app.switcher_armed = app.switcher_fresh = false;
  app.switcher_shown_ms = poc_now_ms();
  app.dirty = true;
  pthread_cond_signal(&app.cond);
  pthread_mutex_unlock(&app.lock);
  POC_LOG("tabs: the switcher, %d tabs", tab_count);
}


static void
switcher_hide(void) {
  if(!app.switcher_open) {
    return;
  }
  pthread_mutex_lock(&app.lock);
  app.switcher_open = app.switcher_armed = app.switcher_fresh = false;
  app.dirty = true;
  pthread_cond_signal(&app.cond);
  pthread_mutex_unlock(&app.lock);
  run_script(SWITCHER, "hide()", 0, ignore_result);
  input_set_cursor(switcher.saved_x, switcher.saved_y);
  app.nav_fresh = true;
}


static void
switcher_toggle(void) {
  if(app.switcher_open) {
    ui_sound(LINK_SOUND_CLOSE);
    switcher_hide();
  } else {
    switcher_show();
  }
}


static void
switcher_step(int d) {
  char script[32];
  int sel = switcher.sel + d;

  sel = sel < 0 ? 0 : sel > switcher_last() ? switcher_last() : sel;
  if(sel == switcher.sel) {
    return;
  }
  switcher.sel = sel;
  snprintf(script, sizeof(script), "select(%d)", sel);
  run_script(SWITCHER, script, 0, ignore_result);
  ui_sound(LINK_SOUND_MOVE);
}


static void
switcher_choose(void) {
  int sel = switcher.sel;

  ui_sound(LINK_SOUND_SELECT);
  switcher_hide();
  if(sel >= tab_count) {
    tab_new(0);
  } else {
    tab_switch(sel);
  }
}


// Triangle: the tab chosen closes, the switcher stays.
static void
switcher_close_tab(void) {
  if(switcher.sel >= tab_count) {
    return;
  }
  ui_sound(LINK_SOUND_CLOSE);
  tab_close(switcher.sel);
  if(switcher.sel > switcher_last()) {
    switcher.sel = switcher_last();
  }
  switcher_render();
}


// While it is open, the controller works it; true if taken.
static bool
switcher_input(const input_event_t* e) {
  if(!app.switcher_open) {
    return false;
  }
  switch(e->kind) {
  case INPUT_MOVE: {  // the stick: where it pulls, a step at a time
    float dx = e->x - switcher.last_x;
    float cx = DISPLAY_WIDTH / 2.0f;
    switcher.last_x = e->x;
    if(fabsf(dx) > 250) {
      break;
    }
    switcher.vx = switcher.vx * 0.55f + dx;
    double now = poc_now_ms();
    if(fabsf(switcher.vx) >= 9 && now - switcher.stepped_ms > 260) {
      switcher_step(switcher.vx > 0 ? 1 : -1);
      switcher.stepped_ms = now;
    }
    if(fabsf(e->x - cx) > 300) {
      input_set_cursor(cx, DISPLAY_HEIGHT / 2.0f);
      switcher.last_x = cx;
    }
    break;
  }
  case INPUT_SCROLL: {
    int dx, dy;
    if(dpad_step(e, &dx, &dy) && dx) {
      switcher_step(dx);
    }
    break;
  }
  case INPUT_BUTTON_DOWN:
    app.swallow_up = true;
    switcher_choose();
    break;
  case INPUT_ENTER:
    switcher_choose();
    break;
  case INPUT_KEYBOARD:
    switcher_close_tab();
    break;
  case INPUT_FORWARD:  // R1: a new tab
    ui_sound(LINK_SOUND_SELECT);
    switcher_hide();
    tab_new(0);
    break;
  case INPUT_BACK:
  case INPUT_TOUCHPAD:
    ui_sound(LINK_SOUND_CLOSE);
    switcher_hide();
    break;
  case INPUT_BUTTON_UP:
  case INPUT_QUIT:
    return false;
  default:  // the triggers: not over it
    break;
  }
  return true;
}


// ---------------------------------------------------------------------------
// The phone (phone.c, phone.html; docs/07, "Telemóvel"). The server's thread
// calls the phone_* functions below; they copy what came and hand it to the
// main thread, which does it as if pressed here. Nothing while paused: the
// pages are gone then.

#define PHONE_KEEP_FREE (64LL * 1024 * 1024)  // a file leaves this much room

static pthread_mutex_t phone_lock = PTHREAD_MUTEX_INITIALIZER;
static char phone_state[2048] = "{}";   // what the TV shows, for the phone
static char phone_last_pin[8];          // the one on the TV now

// The bar says it, over the address, for a moment (chrome.html notify()).
static void
bar_notify(const char* text, const char* icon) {
  text_t t = {};
  text_add(&t, "window.notify && notify(");
  text_json(&t, text);
  text_add(&t, ", ");
  text_json(&t, icon);
  text_add(&t, ")");
  text_run(BAR, &t);
}

// What /api/state answers, kept by the main thread (phone_state_note()).
static void
phone_state_note(void) {
  char url[2048], title[512];
  take_url(WKPageCopyActiveURL(page(PAGE)), url, sizeof(url));
  take_string(WKPageCopyTitle(page(PAGE)), title, sizeof(title));
  text_t t = {};
  text_add(&t, "{\"url\": ");
  // Our own pages are only "the browser's page" to the phone.
  url[strcspn(url, "?#")] = 0;
  text_json(&t, url_is_web(url) ? url : "");
  text_add(&t, ", \"title\": ");
  text_json(&t, url_is_web(url) ? title : "");
  text_add(&t, ", \"theme\": ");
  text_json(&t, theme_now());
  text_printf(&t, ", \"lang\": \"%s\"", lang_code());
  text_printf(&t, ", \"tabs\": %d}", tab_count);
  if(!t.failed && t.s && strlen(t.s) < sizeof(phone_state)) {
    pthread_mutex_lock(&phone_lock);
    strcpy(phone_state, t.s);
    pthread_mutex_unlock(&phone_lock);
  }
  free(t.s);
}

static size_t
phone_state_json(char* json, size_t size, void*) {
  pthread_mutex_lock(&phone_lock);
  size_t n = (size_t)snprintf(json, size, "%s", phone_state);
  pthread_mutex_unlock(&phone_lock);
  return n;
}

// The downloads for the phone (docs/17): the settings page's list, kept by
// the main thread when it changes (downloads_tell()), copied by the
// server's thread.
static char* phone_downloads;  // {"downloads": [...]}, malloc'd

static void
phone_downloads_note(void) {
  if(phone_port() <= 0) {
    return;
  }
  text_t t = {};
  text_add(&t, "{\"downloads\": ");
  downloads_json(&t);
  text_add(&t, "}");
  if(t.failed || !t.s || strlen(t.s) >= PHONE_DOWNLOADS_MAX) {
    free(t.s);
    return;
  }
  pthread_mutex_lock(&phone_lock);
  free(phone_downloads);
  phone_downloads = t.s;
  pthread_mutex_unlock(&phone_lock);
}

static size_t
phone_downloads_json(char* json, size_t size, void*) {
  pthread_mutex_lock(&phone_lock);
  size_t n = phone_downloads ? (size_t)snprintf(json, size, "%s", phone_downloads) : 0;
  pthread_mutex_unlock(&phone_lock);
  return n;
}

typedef struct phone_action {
  char action[12];
  int id;
} phone_action_t;

static void
phone_main_download(void* arg) {
  phone_action_t* a = (phone_action_t*)arg;
  // Retry and open load a page: not while the system is over the app.
  if(!pause_state.away || (strcmp(a->action, "retry") && strcmp(a->action, "open"))) {
    POC_LOG("phone: download %d: %s", a->id, a->action);
    download_do(a->action, a->id);
  }
  free(a);
}

// The server's thread.
static void
phone_download(const char* action, int id, void*) {
  phone_action_t* a = (phone_action_t*)calloc(1, sizeof(phone_action_t));
  if(a) {
    snprintf(a->action, sizeof(a->action), "%s", action);
    a->id = id;
    WKRunLoopCallOnMainThread(phone_main_download, a);
  }
}

#ifdef APP_RN
// 2026-11-16 (docs/32, idea 12, "also from the phone"): the phone's card "O teu jogo" (phone.html, phone.c
// /api/game): the games of the start page's row, and for one of them its notes (game_note_put()) and its pages
// kept (game_pages_json()), typed with the phone's keyboard. The row is copied here by the main thread when it reads
// it (games_refresh()); the server's thread reads the files itself (they are written whole: rename()), and what the
// phone writes is done by the main thread (phone_main_game()), with the game's page on the TV showing it at once.
static game_info_t phone_games[GAMES_MOST];
static int phone_games_count;

static void
phone_games_note(void) {
  pthread_mutex_lock(&phone_lock);
  phone_games_count = games_count < GAMES_MOST ? games_count : GAMES_MOST;
  if(phone_games_count > 0) {
    memcpy(phone_games, games_list, (size_t)phone_games_count * sizeof(phone_games[0]));
  }
  pthread_mutex_unlock(&phone_lock);
}

static void
phone_main_games(void*) {  // the row read again for the next ask (games_refresh(): at most every GAMES_FRESH_MS)
  games_refresh();
}

// The server's thread.
static size_t
phone_game_json(const char* id, char* json, size_t size, void*) {
  game_info_t* row = (game_info_t*)malloc(sizeof(game_info_t) * GAMES_MOST);
  if(!row) {
    return 0;
  }
  pthread_mutex_lock(&phone_lock);
  const int count = phone_games_count;
  if(count > 0) {
    memcpy(row, phone_games, (size_t)count * sizeof(row[0]));
  }
  pthread_mutex_unlock(&phone_lock);
  WKRunLoopCallOnMainThread(phone_main_games, 0);
  int at = 0;  // the one asked for, or the first
  for(int i = 0; i < count; i++) {
    if(!strcmp(row[i].id, id)) {
      at = i;
    }
  }
  text_t t = {};
  text_add(&t, "{\"games\": [");
  for(int i = 0; i < count; i++) {
    text_add(&t, i ? ", {\"id\": " : "{\"id\": ");
    text_json(&t, row[i].id);
    text_add(&t, ", \"name\": ");
    text_json(&t, row[i].name);
    text_printf(&t, ", \"open\": %d}", row[i].open ? 1 : 0);
  }
  text_add(&t, "]");
  if(count > 0) {
    char path[160];
    char* note = (char*)calloc(1, GAME_NOTE_MOST + 1);
    snprintf(path, sizeof(path), GAME_NOTES "/%s.txt", row[at].id);
    FILE* f = note ? fopen(path, "rb") : 0;
    if(f) {
      note[fread(note, 1, GAME_NOTE_MOST, f)] = 0;
      fclose(f);
    }
    text_add(&t, ", \"id\": ");
    text_json(&t, row[at].id);
    text_add(&t, ", \"name\": ");
    text_json(&t, row[at].name);
    text_add(&t, ", \"notes\": ");
    text_json(&t, note ? note : "");
    text_add(&t, ", \"pages\": ");
    game_pages_json(&t, row[at].id);
    free(note);
  }
  text_add(&t, "}");
  size_t n = 0;
  if(!t.failed && t.s && t.n < size) {
    memcpy(json, t.s, t.n + 1);
    n = t.n;
  }
  free(t.s);
  free(row);
  return n;
}

typedef struct phone_game_job {
  char action[8];
  char id[16];
  char body[PHONE_TEXT_MAX + 1];
} phone_game_job_t;

static void
phone_main_game(void* arg) {
  phone_game_job_t* j = (phone_game_job_t*)arg;
  const game_info_t* g = game_of_row(j->id);
  char said[320];
  if(!g) {
    POC_LOG("phone: %s for a game that is not in the row: nothing done", j->action);
  } else if(!strcmp(j->action, "note")) {
    game_note_put(j->id, j->body);
    if(!pause_state.away) {
      snprintf(said, sizeof(said), lang_en() ? LM("%s: notes written on the phone") : "%s: notas escritas no telemóvel", g->name);
      bar_notify(said, "phone");
    }
  } else if(!strcmp(j->action, "keep") && !j->body[0]) {  // the page on the TV
    if(!pause_state.away) {
      game_pin_shown(g);
    }
  } else if(!strcmp(j->action, "keep")) {  // a link from the phone: "address<TAB>title"
    char* tab = strchr(j->body, '\t');
    if(tab) {
      *tab = 0;
    }
    bool ok = (!strncmp(j->body, "http://", 7) || !strncmp(j->body, "https://", 8)) && strlen(j->body) < 2048;
    for(const char* c = j->body; ok && *c; c++) {
      ok = (unsigned char)*c > ' ';
    }
    if(ok && game_pages_write(j->id, j->body, tab ? tab + 1 : "", 0) && !pause_state.away) {
      snprintf(said, sizeof(said), lang_en() ? LM("Kept with %s: it is on the game's page") : "Guardada com %s: está na página do jogo", g->name);
      bar_notify(said, "phone");
    }
    POC_LOG("phone: a page for %s: %s", j->id, ok ? "kept" : "not a web address: refused");
  } else if(!strcmp(j->action, "unpin")) {
    game_pages_write(j->id, 0, 0, j->body);
  }
  if(g && !pause_state.away && own_page_shown(START_URL)) {
    game_hub(j->id);  // the game's page on the TV, when it is open, shows it now
  }
  free(j);
}

// The server's thread.
static void
phone_game_do(const char* action, const char* id, const char* body, void*) {
  phone_game_job_t* j = (phone_game_job_t*)calloc(1, sizeof(phone_game_job_t));
  if(j) {
    snprintf(j->action, sizeof(j->action), "%s", action);
    snprintf(j->id, sizeof(j->id), "%s", id);
    snprintf(j->body, sizeof(j->body), "%s", body);
    WKRunLoopCallOnMainThread(phone_main_game, j);
  }
}

// 2026-11-16: the phone's card "Discord" (phone.c /api/discord): whether a webhook is kept (only its start goes to
// the phone, never its token), and one given (checked here, share_hook_ok()) or "" to forget it. The server's
// thread: the file is small and written whole (.new, then rename); the notice on the main thread.
static size_t
phone_discord_json(char* json, size_t size, void*) {
  char hook[SHARE_HOOK_MAX], where[96] = "";
  const bool set = discord_hook_read(hook, sizeof(hook));
  if(set) {
    share_hook_short(hook, where, sizeof(where));
  }
  memset(hook, 0, sizeof(hook));
  const int n = snprintf(json, size, "{\"set\": %d, \"where\": \"%s\"}", set ? 1 : 0, where);
  return n > 0 && (size_t)n < size ? (size_t)n : 0;
}

static void
phone_discord_said(void* arg) {
  char* text = (char*)arg;
  bar_notify(text, "phone");
  free(text);
}

static int
phone_discord_set(const char* url, void*) {
  char hook[SHARE_HOOK_MAX];
  const char* from = url;
  while(*from == ' ' || *from == '\t') {
    from++;
  }
  snprintf(hook, sizeof(hook), "%s", from);
  for(size_t n = strlen(hook); n && (hook[n - 1] == ' ' || hook[n - 1] == '\t' || hook[n - 1] == '\r' || hook[n - 1] == '\n'); n--) {
    hook[n - 1] = 0;
  }
  if(hook[0] && !share_hook_ok(hook, SHARE_LOCAL)) {
    POC_LOG("share: the phone gave something that is not a Discord webhook's address: not kept");
    return 0;
  }
  discord_hook_write(hook);
  const bool kept = hook[0] != 0;
  memset(hook, 0, sizeof(hook));
  WKRunLoopCallOnMainThread(phone_discord_said, strdup(kept ? (lang_en() ? LM("The Discord webhook was kept") : "O webhook do Discord ficou guardado")
                                                            : (lang_en() ? LM("The Discord webhook was forgotten") : "O webhook do Discord foi esquecido")));
  return 1;
}
#endif

// The settings' section, when it shows: setPhone({...}).
static void
phone_tell(bool paired) {
  if(!own_page_shown(SETTINGS_URL)) {
    return;
  }
  char ip[64] = "";
  int port = phone_port();
  text_t t = {};
  text_printf(&t, "window.setPhone && setPhone({on: %d, url: ", settings.phone ? 1 : 0);
  if(port > 0 && !phone_local_address(ip, sizeof(ip))) {
    char url[96];
    snprintf(url, sizeof(url), "http://%s:%d/", ip, port);
    text_json(&t, url);
  } else {
    text_add(&t, "\"\"");
  }
  text_add(&t, ", pin: ");
  text_json(&t, phone_pin_seconds() ? phone_last_pin : "");
  text_printf(&t, ", seconds: %d, count: %d, paired: %d})", phone_pin_seconds(), phone_count(),
              paired ? 1 : 0);
  text_run(PAGE, &t);
}

static void
phone_main_open(void* arg) {
  char* text = (char*)arg;
  static char url[8192];
  if(!pause_state.away && address_to_url(text, url, sizeof(url)) != ADDRESS_NONE) {
    char shown[512];
    POC_LOG("phone: open %s", url_for_log(url, shown, sizeof(shown)));
    if(!tab_new(url)) {  // no room for one more: here
      load_url(url);
    }
    bar_notify("Aberto a partir do telemóvel", "phone");
  }
  free(text);
}

typedef struct phone_typed {
  int enter;
  char text[];
} phone_typed_t;

static void
phone_main_type(void* arg) {
  phone_typed_t* typed = (phone_typed_t*)arg;
  if(!pause_state.away) {
    if(app.keys_open) {  // the field gets it straight
      keys_cancel();
    }
    int pane = app.focus == BAR ? BAR : PAGE;
    unsigned count = type_text(pane, typed->text);
    if(typed->enter) {
      send_key(pane, KEY_RETURN, 0, 0);
    }
    // Only how much: it may be a password.
    POC_LOG("phone: typed %u character(s)%s", count, typed->enter ? " and enter" : "");
  }
  free(typed);
}

static void
phone_main_key(void* arg) {
  char* name = (char*)arg;
  int pane = app.focus == BAR ? BAR : PAGE;
#define K(n) !strcmp(name, n)
  if(pause_state.away) {
  } else if(K("back")) {
    go_back();
  } else if(K("forward")) {
    WKPageGoForward(page(PAGE));
  } else if(K("reload")) {
    WKPageReload(page(PAGE));
  } else if(K("home")) {
    home_open();
  } else if(K("up") || K("down")) {
    run_script(PAGE, K("up") ? "window.scrollBy({top: -innerHeight * .8, behavior: 'smooth'})"
                             : "window.scrollBy({top: innerHeight * .8, behavior: 'smooth'})",
               0, ignore_result);
  } else if(K("top") || K("bottom")) {
    run_script(PAGE, K("top") ? "window.scrollTo({top: 0, behavior: 'smooth'})"
                              : "window.scrollTo({top: document.documentElement.scrollHeight,"
                                " behavior: 'smooth'})",
               0, ignore_result);
  } else if(K("enter")) {
    send_key(pane, KEY_RETURN, 0, 0);
  } else if(K("tab")) {
    send_key(pane, 0xff09, "\t", 1);
  } else if(K("escape")) {
    send_key(pane, 0xff1b, 0, 0);
  } else if(K("backspace")) {
    send_key(pane, KEY_BACKSPACE, 0, 0);
  } else if(K("tabs")) {
    switcher_toggle();
  } else if(K("newtab")) {
    tab_new(0);
  } else if(K("fullscreen")) {
    fullscreen_toggle();
#ifdef REMOTE_UI
  } else if(K("sidecard")) {  // the page next to a game (side_card_open())
    side_card_open();
#endif
  }
#undef K
  free(name);
}

typedef struct phone_file {
  bool ok;
  long long size;
  char name[256];
} phone_file_t;

static void
phone_main_file(void* arg) {
  phone_file_t* f = (phone_file_t*)arg;
  char text[400];
  if(f->ok) {
    snprintf(text, sizeof(text), "Chegou do telemóvel: %s", f->name);
  } else {
    snprintf(text, sizeof(text), "Um ficheiro do telemóvel não chegou inteiro");
  }
  POC_LOG("phone: a file of %lld bytes %s", f->size, f->ok ? "arrived" : "was given up");
  bar_notify(text, f->ok ? "arrow-download" : "dismiss");
  if(f->ok && own_page_shown(SETTINGS_URL)) {
    run_script(PAGE, "window.toast && toast('Ficheiro recebido do telemóvel')", 0, ignore_result);
  }
  free(f);
}

static void
phone_main_paired(void* arg) {
  int count = (int)(intptr_t)arg;
  POC_LOG("phone: %d phone(s) now", count);
  // The PIN went with the phone that took it: a new one while the section
  // is open, for the next.
  if(own_page_shown(SETTINGS_URL) && settings.phone) {
    phone_pin_open(phone_last_pin);
  }
  phone_tell(true);
}

// The server's thread.
static void
phone_open(const char* text, void*) {
  WKRunLoopCallOnMainThread(phone_main_open, strdup(text));
}

static void
phone_type(const char* text, int enter, void*) {
  size_t n = strlen(text);
  phone_typed_t* typed = (phone_typed_t*)malloc(sizeof(phone_typed_t) + n + 1);
  if(typed) {
    typed->enter = enter;
    memcpy(typed->text, text, n + 1);
    WKRunLoopCallOnMainThread(phone_main_type, typed);
  }
}

static void
phone_key(const char* name, void*) {
  WKRunLoopCallOnMainThread(phone_main_key, strdup(name));
}

// Where a file goes: as a download of its kind would, or where the phone
// said; a .user.js is only a file here (extensions are installed only from
// the browser). Never over another file.
static int
phone_file_path(const char* name, const char* where, long long size, char* path,
                size_t path_size, void*) {
  char clean[256], dir[600];
  struct stat st;

  clean_name(name, clean, sizeof(clean));
  if(!clean[0]) {
    return PHONE_NO_PLACE;
  }
  if(!strcmp(where, "usb")) {
    dir[0] = 0;
    for(int i = 0; i < 8 && !dir[0]; i++) {
      snprintf(dir, sizeof(dir), "/mnt/usb%d", i);
      if(!mounted(dir)) {
        dir[0] = 0;
      }
    }
    if(!dir[0]) {
      return PHONE_NO_USB;
    }
  } else {
    const char* kind = !strcmp(where, "media") ? "media"
                     : !strcmp(where, "downloads") ? "file" : download_kind(clean, "");
    if(!strcmp(kind, "ext")) {
      kind = "file";
    }
    snprintf(dir, sizeof(dir), "%s", download_dir(kind));
  }
  if(!path_allowed(dir) || mkdirs(dir)) {
    return PHONE_NO_PLACE;
  }
  long long room = free_space(dir);
  if(room >= 0 && room < size + PHONE_KEEP_FREE) {
    return PHONE_NO_ROOM;
  }
  unique_path(dir, clean, path, path_size);
  char part[1100];
  snprintf(part, sizeof(part), "%s.part", path);
  if(!stat(part, &st)) {  // one left by a phone that went away
    unlink(part);
  }
  return 0;
}

static void
phone_file_done(const char* path, long long size, void*) {
  phone_file_t* f = (phone_file_t*)calloc(1, sizeof(phone_file_t));
  if(!f) {
    return;
  }
  f->ok = path != 0;
  f->size = size;
  if(path) {
    const char* base = strrchr(path, '/');
    snprintf(f->name, sizeof(f->name), "%s", base ? base + 1 : path);
  }
  WKRunLoopCallOnMainThread(phone_main_file, f);
}

static void
phone_paired(int count, void*) {
  WKRunLoopCallOnMainThread(phone_main_paired, (void*)(intptr_t)count);
}

// The folders a phone may see (docs/18-ficheiros-no-telemovel.md): the
// downloads' and the media folders, and the USB and external drives there
// now. The browser's own data (saved logins, the TorBox key, the history,
// downloads on their way) is private: never listed, never sent. Not the
// packages' folder. Called on the server's thread, like phone_file_path().
static int
phone_places(phone_place_t* places, int max, void*) {
  int n = 0;
  auto add = [&](const char* kind, const char* path) {
    if(n < max && path[0]) {
      snprintf(places[n].kind, sizeof(places[n].kind), "%s", kind);
      snprintf(places[n].path, sizeof(places[n].path), "%s", path);
      n++;
    }
  };
  add("private", DATA_DIR);
  if(path_allowed(settings.downloads_dir)) {
    add("downloads", settings.downloads_dir);
  }
  if(path_allowed(settings.media_dir) && strcmp(settings.media_dir, settings.downloads_dir)) {
    add("media", settings.media_dir);
  }
  char path[32];
  for(int i = 0; i < 8; i++) {
    snprintf(path, sizeof(path), "/mnt/usb%d", i);
    if(mounted(path)) {
      add("usb", path);
    }
  }
  for(int i = 0; i < 2; i++) {
    snprintf(path, sizeof(path), "/mnt/ext%d", i);
    if(mounted(path)) {
      add("ext", path);
    }
  }
  return n;
}

// A video, sound or picture from the phone's list opens on the TV, in a tab
// of its own (as an address from the phone does). The log says only that.
static void
phone_main_show(void* arg) {
  char* path = (char*)arg;
  struct stat st;
  static char url[3 * FILES_PATH_MAX + 8];
  if(!pause_state.away && files_media(path) && !stat(path, &st) && S_ISREG(st.st_mode)) {
    file_url(path, url, sizeof(url));
    POC_LOG("phone: a file opened on the TV");
    last_gesture_ms = poc_now_ms();
    if(!tab_new(url)) {
      load_url(url);
    }
    bar_notify("Aberto a partir do telemóvel", "phone");
  }
  free(path);
}

static void
phone_show(const char* path, void*) {
  char* copy = strdup(path);
  if(copy) {
    WKRunLoopCallOnMainThread(phone_main_show, copy);
  }
}

// A file going to a phone: below the others (760), as "Extrair" is; it
// rests a little after each MiB (phone.c).
static void
phone_sending(void*) {
  struct sched_param param;
  int policy;
  if(!pthread_getschedparam(pthread_self(), &policy, &param)) {
    param.sched_priority = 760;  // FIFO: a bigger number is a lower priority
    pthread_setschedparam(pthread_self(), policy, &param);
  }
}

// On, if the settings let it; the port, or -1.
static int
phone_ensure(void) {
  static const phone_calls_t calls = {
    phone_open, phone_type, phone_key, phone_file_path, phone_file_done, phone_paired,
    phone_state_json, phone_downloads_json, phone_download, phone_places, phone_show,
    phone_sending, 0,
#ifdef APP_RN
    phone_game_json, phone_game_do, phone_discord_json, phone_discord_set,
#endif
  };
  if(!settings.phone) {
    return -1;
  }
  if(phone_port() > 0) {
    return phone_port();
  }
  phone_state_note();
  int port = phone_start(&calls, phone_html);
  phone_downloads_note();
  POC_LOG("phone: the server %s %d", port > 0 ? "listens on port" : "did not start", port);
  return port;
}

// phone?open (the section shows: a PIN), close (it hides), forget.
static void
phone_command(const char* argument) {
  if(!strcmp(argument, "open")) {
    if(phone_ensure() > 0) {
      phone_pin_open(phone_last_pin);
    }
    phone_tell(false);
  } else if(!strcmp(argument, "close")) {
    phone_pin_close();
  } else if(!strcmp(argument, "forget")) {
    phone_forget();
    POC_LOG("phone: all forgotten");
    phone_tell(false);
  }
}

// The switch in the settings.
static void
phone_setting(void) {
  if(settings.phone) {
    if(phone_ensure() > 0) {
      phone_pin_open(phone_last_pin);
    }
  } else {
    phone_pin_close();
    phone_forget();
    phone_stop();
    POC_LOG("phone: the server stopped");
  }
  phone_tell(false);
}

// A page came in: the PIN only lives while the settings show it.
static void
phone_page_changed(void) {
  if(phone_pin_seconds() && !own_page_shown(SETTINGS_URL)) {
    phone_pin_close();
  }
  if(phone_port() > 0) {
    phone_state_note();
  }
}


// ---------------------------------------------------------------------------
// The developer console (ext/devtools.js; docs/07): R3 shows or hides it,
// in the page.

static void
devtools_shown(WKTypeRef result, WKErrorRef, void*) {
  if(result && WKGetTypeID(result) == WKBooleanGetTypeID()) {
    POC_LOG("developer console %s", WKBooleanGetValue((WKBooleanRef)result) ? "open" : "closed");
  } else {  // the page came before the setting
    bar_notify("Recarrega a página para a consola a ver", "code");
  }
}

static void
devtools_toggle(void) {
  if(!settings.devtools) {
    bar_notify("Liga a consola em Definições → Conteúdo dos sites", "code");
    return;
  }
  if(own_page_shown(0)) {
    bar_notify("A consola é para as páginas da web", "code");
    return;
  }
  run_script(PAGE, "window.__bps5console ? __bps5console.toggle() : null", 0, devtools_shown);
}


// ---------------------------------------------------------------------------
// Modo jogo (ext/gamepad.js; docs/07): a page that uses the Gamepad API gets
// the controller offered, and a click on the touch pad gives it to the game
// (the title sends it as it is, INPUT_PAD); another takes it back. A new
// page, another tab or the setting off take it back too.

#ifdef APP_RN
// 2026-11-15 (docs/32, "a button map per site"). The controller as a KEYBOARD, for the
// games of the web made for keys: on a site where the user turned it on (the menu's "Comando como teclado":
// arrow keys, then WASD, then off; kept by host in PADKEYS_FILE), the touch pad gives the controller to the
// page as it does for a page that uses the Gamepad API (game_set()), and each button is a key that goes down
// when it is pressed and up when it is let go (real key events, as a keyboard's): the D-pad and the left stick
// are the arrows (or W A S D), cross is the space bar, and so on (pad_keys[]).
static void game_set(bool on);
#define PADKEYS_FILE DATA_DIR "/padkeys.txt"
#define PADKEYS_MOST 64
static struct {
  char host[128];
  int preset;  // 1: arrows, 2: WASD
} padkeys[PADKEYS_MOST];
static int padkeys_count = -1;  // -1: the file was not read yet
static int padkeys_here;        // the preset of the site shown when the game mode began (0: none)
static uint32_t padkeys_down;   // the keys held now, a bit by row of pad_keys[]

static const struct {
  uint32_t bit;        // INPUT_PAD_*
  int32_t code[2];     // the key of each preset (an X11 key code, as send_key() takes)
  const char* text[2];
} pad_keys[] = {
  {INPUT_PAD_UP, {0xff52, 'w'}, {"", "w"}},      {INPUT_PAD_DOWN, {0xff54, 's'}, {"", "s"}},
  {INPUT_PAD_LEFT, {0xff51, 'a'}, {"", "a"}},    {INPUT_PAD_RIGHT, {0xff53, 'd'}, {"", "d"}},
  {INPUT_PAD_CROSS, {0x20, 0x20}, {" ", " "}},   {INPUT_PAD_SQUARE, {'z', 'j'}, {"z", "j"}},
  {INPUT_PAD_TRIANGLE, {'x', 'k'}, {"x", "k"}},  {INPUT_PAD_CIRCLE, {'c', 'l'}, {"c", "l"}},
  {INPUT_PAD_L1, {'q', 'q'}, {"q", "q"}},        {INPUT_PAD_R1, {'e', 'e'}, {"e", "e"}},
  // (2026-11-17: Enter was R2, which is now the mouse's button, aim_pad(); it is the right stick's press)
  {INPUT_PAD_L2, {0xffe1, 0xffe1}, {"", ""}},    {INPUT_PAD_R3, {0xff0d, 0xff0d}, {"", ""}},  // Shift, Enter
  {INPUT_PAD_OPTIONS, {0xff1b, 0xff1b}, {"", ""}},  // Escape
};

static void
padkeys_load(void) {
  if(padkeys_count >= 0) {
    return;
  }
  padkeys_count = 0;
  FILE* f = fopen(PADKEYS_FILE, "r");
  char line[200];
  while(f && padkeys_count < PADKEYS_MOST && fgets(line, sizeof(line), f)) {
    char host[128];
    int preset = 0;
    if(sscanf(line, "%127s %d", host, &preset) == 2 && (preset == 1 || preset == 2)) {
      snprintf(padkeys[padkeys_count].host, sizeof(padkeys[0].host), "%s", host);
      padkeys[padkeys_count++].preset = preset;
    }
  }
  if(f) {
    fclose(f);
  }
}

static int
padkeys_of(const char* host) {
  padkeys_load();
  for(int i = 0; i < padkeys_count; i++) {
    if(!strcmp(padkeys[i].host, host)) {
      return padkeys[i].preset;
    }
  }
  return 0;
}

// The preset of a host: 0 takes it off the list. Written at once (a few lines).
static void
padkeys_put(const char* host, int preset) {
  padkeys_load();
  int at = -1;
  for(int i = 0; i < padkeys_count; i++) {
    at = !strcmp(padkeys[i].host, host) ? i : at;
  }
  if(preset && at < 0 && padkeys_count < PADKEYS_MOST) {
    at = padkeys_count++;
    snprintf(padkeys[at].host, sizeof(padkeys[0].host), "%s", host);
  }
  if(at >= 0 && preset) {
    padkeys[at].preset = preset;
  } else if(at >= 0) {
    padkeys[at] = padkeys[--padkeys_count];
  }
  FILE* f = fopen(PADKEYS_FILE ".new", "w");
  if(f) {
    for(int i = 0; i < padkeys_count; i++) {
      fprintf(f, "%s %d\n", padkeys[i].host, padkeys[i].preset);
    }
    fclose(f);
    rename(PADKEYS_FILE ".new", PADKEYS_FILE);
  }
}

// The host of the page shown, "" when it is not a web page.
static void
padkeys_host(char* host, size_t size) {
  char url[2048];
  take_url(WKPageCopyCommittedURL(page(PAGE)), url, sizeof(url));
  host[0] = 0;
  if(url_is_web(url)) {
    url_host(url, host, size);
  }
}

static void
pad_key(int row, bool down) {
  const int p = padkeys_here == 2 ? 1 : 0;
  const char* text = pad_keys[row].text[p];
  WKPageHandleKeyboardEvent(page(PAGE), WKKeyboardEventMake(down ? kWKEventKeyDown : kWKEventKeyUp, kWKInputTypeNormal, text,
                                                           (uint32_t)strlen(text), "", pad_keys[row].code[p], 0, 0, 0));
}

// The controller as the title sends it in the game mode: the keys that changed go down or up.
static void
padkeys_input(uint32_t bits, float x, float y) {
  if(y < -0.5f) {
    bits |= INPUT_PAD_UP;
  } else if(y > 0.5f) {
    bits |= INPUT_PAD_DOWN;
  }
  if(x < -0.5f) {
    bits |= INPUT_PAD_LEFT;
  } else if(x > 0.5f) {
    bits |= INPUT_PAD_RIGHT;
  }
  uint32_t now = 0;
  for(size_t i = 0; i < sizeof(pad_keys) / sizeof(pad_keys[0]); i++) {
    now |= bits & pad_keys[i].bit ? 1u << i : 0;
  }
  const uint32_t changed = now ^ padkeys_down;
  for(size_t i = 0; changed && i < sizeof(pad_keys) / sizeof(pad_keys[0]); i++) {
    if(changed & (1u << i)) {
      pad_key((int)i, now & (1u << i));
    }
  }
  padkeys_down = now;
}

static void
padkeys_release(void) {
  if(padkeys_here) {
    padkeys_input(0, 0, 0);
  }
  padkeys_down = 0;
  padkeys_here = 0;
}

// 2026-11-17: with the controller as a keyboard, the right stick is the mouse and R2 its button, as a game on
// the console has them: the left stick (or the D-pad) walks with the direction keys, the right stick looks or
// aims, R2 fires. (The left stick as the mouse was tried the same day and taken back: it is the hand that
// walks.) The title sends the controller only when it changes, so while the stick is held a thread asks for a
// step of the pointer 60 times a second; it sleeps in between, asks for nothing while the stick rests, and ends
// with the game mode. The pointer, hidden in the game mode, shows once it has been moved.
#define AIM_STEP_US 16000
#define AIM_SPEED   20.0f  // pixels a step at full tilt, with the cursor's speed of the settings at 100
static float aim_x, aim_y;        // the right stick, -1..1, past its dead zone (main thread)
static bool aim_button;           // R2 is down: the mouse's left button
static volatile int aim_moving;   // the stick is off its rest: the thread asks for steps
static volatile int aim_running;  // the thread's life
static pthread_t aim_thread;

static void
aim_step(void*) {  // main thread
  if(!app.game || !padkeys_here || (!aim_x && !aim_y)) {
    return;
  }
  const pane_t* p = &app.panes[PAGE];
  const float speed = AIM_SPEED * (settings.cursor_speed > 0 ? settings.cursor_speed : 100) / 100.0f;
  float x, y;
  input_cursor(&x, &y);
  x += aim_x * speed;
  y += aim_y * speed;
  x = x < p->left ? p->left : x > p->left + p->width - 1 ? p->left + p->width - 1 : x;
  y = y < p->top ? p->top : y > p->top + p->height - 1 ? p->top + p->height - 1 : y;
  input_set_cursor(x, y);
  mouse(PAGE, kWKEventMouseMove, aim_button ? kWKEventMouseButtonLeftButton : kWKEventMouseButtonNoButton, x, y, 0);
  pthread_mutex_lock(&app.lock);
  if(!app.aim_shown) {
    app.aim_shown = true;
  }
  app.dirty = true;
  pthread_cond_signal(&app.cond);
  pthread_mutex_unlock(&app.lock);
}

static void*
aim_loop(void*) {
  while(__atomic_load_n(&aim_running, __ATOMIC_ACQUIRE)) {
    usleep(AIM_STEP_US);
    if(__atomic_load_n(&aim_moving, __ATOMIC_ACQUIRE) && __atomic_load_n(&aim_running, __ATOMIC_ACQUIRE)) {
      WKRunLoopCallOnMainThread(aim_step, 0);
    }
  }
  return 0;
}

// The controller in the game mode of a site with keys: the right stick and R2 (main thread).
static void
aim_pad(uint32_t bits, float rx, float ry) {
  const float dead = 0.18f;
  const float far = sqrtf(rx * rx + ry * ry);
  if(far <= dead) {
    aim_x = aim_y = 0;
  } else {  // from the dead zone's edge, slow near it for a fine aim
    const float f = (far - dead) / (1 - dead), g = (f > 1 ? 1 : f) * (f > 1 ? 1 : f) / far;
    aim_x = rx * g;
    aim_y = ry * g;
  }
  __atomic_store_n(&aim_moving, aim_x || aim_y ? 1 : 0, __ATOMIC_RELEASE);
  const bool down = bits & INPUT_PAD_R2;
  if(down != aim_button) {
    float x, y;
    input_cursor(&x, &y);
    aim_button = down;
    mouse(PAGE, down ? kWKEventMouseDown : kWKEventMouseUp, kWKEventMouseButtonLeftButton, x, y, 1);
  }
}

static void
aim_set(bool on) {
  if(on && !aim_running) {
    aim_x = aim_y = 0;
    aim_button = false;
    __atomic_store_n(&aim_moving, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&aim_running, 1, __ATOMIC_RELEASE);
    if(pthread_create(&aim_thread, 0, aim_loop, 0)) {
      __atomic_store_n(&aim_running, 0, __ATOMIC_RELEASE);
    }
  } else if(!on && aim_running) {
    if(aim_button) {  // the button does not stay down in the page
      aim_pad(0, 0, 0);
    }
    __atomic_store_n(&aim_moving, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&aim_running, 0, __ATOMIC_RELEASE);
    pthread_join(aim_thread, 0);  // (it sleeps one step at most)
    aim_x = aim_y = 0;
  }
  pthread_mutex_lock(&app.lock);
  app.aim_shown = false;
  pthread_mutex_unlock(&app.lock);
}

// The menu's "Comando como teclado": the next of off, arrows, WASD for the site shown.
static void
padkeys_toggle(void) {
  char host[256];
  padkeys_host(host, sizeof(host));
  if(!host[0]) {
    bar_notify(lang_en() ? LM("Only web pages take the controller as a keyboard") : "Só as páginas da web recebem o comando como teclado",
               "dismiss");
    return;
  }
  const int next = (padkeys_of(host) + 1) % 3;
  game_set(false);
  padkeys_put(host, next);
  POC_LOG("controller as keyboard: %s on this site (%d site(s) with it)", next == 1 ? "arrow keys" : next == 2 ? "WASD" : "off", padkeys_count);
  bar_notify(next == 1 ? (lang_en() ? LM("Controller as keyboard here: arrow keys. Touch pad to play")
                                    : "Comando como teclado aqui: setas. Touch pad para jogar")
             : next == 2 ? (lang_en() ? LM("Controller as keyboard here: W A S D. Touch pad to play")
                                      : "Comando como teclado aqui: W A S D. Touch pad para jogar")
                         : (lang_en() ? LM("Controller as keyboard is off on this site") : "Comando como teclado desligado neste site"),
             "games");
}
#endif

static void
game_set(bool on) {
  if(on == app.game) {
    return;
  }
#ifdef APP_RN
  if(on) {  // the site's keys, when the user gave it some
    char host[256];
    padkeys_host(host, sizeof(host));
    padkeys_down = 0;
    padkeys_here = host[0] ? padkeys_of(host) : 0;
    aim_set(padkeys_here != 0);  // and with them the right stick as the mouse
  } else {
    padkeys_release();
    aim_set(false);
  }
#endif
  if(on) {
    ring_off();
    menu_hide();
  }
  pthread_mutex_lock(&app.lock);
  app.game = on;
  app.dirty = true;
  pthread_cond_signal(&app.cond);
  pthread_mutex_unlock(&app.lock);
  input_set_raw(on);
  run_script(PAGE, on ? "window.__bps5pad && __bps5pad.connect(1)"
                      : "window.__bps5pad && __bps5pad.connect(0)", 0, ignore_result);
  feel_apply();
  ui_sound(on ? LINK_SOUND_OPEN : LINK_SOUND_CLOSE);
  bar_notify(on ? "Modo jogo: o comando é do jogo. Touch pad para sair"
                : "O comando voltou ao cursor", "games");
  POC_LOG("game mode %s", on ? "on: the controller goes to the page" : "off");
}

static void
game_offer(void) {
  if(!settings.gamepad || app.game || game_wanted) {
    return;
  }
  game_wanted = true;
  bar_notify("Este site usa o comando: clica no touch pad para jogar", "games");
  POC_LOG("a page uses the Gamepad API: game mode offered");
}

// vibrate?<ms>,<strong>,<weak> (0..1): while a game has the controller, or
// right after a press (a page cannot buzz it on its own).
static void
vibrate_command(const char* argument) {
  int ms = 0;
  float strong = 0, weak = 0;
  if(sscanf(argument, "%d,%f,%f", &ms, &strong, &weak) < 1 || !settings.haptics
     || (!app.game && poc_now_ms() - last_gesture_ms > 5000)) {
    return;
  }
#ifdef REMOTE_UI
  strong = strong < 0 ? 0 : strong > 1 ? 1 : strong;
  weak = weak < 0 ? 0 : weak > 1 ? 1 : weak;
  remote_feel(ms > 0 ? (int)(strong * 255) : 0, ms > 0 ? (int)(weak * 255) : 0, ms > 0 ? ms : 10);
#endif
}

// A game's input, or the touch pad that starts one: taken here.
static bool
game_input(const input_event_t* e) {
  if(e->kind == INPUT_PAD) {
    if(app.game) {
      char script[160];
      snprintf(script, sizeof(script), "window.__bps5pad && __bps5pad(%u, %.3f, %.3f, %.3f, %.3f)",
               (unsigned)e->pressed, e->x, e->y, e->dx, e->dy);
      run_script(PAGE, script, 0, ignore_result);
#ifdef APP_RN
      if(padkeys_here) {  // the left stick and the D-pad walk (the keys), the right stick is the mouse (aim_pad())
        padkeys_input((uint32_t)e->pressed, e->x, e->y);
        aim_pad((uint32_t)e->pressed, e->dx, e->dy);
      }
#endif
    }
    return true;
  }
  if(e->kind == INPUT_TOUCHPAD) {
    if(app.game) {
      game_set(false);
      return true;
    }
#ifdef APP_RN
    bool keys_site = false;
    if(!game_wanted) {  // a site the user gave the controller's keys to (padkeys_toggle())
      char host[256];
      padkeys_host(host, sizeof(host));
      keys_site = host[0] && padkeys_of(host);
    }
    if((game_wanted || keys_site) && settings.gamepad && !app.menu_open && !app.radial_open && !app.switcher_open) {
#else
    if(game_wanted && settings.gamepad && !app.menu_open && !app.radial_open && !app.switcher_open) {
#endif
      game_set(true);
      return true;
    }
    return false;
  }
  return app.game;  // nothing else should come; nothing else goes anywhere
}


// ---------------------------------------------------------------------------
// The settings' "Sistema" panel (system.c; docs/07, "Painel do sistema"):
// the page asks every 2 s while it shows (browserps5:system) and gets
// setSystem({...}). Only read, nothing changed.

static void
disk_json(text_t* t, bool* first, const char* label, const char* path, const char* icon) {
  long long total, free_bytes;
  if(system_disk(path, &total, &free_bytes) || total <= 0) {
    return;
  }
  text_add(t, *first ? "{label: " : ", {label: ");
  *first = false;
  text_json(t, label);
  text_add(t, ", path: ");
  text_json(t, path);
  text_printf(t, ", icon: '%s', total: %lld, free: %lld}", icon, total, free_bytes);
}

static void
system_tell(void) {
  static bool logged;
  if(!own_page_shown(SETTINGS_URL)) {
    return;
  }
  system_info_t info;
  system_read(&info);
  if(!logged) {
    logged = true;
    POC_LOG("system: firmware '%s', cpu %d C, soc %d C, memory %lld of %lld MiB free, up %lld s",
            info.firmware, info.cpu_temp, info.soc_temp,
            info.mem_free < 0 ? -1 : info.mem_free >> 20,
            info.mem_total < 0 ? -1 : info.mem_total >> 20, info.uptime_s);
  }
  char ip[64] = "";
  if(phone_local_address(ip, sizeof(ip))) {
    ip[0] = 0;
  }
  int running = 0;
  for(int i = 0; i < DOWNLOADS_MAX; i++) {
    if(downloads[i].id && (downloads[i].state == DL_RUNNING || downloads[i].state == DL_WAITING ||
                           downloads[i].state == DL_MOVING)) {
      running++;
    }
  }

  text_t t = {};
  text_add(&t, "window.setSystem && setSystem({firmware: ");
  text_json(&t, info.firmware);
  text_add(&t, ", ip: ");
  text_json(&t, ip);
  text_printf(&t, ", up: %lld, cpu: %d, soc: %d, mem_total: %lld, mem_free: %lld, disks: [",
              info.uptime_s, info.cpu_temp, info.soc_temp, info.mem_total, info.mem_free);
  bool first = true;
  char path[32], label[32];
  disk_json(&t, &first, "Armazenamento interno", "/data", "storage");
  for(int i = 0; i < 8; i++) {
    snprintf(path, sizeof(path), "/mnt/usb%d", i);
    if(mounted(path)) {
      snprintf(label, sizeof(label), "USB %d", i + 1);
      disk_json(&t, &first, label, path, "usb-stick");
    }
  }
  for(int i = 0; i < 2; i++) {
    snprintf(path, sizeof(path), "/mnt/ext%d", i);
    if(mounted(path)) {
      snprintf(label, sizeof(label), "Disco externo %d", i + 1);
      disk_json(&t, &first, label, path, "storage");
    }
  }
  text_add(&t, "], browser: {version: ");
  text_json(&t, browser_version());
  text_printf(&t, ", up: %.0f, tabs: %d, ads: %u, downloads: %d}})",
              (poc_now_ms() - app.start_ms) / 1000.0, tab_count, ads_blocked, running);
  text_run(PAGE, &t);
}


// ---------------------------------------------------------------------------
// The radial menus (radial.html, docs/07 "Menus radiais"): L2's goes
// somewhere (start, favorites, history, downloads, settings, the tabs,
// search, add a favorite), R2's works the page (reload, zoom, forward and
// back, the ad blocker, full screen, dark sites). Held, the left stick or
// the D-pad points at a sector and letting go does it; a quick press leaves
// it open, and then cross or square does the one pointed at, circle,
// triangle or the same trigger closes it. The cursor waits in the middle
// (the stick's pull points) and goes back where it was after.

#define RADIAL_SECTORS  8
#define RADIAL_TAP_MS   280.0  // a press shorter than this leaves it open
#define RADIAL_PULL     7.0f   // the stick's pull (pixels, smoothed) that points
#define RADIAL_RECENTER 300.0f // the cursor this far from the middle: back to it

static struct {
  int ring;          // 0 L2's, 1 R2's
  int pick;          // the sector pointed at, -1 none
  bool held;         // its trigger is still down
  double down_ms;
  float vx, vy;      // the stick's pull
  float last_x, last_y;
  float saved_x, saved_y;  // the cursor before it opened
} radial = {0, -1};

enum {  // the sectors, from the top clockwise, as in radial.html
  GO_START, GO_FAVORITES, GO_HISTORY, GO_DOWNLOADS, GO_SETTINGS, GO_TABS,
  GO_SEARCH, GO_ADD_FAVORITE,
};
enum {
  PAGE_RELOAD, PAGE_ZOOM_IN, PAGE_FORWARD, PAGE_ADBLOCK, PAGE_FULLSCREEN,
  PAGE_DARK, PAGE_BACK, PAGE_ZOOM_OUT,
};


// 0 up, then clockwise.
static int
radial_sector(float dx, float dy) {
  float degrees = atan2f(dx, -dy) * 57.2957795f;
  if(degrees < 0) {
    degrees += 360;
  }
  return (int)((degrees + 22.5f) / 45.0f) % RADIAL_SECTORS;
}


static bool
radial_enabled(int ring, int sector) {
  char url[4096];

  if(ring == 0 && sector == GO_ADD_FAVORITE) {
    take_url(WKPageCopyActiveURL(page(PAGE)), url, sizeof(url));
    return url_is_web(url);
  }
  if(ring == 1 && sector == PAGE_FORWARD) {
    return WKPageCanGoForward(page(PAGE));
  }
  if(ring == 1 && sector == PAGE_ADBLOCK) {  // this site's: a web page's
    take_url(WKPageCopyActiveURL(page(PAGE)), url, sizeof(url));
    return url_is_web(url);
  }
  if(ring == 1 && sector == PAGE_BACK) {
    return WKPageCanGoBack(page(PAGE)) || app.fullscreen;
  }
  return true;
}


static void
radial_hide(bool sound) {
  if(!app.radial_open) {
    return;
  }
  if(sound) {
    ui_sound(LINK_SOUND_CLOSE);
  }
  pthread_mutex_lock(&app.lock);
  app.radial_open = app.radial_armed = app.radial_fresh = false;
  app.dirty = true;
  pthread_cond_signal(&app.cond);
  pthread_mutex_unlock(&app.lock);
  run_script(RADIAL, "hide()", 0, ignore_result);
  input_set_cursor(radial.saved_x, radial.saved_y);
  radial.held = false;
  radial.pick = -1;
  app.nav_fresh = true;
  POC_LOG("radial menu closed");
}


// Its show() ran: the view's next frame has the ring (radial_painted()).
static void
radial_shown(WKTypeRef, WKErrorRef, void*) {
  pthread_mutex_lock(&app.lock);
  if(app.radial_open) {
    app.radial_armed = true;
  }
  pthread_mutex_unlock(&app.lock);
}


static void
radial_favorite_known(WKTypeRef result, WKErrorRef, void*) {
  bool favorite = result && WKGetTypeID(result) == WKBooleanGetTypeID()
                  && WKBooleanGetValue((WKBooleanRef)result);
  if(app.radial_open && radial.ring == 0) {
    run_script(RADIAL, favorite ? "setFavorite(true)" : "setFavorite(false)", 0,
               ignore_result);
  }
}


static void
radial_show(int ring) {
  char script[512];
  bool was_open = app.radial_open;

  if(!was_open) {
    menu_hide();
    ring_off();
    input_cursor(&radial.saved_x, &radial.saved_y);
    radial.last_x = DISPLAY_WIDTH / 2.0f;
    radial.last_y = DISPLAY_HEIGHT / 2.0f;
    input_set_cursor(radial.last_x, radial.last_y);
  }
  radial.ring = ring;
  radial.pick = -1;
  radial.vx = radial.vy = 0;
#define B(x) ((x) ? "true" : "false")
  snprintf(script, sizeof(script),
           "show({ring: '%s', theme: '%s', zoom: %d, adblock: %s, dark_sites: %s, "
           "fullscreen: %s, canFavorite: %s, canBack: %s, canForward: %s, canAds: %s})",
           ring ? "page" : "go", theme_now(), settings.zoom,
           B(settings.adblock && !ad_site_allowed(page(PAGE))),
           B(settings.dark_sites), B(app.fullscreen), B(radial_enabled(0, GO_ADD_FAVORITE)),
           B(radial_enabled(1, PAGE_BACK)), B(radial_enabled(1, PAGE_FORWARD)),
           B(radial_enabled(1, PAGE_ADBLOCK)));
#undef B
  run_script(RADIAL, script, 0, was_open ? ignore_result : radial_shown);
  if(ring == 0) {
    run_script(BAR, "favoriteIndex(state.url) >= 0", 0, radial_favorite_known);
  }
  ui_sound(LINK_SOUND_OPEN);
  if(!was_open) {
    pthread_mutex_lock(&app.lock);
    app.radial_open = true;
    app.radial_armed = app.radial_fresh = false;
    app.radial_shown_ms = poc_now_ms();  // until its frame comes (compose())
    app.dirty = true;
    pthread_cond_signal(&app.cond);
    pthread_mutex_unlock(&app.lock);
  }
  POC_LOG("radial menu %s open", ring ? "R2's" : "L2's");
}


static void
radial_pick(int sector) {
  char script[32];

  if(sector == radial.pick) {
    return;
  }
  radial.pick = sector;
  snprintf(script, sizeof(script), "pick(%d)", sector);
  run_script(RADIAL, script, 0, ignore_result);
  if(sector >= 0) {
    ui_sound(LINK_SOUND_MOVE);
  }
}


static void
radial_toggle(const char* key, bool on) {
  char argument[48];
  snprintf(argument, sizeof(argument), "%s=%d", key, on ? 1 : 0);
  setting_command(argument);
}


// The sector pointed at, done (or the menu closed, if none or not now).
static void
radial_do(void) {
  int ring = radial.ring, sector = radial.pick;

  if(sector < 0 || !radial_enabled(ring, sector)) {
    radial_hide(true);
    return;
  }
  ui_sound(LINK_SOUND_SELECT);
  radial_hide(false);
  last_gesture_ms = poc_now_ms();  // full screen takes a press
  POC_LOG("radial menu: %s sector %d", ring ? "R2's" : "L2's", sector);
  if(ring == 0) {
    switch(sector) {
    case GO_START: start_open(); break;
    case GO_FAVORITES: settings_open("favoritos"); break;
    case GO_HISTORY: settings_open("historico"); break;
    case GO_DOWNLOADS: settings_open("transferencias"); break;
    case GO_SETTINGS: settings_open("geral"); break;
    case GO_TABS: switcher_show(); break;
    case GO_SEARCH: keyboard_for_bar(); break;
    case GO_ADD_FAVORITE: run_script(BAR, "star.click()", 0, ignore_result); break;
    }
    return;
  }
  switch(sector) {
  case PAGE_RELOAD: WKPageReload(page(PAGE)); break;
  case PAGE_ZOOM_IN: zoom_step("+"); break;
  case PAGE_FORWARD: WKPageGoForward(page(PAGE)); break;
  case PAGE_ADBLOCK: ad_site_toggle(); break;
  case PAGE_FULLSCREEN: fullscreen_toggle(); break;
  case PAGE_DARK: radial_toggle("dark_sites", !settings.dark_sites); break;
  case PAGE_BACK: go_back(); break;
  case PAGE_ZOOM_OUT: zoom_step("-"); break;
  }
}


// The stick, while it is open: where it pulls, not where the cursor is.
static void
radial_point(const input_event_t* e) {
  float dx = e->x - radial.last_x, dy = e->y - radial.last_y;
  float cx = DISPLAY_WIDTH / 2.0f, cy = DISPLAY_HEIGHT / 2.0f;

  radial.last_x = e->x;
  radial.last_y = e->y;
  if(fabsf(dx) > 250 || fabsf(dy) > 250) {  // the jump back to the middle
    return;
  }
  radial.vx = radial.vx * 0.55f + dx;
  radial.vy = radial.vy * 0.55f + dy;
  if(hypotf(radial.vx, radial.vy) >= RADIAL_PULL) {
    radial_pick(radial_sector(radial.vx, radial.vy));
  }
  if(fabsf(e->x - cx) > RADIAL_RECENTER || fabsf(e->y - cy) > RADIAL_RECENTER) {
    input_set_cursor(cx, cy);
    radial.last_x = cx;
    radial.last_y = cy;
  }
}


// The triggers always; the rest while it is open. True if taken.
static bool
radial_input(const input_event_t* e) {
  if(e->kind == INPUT_RADIAL) {
    int ring = e->dx < 0 ? 0 : 1;
    if(e->pressed) {
      if(app.radial_open && ring == radial.ring) {
        if(!radial.held) {  // left open by a tap: the trigger again closes it
          radial_hide(true);
        }
      } else if(app.radial_open || (!app.dialog_open && !app.keys_open)) {
        radial_show(ring);
        radial.held = true;
        radial.down_ms = poc_now_ms();
      }
    } else if(app.radial_open && ring == radial.ring && radial.held) {
      radial.held = false;
      if(radial.pick >= 0) {
        radial_do();
      } else if(poc_now_ms() - radial.down_ms >= RADIAL_TAP_MS) {
        radial_hide(true);  // let go pointing at nothing
      }
    }
    return true;
  }
  if(!app.radial_open) {
    return false;
  }
  switch(e->kind) {
  case INPUT_MOVE:
    radial_point(e);
    break;
  case INPUT_SCROLL: {
    int dx, dy;
    if(e->pressed == 1 && dpad_step(e, &dx, &dy)) {
      radial_pick(radial_sector((float)dx, (float)dy));
    }
    break;
  }
  case INPUT_BUTTON_DOWN:
  case INPUT_ENTER:
    if(e->kind == INPUT_BUTTON_DOWN) {
      app.swallow_up = true;
    }
    if(radial.pick >= 0) {
      radial_do();
    } else {
      radial_hide(true);
    }
    break;
  case INPUT_BACK:
  case INPUT_KEYBOARD:
    radial_hide(true);
    break;
  case INPUT_BUTTON_UP:  // a press from before it opened ends as usual
  case INPUT_QUIT:
    return false;
  default:
    break;
  }
  return true;
}


// On the main thread: WebKit's API is not thread safe.
static void
dispatch(void* arg) {
  input_event_t* e = (input_event_t*)arg;
  int pane;

  if(e->kind != INPUT_MOVE) {
    moves_flush(true);
  }
#ifdef APP_RN
  if(ask_native_input(e)) {
    free(e);
    return;
  }
#endif
  if(game_input(e)) {
    free(e);
    return;
  }
  if(keys_input(e)) {
    free(e);
    return;
  }
#ifdef APP_RN
  if(find_input(e) || overlay_input(e)) {
    free(e);
    return;
  }
#endif
  if(switcher_input(e)) {
    free(e);
    return;
  }
  if(radial_input(e)) {
    free(e);
    return;
  }
  switch(e->kind) {
  case INPUT_MOVE:
    // The stick: the cursor is free again, the D-pad starts from it.
    ring_off();
    app.nav_fresh = true;
    app.keys_drive = false;  // and shown again, if the D-pad's keys had it hidden
    magnet_arm();
    // A drag gets every move.
    if(MOUSE_MOVE_MS && !e->pressed
       && poc_now_ms() - moves.sent_ms < MOUSE_MOVE_MS) {
      moves.pending = true;
      moves.event = *e;
      break;
    }
    move_to(e);
    break;
  case INPUT_BUTTON_DOWN:
    last_gesture_ms = poc_now_ms();
    pane = pane_at(e->x, e->y);
    if(app.menu_open && pane != MENU && pane != KEYS) {
      menu_hide();  // a press outside it closes it, and does nothing else
      app.swallow_up = true;
      break;
    }
    if(pane < 0) {
      app.swallow_up = true;
      break;
    }
    if(pane == PAGE && !app.menu_open && !app.dialog_open && dpad_keys_enter(e->x, e->y)) {
      set_focus(PAGE);  // the D-pad drives the page (dpad_keys_now()): cross is its Enter
      send_key(PAGE, KEY_RETURN, 0, 0);
      app.swallow_up = true;
      break;
    }
    app.pressed = pane;
    if(pane != KEYS) {  // the field keeps the focus while keys are pressed
      set_focus(pane);
    }
#ifdef APP_RN
    if(pane == PAGE) {  // what has the focus before this press (field_at_js)
      run_script(PAGE, field_mark_js, 0, ignore_result);
    }
#endif
    mouse(pane, kWKEventMouseDown, kWKEventMouseButtonLeftButton, e->x, e->y,
          1);
    break;
  case INPUT_BUTTON_UP:
    if(app.swallow_up) {
      app.swallow_up = false;
      app.pressed = -1;
      break;
    }
    pane = app.pressed >= 0 ? app.pressed : pane_at(e->x, e->y);
    app.pressed = -1;
    if(pane < 0) {
      break;
    }
    mouse(pane, kWKEventMouseUp, kWKEventMouseButtonLeftButton, e->x, e->y,
          1);
    // After the click: a text field?
#ifdef APP_RN
    if(pane == PAGE) {  // only one this press gave the focus to, or was on
      static char script[sizeof(field_at_js) + 48];
      const pane_t* p = &app.panes[PAGE];
      const double z = app.zoom > 0 ? app.zoom : 1;
      snprintf(script, sizeof(script), "%s(%d, %d)", field_at_js, (int)((e->x - p->left) / z), (int)((e->y - p->top) / z));
      run_script(PAGE, script, FIELD_CTX(PAGE), field_found);
      break;
    }
#endif
    if(pane != KEYS) {
      run_script(pane, field_script, FIELD_CTX(pane), field_found);
    }
    break;
  case INPUT_SCROLL:
    scroll(e);
    break;
  case INPUT_BACK:
    go_back();
    break;
  case INPUT_FORWARD:
    if(!app.dialog_open && !app.menu_open) {
      WKPageGoForward(page(PAGE));
    }
    break;
  case INPUT_KEYBOARD:
    if(app.menu_open) {  // triangle closes the menu
      menu_hide();
    } else if(app.dialog_open) {  // the keyboard for the dialog's name
      set_focus(DIALOG);
      run_script(DIALOG, "(function () { var n = document.getElementById('name');"
                         " if (n && n.offsetParent) { n.focus(); n.select(); } })()",
                 0, ignore_result);
      run_script(DIALOG, field_script, FIELD_CTX(DIALOG), field_found);
    } else {  // the address: a page's field takes a click
      keyboard_for_bar();
    }
    break;
  case INPUT_ENTER:
    last_gesture_ms = poc_now_ms();
    send_key(app.focus, KEY_RETURN, 0, 0);
    break;
  case INPUT_TOUCHPAD:
    switcher_toggle();
    break;
  case INPUT_STICK:
    if(e->dx > 0) {
      devtools_toggle();
    }
    break;
  case INPUT_RADIAL:  // radial_input() takes them
  case INPUT_PAD:     // and game_input() these
    break;
  case INPUT_QUIT:  // options: the menu, where "Sair" is (it quit at once until 2026-11-05)
    if(!app.keys_open && !app.switcher_open && !app.radial_open) {
      menu_toggle();
    }
    break;
  }
  free(e);
}


// On the input thread.
static void
on_input(const input_event_t* event) {
#ifdef APP_RN
  last_input_ms = poc_now_ms();  // quit_request_check()
#endif
  input_event_t* copy = (input_event_t*)malloc(sizeof(*copy));

  if(event->kind != INPUT_PAD) {
    pthread_mutex_lock(&app.lock);
    app.input_ms = poc_now_ms();
    if(event->kind == INPUT_MOVE || app.cursor_off) {  // the cursor moved (or comes back): show it
      app.dirty = true;
      pthread_cond_signal(&app.cond);
    }
    pthread_mutex_unlock(&app.lock);
  }
  if(copy) {
    *copy = *event;
    WKRunLoopCallOnMainThread(dispatch, copy);
  }
}


#ifdef APP_RN
// BrowserPS5 RN (2026-11-11 02:00): the interface in a WebProcess of its own
// (the bar, the menu, the dialog, the keyboard, the radial menus, the tabs'
// switcher), when UI_PROCESS_MARKER is there. All the panes and all the tabs
// shared one WebProcess, the title's, and one page thread: while a page
// loaded (YouTube 1.1 s, WhatsApp behind 3 s in the feel test) the bar and the
// keyboard did not answer either. With the marker the pages keep the title's
// WebProcess (its memory, the PS5's decoder, WhatsApp's service worker) and
// the interface's panes are made in a second WebKit context, whose WebProcess
// is a payload one that draws through the title's GPU broker, as v1's pages
// did. The title takes the first WebProcess asked for (process-launcher.cpp),
// so the interface's pages are loaded only once the page's is there
// (ui_split_tick()). The pause ends the payload one, as it always did
// (end_web_processes()), and "back" loads the interface's pages again
// (resume_pages()). Not yet run on the console: off without the marker.
#define UI_PROCESS_MARKER "/data/homebrew/" TITLE_ID "/ui-process"
static bool ui_split;         // set once, in main()
static bool ui_split_loaded;  // the interface's pages were asked for
static void ui_panes_load(void);

static void
ui_split_tick(void) {
  static unsigned ticks;
  if(!ui_split || ui_split_loaded) {
    return;
  }
  const int32_t pid = (int32_t)WKPageGetProcessIdentifier(page(PAGE));
  const bool in_title = pid > 0 && pid == PlayStation::titleWebProcessId();
  if(!in_title && ++ticks < 5 * (1000000 / BAR_UPDATE_US)) {
    return;
  }
  ui_split_loaded = true;
  POC_LOG("the interface's pages load now: the page's WebProcess is %d (%s)", pid,
          in_title ? "the title's" : "NOT the title's after 5 s");
  ui_panes_load();
}

// BrowserPS5 RN (2026-11-11): a quit asked from the PC, for the tests with
// no one at the console (the user away, RN left open, every test stopped at
// "an app is open"). The file QUIT_REQUEST there (install_tar puts it) is
// taken as the menu's "Sair", the app's own clean way out, and only when
// the controller has been still for QUIT_IDLE_S: never under a person.
#define QUIT_REQUEST "/data/homebrew/" TITLE_ID "/quit-request"
#define QUIT_IDLE_S 300

static void
quit_request_check(void) {
  static unsigned ticks;
  if(++ticks % (2 * (1000000 / BAR_UPDATE_US)) || access(QUIT_REQUEST, F_OK) != 0) {
    return;
  }
  unlink(QUIT_REQUEST);
  const double idle_s = (poc_now_ms() - last_input_ms) / 1000;
  if(idle_s < QUIT_IDLE_S) {
    POC_LOG("quit request: not taken, the controller moved %.0f s ago", idle_s);
    return;
  }
  POC_LOG("quit request: taken as the menu's quit (the controller still for %.0f s)", idle_s);
  stop(0);
}
#endif

#if defined(APP_RN) && defined(REMOTE_UI)
// The page's watch (2026-11-11): every two seconds
// the page in front is asked for nothing at all ("1"), and when the answer
// takes more than three seconds the log says so, and again at 6, 12, 24...
// and when it comes: how long each freeze of a real session lasted and on
// which site, with no test build. The interface's pages share the page's
// thread unless they have a WebProcess of their own (ui_split): these lines
// are what tells the two apart afterwards.
static struct {
  bool asked;
  double asked_ms;
  int told_s;
} page_watch;

static void
page_watch_answer(WKTypeRef, WKErrorRef, void*) {
  if(page_watch.told_s) {
    POC_LOG("page watch: the page answers again, after %.1f s", (poc_now_ms() - page_watch.asked_ms) / 1000);
  }
  page_watch.asked = false;
  page_watch.told_s = 0;
}

// 2026-11-14 (the user's films: the title closed on the second and on the fifth film of a session, once
// with the console). The title is a media application: 3 to 3.3 GiB of direct memory for the pages'
// heaps, the title's own heap (GStreamer's and the films' compressed frames) and the video decoder, and
// when it ran out GLib ended the WebProcess. A film left behind (another one opened, its page closed)
// keeps its memory until JavaScript collects its objects, which WebKit does when it sees fit. So the
// title says once a second how much direct memory is left in one piece (link.h), and under
// MEMORY_LOW_MIB the pages' unused objects are collected now: every 20 s while it stays low, every 5 s
// under MEMORY_VERY_LOW_MIB. SHOW_VEIL (a test): always low, to see a film play through the collections.
#ifdef SHOW_VEIL
#define MEMORY_LOW_MIB 100000
#else
#define MEMORY_LOW_MIB 448
#endif
#define MEMORY_VERY_LOW_MIB 192
static void
memory_watch_tick(void) {
  static double last_ms, collected_ms;
  static bool low;
  static unsigned collections;
  const double now = poc_now_ms();
  if(now - last_ms < 2000) {
    return;
  }
  last_ms = now;
  if(pause_state.away) {  // (the title's state is its memory too: not looked at while the system may close it)
    return;
  }
  const int free_mib = remote_title_direct_free();
  if(free_mib < 0) {
    return;
  }
  if(free_mib >= MEMORY_LOW_MIB) {
    if(low) {
      POC_LOG("memory: the title has %d MiB of direct memory in one piece again, after %u collection(s)", free_mib,
              collections);
      low = false;
      collections = 0;
    }
    return;
  }
  if(low && now - collected_ms < (free_mib < MEMORY_VERY_LOW_MIB ? 5000 : 20000)) {
    return;
  }
  collected_ms = now;
  low = true;
  collections++;
  WKContextGarbageCollectJavaScriptObjects(ui_context);
  POC_LOG("memory: the title has %d MiB of direct memory left in one piece (low under %d): the pages' unused "
          "objects are collected (%u)", free_mib, MEMORY_LOW_MIB, collections);
}

static void
page_watch_tick(void) {
  static double last_ms;
  const double now = poc_now_ms();
  if(pause_state.away || !app.panes[PAGE].view) {
    page_watch.asked = false;
    return;
  }
  if(!page_watch.asked) {
    if(now - last_ms >= 2000) {
      last_ms = now;
      page_watch.asked = true;
      page_watch.asked_ms = now;
      page_watch.told_s = 0;
      run_script(PAGE, "1", 0, page_watch_answer);
    }
    return;
  }
  const int waited = (int)((now - page_watch.asked_ms) / 1000);
  if(waited >= 3 && (!page_watch.told_s || waited >= page_watch.told_s * 2)) {
    char url[160];
    take_url(WKPageCopyActiveURL(page(PAGE)), url, sizeof(url));
    page_watch.told_s = waited;
    POC_LOG("page watch: the page has not answered for %d s (%s, %d tabs, interface %s)", waited, url, tab_count,
            ui_split ? "in its own WebProcess" : "on the page's thread");
    // 2026-11-11 15:00: the title can be asked for the stack of the page's
    // WebProcess where it stands still (its web_host looks for the file
    // dump-request twice a second; web-stderr.txt gets it). 15:03: THE
    // CONSOLE WENT DOWN in the first run with it (it asked at 6, 12, 24 and
    // 48 s, and the title then signalled every named thread of the stalled
    // process). So: only with the marker dump-on-stall in the title's folder
    // (a test puts it there), once a stall, and the title dumps the main
    // thread alone.
    static double asked_for_stall;  // the stall it was asked for: once each
    if(waited >= 6 && waited < 12 && asked_for_stall != page_watch.asked_ms
       && access("/data/homebrew/" TITLE_ID "/dump-on-stall", F_OK) == 0) {
      asked_for_stall = page_watch.asked_ms;
      const int fd = open("/data/homebrew/" TITLE_ID "/dump-request", O_WRONLY | O_CREAT | O_TRUNC, 0644);
      if(fd >= 0) {
        close(fd);
      }
    }
  }
  if(waited >= 300) {  // an answer that never came (its page ended): asked again
    page_watch.asked = false;
  }
}
#endif

static void
tick(void*) {
#ifdef APP_RN
  quit_request_check();
  ui_split_tick();
#endif
  bar_update(0);
  moves_flush(false);
#ifdef GPU_COMPOSITING
  layer_ids_update();
#endif
  downloads_tell();
#ifdef APP_RN
  multi_tick();
#endif
  session_tick();
#if defined(APP_RN) && defined(REMOTE_UI)
  page_watch_tick();
  memory_watch_tick();
#endif
#if defined(APP_RN) && defined(ENGINE_PLUS)
  rules_site_tick();
#endif
  static unsigned theme_ticks;
  if(++theme_ticks % (60 * (1000000 / BAR_UPDATE_US)) == 0 && strcmp(theme_now(), theme_shown)
#ifdef REMOTE_UI
     && !pause_state.away  // the panels are put back with the theme then
#endif
     ) {
    POC_LOG("theme: %s for the day", theme_now());
    theme_apply();
  }
  // Where the element the D-pad chose is now (the page moves under it).
  if(app.ring.on) {
    nav_run(app.ring.pane, "refresh", 0, 0, false, false);
  }
#ifdef MEDIA_PROBE
  static unsigned ticks;
  if(ticks++ % (1000000 / BAR_UPDATE_US) == 0) {
    if(media_probe_waiting < 0) {
      media_probe_waiting = 0;
      run_script(PAGE, media_probe_js, 0, media_probe_result);
    } else if(++media_probe_waiting % 10 == 0) {
      POC_LOG("video at %.0f ms: the page has not answered for %d s",
              poc_now_ms() - app.start_ms, media_probe_waiting);
    }
  }
#endif
}


static void*
timer_thread(void*) {
  const unsigned ticks = RUN_SECONDS * (1000000 / BAR_UPDATE_US);  // 0: no limit
  for(unsigned i = 0; !ticks || i < ticks; i++) {
    usleep(BAR_UPDATE_US);
    WKRunLoopCallOnMainThread(tick, 0);
  }
  POC_LOG("the run's %d s are over: quitting", RUN_SECONDS);
  WKRunLoopCallOnMainThread(stop, 0);
  return 0;
}


#ifdef CURSOR_TEST
static bool
quitting(void) {
  return __atomic_load_n(&app.quit, __ATOMIC_RELAXED);
}


// The moves a person makes with the stick, through on_input(), round a
// circle over the page at the pace of the controller's polls (16 ms).
static void*
cursor_test_thread(void*) {
  // A 12-sided circle, as cosines and sines times 1000 (no libm needed).
  static const int round[12][2] = {
    {1000, 0}, {866, 500}, {500, 866}, {0, 1000}, {-500, 866}, {-866, 500},
    {-1000, 0}, {-866, -500}, {-500, -866}, {0, -1000}, {500, -866},
    {866, -500},
  };
  sleep(CURSOR_TEST_START);
  for(int r = 0; r < CURSOR_TEST_ROUNDS && !quitting(); r++) {
    double end = poc_now_ms() + CURSOR_TEST_SECONDS * 1000.0;
    POC_LOG("cursor test: moving from %.0f ms", poc_now_ms() - app.start_ms);
    // By the clock: 16 ms sleeps made "10 s" last 12.6 s (16:35).
    for(unsigned step = 0; poc_now_ms() < end && !quitting(); step++) {
      const int* p = round[step % 12];
      input_event_t e = {INPUT_MOVE, 700 + 250 * p[0] / 1000.0f,
                         450 + 250 * p[1] / 1000.0f, 0, 0, 0};
      on_input(&e);
      usleep(16000);
    }
    POC_LOG("cursor test: still from %.0f ms", poc_now_ms() - app.start_ms);
    sleep(CURSOR_STILL_SECONDS);
  }
  POC_LOG("cursor test: done at %.0f ms", poc_now_ms() - app.start_ms);
  return 0;
}
#endif


#ifdef KEYS_TEST
// make KEYS_TEST=1 (docs/06): once the first page is in, the app works its
// own keyboard and the favorites as a user would, through on_input() where
// the controller has a button for it, and the log says what came of each
// step. The screenshot at the end shows the page of favorites with the
// keyboard open. No one needs to be at the TV. It saves a favorite: remove
// DATA_DIR/favorites.json and favorites.html after.
static void
keys_test_event(input_kind_t kind, float dx, float dy) {
  input_event_t e = {kind, 0, 0, dx, dy, 0};

  input_cursor(&e.x, &e.y);
  on_input(&e);
}


static void
keys_test_shown(WKTypeRef result, WKErrorRef, void* ctx) {
  char text[512] = "(no answer)";

  if(result && WKGetTypeID(result) == WKStringGetTypeID()) {
    WKStringGetUTF8CString((WKStringRef)result, text, sizeof(text));
  }
  POC_LOG("keys test: %s: %s", (const char*)ctx, text);
}


static void
keys_test_step(void* arg) {
  int step = (int)(uintptr_t)arg;
  char url[1024];

  take_url(WKPageCopyCommittedURL(page(PAGE)), url, sizeof(url));
  POC_LOG("keys test: step %d at %.0f ms, the page is at %s, the keyboard is %s",
          step, poc_now_ms() - app.start_ms, url, app.keys_open ? "open" : "closed");
  switch(step) {
  case 0:  // triangle: the keyboard for the address
    keys_test_event(INPUT_KEYBOARD, 0, 0);
    break;
  case 1:  // its keys, then D-pad right (q -> w), square (w), circle (gone);
           // D-pad right (w -> e), cross (e, wherever the cursor is), circle
    POC_RESULT(app.keys_open, "keys test: triangle opened the keyboard");
    run_script(KEYS, "'lisboa'.split('').forEach(press)", 0, ignore_result);
    keys_test_event(INPUT_SCROLL, 120, 0);
    keys_test_event(INPUT_ENTER, 0, 0);
    keys_test_event(INPUT_BACK, 0, 0);
    keys_test_event(INPUT_SCROLL, 120, 0);
    keys_test_event(INPUT_BUTTON_DOWN, 0, 0);
    keys_test_event(INPUT_BUTTON_UP, 0, 0);
    run_script(KEYS, "s.chars.join('') + ' (highlight on ' + s.hl.textContent + ')'",
               (void*)"after cross, the keyboard has", keys_test_shown);
    keys_test_event(INPUT_BACK, 0, 0);
    break;
  case 2:  // R1: the text into the address, and Enter
    POC_RESULT(app.keys_open, "keys test: the keyboard is still open after cross");
    run_script(KEYS, "s.chars.join('') + ' (highlight on ' + s.hl.textContent + ')'",
               (void*)"the keyboard has", keys_test_shown);
    keys_test_event(INPUT_FORWARD, 0, 0);
    break;
  case 3:  // the star
    POC_RESULT(!app.keys_open && strstr(url, "q=lisboa") != 0,
               "keys test: the keyboard closed and its text went to the address");
    run_script(BAR, "star.click(), star.textContent + ' ' + favorites.length",
               (void*)"the star and the number of favorites", keys_test_shown);
    break;
  case 4:  // the house: the page of favorites
    run_script(BAR, "home.click()", 0, ignore_result);
    break;
  case 5:  // what it shows; the keyboard again, for the screenshot
    POC_RESULT(own_url(url, START_URL), "keys test: the start page is open");
    run_script(PAGE, "document.querySelectorAll('.tile').length + ' tile(s): ' + "
               "Array.from(document.querySelectorAll('.name')).map(e => e.textContent).join(', ')",
               (void*)"the start page shows", keys_test_shown);
    keys_test_event(INPUT_KEYBOARD, 0, 0);
    break;
  case 6:
    run_script(KEYS, "'rtp.pt'.split('').forEach(press)", 0, ignore_result);
    break;
  default:
    POC_RESULT(app.keys_open, "keys test: done, the keyboard is open for the screenshot");
    stop(0);
  }
}


static void*
keys_test_thread(void*) {
  static const unsigned delays[] = {3, 3, 3, 8, 3, 6, 3, 3};

  for(uintptr_t step = 0; step < sizeof(delays) / sizeof(*delays); step++) {
    sleep(delays[step]);
    WKRunLoopCallOnMainThread(keys_test_step, (void*)step);
  }
  return 0;
}


static void
keys_test(void) {
  static bool started;
  pthread_t thread;

  if(started) {
    return;
  }
  started = true;
  pthread_create(&thread, 0, keys_test_thread, 0);
  pthread_detach(thread);
}
#endif


#ifdef OPTIONS_TEST
// make OPTIONS_TEST=1 (docs/08-4k.md, 2026-11-05 15:39): once the first page
// is in, options as the controller sends it (the menu opens), the menu's
// "Ecrã inteiro" twice (the start page has no video: the page takes the
// screen, then gives it back), then the menu's "Sair", the way the user
// quits: the title's report then says how its exit went.
static void
options_test_event(input_kind_t kind) {
  input_event_t e = {kind, 0, 0, 0, 0, 0};

  input_cursor(&e.x, &e.y);
  on_input(&e);
}


static void
options_test_step(void* arg) {
  int step = (int)(uintptr_t)arg;

  POC_LOG("options test: step %d at %.0f ms (menu %s, full screen %s)", step,
          poc_now_ms() - app.start_ms, app.menu_open ? "open" : "closed",
          app.fullscreen ? "on" : "off");
  switch(step) {
  case 0:
    options_test_event(INPUT_QUIT);
    break;
  case 1:
    POC_RESULT(app.menu_open && !app.quit, "options test: options opened the menu");
    ui_command("menu-do?fullscreen");
    break;
  case 2:
    POC_RESULT(!app.menu_open && app.fullscreen, "options test: the menu's row gave the page the screen");
    options_test_event(INPUT_QUIT);
    break;
  case 3:
    POC_RESULT(app.menu_open, "options test: options opened the menu over the full screen");
    ui_command("menu-do?fullscreen");
    break;
  case 4:
    POC_RESULT(!app.fullscreen, "options test: the row again: the bar is back");
    options_test_event(INPUT_QUIT);
    break;
  default:
    POC_RESULT(app.menu_open, "options test: done; Sair from the menu");
    ui_command("menu-do?quit");
  }
}


static void*
options_test_thread(void*) {
  for(uintptr_t step = 0; step < 6; step++) {
    sleep(3);
    WKRunLoopCallOnMainThread(options_test_step, (void*)step);
  }
  return 0;
}


static void
options_test(void) {
  static bool started;
  pthread_t thread;

  if(started) {
    return;
  }
  started = true;
  pthread_create(&thread, 0, options_test_thread, 0);
  pthread_detach(thread);
}
#endif


#ifdef UI_TEST
// make UI_TEST=1 (docs/07): once the first page is in, the app goes through
// its own look as a user would, a step every few seconds, with a screenshot
// of each in the log (poc/shot.ps1 -All): the menu, the D-pad's ring in it,
// the settings, another theme, the start page with the ring on its search
// box, the downloads. The theme it changes is put back at the end. No one needs to be
// at the TV.
static void
ui_test_dpad(int dy) {
  input_event_t e = {INPUT_SCROLL, 0, 0, 0, dy * 120.0f, 1};

  input_cursor(&e.x, &e.y);
  on_input(&e);
}


static void
ui_test_step(void* arg) {
  static char theme[32];
  int step = (int)(uintptr_t)arg;

  POC_LOG("ui test: step %d at %.0f ms (menu %s, dialog %s, ring %s)", step,
          poc_now_ms() - app.start_ms, app.menu_open ? "open" : "closed",
          app.dialog_open ? "open" : "closed", app.ring.on ? "on" : "off");
  switch(step) {
  case 0:
    snprintf(theme, sizeof(theme), "%s", settings.theme);
    menu_toggle();
    break;
  case 1:
    POC_RESULT(app.menu_open, "ui test: the menu opened");
    ui_test_dpad(1);
    ui_test_dpad(1);
    break;
  case 2:
    POC_RESULT(app.ring.on && app.ring.pane == MENU, "ui test: the D-pad chose in the menu");
    screenshot("the menu, with the D-pad's ring");
    ui_command("menu-do?settings%3Faspeto");
    break;
  case 3:
    POC_RESULT(!app.menu_open && own_page_shown(SETTINGS_URL), "ui test: the settings are open");
    setting_command("theme%3Drubi");
    break;
  case 4:
    screenshot("the settings (Aspeto), theme Rubi");
    start_open();
    break;
  case 5:
    POC_RESULT(own_page_shown(START_URL), "ui test: the start page is open");
    // Up, to the search box: the cursor is still where the menu's ring
    // left it, below the box, and every start page has the box. Down found
    // nothing on a console with no favorites saved (2026-11-05 11:03).
    ui_test_dpad(-1);
    break;
  case 6:
    POC_RESULT(app.ring.on, "ui test: the D-pad chose on the start page");
    screenshot("the start page, with the D-pad's ring");
    {
      char value[48];
      snprintf(value, sizeof(value), "theme%%3D%s", theme);
      setting_command(value);
    }
    settings_open("transferencias");
    break;
  default:
    POC_RESULT(1, "ui test: done, theme %s again", settings.theme);
    stop(0);
  }
}


static void*
ui_test_thread(void*) {
  static const unsigned delays[] = {4, 3, 3, 4, 3, 4, 3, 4};

  for(uintptr_t step = 0; step < sizeof(delays) / sizeof(*delays); step++) {
    sleep(delays[step]);
    WKRunLoopCallOnMainThread(ui_test_step, (void*)step);
  }
  return 0;
}


static void
ui_test(void) {
  static bool started;
  pthread_t thread;

  if(started) {
    return;
  }
  started = true;
  pthread_create(&thread, 0, ui_test_thread, 0);
  pthread_detach(thread);
}
#endif


#if defined(DEMO) || defined(SHOW)  // SHOW (BrowserPS5 RN): show_tour(), below
// make DEMO=1 (docs/09-video.md): the browser shows itself for a video to
// publish. Once the first page is in, the app goes through its tour as a
// user would: the cursor glides (input_set_cursor and INPUT_MOVE, as the
// stick), the keys, triggers and menus come through on_input() as the
// controller's, and the title records each piece of it to its disk
// (remote_capture(): title/capture.h, CAPTURE_DIR/<piece>). The PC fetches
// and edits the pieces. Its own data (DATA_DIR): none of the user's
// favorites, history or cookies show, and it starts as a first start (the
// welcome). No one may touch the controller while it runs. At the end the
// menu's Sair, the way the user quits.
// DEMO_VIDEO: the video piece's page, a Creative Commons film (Big Buck
// Bunny, (c) Blender Foundation, CC BY 3.0, in 2160p60 on YouTube).
#ifndef DEMO_VIDEO
#define DEMO_VIDEO "https://www.youtube.com/watch?v=aqz-KE-bpKQ&t=95s"
#endif
#define DEMO_PIECE_MAX_S 90  // a piece the demo forgets to end ends by itself

static struct {
  pthread_mutex_t lock;
  pthread_cond_t cond;
  unsigned asked, answered;  // demo_ask()'s
  char answer[1200];
  unsigned jobs, jobs_done;  // demo_main()'s
  unsigned loads;            // pages loaded in front (didFinishNavigation)
} demo_state = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0, 0, "", 0, 0, 0};

// Waits on demo_state.cond until *counter reaches value, at most ms.
static bool
demo_wait_for(const unsigned* counter, unsigned value, int ms) {
  struct timespec until;
  bool reached;

  clock_gettime(CLOCK_REALTIME, &until);
  until.tv_sec += ms / 1000;
  until.tv_nsec += (long)(ms % 1000) * 1000000L;
  if(until.tv_nsec >= 1000000000L) {
    until.tv_sec++;
    until.tv_nsec -= 1000000000L;
  }
  pthread_mutex_lock(&demo_state.lock);
  while(*counter < value
        && pthread_cond_timedwait(&demo_state.cond, &demo_state.lock, &until) == 0) {
  }
  reached = *counter >= value;
  pthread_mutex_unlock(&demo_state.lock);
  return reached;
}


static void
demo_sleep(int ms) {
  usleep((useconds_t)ms * 1000);
}


// Something for the main thread (WebKit's API is not thread safe), done
// before this returns.
typedef struct demo_job {
  void (*fn)(const char*);
  char arg[1024];
} demo_job_t;

static void
demo_job_run(void* p) {
  demo_job_t* job = (demo_job_t*)p;

  job->fn(job->arg);
  free(job);
  pthread_mutex_lock(&demo_state.lock);
  demo_state.jobs_done++;
  pthread_cond_broadcast(&demo_state.cond);
  pthread_mutex_unlock(&demo_state.lock);
}

static void
demo_main(void (*fn)(const char*), const char* arg) {
  demo_job_t* job = (demo_job_t*)calloc(1, sizeof(*job));
  unsigned ticket;

  if(!job) {
    return;
  }
  job->fn = fn;
  snprintf(job->arg, sizeof(job->arg), "%s", arg ? arg : "");
  pthread_mutex_lock(&demo_state.lock);
  ticket = ++demo_state.jobs;
  pthread_mutex_unlock(&demo_state.lock);
  WKRunLoopCallOnMainThread(demo_job_run, job);
  demo_wait_for(&demo_state.jobs_done, ticket, 5000);
}

static void demo_do_ui(const char* command) { ui_command(command); }
#ifdef SHOW_VEIL
// (2026-11-14) How much of the screen the app hands to the title as transparent: the videos' holes.
static unsigned long veil_transparent;
static void demo_do_holes(const char*) {
  uint32_t* shot = (uint32_t*)malloc((size_t)DISPLAY_WIDTH * DISPLAY_HEIGHT * 4);
  veil_transparent = 0;
  if(!shot) {
    return;
  }
  pthread_mutex_lock(&app.lock);
  compose(shot);
  pthread_mutex_unlock(&app.lock);
  for(size_t i = 0; i < (size_t)DISPLAY_WIDTH * DISPLAY_HEIGHT; i++) {
    veil_transparent += (shot[i] >> 24) == 0;
  }
  free(shot);
}
#endif
static void demo_do_set(const char* setting) { setting_command(setting); }
static void demo_do_load(const char* url) { load_url(url); }
static void demo_do_start(const char*) { start_open(); }
static void demo_do_settings(const char* section) { settings_open(section); }
static void demo_do_tab(const char* url) { tab_new(url); }


// A script's answer, as text ("" for none), into out: on the main thread
// it is run, here it is waited for (at most 3 s).
typedef struct demo_ask_job {
  int pane;
  char script[3072];
  unsigned ticket;
} demo_ask_job_t;

static void
demo_answered(WKTypeRef result, WKErrorRef, void* ctx) {
  demo_ask_job_t* job = (demo_ask_job_t*)ctx;

  pthread_mutex_lock(&demo_state.lock);
  demo_state.answer[0] = 0;
  if(result && WKGetTypeID(result) == WKStringGetTypeID()) {
    WKStringGetUTF8CString((WKStringRef)result, demo_state.answer, sizeof(demo_state.answer));
  }
  if(demo_state.answered < job->ticket) {
    demo_state.answered = job->ticket;
  }
  pthread_cond_broadcast(&demo_state.cond);
  pthread_mutex_unlock(&demo_state.lock);
  free(job);
}

static void
demo_ask_run(void* p) {
  demo_ask_job_t* job = (demo_ask_job_t*)p;

  run_script(job->pane, job->script, job, demo_answered);
}

static bool
demo_ask(int pane, const char* script, char* out, size_t size) {
  demo_ask_job_t* job = (demo_ask_job_t*)calloc(1, sizeof(*job));
  bool got;

  out[0] = 0;
  if(!job) {
    return false;
  }
  job->pane = pane;
  snprintf(job->script, sizeof(job->script), "%s", script);
  pthread_mutex_lock(&demo_state.lock);
  job->ticket = ++demo_state.asked;
  pthread_mutex_unlock(&demo_state.lock);
  WKRunLoopCallOnMainThread(demo_ask_run, job);
  got = demo_wait_for(&demo_state.answered, job->ticket, 3000);
  pthread_mutex_lock(&demo_state.lock);
  if(got && demo_state.answered == job->ticket) {
    snprintf(out, size, "%s", demo_state.answer);
  }
  pthread_mutex_unlock(&demo_state.lock);
  return out[0] != 0;
}


// The controller, as the title sends it: at the cursor.
static void
demo_event(input_kind_t kind, float dx, float dy, int pressed) {
  input_event_t e = {kind, 0, 0, dx, dy, pressed};

  input_cursor(&e.x, &e.y);
  on_input(&e);
}

static void
demo_dpad(int dx, int dy) {
  demo_event(INPUT_SCROLL, dx * 120.0f, dy * 120.0f, 1);
}

static void
demo_click(void) {
  demo_event(INPUT_BUTTON_DOWN, 0, 0, 0);
  demo_sleep(90);
  demo_event(INPUT_BUTTON_UP, 0, 0, 0);
}

// The cursor to (x, y) in ms, eased in and out, a step a frame, as the
// stick moves it.
static void
demo_glide(float x, float y, int ms) {
  float fx, fy;
  int steps = ms / 16 > 0 ? ms / 16 : 1;

  input_cursor(&fx, &fy);
  for(int i = 1; i <= steps; i++) {
    float t = (float)i / steps;
    float e = t < 0.5f ? 4 * t * t * t : 1 - (-2 * t + 2) * (-2 * t + 2) * (-2 * t + 2) / 2;
    input_event_t ev = {INPUT_MOVE, fx + (x - fx) * e, fy + (y - fy) * e, 0, 0, 0};

    input_set_cursor(ev.x, ev.y);
    on_input(&ev);
    demo_sleep(16);
  }
}

// Where an element is on the screen (its middle): element is a script for
// it in pane's page (null when none).
static bool
demo_find(int pane, const char* element, float* x, float* y) {
  char script[2048];
  char answer[64];
  float ex, ey;

  snprintf(script, sizeof(script),
           "(function () { var e = %s; if (!e) return '';"
           " e.scrollIntoView({block: 'nearest'}); var r = e.getBoundingClientRect();"
           " return (r.left + r.width / 2) + ',' + (r.top + r.height / 2); })()", element);
  if(!demo_ask(pane, script, answer, sizeof(answer))
     || sscanf(answer, "%f,%f", &ex, &ey) != 2) {
    POC_LOG("demo: not found in the %s: %s", app.panes[pane].name, element);
    return false;
  }
  float zoom = pane == PAGE ? (float)app.zoom : 1.0f;
  *x = app.panes[pane].left + ex * zoom;
  *y = app.panes[pane].top + ey * zoom;
  return true;
}

static bool
demo_glide_to(int pane, const char* element, int ms) {
  float x, y;

  if(!demo_find(pane, element, &x, &y)) {
    return false;
  }
  demo_glide(x, y, ms);
  return true;
}

// Typed on the browser's own keyboard, a key at a time, its key lit.
static void
demo_type(const char* text, int ms_per_key) {
  for(const char* c = text; *c; c++) {
    char script[512];
    char one[2] = {*c, 0};

    if(*c == '\'' || *c == '\\') {  // not in the script's string
      continue;
    }
    snprintf(script, sizeof(script),
             "(function (c) { var k = Array.from(document.querySelectorAll('.key')).find(function (e) {"
             " return e.key === c.toLowerCase() || (c === ' ' && e.key && e.key.k === 'space'); });"
             " if (k) { highlight(k); flash(k); } press(c === ' ' ? {k: 'space'} : c); return 'ok'; })('%s')",
             one);
    char answer[8];
    demo_ask(KEYS, script, answer, sizeof(answer));
    demo_sleep(ms_per_key);
  }
}

#ifdef DEMO  // the pieces: recorded by the title itself (SHOW: the TV is filmed)
// A piece of the video: the title records until demo_piece_end(), which
// waits until the title has written all of it to its disk (the title keeps
// it in RAM meanwhile: the disk is slower than the screen, title/capture.c),
// so the next piece starts with the disk free and nothing moves while it
// writes.
static double demo_piece_ms;
static char demo_piece_name[32];
static unsigned demo_pieces_done;

// Until the title has no piece being recorded or written, at most ms; the
// pieces it has written in *done.
static bool
demo_capture_idle(int ms, unsigned* done) {
  double give_up = poc_now_ms() + ms;
  int busy;

  while((busy = remote_capture_status(done)) == 1 && poc_now_ms() < give_up) {
    demo_sleep(250);
  }
  return busy == 0;
}

static void
demo_piece(const char* name) {
  double asked = poc_now_ms();

  if(!demo_capture_idle(600000, &demo_pieces_done)) {
    POC_LOG("demo: the title is still busy with the piece before %s (or gone)", name);
  } else if(poc_now_ms() - asked > 1000) {
    POC_LOG("demo: waited %.1f s for the title before %s", (poc_now_ms() - asked) / 1e3, name);
  }
  snprintf(demo_piece_name, sizeof(demo_piece_name), "%s", name);
  demo_piece_ms = poc_now_ms();
  POC_LOG("demo: piece %s starts at %.0f ms", name, demo_piece_ms - app.start_ms);
  remote_capture(1, name, DEMO_PIECE_MAX_S);
  demo_sleep(300);  // the first frames in before anything moves
}

static void
demo_piece_end(void) {
  unsigned done = 0;
  double ended;

  demo_sleep(300);
  remote_capture(0, 0, 0);
  ended = poc_now_ms();
  POC_LOG("demo: piece %s ends, %.1f s", demo_piece_name, (ended - demo_piece_ms) / 1e3);
  demo_sleep(500);  // the title takes the end
  bool idle = demo_capture_idle(600000, &done);
  POC_LOG("demo: piece %s %s %.1f s after its end (%u written in all)", demo_piece_name,
          idle && done > demo_pieces_done ? "written" : "NOT CONFIRMED WRITTEN",
          (poc_now_ms() - ended) / 1e3, done);
  demo_pieces_done = done;
}
#endif

static unsigned
demo_loads(void) {
  unsigned n;

  pthread_mutex_lock(&demo_state.lock);
  n = demo_state.loads;
  pthread_mutex_unlock(&demo_state.lock);
  return n;
}

static bool
demo_wait_load(unsigned before, int ms) {
  bool loaded = demo_wait_for(&demo_state.loads, before + 1, ms);

  POC_LOG("demo: %s", loaded ? "the page is in" : "no page in time");
  return loaded;
}

// The R2 or L2 menu (ring 1 or 0): open, point at sector (0 up, then
// clockwise, 45 degrees each) with the stick, let go: what is there is done.
static void
demo_radial(int ring, int sector, int hold_ms) {
  float cx = DISPLAY_WIDTH / 2.0f, cy = DISPLAY_HEIGHT / 2.0f;
  float angle = sector * 0.785398f;

  input_set_cursor(cx, cy);
  demo_event(INPUT_RADIAL, ring ? 1.0f : -1.0f, 0, 1);
  demo_sleep(500);
  for(int i = 1; i <= 6; i++) {
    input_event_t ev = {INPUT_MOVE, cx + sinf(angle) * 14 * i, cy - cosf(angle) * 14 * i, 0, 0, 0};
    on_input(&ev);
    demo_sleep(16);
  }
  demo_sleep(hold_ms);
  demo_event(INPUT_RADIAL, ring ? 1.0f : -1.0f, 0, 0);
}

// The video page's consent (the most private answer), its ad (skipped
// when it can be), the 2160p quality, and its state.
static const char demo_consent_js[] =
  "(function () { var b = Array.from(document.querySelectorAll('button, [role=button]')).find(function (e) {"
  " return /rejeitar tudo|reject all/i.test((e.getAttribute('aria-label') || '') + ' ' + e.textContent); });"
  " if (b) { b.click(); return 'rejected'; } return 'none'; })()";
static const char demo_ad_js[] =
  "(function () { var p = document.getElementById('movie_player'); if (!p) return 'no player';"
  " if (p.classList.contains('ad-showing')) { var s = document.querySelector("
  "'.ytp-skip-ad-button, .ytp-ad-skip-button, .ytp-ad-skip-button-modern');"
  " if (s) { s.click(); return 'ad skipped'; } return 'ad'; } return 'video'; })()";
static const char demo_quality_js[] =
  "(function () { var p = document.getElementById('movie_player');"
  " if (p && p.setPlaybackQualityRange) p.setPlaybackQualityRange('hd2160', 'hd2160');"
  " var v = document.querySelector('video'); if (v && v.paused) v.play();"
  " return p && p.getPlaybackQuality ? p.getPlaybackQuality() : 'no player'; })()";
static const char demo_video_js[] =
  "(function () { var v = document.querySelector('video'); if (!v) return 'no video';"
  " return v.videoWidth + 'x' + v.videoHeight + ' at ' + v.currentTime.toFixed(1) + ' s'"
  " + (v.paused ? ' paused' : '') + (v.muted ? ' muted' : ''); })()";
// "<time> <1 if paused>" of the player's video.
static const char demo_time_js[] =
  "(function () { var v = document.querySelector('#movie_player video') || document.querySelector('video');"
  " return v ? v.currentTime.toFixed(2) + ' ' + (v.paused ? 1 : 0) : ''; })()";
// What a user clicks to start it: the big play button when it shows, else
// the picture itself.
static const char demo_play_element[] =
  "(function () { var b = document.querySelector('#movie_player .ytp-large-play-button');"
  " if (b && b.offsetParent && b.getBoundingClientRect().width > 0) return b;"
  " return document.querySelector('#movie_player video') || document.getElementById('movie_player'); })()";

// The video playing, as a user starts it: a click on the player when it is
// paused (a page's own play() is not a user's gesture, and the first try on
// the console stayed at 0.0 s, paused); then its time must move.
static bool
demo_play_video(void) {
  char answer[64];
  double t0, t1;
  int paused0, paused1;

  for(int attempt = 1; attempt <= 4; attempt++) {
    if(!demo_ask(PAGE, demo_time_js, answer, sizeof(answer))
       || sscanf(answer, "%lf %d", &t0, &paused0) != 2) {
      demo_sleep(1000);
      continue;
    }
    if(paused0 && demo_glide_to(PAGE, demo_play_element, 700)) {
      demo_sleep(250);
      demo_click();
      POC_LOG("demo: the video was paused at %.1f s: clicked it (try %d)", t0, attempt);
    }
    demo_sleep(2000);
    if(demo_ask(PAGE, demo_time_js, answer, sizeof(answer))
       && sscanf(answer, "%lf %d", &t1, &paused1) == 2 && !paused1 && t1 > t0 + 0.5) {
      POC_LOG("demo: the video plays (%.1f s -> %.1f s)", t0, t1);
      return true;
    }
    POC_LOG("demo: the video does not play yet (%s)", answer);
  }
  return false;
}

#ifdef DEMO
static void
demo_tour(void) {
  char answer[256];
  unsigned before;

  // 1. The first start: the welcome, its two styles; Ciborgue (the Néon
  // theme, the controller's sounds, the magnet); the start page.
  demo_sleep(2500);
  input_set_cursor(DISPLAY_WIDTH / 2.0f, DISPLAY_HEIGHT * 0.8f);
  demo_piece("01-boas-vindas");
  demo_sleep(1500);
  demo_glide_to(PAGE, "document.getElementById('cinema')", 1100);
  demo_sleep(1200);
  demo_glide_to(PAGE, "document.getElementById('ciborgue')", 900);
  demo_sleep(1000);
  demo_click();
  demo_sleep(2500);
  demo_glide_to(PAGE, "document.querySelectorAll('.tile')[0]", 1000);
  demo_sleep(500);
  demo_glide_to(PAGE, "document.querySelectorAll('.tile')[2]", 900);
  demo_sleep(500);
  demo_glide_to(PAGE, "document.getElementById('q')", 900);
  demo_sleep(800);
  demo_piece_end();

  // 2. Its own keyboard (triangle): an address typed, R1 goes there.
  demo_piece("02-teclado");
  demo_event(INPUT_KEYBOARD, 0, 0, 0);
  demo_sleep(1200);
  demo_type("pt.wikipedia.org/wiki/PlayStation_5", 140);
  demo_sleep(700);
  before = demo_loads();
  demo_event(INPUT_FORWARD, 0, 0, 0);
  demo_wait_load(before, 15000);
  demo_sleep(1500);
  demo_piece_end();

  // 3. The page: the right stick scrolls, the D-pad's ring goes from link
  // to link, R2's menu zooms in and back out, R3 shows the page's console.
  demo_piece("03-navegar");
  for(int i = 0; i < 90; i++) {  // the right stick, 1.5 s
    demo_event(INPUT_SCROLL, 0, 14, 0);
    demo_sleep(16);
  }
  demo_sleep(600);
  for(int i = 0; i < 5; i++) {
    demo_dpad(0, 1);
    demo_sleep(650);
  }
  demo_dpad(1, 0);
  demo_sleep(800);
  demo_radial(1, 1, 700);  // R2, up and right: larger
  demo_sleep(1500);
  demo_radial(1, 7, 700);  // R2, up and left: smaller again
  demo_sleep(1500);
  demo_event(INPUT_STICK, 1, 0, 0);  // R3: the page's developer console
  demo_sleep(4000);
  demo_event(INPUT_STICK, 1, 0, 0);
  demo_sleep(800);
  demo_piece_end();

  // 4. The tabs: two more behind it first (not filmed), then L2's menu,
  // its Separadores, the carousel, another tab.
  demo_main(demo_do_tab, "https://www.blender.org/");
  demo_sleep(6000);
  demo_main(demo_do_tab, "https://www.openstreetmap.org/#map=12/38.7223/-9.1393");
  demo_sleep(6000);
  demo_piece("04-separadores");
  demo_radial(0, 5, 700);  // L2, down and left: the tabs
  demo_sleep(1500);
  demo_dpad(-1, 0);
  demo_sleep(900);
  demo_dpad(-1, 0);
  demo_sleep(900);
  demo_dpad(1, 0);
  demo_sleep(1000);
  demo_event(INPUT_ENTER, 0, 0, 0);
  demo_sleep(2000);
  demo_piece_end();

  // 5. Options: the menu, the D-pad in it; the settings' look, its themes
  // chosen with the cursor.
  demo_piece("05-menu-temas");
  demo_event(INPUT_QUIT, 0, 0, 0);
  demo_sleep(1000);
  for(int i = 0; i < 3; i++) {
    demo_dpad(0, 1);
    demo_sleep(600);
  }
  demo_sleep(400);
  demo_main(demo_do_ui, "menu-do?settings%3Faspeto");
  demo_sleep(1800);
  static const char* const themes[] = {"rubi", "oceano", "champanhe", "claro", "neon"};
  for(const char* theme : themes) {
    char element[96];
    snprintf(element, sizeof(element), "document.querySelector('.theme[data-theme=%s]')", theme);
    if(demo_glide_to(PAGE, element, 700)) {
      demo_sleep(200);
      demo_click();
    } else {
      char setting[48];
      snprintf(setting, sizeof(setting), "theme%%3D%s", theme);
      demo_main(demo_do_set, setting);
    }
    demo_sleep(1300);
  }
  demo_piece_end();

  // 6. The console itself: the Sistema panel; the phone's pairing.
  demo_piece("06-sistema");
  demo_main(demo_do_settings, "sistema");
  demo_sleep(800);
  demo_glide(DISPLAY_WIDTH * 0.62f, DISPLAY_HEIGHT * 0.45f, 900);
  demo_sleep(3000);
  demo_main(demo_do_settings, "telemovel");
  demo_sleep(800);
  demo_glide(DISPLAY_WIDTH * 0.55f, DISPLAY_HEIGHT * 0.55f, 700);
  demo_sleep(3500);
  demo_piece_end();

  // 7. The video: a 2160p60 film on YouTube, decoded by the title; the
  // page, then the whole screen. Loaded, its consent answered and its ad
  // over before the piece; out again well before 40 s of it.
  before = demo_loads();
  demo_main(demo_do_load, DEMO_VIDEO);
  demo_wait_load(before, 20000);
  for(int i = 0; i < 40; i++) {
    demo_ask(PAGE, demo_consent_js, answer, sizeof(answer));
    if(!strcmp(answer, "rejected")) {
      POC_LOG("demo: the video page's consent: rejected");
      demo_sleep(3000);
      continue;
    }
    demo_ask(PAGE, demo_ad_js, answer, sizeof(answer));
    if(!strcmp(answer, "video")) {
      break;
    }
    POC_LOG("demo: the video page: %s", answer);
    demo_sleep(1000);
  }
  demo_ask(PAGE, demo_quality_js, answer, sizeof(answer));
  POC_LOG("demo: the video's quality: %s", answer);
  demo_play_video();
  demo_ask(PAGE, demo_quality_js, answer, sizeof(answer));  // again, now that it plays
  POC_LOG("demo: the video's quality, playing: %s", answer);
  demo_sleep(6000);
  demo_ask(PAGE, demo_video_js, answer, sizeof(answer));
  POC_LOG("demo: the video before its piece: %s", answer);
  demo_glide(DISPLAY_WIDTH * 0.85f, DISPLAY_HEIGHT * 0.9f, 600);
  demo_piece("07-video-4k");
  demo_sleep(9000);
  demo_main(demo_do_ui, "fullscreen");
  demo_sleep(11000);
  demo_ask(PAGE, demo_video_js, answer, sizeof(answer));
  POC_LOG("demo: the video at the end of its piece: %s", answer);
  demo_piece_end();
  demo_main(demo_do_ui, "fullscreen");
  demo_sleep(500);
  demo_main(demo_do_start, "");
  demo_sleep(3000);

  // 8. The end: options, the menu, Sair (filmed up to the menu).
  demo_piece("08-sair");
  demo_event(INPUT_QUIT, 0, 0, 0);
  demo_sleep(2500);
  demo_piece_end();
  POC_RESULT(1, "demo: done in %.0f s; Sair from the menu", (poc_now_ms() - app.start_ms) / 1e3);
  demo_main(demo_do_ui, "menu-do?quit");
}
#endif

#ifdef SHOW
/* make SHOW=1 (BrowserPS5 RN, 2026-11-11): the browser shows itself while
 * the TV is filmed, for a video to publish. DEMO's tour with the user's
 * own data (their sites open as they are: Telegram, WhatsApp, X), nothing
 * recorded by the title, and other stops: a 2160p60 film on YouTube with the
 * player's own statistics on the screen, three sites each in a tab of its
 * own, the tabs back and forth, the menus, the keyboard, a page with the
 * D-pad's ring and the reading mode, the settings' sections (never the
 * history, the phone's pairing, the downloads or TorBox's key: the video is
 * public), and the menu's Sair. No one may touch the controller while it
 * runs. Tabs left from the last time are closed first. */
#ifndef SHOW_VIDEO
#define SHOW_VIDEO DEMO_VIDEO
#endif
static const char show_stats_js[] =
  "(function () { var p = document.getElementById('movie_player');"
  " if (p && p.showVideoInfo) { p.showVideoInfo(); return 'statistics shown'; } return 'no statistics'; })()";
static const char show_stats_off_js[] =
  "(function () { var p = document.getElementById('movie_player'); if (p && p.hideVideoInfo) p.hideVideoInfo();"
  " var v = document.querySelector('video'); if (v) v.pause(); return v ? 'paused at ' + v.currentTime.toFixed(0) + ' s' : 'no video'; })()";

static void show_do_one_tab(const char*) {
  while(tab_count > 1) {
    tab_close(tab_count - 1);
  }
}
static void show_do_keys(const char*) { keyboard_for_bar(); }

// The right stick, down (or up) for ms.
static void
show_scroll(float dy, int ms) {
  for(int i = 0; i < ms / 16; i++) {
    demo_event(INPUT_SCROLL, 0, dy, 0);
    demo_sleep(16);
  }
}

// A site in a tab of its own: until it is in (at most load_ms), then stay_ms
// of it. The address is the user's own favorite at that host when there is
// one, as they saved it (2026-11-11 17:27: Telegram's /a/ and /k/ are two
// apps, each with its own session and theme; with a session in /k/, /a/
// came up light and with no chats).
static void
show_tab(const char* host, const char* fallback, int load_ms, int stay_ms) {
  unsigned before = demo_loads();
  char url[512];
  const char* json = favorites_json;
  const char* at = json ? strstr(json, host) : 0;

  snprintf(url, sizeof(url), "%s", fallback);
  if(at) {
    const char* start = at;
    while(start > json && start[-1] != '"') {
      start--;
    }
    const char* end = strchr(at, '"');
    if(end && (size_t)(end - start) < sizeof(url) && !strncmp(start, "https://", 8)) {
      memcpy(url, start, (size_t)(end - start));
      url[end - start] = 0;
    }
  }
  POC_LOG("show: a tab for %s", url);
  demo_main(demo_do_tab, url);
  demo_wait_load(before, load_ms);
  demo_sleep(stay_ms);
}

// The L2 or R2 menu (ring 0 or 1) open, the stick once round it (each of
// its sectors lit as it passes), then circle: closed with nothing done.
static void
show_radial(int ring) {
  const float cx = DISPLAY_WIDTH / 2.0f, cy = DISPLAY_HEIGHT / 2.0f;

  demo_event(INPUT_RADIAL, ring ? 1.0f : -1.0f, 0, 1);
  demo_sleep(900);
  for(int i = 0; i <= 120; i++) {
    const float a = i * 6.2832f / 96;
    input_event_t ev = {INPUT_MOVE, cx + sinf(a) * 60, cy - cosf(a) * 60, 0, 0, 0};
    on_input(&ev);
    demo_sleep(25);
  }
  demo_sleep(500);
  demo_event(INPUT_BACK, 0, 0, 0);
  demo_sleep(150);
  demo_event(INPUT_RADIAL, ring ? 1.0f : -1.0f, 0, 0);
  demo_sleep(700);
}

// Typed on RN's keyboard, a key at a time, its key lit.
static void
show_type(const char* text, int ms_per_key) {
  for(const char* c = text; *c; c++) {
    char script[512];
    char answer[8];

    if(*c == '\'' || *c == '\\') {  // not in the script's string
      continue;
    }
    snprintf(script, sizeof(script),
             "(function (c) { var k = null; document.querySelectorAll('.key').forEach(function (e) {"
             " if (!k && e.textContent.trim().toLowerCase() === c.toLowerCase()) k = e; });"
             " if (k) highlight(k); insert(c); return 'ok'; })('%c')", *c);
    demo_ask(KEYS, script, answer, sizeof(answer));
    demo_sleep(ms_per_key);
  }
}

// A section of the settings: shown, the cursor over it, a little further down, back up.
static void
show_settings(const char* section, int ms) {
  demo_main(demo_do_settings, section);
  demo_sleep(1100);
  demo_glide(DISPLAY_WIDTH * 0.60f, DISPLAY_HEIGHT * 0.45f, 700);
  demo_sleep(ms / 2);
  show_scroll(12, 700);
  demo_sleep(ms / 2);
}

#ifdef SHOW_PROBE_NOTIFY
static void show_notify_answer(const char*);  // after the questions' own code, below
static void show_notify_forget(const char* origin);
#endif

#ifdef SHOW_YT
// make SHOW=1 SHOW_YT=1 (2026-11-11 17:4x): the 2160p60 film alone,
// from its start, and what the player says every 5 s for two minutes: where
// it is, how much it has ahead, its own figures (the statistics' bandwidth
// and buffer), its error if it shows one. Then Sair.
static const char show_yt_js[] =
  "(function () { var p = document.getElementById('movie_player'), v = document.querySelector('#movie_player video') || document.querySelector('video');"
  " if (!p || !v) return 'no player';"
  " var b = v.buffered, ahead = 0, ranges = b.length; for (var i = 0; i < b.length; i++) if (b.start(i) <= v.currentTime + 0.5 && b.end(i) > v.currentTime) ahead = b.end(i) - v.currentTime;"
  " var s = null; try { s = p.getStatsForNerds ? p.getStatsForNerds(0) : null; } catch (e) { }"
  " var e = document.querySelector('.ytp-error-content-wrap, .ytp-error'), err = e && e.offsetParent ? (e.innerText || '').replace(/\\s+/g, ' ').slice(0, 70) : '';"
  " return 't ' + v.currentTime.toFixed(1) + (v.paused ? ' PAUSED' : '') + ', ready ' + v.readyState + ', ahead ' + ahead.toFixed(1) + ' s in ' + ranges + ' range(s), ' + v.videoWidth + 'x' + v.videoHeight"
  " + ', state ' + (p.getPlayerState ? p.getPlayerState() : '?') + ', ' + (p.getPlaybackQuality ? p.getPlaybackQuality() : '?')"
  " + (s ? ', speed ' + s.bandwidth_kbps + ', buffer ' + s.buffer_health_seconds + ', net ' + s.network_activity_bytes + ', ' + (s.resolution || '') + ', dropped ' + (s.dropped_frames || s.frames || '') : ', no statistics')"
  " + (v.error ? ', video error ' + v.error.code : '') + (err ? ', says: ' + err : ''); })()";

#if defined(SHOW_PROBE) && !defined(SHOW_PROBE_GL)
#define SHOW_GL_TRY ""  // the probe's first run makes no WebGL context
#else
#define SHOW_GL_TRY " try { gl = c.getContext('webgl') || c.getContext('experimental-webgl'); } catch (e) { }"
#endif
static void
show_about(void) {
  // What this browser says of itself to the page (what a site's check of the browser reads): three lines.
  static const char* const about[] = {
    "(function () { var n = navigator, z = ''; try { z = Intl.DateTimeFormat().resolvedOptions().timeZone; } catch (e) { z = 'threw'; }"
    " return 'clock ' + new Date().toISOString() + ', zone ' + z + ' (' + new Date().getTimezoneOffset() + '), platform [' + n.platform + '], vendor [' + n.vendor + '], webdriver ' + n.webdriver"
    " + ', language ' + n.language + ', languages ' + (n.languages || []).join('/') + ', plugins ' + (n.plugins ? n.plugins.length : '?') + ', mimeTypes ' + (n.mimeTypes ? n.mimeTypes.length : '?')"
    " + ', cores ' + n.hardwareConcurrency + ', memory ' + n.deviceMemory + ', touch ' + n.maxTouchPoints + ', cookies ' + n.cookieEnabled + ', online ' + n.onLine"
    " + ', pdf ' + n.pdfViewerEnabled + ', appVersion ' + String(n.appVersion).slice(0, 60); })()",
    "(function () { var s = screen, d = document; return 'visibility ' + d.visibilityState + ', hidden ' + d.hidden + ', focus ' + d.hasFocus()"
    " + ', screen ' + s.width + 'x' + s.height + ' avail ' + s.availWidth + 'x' + s.availHeight + ' depth ' + s.colorDepth + '/' + s.pixelDepth"
    " + ', window ' + innerWidth + 'x' + innerHeight + ' outer ' + outerWidth + 'x' + outerHeight + ' at ' + screenX + ',' + screenY + ', ratio ' + devicePixelRatio"
    " + ', orientation ' + (s.orientation ? s.orientation.type : 'none') + ', pointer ' + matchMedia('(pointer: fine)').matches + '/' + matchMedia('(hover: hover)').matches"
    " + ', history ' + history.length + ', referrer [' + d.referrer.slice(0, 30) + '], timer ' + (function () { var a = performance.now(), b = a, n = 0; while (b === a && n < 1e6) { b = performance.now(); n++; } return (b - a).toFixed(3); })() + ' ms'; })()",
    "(function () { var has = function (x) { var o = window, p = x.split('.'); for (var i = 0; i < p.length; i++) { if (o == null || !(p[i] in Object(o))) return 'n'; o = o[p[i]]; } return o == null ? 'n' : 'y'; }, c = document.createElement('canvas'), gl = null, g2 = null;"
    SHOW_GL_TRY " try { g2 = document.createElement('canvas').getContext('2d'); } catch (e) { }"
    " var list = ['AudioContext', 'webkitAudioContext', 'OfflineAudioContext', 'OffscreenCanvas', 'WebGLRenderingContext', 'WebGL2RenderingContext', 'speechSynthesis', 'Notification', 'navigator.serviceWorker',"
    " 'indexedDB', 'RTCPeerConnection', 'WebAssembly', 'crypto.subtle', 'navigator.mediaDevices', 'navigator.permissions', 'navigator.clipboard', 'navigator.storage', 'navigator.locks', 'SharedWorker', 'Worker',"
    " 'BroadcastChannel', 'IntersectionObserver', 'ResizeObserver', 'requestIdleCallback', 'navigator.getBattery', 'navigator.connection', 'navigator.userAgentData', 'navigator.share', 'PaymentRequest',"
    " 'ApplePaySession', 'safari', 'GestureEvent', 'TouchEvent', 'PointerEvent', 'visualViewport', 'CSS.supports', 'MediaSource', 'ManagedMediaSource', 'navigator.mediaSession', 'navigator.credentials', 'caches', 'DeviceOrientationEvent'];"
    " return 'webgl ' + (gl ? 'yes' : 'no') + ', 2d ' + (g2 ? 'yes' : 'no') + '; ' + list.map(function (x) { return x.replace('navigator.', 'n.') + ' ' + has(x); }).join(', '); })()",
    // 2026-11-14: movy.sx and
    // popcornmovies.st carry disable-devtool, which sends the page to about:blank when it takes the developer
    // tools for open: console.log() of a Date and of a function whose toString counts its calls (2 or more:
    // open), and of an element whose id has a getter (read at all: open). What this engine does with them.
    "(function () { var c = 0, d = new Date(); d.toString = function () { c++; return ''; }; console.log(d);"
    " var c2 = 0, f = function () {}; f.toString = function () { c2++; return ''; }; console.log(f);"
    " var c3 = 0, v = document.createElement('div'); Object.defineProperty(v, 'id', {get: function () { c3++; return ''; }}); console.log(v);"
    " var c4 = 0, r = /./; r.toString = function () { c4++; return ''; }; console.log(r); console.clear();"
    " return 'tools detectors: console.log read a date ' + c + ' time(s), a function ' + c2 + ', an element id ' + c3 + ', a regexp ' + c4"
    " + '; outer - inner ' + (outerWidth - innerWidth) + 'x' + (outerHeight - innerHeight) + '; eruda ' + (typeof window.eruda) + ', vconsole ' + (typeof window._vcOrigConsole); })()",
  };
  for(const char* script : about) {
    char said[1200];
    demo_ask(PAGE, script, said, sizeof(said));
    POC_LOG("show: about %s", said);
  }
}

static void
show_tour(void) {
  char answer[480];
  unsigned before;

  demo_main(show_do_one_tab, "");
#ifdef SHOW_UPDATE
  // make SHOW=1 SHOW_YT=1 SHOW_UPDATE=1 (2026-11-17): the browser's own updates, with GitHub itself. A first run,
  // with a release file older than the newest release: the list is asked for 3 s after the first page, the
  // installer fetched and checked, and this says how it went, then Sair. A second run finds the installer waiting
  // and leaves by itself at its first page (update_apply()), before this has anything to say.
  {
    int s = 0;
    for(; s < 600 && update_state != UPDATE_NONE && update_state != UPDATE_READY_NOW && update_state != UPDATE_NOT_NOW
          && update_state != UPDATE_LEAVING; s++) {
      demo_sleep(1000);
      if(s % 10 == 9) {
        POC_LOG("show: update at %d s: state %d, %llu bytes of the installer", s + 1, (int)update_state,
                (unsigned long long)update_got);
      }
    }
    if(update_state == UPDATE_LEAVING) {
      POC_LOG("show: update: an installer was waiting: the browser leaves by itself");
      return;
    }
    POC_RESULT(update_state == UPDATE_READY_NOW || update_state == UPDATE_NONE,
               "show: update: %s after %d s (%llu bytes fetched); Sair from the menu",
               update_state == UPDATE_READY_NOW ? "a newer release's installer is ready"
               : update_state == UPDATE_NONE ? "nothing newer (or not looked for)" : "it did not come", s,
               (unsigned long long)update_got);
    demo_main(demo_do_ui, "menu-do?quit");
    return;
  }
#endif
#ifdef SHOW_PROBE
  // make SHOW=1 SHOW_YT=1 SHOW_PROBE=1 (the "full" engine's first runs,
  // 2026-11-11): a plain page, what the engine says it has, Web Audio used
  // once (a faint tone for a second and a half, and a sound made off line);
  // with SHOW_PROBE_GL=1 also a WebGL context made, cleared and read back.
  // Then Sair.
  before = demo_loads();
  demo_main(demo_do_load, "http://example.com/");
  demo_wait_load(before, 20000);
  demo_sleep(1500);
  show_about();
  demo_ask(PAGE,
           "(function () { var d = window.__pace = {frames: 0, lag: 0, t0: performance.now()}, last = performance.now();"
           " (function f() { d.frames++; if (performance.now() - d.t0 < 3000) requestAnimationFrame(f); })();"
           " var id = setInterval(function () { var n = performance.now(); d.lag = Math.max(d.lag, n - last - 50); last = n; if (n - d.t0 > 3000) clearInterval(id); }, 50); return 'asked'; })()",
           answer, sizeof(answer));
  demo_sleep(3600);
  demo_ask(PAGE, "(function () { var d = window.__pace || {}; return d.frames + ' animation frames in 3 s, worst timer lag ' + Math.round(d.lag) + ' ms, visibility ' + document.visibilityState; })()",
           answer, sizeof(answer));
  POC_LOG("show: probe: pace %s (bisect: p%d u%d l%d f%d)", answer, bisect_off('p'), bisect_off('u'), bisect_off('l'), bisect_off('f'));
#ifndef SHOW_PROBE_NOAUDIO  // 2026-11-11 20:30: Web Audio's start waits for a sound output that does not open (32 s): left out with this
  demo_ask(PAGE,
           "(function () { window.__a = 'nothing'; try { var C = window.AudioContext || window.webkitAudioContext; if (!C) { __a = 'no AudioContext'; return 'asked'; }"
           " var c = new C(), o = c.createOscillator(), g = c.createGain(); g.gain.value = 0.02; o.connect(g); g.connect(c.destination); o.start(); __a = 'made, state ' + c.state + ', rate ' + c.sampleRate;"
           " c.resume().then(function () { __a += '; resumed, state ' + c.state; setTimeout(function () { __a += ', its time ' + c.currentTime.toFixed(2) + ' s'; o.stop(); c.close(); }, 1500); },"
           " function (e) { __a += '; resume refused ' + e.name; });"
           " var off = new OfflineAudioContext(1, 4410, 44100), s = off.createOscillator(); s.connect(off.destination); s.start();"
           " off.startRendering().then(function (b) { var d = b.getChannelData(0), m = 0; for (var i = 0; i < d.length; i++) m = Math.max(m, Math.abs(d[i])); __a += '; off line ' + b.length + ' frames, peak ' + m.toFixed(2); },"
           " function (e) { __a += '; off line refused ' + e.name; }); } catch (e) { __a = 'threw ' + e; } return 'asked'; })()",
           answer, sizeof(answer));
  demo_sleep(4000);
  demo_ask(PAGE, "(function () { return String(window.__a) + '; mediaSession ' + (navigator.mediaSession ? 'yes' : 'no') + ', Notification ' + (window.Notification ? Notification.permission : 'no')"
                 " + ', MathML ' + (document.createElementNS('http://www.w3.org/1998/Math/MathML', 'math') instanceof (window.MathMLElement || function () { }) ? 'yes' : 'no'); })()",
           answer, sizeof(answer));
  POC_LOG("show: probe: audio %s", answer);
#endif
#ifdef SHOW_PROBE_GL
  demo_ask(PAGE,
           "(function () { try { var c = document.createElement('canvas'); c.width = 64; c.height = 64; var gl = c.getContext('webgl'); if (!gl) return 'no context';"
           " var e = gl.getExtension('WEBGL_debug_renderer_info'); gl.clearColor(1, 0, 0.5, 1); gl.clear(gl.COLOR_BUFFER_BIT); var px = new Uint8Array(4); gl.readPixels(1, 1, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, px);"
           " return gl.getParameter(gl.VERSION) + ' | ' + gl.getParameter(gl.SHADING_LANGUAGE_VERSION) + ' | ' + (e ? gl.getParameter(e.UNMASKED_VENDOR_WEBGL) + ' / ' + gl.getParameter(e.UNMASKED_RENDERER_WEBGL) : 'no renderer info')"
           " + ' | pixel ' + Array.prototype.join.call(px, ',') + ' | error ' + gl.getError() + ' | ' + gl.getSupportedExtensions().length + ' extensions'; } catch (x) { return 'threw ' + x; } })()",
           answer, sizeof(answer));
  POC_LOG("show: probe: webgl %s", answer);
  demo_sleep(2000);
  // 2026-11-11 21:30: the same on the screen: a canvas over the page with a
  // triangle that turns (two shaders, a buffer, a uniform), six seconds of
  // animation frames; a picture of the screen after three (is it composited?).
  demo_ask(PAGE,
           "(function () { try { var c = document.createElement('canvas'); c.width = 640; c.height = 360;"
           " c.style.cssText = 'position:fixed;left:60px;top:60px;width:640px;height:360px;z-index:99999;border:2px solid #fff';"
           " document.body.appendChild(c); var gl = c.getContext('webgl', {antialias: true}); if (!gl) return 'no context';"
           " function sh(t, src) { var x = gl.createShader(t); gl.shaderSource(x, src); gl.compileShader(x); if (!gl.getShaderParameter(x, gl.COMPILE_STATUS)) throw 'shader: ' + gl.getShaderInfoLog(x); return x; }"
           " var p = gl.createProgram();"
           " gl.attachShader(p, sh(gl.VERTEX_SHADER, 'attribute vec2 a; attribute vec3 k; uniform float r; varying vec3 v; void main() { float c = cos(r), s = sin(r); gl_Position = vec4(a.x * c - a.y * s, a.x * s + a.y * c, 0.0, 1.0); v = k; }'));"
           " gl.attachShader(p, sh(gl.FRAGMENT_SHADER, 'precision mediump float; varying vec3 v; void main() { gl_FragColor = vec4(v, 1.0); }'));"
           " gl.linkProgram(p); if (!gl.getProgramParameter(p, gl.LINK_STATUS)) throw 'link: ' + gl.getProgramInfoLog(p); gl.useProgram(p);"
           " var b = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, b);"
           " gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([0, 0.8, 1, 0, 0, -0.7, -0.6, 0, 1, 0, 0.7, -0.6, 0, 0, 1]), gl.STATIC_DRAW);"
           " var a = gl.getAttribLocation(p, 'a'), k = gl.getAttribLocation(p, 'k');"
           " gl.enableVertexAttribArray(a); gl.vertexAttribPointer(a, 2, gl.FLOAT, false, 20, 0); gl.enableVertexAttribArray(k); gl.vertexAttribPointer(k, 3, gl.FLOAT, false, 20, 8);"
           " var r = gl.getUniformLocation(p, 'r'), d = window.__gl = {frames: 0, t0: performance.now(), error: 0, lost: false, px: new Uint8Array(4)};"
           " c.addEventListener('webglcontextlost', function () { d.lost = true; });"
           " (function f() { gl.viewport(0, 0, c.width, c.height); gl.clearColor(0.05, 0.05, 0.12, 1); gl.clear(gl.COLOR_BUFFER_BIT); gl.uniform1f(r, (performance.now() - d.t0) / 1000);"
           " gl.drawArrays(gl.TRIANGLES, 0, 3); d.frames++; gl.readPixels(320, 180, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, d.px); d.error = d.error || gl.getError();"
           " if (performance.now() - d.t0 < 6000) requestAnimationFrame(f); else d.done = true; })();"
           " return 'drawing'; } catch (x) { return 'threw ' + x; } })()",
           answer, sizeof(answer));
  POC_LOG("show: probe: webgl on the screen: %s", answer);
  demo_sleep(3000);
  demo_main([](const char*) { screenshot("webgl"); }, "");
  demo_sleep(4000);
  demo_ask(PAGE, "(function () { var d = window.__gl; return d ? d.frames + ' frames in 6 s, centre pixel ' + Array.prototype.join.call(d.px, ',') + ', error ' + d.error + ', lost ' + d.lost + ', done ' + !!d.done : 'nothing drawn'; })()",
           answer, sizeof(answer));
  POC_LOG("show: probe: webgl on the screen: %s", answer);
  // WebGL 2: a texture as a framebuffer read back, the canvas drawn into a 2D canvas, a data URL of it.
  demo_ask(PAGE,
           "(function () { try { var c = document.createElement('canvas'); c.width = 32; c.height = 32; var gl = c.getContext('webgl2'); if (!gl) return 'no webgl2 context';"
           " var t = gl.createTexture(); gl.bindTexture(gl.TEXTURE_2D, t);"
           " gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, 2, 2, 0, gl.RGBA, gl.UNSIGNED_BYTE, new Uint8Array([255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255]));"
           " var f = gl.createFramebuffer(); gl.bindFramebuffer(gl.FRAMEBUFFER, f); gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, t, 0);"
           " var ok = gl.checkFramebufferStatus(gl.FRAMEBUFFER) == gl.FRAMEBUFFER_COMPLETE, px = new Uint8Array(4); gl.readPixels(1, 0, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, px);"
           " gl.bindFramebuffer(gl.FRAMEBUFFER, null); gl.clearColor(0, 1, 0, 1); gl.clear(gl.COLOR_BUFFER_BIT);"
           " var c2 = document.createElement('canvas'); c2.width = c2.height = 32; var g = c2.getContext('2d'); g.drawImage(c, 0, 0); var q = g.getImageData(4, 4, 1, 1).data;"
           " return gl.getParameter(gl.VERSION) + ' | ' + gl.getParameter(gl.SHADING_LANGUAGE_VERSION) + ' | framebuffer ' + (ok ? 'complete' : 'incomplete') + ', texel ' + Array.prototype.join.call(px, ',')"
           " + ' | into 2d ' + Array.prototype.join.call(q, ',') + ' | data URL ' + c.toDataURL().length + ' | max texture ' + gl.getParameter(gl.MAX_TEXTURE_SIZE) + ' | error ' + gl.getError(); } catch (x) { return 'threw ' + x; } })()",
           answer, sizeof(answer));
  POC_LOG("show: probe: webgl2 %s", answer);
  demo_sleep(1500);
#endif
#ifdef SHOW_PROBE_NOTIFY
  // make SHOW_PROBE_NOTIFY=1 (2026-11-11 21:45): a site's own Notification on the "full" engine. An https page asks
  // for the permission: the app's window (a picture of it), answered "Permitir" here; then a notice (the PS5's
  // toast: "BrowserPS5 RN · teste"), its "show" event, and the site forgotten again.
  before = demo_loads();
  demo_main(demo_do_load, "https://example.com/");
  demo_wait_load(before, 20000);
  demo_sleep(1500);
  demo_ask(PAGE, "(function () { window.__n = {perm: 'nothing yet'}; try { __n.before = Notification.permission;"
                 " var r = Notification.requestPermission(function (p) { __n.callback = p; });"
                 " if (r && r.then) r.then(function (p) { __n.perm = p; }, function (e) { __n.perm = 'refused ' + e; }); } catch (e) { __n.perm = 'threw ' + e; }"
                 " return 'asked, permission before: ' + __n.before; })()",
           answer, sizeof(answer));
  POC_LOG("show: probe: notify %s", answer);
  demo_sleep(1500);
  demo_main([](const char*) { screenshot("notify"); }, "");
  demo_sleep(1500);
  demo_main(show_notify_answer, "");
  demo_sleep(1000);
  demo_ask(PAGE, "(function () { try { var n = new Notification('BrowserPS5 RN', {body: 'teste'}); __n.made = 'made';"
                 " n.onshow = function () { __n.made = 'shown'; }; n.onerror = function () { __n.made = 'error event'; }; } catch (e) { __n.made = 'threw ' + e; }"
                 " return 'the answer: ' + __n.perm + ', callback ' + __n.callback + ', permission now ' + Notification.permission; })()",
           answer, sizeof(answer));
  POC_LOG("show: probe: notify %s", answer);
  demo_sleep(2500);
  demo_ask(PAGE, "(function () { return 'the notice: ' + __n.made; })()", answer, sizeof(answer));
  POC_LOG("show: probe: notify %s", answer);
  // 23:05: the D-pad as the arrow keys (the settings' "Teclas de setas"), on this page: the keys it gets, and
  // the cursor hidden while they drive. The user's own choice is put back.
  {
    static char back[40];
    snprintf(back, sizeof(back), "dpad%%3D%s", settings.dpad);
    demo_main(demo_do_set, "dpad%3Dkeys");
    demo_sleep(600);
    demo_ask(PAGE, "(function () { window.__k = []; addEventListener('keydown', function (e) { __k.push(e.key); }, true); return 'listening'; })()",
             answer, sizeof(answer));
    demo_dpad(0, 1);
    demo_sleep(300);
    demo_dpad(1, 0);
    demo_sleep(300);
    demo_dpad(0, -1);
    demo_sleep(300);
    demo_dpad(-1, 0);
    demo_sleep(500);
    demo_ask(PAGE, "(function () { return (window.__k || []).join(' '); })()", answer, sizeof(answer));
    POC_LOG("show: probe: D-pad as keys: the page got [%s], the cursor is %s", answer, app.cursor_off ? "hidden" : "SHOWN");
    demo_glide(DISPLAY_WIDTH * 0.55f, DISPLAY_HEIGHT * 0.50f, 400);
    demo_sleep(300);
    POC_LOG("show: probe: D-pad as keys: after the stick moved, the cursor is %s", app.cursor_off ? "HIDDEN" : "shown");
    demo_main(demo_do_set, back);
    demo_sleep(600);
    POC_LOG("show: probe: the D-pad is \"%s\" again", settings.dpad);
  }
  // The settings' list of the sites answered for ("Notificações dos sites"), and its "Esquecer".
  demo_main(demo_do_settings, "privacidade");
  demo_sleep(2500);
  demo_ask(PAGE, "(function () { var b = document.getElementById('notify-sites'), h = document.getElementById('notify-head');"
                 " if (!b) return 'no list in this page: ' + location.href.slice(0, 50);"
                 " b.scrollIntoView({block: 'center'});"
                 " return 'heading ' + (h && !h.hidden ? '[' + h.textContent + ']' : 'hidden') + ', rows ' + b.querySelectorAll('.item').length + ': ' + b.innerText.split(String.fromCharCode(10)).filter(Boolean).join(' / ').slice(0, 160); })()",
           answer, sizeof(answer));
  POC_LOG("show: probe: notify sites in the settings: %s", answer);
  demo_sleep(700);
  demo_main([](const char*) { screenshot("notify sites"); }, "");
  demo_ask(PAGE, "(function () { var x = document.querySelector('#notify-sites .ib.x'); if (!x) return 'no Esquecer button'; x.click();"
                 " return 'pressed Esquecer; rows now ' + document.querySelectorAll('#notify-sites .item').length; })()",
           answer, sizeof(answer));
  POC_LOG("show: probe: notify sites in the settings: %s", answer);
  demo_sleep(1200);
  demo_main(show_notify_forget, "https://example.com");  // if the button did not: the test's site must not stay
  demo_sleep(500);
  // 2026-11-14: RN's own switches in the settings ("Transferências rápidas", "Páginas leves ao lado do jogo"):
  // are they shown, and the first one pressed twice (the other way, then back as the user has it).
  {
    const int multipart_before = settings.multipart;
    demo_main(demo_do_settings, "transferencias");
    demo_sleep(2500);
    demo_ask(PAGE,
             "(function () { var b = document.querySelector('[data-switch=\"multipart\"]'), s = document.querySelector('[data-switch=\"side_mobile\"]'),"
             " k = document.querySelector('[data-switch=\"ps5_keyboard\"]');"
             " if (!b) return 'no switch in this page: ' + location.href.slice(0, 50);"
             " function shown(e) { return e ? getComputedStyle(e).display : 'not there'; }"
             " b.scrollIntoView({block: 'center'}); var was = b.classList.contains('on'), box = b.getBoundingClientRect(); b.click();"
             " return 'multipart ' + shown(b) + ' ' + Math.round(box.width) + 'x' + Math.round(box.height) + ', icon ' + (document.getElementById('i-top-speed') ? 'yes' : 'NO')"
             " + ', was ' + (was ? 'on' : 'off') + ', pressed: ' + (b.classList.contains('on') ? 'on' : 'off')"
             " + '; side_mobile ' + shown(s) + '; system_keyboard ' + shown(k); })()",
             answer, sizeof(answer));
    demo_sleep(700);
    POC_LOG("show: probe: RN's switches: %s; settings.multipart %d -> %d", answer, multipart_before, settings.multipart);
    demo_main([](const char*) { screenshot("rn switches"); }, "");
    if(settings.multipart != multipart_before) {  // back as it was, by the same button
      demo_ask(PAGE, "(function () { var b = document.querySelector('[data-switch=\"multipart\"]'); b.click(); return b.classList.contains('on') ? 'on' : 'off'; })()",
               answer, sizeof(answer));
      demo_sleep(700);
    }
    if(settings.multipart != multipart_before) {  // the button did not: the user's choice must not stay changed
      demo_main(demo_do_set, multipart_before ? "multipart%3D1" : "multipart%3D0");
      demo_sleep(500);
    }
    POC_LOG("show: probe: RN's switches: pressed again: %s, settings.multipart %d (it was %d)", answer, settings.multipart, multipart_before);
    demo_main(demo_do_settings, "acerca");
    demo_sleep(2500);
    demo_ask(PAGE, "(function () { var v = document.getElementById('about-version'), e = document.getElementById('about-engine');"
                   " return (v ? v.textContent : 'no version') + ' | ' + (e ? e.textContent : 'no engine'); })()", answer, sizeof(answer));
    POC_LOG("show: probe: the settings' About says: %s", answer);
    {  // BrowserPS5 RN's other languages (2026-11-14): the settings and the bar in Spanish and in German, a notice
       // of the app's own, then the language the user has, whatever happened in between
      static char lang_back[24], lang_set[2][24], bar_said[300];
      static const char* const codes[] = {"es", "de"};
      snprintf(lang_back, sizeof(lang_back), "lang%%3D%s", settings.lang);
      for(int k = 0; k < 2; k++) {
        snprintf(lang_set[k], sizeof(lang_set[k]), "lang%%3D%s", codes[k]);
        demo_main(demo_do_set, lang_set[k]);
        demo_sleep(1200);
        demo_main(demo_do_settings, "geral");
        demo_sleep(2500);
        demo_ask(PAGE,
                 "(function () { var on = document.querySelector('.choices[data-key=\"lang\"] .choice.on b'), shown = 0;"
                 " document.querySelectorAll('.choices[data-key=\"lang\"] .choice').forEach(function (b) { if (getComputedStyle(b).display !== 'none') shown++; });"
                 " return document.documentElement.lang + ' | ' + document.title + ' | ' + [].map.call(document.querySelectorAll('nav a, nav button'), function (a) { return a.textContent.trim(); }).slice(0, 7).join(', ')"
                 " + ' | chosen ' + (on ? on.textContent : '?') + ' of ' + shown + ' | ' + (document.querySelector('main h1') || {}).textContent; })()",
                 answer, sizeof(answer));
        demo_ask(BAR, "(function () { var a = document.getElementById('address'); return document.documentElement.lang + ' | ' + (a ? a.placeholder : 'no address field'); })()",
                 bar_said, sizeof(bar_said));
        POC_LOG("show: probe: language %s: settings %s; bar %s; the app says \"%s\" and \"%s\"", codes[k], answer, bar_said,
                LM("Open a page first"), LM("Try again"));
        demo_main([](const char*) { screenshot("language"); }, "");
        demo_sleep(500);
      }
      demo_main(demo_do_set, lang_back);
      demo_sleep(1200);
      POC_LOG("show: probe: the language is \"%s\" again", settings.lang);
    }
    {  // a site that is not there, and an address nobody answers at: the page each gets (error_page_show())
      static char bar_url[600];
      const char* const sites[] = {"http://bps5-no-such-site.invalid/page?x=1", "http://127.0.0.1:9/"};
      for(const char* site : sites) {
        demo_main(demo_do_load, site);
        demo_sleep(6000);
        demo_ask(PAGE,
                 "(function () { var h = document.querySelector('h1'), a = document.querySelector('a.again'), s = document.querySelector('small'),"
                 " o = document.querySelector('p.host'); return (h ? h.textContent : 'NO error page: ' + document.title + ' ' + location.href.slice(0, 60))"
                 " + ' | ' + (o ? o.textContent : '') + ' | ' + (a ? a.textContent + ' -> ' + a.getAttribute('href') : 'no button') + ' | ' + (s ? s.textContent : ''); })()",
                 answer, sizeof(answer));
        demo_main([](const char*) { take_url(WKPageCopyActiveURL(page(PAGE)), bar_url, sizeof(bar_url)); }, "");
        demo_sleep(300);
        POC_LOG("show: probe: a site that does not load: %s; the bar has %s", answer, bar_url);
        demo_main([](const char*) {  // what error_page_of() goes by: how the engine commits such a page
          static char committed[80];
          take_url(WKPageCopyCommittedURL(page(PAGE)), committed, sizeof(committed));
          POC_LOG("show: probe: that page is committed as \"%s\"; it counts as one that did not load: %s; back is %s", committed,
                  error_page_of(page(PAGE)) ? "yes" : "NO", WKPageCanGoBack(page(PAGE)) ? "possible" : "not possible");
        }, "");
        demo_sleep(300);
        demo_main([](const char*) { screenshot("error page"); }, "");
        demo_sleep(500);
      }
      demo_main([](const char*) { start_open(); }, "");  // from such a page to the browser's own: no longer "did not load"
      demo_sleep(2500);
      demo_main([](const char*) {
        POC_LOG("show: probe: the start page after it %s", error_page_of(page(PAGE)) ? "STILL counts as a page that did not load"
                                                                                      : "is the browser's own again (not one that did not load)");
      }, "");
      demo_sleep(300);
      // A title with half of an emoji (a lone surrogate): the engine's strict copy wrote no end for it (wk_utf8()).
      demo_ask(PAGE, "(function () { window.__bps5title = document.title; document.title = 'half \\ud83c'; return 'set'; })()", answer, sizeof(answer));
      demo_sleep(700);
      demo_main([](const char*) {
        static char title[64];
        memset(title, '#', sizeof(title) - 1);  // (what would show after its letters, were no end written)
        take_string(WKPageCopyTitle(page(PAGE)), title, sizeof(title));
        POC_LOG("show: probe: a title with half an emoji is taken as \"%s\" (%zu bytes; 8 are right: \"half \" and U+FFFD)", title, strlen(title));
      }, "");
      demo_sleep(300);
      demo_ask(PAGE, "(function () { document.title = window.__bps5title; return 'back'; })()", answer, sizeof(answer));
      demo_sleep(300);
      // A tab on this console's own services, asked for as a site's window.open() would (site_may_open()): with the
      // address written as one number, which the engine reads as 127.0.0.1. Nothing is opened.
      demo_main([](const char*) {
        const int before = tab_count;
        last_gesture_ms = poc_now_ms();
        page_command("newtab?http%3A%2F%2F2130706433%3A8080%2Fbps5-probe", true);
        POC_LOG("show: probe: a tab on this console's services, asked for as a site would: %s (tabs %d -> %d); 2130706433 is %s, example.com is %s",
                tab_count == before ? "not opened" : "OPENED", before, tab_count,
                url_this_console("http://2130706433:8080/") ? "this console" : "NOT taken as this console",
                url_this_console("https://example.com/") ? "TAKEN AS THIS CONSOLE" : "not this console");
      }, "");
      demo_sleep(1500);
      // 2026-11-14: a window of another site, asked for by the page's own script right after a press, as the
      // advertisements of film sites do (tabs_js, popup_asked()): not opened, and the page stays where it is.
      {
        static int tabs_before;
        static unsigned blocked_before;
        static char url_before[2048], url_after[2048], gave[200];
        demo_main([](const char*) {
          tabs_before = tab_count;
          blocked_before = popups_blocked;
          last_gesture_ms = poc_now_ms();
        }, "");
        demo_ask(PAGE, "location.href", url_before, sizeof(url_before));
        demo_ask(PAGE, "(function () { window.dispatchEvent(new MouseEvent('mousedown'));"
                 " var w = window.open('https://ads.example.net/bps5-probe'); return String(w); })()", gave, sizeof(gave));
        demo_sleep(1500);
        demo_ask(PAGE, "location.href", url_after, sizeof(url_after));
        demo_main([](const char*) {
          POC_LOG("show: probe: a window of another site, asked for by a script after a press: %s (tabs %d -> %d, blocked %u -> %u, "
                  "window.open gave %s, the page %s)",
                  tab_count == tabs_before && popups_blocked == blocked_before + 1 && !strcmp(url_before, url_after) ? "not opened" : "WRONG",
                  tabs_before, tab_count, blocked_before, popups_blocked, gave, strcmp(url_before, url_after) ? "MOVED" : "stayed");
        }, "");
      }
      // 2026-11-14, the ad blocker's second part: what is hidden in a page (the lists 3 and 4), and a page that
      // takes the tab to an advertising host (strict_host()). On a real page, example.com.
      {
        static char hid[300], url_before[2048], url_after[2048];
        static unsigned blocked_before;
        demo_main(demo_do_load, "http://example.com/");
        demo_sleep(4000);
        demo_ask(PAGE, "(function () { var mk = function (id, cls) { var e = document.createElement('div'); if (id) e.id = id; if (cls) e.className = cls;"
                 " e.textContent = 'x'; document.body.appendChild(e); return e; };"
                 " var a = mk('google_ads', ''), b = mk('', 'ad_unit'), c = mk('', 'bps5-probe-plain');"
                 " var d = function (e) { return getComputedStyle(e).display; };"
                 " var r = '#google_ads ' + d(a) + ', .ad_unit ' + d(b) + ', a plain one ' + d(c); a.remove(); b.remove(); c.remove(); return r; })()",
                 hid, sizeof(hid));
        demo_main([](const char*) {
          POC_LOG("show: probe: hidden in a page: %s (%s; lists ready: hiding %d, by address %d; right is none, none, block)", hid,
                  strstr(hid, "#google_ads none, .ad_unit none, a plain one block") ? "right" : rules_filter[3] ? "WRONG" : "the list is not ready yet",
                  rules_filter[3] != 0, rules_filter[4] != 0);
          blocked_before = strict_blocked;
        }, "");
        demo_ask(PAGE, "location.href", url_before, sizeof(url_before));
        demo_ask(PAGE, "(function () { location.href = 'http://doubleclick.net/bps5-probe'; return 'asked'; })()", hid, sizeof(hid));
        demo_sleep(2500);
        demo_ask(PAGE, "location.href", url_after, sizeof(url_after));
        demo_main([](const char*) {
          POC_LOG("show: probe: a page that takes the tab to an advertising host: %s (blocked %u -> %u of %zu hosts, the page %s at %s)",
                  strict_blocked == blocked_before + 1 && !strcmp(url_before, url_after) ? "not let" : strict_count ? "WRONG" : "no list of hosts",
                  blocked_before, strict_blocked, strict_count, strcmp(url_before, url_after) ? "MOVED" : "stayed", url_after);
        }, "");
      }
      // 2026-11-14: the start page's "your game" row (games.c, start-rn.js): what the console says of its games,
      // and what the page made of it.
      {
        static char row[500];
        demo_main([](const char*) {
          games_ms = -1e9;  // looked for now
          start_open();
        }, "");
        demo_sleep(3500);
        demo_ask(PAGE, "(function () { var b = document.getElementById('games-box'); if (!b) return 'no row (' + (window.data && data.games ? data.games.length : 'no') + ' games in the data)';"
                 " var i = b.querySelector('.g-cover img'), n = b.querySelector('.g-name');"
                 " return 'row there: ' + b.querySelectorAll('a.g-chip').length + ' links, ' + b.querySelectorAll('.g-other').length + ' other game(s), the game [' + (n ? n.textContent : '') + '], '"
                 " + (b.querySelector('.g-state') || {textContent: ''}).textContent + ', its icon ' + (i ? i.naturalWidth + 'x' + i.naturalHeight : 'not shown') + ', colour ' + b.style.getPropertyValue('--g-rgb'); })()",
                 row, sizeof(row));
        demo_main([](const char*) {
          POC_LOG("show: probe: games: %s", games_note);
          POC_LOG("show: probe: the start page's game row: %s", row);
          screenshot("the start page with the game row");
        }, "");
        demo_ask(PAGE, "(function () { var b = document.getElementById('games-box'); if (b) b.scrollIntoView({block: 'center'}); return 'scrolled'; })()", row, sizeof(row));
        demo_sleep(900);
        demo_main([](const char*) { screenshot("the game row"); }, "");
        // Each of the other games picked in turn: its name's length and its trophies as the row says them; the
        // first with trophies stays shown for the picture.
        demo_ask(PAGE, "(function () { var b = document.getElementById('games-box'), out = [], keep = null; if (!b) return 'no row';"
                 " var others = b.querySelectorAll('.g-other');"
                 " for (var i = 0; i < others.length; i++) { others[i].click(); var c = b.querySelector('.g-cup'), n = b.querySelector('.g-name');"
                 "   out.push((n ? n.textContent.length : 0) + ' letters ' + (c ? c.textContent.replace(/[^0-9\\/ ]/g, '').trim() : 'none')); if (c && !keep) keep = others[i]; }"
                 " if (keep) keep.click(); return others.length + ' game(s): ' + out.join('; '); })()", row, sizeof(row));
        demo_sleep(900);
        demo_main([](const char*) {
          POC_LOG("show: probe: the row's games, their trophies (have / all): %s", row);
          screenshot("the game row with trophies");
          // The PS5's captures as the files dialog would list them (xps5/captures.c): counts only.
          capture_t* list = (capture_t*)malloc(CAPTURES_MAX * sizeof(*list));
          const int n = list ? captures_list(list, CAPTURES_MAX) : -1;
          int pictures = 0, clips = 0, hdr = 0, named = 0;
          for(int i = 0; i < n; i++) {
            pictures += list[i].kind == CAPTURE_JPEG || list[i].kind == CAPTURE_PNG;
            clips += list[i].kind == CAPTURE_MP4 || list[i].kind == CAPTURE_WEBM;
            hdr += list[i].kind == CAPTURE_JXR;
            named += strstr(list[i].path, "/" CAPTURES_APP "/") != 0;
          }
          POC_LOG("show: probe: the PS5's captures: %d listed (%d screenshot(s), %d clip(s), %d in HDR), %d under a game's folder; "
                  "folders read %d, not readable %d, the newest %lld s old",
                  n, pictures, clips, hdr, named, captures_stats.folders, captures_stats.unreadable,
                  n > 0 ? (long long)time(0) - list[0].time : -1LL);
          free(list);
        }, "");
        // 2026-11-15: etaHEN's cheat files (cheat_ask()). What the console has of etaHEN; one small file of the
        // repository asked for as the start page's row does, where it landed, and it is REMOVED again with its
        // line (and the folders, when this made them); then a name that is no cheat file's, which must not be taken.
        {
          static int state_before, files_before[3];
          static unsigned saved_before;
          static char said[120];
          demo_main([](const char*) {
            static const char* const kinds[] = {"json", "shn", "mc4"};
            state_before = cheats_state();
            for(int k = 0; k < 3; k++) {
              char dir[96];
              snprintf(dir, sizeof(dir), CHEATS_DIR "/%s", kinds[k]);
              files_before[k] = -1;
              if(DIR* d = opendir(dir)) {
                files_before[k] = 0;
                for(struct dirent* e; (e = readdir(d));) {
                  files_before[k] += e->d_name[0] != '.';
                }
                closedir(d);
              }
            }
            saved_before = cheats_saved;
            cheat_last_path[0] = 0;
            POC_LOG("show: probe: etaHEN on this console: %s; cheat files there: json %d, shn %d, mc4 %d (-1: no such folder)",
                    state_before == 2 ? CHEATS_DIR " is there" : state_before == 1 ? CHEATS_HOME " is there, without a cheats folder"
                                                                                  : "no " CHEATS_HOME,
                    files_before[0], files_before[1], files_before[2]);
          }, "");
          // The panel itself, for the fourth game of the row: what it lists once the indexes are read, and a picture.
          static char panel[400];
          demo_ask(PAGE, "(function () { var o = document.querySelectorAll('#games-box .g-other'); if (o[3]) o[3].click();"
                   " var c = document.querySelectorAll('#games-box .g-links a')[1]; if (!c) return 'no cheats link'; c.click(); return 'opened'; })()",
                   panel, sizeof(panel));
          demo_sleep(7000);
          demo_ask(PAGE, "(function () { var p = document.getElementById('cheats-panel'); if (!p) return 'no panel';"
                   " var rows = p.querySelectorAll('.c-row'), l = p.querySelector('.c-list');"
                   " return rows.length + ' file(s) listed' + (rows.length ? ', the first ' + rows[0].querySelector('.c-what').textContent.replace(/[^ -~]/g, '-') + ' [' + rows[0].querySelector('a').getAttribute('href') + ']'"
                   " : ', saying: ' + (l ? l.textContent.replace(/[^ -~]/g, '-').slice(0, 90) : '')) + '; ' + p.querySelectorAll('.c-what b').length + ' marked as the version here'; })()",
                   panel, sizeof(panel));
          demo_main([](const char*) {
            POC_LOG("show: probe: the cheats panel of the fourth game: %s", panel);
            screenshot("the cheats panel");
          }, "");
          demo_ask(PAGE, "(function () { var p = document.getElementById('cheats-panel'); if (p) p.remove(); return 'closed'; })()", panel, sizeof(panel));
          // 2026-11-15: the game hub of the second game (its cover is pressed): its trophies, one by one.
          demo_ask(PAGE, "(function () { var o = document.querySelectorAll('#games-box .g-other'); if (o[1]) o[1].click();"
                   " var c = document.querySelector('#games-box .g-cover'); if (!c) return 'no cover'; c.click(); return 'opened'; })()",
                   panel, sizeof(panel));
          demo_sleep(3000);
          demo_ask(PAGE, "(function () { var p = document.getElementById('game-hub'); if (!p) return 'no hub';"
                   " var rows = p.querySelectorAll('.t-row'), missing = p.querySelectorAll('.t-row:not(.have)'), links = p.querySelectorAll('.t-row a');"
                   " return rows.length + ' trophy row(s), ' + missing.length + ' not won yet, ' + links.length + ' guide link(s), play button '"
                   " + (p.querySelector('a.h-play') ? 'there' : 'MISSING') + ', first row: ' + (rows[0] ? rows[0].textContent.replace(/[^ -~]/g, '-').slice(0, 70) : 'none'); })()",
                   panel, sizeof(panel));
          demo_main([](const char*) {
            POC_LOG("show: probe: the game hub of the second game: %s", panel);
            screenshot("the game hub");
          }, "");
          // The notes of that game: written as the hub writes them, read back by opening the hub again, then removed.
          demo_ask(PAGE, "(function () { var p = document.getElementById('game-hub'), n = p && p.querySelector('textarea.h-notes'); if (!n) return 'no notes box';"
                   " n.value = 'BPS5 probe: door code 4-7-1, boss weak to fire'; n.dispatchEvent(new Event('change')); return 'written'; })()", panel, sizeof(panel));
          demo_sleep(1200);
          demo_ask(PAGE, "(function () { var p = document.getElementById('game-hub'); if (p) p.remove(); var c = document.querySelector('#games-box .g-cover'); if (c) c.click(); return 'reopened'; })()",
                   panel, sizeof(panel));
          demo_sleep(2500);
          demo_ask(PAGE, "(function () { var p = document.getElementById('game-hub'), n = p && p.querySelector('textarea.h-notes'), s = p && p.querySelector('.h-sum');"
                   " return (n ? 'notes read back: [' + n.value + ']' : 'no notes box') + '; trophies summed: ' + (s ? s.textContent.replace(/[^ -~]/g, '-') : 'no sum'); })()", panel, sizeof(panel));
          demo_main([](const char*) {
            POC_LOG("show: probe: the game hub again: %s", panel);
          }, "");
          // 2026-11-16: pages kept with that game: the menu's way refuses the start page; one kept as the menu keeps
          // them, read back in its page, taken away with its button, and its file gone with it.
          demo_ask(PAGE, "(function () { var a = document.querySelector('#game-hub a.h-play'); return a ? a.getAttribute('href').split('?')[1] : ''; })()",
                   panel, sizeof(panel));
          demo_main([](const char*) {
            static char id[16];
            snprintf(id, sizeof(id), "%.9s", panel);
            game_pin_current();  // (the start page is shown: refused)
            const bool ok = game_of_row(id) && game_pages_write(id, "https://example.com/bps5-probe-guide", "BPS5 probe: the map of the game", 0);
            POC_LOG("show: probe: a page kept with %s as the menu keeps them: %s", id, ok ? "written" : "NOT written");
          }, "");
          demo_ask(PAGE, "(function () { var p = document.getElementById('game-hub'); if (p) p.remove(); var c = document.querySelector('#games-box .g-cover'); if (c) c.click(); return 'reopened'; })()",
                   panel, sizeof(panel));
          demo_sleep(2500);
          demo_ask(PAGE, "(function () { var p = document.getElementById('game-hub'); if (!p) return 'no hub'; var r = p.querySelectorAll('.h-pin');"
                   " var s = r.length ? r[0].textContent.replace(/[^ -~]/g, '-') + ' [' + r[0].getAttribute('href') + ']' : 'none';"
                   " var x = p.querySelector('.h-unpin'); if (x) x.click(); return r.length + ' page(s) kept listed, the first ' + s + '; after its remove button '"
                   " + p.querySelectorAll('.h-pin').length + ' listed, the hint ' + (p.querySelector('.h-pins-none') ? 'shown' : 'NOT shown'); })()", panel, sizeof(panel));
          demo_sleep(800);
          demo_main([](const char*) {
            POC_LOG("show: probe: pages kept with the game: %s", panel);
            DIR* d = opendir(GAME_PAGES);
            int files = 0;
            for(struct dirent* e; d && (e = readdir(d));) {
              files += e->d_name[0] != '.';
            }
            if(d) {
              closedir(d);
            }
            POC_LOG("show: probe: pages kept with the game: %d file(s) left in " GAME_PAGES, files);
          }, "");
          // 2026-11-16: the same game from the phone's card (phone.c /api/game): what the card is given, notes and a
          // page written as the phone writes them (handed to the main thread), the game's page on the TV showing
          // them at once, a javascript: address refused, then the page taken away (the notes go with the next step).
          static char probe_game[16];
          static char probe_json[PHONE_GAME_MAX];
          demo_ask(PAGE, "(function () { var a = document.querySelector('#game-hub a.h-play'); return a ? a.getAttribute('href').split('?')[1] : ''; })()",
                   panel, sizeof(panel));
          demo_main([](const char*) {
            snprintf(probe_game, sizeof(probe_game), "%.9s", panel);
            const size_t n = phone_game_json(probe_game, probe_json, sizeof(probe_json), 0);
            int count = 0;
            for(const char* p = probe_json; (p = strstr(p, "{\"id\": ")); p++) {
              count++;
            }
            char chosen[32];
            snprintf(chosen, sizeof(chosen), "\"id\": \"%s\", \"name\"", probe_game);
            POC_LOG("show: probe: the phone's game card: %zu bytes, %d game(s) of the row, %s chosen, notes %s, pages %s", n, count,
                    strstr(probe_json, chosen) ? probe_game : "NOT", strstr(probe_json, "\"notes\": ") ? "there" : "MISSING",
                    strstr(probe_json, "\"pages\": [") ? "there" : "MISSING");
            phone_game_do("note", probe_game, "BPS5 phone probe: written on the phone", 0);
            phone_game_do("keep", probe_game, "https://example.com/bps5-phone-page\tBPS5 phone probe: a page from the phone", 0);
            phone_game_do("keep", probe_game, "javascript:alert(1)\tbad", 0);
          }, "");
          demo_sleep(2500);
          demo_ask(PAGE, "(function () { var p = document.getElementById('game-hub'); if (!p) return 'no hub'; var n = p.querySelector('textarea.h-notes'), r = p.querySelectorAll('.h-pin');"
                   " return 'notes [' + (n ? n.value : '?') + '] and ' + r.length + ' page(s) kept' + (r.length ? ', the first ' + r[0].textContent.replace(/[^ -~]/g, '-') : ''); })()",
                   panel, sizeof(panel));
          demo_main([](const char*) {
            POC_LOG("show: probe: the game's page on the TV after the phone wrote: %s", panel);
            phone_game_json(probe_game, probe_json, sizeof(probe_json), 0);
            POC_LOG("show: probe: the phone's card read again: notes %s, its page %s, the javascript: one %s",
                    strstr(probe_json, "written on the phone") ? "there" : "NOT there", strstr(probe_json, "bps5-phone-page") ? "there" : "NOT there",
                    strstr(probe_json, "javascript:") ? "KEPT (WRONG)" : "refused");
            phone_game_do("unpin", probe_game, "https://example.com/bps5-phone-page", 0);
          }, "");
          demo_sleep(1200);
          demo_main([](const char*) {
            struct stat st;
            char path[160];
            phone_game_json(probe_game, probe_json, sizeof(probe_json), 0);
            snprintf(path, sizeof(path), GAME_PAGES "/%s.txt", probe_game);
            POC_LOG("show: probe: the phone's page taken away: %s; the game's file of pages %s",
                    strstr(probe_json, "bps5-phone-page") ? "STILL there" : "gone", stat(path, &st) ? "gone" : "STILL there");
          }, "");
          demo_ask(PAGE, "(function () { var p = document.getElementById('game-hub'), n = p && p.querySelector('textarea.h-notes'); if (!n) return 'no notes box';"
                   " n.value = ''; n.dispatchEvent(new Event('change')); return 'emptied'; })()", panel, sizeof(panel));
          demo_sleep(1000);
          // 2026-11-16 (docs/32, idea 4): a capture to Discord, with the probe's own listener as Discord. No webhook
          // kept: the page is asked for one (its box, over a stand-in of the capture's view); a wrong address is
          // refused and asked again; the stand-in's address given as the page gives it (spaces around): kept
          // (0600), the test picture sent, Discord's 204 a notice. A webhook of the user's, if any, is put back.
          static char probe_hook_keep[SHARE_HOOK_MAX];
          static bool probe_hook_had;
          static char probe_hook_js[600];
          demo_main([](const char*) {
            static const unsigned char png[] = {
              0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01,
              0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00, 0x0a, 0x49, 0x44, 0x41,
              0x54, 0x78, 0x9c, 0x63, 0x00, 0x01, 0x00, 0x00, 0x05, 0x00, 0x01, 0x0d, 0x0a, 0x2d, 0xb4, 0x00, 0x00, 0x00, 0x00, 0x49,
              0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
            probe_hook_had = discord_hook_read(probe_hook_keep, sizeof(probe_hook_keep));
            unlink(DISCORD_HOOK);
            FILE* f = fopen(PROBE_SHOT, "wb");
            const bool written = f && fwrite(png, 1, sizeof(png), f) == sizeof(png);
            if(f) {
              fclose(f);
            }
            const bool listening = probe_hook_listen();
            POC_LOG("show: probe: discord: a webhook of the user's %s; the test picture %s; the stand-in listening on 127.0.0.1:%d: %s",
                    probe_hook_had ? "kept aside" : "not there", written ? "written" : "NOT written", probe_hook_port, listening ? "yes" : "NO");
            snprintf(probe_hook_js, sizeof(probe_hook_js),
                     "(function () { var b = document.querySelector('#shot-view .s-hook'), f = b && b.querySelector('input'); if (!f) return 'NOT asked again';"
                     " f.value = ' http://127.0.0.1:%d/api/webhooks/123456789/BPS5probeToken-AbCdEfGhIjKl_0123 ';"
                     " b.dispatchEvent(new Event('submit', {cancelable: true})); return 'asked again; the stand-in given, the box ' + (b.isConnected ? 'STILL there' : 'gone'); })()",
                     probe_hook_port);
          }, "");
          demo_ask(PAGE, "(function () { var v = document.createElement('div'); v.id = 'shot-view'; document.body.append(v);"
                   " var a = document.querySelector('#game-hub a.h-play'); if (!a) return 'no hub';"
                   " location.href = 'browserps5:shot-share?' + a.getAttribute('href').split('?')[1] + '=' + encodeURIComponent('file://" PROBE_SHOT "'); return 'asked'; })()",
                   panel, sizeof(panel));
          demo_sleep(1500);
          demo_ask(PAGE, "(function () { var b = document.querySelector('#shot-view .s-hook'), f = b && b.querySelector('input'); if (!f) return 'NO webhook box';"
                   " var t = b.querySelector('b').textContent.replace(/[^ -~]/g, '-'), k = b.querySelectorAll('button').length;"
                   " f.value = 'https://example.com/not-a-webhook'; b.dispatchEvent(new Event('submit', {cancelable: true}));"
                   " return 'the box [' + t + '] with ' + k + ' button(s), the field ' + (document.activeElement === f ? 'focused' : 'NOT focused') + '; a wrong address given'; })()",
                   panel, sizeof(panel));
          demo_main([](const char*) {
            POC_LOG("show: probe: discord: with no webhook kept: %s", panel);
          }, "");
          demo_sleep(1500);
          demo_ask(PAGE, probe_hook_js, panel, sizeof(panel));
          demo_main([](const char*) {
            POC_LOG("show: probe: discord: after the wrong address: %s", panel);
          }, "");
          for(int i = 0; i < 40 && !__atomic_load_n(&probe_hook_done, __ATOMIC_ACQUIRE); i++) {
            demo_sleep(250);
          }
          demo_sleep(1000);
          demo_main([](const char*) {
            struct stat st;
            char kept[SHARE_HOOK_MAX];
            const bool there = discord_hook_read(kept, sizeof(kept)) && !stat(DISCORD_HOOK, &st);
            POC_LOG("show: probe: discord: the stand-in got %s; the webhook %s%s (mode %o, spaces %s); the send %s",
                    probe_hook_saw, there ? "kept" : "NOT kept", there && strstr(kept, "127.0.0.1") ? " (the stand-in's)" : "",
                    there ? (unsigned)(st.st_mode & 0777) : 0u, there && !strchr(kept, ' ') ? "taken off" : "LEFT", share_busy ? "STILL busy" : "over");
            memset(kept, 0, sizeof(kept));
            char json[256];  // the phone's card (phone.c /api/discord): what it is told, a wrong address, the forget
            phone_discord_json(json, sizeof(json), 0);
            const int bad = phone_discord_set("https://example.com/not-a-webhook", 0);
            POC_LOG("show: probe: discord from the phone: the card is told %s (the token %s); a wrong address %s",
                    json, strstr(json, "BPS5probe") ? "SHOWN (WRONG)" : "not shown", bad ? "KEPT (WRONG)" : "refused");
            phone_discord_set("", 0);
            phone_discord_json(json, sizeof(json), 0);
            POC_LOG("show: probe: discord from the phone: forgotten, the card is told %s", json);
            unlink(PROBE_SHOT);
            unlink(DISCORD_HOOK);
            if(probe_hook_had) {
              discord_hook_write(probe_hook_keep);
            }
            memset(probe_hook_keep, 0, sizeof(probe_hook_keep));
            POC_LOG("show: probe: discord: the test picture and the stand-in's webhook removed%s", probe_hook_had ? ", the user's put back" : "");
          }, "");
          demo_ask(PAGE, "(function () { var v = document.getElementById('shot-view'); if (v) v.remove(); return 'closed'; })()", panel, sizeof(panel));
          demo_ask(PAGE, "(function () { var p = document.getElementById('game-hub'); if (p) p.remove(); return 'closed'; })()", panel, sizeof(panel));
          // 2026-11-16: "Your games", every game of the console (library_tell()), then the page of its last game.
          demo_ask(PAGE, "(function () { var a = document.querySelector('#games-box .g-lib'); if (!a) return 'no library button'; a.click(); return 'opened'; })()",
                   panel, sizeof(panel));
          demo_sleep(3500);
          demo_ask(PAGE, "(function () { var p = document.getElementById('game-lib'); if (!p) return 'no library';"
                   " var g = p.querySelectorAll('.l-game'), i = p.querySelectorAll('.l-game img'), n = p.querySelector('.l-title');"
                   " var az = p.querySelector('.l-tools a[data-value=az]'); if (az) az.click(); var a = p.querySelectorAll('.l-game .l-name');"
                   " var ps4 = p.querySelector('.l-tools a[data-value=ps4]'); if (ps4) ps4.click(); var four = p.querySelectorAll('.l-game').length;"
                   " var all = p.querySelector('.l-tools a[data-value=all]'); if (all) all.click();"
                   " var tr = p.querySelector('.l-tools a[data-value=trophies]'); if (tr) tr.click(); var pc = p.querySelectorAll('.l-game .l-pct'), s = p.querySelector('.l-sum');"
                   " var top = pc[0] ? pc[0].parentNode.title.replace(/[^ -~]/g, '-') + ' ' + pc[0].textContent.replace(/[^ -~]/g, '-') : 'none';"
                   " return (n ? n.textContent.replace(/[^ -~]/g, '-') : 'no title') + ': ' + g.length + ' game(s), ' + i.length + ' cover(s) asked for, by name the first '"
                   " + (a[0] ? a[0].textContent.replace(/[^ -~]/g, '-') : 'none') + ' and the last ' + (a.length ? a[a.length - 1].textContent.replace(/[^ -~]/g, '-') : 'none')"
                   " + ', ' + four + ' of PS4; ' + pc.length + ' with trophies, by trophies the first ' + top + '; ' + (s ? s.textContent.replace(/[^ -~]/g, '-') : 'no sum'); })()", panel, sizeof(panel));
          demo_main([](const char*) {
            POC_LOG("show: probe: your games: %s", panel);
            screenshot("your games");
          }, "");
          // (2026-11-16) the characters beyond ASCII in the games' names, by code point: some showed as boxes (no
          // font of the console has them), to know which
          demo_ask(PAGE, "(function () { var c = {}; document.querySelectorAll('#game-lib .l-game').forEach(function (e) {"
                   " for (var ch of e.textContent) { var k = ch.codePointAt(0); if (k > 126) c[k] = (c[k] || 0) + 1; } });"
                   " var l = Object.keys(c).map(Number).sort(function (a, b) { return a - b; });"
                   " return l.length + ' kind(s): ' + l.map(function (k) { return 'U+' + k.toString(16).toUpperCase() + ' x' + c[k]; }).join(', '); })()",
                   panel, sizeof(panel));
          demo_main([](const char*) {
            POC_LOG("show: probe: your games' names beyond ASCII: %s", panel);
          }, "");
          // Emoji (Noto Emoji in /user/homebrew/share/fonts/noto, 2026-11-16: before it every emoji was a box): a
          // line of them over the page, its picture, then gone. The PS5's own symbols (U+EFDC...) are no emoji.
          demo_ask(PAGE, "(function () { var d = document.createElement('div'); d.id = 'bps5-emoji';"
                   " d.style.cssText = 'position:fixed;left:40px;top:120px;z-index:99999;font-size:64px;line-height:1.3;padding:16px 24px;"
                   "background:#123;color:#fff;border-radius:16px';"
                   " d.textContent = 'Emoji: \\u{1F600} \\u{1F3AE} \\u{1F44D} \\u2764\\uFE0F \\u{1F680} \\u2605 \\u{1F6D2} | PS5: \\uEFDC \\uEFDD';"
                   " document.body.append(d); return 'shown'; })()", panel, sizeof(panel));
          demo_sleep(800);
          demo_main([](const char*) {
            POC_LOG("show: probe: a line of emoji: %s", panel);
            screenshot("emoji");
          }, "");
          demo_ask(PAGE, "(function () { var d = document.getElementById('bps5-emoji'); if (d) d.remove(); return 'gone'; })()", panel, sizeof(panel));
          demo_ask(PAGE, "(function () { var p = document.getElementById('game-lib'); var g = p && p.querySelectorAll('.l-game'); if (!g || !g.length) return 'no game';"
                   " g[g.length - 1].click(); return 'opened ' + g[g.length - 1].title.replace(/[^ -~]/g, '-'); })()", panel, sizeof(panel));
          demo_sleep(3000);
          demo_ask(PAGE, "(function () { var p = document.getElementById('game-hub'); if (!p) return 'no hub';"
                   " var n = p.querySelector('.h-name'), t = p.querySelector('.t-list');"
                   " return 'the page of ' + (n ? n.textContent.replace(/[^ -~]/g, '-') : '?') + ': ' + p.querySelectorAll('.t-row').length + ' trophy row(s), play button '"
                   " + (p.querySelector('a.h-play') ? 'there' : 'MISSING') + ', the list says: ' + (t ? t.textContent.replace(/[^ -~]/g, '-').slice(0, 60) : 'nothing'); })()",
                   panel, sizeof(panel));
          demo_main([](const char*) {
            POC_LOG("show: probe: a game of the library: %s", panel);
          }, "");
          demo_ask(PAGE, "(function () { var p = document.getElementById('game-hub'); if (p) p.remove(); var l = document.getElementById('game-lib'); if (l) l.remove(); return 'closed'; })()",
                   panel, sizeof(panel));
          // 2026-11-16: "Homebrew", the scene's tools and their newest versions (read by the page from GitHub).
          demo_ask(PAGE, "(function () { var a = document.querySelector('#players-box .p-homebrew'); if (!a) return 'no homebrew link'; a.click(); return 'opened'; })()",
                   panel, sizeof(panel));
          demo_sleep(8000);
          demo_ask(PAGE, "(function () { var p = document.getElementById('hb-panel'); if (!p) return 'no panel';"
                   " var items = p.querySelectorAll('.hb-item'), elf = p.querySelectorAll('.hb-file.elf'), n = p.querySelector('.hb-none');"
                   " return items.length + ' tool(s), ' + elf.length + ' .elf file(s), ' + p.querySelectorAll('.hb-new').length + ' marked new'"
                   " + (items[0] ? ', the first: ' + items[0].querySelector('.hb-top').textContent.replace(/[^ -~]/g, '-') : '')"
                   " + (n ? ', saying: ' + n.textContent.replace(/[^ -~]/g, '-').slice(0, 80) : '') + ', ' + p.querySelector('.hb-when').textContent.replace(/[^ -~]/g, '-')"
                   " + '; running here: ' + Array.prototype.map.call(p.querySelectorAll('.hb-on'), function (e) { return e.parentNode.querySelector('.hb-name').textContent + ' (' + e.textContent.replace(/[^ -~]/g, '-') + ')'; }).join(', '); })()",
                   panel, sizeof(panel));
          demo_main([](const char*) {
            POC_LOG("show: probe: homebrew: %s", panel);
            screenshot("homebrew");
          }, "");
          demo_ask(PAGE, "(function () { var p = document.getElementById('hb-panel'); if (p) p.remove(); return 'closed'; })()", panel, sizeof(panel));
          // 2026-11-15: the console's health (health.c): one reading in the log, then the panel as the page shows it.
          demo_main([](const char*) {
            console_health_t h;
            console_health_read(&h);
            POC_LOG("show: probe: the console's health: processor %d C, chip %d C, power GPU %d + CPU %d + memory %d mW, %d MHz, "
                    "the fan aims for %d C, on for %lld s, %d drive(s) (the first %s: %lld of %lld MiB free), address %s on %s (-1: no reading)",
                    h.cpu_c, h.soc_c, h.gpu_mw, h.cpu_mw, h.mem_mw, h.cpu_mhz, h.fan_c, h.uptime_s, h.disk_count,
                    h.disk_count ? h.disks[0].path : "none", h.disk_count ? h.disks[0].free >> 20 : 0LL,
                    h.disk_count ? h.disks[0].total >> 20 : 0LL, h.ip[0] ? "there" : "NONE", h.net);
          }, "");
          demo_ask(PAGE, "(function () { var c = document.querySelector('#players-box a.p-health'); if (!c) return 'no health link'; c.click(); return 'opened'; })()",
                   panel, sizeof(panel));
          demo_sleep(3500);
          demo_ask(PAGE, "(function () { var p = document.getElementById('health-panel'); if (!p) return 'no panel';"
                   " return p.querySelectorAll('.hl-tile').length + ' tile(s), ' + p.querySelectorAll('.hl-disk').length + ' drive(s): '"
                   " + [].map.call(p.querySelectorAll('.hl-tile'), function (x) { return x.textContent.replace(/[^ -~]/g, '-'); }).join(' | ').slice(0, 260); })()",
                   panel, sizeof(panel));
          demo_main([](const char*) {
            POC_LOG("show: probe: the console health panel: %s", panel);
            screenshot("the console health panel");
          }, "");
          demo_ask(PAGE, "(function () { var p = document.getElementById('health-panel'); if (p) p.querySelector('a.hl-close').click(); return 'closed'; })()", panel, sizeof(panel));
          // 2026-11-15: the controller as a keyboard (padkeys_toggle(), padkeys_input()): on example.com, turned on
          // as the menu does, the touch pad gives the controller to the page, right and cross go down and up,
          // and the page says which keys it saw; then all of it is undone.
          demo_main(demo_do_load, "http://example.com/");
          demo_sleep(3500);
          demo_ask(PAGE, "(function () { window.__k = []; ['keydown', 'keyup'].forEach(function (n) { addEventListener(n, function (e) {"
                   " __k.push((n === 'keydown' ? 'down ' : 'up ') + (e.key === ' ' ? 'Space' : e.key)); }, true); }); return 'listening'; })()", panel, sizeof(panel));
          demo_main([](const char*) {
            padkeys_toggle();  // arrow keys on this site
            demo_event(INPUT_TOUCHPAD, 0, 0, 0);
          }, "");
          demo_sleep(700);
          demo_main([](const char*) {
            input_event_t e = {INPUT_PAD, 0, 0, 0, 0, (int)(INPUT_PAD_RIGHT | INPUT_PAD_CROSS)};
            on_input(&e);
          }, "");
          demo_sleep(300);
          demo_main([](const char*) {
            input_event_t e = {INPUT_PAD, 0.9f, 0, 0, 0, 0};  // the buttons let go, the stick to the right
            on_input(&e);
          }, "");
          demo_sleep(300);
          demo_main([](const char*) {
            input_event_t e = {INPUT_PAD, 0, 0, 0, 0, 0};
            on_input(&e);
          }, "");
          demo_sleep(400);
          demo_ask(PAGE, "(window.__k || ['no list']).join(', ')", panel, sizeof(panel));
          demo_main([](const char*) {
            // (right and cross down; then the buttons let go with the stick to the right: only cross goes up, the
            // direction stays held; then the stick lets go)
            const bool right = !strcmp(panel, "down ArrowRight, down Space, up Space, up ArrowRight");
            POC_LOG("show: probe: the controller as a keyboard (game mode %d): the page saw [%s]: %s", app.game, panel,
                    right ? "right" : "NOT what was pressed");
            // the controller back to the cursor, as the user does it (on_input() queues the press: it is
            // taken after this, so the keys are turned off in a step of their own, or the press would come
            // with game mode already off and open the tabs' switcher, as in the run of 2026-11-16 11:27)
            demo_event(INPUT_TOUCHPAD, 0, 0, 0);
          }, "");
          demo_sleep(500);
          demo_main([](const char*) {
            const int game = app.game, switcher = app.switcher_open;
            padkeys_toggle();                     // W A S D
            padkeys_toggle();                     // off: the site is off the list again
            POC_LOG("show: probe: the controller as a keyboard: undone (the touch pad gave game mode %d, switcher %d; now game mode %d, %d site(s) on the list)",
                    game, switcher, app.game, padkeys_count);
          }, "");
          // (2026-11-16: the steps after this one are the start page's: back to it, or they run on example.com and
          // the page refuses them, as in the run of 11:24)
          {
            const unsigned before = demo_loads();
            demo_main(demo_do_start, "");
            demo_wait_load(before, 15000);
            demo_sleep(1500);
          }
          demo_ask(PAGE, "(function () { location.href = 'browserps5:cheat?CUSA01765_01.00.json'; return 'asked'; })()", said, sizeof(said));
          demo_sleep(8000);
          demo_main([](const char*) {
            struct stat st;
            const bool there = cheat_last_path[0] && !stat(cheat_last_path, &st);
            POC_LOG("show: probe: a cheat file asked for from the start page: %s (%lld bytes at %s, saved %u -> %u)",
                    there && st.st_size > 20 ? "saved" : "NOT saved", there ? (long long)st.st_size : 0LL,
                    cheat_last_path[0] ? cheat_last_path : "nowhere", saved_before, cheats_saved);
            bar_update(0);
            POC_LOG("show: probe: after it the bar has %s", strstr(app.bar_state, "PS5_Cheats") ? "the file's address (WRONG)" : "the page's own address (right)");
            if(there) {
              POC_LOG("show: probe: the test's cheat file is removed again (%s)", unlink(cheat_last_path) ? "FAILED" : "gone");
            }
            if(download_t* gone = download_by_id(cheat_last_id)) {
              unlink(gone->partial);
              if(gone->ref) {
                WKRelease(gone->ref);
              }
              memset(gone, 0, sizeof(*gone));
              downloads_changed = true;
            }
            if(state_before < 2) {  // the folders this made
              rmdir(CHEATS_DIR "/json");
              if(state_before == 1) {
                rmdir(CHEATS_DIR);
              }
            }
            games_stale();
          }, "");
          demo_ask(PAGE, "(function () { location.href = 'browserps5:cheat?..%2F..%2Fetc%2Fpasswd.json'; return 'asked'; })()", said, sizeof(said));
          demo_sleep(1200);
          demo_main([](const char*) {
            POC_LOG("show: probe: a name that is no cheat file's, asked for the same way: %s (saved %u -> %u)",
                    cheats_saved == saved_before + (cheat_last_path[0] ? 1 : 0) ? "not taken" : "WRONG", saved_before, cheats_saved);
          }, "");
          // 2026-11-15: the game patches for etaHEN's plugin (patch_ask(), patch_set(), patches_tell()). The page's
          // number for a patch against the plugin's own sum; the panel of a game that has patches; one switch
          // written, told to the page, and taken away; the game's file fetched under the name the plugin reads,
          // and removed; a name that is no patch file's. Nothing of the user's is touched: with that file or that
          // switch already on the console, that part is left out.
          {
#define PROBE_PATCH_FILE   PATCHES_DIR "/xml/CUSA00900.xml"
#define PROBE_PATCH_SWITCH PATCHES_SETTINGS "/0x00000000b5505e57.txt"
            static char sum[64], told[400];
            static bool had_file, had_switch, no_dir, no_kind, no_settings;
            static unsigned before;
            demo_ask(PAGE, "window.bps5PatchHash ? bps5PatchHash('Bloodborne\\u00e960 FPS01.09/data/etaHEN/patches/xml/CUSA00900.xmleboot.bin') : 'no function'",
                     sum, sizeof(sum));
            demo_main([](const char*) {
              char mine[24];
              struct stat st;
              snprintf(mine, sizeof(mine), "%016llx",
                       (unsigned long long)patch_sum("Bloodborne\xc3\xa9" "60 FPS01.09/data/etaHEN/patches/xml/CUSA00900.xmleboot.bin"));
              POC_LOG("show: probe: a patch's number, the page's %s and the plugin's sum here %s: %s", sum, mine,
                      strcmp(sum, mine) ? "NOT the same" : "the same");
              had_file = !stat(PROBE_PATCH_FILE, &st);
              had_switch = !stat(PROBE_PATCH_SWITCH, &st);
              no_dir = stat(PATCHES_DIR, &st) != 0;
              no_kind = stat(PATCHES_DIR "/xml", &st) != 0;
              no_settings = stat(PATCHES_SETTINGS, &st) != 0;
              POC_LOG("show: probe: patches on this console before: %s%s%s; the test's file %s, the test's switch %s",
                      no_dir ? "no patches folder" : PATCHES_DIR " is there", no_kind ? ", no xml folder" : "", no_settings ? ", no settings folder" : "",
                      had_file ? "IS THE USER'S (left alone)" : "not there", had_switch ? "IS THERE (left alone)" : "not there");
            }, "");
            // the panel of a game that has patches in the list (a PS4 game: whatever this console's games are)
            demo_ask(PAGE, "(function () { if (!window.bps5PatchesFor) return 'no function'; bps5PatchesFor('CUSA00900', true, '01.09', 'Bloodborne'); return 'opened'; })()",
                     told, sizeof(told));
            demo_sleep(6000);
            demo_ask(PAGE, "(function () { var p = document.getElementById('patches-panel'); if (!p) return 'no panel';"
                           " var rows = p.querySelectorAll('.p-row'), on = p.querySelectorAll('.p-switch.on'), yours = p.querySelectorAll('.p-name b');"
                           " return rows.length + ' patch row(s), ' + on.length + ' on, ' + yours.length + ' of the version given; first: '"
                           " + (rows.length ? rows[0].querySelector('.p-name').firstChild.textContent + ' [' + rows[0].querySelector('.p-switch').textContent + ']'"
                           " : p.querySelector('.c-list').textContent.slice(0, 90)); })()", told, sizeof(told));
            demo_main([](const char*) {
              POC_LOG("show: probe: the patches panel of a game in the list (%s, opened by its id): %s", "CUSA00900", told);
              screenshot("the patches panel");
            }, "");
            demo_ask(PAGE, "(function () { var p = document.getElementById('patches-panel'); if (p) p.remove(); window.__p = '';"
                           " var a = window.bps5PatchSet, b = window.bps5Patches;"
                           " window.bps5PatchSet = function (id, n, on, saved) { window.__p += 'set ' + id + ' ' + n + ' ' + on + '; '; if (a) a(id, n, on, saved); };"
                           " window.bps5Patches = function (id, s) { window.__p += 'state ' + id + ' on=' + (s && s.on) + ' saved=' + (s && s.saved) + '; '; if (b) b(id, s); };"
                           " return 'listening'; })()", told, sizeof(told));
            demo_main([](const char*) {
              before = patches_set_count;
              if(had_switch) {
                POC_LOG("show: probe: the test's switch is on the console already: the switch test is left out");
              }
            }, "");
            demo_ask(PAGE, "(function () { location.href = 'browserps5:patches?CUSA00900=xml=00000000b5505e57,00000000b5505e58'; return 'asked'; })()", told, sizeof(told));
            demo_sleep(600);
            if(!had_switch) {  // (set by the step before, which has run by now: these steps wait for one another)
              demo_ask(PAGE, "(function () { location.href = 'browserps5:patch-set?CUSA00900=xml=00000000b5505e57=1'; return 'asked'; })()", told, sizeof(told));
            }
            demo_sleep(600);
            demo_ask(PAGE, "(function () { location.href = 'browserps5:patches?CUSA00900=xml=00000000b5505e57,00000000b5505e58'; return 'asked'; })()", told, sizeof(told));
            demo_sleep(600);
            demo_ask(PAGE, "window.__p || 'nothing told'", told, sizeof(told));
            demo_main([](const char*) {
              struct stat st;
              char first = 0;
              FILE* f = fopen(PROBE_PATCH_SWITCH, "rb");
              if(f) {
                if(fread(&first, 1, 1, f) != 1) {
                  first = 0;
                }
                fclose(f);
              }
              const bool right = had_switch || (strstr(told, "state CUSA00900 on=00") && strstr(told, "set CUSA00900 00000000b5505e57 true") &&
                                                strstr(told, "state CUSA00900 on=10") && first == '1' && patches_set_count == before + 1);
              POC_LOG("show: probe: a patch's switch, written and told to the page: %s (the file says '%c', written %u -> %u; the page was told: %s)",
                      right ? "right" : "WRONG", first ? first : '-', before, patches_set_count, told);
              if(!had_switch && !stat(PROBE_PATCH_SWITCH, &st)) {
                POC_LOG("show: probe: the test's switch is removed again (%s)", unlink(PROBE_PATCH_SWITCH) ? "FAILED" : "gone");
              }
              if(no_settings) {
                rmdir(PATCHES_SETTINGS);
              }
              before = cheats_saved;
              cheat_last_path[0] = 0;
            }, "");
            demo_ask(PAGE, "(function () { location.href = 'browserps5:patch?CUSA00900=xml/..%2F..%2Fetc%2Fpasswd.xml'; return 'asked'; })()", told, sizeof(told));
            demo_sleep(1200);
            demo_main([](const char*) {
              POC_LOG("show: probe: a name that is no patch file's: %s (saved %u -> %u)", cheats_saved == before && !cheat_last_path[0] ? "not taken" : "WRONG",
                      before, cheats_saved);
            }, "");
            if(had_file) {
              demo_main([](const char*) { POC_LOG("show: probe: the user has this game's patch file: fetching one is left out"); }, "");
            } else {
              demo_ask(PAGE, "(function () { location.href = 'browserps5:patch?CUSA00900=xml/Bloodborne-Orbis.xml'; return 'asked'; })()", told, sizeof(told));
            }
            demo_sleep(8000);
            demo_main([](const char*) {
              if(had_file) {
                return;
              }
              struct stat st;
              const bool there = !strcmp(cheat_last_path, PROBE_PATCH_FILE) && !stat(cheat_last_path, &st);
              POC_LOG("show: probe: a game's patch file asked for from the start page: %s (%lld bytes at %s, saved %u -> %u)",
                      there && st.st_size > 200 ? "saved under the name the plugin reads" : "NOT saved", there ? (long long)st.st_size : 0LL,
                      cheat_last_path[0] ? cheat_last_path : "nowhere", before, cheats_saved);
              if(cheat_last_path[0]) {
                POC_LOG("show: probe: the test's patch file is removed again (%s)", unlink(cheat_last_path) ? "FAILED" : "gone");
                if(download_t* gone = download_by_id(cheat_last_id)) {
                  unlink(gone->partial);
                  if(gone->ref) {
                    WKRelease(gone->ref);
                  }
                  memset(gone, 0, sizeof(*gone));
                  downloads_changed = true;
                }
              }
              if(no_kind) {  // the folders this made
                rmdir(PATCHES_DIR "/xml");
              }
              if(no_dir) {
                rmdir(PATCHES_DIR);
              }
              POC_LOG("show: probe: patches on this console after: %s", stat(PATCHES_DIR, &st) ? "no patches folder" : PATCHES_DIR " is there");
              games_stale();
            }, "");
          }
        }
      }
    }
  }
#endif
  // The PDF reader (pdf_open_path()), with a PDF of one page written here: nothing is downloaded.
  if(pdf_reader()) {
    static const char pdf[] =
      "%PDF-1.4\n1 0 obj<</Type/Catalog/Pages 2 0 R>>endobj\n2 0 obj<</Type/Pages/Kids[3 0 R]/Count 1>>endobj\n"
      "3 0 obj<</Type/Page/Parent 2 0 R/MediaBox[0 0 300 144]/Contents 4 0 R/Resources<</Font<</F1 5 0 R>>>>>>endobj\n"
      "4 0 obj<</Length 44>>stream\nBT /F1 24 Tf 40 60 Td (BrowserPS5) Tj ET\nendstream endobj\n"
      "5 0 obj<</Type/Font/Subtype/Type1/BaseFont/Helvetica>>endobj\ntrailer<</Root 1 0 R/Size 6>>\n%%EOF\n";
    static char test[1100];
    snprintf(test, sizeof(test), "%s/browserps5-show-test.pdf", settings.downloads_dir);
    FILE* f = fopen(test, "w");
    if(f) {
      fwrite(pdf, 1, sizeof(pdf) - 1, f);
      fclose(f);
      before = demo_loads();
      demo_main([](const char*) { pdf_open_path(test); }, "");
      demo_wait_load(before, 15000);
      demo_sleep(9000);
      demo_main([](const char*) { screenshot("show"); }, "");
      demo_ask(PAGE,
               "(function () { var a = window.PDFViewerApplication, v = a && a.pdfViewer, p = document.querySelector('.page'), c = document.getElementById('viewerContainer');"
               " return 'view: scale ' + (v ? v.currentScale : '?') + ', pages in the view ' + (v ? v.pagesCount : '?') + ', .page ' + document.querySelectorAll('.page').length"
               " + (p ? ' ' + p.offsetWidth + 'x' + p.offsetHeight + ' loaded ' + p.getAttribute('data-loaded') : '') + ', container ' + (c ? c.clientWidth + 'x' + c.clientHeight : 'none')"
               " + ', number field ' + (document.getElementById('numPages') ? document.getElementById('numPages').textContent : '?') + ', initialized ' + (a ? a.initialized : '?') + ', window ' + innerWidth + 'x' + innerHeight; })()",
               answer, sizeof(answer));
      POC_LOG("show: probe: pdf %s", answer);
      demo_ask(PAGE,
               "(function () { var a = window.PDFViewerApplication, errs = ((window.__bps5diag || {errors: ['no diag']}).errors.join(' | ') || 'none');"
               " if (!a) return 'no PDFViewerApplication, title ' + document.title + ', scripts ' + document.scripts.length + ', module support ' + ('noModule' in HTMLScriptElement.prototype) + ', errors: ' + errs;"
               " return 'pages ' + a.pagesCount + ', document ' + (a.pdfDocument ? 'open' : 'none') + ', title ' + document.title + ', text spans ' + document.querySelectorAll('.textLayer span').length"
               " + ', canvases ' + document.querySelectorAll('canvas').length + ', says ' + (document.querySelector('.textLayer') ? document.querySelector('.textLayer').textContent.slice(0, 20) : '')"
               " + ', errors: ' + ((window.__bps5diag || {errors: []}).errors.join(' | ') || 'none'); })()",
               answer, sizeof(answer));
      POC_LOG("show: probe: pdf %s", answer);
      unlink(test);
    }
  } else {
    POC_LOG("show: probe: pdf: no reader installed (%s)", PDF_VIEWER);
  }
  POC_RESULT(1, "show: probe done in %.0f s; Sair from the menu", (poc_now_ms() - app.start_ms) / 1e3);
  demo_main(demo_do_ui, "menu-do?quit");
  return;
#endif
#ifdef SHOW_PDF
  // make SHOW=1 SHOW_YT=1 SHOW_PDF=1 (2026-11-12): a real PDF from the Internet, as a
  // user opens one: the tab goes to its address, the browser downloads it and shows it in its reader
  // (pdf_wanted, pdf_open()). A paper of 15 pages from arXiv. What the reader says of it, a picture of the first
  // page and one further down. The file is REMOVED at the end, and its line.
  {
    char said[700];
    static int line;
    static char back[40];
    snprintf(back, sizeof(back), "ask_downloads%%3D%d", settings.ask_downloads ? 1 : 0);
    demo_main(demo_do_set, "ask_downloads%3D0");
    demo_sleep(600);
    int newest = 0;
    for(const download_t& x : downloads) {
      newest = x.id > newest ? x.id : newest;
    }
    const double t0 = poc_now_ms();
    demo_main(demo_do_load, "https://arxiv.org/pdf/1706.03762");
    static const char state_js[] =
      "(function () { var a = window.PDFViewerApplication, v = a && a.pdfViewer;"
      " if (!a) return 'no reader yet: ' + location.href.slice(0, 60);"
      " return 'pages ' + a.pagesCount + ', scale ' + (v ? v.currentScale.toFixed(2) : '?') + ', page ' + (v ? v.currentPageNumber : '?') + ', document ' + (a.pdfDocument ? 'open' : 'none')"
      " + ', drawn ' + document.querySelectorAll('.page[data-loaded=\"true\"]').length + ', text spans ' + document.querySelectorAll('.textLayer span').length"
      " + ', title [' + document.title.slice(0, 50) + '], says [' + (document.querySelector('.textLayer') ? document.querySelector('.textLayer').textContent.replace(/[^ -~]+/g, ' ').slice(0, 60) : '') + ']'"
      " + ', errors: ' + ((window.__bps5diag || {errors: []}).errors.join(' | ') || 'none'); })()";
    bool open = false;
    for(int i = 0; i < 20 && !open; i++) {  // 40 s for the download and the reader
      demo_sleep(2000);
      demo_ask(PAGE, state_js, said, sizeof(said));
      open = strstr(said, "document open") != 0;
    }
    POC_LOG("show: pdf: %.1f s after the address was asked for: %s", (poc_now_ms() - t0) / 1e3, said);
    demo_sleep(4000);
    demo_ask(PAGE, state_js, said, sizeof(said));
    POC_LOG("show: pdf: %s", said);
    demo_main([](const char*) { screenshot("pdf first page"); }, "");
    demo_ask(PAGE, "(function () { var a = window.PDFViewerApplication; if (!a || !a.pdfViewer) return 'no reader'; a.pdfViewer.currentPageNumber = 3; return 'to page 3'; })()",
             said, sizeof(said));
    demo_sleep(5000);
    demo_ask(PAGE, state_js, said, sizeof(said));
    POC_LOG("show: pdf: after going to page 3: %s", said);
    demo_main([](const char*) { screenshot("pdf page 3"); }, "");
    for(download_t& x : downloads) {
      if(x.id > newest) {
        POC_LOG("show: pdf: its download: %s, %lld bytes, %s, in %s", download_states[x.state], x.total, x.name, x.path);
        line = x.id;
      }
    }
    demo_main([](const char*) {  // the test's file and its line go
      download_t* gone = download_by_id(line);
      if(gone) {
        if(gone->path[0] == '/') {
          POC_LOG("show: pdf: removed %s (%s)", gone->path, unlink(gone->path) ? "FAILED" : "gone");
        }
        unlink(gone->partial);
        if(gone->ref) {
          WKRelease(gone->ref);
        }
        memset(gone, 0, sizeof(*gone));
        downloads_changed = true;
      }
    }, "");
    demo_main(demo_do_set, back);
    demo_sleep(800);
    POC_RESULT(1, "show: pdf done in %.0f s; Sair from the menu", (poc_now_ms() - app.start_ms) / 1e3);
    demo_main(demo_do_ui, "menu-do?quit");
    return;
  }
#endif
#ifdef SHOW_DL
  // make SHOW=1 SHOW_YT=1 SHOW_DL=1 (2026-11-12): real
  // downloads of two test files of 1 GB, one from a plain HTTP/1.1 server and one over HTTPS (HTTP/2), into the
  // downloads' folder: how long each takes, whether it went in several parts (multi_*), its size and a sum of
  // its bytes (FNV-1a, to compare a run in parts with one without: /data/homebrew/PPSA97024/no-multipart).
  // Each file is REMOVED as soon as it is measured, and its line with it. "Ask where to save" is off for the
  // run and put back. Then Sair.
  {
#ifdef SHOW_DL_SMALL  // one file of 100 MB (parts of 25 MB): a minute
    static const char* const files[] = {"http://speedtest.belwue.net/100M"};
#elif defined(SHOW_DL_DROP)  // the test's own server's file, with the line failing twice under it
    static const char* const files[] = {"http://127.0.0.1:8111/bps5-test.bin"};
#elif defined(SHOW_DL_CANCEL)  // 1 GB, cancelled by the user after 20 s: what is left of it?
    static const char* const files[] = {"http://speedtest.belwue.net/1G"};
#elif defined(SHOW_DL_BIG)  // one file of 10 GiB (more than 32 bits of size)
    static const char* const files[] = {"http://speedtest.belwue.net/10G"};
#else
    static const char* const files[] = {"http://speedtest.belwue.net/1G", "https://proof.ovh.net/files/1Gb.dat"};
#endif
    static char back[40];
    static int line;
    snprintf(back, sizeof(back), "ask_downloads%%3D%d", settings.ask_downloads ? 1 : 0);
    demo_main(demo_do_set, "ask_downloads%3D0");
    demo_sleep(600);
    POC_LOG("show: dl: several parts are %s for this run", access(MULTI_OFF_FILE, F_OK) ? "ON" : "OFF (the no-multipart marker)");
#ifdef SHOW_DL_DROP
#if defined(SHOW_DL_KEEP) && (SHOW_DL_KEEP == 1 || SHOW_DL_KEEP == 4)  // slowly: the browser is closed 70 s in, with the download unfinished
    drop_rate = 1LL << 20;
    multi_test_fall = SHOW_DL_KEEP == 4;  // 4: as a fall would (the map of every 15 s is what the next run has)
#endif
#ifdef SHOW_DL_DROP_SOLO  // a server that gives one connection at a time
    drop_one = true;
    drop_rate = 12LL << 20;
#endif
#ifdef SHOW_DL_DROP_OFF  // by one connection from the start, as with "Transferências rápidas" off
    multi_test_off = true;
    drop_rate = 12LL << 20;
#endif
#ifdef SHOW_DL_DUO  // two at once, quickly
    drop_rate = 6LL << 20;
#endif
    drop_clean();
    POC_LOG("show: dl: drop: the test's server on 127.0.0.1:%d %s (%lld bytes, %lld a second each connection%s)", DROP_PORT,
            drop_server() ? "listens" : "DID NOT START", (long long)DROP_SIZE, drop_rate,
            drop_one ? ", one connection at a time" : "");
#define DL_SAY(i) true
#else
#define DL_SAY(i) ((i) % ((i) < 60 ? 5 : 15) == 4)
#endif
#ifdef SHOW_DL_DUO
    {  // 2026-11-14: TWO downloads in parts at once, begun from the same tab, of the test's own server. What the PC's
       // simulation found (tests-host, run_duo()): the pieces of one took the place of the other's in the tab
       // (multi_page_asked() now has them wait their turn). Here with the real engine: both files, byte for byte,
       // how many connections each had, and how many of its pieces were never begun ("failures").
      static const char* const two[] = {"http://127.0.0.1:8111/bps5-test.bin", "http://127.0.0.1:8111/bps5-test.bin?second"};
      static int ids[2];
      int newest = 0, most[2] = {0, 0}, failures[2] = {0, 0};
      bool in_parts[2] = {false, false};
      for(const download_t& x : downloads) {
        newest = x.id > newest ? x.id : newest;
      }
      const double t0 = poc_now_ms();
      demo_main(demo_do_load, two[0]);
      demo_sleep(2500);  // (the first is a download by now: the tab is free for the second address)
      demo_main(demo_do_load, two[1]);
      for(int i = 0; i < 300; i++) {  // ten minutes at most
        int done = 0;
        demo_sleep(2000);
        for(const download_t& x : downloads) {
          if(x.id > newest && x.id != ids[0] && x.id != ids[1]) {
            ids[ids[0] ? 1 : 0] = x.id;
          }
        }
        for(int k = 0; k < 2; k++) {
          const download_t* d = ids[k] ? download_by_id(ids[k]) : 0;
          const multi_t* m = d ? multi_of(d->id) : 0;
          if(m) {
            in_parts[k] = in_parts[k] || !m->solo;
            most[k] = multi_workers(m) > most[k] ? multi_workers(m) : most[k];
            failures[k] = m->failures;
          }
          done += d && d->state >= DL_DONE;
          if(d && (i % 5 == 4 || d->state != DL_RUNNING)) {
            POC_LOG("show: dl: duo: file %d at %.0f s: %s, %lld of %lld bytes, %d connection(s) now, %d failure(s), %s", k + 1,
                    (poc_now_ms() - t0) / 1e3, download_states[d->state], d->received, d->total, m ? multi_workers(m) : 0, failures[k],
                    m && !m->solo ? "in parts" : m ? "one part, kept" : "one part");
          }
        }
        if(done == 2) {
          break;
        }
      }
      for(int k = 0; k < 2; k++) {
        const download_t* d = ids[k] ? download_by_id(ids[k]) : 0;
        if(d && d->state == DL_DONE) {
          long long wrong = -1;
          const long long read = drop_check(d->path, &wrong);
          POC_LOG("show: dl: duo: file %d %s (%lld of %lld bytes read, first wrong byte at %lld); %s, up to %d connections, %d of its pieces never begun",
                  k + 1, wrong < 0 && read == DROP_SIZE ? "IS what the server made" : "is NOT what the server made", read, (long long)DROP_SIZE, wrong,
                  in_parts[k] ? "IN PARTS" : "in one part", most[k], failures[k]);
        } else {
          POC_LOG("show: dl: duo: file %d did NOT come through: %s", k + 1, d ? download_states[d->state] : "it never began");
        }
      }
      POC_LOG("show: dl: duo: the two took %.0f s; the server saw %d connections, %d for ranges, %d refused", (poc_now_ms() - t0) / 1e3,
              drop_connections, drop_ranges, drop_refused);
      demo_main([](const char*) {  // the test's files and their lines go
        for(int k = 0; k < 2; k++) {
          download_t* gone = ids[k] ? download_by_id(ids[k]) : 0;
          if(gone) {
            if(gone->state == DL_RUNNING) {
              download_cancel(gone);
            }
            if(gone->path[0] == '/') {
              POC_LOG("show: dl: removed %s (%s)", gone->path, unlink(gone->path) ? "FAILED" : "gone");
            }
            unlink(gone->partial);
            if(gone->ref) {
              WKRelease(gone->ref);
            }
            memset(gone, 0, sizeof(*gone));
            downloads_changed = true;
          }
        }
      }, "");
      demo_sleep(1500);
      drop_clean();
      demo_main(demo_do_set, back);
      demo_sleep(600);
      POC_RESULT(1, "show: dl: duo done in %.0f s; Sair from the menu", (poc_now_ms() - app.start_ms) / 1e3);
      demo_main(demo_do_ui, "menu-do?quit");
      return;
    }
#endif
#if defined(SHOW_DL_KEEP) && SHOW_DL_KEEP == 3
    {  // after a run 1: the list's line of the download left unfinished, and its "Tirar da lista" (pressed twice)
      static char said[1200];
      static int kept_id;
      int maps = 0, partials = 0;
      char near[600];
      for(const download_t& x : downloads) {
        if(x.id && x.kept) {
          kept_id = x.id;
        }
      }
      POC_LOG("show: dl: keep: %s", kept_id ? "a download was kept from the run before" : "NO download was kept from the run before");
      demo_sleep(6000);  // (the bar's notice of it comes 7 s into the session)
      demo_main([](const char*) { screenshot("kept: the notice"); }, "");
      demo_main(demo_do_settings, "transferencias");
      demo_sleep(3000);
      demo_ask(PAGE,
               "(function () { var rows = document.querySelectorAll('#downloads .item'), out = [];"
               " rows.forEach(function (r) { var b = []; r.querySelectorAll('button').forEach(function (x) { b.push(x.title); });"
               " var t = r.querySelector('.text'); out.push((t ? t.innerText.split(String.fromCharCode(10)).join(' | ') : '?') + ' [' + b.join(', ') + ']'); });"
               " if (rows[0]) rows[0].scrollIntoView({block: 'center'});"
               " return rows.length + ' line(s): ' + out.join(' ;; ').slice(0, 900); })()",
               said, sizeof(said));
      POC_LOG("show: dl: keep: the list: %s", said);
      demo_sleep(700);
      demo_main([](const char*) { screenshot("kept: the list"); }, "");
      demo_ask(PAGE,
               "(function () { var x = document.querySelectorAll('#downloads .item button.ib.x'); if (!x.length) return 'no button';"
               " var b = x[x.length - 1], t = b.title; b.click(); b.click(); return 'pressed twice: ' + t; })()",
               said, sizeof(said));
      demo_sleep(1500);
      snprintf(near, sizeof(near), "%s/" PARTIAL_NEAR, settings.downloads_dir);
      const char* const places[] = {PARTIAL_DIR, near};
      for(const char* place : places) {
        if(DIR* dir = opendir(place)) {
          for(struct dirent* e; (e = readdir(dir));) {
            maps += name_ends_with(e->d_name, ".map");
            partials += name_ends_with(e->d_name, ".multi") || name_ends_with(e->d_name, ".part");
          }
          closedir(dir);
        }
      }
      POC_LOG("show: dl: keep: %s; its line is %s; left behind: %d map(s), %d partial file(s) (0 and 0: nothing)", said,
              download_by_id(kept_id) ? "STILL THERE" : "gone", maps, partials);
      demo_main([](const char*) {  // whatever the button did: nothing of the test stays
        download_t* gone = download_by_id(kept_id);
        if(gone && gone->kept) {
          multi_kept_forget(gone);
          memset(gone, 0, sizeof(*gone));
          downloads_changed = true;
        }
      }, "");
      demo_sleep(500);
      drop_clean();
      demo_main(demo_do_set, back);
      demo_sleep(600);
      POC_RESULT(1, "show: dl: keep: run 3 done in %.0f s; Sair from the menu", (poc_now_ms() - app.start_ms) / 1e3);
      demo_main(demo_do_ui, "menu-do?quit");
      return;
    }
#endif
#if defined(SHOW_DL_KEEP) && SHOW_DL_KEEP == 2
    {  // the second run: the download the first one left unfinished goes on from where it was
      static int kept_id;
      download_t* k = 0;
      for(download_t& x : downloads) {
        if(x.id && x.kept) {
          k = &x;
        }
      }
      if(!k) {
        POC_LOG("show: dl: keep: NO download was kept from the run before");
      } else {
        const double t0 = poc_now_ms();
        const long long before = k->received;
        long long wrong = -1;
        POC_LOG("show: dl: keep: kept from the run before: %s for %s, %lld of %lld bytes in, %s", k->name, k->dir, k->received,
                k->total, download_states[k->state]);
        kept_id = k->id;
        demo_main([](const char*) { download_do("retry", kept_id); }, "");
        demo_sleep(1500);
        for(int i = 0; i < 150 && k->state < DL_DONE; i++) {
          const multi_t* m = multi_of(k->id);
          POC_LOG("show: dl: keep at %.0f s: %s, %lld of %lld bytes, %d connections", (poc_now_ms() - t0) / 1e3,
                  download_states[k->state], k->received, k->total, m ? multi_workers(m) : 0);
          demo_sleep(2000);
        }
        const long long read = k->state == DL_DONE ? drop_check(k->path, &wrong) : 0;
        POC_LOG("show: dl: keep: %s %.1f s after going on from %lld bytes; the file %s (%lld of %lld bytes read, first wrong byte at %lld); the server saw %d connections, %d for ranges",
                download_states[k->state], (poc_now_ms() - t0) / 1e3, before,
                k->state == DL_DONE && wrong < 0 && read == DROP_SIZE ? "IS what the server made" : "is NOT what the server made",
                read, (long long)DROP_SIZE, wrong, drop_connections, drop_ranges);
        demo_main([](const char*) {  // nothing of the test stays
          download_t* gone = download_by_id(kept_id);
          if(gone) {
            if(gone->kept) {
              multi_kept_forget(gone);
            }
            if(gone->state == DL_RUNNING) {
              download_cancel(gone);
            }
            if(gone->path[0] == '/') {
              POC_LOG("show: dl: keep: removed %s (%s)", gone->path, unlink(gone->path) ? "FAILED" : "gone");
            }
            if(gone->ref) {
              WKRelease(gone->ref);
            }
            memset(gone, 0, sizeof(*gone));
            downloads_changed = true;
          }
        }, "");
        demo_sleep(1500);
      }
      {
        int maps = 0, partials = 0;
        char near[600];
        snprintf(near, sizeof(near), "%s/" PARTIAL_NEAR, settings.downloads_dir);
        const char* const places[] = {PARTIAL_DIR, near};
        for(const char* place : places) {
          if(DIR* dir = opendir(place)) {
            for(struct dirent* e; (e = readdir(dir));) {
              maps += name_ends_with(e->d_name, ".map");
              partials += name_ends_with(e->d_name, ".multi") || name_ends_with(e->d_name, ".part");
            }
            closedir(dir);
          }
        }
        POC_LOG("show: dl: keep: left behind: %d map(s), %d partial file(s) (0 and 0: nothing)", maps, partials);
      }
      drop_clean();
      demo_main(demo_do_set, back);
      demo_sleep(600);
      POC_RESULT(1, "show: dl: keep: run 2 done in %.0f s; Sair from the menu", (poc_now_ms() - app.start_ms) / 1e3);
      demo_main(demo_do_ui, "menu-do?quit");
      return;
    }
#endif
    {
      struct statvfs room;
      if(!statvfs(settings.downloads_dir, &room)) {
        POC_LOG("show: dl: %s has %.1f GiB free", settings.downloads_dir, (double)room.f_bavail * room.f_frsize / (1 << 30));
      }
    }
    static unsigned crc_table[256];
    for(unsigned n = 0; n < 256; n++) {  // CRC-32 (zlib's), to compare with the same file taken on the PC
      unsigned c = n;
      for(int k = 0; k < 8; k++) {
        c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
      }
      crc_table[n] = c;
    }
    for(const char* file : files) {
      int newest = 0;
      for(const download_t& x : downloads) {
        newest = x.id > newest ? x.id : newest;
      }
      demo_main(demo_do_load, file);
      const double t0 = poc_now_ms();
      download_t* d = 0;
      bool parts = false;
      for(int i = 0; i < 900; i++) {  // half an hour at most
        demo_sleep(2000);
        for(download_t& x : downloads) {
          if(!d && x.id > newest) {
            d = &x;
          }
        }
        if(!d) {
          continue;
        }
        const multi_t* m = multi_of(d->id);
        parts = parts || (m && !m->solo);
#ifdef SHOW_DL_DROP
        {  // the line fails twice: cut for 6 s (connections reset, new ones too), later quiet for 26 s
#ifdef SHOW_DL_KEEP  // the line never fails: the browser is closed in the middle instead
          const int mode = 0;
#elif defined(SHOW_DL_DROP_QUIET)  // quiet only, from 16 s: for 26 s; for 70 s by one connection (its own is dropped after 60)
          const int mode = d->state != DL_RUNNING ? 0 : i >= 7 && i < (drop_one || multi_test_off ? 42 : 20) ? 2 : 0;
#else
          const int mode = d->state != DL_RUNNING ? 0 : i >= 7 && i < 10 ? 1 : i >= 14 && i < 27 ? 2 : 0;
#endif
          if(mode != drop_mode) {
            drop_mode = mode;
            POC_LOG("show: dl: drop: at %.0f s the line is %s (%lld bytes in, %d connections; the server saw %d, %d for ranges, %d cut)",
                    (poc_now_ms() - t0) / 1e3, mode == 1 ? "CUT" : mode == 2 ? "QUIET" : "back", d->received,
                    m ? multi_workers(m) : 1, drop_connections, drop_ranges, drop_cut);
          }
        }
#endif
        if(DL_SAY(i) || d->state != DL_RUNNING) {
          POC_LOG("show: dl at %.0f s: %s, %lld of %lld bytes, %.2f MB/s now, %s", (poc_now_ms() - t0) / 1e3,
                  download_states[d->state], d->received, d->total, dl_rate_speed(&d->rate, poc_now_ms()) / 1e6,
                  m && !m->solo ? "in parts" : m ? "one part, kept" : "one part");
        if(m && DL_SAY(i)) {
          POC_LOG("show: dl: %d connections now, %d pieces, %d failures%s", multi_workers(m), m->segs, m->failures,
                  m->trouble ? ", waiting for the line" : "");
        }
        }
        if(d->state >= DL_DONE) {
          break;
        }
#if defined(SHOW_DL_KEEP) && SHOW_DL_KEEP == 5
        if(i == 9) {  // 20 s in: "Pausar"; 8 s later: "Continuar"
          static int paused;
          const int ranges = drop_ranges;
          paused = d->id;
          POC_LOG("show: dl: keep: pausing with %lld of %lld bytes in, %d connections", d->received, d->total, m ? multi_workers(m) : 0);
          demo_main([](const char*) { download_do("pause", paused); }, "");
          demo_sleep(8000);
          POC_LOG("show: dl: keep: paused: state %s, %s, %d set(s) alive, %lld of %lld bytes shown, the server serves %d connection(s) (0: all ended)",
                  download_states[d->state], d->kept ? "kept" : "NOT kept", (int)(!!multi_of(d->id)), d->received, d->total, drop_active);
          demo_main([](const char*) { download_do("retry", paused); }, "");
          demo_sleep(1500);
          POC_LOG("show: dl: keep: going on: state %s, %lld bytes in, the server saw %d more range request(s)", download_states[d->state],
                  d->received, drop_ranges - ranges);
        }
#endif
#if defined(SHOW_DL_KEEP) && (SHOW_DL_KEEP == 1 || SHOW_DL_KEEP == 4)
        if(i == 34) {  // 70 s in: the browser is closed with the download unfinished (stop(): multi_keep_all())
          POC_LOG("show: dl: keep: closing the browser with %lld of %lld bytes in (%d map(s) written so far, every 15 s from 60 s on)",
                  d->received, d->total, multi_maps_written);
          demo_main(demo_do_set, back);
          demo_sleep(600);
          POC_RESULT(1, "show: dl: keep: run 1 done in %.0f s; Sair from the menu with the download unfinished",
                     (poc_now_ms() - app.start_ms) / 1e3);
          demo_main(demo_do_ui, "menu-do?quit");
          return;
        }
#endif
#ifdef SHOW_DL_CANCEL
        if(i == 9) {  // 20 s in, in parts by then: the user's "Cancelar"
          static int cancelled;
          cancelled = d->id;
          POC_LOG("show: dl: cancelling it now (%lld bytes in, %s)", d->received, m ? "in parts" : "one part");
          demo_main([](const char*) {
            download_t* it = download_by_id(cancelled);
            if(it) {
              download_cancel(it);
            }
          }, "");
        }
#endif
      }
      if(!d) {
        POC_LOG("show: dl: %s: NO download began", file);
        continue;
      }
#ifdef SHOW_DL_CANCEL
      {  // what the cancelled download left: its folders' files, and the address the bar has
        demo_sleep(3000);
        char near[600];
        snprintf(near, sizeof(near), "%s/" PARTIAL_NEAR, settings.downloads_dir);
        const char* const places[] = {near, PARTIAL_DIR, settings.downloads_dir};
        for(const char* place : places) {
          int files_left = 0;
          long long bytes_left = 0;
          if(DIR* dir = opendir(place)) {
            for(struct dirent* e; (e = readdir(dir));) {
              struct stat st;
              char path[900];
              snprintf(path, sizeof(path), "%s/%s", place, e->d_name);
              if(e->d_name[0] != '.' && !stat(path, &st) && S_ISREG(st.st_mode)) {
                files_left++;
                bytes_left += st.st_size;
              }
            }
            closedir(dir);
          }
          POC_LOG("show: dl: after the cancel, %s has %d file(s), %lld bytes", place, files_left, bytes_left);
        }
        POC_LOG("show: dl: after the cancel its state is %s, %d set(s) of parts alive", download_states[d->state],
                (int)(!!multi_of(d->id)));
      }
#endif
      const double took = (poc_now_ms() - t0) / 1e3;
      unsigned sum = 0xffffffffu;
      long long size = 0;
      if(d->state == DL_DONE) {  // its bytes' CRC-32, a MiB at a time, with a rest after each
        static unsigned char buffer[1 << 20];
        const int in = open(d->path, O_RDONLY);
        for(ssize_t n; in >= 0 && (n = read(in, buffer, sizeof(buffer))) > 0;) {
          for(ssize_t k = 0; k < n; k++) {
            sum = crc_table[(sum ^ buffer[k]) & 0xff] ^ (sum >> 8);
          }
          size += n;
          usleep(300);
        }
        if(in >= 0) {
          close(in);
        }
      }
      POC_LOG("show: dl: %s: %s, %lld bytes in %.1f s = %.2f MB/s, %s; the file has %lld bytes, crc32 %08x", file,
              download_states[d->state], d->total, took, took > 0 ? d->total / took / 1e6 : 0.0,
              parts ? "IN PARTS" : "in one part", size, sum ^ 0xffffffffu);
#ifdef SHOW_DL_DROP
      drop_mode = 0;
      if(d->state == DL_DONE) {  // is the file what the server made, byte for byte?
        static unsigned char got[1 << 20], want[1 << 20];
        const int in = open(d->path, O_RDONLY);
        long long at = 0, wrong = -1;
        for(ssize_t n; in >= 0 && wrong < 0 && (n = read(in, got, sizeof(got))) > 0; at += n) {
          drop_fill(want, at, (size_t)n);
          if(memcmp(got, want, (size_t)n)) {
            for(ssize_t k = 0; k < n && wrong < 0; k++) {
              if(got[k] != want[k]) {
                wrong = at + k;
              }
            }
          }
          usleep(300);
        }
        if(in >= 0) {
          close(in);
        }
        POC_LOG("show: dl: drop: the file %s (%lld of %lld bytes read, first wrong byte at %lld); the server saw %d connections, %d for ranges, %d cut, %d refused",
                wrong < 0 && at == DROP_SIZE ? "IS what the server made" : "is NOT what the server made", at,
                (long long)DROP_SIZE, wrong, drop_connections, drop_ranges, drop_cut, drop_refused);
      } else {
        POC_LOG("show: dl: drop: the download did NOT come through: %s", download_states[d->state]);
      }
#endif
      line = d->id;
      demo_main([](const char*) {  // the test's file and its line go
        download_t* gone = download_by_id(line);
        if(gone) {
          if(gone->state == DL_RUNNING) {
            download_cancel(gone);
          }
          if(gone->path[0] == '/') {
            POC_LOG("show: dl: removed %s (%s)", gone->path, unlink(gone->path) ? "FAILED" : "gone");
          }
          unlink(gone->partial);
          if(gone->ref) {
            WKRelease(gone->ref);
          }
          memset(gone, 0, sizeof(*gone));
          downloads_changed = true;
        }
      }, "");
      demo_sleep(1500);
    }
    demo_main(demo_do_set, back);
    demo_sleep(600);
#ifdef SHOW_DL_DROP
    {
      int lines = 0;
      for(const download_t& x : downloads) {
        lines += x.id != 0;
      }
      drop_clean();
      POC_LOG("show: dl: drop: %d download(s) left in the list (0: no part became a download of its own)", lines);
    }
#endif
    POC_RESULT(1, "show: dl done in %.0f s; Sair from the menu", (poc_now_ms() - app.start_ms) / 1e3);
    demo_main(demo_do_ui, "menu-do?quit");
    return;
  }
#endif
#ifdef SHOW_EMU
  // make SHOW=1 SHOW_YT=1 SHOW_EMU=1 (2026-11-17): emulators that run inside a page, with content that is free to
  // give out. JSNES (an NES in JavaScript: a canvas and Web Audio) with LJ65 (free software, by Damian Yerrick)
  // and with Owlia (homebrew the site itself offers), and v86 (an x86 PC in WebAssembly) starting FreeDOS. For
  // JSNES: a press on its screen, then how many frames a second the page draws and how many different pictures
  // its screen showed in 14 s, with Start pressed as a user would (square is Enter; the D-pad as the arrow keys,
  // put back after). For v86: how long until the A:\> prompt, and the speed it says. A picture of each.
  {
    static char said[1200];
    static char back[40];
    int right = 0;
    snprintf(back, sizeof(back), "dpad%%3D%s", settings.dpad);
    demo_main(demo_do_set, "dpad%3Dkeys");
    demo_sleep(600);
    static const char measure_js[] =
      "(function () { var c = document.querySelector('canvas'); if (!c) return 'no canvas | ' + document.title + ' | ' + location.href;"
      " var o = document.createElement('canvas'); o.width = c.width; o.height = c.height; var g = o.getContext('2d');"
      " function h() { try { g.drawImage(c, 0, 0); var d = g.getImageData(0, 0, o.width, o.height).data, s = 0;"
      " for (var i = 0; i < d.length; i += 97) s = (s * 31 + d[i]) >>> 0; return s; } catch (x) { return 'threw ' + x; } }"
      " var e = window.__emu = {n: 0, t0: performance.now(), ms: 0, p: {}, k: []};"
      " addEventListener('keydown', function (v) { e.k.push(v.key + ':' + v.keyCode + ':' + v.code); }, true);"
      " e.up = 0; addEventListener('keyup', function () { e.up++; }, true);"
      " function f() { e.n++; e.ms = performance.now() - e.t0; if (e.n % 6 == 0) e.p[h()] = 1; if (e.ms < 14000) requestAnimationFrame(f); }"
      " requestAnimationFrame(f);"
      " return 'canvas ' + c.width + 'x' + c.height + ', shown ' + c.clientWidth + 'x' + c.clientHeight + ', focus ' + document.hasFocus()"
      " + ', WebAssembly ' + (typeof WebAssembly) + ', AudioContext ' + (typeof AudioContext) + ', ' + document.title.slice(0, 40); })()";
    static const char result_js[] =
      "(function () { var e = window.__emu; if (!e) return 'no measure';"
      " return e.n + ' frames in ' + Math.round(e.ms) + ' ms = ' + (e.n * 1000 / (e.ms || 1)).toFixed(1) + ' a second, ' + Object.keys(e.p).length"
      " + ' different pictures, keys [' + e.k.join(' ') + '], ' + e.up + ' let go, focus ' + document.hasFocus(); })()";
    static const char* const games[] = {"https://jsnes.org/run/lj65", "https://jsnes.org/run/owlia"};
    for(const char* game : games) {
      const double t0 = poc_now_ms();
      before = demo_loads();
      demo_main(demo_do_load, game);
      const bool loaded = demo_wait_load(before, 30000);
      POC_LOG("show: emu: %s: %s in %.1f s", game, loaded ? "loaded" : "NOT finished", (poc_now_ms() - t0) / 1e3);
      demo_sleep(5000);
      demo_glide(DISPLAY_WIDTH * 0.50f, DISPLAY_HEIGHT * 0.60f, 500);  // onto its screen, and a press there
      demo_sleep(200);
      demo_click();
      demo_sleep(800);
      demo_ask(PAGE, measure_js, said, sizeof(said));
      POC_LOG("show: emu: %s: %s", game, said);
      for(int i = 0; i < 3; i++) {  // Start, as square gives it
        demo_sleep(1500);
        demo_event(INPUT_ENTER, 0, 0, 0);
      }
      demo_sleep(1500);
      demo_dpad(0, 1);
      demo_sleep(700);
      demo_dpad(-1, 0);
      demo_sleep(700);
      demo_dpad(1, 0);
      demo_sleep(6500);
      demo_main([](const char*) { screenshot("an emulator"); }, "");
      demo_sleep(1500);
      demo_ask(PAGE, result_js, said, sizeof(said));
      float rate = 0;
      int pictures = 0;
      const char* at = strstr(said, " ms = ");
      if(at) {
        sscanf(at, " ms = %f a second, %d different", &rate, &pictures);
      }
      const bool runs = rate >= 30 && pictures >= 2;
      right += runs;
      POC_LOG("show: emu: %s: %s: %s", game, runs ? "it runs" : "it does NOT run as it should", said);
    }
    {  // v86: FreeDOS to its prompt
      const double t0 = poc_now_ms();
      before = demo_loads();
      demo_main(demo_do_load, "https://copy.sh/v86/?profile=freedos");
      const bool loaded = demo_wait_load(before, 30000);
      POC_LOG("show: emu: v86: the page %s in %.1f s", loaded ? "loaded" : "NOT finished", (poc_now_ms() - t0) / 1e3);
      bool prompt = false;
      for(int i = 0; i < 40 && !prompt; i++) {  // 80 s for the PC to start
        demo_sleep(2000);
        demo_ask(PAGE,
                 "(function () { var t = document.querySelector('#screen_container'), i = document.querySelector('#runtime_infos');"
                 " var s = t ? t.innerText.replace(/\\s+/g, ' ') : '';"
                 " return (s.indexOf('A:\\\\>') >= 0 ? 'PROMPT' : 'starting') + ' | ' + (i ? i.innerText.replace(/\\s+/g, ' ').slice(0, 90) : 'no info')"
                 " + ' | WebAssembly ' + (typeof WebAssembly) + ' | ' + s.slice(-110); })()",
                 said, sizeof(said));
        prompt = !strncmp(said, "PROMPT", 6);
      }
      right += prompt;
      POC_LOG("show: emu: v86: %s %.1f s after the address was asked for: %s", prompt ? "FreeDOS's prompt" : "NO prompt",
              (poc_now_ms() - t0) / 1e3, said);
      demo_main([](const char*) { screenshot("v86"); }, "");
      demo_sleep(1200);
    }
    demo_main(demo_do_set, back);
    demo_sleep(600);
    POC_RESULT(right == 3, "show: emu: %d of 3 emulators ran, in %.0f s; Sair from the menu", right,
               (poc_now_ms() - app.start_ms) / 1e3);
    demo_main(demo_do_ui, "menu-do?quit");
    return;
  }
#endif
#ifdef SHOW_GAMES
  // make SHOW=1 SHOW_YT=1 SHOW_GAMES=1 (2026-11-17): online games that are played in a page, one after another.
  // For each: did it load, what it has (canvases, WebGL, pointer lock), a picture; then its Play button pressed
  // as a person would (the cursor on it and a press; with none found, a press in the middle and Enter), and
  // after it: how many frames a second the page draws, the WebSockets it opened to its server and the messages
  // that came (counted from the moment the page had loaded), and a second picture. Nothing is typed or signed in.
  {
    static const char* const games[] = {
      "https://slither.io/", "https://diep.io/", "https://krunker.io/", "https://smashkarts.io/",
      "https://skribbl.io/", "https://agar.io/", "https://lichess.org/tv",
    };
    static const char hook_js[] =
      "(function () { if (window.__g) return 'hooked already'; var W = window.WebSocket, g = window.__g = {ws: 0, open: 0, msg: 0, err: 0, urls: []};"
      " window.WebSocket = function (u, p) { g.ws++; g.urls.push(String(u).split('?')[0].slice(0, 50));"
      " var s = p === undefined ? new W(u) : new W(u, p); s.addEventListener('open', function () { g.open++; });"
      " s.addEventListener('message', function () { g.msg++; }); s.addEventListener('error', function () { g.err++; }); return s; };"
      " window.WebSocket.prototype = W.prototype; window.WebSocket.CONNECTING = 0; window.WebSocket.OPEN = 1; window.WebSocket.CLOSING = 2;"
      " window.WebSocket.CLOSED = 3; return 'hooked'; })()";
    static const char state_js[] =
      "(function () { var cs = document.querySelectorAll('canvas'), big = 0, bw = 0, bh = 0;"
      " for (var i = 0; i < cs.length; i++) { var a = cs[i].clientWidth * cs[i].clientHeight; if (a > big) { big = a; bw = cs[i].clientWidth; bh = cs[i].clientHeight; } }"
      " var gl = 'none'; try { var t = document.createElement('canvas'); gl = t.getContext('webgl2') ? 'webgl2' : t.getContext('webgl') ? 'webgl' : 'none'; } catch (e) { gl = 'threw'; }"
      " return document.readyState + ' | ' + document.title.slice(0, 40) + ' | canvases ' + cs.length + ' (largest ' + bw + 'x' + bh + '), ' + gl"
      " + ', WebAssembly ' + (typeof WebAssembly) + ', pointer lock ' + ('requestPointerLock' in Element.prototype) + ', gamepads ' + (typeof navigator.getGamepads)"
      " + ', frames in the page ' + window.frames.length + ' | ' + location.href.slice(0, 60); })()";
    static const char play_js[] =
      "(function () { var re = /^\\s*(play|play now|play!|jogar|start|start game|enter|go|quick play|play as guest|click to play)\\s*$/i, best = null;"
      " var els = document.querySelectorAll('button, a, input[type=button], input[type=submit], div, span');"
      " for (var i = 0; i < els.length && !best; i++) { var e = els[i], t = (e.value || e.innerText || '').trim(); if (t.length > 20 || !re.test(t)) continue;"
      " var r = e.getBoundingClientRect(); if (r.width < 20 || r.height < 12 || r.bottom < 0 || r.top > innerHeight) continue;"
      " best = [Math.round(r.left + r.width / 2), Math.round(r.top + r.height / 2), innerWidth, innerHeight, t.replace(/\\s+/g, '_')]; }"
      " return best ? best.join(' ') : '-1 -1 ' + innerWidth + ' ' + innerHeight + ' none'; })()";
    static const char frames_js[] =
      "(function () { var e = window.__f = {n: 0, t0: performance.now(), ms: 0};"
      " function f() { e.n++; e.ms = performance.now() - e.t0; if (e.ms < 6000) requestAnimationFrame(f); } requestAnimationFrame(f); return 'counting'; })()";
    static const char after_js[] =
      "(function () { var e = window.__f || {n: 0, ms: 1}, g = window.__g || {ws: -1, open: 0, msg: 0, err: 0, urls: []}, cs = document.querySelectorAll('canvas');"
      " return (e.n * 1000 / (e.ms || 1)).toFixed(1) + ' frames a second (' + e.n + ' in ' + Math.round(e.ms) + ' ms), canvases ' + cs.length"
      " + ', WebSockets made ' + g.ws + ', open ' + g.open + ', messages ' + g.msg + ', errors ' + g.err + ' [' + g.urls.slice(0, 3).join(' ') + ']'"
      " + ', pointer locked ' + (document.pointerLockElement ? 'yes' : 'no') + ' | ' + document.title.slice(0, 40) + ' | ' + location.href.slice(0, 60); })()";
    static char said[1200];
    int loaded_count = 0, count = 0;
    for(const char* game : games) {
      const double t0 = poc_now_ms();
      before = demo_loads();
      demo_main(demo_do_load, game);
      const bool loaded = demo_wait_load(before, 30000);
      const double took = (poc_now_ms() - t0) / 1e3;
      demo_sleep(strstr(game, "smashkarts") ? 40000 : 7000);  // (a Unity game: its own loading comes after the page's)
      demo_ask(PAGE, hook_js, said, sizeof(said));
      demo_ask(PAGE, "(function () { var a = document.activeElement; if (a && a.blur) a.blur(); return 'no field in focus'; })()", said,
               sizeof(said));
      demo_ask(PAGE, state_js, said, sizeof(said));
      count++;
      loaded_count += loaded;
      POC_LOG("show: games: %s: %s in %.1f s: %s", game, loaded ? "loaded" : "NOT finished", took, said);
      demo_main([](const char*) { screenshot("a game, as it opens"); }, "");
      demo_sleep(800);
      int cx = -1, cy = -1, iw = 0, ih = 0;
      char what[64] = "";
      demo_ask(PAGE, play_js, said, sizeof(said));
      sscanf(said, "%d %d %d %d %63s", &cx, &cy, &iw, &ih, what);
      if(iw > 0 && ih > 0) {  // the page is the screen's width and its lower part
        const float scale = (float)DISPLAY_WIDTH / iw, top = DISPLAY_HEIGHT - ih * scale;
        if(cx >= 0) {
          demo_glide(cx * scale, top + cy * scale, 500);
        } else {
          demo_glide(DISPLAY_WIDTH * 0.5f, top + ih * scale * 0.5f, 500);
        }
      } else {
        demo_glide(DISPLAY_WIDTH * 0.5f, DISPLAY_HEIGHT * 0.6f, 500);
      }
      demo_sleep(250);
      demo_click();
      // A game's page often keeps its name field in focus, and a press then opens the browser's keyboard over
      // the game (the first run: it stayed over every game after the first and took the presses). Closed here.
      demo_sleep(900);
      {
        static bool keys_were_open;
        demo_main([](const char*) {
          keys_were_open = app.keys_open;
          keys_cancel();
        }, "");
        POC_LOG("show: games: %s: the keyboard after the press: %s", game, keys_were_open ? "OPEN over the game" : "not open");
      }
      demo_sleep(500);
      if(cx < 0) {
        demo_event(INPUT_ENTER, 0, 0, 0);
        demo_sleep(900);
        demo_main([](const char*) { keys_cancel(); }, "");
      }
      POC_LOG("show: games: %s: Play: %s", game, cx >= 0 ? what : "no button found: a press in the middle and Enter");
      demo_sleep(9000);
      demo_ask(PAGE, frames_js, said, sizeof(said));
      demo_sleep(6500);
      demo_ask(PAGE, after_js, said, sizeof(said));
      POC_LOG("show: games: %s: after Play: %s", game, said);
      demo_main([](const char*) { screenshot("a game, after Play"); }, "");
      demo_sleep(800);
    }
#ifdef APP_RN
    // The right stick as the mouse and R2 as its button, in the game mode of a site with the controller as a
    // keyboard (aim_pad()): on example.com, W A S D given to the site as the menu does it, the touch pad for the
    // game mode, the left stick held up (the key w) and the right stick right and down with R2, then all let go;
    // what the page saw, and all undone.
    {
      before = demo_loads();
      demo_main(demo_do_load, "http://example.com/");
      demo_wait_load(before, 15000);
      demo_sleep(1500);
      demo_ask(PAGE, "(function () { var a = window.__a = {moves: 0, x0: null, y0: null, x1: 0, y1: 0, held: 0, log: []};"
               " addEventListener('mousemove', function (e) { a.moves++; if (a.x0 === null) { a.x0 = e.clientX; a.y0 = e.clientY; } a.x1 = e.clientX; a.y1 = e.clientY;"
               " if (e.buttons & 1) a.held++; }, true);"
               " ['mousedown', 'mouseup', 'keydown', 'keyup'].forEach(function (n) { addEventListener(n, function (e) { a.log.push(n + (e.key ? ' ' + e.key : '')); }, true); });"
               " return 'listening'; })()", said, sizeof(said));
      demo_main([](const char*) {
        padkeys_toggle();  // arrow keys
        padkeys_toggle();  // W A S D
        demo_event(INPUT_TOUCHPAD, 0, 0, 0);
      }, "");
      demo_sleep(700);
      demo_main([](const char*) {
        input_event_t e = {INPUT_PAD, 0, -0.9f, 0.9f, 0.5f, (int)INPUT_PAD_R2};  // walk up (w); aim right and down, firing
        on_input(&e);
      }, "");
      demo_sleep(700);
      demo_main([](const char*) {
        input_event_t e = {INPUT_PAD, 0, 0, 0, 0, 0};
        on_input(&e);
      }, "");
      demo_sleep(400);
      static int game_was;
      static bool pointer_shown;
      demo_main([](const char*) {
        game_was = app.game;
        pointer_shown = app.aim_shown;
      }, "");
      demo_ask(PAGE, "(function () { var a = window.__a; if (!a) return 'no list'; return 'mouse moves ' + a.moves + ' (' + a.held + ' with the button held), from '"
               " + a.x0 + ',' + a.y0 + ' to ' + a.x1 + ',' + a.y1 + '; ' + a.log.join(', '); })()", said, sizeof(said));
      POC_LOG("show: games: the right stick as the mouse (game mode %d, pointer %s): %s", game_was, pointer_shown ? "shown" : "hidden", said);
      demo_main([](const char*) { demo_event(INPUT_TOUCHPAD, 0, 0, 0); }, "");  // the controller back to the cursor
      demo_sleep(500);
      demo_main([](const char*) {
        padkeys_toggle();  // off: the site is off the list again
        POC_LOG("show: games: the controller as a keyboard undone (game mode %d, %d site(s) on the list)", app.game, padkeys_count);
      }, "");
      demo_sleep(400);
    }
#endif
    POC_RESULT(loaded_count == count, "show: games: %d of %d finished loading, in %.0f s; Sair from the menu", loaded_count, count,
               (poc_now_ms() - app.start_ms) / 1e3);
    demo_main(demo_do_ui, "menu-do?quit");
    return;
  }
#endif
#ifdef SHOW_SITES
  // make SHOW=1 SHOW_YT=1 SHOW_SITES=1 (2026-11-14, for the release): the sites people open most, one after
  // another in the same tab, as they are for this user (his cookies; nothing goes to his history: SHOW_YT).
  // For each: did its load finish, in how long, and what the page has (its state, title, text, pictures that
  // came, links, scripts, videos, canvases, where it ended up), with a picture of the screen. A page that takes
  // the WebProcess down says so in the log ("WebProcess crashed") and the next one goes on.
  // make ... SHOW_SITES=1 SHOW_SITE=<address> (2026-11-17, "does this site open?" asked about one site): that
  // one address in the place of the list, in a private tab (nothing of it is kept: no cookies, no storage, and
  // no test writes the history), with no picture of the screen.
  {
#ifdef SHOW_SITE
    static const char* const sites[] = {SHOW_SITE};
    demo_sleep(2500);
    demo_main(demo_do_ui, "newprivate");
    demo_sleep(2500);
    POC_LOG("show: sites: one address, in a private tab (%s)", tab_is_private() ? "private" : "NOT PRIVATE");
#else
    static const char* const sites[] = {
      "https://www.google.com/search?q=playstation+5",
      "https://www.wikipedia.org/",
      "https://en.wikipedia.org/wiki/WebKit",
      "https://www.youtube.com/",
      "https://github.com/WebKit/WebKit",
      "https://www.reddit.com/r/PS5/",
      "https://www.amazon.com/",
      "https://www.ebay.com/",
      "https://www.bbc.com/news",
      "https://www.publico.pt/",
      "https://x.com/explore",
      "https://www.twitch.tv/directory",
      "https://www.instagram.com/",
      "https://www.facebook.com/",
      "https://www.netflix.com/",
      "https://open.spotify.com/",
      "https://www.google.com/maps",
      "https://web.whatsapp.com/",
      "https://web.telegram.org/k/",
      "https://mail.google.com/",
      "https://www.imdb.com/",
      "https://stackoverflow.com/questions",
      "https://www.speedtest.net/",
      "https://www.playstation.com/",
    };
#endif
    static char said[1200];
    int finished = 0, count = 0;
    const double all0 = poc_now_ms();
    for(const char* site : sites) {
      const double t0 = poc_now_ms();
      before = demo_loads();
      demo_main(demo_do_load, site);
      const bool loaded = demo_wait_load(before, 30000);
      const double took = (poc_now_ms() - t0) / 1e3;
      demo_sleep(3500);
      demo_ask(PAGE,
               "(function () { var b = document.body, im = document.images, n = 0; for (var i = 0; i < im.length; i++) if (im[i].complete && im[i].naturalWidth > 0) n++;"
               " return document.readyState + ' | ' + document.title.slice(0, 50) + ' | text ' + (b ? b.innerText.length : -1) + ', pictures ' + n + ' of ' + im.length"
               " + ', links ' + document.links.length + ', scripts ' + document.scripts.length + ', videos ' + document.querySelectorAll('video').length"
               " + ', canvases ' + document.querySelectorAll('canvas').length + ' | ' + location.href.slice(0, 70); })()",
               said, sizeof(said));
      count++;
      finished += loaded;
      POC_LOG("show: sites: %s: %s in %.1f s: %s", site, loaded ? "loaded" : "NOT finished", took, said);
#ifndef SHOW_SITE
      demo_main([](const char*) { screenshot("a site"); }, "");
#endif
#ifdef SHOW_SITE_WAIT  // (with SHOW_SITE) that many seconds later: the page's video, if it has one, and how its text begins
      demo_sleep(SHOW_SITE_WAIT * 1000);
      demo_ask(PAGE,
               "(function () { var v = document.querySelector('video');"
               " return (v ? 'video t ' + v.currentTime.toFixed(1) + (v.paused ? ' PAUSED' : ' playing') + (v.currentSrc ? '' : ', no source') + ', ' + v.videoWidth + 'x' + v.videoHeight : 'no video')"
               " + ' | text ' + (document.body ? document.body.innerText.replace(/\\s+/g, ' ').slice(0, 120) : ''); })()",
               said, sizeof(said));
      POC_LOG("show: sites: %d s later: %s", SHOW_SITE_WAIT, said);
#ifdef SHOW_SITE_ERRORS  // (2026-11-18, a site whose video stays without a source) the video's state and what the page's scripts complained of
      demo_ask(PAGE,
               "(function () { var vs = document.querySelectorAll('video'), out = [];"
               " for (var i = 0; i < vs.length && i < 3; i++) { var v = vs[i];"
               " out.push('video ' + i + ': src ' + String(v.getAttribute('src') || '').slice(0, 60) + ', current ' + String(v.currentSrc).slice(0, 60)"
               " + ', sources ' + v.querySelectorAll('source').length + ', network ' + v.networkState + ', ready ' + v.readyState"
               " + ', error ' + (v.error ? v.error.code + ' ' + v.error.message : 'none') + ', preload ' + v.preload); }"
               " var fr = document.querySelectorAll('iframe'), fs = [];"
               " for (var j = 0; j < fr.length && j < 4; j++) fs.push(String(fr[j].src).slice(0, 70));"
               " var ms = window.MediaSource ? ['video/mp4; codecs=\"avc1.64001F\"', 'video/webm; codecs=\"vp9\"', 'audio/mp4; codecs=\"mp4a.40.2\"'].map(function (t) { return MediaSource.isTypeSupported(t) ? 1 : 0; }).join('') : 'none';"
               " var hls = document.createElement('video').canPlayType('application/vnd.apple.mpegurl');"
               " var log = (window.__bps5log || []).slice(-12).join(' || ');"
               " return (out.join(' ; ') || 'no video') + ' | frames ' + fs.join(' ') + ' | MSE ' + ms + ', HLS \"' + hls + '\" | log ' + (log || 'empty'); })()",
               said, sizeof(said));
      POC_LOG("show: sites: the page's video and log: %s", said);
      demo_ask(PAGE,
               "(function () { var v = document.querySelector('video'), p = v, chain = [];"
               " for (var i = 0; i < 6 && p; i++) { chain.push(p.tagName + '.' + String(p.className).slice(0, 40)); p = p.parentElement; }"
               " var pl = v && v.closest('.player, [class*=player]');"
               " var bs = pl ? Array.prototype.map.call(pl.querySelectorAll('button'), function (b) { return (b.getAttribute('aria-label') || b.textContent || '').trim().slice(0, 16) + (b.offsetWidth ? '' : '(hidden)'); }).slice(0, 12) : [];"
               " return 'chain ' + chain.join(' < ') + ' | player buttons ' + bs.length + ': ' + bs.join(', ') + ' | player text ' + (pl ? pl.innerText.replace(/\\s+/g, ' ').slice(0, 160) : 'none')"
               " + ' | player html ' + (pl ? pl.innerHTML.length : 0) + ' | ua ' + navigator.userAgent.slice(0, 140); })()",
               said, sizeof(said));
      POC_LOG("show: sites: the player: %s", said);
      // Vimeo's player starts from module scripts and import() (2026-11-18): what of it came, and what an
      // import() of its module does now.
      demo_ask(PAGE,
               "(function () { var r = performance.getEntriesByType('resource').filter(function (e) { return /\\.(m?js|css)(\\?|$)/.test(e.name); })"
               ".map(function (e) { return e.name.replace(/^https?:\\/\\//, '').slice(0, 26) + '..' + e.name.split('/').pop().slice(0, 28) + ' ' + Math.round(e.duration) + 'ms ' + e.transferSize; });"
               " var c = window.playerConfig, u = c && c.request && c.request.urls ? c.request.urls.js_module : '';"
               " window.__imp = 'not tried'; window.__eme = navigator.requestMediaKeySystemAccess ? 'pending' : 'no requestMediaKeySystemAccess';"
               " if (navigator.requestMediaKeySystemAccess) navigator.requestMediaKeySystemAccess('com.widevine.alpha', [{ initDataTypes: ['cenc'], videoCapabilities: [{ contentType: 'video/mp4; codecs=\"avc1.42E01E\"' }] }])"
               ".then(function () { window.__eme = 'granted'; }, function (e) { window.__eme = 'refused: ' + String(e).slice(0, 80); });"
               " if (u) { window.__imp = 'pending'; import(u).then(function (m) { window.__imp = 'loaded: ' + Object.keys(m).slice(0, 5).join(','); }, function (e) { window.__imp = 'failed: ' + String(e).slice(0, 120); }); }"
               " return 'dynamicImport ' + window.dynamicImportSupported + ', cssLayers ' + window.cssLayersSupported + ', noModule ' + window.noModuleLoading"
               " + ', module ' + String(u).slice(-40) + ' | ' + r.length + ' scripts/styles: ' + r.slice(0, 14).join(' ; '); })()",
               said, sizeof(said));
      POC_LOG("show: sites: modules: %s", said);
      demo_sleep(8000);
      demo_ask(PAGE, "String(window.__imp) + ' | DRM (Widevine asked): ' + String(window.__eme)", said, sizeof(said));
      POC_LOG("show: sites: our import() 8 s later: %s", said);
      // A player that loads its film only once it is asked to play (as a person does: its play button).
      demo_ask(PAGE,
               "(function () { var c = Array.prototype.slice.call(document.querySelectorAll('button, [role=button]')).filter(function (b) {"
               " var t = (b.getAttribute('aria-label') || b.title || b.textContent || '').trim(); return /^(play|reproduzir)\\b/i.test(t) && b.offsetWidth > 0; });"
               " var v = document.querySelector('video');"
               " if (window.jwplayer) { try { jwplayer().setMute(true); jwplayer().play(); return 'jwplayer().play() (the Internet Archive\\'s player)'; } catch (e) { return 'jwplayer threw ' + e; } }"
               " if (c.length) { c[0].click(); return 'pressed ' + (c[0].getAttribute('aria-label') || c[0].textContent).trim().slice(0, 40); }"
               " if (v) { (v.parentElement || v).click(); return 'clicked the box of the video'; } return 'nothing to press'; })()",
               said, sizeof(said));
      POC_LOG("show: sites: play: %s", said);
      demo_sleep(12000);
      demo_ask(PAGE,
               "(function () { var v = document.querySelector('video');"
               " if (!v && window.jwplayer) { try { var j = jwplayer(); return 'jwplayer ' + j.getState() + ' at ' + Number(j.getPosition()).toFixed(1) + ' of ' + Number(j.getDuration()).toFixed(1)"
               " + ', quality ' + JSON.stringify((j.getQualityLevels() || [])[j.getCurrentQuality()] || null).slice(0, 80); } catch (e) { return 'jwplayer threw ' + e; } }"
               " if (!v) return 'no video';"
               " return 't ' + v.currentTime.toFixed(1) + (v.paused ? ' PAUSED' : ' playing') + ', ready ' + v.readyState + ', network ' + v.networkState"
               " + ', ' + v.videoWidth + 'x' + v.videoHeight + ', from ' + String(v.currentSrc).slice(0, 50)"
               " + (v.error ? ', ERROR ' + v.error.code + ' ' + String(v.error.message || '').slice(0, 70) : '')"
               " + ' | log ' + (window.__bps5log || []).slice(-6).join(' || '); })()",
               said, sizeof(said));
      POC_LOG("show: sites: 12 s after play: %s", said);
#endif
#endif
      demo_sleep(800);
    }
    POC_RESULT(finished == count, "show: sites: %d of %d finished loading, in %.0f s; Sair from the menu", finished, count,
               (poc_now_ms() - all0) / 1e3);
    demo_main(demo_do_ui, "menu-do?quit");
    return;
  }
#endif
#ifdef SHOW_YT_EMBED
  // make SHOW=1 SHOW_YT=1 SHOW_YT_EMBED=1 (2026-11-11 23:30): a YouTube film inside another site's page (an
  // <iframe> of youtube.com/embed, as news sites and forums have). The frame takes the name its page gives
  // (Safari on a Mac: user_agent_for() changes the name of a tab's own page only): is it refused a minute in,
  // as YouTube's own page was? The page (example.com) hears the player through YouTube's own messages for
  // embedding pages (enablejsapi): where the film is, every 5 s for 100 s. Then Sair.
  {
    char said[700];
    before = demo_loads();
    demo_main(demo_do_load, "https://example.com/");
    demo_wait_load(before, 20000);
    demo_sleep(1000);
    demo_ask(PAGE,
             "(function () { window.__e = {t: -1, state: '?', q: '', n: 0, err: ''};"
             " var f = document.createElement('iframe'); f.id = 'yt'; f.width = 1280; f.height = 720; f.allow = 'autoplay; encrypted-media; fullscreen';"
             " f.style.cssText = 'position:fixed;left:40px;top:20px;border:0;z-index:9999';"
             " f.src = 'https://www.youtube.com/embed/aqz-KE-bpKQ?autoplay=1&mute=1&enablejsapi=1&vq=hd2160&origin=' + encodeURIComponent(location.origin);"
             " addEventListener('message', function (m) { if (!/youtube/.test(m.origin)) return; var d; try { d = JSON.parse(m.data); } catch (x) { return; } __e.n++;"
             " if (d.event === 'onError') __e.err = String(d.info); var i = d.info; if (i && typeof i === 'object') {"
             " if (typeof i.currentTime === 'number') __e.t = i.currentTime; if (typeof i.playerState === 'number') __e.state = i.playerState;"
             " if (i.playbackQuality) __e.q = i.playbackQuality; if (i.videoData && i.videoData.errorCode) __e.err = i.videoData.errorCode; } });"
             " f.onload = function () { var say = function () { try { f.contentWindow.postMessage(JSON.stringify({event: 'listening', id: 1, channel: 'widget'}), 'https://www.youtube.com'); } catch (x) { } };"
             " say(); setInterval(say, 2000); };"
             " document.body.appendChild(f); return 'embedded'; })()",
             said, sizeof(said));
    POC_LOG("show: embed: %s", said);
    const double e0 = poc_now_ms();
    for(int i = 0; i < 21; i++) {
      demo_sleep(5000);
      demo_ask(PAGE, "(function () { var e = window.__e; return 't ' + e.t.toFixed(1) + ', state ' + e.state + ', quality ' + e.q + ', messages ' + e.n + (e.err ? ', error ' + e.err : ''); })()",
               said, sizeof(said));
      POC_LOG("show: embed at %.0f s: %s", (poc_now_ms() - e0) / 1e3, said);
      if(i == 1 && !strstr(said, "state 1,")) {  // not playing by itself: a press on it, as the user would
        demo_glide(680, app.bar_height + 380, 600);
        demo_click();
        POC_LOG("show: embed: not playing by itself: cross on the frame");
      }
      if(i == 4) {
        demo_main([](const char*) { screenshot("embed"); }, "");
      }
    }
    POC_RESULT(1, "show: embed done in %.0f s; Sair from the menu", (poc_now_ms() - app.start_ms) / 1e3);
    demo_main(demo_do_ui, "menu-do?quit");
    return;
  }
#endif
#ifdef SHOW_YT_TV
  // make SHOW=1 SHOW_YT=1 SHOW_YT_TV=1 (2026-11-11 21:50): YouTube's own page for televisions and consoles
  // (youtube.com/tv), asked for as what this browser is (a PlayStation 5's WebKit: user_agent_apply()). Is it
  // given to this browser, does the film start by itself, and does it go on after the minute at which the
  // desktop page's film is refused half of the times? Two minutes of what the page shows, three pictures. Then Sair.
#ifdef SHOW_VIDEOS
  // make SHOW=1 SHOW_YT=1 SHOW_YT_TV=1 SHOW_VIDEOS=1 (2026-11-18): which kinds of video play, outside YouTube. In
  // a private tab (nothing is kept): what the engine says it plays (files and Media Source, by codec; HLS by
  // itself, which sites give a browser that calls itself Safari); three videos put into a plain page by their
  // address (an MP4 file, a WebM file, an HLS address); then the players of some sites, each asked to play its
  // largest video without sound: where it is 8 and 12 s later, its size, where it comes from, its error. Then
  // Sair. No picture of the screen.
  {
    static const struct { const char* name; const char* page; const char* video; const char* preload; } list[] = {
#ifdef SHOW_VIDEOS_FILES  // only the files, by their preload (the buffer on the disk is asked for with "auto", and with play() after "none")
      {"an MP4 file of 5 s (H.264), preload auto", "https://example.com/", "https://interactive-examples.mdn.mozilla.net/media/cc0-videos/flower.mp4", "auto"},
      {"an MP4 file of 52 s (H.264), as a page gives it", "https://example.com/", "https://media.w3.org/2010/05/sintel/trailer.mp4", 0},
      {"an MP4 file of 5 s (H.264), preload none", "https://example.com/", "https://interactive-examples.mdn.mozilla.net/media/cc0-videos/flower.mp4", "none"},
      {"an MP4 file of 5 s (H.264), preload metadata", "https://example.com/", "https://interactive-examples.mdn.mozilla.net/media/cc0-videos/flower.mp4", "metadata"},
      {"a WebM file (VP8, Vorbis)", "https://example.com/", "https://interactive-examples.mdn.mozilla.net/media/cc0-videos/flower.webm", 0},
#else
      {"an MP4 file (H.264)", "https://example.com/", "https://interactive-examples.mdn.mozilla.net/media/cc0-videos/flower.mp4"},
      {"a WebM file", "https://example.com/", "https://interactive-examples.mdn.mozilla.net/media/cc0-videos/flower.webm"},
      {"an HLS address, by itself", "https://example.com/", "https://test-streams.mux.dev/x36xhzz/x36xhzz.m3u8"},
      {"hls.js's own page (HLS over Media Source)", "https://hlsjs.video-dev.org/demo/", 0},
      {"dash.js's sample (DASH over Media Source)", "https://reference.dashif.org/dash.js/latest/samples/getting-started/auto-load-single-video.html", 0},
      {"Vimeo", "https://vimeo.com/76979871", 0},
      {"Twitch's first page", "https://www.twitch.tv/", 0},
      {"Kick's first page", "https://kick.com/", 0},
      {"the Internet Archive", "https://archive.org/details/BigBuckBunny_124", 0},
#endif
    };
    static const char can_js[] =
      "(function () { var v = document.createElement('video'), M = window.MediaSource || window.ManagedMediaSource;"
      " function c(t) { return v.canPlayType(t) || 'no'; } function m(t) { return M && M.isTypeSupported ? (M.isTypeSupported(t) ? 'yes' : 'no') : 'none'; }"
      " var T = {h264: 'video/mp4; codecs=\"avc1.640028\"', hevc: 'video/mp4; codecs=\"hvc1.1.6.L120.90\"', vp9: 'video/webm; codecs=\"vp9\"', vp9mp4: 'video/mp4; codecs=\"vp09.00.40.08\"',"
      " av1: 'video/mp4; codecs=\"av01.0.08M.08\"', vp8: 'video/webm; codecs=\"vp8\"', aac: 'audio/mp4; codecs=\"mp4a.40.2\"', opus: 'audio/webm; codecs=\"opus\"', mp3: 'audio/mpeg', flac: 'audio/flac', ac3: 'audio/mp4; codecs=\"ac-3\"'};"
      " var out = []; for (var k in T) out.push(k + ' ' + c(T[k]) + '/' + m(T[k]));"
      " return 'HLS by itself: ' + c('application/vnd.apple.mpegurl') + ' | MediaSource ' + (window.MediaSource ? 'yes' : 'no') + ', ManagedMediaSource ' + (window.ManagedMediaSource ? 'yes' : 'no')"
      " + ' | as a file/over Media Source: ' + out.join('; '); })()";
    static const char find_js[] =
      "(function () { var vs = Array.prototype.slice.call(document.querySelectorAll('video')), v = null, a = -1; window.__ve = '';"
      " vs.forEach(function (x) { var r = x.getBoundingClientRect(), s = r.width * r.height; if (s > a) { a = s; v = x; } });"
      " window.__v = v; if (!v) return 'no video in the page itself (' + document.querySelectorAll('iframe').length + ' frame(s))';"
      " try { v.muted = true; var p = v.play(); if (p && p.catch) p.catch(function (e) { window.__ve = String(e).slice(0, 90); }); } catch (e) { window.__ve = 'threw ' + e; }"
      " return vs.length + ' video(s), the largest asked to play'; })()";
    static const char state_js[] =
      "(function () { var v = window.__v; if (!v) return 'no video'; var s = v.currentSrc || '';"
      " var kind = s.indexOf('blob:') === 0 ? 'Media Source' : /\\.m3u8/.test(s) ? 'an HLS address' : /\\.mp4/.test(s) ? 'an MP4 file' : /\\.webm/.test(s) ? 'a WebM file' : s ? 'address ' + s.slice(0, 50) : 'no source';"
      " return 't ' + v.currentTime.toFixed(1) + (v.ended ? ' ENDED' : v.paused ? ' PAUSED' : ' playing') + ' of ' + (isFinite(v.duration) ? v.duration.toFixed(1) : 'no length') + ', ready ' + v.readyState + ', ' + v.videoWidth + 'x' + v.videoHeight + ', from ' + kind"
      " + (v.error ? ', ERROR ' + v.error.code + ' ' + String(v.error.message || '').slice(0, 70) : '') + (window.__ve ? ', play() said: ' + window.__ve : ''); })()";
    char said[1200], js[900];
    int plays = 0, count = 0;

    demo_sleep(2500);
    demo_main(demo_do_ui, "newprivate");
    demo_sleep(2500);
    POC_LOG("show: videos: in a private tab (%s)", tab_is_private() ? "private" : "NOT PRIVATE");
    for(const auto& one : list) {
      before = demo_loads();
      demo_main(demo_do_load, one.page);
      const bool loaded = demo_wait_load(before, 30000);
      if(count == 0) {
        demo_ask(PAGE, can_js, said, sizeof(said));
        POC_LOG("show: videos: the engine says: %s", said);
      }
      if(one.video) {
        snprintf(js, sizeof(js),
                 "(function (u, pre) { var v = document.createElement('video'); v.muted = true; v.style.cssText = 'position:fixed;left:0;top:0;width:640px;height:360px';"
                 " if (pre) v.preload = pre; v.src = u; document.body.appendChild(v); window.__v = v; window.__ve = ''; var p = v.play(); if (p && p.catch) p.catch(function (e) { window.__ve = String(e).slice(0, 90); });"
                 " return 'put into the page and asked to play'; })('%s', '%s')", one.video, one.preload ? one.preload : "");
        demo_sleep(1500);
        demo_ask(PAGE, js, said, sizeof(said));
      } else {
        demo_sleep(7000);
        demo_ask(PAGE, find_js, said, sizeof(said));
      }
      POC_LOG("show: videos: %s: page %s; %s", one.name, loaded ? "loaded" : "NOT finished", said);
      demo_sleep(8000);
      demo_ask(PAGE, state_js, said, sizeof(said));
      const double t8 = strncmp(said, "t ", 2) ? -1 : atof(said + 2);
      POC_LOG("show: videos: %s: 8 s later: %s", one.name, said);
      demo_sleep(4000);
      demo_ask(PAGE, state_js, said, sizeof(said));
      const double t12 = strncmp(said, "t ", 2) ? -1 : atof(said + 2);
      // (a clip shorter than the wait is at its end by now: that counts, 2026-11-18)
      const bool playing = (t12 > t8 + 1 && t8 >= 0) || (strstr(said, " ENDED") && t12 > 1);
      POC_LOG("show: videos: %s: 12 s later: %s => %s", one.name, said, playing ? "PLAYS" : "does not play");
      if(playing && !strstr(said, " ENDED") && !strstr(said, "no length")) {  // a jump ahead: the bytes are asked of the server again
        demo_ask(PAGE, "(function () { var v = window.__v; if (!v || !(v.duration > 30)) return 'no jump (short or no video)'; var to = Math.min(40, v.duration - 8); v.currentTime = to; return 'jumped to ' + to.toFixed(0); })()", said, sizeof(said));
        if(!strncmp(said, "jumped", 6)) {
          char jumped[64];
          snprintf(jumped, sizeof(jumped), "%s", said);
          demo_sleep(4000);
          demo_ask(PAGE, state_js, said, sizeof(said));
          POC_LOG("show: videos: %s: %s, 4 s later: %s", one.name, jumped, said);
        }
      }
      plays += playing;
      count++;
      demo_ask(PAGE, "(function () { var v = window.__v; if (v) { try { v.pause(); } catch (e) {} } return 'stopped'; })()", said, sizeof(said));
    }
    POC_RESULT(1, "show: videos: %d of %d play; done in %.0f s; Sair from the menu", plays, count, (poc_now_ms() - app.start_ms) / 1e3);
    demo_main(demo_do_ui, "menu-do?quit");
    return;
  }
#endif
#ifdef SHOW_YT_DIAG
  // make SHOW=1 SHOW_YT=1 SHOW_YT_TV=1 SHOW_YT_DIAG=1 (2026-11-18): the page for televisions answers a film with
  // "An error occurred, the YouTube app must be restarted" since 2026-11-17 23:47. Why: what the page's scripts
  // complained of from its start (extensions_apply() keeps it), what the page says it is (its own configuration:
  // the client's name and version), what the browser tells sites it is, and what the page's own request for the
  // film is answered (asked once more here, with the page's own configuration: its status and its reason).
  {
    static const char ask_js[] =
      "(function () { window.__d2 = 'no answer yet'; try { var g = window.ytcfg && ytcfg.get ? function (k) { return ytcfg.get(k); } : function () { return undefined; };"
      " var ctx = g('INNERTUBE_CONTEXT'), key = g('INNERTUBE_API_KEY'), c = ctx && ctx.client || {};"
      " window.__d = 'client ' + c.clientName + ' ' + c.clientVersion + ' | platform ' + c.platform + ' | form ' + c.clientFormFactor + ' | model ' + c.deviceModel + ' | os ' + c.osName + ' ' + c.osVersion + ' | browser ' + c.browserName + ' ' + c.browserVersion"
      " + ' | signed in ' + g('LOGGED_IN') + ' | ua ' + navigator.userAgent;"
      " if (!ctx) { window.__d2 = 'no configuration to ask with'; return window.__d; }"
      " fetch('/youtubei/v1/player?prettyPrint=false' + (key ? '&key=' + key : ''), {method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify({context: ctx, videoId: 'aqz-KE-bpKQ'})})"
      " .then(function (r) { return r.text().then(function (t) { var j = {}; try { j = JSON.parse(t); } catch (e) {} var p = j.playabilityStatus || {};"
      " window.__d2 = 'HTTP ' + r.status + ' | status ' + p.status + ' | reason ' + (p.reason || '') + ' | ' + JSON.stringify(p.errorScreen || p.messages || {}).slice(0, 300) + ' | formats ' + (j.streamingData ? (j.streamingData.adaptiveFormats || []).length : 'none') + ' | bytes ' + t.length; }); })"
      " .catch(function (e) { window.__d2 = 'the request failed: ' + e; });"
      " } catch (e) { window.__d = 'threw ' + e; } return window.__d; })()";
    char said[1200];

    demo_sleep(2500);
    before = demo_loads();
    demo_main(demo_do_load, "https://www.youtube.com/tv#/watch?v=aqz-KE-bpKQ");
    demo_wait_load(before, 25000);
    demo_sleep(20000);
    demo_ask(PAGE, "(function () { var v = document.querySelector('video'); return (v ? 'video t ' + v.currentTime.toFixed(1) + (v.paused ? ' PAUSED' : ' playing') + (v.currentSrc ? '' : ', no source') : 'no video') + ' | text ' + (document.body ? document.body.innerText.replace(/\\s+/g, ' ').slice(0, 140) : ''); })()", said, sizeof(said));
    POC_LOG("show: tv diag: the page: %s", said);
    for(int i = 0; i < 4; i++) {
      char js[160];
      snprintf(js, sizeof(js), "(function () { var L = window.__bps5log; return L ? L.length + ' kept: ' + L.join(' || ').slice(%d, %d) : 'nothing kept'; })()", i * 1000, i * 1000 + 1000);
      demo_ask(PAGE, js, said, sizeof(said));
      POC_LOG("show: tv diag: complaints %d: %s", i, said);
      if(strlen(said) < 900) {
        break;
      }
    }
    demo_ask(PAGE, ask_js, said, sizeof(said));
    POC_LOG("show: tv diag: %s", said);
    demo_sleep(7000);
    demo_ask(PAGE, "window.__d2", said, sizeof(said));
    POC_LOG("show: tv diag: the film's request: %s", said);
    POC_RESULT(1, "show: tv diag done in %.0f s; Sair from the menu", (poc_now_ms() - app.start_ms) / 1e3);
    demo_main(demo_do_ui, "menu-do?quit");
    return;
  }
#endif
#ifdef SHOW_YT_BACK
  // make SHOW=1 SHOW_YT=1 SHOW_YT_TV=1 SHOW_YT_BACK=1 (2026-11-17, a report: circle on a film of this page went
  // to the browser's start page). A film of the page opened from the start page, as its address; circle once:
  // the page must still be this one, out of the film (tv_page_back(): the page's own "back"). Circle again, on
  // the page's first screen: the browser must be out of the page, on the one before it. Where the page is after
  // each, how long its history is, and whether it asked for its window to be closed (window.close, counted by
  // the page for this test). Then Sair. (The first run pressed cross on what the page had lit to open a film:
  // with no account the page starts at its welcome, and cross went into its sign-in. Not done again.)
  {
    static const char where_js[] =
      "(function () { var v = document.querySelector('video'), a = document.activeElement;"
      " return location.href.slice(0, 90) + ' | history ' + history.length"
      " + ' | ' + (v ? 'video t ' + v.currentTime.toFixed(1) + (v.paused ? ' PAUSED' : ' playing') + (v.currentSrc ? '' : ', no source') : 'no video')"
      " + ' | closes ' + (window.__bps5closes === undefined ? 'not counted' : window.__bps5closes)"
      " + ' | focus ' + (a ? a.tagName + '.' + String(a.className).slice(0, 40) : 'none')"
      " + ' | text ' + (document.body ? document.body.innerText.replace(/\\s+/g, ' ').slice(0, 150) : ''); })()";
    static const char hook_js[] =
      "(function () { if (window.__bps5closes === undefined) { window.__bps5closes = 0;"
      " window.close = function () { window.__bps5closes++; }; } return 'counting'; })()";
    static const char film_js[] =
      "(function () { var v = document.querySelector('video'); return v && v.currentSrc && v.currentTime > 1 && !v.paused ? 'playing' : 'not yet'; })()";
    char said[900];
    bool film = false, stayed = false, left = false;

    demo_sleep(2500);
    before = demo_loads();
    demo_main(demo_do_load, "https://www.youtube.com/tv#/watch?v=aqz-KE-bpKQ");
    demo_wait_load(before, 25000);
    for(int i = 0; i < 25 && !film; i++) {
      demo_sleep(1000);
      demo_ask(PAGE, film_js, said, sizeof(said));
      film = !strcmp(said, "playing");
    }
    demo_sleep(3000);
    demo_ask(PAGE, hook_js, said, sizeof(said));
    demo_ask(PAGE, where_js, said, sizeof(said));
    POC_LOG("show: tv back: before circle (the film %s): %s", film ? "plays" : "DID NOT START", said);
    demo_main([](const char*) { screenshot("tv film"); }, "");
    for(int i = 1; i <= 2; i++) {
      demo_event(INPUT_BACK, 0, 0, 0);
      demo_sleep(i == 1 ? 5000 : 4000);
      if(i == 1) {
        demo_ask(PAGE, hook_js, said, sizeof(said));
      }
      demo_ask(PAGE, where_js, said, sizeof(said));
      POC_LOG("show: tv back: after circle %d: %s", i, said);
      if(i == 1) {
        stayed = !strncmp(said, "https://www.youtube.com/tv", 26) && !strstr(said, "#/watch");
      } else {
        left = said[0] && strncmp(said, "https://www.youtube.com/tv", 26);
      }
      demo_main([](const char*) { screenshot("tv after circle"); }, "");
    }
    POC_LOG("show: tv back: the app's full screen is %s after leaving", app.fullscreen ? "ON" : "off");
    POC_RESULT(film && stayed && left, "show: tv back: %s; %s; done in %.0f s; Sair from the menu",
               !film ? "the film did not start" : stayed ? "circle on a film stayed in the page, out of the film" : "circle on a film LEFT the page or the film stayed",
               left ? "circle on the page's first screen left the page" : "circle on the page's first screen DID NOT leave the page",
               (poc_now_ms() - app.start_ms) / 1e3);
    demo_main(demo_do_ui, "menu-do?quit");
    return;
  }
#endif
  {
    static const char tv_js[] =
      "(function () { var v = document.querySelector('video'), b = v && v.buffered, ahead = 0;"
      " if (b) for (var i = 0; i < b.length; i++) if (b.start(i) <= v.currentTime + 0.5 && b.end(i) > v.currentTime) ahead = b.end(i) - v.currentTime;"
      " var a = document.activeElement;"
      " return location.href.slice(0, 80) + ' | title ' + document.title.slice(0, 40) + ' | '"
      " + (v ? 't ' + v.currentTime.toFixed(1) + (v.paused ? ' PAUSED' : '') + ', ready ' + v.readyState + ', ahead ' + ahead.toFixed(1) + ', ' + v.videoWidth + 'x' + v.videoHeight + (v.error ? ', error ' + v.error.code : '') : 'no video')"
      " + ' | focus ' + (a ? a.tagName + '.' + String(a.className).slice(0, 30) : 'none')"
      " + ' | text ' + (document.body ? document.body.innerText.replace(/\\s+/g, ' ').slice(0, 110) : ''); })()";
    char said[700];
    demo_sleep(2500);
    demo_main([](const char*) { screenshot("start page"); }, "");  // the start page's "YouTube na TV" tile (start-rn.js)
    demo_ask(PAGE, "(function () { var t = document.querySelector('.tile.tv'); return t ? 'the tile: ' + t.innerText.split(String.fromCharCode(10)).filter(Boolean).join(' / ') + ' -> ' + t.href : 'no YouTube TV tile on ' + location.href.slice(0, 60); })()",
             said, sizeof(said));
    POC_LOG("show: tv: %s", said);
    before = demo_loads();
    demo_main(demo_do_load, "https://www.youtube.com/tv#/watch?v=aqz-KE-bpKQ");
    demo_wait_load(before, 25000);
    const double tv0 = poc_now_ms();
    for(int i = 0; i < 16; i++) {
      demo_ask(PAGE, tv_js, said, sizeof(said));
      POC_LOG("show: tv at %.0f s: %s", (poc_now_ms() - tv0) / 1e3, said);
      if(i == 2 || i == 22) {
        demo_main([](const char*) { screenshot("tv"); }, "");
      }
      // 22:35: the controller on this page (dpad_keys_now()): the D-pad is the arrow keys, cross is Enter.
      // Down shows the film's controls with the play button lit; cross a second later presses it (the film
      // pauses); cross again plays it. What the page says after each, and a picture with the controls there.
      if(i == 11) {
        demo_dpad(0, 1);
        demo_sleep(1200);
        demo_ask(PAGE, tv_js, said, sizeof(said));
        POC_LOG("show: tv: after the D-pad's down: %s", said);
        demo_main([](const char*) { screenshot("tv controls"); }, "");
        demo_click();
        demo_sleep(1500);
        demo_ask(PAGE, tv_js, said, sizeof(said));
        POC_LOG("show: tv: after cross: %s", said);
        demo_click();
        demo_sleep(1500);
        demo_ask(PAGE, tv_js, said, sizeof(said));
        POC_LOG("show: tv: after cross again: %s", said);
      }
      demo_sleep(5000);
    }
    show_about();
    POC_LOG("show: tv: the app's full screen is %s on this page", app.fullscreen ? "on" : "OFF");
    POC_RESULT(1, "show: tv done in %.0f s; Sair from the menu", (poc_now_ms() - app.start_ms) / 1e3);
    demo_main(demo_do_ui, "menu-do?quit");
    return;
  }
#endif
#ifdef SHOW_YT_LET  // the same with the ad blocker off for the run (put back before Sair): is it what stops the film?
  const int adblock = settings.adblock;
  demo_main(demo_do_set, "adblock%3D0");
  POC_LOG("show: the ad blocker is off for this run (it was %s)", adblock ? "on" : "off");
  demo_sleep(1500);
#endif
  before = demo_loads();
  demo_main(demo_do_load, "https://www.youtube.com/watch?v=aqz-KE-bpKQ");
  demo_wait_load(before, 25000);
  for(int i = 0; i < 40; i++) {
    demo_ask(PAGE, demo_consent_js, answer, sizeof(answer));
    if(!strcmp(answer, "rejected")) {
      demo_sleep(3000);
      continue;
    }
    demo_ask(PAGE, demo_ad_js, answer, sizeof(answer));
    if(!strcmp(answer, "video")) {
      break;
    }
    POC_LOG("show: the video page: %s", answer);
    demo_sleep(1000);
  }
#ifdef SHOW_YT_QUALITY  // make SHOW_YT_QUALITY=hd1080: that quality instead of 2160p (does the film stop there too?)
  static const char quality_js[] =
    "(function () { var p = document.getElementById('movie_player');"
    " if (p && p.setPlaybackQualityRange) p.setPlaybackQualityRange('" SHOW_YT_QUALITY "', '" SHOW_YT_QUALITY "');"
    " var v = document.querySelector('video'); if (v && v.paused) v.play();"
    " return p && p.getPlaybackQuality ? p.getPlaybackQuality() : 'no player'; })()";
#else
  const char* const quality_js = demo_quality_js;
#endif
  demo_ask(PAGE, quality_js, answer, sizeof(answer));
  demo_play_video();
  demo_ask(PAGE, quality_js, answer, sizeof(answer));
  POC_LOG("show: the video's quality, playing: %s", answer);
  show_about();
  const double t0 = poc_now_ms();
  for(int i = 0; i < 18; i++) {
    demo_ask(PAGE, show_yt_js, answer, sizeof(answer));
    POC_LOG("show: yt at %.0f s: %s", (poc_now_ms() - t0) / 1e3, answer);
    if(i == 1 || i == 3) {  // the functions turned on by name (2026-11-11): used once, and what they said
      char said[300];
      demo_ask(PAGE, i == 1
               ? "(function () { window.__t = {}; try { navigator.clipboard.writeText('bps5').then(function () { __t.clip = 'written'; }, function (e) { __t.clip = 'refused ' + e.name; }); } catch (e) { __t.clip = 'threw ' + e; }"
                 " try { var c = new OffscreenCanvas(64, 64).getContext('2d'); c.fillRect(0, 0, 8, 8); __t.off = 'drew, pixel ' + c.getImageData(1, 1, 1, 1).data[3]; } catch (e) { __t.off = 'threw ' + e; }"
                 " try { navigator.permissions.query({name: 'notifications'}).then(function (st) { __t.perm = st.state; }, function (e) { __t.perm = 'refused ' + e.name; }); } catch (e) { __t.perm = 'threw ' + e; } return 'asked'; })()"
               : "(function () { var t = window.__t || {}; return 'clipboard ' + t.clip + '; OffscreenCanvas ' + t.off + '; permissions ' + t.perm; })()",
               said, sizeof(said));
      POC_LOG("show: new functions: %s", said);
    }
#ifdef SHOW_VEIL
    // make SHOW=1 SHOW_YT=1 SHOW_VEIL=1 (2026-11-14, the user's films: a black box over nearly all of the
    // video whenever the subtitles came or the cursor touched the seek bar). Two ways a video's hole gets
    // painted into an intermediate surface: an element with a backdrop-filter over it (30-40 s), then a
    // filter on the player around it (40-50 s). The title's video line says the video's place and how
    // often it moved (0 is right: the page stands still); here, how much of the screen is transparent
    // (the hole: it must stay as large with the filter on).
    if(i >= 5 && i <= 10) {
      char said[300] = "";
      if(i == 6) {
        demo_ask(PAGE, "(function () { var d = document.createElement('div'); d.id = 'bps5veil';"
                 " d.style.cssText = 'position:fixed;left:30%;top:55%;width:40%;height:12%;z-index:2147483647;"
                 "background:rgba(0,0,0,.4);backdrop-filter:blur(6px);-webkit-backdrop-filter:blur(6px);"
                 "color:#fff;font:28px sans-serif;text-align:center';"
                 " d.textContent = 'backdrop-filter over the video'; document.documentElement.appendChild(d);"
                 " var v = document.querySelector('video'), r = v ? v.getBoundingClientRect() : null;"
                 " return 'on; backdrop-filter ' + getComputedStyle(d).backdropFilter + '; the video at ' +"
                 " (r ? [r.left | 0, r.top | 0, r.width | 0, r.height | 0].join(',') : 'none'); })()",
                 said, sizeof(said));
        POC_LOG("show: veil: an element with a backdrop-filter over the video: %s", said);
      } else if(i == 8) {
        demo_ask(PAGE, "(function () { var d = document.getElementById('bps5veil'); if (d) d.remove();"
                 " var p = document.getElementById('movie_player'); if (p) p.style.filter = 'brightness(1)';"
                 " return p ? getComputedStyle(p).filter : 'no player'; })()", said, sizeof(said));
        POC_LOG("show: veil: the element is gone; a filter on the player around the video: %s", said);
      } else if(i == 10) {
        demo_ask(PAGE, "(function () { var p = document.getElementById('movie_player'); if (p) p.style.filter = '';"
                 " return 'off'; })()", said, sizeof(said));
        POC_LOG("show: veil: the filter is %s", said);
      }
      demo_sleep(1500);
      demo_main(demo_do_holes, "");
      POC_LOG("show: veil: at step %d, %lu of the screen's %d pixels are transparent (the video's hole)", i,
              veil_transparent, DISPLAY_WIDTH * DISPLAY_HEIGHT);
    }
#endif
    if(i == 5 || i == 17) {  // the player's own debug information ("copy debug info"), 25 s in and at the end
      for(int part = 0; part < 7; part++) {
        char script[420], said[1200];
        snprintf(script, sizeof(script),
                 "(function (n) { var p = document.getElementById('movie_player'), t = ''; try { t = p.getDebugText ? p.getDebugText(true) : ''; } catch (e) { t = 'threw ' + e; }"
                 " if (!t) try { t = JSON.stringify(p.getVideoStats ? p.getVideoStats() : {}); } catch (e) { t = 'no stats ' + e; }"
                 " return String(t).slice(n * 1000, n * 1000 + 1000); })(%d)", part);
        if(!demo_ask(PAGE, script, said, sizeof(said))) {
          break;
        }
        POC_LOG("show: debug %d.%d %s", i, part, said);
      }
    }
    demo_sleep(5000);
  }
  // What the page asked the servers that matter for (the film's, the player's check of the browser), and its errors.
  for(int i = 0; i < 70; i++) {
    char script[2600];
    snprintf(script, sizeof(script),
             "(function (i) { if (!window.__bps5lines) { var d = window.__bps5diag || {errors: ['no diag script in the page'], res: {}, order: []};"
             " window.__bps5lines = d.order.map(function (k) { var b = d.res[k];"
             " return k + ': ' + b.n + 'x, status ' + JSON.stringify(b.st) + ', from ' + Math.round(b.first / 1000) + ' to ' + Math.round(b.last / 1000) + ' s, ' + Math.round(b.bytes / 1024) + ' KB, longest ' + Math.round(b.slow) + ' ms'; })"
             " .concat(d.errors); }"
             " return i < window.__bps5lines.length ? window.__bps5lines[i].slice(0, 440) : ''; })(%d)", i);
    if(!demo_ask(PAGE, script, answer, sizeof(answer))) {
      break;
    }
    POC_LOG("show: asked %s", answer);
  }
  // 8K (the user, 17:5x): asked at 17:55 with a film that has it (sLprVF6d7Ug): the player offers this browser
  // up to hd2160; isTypeSupported: VP9 at 7680x4320 yes, AV1 no. YouTube's 8K is AV1.
#ifdef SHOW_YT_LET
  if(adblock) {
    demo_main(demo_do_set, "adblock%3D1");
    POC_LOG("show: the ad blocker is on again");
    demo_sleep(1000);
  }
#endif
  POC_RESULT(1, "show: done in %.0f s; Sair from the menu", (poc_now_ms() - app.start_ms) / 1e3);
  demo_main(demo_do_ui, "menu-do?quit");
}
#else
static void
show_tour(void) {
  char answer[256];
  char theme[sizeof(settings.theme)];
  char script[128];
  unsigned before;
  const int system_keyboard = settings.system_keyboard;

  snprintf(theme, sizeof(theme), "%s", settings.theme);
  POC_LOG("show: starts %.0f ms after the app; the theme is %s", poc_now_ms() - app.start_ms, theme);
  demo_main(show_do_one_tab, "");

  // 1. The start page: its tiles under the cursor (the neon light after it).
  demo_main(demo_do_start, "");
  demo_sleep(2500);
  input_set_cursor(DISPLAY_WIDTH / 2.0f, DISPLAY_HEIGHT * 0.82f);
  for(int i = 0; i < 5; i++) {
    snprintf(script, sizeof(script), "document.querySelectorAll('.tile')[%d]", i);
    if(!demo_glide_to(PAGE, script, 650)) {
      break;
    }
    demo_sleep(450);
  }
  demo_sleep(600);

  // 2. YouTube: a 2160p60 film (Big Buck Bunny, CC BY), the player's own
  // statistics over it, then the whole screen.
  before = demo_loads();
  demo_main(demo_do_load, SHOW_VIDEO);
  demo_wait_load(before, 25000);
  for(int i = 0; i < 40; i++) {
    demo_ask(PAGE, demo_consent_js, answer, sizeof(answer));
    if(!strcmp(answer, "rejected")) {
      demo_sleep(3000);
      continue;
    }
    demo_ask(PAGE, demo_ad_js, answer, sizeof(answer));
    if(!strcmp(answer, "video")) {
      break;
    }
    POC_LOG("show: the video page: %s", answer);
    demo_sleep(1000);
  }
  demo_ask(PAGE, demo_quality_js, answer, sizeof(answer));
  demo_play_video();
  demo_ask(PAGE, demo_quality_js, answer, sizeof(answer));  // again, now that it plays
  POC_LOG("show: the video's quality, playing: %s", answer);
  demo_sleep(5000);
  demo_ask(PAGE, show_stats_js, answer, sizeof(answer));
  POC_LOG("show: %s", answer);
  demo_glide(DISPLAY_WIDTH * 0.86f, DISPLAY_HEIGHT * 0.90f, 700);
  demo_sleep(9000);
  demo_ask(PAGE, demo_video_js, answer, sizeof(answer));
  POC_LOG("show: the video in the page: %s", answer);
  demo_main(demo_do_ui, "fullscreen");
  demo_sleep(10000);
  demo_ask(PAGE, demo_video_js, answer, sizeof(answer));
  POC_LOG("show: the video on the whole screen: %s", answer);
  demo_main(demo_do_ui, "fullscreen");
  demo_sleep(1200);
  demo_ask(PAGE, show_stats_off_js, answer, sizeof(answer));
  POC_LOG("show: the video %s", answer);
  demo_sleep(800);

  // 3. Three sites, each in a tab of its own, as they are for the user.
  show_tab("web.telegram.org", "https://web.telegram.org/k/", 20000, 9000);
  show_tab("web.whatsapp.com", "https://web.whatsapp.com/", 25000, 14000);
  show_tab("//x.com", "https://x.com/home", 20000, 5000);
  show_scroll(16, 2500);
  demo_sleep(1200);
  show_scroll(-16, 1200);
  demo_sleep(800);
  // The tabs back and forth: each is there at once, as it was.
  static const int order[] = {0, 1, 2, 3, 1, 3};
  for(int tab : order) {
    snprintf(script, sizeof(script), "document.querySelectorAll('#strip .tab')[%d]", tab);
    if(demo_glide_to(BAR, script, 500)) {
      demo_sleep(150);
      demo_click();
    }
    demo_sleep(1700);
  }

  // 4. The menus: options, L2's and R2's, the tabs' carousel (the touch pad).
  demo_glide(DISPLAY_WIDTH * 0.5f, DISPLAY_HEIGHT * 0.6f, 600);
  demo_event(INPUT_QUIT, 0, 0, 0);
  demo_sleep(1500);
  static const int moves[][2] = {{1, 0}, {1, 0}, {1, 0}, {0, 1}, {-1, 0}, {-1, 0}};
  for(const int* m : moves) {
    demo_dpad(m[0], m[1]);
    demo_sleep(550);
  }
  demo_sleep(900);
  demo_event(INPUT_QUIT, 0, 0, 0);
  demo_sleep(900);
  show_radial(0);
  show_radial(1);
  demo_event(INPUT_TOUCHPAD, 0, 0, 0);
  demo_sleep(1500);
  demo_dpad(-1, 0);
  demo_sleep(800);
  demo_dpad(-1, 0);
  demo_sleep(800);
  demo_dpad(1, 0);
  demo_sleep(900);
  demo_event(INPUT_ENTER, 0, 0, 0);
  demo_sleep(1800);

  // 5. A new tab; the browser's own keyboard, an address typed with its
  // suggestions; the page: the right stick, the D-pad's ring, the reading mode.
  demo_main(demo_do_ui, "newtab");
  demo_sleep(1800);
  settings.system_keyboard = 0;  // the browser's own (for the tour only: put back below, never saved)
  demo_main(show_do_keys, "");
  demo_sleep(1300);
  show_type("en.wikipedia.org/wiki/PlayStation_5", 85);
  demo_sleep(900);
  before = demo_loads();
  demo_ask(KEYS, "(function () { kb.done(); return 'done'; })()", answer, sizeof(answer));
  settings.system_keyboard = system_keyboard;
  demo_wait_load(before, 20000);
  demo_sleep(1500);
  show_scroll(14, 1600);
  demo_sleep(500);
  for(int i = 0; i < 5; i++) {
    demo_dpad(0, 1);
    demo_sleep(550);
  }
  demo_sleep(500);
  demo_main(demo_do_ui, "reader");
  demo_sleep(2200);
  show_scroll(12, 1500);
  demo_sleep(900);
  demo_event(INPUT_BACK, 0, 0, 0);
  demo_sleep(1300);

  // 6. The settings: the sections that say nothing of the user.
  show_settings("geral", 2400);
  demo_main(demo_do_settings, "aspeto");
  demo_sleep(1300);
  static const char* const themes[] = {"oceano", "rubi", "champanhe"};
  for(const char* name : themes) {
    snprintf(script, sizeof(script), "document.querySelector('.theme[data-theme=%s]')", name);
    if(demo_glide_to(PAGE, script, 650)) {
      demo_sleep(200);
      demo_click();
      demo_sleep(1400);
    }
  }
  snprintf(script, sizeof(script), "theme%%3D%s", theme);  // the user's own again
  demo_main(demo_do_set, script);
  demo_sleep(1200);
  show_settings("comando", 2600);
  show_settings("conteudo", 2400);
  show_settings("privacidade", 2200);
  show_settings("extensoes", 2200);
  show_settings("sistema", 3000);
  show_settings("acerca", 2600);

  // 7. The end: options, the menu, its Sair.
  demo_event(INPUT_QUIT, 0, 0, 0);
  demo_sleep(2200);
  POC_RESULT(1, "show: done in %.0f s; Sair from the menu", (poc_now_ms() - app.start_ms) / 1e3);
  demo_main(demo_do_ui, "menu-do?quit");
}
#endif
#endif

static void*
demo_thread(void*) {
#ifdef SHOW
  show_tour();
#else
  demo_tour();
#endif
  return 0;
}

static void
demo(void) {
  static bool started;
  pthread_t thread;

  pthread_mutex_lock(&demo_state.lock);
  demo_state.loads++;
  pthread_cond_broadcast(&demo_state.cond);
  pthread_mutex_unlock(&demo_state.lock);
  if(started) {
    return;
  }
  started = true;
  pthread_create(&thread, 0, demo_thread, 0);
  pthread_detach(thread);
}

// 2026-11-17: a test is started by the first page loaded (demo()). When that page never came (the start page
// stalled twice the same day, the network process silent from the first second), no test ran, nothing asked for
// "Sair", and the browser stayed open on the console until it was closed by hand. A test build closes itself
// now: no page DEMO_GUARD_S seconds in, it says so and asks for "Sair" as a test's end does. It sleeps a second
// at a time and ends with the first page.
#define DEMO_GUARD_S 90
static void*
demo_guard(void*) {
  for(int s = 0; s < DEMO_GUARD_S; s++) {
    usleep(1000000);
    pthread_mutex_lock(&demo_state.lock);
    const unsigned loads = demo_state.loads;
    pthread_mutex_unlock(&demo_state.lock);
    if(loads) {
      return 0;
    }
  }
  POC_RESULT(0, "no page loaded in the first %d s: the test did not start; Sair from the menu", DEMO_GUARD_S);
  demo_main(demo_do_ui, "menu-do?quit");
  return 0;
}

#ifdef DEMO
// Before the settings are read: a first start, with sample favorites.
static void
demo_reset(void) {
  static const char favorites[] =
    "[{\"url\": \"https://www.youtube.com/\", \"title\": \"YouTube\"},"
    " {\"url\": \"https://pt.wikipedia.org/\", \"title\": \"Wikipédia\"},"
    " {\"url\": \"https://www.blender.org/\", \"title\": \"Blender\"},"
    " {\"url\": \"https://www.openstreetmap.org/\", \"title\": \"OpenStreetMap\"},"
    " {\"url\": \"https://github.com/\", \"title\": \"GitHub\"}]";
  mkdir("/user/homebrew/browserps5", 0755);
#ifdef APP_RN
  mkdir("/user/homebrew/browserps5-rn", 0755);
#endif
  mkdir(DATA_DIR, 0755);
  unlink(SETTINGS_FILE);
  unlink(HISTORY_FILE);
  FILE* f = fopen(FAVORITES_FILE, "w");
  if(f) {
    fputs(favorites, f);
    fclose(f);
  }
  POC_LOG("demo: its own data in %s, a first start", DATA_DIR);
}
#endif
#endif


// The page's navigations (each tab's: the same; p == page(PAGE) is the one
// in front).
static void
page_client_install(WKPageRef p) {
  WKPageNavigationClientV3 pageClient;
  memset(&pageClient, 0, sizeof(pageClient));
  pageClient.base.version = 3;
  pageClient.base.clientInfo = &app.panes[PAGE];
  pageClient.decidePolicyForNavigationAction = decidePageNavigation;
  pageClient.decidePolicyForNavigationResponse = decidePageResponse;
  pageClient.didStartProvisionalNavigation = didStartProvisionalNavigation;
  pageClient.didCommitNavigation = didCommitNavigation;
  pageClient.didFinishNavigation = didFinishNavigation;
#ifdef APP_RN
  pageClient.didFailProvisionalNavigation = didFailProvisionalNavigation;
#else
  pageClient.didFailProvisionalNavigation = didFailNavigation;
#endif
  pageClient.didFailNavigation = didFailNavigation;
  pageClient.webProcessDidCrash = webProcessDidCrash;
  pageClient.navigationActionDidBecomeDownload = navigationActionDidBecomeDownload;
  pageClient.navigationResponseDidBecomeDownload = navigationResponseDidBecomeDownload;
  WKPageSetPageNavigationClient(p, &pageClient.base);
#ifdef APP_RN
  find_client_install(p);
  upload_client_install(p);
#endif
}


#ifdef APP_RN
// ---------------------------------------------------------------------------
// BrowserPS5 RN (2026-11-11): find in the page, the reading mode, the
// translation and the private tabs: what a desktop browser has and RN had
// not. All from the menu's second row of tiles (menu.html, data-rn).

static bool
tab_is_private(void) {
  return tab_current >= 0 && tab_current < tab_count && tabs[tab_current].priv;
}

// Is it a tab's page (a private one's too)?
static bool
tab_page(WKPageRef p) {
  for(int i = 0; i < tab_count; i++) {
    if(WKViewGetPage(tabs[i].view) == p) {
      return true;
    }
  }
  return false;
}

// A download's parts are asked for through its tab's page: is it still a tab's, and not a private one's?
static bool
multi_page_fine(WKPageRef p) {
  for(int i = 0; i < tab_count; i++) {
    if(WKViewGetPage(tabs[i].view) == p) {
      return !tabs[i].priv;
    }
  }
  return false;
}

// Find in the page (WebKit's own: every match highlighted, the current one
// selected and scrolled to). The keyboard asks for the text; then R1 goes to
// the next match and circle ends it (find_input()).
static struct {
  bool active;
  char text[256];
} find_state;

static void
find_run(void) {
  WKStringRef s = string(find_state.text);
  WKPageFindString(page(PAGE), s,
                   kWKFindOptionsCaseInsensitive | kWKFindOptionsWrapAround | kWKFindOptionsShowHighlight
                   | kWKFindOptionsShowOverlay, 500);
  WKRelease(s);
}

static void
find_start(const char* text) {
  snprintf(find_state.text, sizeof(find_state.text), "%s", text);
  if(!find_state.text[0]) {
    return;
  }
  find_state.active = true;
  POC_LOG("find: looking for %zu byte(s) in the page", strlen(find_state.text));
  find_run();
}

static void
find_end(void) {
  if(find_state.active) {
    find_state.active = false;
    WKPageHideFindUI(page(PAGE));
    POC_LOG("find: ended");
  }
}

static void
find_open(void) {
  app.ime_pane = PAGE;
  app.ime_replace = true;
  app.ime_enter = false;
  find_typing = true;
  POC_LOG("find: the keyboard for the text to look for");
  int err = open_keyboard("search", "search", IME_TEXT, IME_ENTER_SEARCH, find_state.text);
  if(err && err != 1) {
    find_typing = false;
  }
}

static void
find_found(WKPageRef p, WKStringRef, unsigned count, const void*) {
  char text[160];
  if(p != page(PAGE)) {
    return;
  }
  if(!strcmp(lang_code(), "en")) {
    snprintf(text, sizeof(text), "%u match%s \xc2\xb7 R1: next \xc2\xb7 \xe2\x97\x8b: close", count, count == 1 ? "" : "es");
  } else if(lang_en()) {  // (the pages' other languages: lang_c.h)
    snprintf(text, sizeof(text), LM("%u found \xc2\xb7 R1: next \xc2\xb7 \xe2\x97\x8b: close"), count);
  } else {
    snprintf(text, sizeof(text), "%u resultado%s \xc2\xb7 R1: seguinte \xc2\xb7 \xe2\x97\x8b: fechar", count, count == 1 ? "" : "s");
  }
  bar_notify(text, "search");
}

static void
find_failed(WKPageRef p, WKStringRef, const void*) {
  if(p != page(PAGE)) {
    return;
  }
  find_state.active = false;
  bar_notify(lang_en() ? LM("Nothing found in this page") : "Nada encontrado nesta página", "search");
}

static void
find_client_install(WKPageRef p) {
  WKPageFindClientV0 client;
  memset(&client, 0, sizeof(client));
  client.base.version = 0;
  client.didFindString = find_found;
  client.didFailToFindString = find_failed;
  WKPageSetPageFindClient(p, &client.base);
}

// While a find is on: R1 the next match, circle (or L1) ends it. True if taken.
static bool
find_input(const input_event_t* e) {
  if(!find_state.active || app.keys_open || app.menu_open || app.dialog_open) {
    return false;
  }
  if(e->kind == INPUT_FORWARD) {
    find_run();
    return true;
  }
  if(e->kind == INPUT_BACK) {
    find_end();
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Files for a page (2026-11-11): <input type="file">, the paper clip of
// WhatsApp, Telegram, X, a form's "choose a file". WebKit asks the app
// (runOpenPanel) and until now nobody answered: nothing happened. The
// dialog's files mode (dialog.html, dialog.files()) shows the places a file
// of the user's can be in, the PS5's own screenshots and video clips first,
// and what is chosen goes back to the page. Only the network process reads
// the file (a payload: it sees these folders; the page's WebProcess in the
// title does not have to).
static struct {
  WKOpenPanelResultListenerRef listener;  // kept while the dialog asks
  WKArrayRef allowed;                     // the MIME types the page takes (WebKit wants them back)
  bool multiple;
  char dir[512];                          // where the last one came from
} upload;

#define CAPTURES_PHOTO "/user/av_contents/photo"
#define CAPTURES_VIDEO "/user/av_contents/video"

static bool
upload_pending(void) {
  return upload.listener != 0;
}

// The folders a file may be chosen from: the console's storage for the
// user's files, the drives, and the PS5's captures. Nothing of the system.
static bool
upload_path_ok(const char* path) {
  static const char* const roots[] = {"/data", "/mnt/usb", "/mnt/ext", "/user/av_contents"};
  if(strstr(path, "/../") || strstr(path, "/./") || strlen(path) >= 1000) {
    return false;
  }
  size_t n = strlen(path);
  if(n >= 3 && !strcmp(path + n - 3, "/..")) {
    return false;
  }
  for(size_t i = 0; i < sizeof(roots) / sizeof(*roots); i++) {
    size_t r = strlen(roots[i]);
    if(!strncmp(path, roots[i], r) && (path[r] == 0 || path[r] == '/' || (i > 0 && i < 3 && isdigit((unsigned char)path[r])))) {
      return true;
    }
  }
  return false;
}

static void
upload_end(bool cancel) {
  if(upload.listener) {
    if(cancel) {
      WKOpenPanelResultListenerCancel(upload.listener);
      POC_LOG("upload: no file chosen");
    }
    WKRelease(upload.listener);
    upload.listener = 0;
  }
  if(upload.allowed) {
    WKRelease(upload.allowed);
    upload.allowed = 0;
  }
}

static bool
is_folder(const char* path) {
  struct stat st;
  return !stat(path, &st) && S_ISDIR(st.st_mode);
}

static void
runOpenPanel(WKPageRef p, WKFrameRef, WKOpenPanelParametersRef parameters,
             WKOpenPanelResultListenerRef listener, const void*) {
  if(p != page(PAGE)) {  // a tab behind asks nothing
    WKOpenPanelResultListenerCancel(listener);
    return;
  }
  upload_end(true);  // one asked before and never answered
  upload.listener = (WKOpenPanelResultListenerRef)WKRetain(listener);
  upload.allowed = WKOpenPanelParametersCopyAllowedMIMETypes(parameters);
  upload.multiple = WKOpenPanelParametersGetAllowsMultipleFiles(parameters);

  // What the page takes, for the dialog's line under the title.
  char accept[200] = "";
  WKArrayRef types = WKOpenPanelParametersCopyAcceptedMIMETypes(parameters);
  for(size_t i = 0; types && i < WKArrayGetSize(types) && i < 6; i++) {
    WKTypeRef item = WKArrayGetItemAtIndex(types, i);
    if(item && WKGetTypeID(item) == WKStringGetTypeID()) {
      char one[64];
      WKStringGetUTF8CString((WKStringRef)item, one, sizeof(one));
      size_t n = strlen(accept);
      snprintf(accept + n, sizeof(accept) - n, "%s%s", n ? ", " : "", one);
    }
  }
  if(types) {
    WKRelease(types);
  }
  if(!upload.dir[0] || !is_folder(upload.dir)) {
    snprintf(upload.dir, sizeof(upload.dir), "%s",
             is_folder(CAPTURES_PHOTO) ? CAPTURES_PHOTO : settings.downloads_dir);
  }

  text_t t = {};
  bool first = true;
  char path[32], label[32];
  text_printf(&t, "dialog.files({multiple: %s, accept: ", upload.multiple ? "true" : "false");
  text_json(&t, accept);
  text_add(&t, ", dir: ");
  text_json(&t, upload.dir);
  text_add(&t, ", places: [");
  if(is_folder(CAPTURES_PHOTO)) {
    place_json(&t, &first, lang_en() ? LM("PS5 screenshots") : "Capturas da PS5", CAPTURES_PHOTO, "image");
  }
  if(is_folder(CAPTURES_VIDEO)) {
    place_json(&t, &first, lang_en() ? LM("PS5 video clips") : "Vídeos da PS5", CAPTURES_VIDEO, "video-clip");
  }
  place_json(&t, &first, "Transferências", settings.downloads_dir, "arrow-download");
  place_json(&t, &first, "Multimédia", settings.media_dir, "video-clip");
  place_json(&t, &first, "Armazenamento interno", "/data", "storage");
  for(int i = 0; i < 8; i++) {
    snprintf(path, sizeof(path), "/mnt/usb%d", i);
    if(mounted(path)) {
      snprintf(label, sizeof(label), "USB %d", i + 1);
      place_json(&t, &first, label, path, "usb-stick");
    }
  }
  for(int i = 0; i < 2; i++) {
    snprintf(path, sizeof(path), "/mnt/ext%d", i);
    if(mounted(path)) {
      snprintf(label, sizeof(label), "Disco externo %d", i + 1);
      place_json(&t, &first, label, path, "storage");
    }
  }
  text_printf(&t, "], theme: '%s'})", theme_now());
  POC_LOG("upload: the page asks for %s (%s)", upload.multiple ? "files" : "a file", accept[0] ? accept : "any kind");
  dialog_show(&t, 0);
}

// ---------------------------------------------------------------------------
// A page's alert(), confirm() and prompt() (2026-11-11). While one is open
// the page's script waits, and with it the whole WebProcess: the dialog's
// own page must be in another one to show the question at all. So these are
// only there with the interface in a WebProcess of its own (ui_split, the
// marker UI_PROCESS_MARKER); without it nothing changes: alert() returns at
// once, confirm() says no, prompt() gives nothing, as before. The dialog's
// ask mode (dialog.html, dialog.ask()) shows the site, the text, a field
// for prompt(), "OK" and "Cancelar" (circle). A page that asks more than
// five times in ten seconds gets no more windows for half a minute.
static struct {
  int kind;           // 0 none, 1 alert, 2 confirm, 3 prompt, 4 RN: a site asks to show notifications
  WKTypeRef listener; // kept until answered
  double times[5];    // when the last five were asked
  double quiet_until; // no windows before this (a page asking on and on)
} ask;

#ifdef APP_RN
static void notify_permission_done(WKTypeRef request, bool ok);
#endif

static void
ask_answer(int kind, WKTypeRef listener, bool ok, const char* text) {
#ifdef APP_RN
  if(kind == 4) {
    notify_permission_done(listener, ok);
    return;
  }
#endif
  if(kind == 1) {
    WKPageRunJavaScriptAlertResultListenerCall((WKPageRunJavaScriptAlertResultListenerRef)listener);
  } else if(kind == 2) {
    WKPageRunJavaScriptConfirmResultListenerCall((WKPageRunJavaScriptConfirmResultListenerRef)listener, ok);
  } else if(kind == 3) {
    WKStringRef value = ok ? string(text ? text : "") : 0;  // none: prompt() gives null
    WKPageRunJavaScriptPromptResultListenerCall((WKPageRunJavaScriptPromptResultListenerRef)listener, value);
    if(value) {
      WKRelease(value);
    }
  }
}

static void
ask_end(bool ok, const char* text) {
  if(!ask.kind) {
    return;
  }
  const int kind = ask.kind;
  WKTypeRef listener = ask.listener;
  ask.kind = 0;
  ask.listener = 0;
  ask_answer(kind, listener, ok, text);
  WKRelease(listener);
  POC_LOG("ask: answered (%s)", ok ? "OK" : "cancelled");
}

// The page that asked is gone (its WebProcess ended): nothing to answer to
// but the listener, and the dialog closes.
static void ask_native_close(void);

static bool ask_abandoned;  // the answer being given is nobody's: not one to remember (notify_permission_done())

static void
ask_abandon(void) {
  if(ask.kind) {
    ask_abandoned = true;
    ask_end(false, 0);
    ask_abandoned = false;
    if(ask_native.open) {
      ask_native_close();
    } else {
      dialog_hide();
    }
  }
}

// The app's own window (ask_box.h): made, shown; closed; what the controller does to it.
static bool
ask_native_show(int kind, const char* host, const char* text, const char* value) {
  char title[320];
  ask_box_t box = {};

  if(kind == 4) {  // a permission: the site's name alone, and what each button does
    snprintf(title, sizeof(title), "%s", host);
  } else if(host && *host) {
    snprintf(title, sizeof(title), lang_en() ? LM("%s says") : "%s diz", host);
  } else {
    snprintf(title, sizeof(title), "%s", lang_en() ? LM("This page says") : "Esta página diz");
  }
  const char* const yes = kind != 4 ? "OK" : lang_en() ? LM("Allow") : "Permitir";
  const char* const no = kind == 1 ? 0 : kind == 4 ? (lang_en() ? LM("Block") : "Bloquear") : lang_en() ? LM("Cancel") : "Cancelar";
  if(ask_box_draw(&box, "/data/homebrew/" TITLE_ID "/fonts", title, text, kind == 3 ? value : 0, yes, no, app.accent)) {
    POC_LOG("ask: the window could not be drawn (no font?)");
    return false;
  }
  pthread_mutex_lock(&app.lock);
  free(ask_native.box.pixels);
  ask_native.box = box;
  ask_native.has_value = kind == 3;
  snprintf(ask_native.value, sizeof(ask_native.value), "%s", value ? value : "");
  ask_native.shown_ms = poc_now_ms();
  ask_native.open = true;
  app.dirty = true;
  pthread_cond_signal(&app.cond);
  pthread_mutex_unlock(&app.lock);
  ui_sound(LINK_SOUND_OPEN);
  return true;
}

static void
ask_native_close(void) {
  pthread_mutex_lock(&app.lock);
  ask_native.open = false;
  free(ask_native.box.pixels);
  ask_native.box.pixels = 0;
  app.dirty = true;
  pthread_cond_signal(&app.cond);
  pthread_mutex_unlock(&app.lock);
}

// While it is open the page waits and nothing else of the browser can draw:
// cross (or Enter) is OK, circle is Cancelar (an alert has only OK: both
// close it). A press within 300 ms of its showing was meant for the page.
static bool
ask_native_input(const input_event_t* e) {
  if(!ask_native.open) {
    return false;
  }
  const bool ok = e->kind == INPUT_BUTTON_DOWN || e->kind == INPUT_ENTER;
  if((ok || e->kind == INPUT_BACK) && poc_now_ms() - ask_native.shown_ms >= 300) {
    if(e->kind == INPUT_BUTTON_DOWN) {
      app.swallow_up = true;
    }
    static char value[sizeof(ask_native.value)];
    snprintf(value, sizeof(value), "%s", ask_native.value);
    ui_sound(ok ? LINK_SOUND_SELECT : LINK_SOUND_CLOSE);
    ask_native_close();
    ask_end(ok, ask_native.has_value ? value : 0);
  }
  return e->kind != INPUT_MOVE;  // the cursor still moves
}

// True if the dialog shows the question (the listener is answered later).
static bool
ask_show(WKPageRef p, int kind, WKStringRef message, WKStringRef value, WKTypeRef listener) {
  const double now = poc_now_ms();
  if(p != page(PAGE) || ask.kind || app.dialog_open || app.keys_open || pause_state.away || now < ask.quiet_until) {
    return false;  // a tab behind, one open already, or a page asking on and on: the answer is "no"
  }
  memmove(&ask.times[0], &ask.times[1], sizeof(ask.times) - sizeof(ask.times[0]));
  ask.times[4] = now;
  if(ask.times[0] > 0 && now - ask.times[0] < 10000) {
    ask.quiet_until = now + 30000;
    bar_notify(lang_en() ? LM("This page keeps opening windows: none for half a minute")
                         : "Esta página não pára de abrir janelas: meio minuto sem nenhuma", "info");
    POC_LOG("ask: five in ten seconds, no more for 30 s");
    return false;
  }
  static char text[4096];
  static char given[1024];
  char url[2048], host[256];
  text[0] = 0;
  given[0] = 0;
  if(message) {
    WKStringGetUTF8CString(message, text, sizeof(text));
  }
  if(value) {
    WKStringGetUTF8CString(value, given, sizeof(given));
  }
  take_url(WKPageCopyActiveURL(p), url, sizeof(url));
  if(!ui_split) {  // the dialog's page could not draw (it shares the waiting WebProcess): the app's own window
    if(!ask_native_show(kind, url_is_web(url) ? url_host(url, host, sizeof(host)) : "", text, given)) {
      return false;
    }
    ask.kind = kind;
    ask.listener = WKRetain(listener);
    POC_LOG("ask: the page's %s, %zu bytes of text, in the app's own window", kind == 1 ? "alert" : kind == 2 ? "confirm" : "prompt",
            strlen(text));
    return true;
  }
  text_t t = {};
  text_printf(&t, "dialog.ask({kind: '%s', host: ", kind == 1 ? "alert" : kind == 2 ? "confirm" : "prompt");
  text_json(&t, url_is_web(url) ? url_host(url, host, sizeof(host)) : "");
  text_add(&t, ", text: ");
  text_json(&t, text);
  text_add(&t, ", value: ");
  text_json(&t, given);
  text_printf(&t, ", theme: '%s'})", theme_now());
  if(t.failed) {
    free(t.s);
    return false;
  }
  ask.kind = kind;
  ask.listener = WKRetain(listener);
  POC_LOG("ask: the page's %s, %zu bytes of text", kind == 1 ? "alert" : kind == 2 ? "confirm" : "prompt", strlen(text));
  dialog_show(&t, 0);
  return true;
}

static void
runJavaScriptAlert(WKPageRef p, WKStringRef text, WKFrameRef, WKSecurityOriginRef,
                   WKPageRunJavaScriptAlertResultListenerRef listener, const void*) {
  if(!ask_show(p, 1, text, 0, listener)) {
    ask_answer(1, listener, false, 0);
  }
}

static void
runJavaScriptConfirm(WKPageRef p, WKStringRef text, WKFrameRef, WKSecurityOriginRef,
                     WKPageRunJavaScriptConfirmResultListenerRef listener, const void*) {
  if(!ask_show(p, 2, text, 0, listener)) {
    ask_answer(2, listener, false, 0);
  }
}

static void
runJavaScriptPrompt(WKPageRef p, WKStringRef text, WKStringRef value, WKFrameRef, WKSecurityOriginRef,
                    WKPageRunJavaScriptPromptResultListenerRef listener, const void*) {
  if(!ask_show(p, 3, text, value, listener)) {
    ask_answer(3, listener, false, 0);
  }
}

#ifdef APP_RN
// ---------------------------------------------------------------------------
// BrowserPS5 RN (2026-11-11 22:10): a site's own Notification on an engine
// that has it (the "full" one; another never calls any of this). notify_js
// stays for the sites of its list (granted from the start, no question).
// Any other https site that asks, with the user's press behind it, gets the
// app's window: "<site> / Este site quer mostrar notificações", Permitir (✕)
// or Bloquear (○). The answer is kept by origin in DATA_DIR/notify-sites.txt
// ("+https://site" or "-https://site", a line each; delete a line to be asked
// again) and told to the engine, which gives it to the pages as
// Notification.permission. A notice of an allowed site is the PS5's toast,
// as notify_js's, and its tab's dot; the tab coming in front is its click.
// A private tab is never asked and never allowed: nothing of it is kept.
#define NOTIFY_SITES_FILE DATA_DIR "/notify-sites.txt"
#define NOTIFY_SITES_MAX 128
static struct {
  char origin[192];
  bool allowed;
} notify_known[NOTIFY_SITES_MAX];
static int notify_known_count;
static char notify_asking[192];  // the origin the open question is about

static void
notify_known_load(void) {
  static bool done;
  if(done) {
    return;
  }
  done = true;
  FILE* f = fopen(NOTIFY_SITES_FILE, "r");
  if(!f) {
    return;
  }
  char line[256];
  while(notify_known_count < NOTIFY_SITES_MAX && fgets(line, sizeof(line), f)) {
    line[strcspn(line, "\r\n")] = 0;
    if((line[0] != '+' && line[0] != '-') || strncmp(line + 1, "https://", 8) || strlen(line + 1) >= sizeof(notify_known[0].origin)) {
      continue;
    }
    notify_known[notify_known_count].allowed = line[0] == '+';
    strcpy(notify_known[notify_known_count].origin, line + 1);
    notify_known_count++;
  }
  fclose(f);
}

static int
notify_known_find(const char* origin) {
  notify_known_load();
  for(int i = 0; i < notify_known_count; i++) {
    if(!strcmp(notify_known[i].origin, origin)) {
      return i;
    }
  }
  return -1;
}

// The user's answer for a site: kept (the oldest goes when the list is full), the file written whole.
static void
notify_known_set(const char* origin, bool allowed) {
  int i = notify_known_find(origin);
  if(i < 0) {
    if(notify_known_count == NOTIFY_SITES_MAX) {
      memmove(&notify_known[0], &notify_known[1], sizeof(notify_known) - sizeof(notify_known[0]));
      notify_known_count--;
    }
    i = notify_known_count++;
    snprintf(notify_known[i].origin, sizeof(notify_known[i].origin), "%s", origin);
  }
  notify_known[i].allowed = allowed;
  FILE* f = fopen(NOTIFY_SITES_FILE ".new", "w");
  if(!f) {
    POC_LOG("notify: %s could not be written (errno %d)", NOTIFY_SITES_FILE, errno);
    return;
  }
  for(int k = 0; k < notify_known_count; k++) {
    fprintf(f, "%c%s\n", notify_known[k].allowed ? '+' : '-', notify_known[k].origin);
  }
  fclose(f);
  rename(NOTIFY_SITES_FILE ".new", NOTIFY_SITES_FILE);
}

// A site's answer forgotten: it is asked again the next time.
static void
notify_known_forget(const char* origin) {
  const int i = notify_known_find(origin);
  if(i < 0) {
    return;
  }
  memmove(&notify_known[i], &notify_known[i + 1], (size_t)(notify_known_count - i - 1) * sizeof(notify_known[0]));
  notify_known_count--;
  FILE* f = fopen(NOTIFY_SITES_FILE ".new", "w");
  if(f) {
    for(int k = 0; k < notify_known_count; k++) {
      fprintf(f, "%c%s\n", notify_known[k].allowed ? '+' : '-', notify_known[k].origin);
    }
    fclose(f);
    rename(NOTIFY_SITES_FILE ".new", NOTIFY_SITES_FILE);
  }
  if(notify_manager) {
    WKStringRef name = string(origin);
    WKSecurityOriginRef o = WKSecurityOriginCreateFromString(name);
    WKTypeRef one[] = {o};
    WKArrayRef list = WKArrayCreate(one, 1);
    WKNotificationManagerProviderDidRemoveNotificationPolicies(notify_manager, list);
    WKRelease(list);
    WKRelease(o);
    WKRelease(name);
  }
}

// The settings page's list ("Notificações dos sites"): [{"origin": ..., "allowed": ...}], and its "forget".
// False with an engine that has no Notification of its own (no site can ask: no list to show).
static bool
notify_sites_json(text_t* t) {
#ifdef ENGINE_DIR
  if(strcmp(ENGINE_DIR, "full")) {
    return false;
  }
#else
  return false;
#endif
  notify_known_load();
  text_add(t, "[");
  for(int i = 0; i < notify_known_count; i++) {
    text_add(t, i ? ", {\"origin\": " : "{\"origin\": ");
    text_json(t, notify_known[i].origin);
    text_add(t, notify_known[i].allowed ? ", \"allowed\": true}" : ", \"allowed\": false}");
  }
  text_add(t, "]");
  return true;
}

static void
notify_forget_command(const char* argument) {
  char origin[192];
  address_decode(argument, origin, sizeof(origin));
  notify_known_forget(origin);
  POC_LOG("notify: %s forgotten from the settings (asked again the next time)", origin);
}

// The question's answer (ask_answer(), kind 4): to the page, and kept unless it was nobody's.
static void
notify_permission_done(WKTypeRef request, bool ok) {
  if(ok) {
    WKNotificationPermissionRequestAllow((WKNotificationPermissionRequestRef)request);
  } else {
    WKNotificationPermissionRequestDeny((WKNotificationPermissionRequestRef)request);
  }
  if(ask_abandoned || !notify_asking[0]) {
    POC_LOG("notify: %s was not answered: refused this once", notify_asking);
    return;
  }
  notify_known_set(notify_asking, ok);
  if(notify_manager) {
    WKStringRef name = string(notify_asking);
    WKSecurityOriginRef origin = WKSecurityOriginCreateFromString(name);
    WKNotificationManagerProviderDidUpdateNotificationPolicy(notify_manager, origin, ok);
    WKRelease(origin);
    WKRelease(name);
  }
  POC_LOG("notify: %s: notifications %s by the user", notify_asking, ok ? "allowed" : "blocked");
  bar_notify(ok ? (lang_en() ? LM("Notifications allowed for this site") : "Notificações permitidas para este site")
                : (lang_en() ? LM("Notifications blocked for this site") : "Notificações bloqueadas para este site"), "info");
}

// A page calls Notification.requestPermission().
static void
decideNotificationPermission(WKPageRef p, WKSecurityOriginRef origin, WKNotificationPermissionRequestRef request, const void*) {
  char name[192], host[256];
  int i = 0;

  take_string(WKSecurityOriginCopyToString(origin), name, sizeof(name));
  while(i < tab_count && WKViewGetPage(tabs[i].view) != p) {
    i++;
  }
  if(strncmp(name, "https://", 8) || i == tab_count || tabs[i].priv) {
    WKNotificationPermissionRequestDeny(request);
    POC_LOG("notify: %s asked to show notifications: refused (%s)", name,
            i < tab_count && tabs[i].priv ? "a private tab" : "not an https page of a tab");
    return;
  }
  const int known = notify_known_find(name);
  if(known >= 0) {
    if(notify_known[known].allowed) {
      WKNotificationPermissionRequestAllow(request);
    } else {
      WKNotificationPermissionRequestDeny(request);
    }
    return;
  }
  if(p != page(PAGE) || ask.kind || app.dialog_open || app.keys_open || pause_state.away || poc_now_ms() < ask.quiet_until
     || !ask_native_show(4, url_host(name, host, sizeof(host)),
                         lang_en() ? LM("This site wants to show notifications.") : "Este site quer mostrar notificações.", 0)) {
    WKNotificationPermissionRequestDeny(request);  // a tab behind or a window open already: refused this once, asked again another time
    POC_LOG("notify: %s asked to show notifications: no window now, refused this once", name);
    return;
  }
  snprintf(notify_asking, sizeof(notify_asking), "%s", name);
  ask.kind = 4;
  ask.listener = WKRetain(request);
  POC_LOG("notify: %s asks to show notifications: the app's window", name);
}

// The engine has a notification to show (a site the user allowed).
static void
notify_native_show(WKPageRef p, WKNotificationRef n, const void*) {
  static char title[1200], body[1200], icon[1200];
  char name[192], host[256];
  int i = 0;

  take_string(WKSecurityOriginCopyToString(WKNotificationGetSecurityOrigin(n)), name, sizeof(name));
  const int known = notify_known_find(name);
  if(known < 0 || !notify_known[known].allowed) {
    POC_LOG("notify: a notice of %s, which the user did not allow: not shown", name);
    return;
  }
  while(i < tab_count && WKViewGetPage(tabs[i].view) != p) {
    i++;
  }
  take_string(WKNotificationCopyTitle(n), title, sizeof(title));
  take_string(WKNotificationCopyBody(n), body, sizeof(body));
  take_string(WKNotificationCopyIconURL(n), icon, sizeof(icon));
  notify_clean(title, 120);
  notify_clean(body, 200);
  notify_clean(icon, 1000);
  const uint64_t id = WKNotificationGetID(n);
  if(i < tab_count) {
    tabs[i].notice = id;
  }
  notify_toast(i < tab_count ? i : -1, url_host(name, host, sizeof(host)), title, body, icon);
  if(notify_manager) {
    WKNotificationManagerProviderDidShowNotification(notify_manager, id);  // the page's "show" event
  }
}

// What the engine tells a new WebProcess: every site answered for so far.
static WKDictionaryRef
notify_native_permissions(const void*) {
  WKMutableDictionaryRef all = WKMutableDictionaryCreate();
  notify_known_load();
  for(int i = 0; i < notify_known_count; i++) {
    WKStringRef key = string(notify_known[i].origin);
    WKBooleanRef value = WKBooleanCreate(notify_known[i].allowed);
    WKDictionarySetItem(all, key, value);
    WKRelease(value);
    WKRelease(key);
  }
  return all;  // the engine takes it over
}

#ifdef SHOW_PROBE_NOTIFY
// The probe's two steps on the main thread: "Permitir", as the cross button does; the test site forgotten.
static void
show_notify_answer(const char*) {
  POC_LOG("show: probe: notify: the window is %s (question kind %d)", ask_native.open ? "open" : "NOT open", ask.kind);
  if(ask.kind == 4) {
    ask_native_close();
    ask_end(true, 0);
  }
}

static void
show_notify_forget(const char* origin) {
  notify_known_forget(origin);
}
#endif

static void
notify_native_install(WKContextRef context) {
  static WKNotificationProviderV0 provider;
  provider.base.version = 0;
  provider.show = notify_native_show;
  provider.notificationPermissions = notify_native_permissions;
  notify_manager = WKContextGetNotificationManager(context);
  if(notify_manager) {
    WKNotificationManagerSetProvider(notify_manager, &provider.base);
  }
}
#endif

// What the dialog's ask mode sent. True if the command was one of its.
static bool
ask_command(const char* command, const char* text) {
  if(!strncmp(command, "ask-ok", 6) && (command[6] == 0 || command[6] == '?')) {
    ask_end(true, text);
    dialog_hide();
    return true;
  }
  if(!strncmp(command, "ask-cancel", 10) && (command[10] == 0 || command[10] == '?')) {
    ask_end(false, 0);
    dialog_hide();
    return true;
  }
  return false;
}

// 2026-11-11: the window's frame, for window.outerWidth/outerHeight and
// screenX/Y (they were 0x0: no window around the page): the whole screen.
static WKRect
getWindowFrame(WKPageRef, const void*) {
  return WKRectMake(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
}

static void
upload_client_install(WKPageRef p) {
  WKPageUIClientV6 client;
  memset(&client, 0, sizeof(client));
  client.base.version = 0;
  client.runOpenPanel = runOpenPanel;
  if(!bisect_off('f')) {
    client.getWindowFrame = getWindowFrame;
  }
  {  // 2026-11-11: always: without ui_split the app draws the window itself (ask_native_show())
    client.base.version = 6;
    client.runJavaScriptAlert = runJavaScriptAlert;
    client.runJavaScriptConfirm = runJavaScriptConfirm;
    client.runJavaScriptPrompt = runJavaScriptPrompt;
  }
#ifdef APP_RN
  client.decidePolicyForNotificationPermissionRequest = decideNotificationPermission;
#endif
  WKPageSetPageUIClient(p, &client.base);
}

typedef struct upload_entry {
  char name[256];
  long long size;
  long long when;
  bool folder;
} upload_entry_t;

static int
upload_order(const void* a, const void* b) {
  const upload_entry_t* x = (const upload_entry_t*)a;
  const upload_entry_t* y = (const upload_entry_t*)b;
  if(x->folder != y->folder) {
    return x->folder ? -1 : 1;
  }
  if(x->folder || x->when == y->when) {
    return strcasecmp(x->name, y->name);
  }
  return x->when > y->when ? -1 : 1;  // the newest file first
}

#ifdef APP_RN
// 2026-11-14 (a gaming browser: what was just captured in a game goes to X, Discord or WhatsApp without a
// phone). The PS5 keeps its captures many folders deep (photo/NPXS40087/<the game's id>/<number>/<file>): in the
// two places of the dialog that are theirs they come as ONE list, the newest first, each called by its game's
// name and its time (xps5/captures.c lists them as XPS5 does). A screenshot of a game in HDR is a
// .jxr, which no site takes: left out, and counted in the log. False when the folder does not read: the plain
// list then.
static const char* games_lang(void);

static bool
upload_captures(const char* dir) {
  capture_t* list = (capture_t*)malloc(CAPTURES_MAX * sizeof(*list));
  const bool video = !strcmp(dir, CAPTURES_VIDEO);
  const int count = list ? captures_list(list, CAPTURES_MAX) : -1;
  const size_t root = strlen(dir);
  char game[16] = "", game_name[128] = "";
  int shown = 0, hdr = 0;
  text_t t = {};

  if(count < 0) {
    free(list);
    return false;
  }
  snprintf(upload.dir, sizeof(upload.dir), "%s", dir);
  text_add(&t, "dialog.entries(");
  text_json(&t, dir);
  text_add(&t, ", [], [");
  for(int i = 0; i < count; i++) {
    const capture_t* c = &list[i];
    if(c->kind == CAPTURE_JXR) {
      hdr += !video;
      continue;
    }
    if((c->kind == CAPTURE_MP4 || c->kind == CAPTURE_WEBM) != video || strncmp(c->path, dir, root) || c->path[root] != '/') {
      continue;
    }
    char id[16] = "", label[200], when[32] = "";
    const char* app = strstr(c->path, "/" CAPTURES_APP "/");
    if(app && strcspn(app + strlen(CAPTURES_APP) + 2, "/") == 9) {
      snprintf(id, sizeof(id), "%.9s", app + strlen(CAPTURES_APP) + 2);
    }
    if(id[0] && strcmp(id, game)) {
      snprintf(game, sizeof(game), "%s", id);
      if(!games_name(id, games_lang(), game_name, sizeof(game_name))) {
        game_name[0] = 0;
      }
    }
    const time_t at = (time_t)c->time;
    struct tm tm;
    if(localtime_r(&at, &tm)) {
      strftime(when, sizeof(when), "%d/%m %H:%M", &tm);
    }
    snprintf(label, sizeof(label), "%.120s \xc2\xb7 %s", id[0] && game_name[0] ? game_name : video ? "PS5" : "PS5", when);
    text_add(&t, shown ? ", {n: " : "{n: ");
    text_json(&t, c->path + root + 1);
    text_add(&t, ", l: ");
    text_json(&t, label);
    text_printf(&t, ", s: %lld, t: %lld}", c->size, c->time);
    shown++;
  }
  text_add(&t, "])");
  POC_LOG("upload: the PS5's %s as one list, the newest first: %d of %d capture(s) shown, %d in HDR (.jxr) left out",
          video ? "clips" : "screenshots", shown, count, hdr);
  free(list);
  text_run(DIALOG, &t);
  return true;
}
#endif

// entries?<dir>: its folders and its files, for the dialog's list.
static void
upload_entries(const char* dir) {
  enum { MAX = 600 };
#ifdef APP_RN
  if((!strcmp(dir, CAPTURES_PHOTO) || !strcmp(dir, CAPTURES_VIDEO)) && upload_captures(dir)) {
    return;
  }
#endif
  upload_entry_t* list = (upload_entry_t*)calloc(MAX, sizeof(*list));
  int count = 0;
  text_t t = {};

  if(list && upload_path_ok(dir)) {
    DIR* d = opendir(dir);
    for(struct dirent* e; d && count < MAX && (e = readdir(d));) {
      char full[1400];
      struct stat st;
      if(e->d_name[0] == '.' || strlen(e->d_name) >= sizeof(list[0].name)) {
        continue;
      }
      snprintf(full, sizeof(full), "%s/%s", dir, e->d_name);
      if(stat(full, &st) || (!S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode))) {
        continue;
      }
      snprintf(list[count].name, sizeof(list[count].name), "%s", e->d_name);
      list[count].folder = S_ISDIR(st.st_mode);
      list[count].size = (long long)st.st_size;
      list[count].when = (long long)st.st_mtime;
      count++;
    }
    if(d) {
      closedir(d);
    }
    snprintf(upload.dir, sizeof(upload.dir), "%s", dir);
  }
  if(list) {
    qsort(list, (size_t)count, sizeof(*list), upload_order);
  }
  text_add(&t, "dialog.entries(");
  text_json(&t, dir);
  text_add(&t, ", [");
  int folders = 0;
  for(int i = 0; i < count && list[i].folder; i++, folders++) {
    text_add(&t, i ? ", " : "");
    text_json(&t, list[i].name);
  }
  text_add(&t, "], [");
  for(int i = folders; i < count; i++) {
    text_add(&t, i > folders ? ", {n: " : "{n: ");
    text_json(&t, list[i].name);
    text_printf(&t, ", s: %lld, t: %lld}", list[i].size, list[i].when);
  }
  text_add(&t, "])");
  free(list);
  text_run(DIALOG, &t);
}

// A path as a file: address.
static WKURLRef
file_url(const char* path) {
  static const char hex[] = "0123456789ABCDEF";
  char url[3200];
  size_t n = (size_t)snprintf(url, sizeof(url), "file://");
  for(const unsigned char* c = (const unsigned char*)path; *c && n + 4 < sizeof(url); c++) {
    if(isalnum(*c) || *c == '/' || *c == '-' || *c == '_' || *c == '.' || *c == '~') {
      url[n++] = (char)*c;
    } else {
      url[n++] = '%';
      url[n++] = hex[*c >> 4];
      url[n++] = hex[*c & 15];
    }
  }
  url[n] = 0;
  return WKURLCreateWithUTF8CString(url);
}

// What the dialog's files mode sent. True if the command was one of its.
static bool
upload_command(const char* command, const char* text) {
  if(!strncmp(command, "entries?", 8)) {
    if(upload_pending()) {
      upload_entries(text);
    }
    return true;
  }
  if(!strcmp(command, "upload-cancel") || !strncmp(command, "upload-cancel?", 14)) {
    upload_end(true);
    dialog_hide();
    return true;
  }
  if(strncmp(command, "upload?", 7)) {
    return false;
  }
  if(!upload_pending()) {
    dialog_hide();
    return true;
  }
  enum { MAX = 16 };
  WKTypeRef urls[MAX];
  size_t count = 0;
  char* paths = strdup(text);
  for(char* path = paths, *next; path && *path && count < MAX; path = next) {
    next = strchr(path, '\n');
    if(next) {
      *next++ = 0;
    }
    struct stat st;
    if(upload_path_ok(path) && !stat(path, &st) && S_ISREG(st.st_mode)) {
      urls[count++] = file_url(path);
      POC_LOG("upload: a file of %lld bytes for the page", (long long)st.st_size);
      if(!upload.multiple) {
        break;
      }
    } else {
      POC_LOG("upload: a path that is not taken");
    }
  }
  free(paths);
  if(!count) {
    upload_end(true);
  } else {
    WKArrayRef files = WKArrayCreate(urls, count);
    WKArrayRef none = WKArrayCreate(0, 0);
    WKOpenPanelResultListenerChooseFiles(upload.listener, files, upload.allowed ? upload.allowed : none);
    WKRelease(none);
    WKRelease(files);
    for(size_t i = 0; i < count; i++) {
      WKRelease(urls[i]);
    }
    upload_end(false);
  }
  dialog_hide();
  return true;
}

// The tab closed last comes back, in a tab of its own (its page loads again:
// the closed one's is gone).
static void
tab_reopen(void) {
  if(!closed_count) {
    return;
  }
  if(tab_count >= TABS_MAX) {
    bar_notify(lang_en() ? LM("No room for another tab") : "Não cabem mais separadores", "window-new");
    return;
  }
  char url[sizeof(closed_tabs[0])];
  snprintf(url, sizeof(url), "%s", closed_tabs[--closed_count]);
  POC_LOG("tabs: the one closed last comes back (%d more kept)", closed_count);
  if(!tab_new(url)) {
    closed_count++;  // still there for another try
  }
  bar_tabs();
}

// While the link menu or the reading mode is open over the page in front
// (overlay_tab): circle (or L1) closes it instead of going back a page. The
// page says when it is closed another way (browserps5:overlay?0). True if taken.
static bool
overlay_input(const input_event_t* e) {
  if(e->kind != INPUT_BACK || overlay_tab < 0 || app.keys_open || app.menu_open || app.dialog_open) {
    return false;
  }
  const bool here = tab_current >= 0 && tab_current < tab_count && tabs[tab_current].id == overlay_tab;
  overlay_tab = -1;
  if(!here) {
    return false;
  }
  run_script(PAGE, "window.__bps5overlayClose && __bps5overlayClose()", 0, ignore_result);
  POC_LOG("overlay: closed with circle");
  return true;
}

// The reading mode's answer (ext/reader.js): "on", "off" or "none".
static void
reader_said(WKTypeRef result, WKErrorRef, void*) {
  char text[16] = "";
  if(result && WKGetTypeID(result) == WKStringGetTypeID()) {
    WKStringGetUTF8CString((WKStringRef)result, text, sizeof(text));
  }
  POC_LOG("reader: %s", text[0] ? text : "(no answer)");
  if(strcmp(text, "on") && strcmp(text, "off")) {
    bar_notify(lang_en() ? LM("No article found in this page") : "Não encontrei um artigo nesta página", "document");
  }
}

// The page through Google's translator, into the browser's language.
static void
translate_page(void) {
  static char to[8192];
  char url[4096];
  static const char hex[] = "0123456789ABCDEF";
  take_url(WKPageCopyActiveURL(page(PAGE)), url, sizeof(url));
  if(!url_is_web(url)) {
    bar_notify(lang_en() ? LM("Open a page first") : "Abre uma página primeiro", "globe");
    return;
  }
  size_t n = (size_t)snprintf(to, sizeof(to), "https://translate.google.com/translate?sl=auto&tl=%s&u=",
                              !strcmp(lang_code(), "pt") ? "pt-PT" : lang_code());
  for(const unsigned char* c = (const unsigned char*)url; *c && n + 4 < sizeof(to); c++) {
    if(isalnum(*c) || *c == '-' || *c == '_' || *c == '.' || *c == '~') {
      to[n++] = (char)*c;
    } else {
      to[n++] = '%';
      to[n++] = hex[*c >> 4];
      to[n++] = hex[*c & 15];
    }
  }
  to[n] = 0;
  POC_LOG("translate: the page through the translator");
  load_url(to);
}
#endif

static void
invalid_message(WKStringRef name) {
  char text[160] = "?";
  if(name) {
    WKStringGetUTF8CString(name, text, sizeof(text));
  }
  POC_LOG("FAIL WebKit refused a message from the WebProcess: %s (it ends the WebProcess)", text);
}

// 2026-11-09 18:3x: WebKit's own full screen (the user, of the CSS one in
// ext/fullscreen.js: "a patch": two progress bars, the video bent after a
// few resizes). A site's requestFullscreen() (after a press, WebKit checks)
// comes here from the PlayStation view: the page's element is WebKit's
// :fullscreen; the app only gives the page the whole screen, no bar.
static void
view_enter_fullscreen(WKViewRef view, const void* info) {
  if(info != &app.panes[PAGE] || view != app.panes[PAGE].view) {
    return;
  }
  POC_LOG("full screen: WebKit's");
  app.native_fullscreen = true;
  last_gesture_ms = poc_now_ms();  // WebKit took it only after a press
  fullscreen_set(true);
}

static void
view_exit_fullscreen(WKViewRef view, const void* info) {
  (void)view;
  // 2026-11-09 21:2x: the user could not leave a full screen after the PS
  // button (Circle gave the bar back, the page stayed full): every exit logged.
  POC_LOG("full screen: WebKit's ends (the app's %s)", app.native_fullscreen ? "too" : "had ended");
  if(info != &app.panes[PAGE] || !app.native_fullscreen) {
    return;
  }
  app.native_fullscreen = false;
  fullscreen_set(false);
}

// Circle, the menu: WebKit's full screen ends by WebKit (its exit comes back
// to view_exit_fullscreen); the bar is back at once either way.
static bool
native_fullscreen_exit(void) {
  if(!app.native_fullscreen) {
    return false;
  }
  app.native_fullscreen = false;
  const bool webkit_full = app.panes[PAGE].view && WKViewIsFullScreen(app.panes[PAGE].view);
  POC_LOG("full screen: the app ends it; WebKit's view %s", webkit_full ? "is full: asked to end" : "is not full");
  if(webkit_full) {
    WKViewRequestExitFullScreen(app.panes[PAGE].view);
  }
  fullscreen_set(false);
  return true;
}

// A view for a pane (its client info): hidden until shown.
static WKViewRef
view_create(pane_t* pane, int width, int height, WKContextRef context,
            WKWebsiteDataStoreRef data, WKPreferencesRef preferences,
            WKUserContentControllerRef content) {
  WKPageConfigurationRef config = WKPageConfigurationCreate();
  WKViewClientV0 client;

  WKPageConfigurationSetContext(config, context);
  WKPageConfigurationSetWebsiteDataStore(config, data);
  if(preferences) {
    WKPageConfigurationSetPreferences(config, preferences);
  }
  if(content) {
    WKPageConfigurationSetUserContentController(config, content);
  }
  WKViewRef view = WKViewCreate(config);
  WKRelease(config);

  memset(&client, 0, sizeof(client));
  client.base.version = 0;
  client.base.clientInfo = pane;
#ifndef GPU_COMPOSITING  // the frames come from the WebProcess's compositor
  client.setViewNeedsDisplay = setViewNeedsDisplay;
#endif
  client.setCursor = setCursor;
  client.enterFullScreen = view_enter_fullscreen;
  client.exitFullScreen = view_exit_fullscreen;
  client.closeFullScreen = view_exit_fullscreen;
  WKViewSetViewClient(view, &client.base);
  WKViewSetSize(view, WKSizeMake(width, height));
  return view;
}


// A view: its place on the screen, where its rows go (pixels, stride
// apart), and for the page its own preferences and scripts.
static void
create_pane(int i, const char* name, int left, int top, int width, int height,
            uint32_t* pixels, int stride, WKContextRef context, WKWebsiteDataStoreRef data,
            WKPreferencesRef preferences, WKUserContentControllerRef content) {
  pane_t* pane = &app.panes[i];

  pane->name = name;
  pane->left = left;
  pane->top = top;
  pane->width = width;
  pane->height = height;
  pane->pixels = pixels;
  pane->stride = stride;
  pane->view = view_create(pane, width, height, context, data, preferences, content);
  WKViewSetVisible(pane->view, true);
  WKViewSetActive(pane->view, true);
}


// The bar's, the menu's, the dialog's and the keyboard's navigations.
static void
ui_client(int i, WKPageNavigationDidFinishNavigationCallback loaded) {
  WKPageNavigationClientV0 client;

  memset(&client, 0, sizeof(client));
  client.base.version = 0;
  client.base.clientInfo = &app.panes[i];
  client.decidePolicyForNavigationAction = decideBarNavigation;
  client.didFinishNavigation = loaded;
  client.webProcessDidCrash = webProcessDidCrash;
  WKPageSetPageNavigationClient(page(i), &client.base);
}


static void
load_html(int pane, const char* html) {
  WKStringRef s = string(html);
  WKPageLoadHTMLString(page(pane), s, 0);
  WKRelease(s);
}


#ifdef APP_RN
// The interface's pages, each into its pane: at the start, or once the
// page's WebProcess is there when the interface has its own (ui_split_tick()).
static void
ui_panes_load(void) {
  load_html(BAR, chrome_html);
  load_html(KEYS, keyboard_html);
  load_html(MENU, menu_html);
  load_html(DIALOG, dialog_html);
  load_html(RADIAL, radial_html);
  load_html_at(SWITCHER, tabs_html, "file://" TABS_DIR "/");
}
#endif


int
main(void) {
  pthread_t display;
  pthread_t timer;
  int err;

  app.start_ms = poc_now_ms();
  app.first_paint_ms = -1;
  app.pressed = -1;
  app.hover = PAGE;
  app.bar_height = BAR_HEIGHT;
  app.nav_fresh = true;
#ifdef GPU_COMPOSITING
  // Before any preferences exist: WebKit's WebPreferencesPlayStation.cpp
  // turns accelerated compositing on with it (poc/webkit/build-webkit.sh).
  setenv("BROWSERPS5_GPU", "1", 1);
#endif
  crash_log_install();
#ifdef APP_RN
  // 2026-11-12: a title
  // of the media category may not listen, so its two services for this process (the GPU's owner on 9040, the
  // WebProcess's host on 9060) are listened for here, and the title calls in (common/title_link.h). Before
  // anything asks for either. With a title that listens itself (category 0), the ports are taken: nothing runs.
  {
    static const int title_ports[] = {9040, 9060};
    const int relayed = title_link_relay_start(title_ports, 2);
    if(relayed) {
      POC_LOG("title link: the title does not listen (a media title): this process stands in for %d of its ports", relayed);
    }
  }
  // BrowserPS5 RN's own data folder, before the start page is written in it
  // (v1's has been there since its first start).
  mkdir("/user/homebrew/browserps5-rn", 0755);
  mkdir(DATA_DIR, 0755);
  // A cross-origin isolated page (WhatsApp Web) stays in the title's
  // WebProcess with its service worker (webkit/ui/WebPageProxy.cpp.patch,
  // 2026-11-10 20:50): in a process of its own it waited on the worker
  // forever after login. v1 does not set it and still moves such pages.
#ifndef SPEED_ISOLATED_MOVES  // wa_plan_moves.h: moved, as in v1
  setenv("BROWSERPS5_ISOLATED_STAYS", "1", 1);
#endif
  // 2026-11-10 22:20: one site's service workers per WebProcess. Only
  // WhatsApp Web's run in the title's (its in a payload WebProcess: that
  // process stopped after 8 s and the browser froze); Telegram's and the
  // others' in payload WebProcesses (webkit/ui/WebProcessPool.cpp.patch).
  setenv("BROWSERPS5_SW_TITLE_DOMAIN", "whatsapp.com", 1);
#endif
  POC_LOG("pid %d, WKContextCreateWithConfiguration at 0x%lx", getpid(),
          (unsigned long)(uintptr_t)&WKContextCreateWithConfiguration);

  if(!(app.screen=(uint32_t*)calloc(DISPLAY_WIDTH * DISPLAY_HEIGHT, 4))
     || !(app.frame=(uint32_t*)calloc(DISPLAY_WIDTH * DISPLAY_HEIGHT, 4))
     || !(app.keys=(uint32_t*)calloc(DISPLAY_WIDTH * KEYS_HEIGHT, 4))
     || !(app.menu=(uint32_t*)calloc(MENU_WIDTH * MENU_HEIGHT, 4))
     || !(app.dialog=(uint32_t*)calloc(DIALOG_WIDTH * DIALOG_HEIGHT, 4))
     || !(app.radial=(uint32_t*)calloc(RADIAL_SIZE * RADIAL_SIZE, 4))
     || !(app.switcher_px=(uint32_t*)calloc(DISPLAY_WIDTH * DISPLAY_HEIGHT, 4))) {
    POC_PERROR("calloc");
    return 1;
  }
  if(panel_make(&menu_panel, MENU_WIDTH, MENU_HEIGHT, PANEL_RADIUS, 40, 14, 0.5f)
     || panel_make(&dialog_panel, DIALOG_WIDTH, DIALOG_HEIGHT, PANEL_RADIUS, 56, 20, 0.6f)
     || panel_make(&radial_panel, RADIAL_SIZE, RADIAL_SIZE, RADIAL_SIZE / 2, 48, 16, 0.55f)) {
    POC_PERROR("panel_make");
    return 1;
  }
  cursors_init();

  // The settings, before anything is shown.
#ifdef DEMO
  demo_reset();
#endif
  mkdir(DATA_DIR, 0755);  // made by the NetworkProcess too, after this
  mkdir(TABS_DIR, 0755);
  // 2026-11-10: WebKit's temporary files (poc/webkit/storage/
  // FileSystemPOSIX.cpp.patch: the PS5 has no /tmp; IndexedDB's Blobs, which
  // WhatsApp Web needs). The same path as the patch; what a fall left there
  // goes, before any WebKit process runs.
#ifdef APP_RN
  // 2026-11-17: its parents first. They are the earlier BrowserPS5's data folder, not RN's: on a console that
  // never had it the mkdir below failed, the engine had nowhere for its temporary files, and 1.0.0-rc12 closed
  // a second after it started (1.0.0-rc13's installer makes the folder too).
  mkdir("/user/homebrew/browserps5", 0755);
  mkdir("/user/homebrew/browserps5/data", 0755);
#endif
  mkdir(WEBKIT_TEMP_DIR, 0755);
  if(DIR* dir = opendir(WEBKIT_TEMP_DIR)) {
    while(struct dirent* entry = readdir(dir)) {
      if(entry->d_name[0] != '.') {
        char path[256];
        snprintf(path, sizeof(path), "%s/%s", WEBKIT_TEMP_DIR, entry->d_name);
        unlink(path);
      }
    }
    closedir(dir);
  }
  bool read = !settings_load(&settings, SETTINGS_FILE);
#ifdef APP_RN
  // 2026-11-14: on a console that never had the browser (no settings file) its language is the console's, where
  // there is a table for it, English for every other. It was Portuguese for everyone, and the choice of another
  // was in a welcome written in Portuguese. A file from before keeps what it says. Nothing is written here: the
  // welcome's end, or the first setting changed, writes the file. (Not in the video's tour: its texts are fixed.)
  {
    int console_lang = -1;
    const int lang_err = sceSystemServiceParamGetInt(1, &console_lang);  // SCE_SYSTEM_SERVICE_PARAM_ID_LANG
    const char* by = read ? "its settings say so" : lang_err ? "the console did not say: as before" : "the console's";
#ifndef DEMO
    if(!read && !lang_err) {
      snprintf(settings.lang, sizeof(settings.lang), "%s", lang_of_console(console_lang));
    }
#endif
    POC_LOG("language: the console's is %d (0x%x, that is \"%s\"); the browser's is \"%s\" (%s)", console_lang, (unsigned)lang_err,
            lang_err ? "?" : lang_of_console(console_lang), settings.lang, by);
  }
  engine_env_check();
#endif
  ext_load(&extensions, EXTENSIONS_DIR);
  ad_hosts_init();
  partial_clean();
#ifdef APP_RN
  downloads_from_folders();
#endif
  app.zoom = settings.zoom / 100.0;
  app.accent = settings_accent(theme_now());
  address_set_search(settings_engine(&settings)->prefix);
  POC_LOG("settings %s: theme %s, start %s/%s, search %s, zoom %d%%, ad blocker %s, "
          "D-pad %s, %d extension(s)", read ? "read" : "by default", settings.theme,
          settings.startup, settings.home_kind, settings.search, settings.zoom,
          settings.adblock ? "on" : "off", settings.dpad, extensions.count);

  if((err=display_open())) {
    POC_RESULT(0, "display_open: 0x%x", err);
    return 1;
  }
  WKRunLoopInitializeMain();

#ifdef ENGINE_DIR
  // 2026-11-11 12:35: the paths below are never used by this port (the
  // "plus" engine's first run started the usual NetworkProcess, whose
  // messages have other numbers: no page at all). The engine's library
  // reads this instead (webkit/runtime/ProcessLauncherPlayStation.own.inc).
  setenv("BROWSERPS5_PROCESS_DIR", "/user/homebrew/browserps5-rn/" ENGINE_DIR, 1);
#endif
  WKContextConfigurationRef contextConfig = WKContextConfigurationCreate();
#ifdef APP_RN
  // 2026-11-11: navigator.languages was empty (the port reads SYSTEM_LANGUAGE,
  // which a payload has not): the app's own language first, then English.
  {
    WKTypeRef languages[4];
    size_t n = 0;
    if(!lang_en()) {
      languages[n++] = string("pt-PT");
      languages[n++] = string("pt");
    }
#ifdef APP_RN
    {  // (the pages' other languages: "es-ES", "es" and so on)
      static const char* const more[][2] = {{"es", "es-ES"}, {"fr", "fr-FR"}, {"de", "de-DE"}, {"it", "it-IT"}, {"nl", "nl-NL"}};
      for(const auto& one : more) {
        if(!strcmp(lang_code(), one[0])) {
          languages[n++] = string(one[1]);
          languages[n++] = string(one[0]);
        }
      }
    }
#endif
    languages[n++] = string("en-US");
    languages[n++] = string("en");
    WKArrayRef list = WKArrayCreate(languages, n);
    if(!bisect_off('l'))
    WKContextConfigurationSetOverrideLanguages(contextConfig, list);
    WKRelease(list);
  }
#endif
  WKContextConfigurationSetWebProcessPath(contextConfig,
                                          string(WEB_PROCESS_PATH));
  WKContextConfigurationSetNetworkProcessPath(contextConfig,
                                              string(NETWORK_PROCESS_PATH));
  // One WebProcess for all the views, kept across sites: with process swaps
  // each new site started a WebProcess (12 in 4 minutes), and the UIProcess
  // crashed in one, on a CheckedPtr to a dead object (TV test 1,
  // 2026-11-03). Turning the swaps off was not enough (TV test 2): sites
  // with Cross-Origin-Opener-Policy still swap. The single process mode is
  // the first thing WebProcessPool::processForNavigationInternal checks.
  WKContextConfigurationSetProcessSwapsOnNavigation(contextConfig, false);
  WKContextConfigurationSetPrewarmsProcessesAutomatically(contextConfig,
                                                          false);
  WKContextRef context = WKContextCreateWithConfiguration(contextConfig);
  // 2026-11-09 18:5x: a WebProcess that sends a message WebKit refuses is
  // ended by WebKit with no word (the page crashed on YouTube's full screen
  // button): the message's name in the log.
  WKContextSetInvalidMessageFunction(invalid_message);
  ui_context = context;
#ifdef APP_RN
  notify_native_install(context);  // a site's own notifications: asked for and shown by the app
#endif
  WKContextSetUsesSingleWebProcess(context, true);
#ifdef APP_RN
  {
    // 2026-11-11 11:50, the console's first tour: on Wikipedia (default-src
    // 'self' ...) the link menu's "open" never came, and circle went back a
    // page with the menu open. The scripts of ours talk to the app from a
    // hidden frame (browserps5:<command>), and a site's Content-Security-Policy
    // stops that frame before the app is asked (frame-src, or default-src).
    // The scheme is ours alone and loads nothing: it is out of the policies.
    WKStringRef scheme = string("browserps5");
    WKContextRegisterURLSchemeAsBypassingContentSecurityPolicy(context, scheme);
    WKRelease(scheme);
  }
#endif
  WKContextRef chrome_context = context;  // the interface's panes: the same, unless RN's ui_split
#ifdef APP_RN
  ui_split = access(UI_PROCESS_MARKER, F_OK) == 0;
  if(ui_split) {
    chrome_context = WKContextCreateWithConfiguration(contextConfig);
    WKContextSetUsesSingleWebProcess(chrome_context, true);
    POC_LOG("the interface gets a WebProcess of its own (the ui-process marker); the pages keep the title's");
  }
#endif

  // Cookies and storage somewhere writable: by default the cookies went to
  // "cookie.jar.db" in the NetworkProcess's current directory, and YouTube's
  // consent page came back every time.
  WKWebsiteDataStoreConfigurationRef dataConfig =
    WKWebsiteDataStoreConfigurationCreate();
  WKWebsiteDataStoreConfigurationSetCookieStorageFile(dataConfig,
      string(DATA_DIR "/cookies.db"));
  WKWebsiteDataStoreConfigurationSetLocalStorageDirectory(dataConfig,
      string(DATA_DIR "/localstorage"));
  WKWebsiteDataStoreConfigurationSetIndexedDBDatabaseDirectory(dataConfig,
      string(DATA_DIR "/indexeddb"));
  WKWebsiteDataStoreConfigurationSetGeneralStorageDirectory(dataConfig,
      string(DATA_DIR "/storage"));
  // 2026-11-10: the service workers' registrations and their Cache API on the
  // disk too, in our folder (with none given, WebKit's default place).
  WKWebsiteDataStoreConfigurationSetServiceWorkerRegistrationDirectory(dataConfig,
      string(DATA_DIR "/serviceworkers"));
  WKWebsiteDataStoreConfigurationSetCacheStorageDirectory(dataConfig,
      string(DATA_DIR "/cachestorage"));
  // The pages' HTTP cache on the disk (docs/21-cache-no-disco.md). With no
  // folder given, the PlayStation port puts it in /app0/WebKitCache, which
  // is not ours to write: nothing was ever cached, and every visit fetched
  // all again (YouTube's scripts too). Its size is WebKit's, from the free
  // space (the PrimaryWebBrowser cache model: 1 GB with 16 GB or more free).
  // The setting is taken here, at the start.
  {
    long long total = 0, free_bytes = -1;
    if(system_disk(DATA_DIR, &total, &free_bytes)) {
      free_bytes = -1;
    }
    if(settings.disk_cache) {
      WKWebsiteDataStoreConfigurationSetNetworkCacheDirectory(dataConfig, string(CACHE_DIR));
      POC_LOG("disk cache: on, in %s (%lld MiB free)", CACHE_DIR,
              free_bytes < 0 ? -1LL : free_bytes / (1024 * 1024));
    } else {
      POC_LOG("disk cache: off (the setting)");
    }
  }
  // The WebProcess in the browser's title (2c-2, docs/05): libWebKit's
  // launcher (poc/webkit/process-launcher) asks the title's host first and
  // starts a payload if no title answers; BROWSERPS5_WEB_IN_TITLE=0 skips
  // the title. The setting is taken here, before the first WebProcess.
  if(!settings.web_in_title) {
    setenv("BROWSERPS5_WEB_IN_TITLE", "0", 1);
    POC_LOG("WebProcess in the title: off (the setting)");
  }
#ifdef WEB_IN_TITLE_OFF
  // make WEB_IN_TITLE_OFF=1 (2026-11-06 15:00, test builds only): the
  // WebProcess as a payload with the 2c-2 WebKit, without touching the
  // user's settings file.
  setenv("BROWSERPS5_WEB_IN_TITLE", "0", 1);
  POC_LOG("WebProcess in the title: off (test build)");
#endif
  WKWebsiteDataStoreRef data = WKWebsiteDataStoreCreateWithConfiguration(
      dataConfig);
  data_store = data;
#ifdef APP_RN
  // sw_only_js: the other sites' service workers of before (Telegram Web's)
  // go, once; WhatsApp's registers again on its next load.
  if(access(DATA_DIR "/sw-whatsapp-only", F_OK) != 0) {
    WKWebsiteDataStoreRemoveAllServiceWorkerRegistrations(data_store, 0, data_removed);
    FILE* mark = fopen(DATA_DIR "/sw-whatsapp-only", "w");
    if(mark) {
      fputs("the service workers of before removed (main.cpp, sw_only_js)\n", mark);
      fclose(mark);
    }
    POC_LOG("service workers: the ones of before removed, from now on WhatsApp's only");
  }
#endif

  // The page's own preferences (the settings change them; the bar and the
  // panels keep WebKit's) and the scripts that go into each of its pages.
  page_preferences = WKPreferencesCreate();
  WKPreferencesSetFullScreenEnabled(page_preferences, true);  // 18:3x: WebKit's own (view_enter_fullscreen)
  // 2026-11-09: no service workers. A page's worker runs in a second
  // WebProcess (a payload, not the title) that never gets its connections
  // (ipc-tcp "pairs 0"): the page's requests then wait for it until WebKit's
  // fetch timeout (~60 s each), and the browser stops navigating (YouTube on
  // the second visit, publico.pt; BrowserPS5-WebKit/README.md).
  {
    // 2026-11-10: on again. They now run in the page's own WebProcess, the
    // one in the title (poc/webkit/ui/WebProcessPool.cpp.patch): WhatsApp
    // Web needs them (without them it deletes and remakes its databases in a
    // loop, "erro na base de dados"). BROWSERPS5_NO_SW=1 in the environment
    // turns them off as before.
    // Off by default again at 17:30: with them on, the browser stopped after
    // Telegram Web in 2 of 4 runs. BROWSERPS5_SW=1 in the environment turns
    // them on, for the tests (the test builds always have them on).
    // 18:00, with WebKit d97fcf9b (ui/WebPageProxy.cpp.patch: no process swap
    // for COOP; ui/WebProcessPool.cpp.patch: one site's workers per process):
    // WhatsApp's QR, Telegram's QR, YouTube twice, publico.pt, Wikipedia and
    // WhatsApp again all load in 2 runs of 4. In the other 2 the page's
    // WebProcess stops whole right after Telegram Web loads (all its threads,
    // the probes too; not stopped by a signal: the launcher's stop watch).
    // Still off until that is found.
#if defined(SPEED_TEST) || defined(SW_ON)
    const bool sw = true;  // the test builds and make SW_ON=1: the UIProcess gets no env.txt
#else
    const bool sw = getenv("BROWSERPS5_SW") && getenv("BROWSERPS5_SW")[0] == '1';
#endif
    WKStringRef key = string("ServiceWorkersEnabled");
    WKPreferencesSetBoolValueForKeyForTesting(page_preferences, sw, key);
    WKRelease(key);
    POC_LOG("service workers: %s", sw ? "on (BROWSERPS5_SW=1), in the page's WebProcess" : "off");
    // Shared workers off (2026-11-10, Telegram Web at 17:10): one in the
    // page's WebProcess stopped the browser, and the title's WebProcess cannot
    // host one. Pages fall back to their own workers (Telegram does).
    WKStringRef shared = string("SharedWorkerEnabled");
    WKPreferencesSetBoolValueForKeyForTesting(page_preferences, false, shared);
    WKRelease(shared);
  }
#ifdef SPEED_TEST
  speed_guard();  // no first page in 90 s: the test build quits by itself
#endif
  // 2026-11-10: the Storage API (navigator.storage), on as in the Safari the
  // pages are told they talk to (WebKit has it on only for Cocoa, GTK and
  // WPE). WhatsApp Web stopped at "database error" without it. It reaches
  // each WebProcess's shared preferences with the full screen's sync below.
  static const char* const storage_api[] = {"StorageAPIEnabled", "StorageAPIEstimateEnabled"};
  for(const char* name : storage_api) {
    WKStringRef key = string(name);
    WKPreferencesSetBoolValueForKeyForTesting(page_preferences, true, key);
    WKRelease(key);
  }
#ifdef APP_RN
  // 2026-11-11: what
  // the engine has and keeps off outside Apple's ports, on as in the Safari
  // the pages are told they talk to: the Permissions API
  // (navigator.permissions), the asynchronous clipboard (navigator.clipboard;
  // the console has no clipboard: it keeps nothing) and OffscreenCanvas
  // (built into the plus engine, never turned on).
  static const char* const safari_has[] = {"PermissionsAPIEnabled", "AsyncClipboardAPIEnabled", "OffscreenCanvasEnabled"};
  for(const char* name : safari_has) {
    if(bisect_off('p')) {
      break;
    }
    WKStringRef key = string(name);
    WKPreferencesSetBoolValueForKeyForTesting(page_preferences, true, key);
    WKRelease(key);
  }
  // The "full" engine's WebGL and Web Audio (2026-11-11). Both fell at first
  // (20:40: a WebGL context crashed the WebProcess and the app with it; Web
  // Audio's start stopped the page for 32 s) and were off without a marker
  // file. Fixed the same evening: Web Audio 21:07 (media/GStreamerCommon.cpp
  // .patch), WebGL 21:13 (ANGLE took the system's EGL with dlopen(), which
  // fails in the title: webgl/FunctionsEGLDL.cpp.patch; 21:19: WebGL 1 and 2,
  // shaders, textures, 60 animation frames a second on the screen). Both are
  // ON now. /data/homebrew/PPSA97024/webgl-off turns WebGL off again (a way
  // back without a new build). An engine without them ignores the key.
  if(!bisect_off('u')) {
    const bool off = !access("/data/homebrew/" TITLE_ID "/webgl-off", F_OK);
    WKStringRef key = string("WebGLEnabled");
    WKPreferencesSetBoolValueForKeyForTesting(page_preferences, !off, key);
    WKRelease(key);
    if(off) {
      POC_LOG("WebGLEnabled: OFF for the pages (/data/homebrew/" TITLE_ID "/webgl-off)");
    }
  }
#endif
#ifdef MEDIA_PROBE
  // 2026-11-10 21:50: the pages' and their workers' console messages into
  // the log (WhatsApp Web's workers stopped answering, with no error event).
  WKPreferencesSetLogsPageMessagesToSystemConsoleEnabled(page_preferences, true);
#endif
  page_content = WKUserContentControllerCreate();
#ifdef ENGINE_PLUS
  rules_start();  // the ad blocker's hosts as content rules (and the user's list)
#ifdef APP_RN
  strict_load();
#endif
#endif
  preferences_apply(false);

  create_pane(BAR, "bar", 0, 0, DISPLAY_WIDTH, BAR_HEIGHT, app.screen, DISPLAY_WIDTH,
              chrome_context, data, 0, 0);
  create_pane(PAGE, "page", 0, BAR_HEIGHT, DISPLAY_WIDTH, DISPLAY_HEIGHT - BAR_HEIGHT,
              app.screen + (size_t)BAR_HEIGHT * DISPLAY_WIDTH, DISPLAY_WIDTH, context,
              data, page_preferences, page_content);
  tabs[0].view = app.panes[PAGE].view;  // the first tab
  tabs[0].id = tab_next_id++;
  tab_count = 1;
  // Shown only while open: the menu and the dialog over the page, the
  // keyboard over the page's bottom rows.
  create_pane(MENU, "menu", MENU_LEFT, MENU_TOP, MENU_WIDTH, MENU_HEIGHT, app.menu,
              MENU_WIDTH, chrome_context, data, 0, 0);
  create_pane(DIALOG, "dialog", DIALOG_LEFT, DIALOG_TOP, DIALOG_WIDTH, DIALOG_HEIGHT,
              app.dialog, DIALOG_WIDTH, chrome_context, data, 0, 0);
  create_pane(KEYS, "keyboard", 0, DISPLAY_HEIGHT - KEYS_HEIGHT, DISPLAY_WIDTH,
              KEYS_HEIGHT, app.keys, DISPLAY_WIDTH, chrome_context, data, 0, 0);
  create_pane(RADIAL, "radial", RADIAL_LEFT, RADIAL_TOP, RADIAL_SIZE, RADIAL_SIZE,
              app.radial, RADIAL_SIZE, chrome_context, data, 0, 0);
  create_pane(SWITCHER, "tabs", 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, app.switcher_px,
              DISPLAY_WIDTH, chrome_context, data, 0, 0);
#ifdef GPU_COMPOSITING
  layer_ids_update();
  POC_LOG("compositing on the GPU: bar %llu, page %llu, menu %llu, dialog %llu, "
          "keyboard %llu", (unsigned long long)app.panes[BAR].page_id,
          (unsigned long long)app.panes[PAGE].page_id,
          (unsigned long long)app.panes[MENU].page_id,
          (unsigned long long)app.panes[DIALOG].page_id,
          (unsigned long long)app.panes[KEYS].page_id);
#endif
  favorites_load();
  POC_LOG("logins: %d saved", logins_load(LOGINS_FILE));
#ifdef REMOTE_UI
  torbox_http_init();
  torbox_key_load(TORBOX_KEY_FILE);
#endif

  ui_client(BAR, barLoaded);
  ui_client(MENU, uiLoaded);
  ui_client(DIALOG, uiLoaded);
  ui_client(KEYS, uiLoaded);
  ui_client(RADIAL, uiLoaded);

  ui_client(SWITCHER, uiLoaded);
  page_client_install(page(PAGE));

#ifdef APP_RN
  if(!ui_split) {  // else once the page's WebProcess is there (ui_split_tick())
    ui_panes_load();
  }
#else
  load_html(BAR, chrome_html);
  load_html(KEYS, keyboard_html);
  load_html(MENU, menu_html);
  load_html(DIALOG, dialog_html);
  load_html(RADIAL, radial_html);
  load_html_at(SWITCHER, tabs_html, "file://" TABS_DIR "/");
#endif

  // The settings that go into WebKit.
  user_agent_apply();
  cookies_apply();
  WKPageSetPageZoomFactor(page(PAGE), app.zoom);
  layout();
  extensions_apply();
#ifdef VIDEO_TEST
  // The clip's own origin as the page's, so loading it is same-origin.
#ifdef VIDEO_TEST_STALL_MS
  // The MSE page reads the clip with XMLHttpRequest: a file: page may.
  WKPreferencesSetFileAccessFromFileURLsAllowed(page_preferences, true);
  static char test_html[sizeof(video_test_mse_html) + 1024];
  snprintf(test_html, sizeof(test_html), video_test_mse_html, VIDEO_TEST, VIDEO_TEST_AUDIO,
           VIDEO_TEST_STALL_MS,
           STALL_EVERY_MS);
#else
  static char test_html[sizeof(video_test_html) + 1024];
  snprintf(test_html, sizeof(test_html), video_test_html, VIDEO_TEST);
#endif
  WKStringRef test_page = string(test_html);
  WKURLRef base = WKURLCreateWithUTF8CString(VIDEO_TEST);
  WKPageLoadHTMLString(page(PAGE), test_page, base);
  WKRelease(base);
  WKRelease(test_page);
  set_focus(PAGE);
  POC_LOG("video test with %s, running for %d s", VIDEO_TEST, RUN_SECONDS);
#else
  // make HOME_URL=..., the tabs of the last time, the last page, or the
  // home page. A test run (HOME_URL) neither reads nor writes the tabs.
  char shown[512];
  if(!HOME_URL_GIVEN) {
    session_start();
  }
  // "Continuar onde fiquei", unless those of the last start did not last.
  const bool reopen = !HOME_URL_GIVEN && !strcmp(settings.startup, "last") && session_safe();
  if(HOME_URL_GIVEN) {
    load_url(HOME_URL);
    POC_LOG("loading %s, running for %d s", url_for_log(HOME_URL, shown, sizeof(shown)),
            RUN_SECONDS);
  } else if(reopen && tabs_restore()) {
    session_opened();
    POC_LOG("loading the tabs of the last time, running for %d s", RUN_SECONDS);
  } else if(reopen && url_is_web(settings.last_url)) {
    session_opened();
    load_url(settings.last_url);
    POC_LOG("loading the last page, %s, running for %d s",
            url_for_log(settings.last_url, shown, sizeof(shown)), RUN_SECONDS);
  } else {
    home_open();
    POC_LOG("loading the home page, running for %d s", RUN_SECONDS);
  }
  set_focus(PAGE);
#endif

  if((err=input_start(on_input, DISPLAY_WIDTH, DISPLAY_HEIGHT))) {
    POC_RESULT(0, "no controller (0x%x): no input", err);
  }
  input_set_speed(settings.cursor_speed, settings.scroll_speed);
  feel_apply();
#ifdef REMOTE_UI
  remote_on_away(on_away);  // the views exist now
  remote_on_closed(on_title_closed);
  POC_LOG("pause when the system is over the app: %s",
          AWAY_ENDS_WEB_PROCESS ? "the WebProcess ends, the GPU is let go"
                                : "the views are hidden");
#endif
  pthread_create(&display, 0, display_thread, 0);
  pthread_create(&timer, 0, timer_thread, 0);
  pthread_t magnet;
  if(!pthread_create(&magnet, 0, magnet_thread, 0)) {
    pthread_detach(magnet);
  }
#ifdef MEDIA_PROBE
  pthread_t load_probe;
  pthread_create(&load_probe, 0, load_probe_thread, 0);
  pthread_detach(load_probe);
#endif
#ifdef CURSOR_TEST
  pthread_t cursor_test;
  pthread_create(&cursor_test, 0, cursor_test_thread, 0);
  pthread_detach(cursor_test);
#endif
#if defined(DEMO) || defined(SHOW)
  pthread_t guard;  // closes a test build whose first page never loads
  if(!pthread_create(&guard, 0, demo_guard, 0)) {
    pthread_detach(guard);
  }
#endif

  WKRunLoopRunMain();

  pthread_mutex_lock(&app.lock);
  app.quit = true;
  pthread_cond_signal(&app.cond);
  pthread_mutex_unlock(&app.lock);
  pthread_join(display, 0);
#ifdef APP_RN
  __atomic_store_n(&main_ending, 1, __ATOMIC_RELEASE);
  POC_LOG("the run loop ended %.0f ms after start: the title is told now", poc_now_ms() - app.start_ms);
#endif
  display_close();
#ifdef APP_RN
  if(play_after[0]) {  // game_play(): play_after.elf asks for the game once this process and its launcher are gone
    POC_LOG("play %s: left to play_after.elf (%s)", play_after, PLAY_AFTER_LOG);
  }
#endif

  POC_LOG("done: %u paint(s), first paint %.0f ms after start", app.paints,
          app.first_paint_ms);
  // Not return: WebKit's threads are still running, and the static
  // destructors under them crashed the app on its way out (hello_page).
  _exit(app.paints ? 0 : 1);
}
