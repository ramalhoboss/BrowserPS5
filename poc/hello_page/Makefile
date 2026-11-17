#   Copyright (C) 2026 BrowserPS5 contributors
#
# This file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# Milestone M1 (docs/02-desenho-sem-gpu.md): a fixed HTML page on the TV.
#   hello_page_app.elf  WebKit's UIProcess (main.cpp), runs inside the app
#   starter.elf         bigapp_gl's launcher, embeds hello_page_app.elf
#   hello_page.elf      the launcher payload (../launcher/launcher.c),
#                       embeds starter.elf: this is the one to send
#
# Needs poc/webkit/build-webkit.sh first, and on the PS5 the tars from
# poc/webkit/package.sh and poc/gl_soft (Mesa softpipe), installed with
# install_tar.elf.

ifdef PS5_PAYLOAD_SDK
    include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
    $(error PS5_PAYLOAD_SDK is undefined)
endif

WEBKIT_BUILD ?= $(HOME)/browserps5-build/webkit-build
WEBKIT_LIB   := $(firstword $(wildcard $(WEBKIT_BUILD)/lib/libWebKit.so*))

COMMON   := ../common
BIGAPP   := ../bigapp_gl
LAUNCHER := ../launcher
PL       := ../webkit/process-launcher
CFLAGS   := -Wall -Werror -O2 -I$(COMMON) -I$(PL)
CXXFLAGS := -Wall -O2 -std=c++20 -I$(COMMON) \
            -I$(WEBKIT_BUILD)/WebKit/Headers \
            -I$(WEBKIT_BUILD)/JavaScriptCore/Headers

# Milestone M2: make URL=https://example.com/ loads that page instead of the
# fixed one (make clean first: the ELF names stay the same).
ifdef URL
    CXXFLAGS += -DPAGE_URL='"$(URL)"'
endif
# make TYPE_TEST=Lisboa types that into a test page by itself and logs what
# the field got (see type_test() in main.cpp).
ifdef TYPE_TEST
    CXXFLAGS += -DTYPE_TEST='"$(TYPE_TEST)"'
endif
# Milestone M3: make RUN_SECONDS=300 runs that long (options quits earlier).
ifdef RUN_SECONDS
    CXXFLAGS += -DRUN_SECONDS=$(RUN_SECONDS)
endif

LAUNCHER_FD := $(shell sed -n 's/^\#define BROWSERPS5_LAUNCHER_FD //p' $(PL)/launcher_proto.h)

all: hello_page.elf

display.o: display.c display.h $(COMMON)/video_out.h $(COMMON)/poc_log.h
	$(CC) $(CFLAGS) -c -o $@ display.c

# The screenshot's PNG uses the SDK sysroot's static zlib (libWebKit has its
# own copy, not exported).
HB := $(PS5_PAYLOAD_SDK)/target/user/homebrew
shot.o: shot.c shot.h
	$(CC) $(CFLAGS) -I$(HB)/include -c -o $@ shot.c

input.o: input.c input.h $(COMMON)/poc_log.h
	$(CC) $(CFLAGS) -c -o $@ input.c

ime.o: ime.c ime.h $(COMMON)/poc_log.h
	$(CC) $(CFLAGS) -c -o $@ ime.c

hello_page_app.elf: main.cpp display.h display.o ime.h ime.o input.h input.o \
                    shot.h shot.o $(COMMON)/poc_log.h
	@test -n "$(WEBKIT_LIB)" || { echo "libWebKit not found in $(WEBKIT_BUILD)/lib"; exit 1; }
	$(CXX) $(CXXFLAGS) -o $@ main.cpp display.o ime.o input.o shot.o \
	    $(WEBKIT_LIB) $(HB)/lib/libz.a -lSceVideoOut -lSceSystemService \
	    -lScePad -lSceUserService -lSceImeDialog

# bigapp_gl's launcher embeds ./payload.elf (.incbin, from the current
# directory). Add -DBIGAPP_TRACE_CRASH to have it stay attached to the app
# and log where it crashes (see trace_crash() there); traced, every new
# thread also stops the app, so it is off by default.
starter.elf: $(BIGAPP)/main.c hello_page_app.elf
	cp hello_page_app.elf payload.elf
	$(CC) $(CFLAGS) -DBIGAPP_CTL_FD=$(LAUNCHER_FD) $(STARTER_FLAGS) \
	    -o $@ $(BIGAPP)/main.c \
	    $(COMMON)/hbldr/elfldr.c $(COMMON)/hbldr/pt.c \
	    -lSceSystemService -lSceUserService

# launcher.c embeds ./starter.elf.
hello_page.elf: $(LAUNCHER)/launcher.c starter.elf $(PL)/launcher_proto.h
	$(CC) $(CFLAGS) -o $@ $(LAUNCHER)/launcher.c \
	    $(COMMON)/elfldr/elfldr.c $(COMMON)/elfldr/pt.c

clean:
	rm -f *.elf *.o

.PHONY: all clean
