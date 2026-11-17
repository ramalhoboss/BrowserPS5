#   Copyright (C) 2026 BrowserPS5 contributors
#
# This file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# gl_zink.elf is ../gl_soft's test built for the console's GPU (-DGL_ZINK):
# it links the zink Mesa that build.sh puts in MESA_STAGE. On the PS5 it
# loads those libraries from /user/homebrew/lib, so install
# mesa-zink-libs.tar there first (../install_tar), and open the owner title
# (../gpu_broker, PPSA97010) before sending it.

ifdef PS5_PAYLOAD_SDK
    include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
    $(error PS5_PAYLOAD_SDK is undefined)
endif

MESA_STAGE ?= $(HOME)/browserps5-build/mesa-zink/stage/user/homebrew

COMMON := ../common
CFLAGS := -Wall -Werror -O1 -DGL_ZINK -I$(COMMON) -I$(MESA_STAGE)/include
LDADD  := -L$(MESA_STAGE)/lib -lEGL_zink -lGLESv2_zink

# gl_zink_probe-*.elf: probe.c with one zink setting each (ZINK_ENV).
PROBES := gl_zink_probe.elf gl_zink_probe-lazy.elf gl_zink_probe-noshobj.elf \
          gl_zink_probe-sync.elf gl_zink_probe-nothread.elf

all: gl_zink.elf $(PROBES)

gl_zink.elf: ../gl_soft/main.c $(COMMON)/poc_log.h
	$(CC) $(CFLAGS) -o $@ ../gl_soft/main.c $(LDADD)

gl_zink_probe.elf: probe.c $(COMMON)/poc_log.h
	$(CC) $(CFLAGS) -o $@ probe.c $(LDADD)

gl_zink_probe-lazy.elf: probe.c $(COMMON)/poc_log.h
	$(CC) $(CFLAGS) '-DZINK_ENV="ZINK_DESCRIPTORS=lazy"' -o $@ probe.c $(LDADD)

gl_zink_probe-noshobj.elf: probe.c $(COMMON)/poc_log.h
	$(CC) $(CFLAGS) '-DZINK_ENV="ZINK_DEBUG=noshobj"' -o $@ probe.c $(LDADD)

gl_zink_probe-sync.elf: probe.c $(COMMON)/poc_log.h
	$(CC) $(CFLAGS) '-DZINK_ENV="ZINK_DEBUG=sync,flushsync"' -o $@ probe.c $(LDADD)

gl_zink_probe-nothread.elf: probe.c $(COMMON)/poc_log.h
	$(CC) $(CFLAGS) '-DZINK_ENV="GALLIUM_THREAD=0"' -o $@ probe.c $(LDADD)

clean:
	rm -f *.elf

.PHONY: all clean
