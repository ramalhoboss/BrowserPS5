#   Copyright (C) 2026 BrowserPS5 contributors
#
# This file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# The browser (docs/02-desenho-sem-gpu.md): the address bar over the page.
#   browser_app.elf  WebKit's UIProcess (main.cpp), runs inside the app
#   starter.elf      bigapp_gl's launcher, embeds browser_app.elf
#   browser.elf      the launcher payload (../launcher/launcher.c), embeds
#                    starter.elf: this is the one to send
#
# The display, the controller, the system keyboard and the screenshot are
# hello_page's (M1-M3). Needs the same WebKit build and tars as hello_page.
# make test runs address.c's tests on the build machine.
#
# make browser_gpu.elf: the same browser with its screen in the title
# PPSA97020 (title/, docs/03-video-e-gpu.md, G1), which also owns the GPU:
#   title/browser_ui.elf  WebKit's UIProcess as a payload; title/remote.c
#                         does the display, controller and keyboard through
#                         the title
#   browser_gpu.elf       the launcher payload, embeds title/browser_ui.elf:
#                         send this one, with title/title.tar installed
# and WebKit's compositor on the GPU (G2): the WebProcess draws with Mesa's
# zink through the title and hands the frames over in shared memory
# (../webkit/gpu/bps5_layer.h). Needs WebKit with the gpu/ patches and the
# browser's Mesa (BROWSER=1 ../gl_zink/build.sh) installed.
# make GPU_COMPOSITING=0 browser_gpu.elf paints on the CPU as in G1.

ifdef PS5_PAYLOAD_SDK
    include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
    $(error PS5_PAYLOAD_SDK is undefined)
endif

WEBKIT_BUILD ?= $(HOME)/browserps5-build/webkit-build
WEBKIT_LIB   := $(firstword $(wildcard $(WEBKIT_BUILD)/lib/libWebKit.so*))

COMMON   := ../common
HELLO    := ../hello_page
BIGAPP   := ../bigapp_gl
LAUNCHER := ../launcher
PL       := ../webkit/process-launcher
CFLAGS   := -Wall -Werror -O2 -I$(COMMON) -I$(PL)
CXXFLAGS := -Wall -O2 -std=c++20 -I$(COMMON) -I$(HELLO) \
            -I$(WEBKIT_BUILD)/WebKit/Headers \
            -I$(WEBKIT_BUILD)/JavaScriptCore/Headers

# make HOME_URL=https://... starts there (make clean first: the ELF names
# stay the same).
ifdef HOME_URL
    CXXFLAGS += -DHOME_URL='"$(HOME_URL)"'
endif
# make GO_TEST=lisboa types that into the bar by itself, then checks where
# the page went (see go_test() in main.cpp).
ifdef GO_TEST
    CXXFLAGS += -DGO_TEST='"$(GO_TEST)"'
endif
# make NAV_TEST=<rounds>: from the start page, the first favorite, then ⌂,
# that many times, 15 s a step (the freezes of 2026-11-05; nav_test()).
ifdef NAV_TEST
    CXXFLAGS += -DNAV_TEST=$(NAV_TEST)
endif
# make KEYS_TEST=1 (with the title: browser_gpu.elf) works the browser's own
# keyboard and the favorites by itself (keys_test() in main.cpp, docs/06).
ifdef KEYS_TEST
    CXXFLAGS += -DKEYS_TEST
endif
# make OPTIONS_TEST=1: options opens the menu, its full screen row both
# ways, then its Sair (options_test() in main.cpp, docs/08-4k.md).
ifdef OPTIONS_TEST
    CXXFLAGS += -DOPTIONS_TEST
endif
# make H2_TEST=1 HOME_URL=https://www.google.com/: Google, Wikipedia and
# GitHub twice each; after each load the page's protocol (h2 = HTTP/2), its
# times and what came from a cache, at the end the disk cache's size
# (h2_test() in main.cpp, docs/22-http2.md, docs/21-cache-no-disco.md).
ifdef H2_TEST
    CXXFLAGS += -DH2_TEST
