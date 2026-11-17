#   Copyright (C) 2026 BrowserPS5 contributors
#
# This file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.

# play_after.elf on its own (the browser's Makefile builds it too, into the app: main.c says why).

ifdef PS5_PAYLOAD_SDK
    include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
    $(error PS5_PAYLOAD_SDK is undefined)
endif

COMMON := ../common
CFLAGS := -Wall -Werror -O1 -I$(COMMON) -I../browser/title

all: play_after.elf

play_after.elf: main.c play_after.h $(COMMON)/poc_log.h ../browser/title/link.h
	$(CC) $(CFLAGS) -o $@ main.c -lSceSystemService -lSceUserService

clean:
	rm -f *.elf

.PHONY: all clean
