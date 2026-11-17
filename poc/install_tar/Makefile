#   Copyright (C) 2026 BrowserPS5 contributors
#
# This file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.

ifdef PS5_PAYLOAD_SDK
    include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
    $(error PS5_PAYLOAD_SDK is undefined)
endif

COMMON := ../common
CFLAGS := -Wall -Werror -O1 -I$(COMMON)

all: install_tar.elf

install_tar.elf: main.c $(COMMON)/poc_log.h
	$(CC) $(CFLAGS) -o $@ main.c

clean:
	rm -f *.elf

.PHONY: all clean