endif
# make CLOSE_TITLE_BY_SYSTEM=1: the quit has the system close the title once
# it let go of the GPU and still shows the screen (title/remote.c,
# close_title()). Only for a console test the user agrees to: the system's
# close of a title that had closed everything took the console down twice
# (2026-11-05 16:51, 17:14; docs/08-4k.md).
ifdef CLOSE_TITLE_BY_SYSTEM
    CFLAGS += -DCLOSE_TITLE_BY_SYSTEM
endif
ifdef RUN_SECONDS
    CXXFLAGS += -DRUN_SECONDS=$(RUN_SECONDS)
endif
# NO_SHOT=1: no screenshot in the log when the browser stops (the browser
# the title starts from the home screen: its log is a file on the console).
ifdef NO_SHOT
    CXXFLAGS += -DNO_SHOT
endif
# MOUSE_MOVE_MS=n: a mouse move to the page at most every n ms (main.cpp).
# CURSOR_TEST=1: the cursor goes round by itself, 4 s on, 4 s off from 36 s
# (CURSOR_TEST_START=s, CURSOR_TEST_SECONDS=s), with MEDIA_PROBE to see what
# that does to a video (docs/03-video-e-gpu.md).
ifdef MOUSE_MOVE_MS
    CXXFLAGS += -DMOUSE_MOVE_MS=$(MOUSE_MOVE_MS)
endif
# DISPLAY_PRIORITY=n: the display thread's priority (690 by default, a little
# above the WebProcess's 700; 0 leaves it as created). See main.cpp.
ifdef DISPLAY_PRIORITY
    CXXFLAGS += -DDISPLAY_PRIORITY=$(DISPLAY_PRIORITY)
endif
ifdef CURSOR_TEST
    CXXFLAGS += -DCURSOR_TEST
endif
ifdef CURSOR_TEST_START
    CXXFLAGS += -DCURSOR_TEST_START=$(CURSOR_TEST_START)
endif
ifdef CURSOR_TEST_SECONDS
    CXXFLAGS += -DCURSOR_TEST_SECONDS=$(CURSOR_TEST_SECONDS)
endif
# make VIDEO_TEST=<clip URL> WEBKIT_BUILD=$HOME/browserps5-build/webkit-build-media:
# a muted <video> of that clip instead of the home page, its progress in the
# log (docs/03-video-e-gpu.md, V2). LOG_TITLE=1 logs the titles of any page.
ifdef VIDEO_TEST
    CXXFLAGS += -DVIDEO_TEST='"$(VIDEO_TEST)"'
endif
# VIDEO_TEST_STALL_MS=<ms> with VIDEO_TEST: the clip through MediaSource, the
# page's main thread busy that long every 8 s (scrolling under a video).
ifdef VIDEO_TEST_STALL_MS
    CXXFLAGS += -DVIDEO_TEST_STALL_MS=$(VIDEO_TEST_STALL_MS)
endif
# VIDEO_TEST_AUDIO=<sound clip URL> with VIDEO_TEST_STALL_MS: a second
# SourceBuffer, the sound (Opus WebM), as YouTube's player has.
ifdef VIDEO_TEST_AUDIO
    CXXFLAGS += -DVIDEO_TEST_AUDIO='"$(VIDEO_TEST_AUDIO)"'
endif
# WEB_IN_TITLE_OFF=1 (2026-11-06, tests only): the WebProcess as a payload
# with the 2c-2 WebKit (BROWSERPS5_WEB_IN_TITLE=0), the settings untouched.
ifdef WEB_IN_TITLE_OFF
    CXXFLAGS += -DWEB_IN_TITLE_OFF
endif
ifdef LOG_TITLE
    CXXFLAGS += -DLOG_TITLE
endif
# MEDIA_PROBE=1: the state of the page's first <video> in the log, every
# second it changed, for pages that are not ours (V3, YouTube).
# UI_TEST=1: the menu, the D-pad, the settings, a theme and the start page,
# worked by the app itself, with a screenshot of each (ui_test() in main.cpp).
ifdef UI_TEST
    CXXFLAGS += -DUI_TEST
