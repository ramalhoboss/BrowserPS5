#   Copyright (C) 2026 BrowserPS5 contributors
#
# This file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# gl_soft.elf links the Mesa that build-mesa-soft.sh puts in MESA_STAGE.
# On the PS5 it loads those libraries from /user/homebrew/lib, so install
# mesa-soft-libs.tar there first (../install_tar).
#
# bigapp_soft.elf is bigapp_gl's launcher with gl_soft built to also show
# its frames on the TV (-DSHOW_ON_TV), inside an app process.

ifdef PS5_PAYLOAD_SDK
    include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
    $(error PS5_PAYLOAD_SDK is undefined)
endif

MESA_STAGE ?= $(HOME)/browserps5-build/mesa-soft/stage/user/homebrew

COMMON := ../common
BIGAPP := ../bigapp_gl
CFLAGS := -Wall -Werror -O1 -I$(COMMON) -I$(MESA_STAGE)/include
LDADD  := -L$(MESA_STAGE)/lib -lEGL -lGLESv2

all: gl_soft.elf bigapp_soft.elf

gl_soft.elf: main.c $(COMMON)/poc_log.h
	$(CC) $(CFLAGS) -o $@ main.c $(LDADD)

gl_soft_tv.elf: main.c $(COMMON)/poc_log.h $(COMMON)/video_out.h
	$(CC) $(CFLAGS) -DSHOW_ON_TV -o $@ main.c $(LDADD) \
	    -lSceVideoOut -lSceSystemService

# The launcher embeds ./payload.elf (.incbin, from the current directory).
bigapp_soft.elf: $(BIGAPP)/main.c gl_soft_tv.elf
	cp gl_soft_tv.elf payload.elf
	$(CC) $(CFLAGS) -o $@ $(BIGAPP)/main.c \
	    $(COMMON)/hbldr/elfldr.c $(COMMON)/hbldr/pt.c \
	    -lSceSystemService -lSceUserService

clean:
	rm -f *.elf

.PHONY: all clean
