#   Copyright (C) 2026 BrowserPS5 contributors
#
# This file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# Milestone M1b (docs/02-desenho-sem-gpu.md):
#   launcher_test.elf  the launcher payload; embeds starter.elf
#   starter.elf        bigapp_gl's launcher with the control socket option;
#                      embeds ui_test.elf, which runs inside the app
#   child.tar          spawn_ipc's child.elf for /user/homebrew/browserps5/poc,
#                      to install with install_tar.elf first

ifdef PS5_PAYLOAD_SDK
    include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
    $(error PS5_PAYLOAD_SDK is undefined)
endif

COMMON := ../common
BIGAPP := ../bigapp_gl
PL     := ../webkit/process-launcher
CFLAGS := -Wall -Werror -O1 -I$(COMMON) -I$(PL)

# The starter must put the socket where launcher_proto.h says.
LAUNCHER_FD := $(shell sed -n 's/^\#define BROWSERPS5_LAUNCHER_FD //p' $(PL)/launcher_proto.h)

all: launcher_test.elf child.tar

ui_test.elf: ui_test.c ../spawn_ipc/ipc_proto.h $(PL)/launcher_client.h $(PL)/launcher_proto.h $(COMMON)/poc_log.h
	$(CC) $(CFLAGS) -o $@ ui_test.c

# bigapp_gl's launcher embeds ./payload.elf (.incbin, from the current
# directory).
starter.elf: $(BIGAPP)/main.c ui_test.elf
	cp ui_test.elf payload.elf
	$(CC) $(CFLAGS) -DBIGAPP_CTL_FD=$(LAUNCHER_FD) -o $@ $(BIGAPP)/main.c \
	    $(COMMON)/hbldr/elfldr.c $(COMMON)/hbldr/pt.c \
	    -lSceSystemService -lSceUserService

launcher_test.elf: launcher.c starter.elf $(PL)/launcher_proto.h $(COMMON)/elfldr/elfldr.c $(COMMON)/elfldr/pt.c
	$(CC) $(CFLAGS) -o $@ launcher.c $(COMMON)/elfldr/elfldr.c $(COMMON)/elfldr/pt.c

child.tar: ../spawn_ipc/child.elf
	rm -rf stage
	mkdir -p stage/user/homebrew/browserps5/poc
	cp $< stage/user/homebrew/browserps5/poc/child.elf
	# Only the file: install_tar refuses directory entries such as user/.
	tar --format=ustar -C stage -cf $@ user/homebrew/browserps5/poc/child.elf
	rm -rf stage

clean:
	rm -f *.elf *.tar

.PHONY: all clean