endif
# SW_ON=1 (2026-11-10): service workers on in a normal build (main.cpp; the
# UIProcess reads no env.txt), for WhatsApp Web and Telegram Web.
# APP_RN=1 (2026-11-10): BrowserPS5 RN, the second app (title PPSA97024,
# ../browser-rn/build-rn.sh): its own data (DATA_DIR), its own pages
# (../browser-rn/pages over these) and the title it opens (remote.c).
# Built in a copy of this folder, never here: v1 stays as it is.
# RN_TITLE_ID (2026-11-13): the app's title ID. PPSA97025 since that night: the title of the PS5's media tab
# (a title's tab is set when the system registers it; PPSA97024, registered as a game, was removed).
ifdef APP_RN
    # (2026-11-14: PPSA97024, the games tab's, again: the media tab's title has 4 GiB of memory in all)
    RN_TITLE_ID ?= PPSA97024
    CFLAGS   += -DAPP_RN -DTITLE_ID='"$(RN_TITLE_ID)"'
    CXXFLAGS += -DAPP_RN -DTITLE_ID='"$(RN_TITLE_ID)"'
endif
# ENGINE_PLUS=1 (2026-11-11, BrowserPS5 RN): for the WebKit built with
# ENABLE_CONTENT_EXTENSIONS and ENABLE_DARK_MODE_CSS
# (~/browserps5-build/webkit-build-plus; WEBKIT_BUILD must name it, the UI
# links its libWebKit): the ad blocker's hosts as WebKit's own content rules,
# and the sites' own dark theme (main.cpp, ENGINE_PLUS).
ifdef ENGINE_PLUS
    CXXFLAGS += -DENGINE_PLUS
endif
# ENGINE_DIR=skia (2026-11-11): the UI of another engine variant
# (~/browserps5-build/webkit-build-skia, USE_SKIA=ON; WEBKIT_BUILD must name
# it): its network process and WebProcesses are the ones in
# /user/homebrew/browserps5-rn/$(ENGINE_DIR)/ (browser-rn/pack-lib-rn.sh).
ifdef ENGINE_DIR
    CXXFLAGS += -DENGINE_DIR='"$(ENGINE_DIR)"'
endif
ifdef SW_ON
    CXXFLAGS += -DSW_ON
endif
# SPEED_TEST=1 (2026-11-09): speed_test.h, the same speed test as NK's.
ifdef SPEED_PLAN
    CXXFLAGS += -DSPEED_PLAN_FILE='"$(SPEED_PLAN)"'
endif
ifdef SPEED_TEST
    CXXFLAGS += -DSPEED_TEST
endif
# DEMO=1 (with the title: browser_gpu.elf; docs/09-video.md): the browser's
# tour for a video, worked by the app, each piece recorded by the title
# (demo_tour() in main.cpp), with its own data and overlay.c's cursors.
# DEMO_VIDEO=<page> for its video piece.
ifdef DEMO
    CXXFLAGS += -DDEMO
endif
# SHOW=1 (BrowserPS5 RN, 2026-11-11): the tour the user films from the TV,
# with their own data (main.cpp show_tour()); SHOW_VIDEO=<page> for its film.
ifdef SHOW
    CXXFLAGS += -DSHOW
endif
ifdef SHOW_YT
    CXXFLAGS += -DSHOW_YT
endif
ifdef SHOW_YT_QUALITY
    CXXFLAGS += -DSHOW_YT_QUALITY='"$(SHOW_YT_QUALITY)"'
endif
ifdef SHOW_PROBE
    CXXFLAGS += -DSHOW_PROBE
endif
ifdef SHOW_PROBE_NOAUDIO
    CXXFLAGS += -DSHOW_PROBE_NOAUDIO
endif
ifdef SHOW_PROBE_GL
    CXXFLAGS += -DSHOW_PROBE_GL
endif
ifdef SHOW_PROBE_NOTIFY
    CXXFLAGS += -DSHOW_PROBE_NOTIFY
endif
ifdef SHOW_PDF
    CXXFLAGS += -DSHOW_PDF
endif
ifdef SHOW_UPDATE
    CXXFLAGS += -DSHOW_UPDATE
endif
ifdef SHOW_DL_KEEP
    CXXFLAGS += -DSHOW_DL_KEEP=$(SHOW_DL_KEEP)
endif
ifdef SHOW_DL_DROP_QUIET
    CXXFLAGS += -DSHOW_DL_DROP_QUIET
