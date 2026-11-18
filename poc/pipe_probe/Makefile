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
CFLAGS := -Wall -Werror -O1 -I$(COMMON) -pthread

all: pipe_probe.elf pipe_room.elf dns_probe.elf

pipe_probe.elf: main.c $(COMMON)/poc_log.h
	$(CC) $(CFLAGS) -o $@ main.c

# How many pipes take a byte now, 6000 tried at most (it stops sooner at the process's limit of open files).
pipe_room.elf: main.c $(COMMON)/poc_log.h
	$(CC) $(CFLAGS) -DPIPE_ROOM=6000 -o $@ main.c

# Several names resolved at once, a few times: how long each took.
dns_probe.elf: dns.c $(COMMON)/poc_log.h
	$(CC) $(CFLAGS) -o $@ dns.c

clean:
	rm -f *.elf

.PHONY: all clean