endif
ifdef SHOW_DL_DROP_OFF
    CXXFLAGS += -DSHOW_DL_DROP_OFF
endif
ifdef SHOW_DL_DROP_SOLO
    CXXFLAGS += -DSHOW_DL_DROP_SOLO
endif
ifdef SHOW_DL_DUO
    CXXFLAGS += -DSHOW_DL_DUO
endif
ifdef SHOW_DL_DROP
    CXXFLAGS += -DSHOW_DL_DROP
endif
ifdef SHOW_DL_CANCEL
    CXXFLAGS += -DSHOW_DL_CANCEL
endif
ifdef SHOW_DL_BIG
    CXXFLAGS += -DSHOW_DL_BIG
endif
ifdef SHOW_DL_SMALL
    CXXFLAGS += -DSHOW_DL_SMALL
endif
ifdef SHOW_DL
    CXXFLAGS += -DSHOW_DL
endif
ifdef SHOW_SITES
    CXXFLAGS += -DSHOW_SITES
endif
ifdef SHOW_SITE
    CXXFLAGS += -DSHOW_SITE='"$(SHOW_SITE)"'
endif
ifdef SHOW_SITE_WAIT
    CXXFLAGS += -DSHOW_SITE_WAIT=$(SHOW_SITE_WAIT)
endif
ifdef SHOW_EMU
    CXXFLAGS += -DSHOW_EMU
endif
ifdef SHOW_GAMES
    CXXFLAGS += -DSHOW_GAMES
endif
ifdef SHOW_YT_EMBED
    CXXFLAGS += -DSHOW_YT_EMBED
endif
ifdef SHOW_YT_TV
    CXXFLAGS += -DSHOW_YT_TV
endif
ifdef SHOW_YT_BACK
    CXXFLAGS += -DSHOW_YT_BACK
endif
ifdef SHOW_YT_PS5UA
    CXXFLAGS += -DSHOW_YT_PS5UA
endif
ifdef SHOW_YT_BARE
    CXXFLAGS += -DSHOW_YT_BARE
endif
ifdef SHOW_YT_LET
    CXXFLAGS += -DSHOW_YT_LET
endif
# SHOW_VEIL=1 (2026-11-14, with SHOW=1 SHOW_YT=1): while the film plays, an element with a
# backdrop-filter over the video, then a filter on the player around it (main.cpp).
ifdef SHOW_VEIL
    CXXFLAGS += -DSHOW_VEIL
endif
# SYSIME_UI=1 (2026-11-14): the system keyboard when the setting says so and the title has its library
# loaded (title/sysime.c); main.cpp, open_keyboard().
ifdef SYSIME_UI
    CXXFLAGS += -DSYSIME_UI
endif
ifdef SHOW_VIDEO
    CXXFLAGS += -DSHOW_VIDEO='"$(SHOW_VIDEO)"'
endif
ifdef DEMO_VIDEO
    CXXFLAGS += -DDEMO_VIDEO='"$(DEMO_VIDEO)"'
endif
# WEBKIT_BEFORE_2C2=1: for a console whose WebKit is from before 2c-2
# (docs/05; the TV set's, f8ad1301), which has no titleWebProcessId().
ifdef WEBKIT_BEFORE_2C2
    CXXFLAGS += -DWEBKIT_BEFORE_2C2
endif
ifdef MEDIA_PROBE
    CXXFLAGS += -DMEDIA_PROBE
endif

LAUNCHER_FD := $(shell sed -n 's/^\#define BROWSERPS5_LAUNCHER_FD //p' $(PL)/launcher_proto.h)
HB := $(PS5_PAYLOAD_SDK)/target/user/homebrew

all: browser.elf

display.o: $(HELLO)/display.c $(HELLO)/display.h $(COMMON)/video_out.h $(COMMON)/poc_log.h
	$(CC) $(CFLAGS) -DPOC_NAME='"browser/display"' -c -o $@ $(HELLO)/display.c

shot.o: $(HELLO)/shot.c $(HELLO)/shot.h
	$(CC) $(CFLAGS) -I$(HB)/include -c -o $@ $(HELLO)/shot.c

input.o: $(HELLO)/input.c $(HELLO)/input.h $(COMMON)/poc_log.h
	$(CC) $(CFLAGS) -DPOC_NAME='"browser"' -c -o $@ $(HELLO)/input.c

ime.o: $(HELLO)/ime.c $(HELLO)/ime.h $(COMMON)/poc_log.h
	$(CC) $(CFLAGS) -DPOC_NAME='"browser"' -c -o $@ $(HELLO)/ime.c

overlay.o: overlay.c overlay.h
	$(CC) $(CFLAGS) -c -o $@ overlay.c

settings.o: settings.c settings.h
	$(CC) $(CFLAGS) -c -o $@ settings.c

address.o: address.c address.h
	$(CC) $(CFLAGS) -c -o $@ address.c

# The pages with theme.css and icons.svg put in (include.awk).
PAGES := chrome keyboard menu dialog radial tabs start settings phone
EXT   := ext/gm.js ext/adblock.js ext/dark.js ext/fullscreen.js ext/adhosts.txt ext/gamepad.js ext/devtools.js ext/manager.js ext/reader.js ext/linkmenu.js

%.built.html: %.html theme.css icons.svg include.awk lang.js
	awk -v dir=. -f include.awk $< > $@.new && mv $@.new $@

# settings.html has the QR code maker in it too.
settings.built.html: qr.js
# The menu too (RN: the page's address for the phone).
menu.built.html: qr.js

pages.o: pages.S $(PAGES:%=%.built.html) nav.js $(EXT)
	$(CC) -c -o $@ pages.S

# The build machine's own Windows cursors (main.cpp, cur2c.c), made at each
# build and never part of the project: they are not ours to publish. With
# none (another system), overlay.c draws its own.
HOSTCC          ?= gcc
WINDOWS_CURSORS ?= /mnt/c/Windows/Cursors
CURSOR_SIZE     ?= 64

cur2c: cur2c.c
	$(HOSTCC) -Wall -Werror -O2 -o $@ cur2c.c

cursors_win.h: cur2c
	@if ./cur2c cursor_arrow $(CURSOR_SIZE) "$(WINDOWS_CURSORS)/aero_arrow.cur" > $@.arrow 2>/dev/null \
	    && ./cur2c cursor_hand $(CURSOR_SIZE) "$(WINDOWS_CURSORS)/aero_link.cur" > $@.hand 2>/dev/null; then \
	  { echo "#define CURSORS_WIN 1"; cat $@.arrow $@.hand; } > $@; echo "cursors: the Windows ones"; \
	else echo "// No Windows cursors here: overlay.c draws its own." > $@; echo "cursors: overlay.c's"; fi; \
	rm -f $@.arrow $@.hand

OBJS := address.o pages.o display.o ime.o input.o shot.o overlay.o settings.o dl_rate.o session.o

browser_app.elf: main.cpp $(OBJS) address.h overlay.h settings.h dl_rate.h cursors_win.h $(HELLO)/display.h $(HELLO)/ime.h \
                 $(HELLO)/input.h $(HELLO)/shot.h $(COMMON)/poc_log.h
	@test -n "$(WEBKIT_LIB)" || { echo "libWebKit not found in $(WEBKIT_BUILD)/lib"; exit 1; }
	$(CXX) $(CXXFLAGS) -o $@ main.cpp $(OBJS) \
	    $(WEBKIT_LIB) $(HB)/lib/libz.a -lSceVideoOut -lSceSystemService \
	    -lScePad -lSceUserService -lSceImeDialog

# bigapp_gl's launcher embeds ./payload.elf (.incbin, from the current
# directory).
starter.elf: $(BIGAPP)/main.c browser_app.elf
	cp browser_app.elf payload.elf
	$(CC) $(CFLAGS) -DBIGAPP_CTL_FD=$(LAUNCHER_FD) $(STARTER_FLAGS) \
	    -o $@ $(BIGAPP)/main.c \
	    $(COMMON)/hbldr/elfldr.c $(COMMON)/hbldr/pt.c \
	    -lSceSystemService -lSceUserService

# launcher.c embeds ./starter.elf.
browser.elf: $(LAUNCHER)/launcher.c starter.elf $(PL)/launcher_proto.h ../webkit/ipc-transport/ipc_transport.h
	$(CC) $(CFLAGS) -o $@ $(LAUNCHER)/launcher.c \
	    $(COMMON)/elfldr/elfldr.c $(COMMON)/elfldr/pt.c

TITLE := title

$(TITLE)/remote.o: $(TITLE)/remote.c $(TITLE)/link.h $(TITLE)/remote.h $(HELLO)/display.h \
                   $(HELLO)/input.h $(HELLO)/ime.h $(COMMON)/poc_log.h
	$(CC) $(CFLAGS) -I$(HELLO) -DPOC_NAME='"browser"' -c -o $@ $(TITLE)/remote.c

# dl_rate.c: a download's speed and time left (docs/15-transferencias.md).
dl_rate.o: dl_rate.c dl_rate.h
	$(CC) $(CFLAGS) -c -o $@ dl_rate.c

# unzip.c: "Extrair" for a downloaded .zip, with the SDK's zlib (docs/16).
unzip.o: unzip.c unzip.h
	$(CC) $(CFLAGS) -I$(HB)/include -c -o $@ unzip.c

# files.c: the console's files for the phone (docs/18-ficheiros-no-telemovel.md).
files.o: files.c files.h
	$(CC) $(CFLAGS) -c -o $@ files.c

# session.c: the tabs of the last time (docs/20-separadores-da-ultima-vez.md).
session.o: session.c session.h
	$(CC) $(CFLAGS) -c -o $@ session.c

phone.o: phone.c phone.h files.h
	$(CC) $(CFLAGS) -c -o $@ phone.c

# side_card.c: the menu's "Abrir ao lado do jogo" (docs/10-achados-do-desktop.md).
side_card.o: side_card.c side_card.h
	$(CC) $(CFLAGS) -c -o $@ side_card.c

# torrent.c: magnets and .torrent files to Transmission (../torrent, docs/11).
torrent.o: torrent.c torrent.h
	$(CC) $(CFLAGS) -c -o $@ torrent.c

# torbox.c: the user's TorBox account; torbox_http.c its HTTPS, the SDK's
# libcurl and OpenSSL (docs/11).
torbox.o: torbox.c torbox.h torrent.h
	$(CC) $(CFLAGS) -c -o $@ torbox.c
torbox_http.o: torbox_http.c torbox.h
	$(CC) $(CFLAGS) -I$(HB)/include -c -o $@ torbox_http.c

# logins.c: the saved logins (docs/12).
logins.o: logins.c logins.h
	$(CC) $(CFLAGS) -c -o $@ logins.c

UI_OBJS := address.o pages.o shot.o overlay.o settings.o phone.o system.o side_card.o dl_rate.o unzip.o files.o session.o torrent.o \
           torbox.o torbox_http.o logins.o $(TITLE)/remote.o

# 2026-11-11 (BrowserPS5 RN): ask_box.c, the window of a page's alert(),
# confirm() or prompt() drawn by the app itself with PlutoVG (title/plutovg,
# MIT, the one the title's starting screen uses).
PVG_OBJS := $(patsubst $(TITLE)/plutovg/source/%.c,pvg-%.o,$(wildcard $(TITLE)/plutovg/source/*.c))
pvg-%.o: $(TITLE)/plutovg/source/%.c
	$(CC) -std=gnu11 -O2 -DPLUTOVG_BUILD -DPLUTOVG_BUILD_STATIC -I$(TITLE)/plutovg/include -c -o $@ $<
ask_box.o: ask_box.c ask_box.h
	$(CC) $(CFLAGS) -DPLUTOVG_BUILD_STATIC -I$(TITLE)/plutovg/include -c -o $@ ask_box.c
# games.c (2026-11-14): the games on the console, for the start page's "your game" row; xps5/ is from the user's
# own XPS5 (GPL-3.0-or-later): a game's name and kind from the PS5's data, and its small JSON reader.
games.o: games.c games.h xps5/game.h xps5/trophy.h xps5/json.h
	$(CC) $(CFLAGS) -DPLUTOVG_BUILD_STATIC -I$(TITLE)/plutovg/include -c -o $@ games.c
xps5-%.o: xps5/%.c xps5/game.h xps5/json.h xps5/mem.h xps5/trophy.h xps5/captures.h
	$(CC) $(CFLAGS) -Ixps5 -c -o $@ $<
# health.c (2026-11-15): the console's temperatures, power, fan target, drives and address, only read (the way
# PlayBridge reads them), for the start page's "console health" panel.
health.o: health.c health.h
	$(CC) $(CFLAGS) -c -o $@ health.c
# play_after.elf (2026-11-16): the game hub's "Play" sends it to the payload loader and leaves as "Sair" does;
# it asks for the game once the browser is gone (../play_after/main.c says why).
PLAY_AFTER := ../play_after
play_after.elf: $(PLAY_AFTER)/main.c $(PLAY_AFTER)/play_after.h $(COMMON)/poc_log.h $(TITLE)/link.h
	$(CC) -Wall -Werror -O1 -I$(COMMON) -I$(TITLE) -o $@ $(PLAY_AFTER)/main.c -lSceSystemService -lSceUserService
play_after_elf.o: play_after_elf.S play_after.elf
	$(CC) -c -o $@ play_after_elf.S
# share.c (2026-11-16): a capture sent to a Discord channel by its webhook (share.h).
share.o: share.c share.h
	$(CC) $(CFLAGS) -I$(HB)/include -c -o $@ share.c
# The browser's own updates (2026-11-17; update.h): what GitHub's list of releases says (update.c, tested on the
# PC: make update_test), the two requests (update_net.c, libcurl), and update_after.elf, which hands a release's
# installer to the payload loader once the browser has left (../update_after/main.c).
UPDATE_AFTER := ../update_after
update.o: update.c update.h xps5/json.h
	$(CC) $(CFLAGS) -c -o $@ update.c
update_net.o: update_net.c update_net.h $(COMMON)/sha256.h
	$(CC) $(CFLAGS) -I$(HB)/include -c -o $@ update_net.c
update_after.elf: $(UPDATE_AFTER)/main.c $(UPDATE_AFTER)/update_after.h $(COMMON)/poc_log.h $(COMMON)/sha256.h $(TITLE)/link.h
	$(CC) -Wall -Werror -O1 -I$(COMMON) -I$(TITLE) -o $@ $(UPDATE_AFTER)/main.c -lSceSystemService -lSceUserService
update_after_elf.o: update_after_elf.S update_after.elf
	$(CC) -c -o $@ update_after_elf.S
update_test: update_test.c update.c update.h xps5/json.c xps5/mem.c $(COMMON)/sha256.h
	gcc -std=gnu11 -g -O1 -Wall -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined -o $@ update_test.c update.c xps5/json.c xps5/mem.c
	./update_test
ifdef APP_RN
    UI_OBJS += ask_box.o $(PVG_OBJS) games.o xps5-game.o xps5-json.o xps5-mem.o xps5-trophy.o xps5-captures.o health.o \
               play_after_elf.o share.o update.o update_net.o update_after_elf.o
endif

# REMOTE_UI: the pause when the system is over the app (title/remote.h).
UI_FLAGS := -DREMOTE_UI -I$(TITLE)
GPU_COMPOSITING ?= 1
ifeq ($(GPU_COMPOSITING),1)
    UI_FLAGS += -DGPU_COMPOSITING -I../webkit/gpu
endif
# AWAY_HIDE=1: on the pause, only hide the views; the WebProcess and its GPU
# client stay (main.cpp, "The pause").
ifdef AWAY_HIDE
    UI_FLAGS += -DAWAY_HIDE
endif

$(TITLE)/browser_ui.elf: main.cpp $(UI_OBJS) address.h overlay.h settings.h dl_rate.h unzip.h files.h session.h phone.h system.h side_card.h torrent.h torbox.h logins.h cursors_win.h $(HELLO)/display.h $(HELLO)/ime.h \
                         $(HELLO)/input.h $(HELLO)/shot.h $(COMMON)/poc_log.h \
                         ../webkit/gpu/bps5_layer.h $(TITLE)/remote.h $(TITLE)/link.h
	@test -n "$(WEBKIT_LIB)" || { echo "libWebKit not found in $(WEBKIT_BUILD)/lib"; exit 1; }
	$(CXX) $(CXXFLAGS) $(UI_FLAGS) -o $@ main.cpp $(UI_OBJS) \
	    $(WEBKIT_LIB) $(HB)/lib/libcurl.a $(HB)/lib/libpsl.a $(HB)/lib/libssl.a $(HB)/lib/libcrypto.a \
	    $(HB)/lib/libbrotlidec.a $(HB)/lib/libbrotlicommon.a $(HB)/lib/libz.a \
	    -lSceSystemService -lSceUserService -lSceNotification

browser_gpu.elf: $(LAUNCHER)/launcher.c $(TITLE)/browser_ui.elf $(PL)/launcher_proto.h ../webkit/ipc-transport/ipc_transport.h
	$(CC) $(CFLAGS) -DLAUNCHER_FIRST_CHILD='"$(TITLE)/browser_ui.elf"' -o $@ \
	    $(LAUNCHER)/launcher.c $(COMMON)/elfldr/elfldr.c $(COMMON)/elfldr/pt.c

# On the build machine, not the PS5. unzip_test takes zlib.h from the SDK
# (after the system's headers) and the build machine's own libz.
HOST_LIBZ ?= $(firstword $(wildcard /usr/lib/x86_64-linux-gnu/libz.so /usr/lib/x86_64-linux-gnu/libz.so.1) -lz)
test: address.c address.h address_test.c dl_rate.c dl_rate.h dl_rate_test.c unzip.c unzip.h unzip_test.c files.c files.h files_test.c session.c session.h session_test.c settings.c settings.h settings_test.c phone.c phone.h phone_test.c system.c system.h system_test.c overlay.c overlay.h overlay_test.c side_card.c side_card.h side_card_test.c torrent.c torrent.h torrent_test.c torbox.c torbox.h torbox_test.c logins.c logins.h logins_test.c lang.js lang_test.js
	gcc -Wall -Werror -O2 -o address_test address_test.c address.c
	./address_test
	gcc -Wall -Werror -O2 -o dl_rate_test dl_rate_test.c dl_rate.c
	./dl_rate_test
	gcc -Wall -Werror -O2 -idirafter $(HB)/include -o unzip_test unzip_test.c unzip.c $(HOST_LIBZ)
	./unzip_test
	gcc -Wall -Werror -O2 -o files_test files_test.c files.c
	./files_test
	gcc -Wall -Werror -O2 -o session_test session_test.c session.c
	./session_test
	gcc -Wall -Werror -O2 -o settings_test settings_test.c settings.c
	./settings_test
	gcc -Wall -Werror -O2 -pthread -o phone_test phone_test.c phone.c files.c
	./phone_test
	gcc -Wall -Werror -O2 -DSHARE_NO_CURL -o share_test share_test.c share.c
	./share_test
	gcc -Wall -Werror -O2 -o system_test system_test.c system.c
	./system_test
	gcc -Wall -Werror -O2 -o overlay_test overlay_test.c overlay.c -lm
	./overlay_test
	gcc -Wall -Werror -O2 -DSIDE_CARD_HOST_TEST -o side_card_test side_card_test.c side_card.c
	./side_card_test
	gcc -Wall -Werror -O2 -pthread -DTORRENT_HOST_TEST -o torrent_test torrent_test.c torrent.c
	./torrent_test
	gcc -Wall -Werror -O2 -pthread -DTORBOX_HOST_TEST -DTORRENT_HOST_TEST -o torbox_test torbox_test.c torbox.c torrent.c
	./torbox_test
	gcc -Wall -Werror -O2 -o logins_test logins_test.c logins.c
	./logins_test
	# lang.js (docs/13-idioma.md): only where node is (the PC: node lang_test.js).
	if command -v node >/dev/null; then node lang_test.js; else echo "lang_test.js: no node here, skipped"; fi

clean:
	rm -f *.elf *.o address_test dl_rate_test unzip_test files_test session_test settings_test phone_test system_test overlay_test cur2c cursors_win.h *.built.html \
	    $(TITLE)/*.elf $(TITLE)/*.o

.PHONY: all clean test
