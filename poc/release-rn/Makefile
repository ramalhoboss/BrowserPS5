#   Copyright (C) 2026 BrowserPS5 contributors
#
# This file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.

# The release's ELFs (build-release-rn.sh runs this: WORK has release.h and the pack, OUT gets the ELFs).

ifdef PS5_PAYLOAD_SDK
    include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
    $(error PS5_PAYLOAD_SDK is undefined)
endif

COMMON := ../common
WORK ?= .
OUT ?= .
# the SDK's ports: liblzma
HB := $(PS5_PAYLOAD_SDK)/target/user/homebrew
CFLAGS := -Wall -Werror -O1 -I$(COMMON) -I$(WORK)

all: $(OUT)/BrowserPS5.elf $(OUT)/BrowserPS5-uninstall.elf $(OUT)/BrowserPS5-uninstall-all.elf

$(OUT)/BrowserPS5.elf: install.c ../install_tar/main.c $(COMMON)/poc_log.h $(WORK)/release.h $(WORK)/pack.tar.xz
	$(CC) $(CFLAGS) -I$(HB)/include -o $@ install.c $(HB)/lib/liblzma.a -lSceSystemService

$(OUT)/BrowserPS5-uninstall.elf: uninstall.c ../remove_files/main.c $(COMMON)/poc_log.h
	$(CC) $(CFLAGS) -o $@ uninstall.c -lSceSystemService

$(OUT)/BrowserPS5-uninstall-all.elf: uninstall.c ../remove_files/main.c $(COMMON)/poc_log.h
	$(CC) $(CFLAGS) -DREMOVE_DATA -o $@ uninstall.c -lSceSystemService

# A test's: says what the uninstaller would remove, and removes nothing (never in the release's folder).
dry: $(WORK)/BrowserPS5-uninstall-dry.elf

$(WORK)/BrowserPS5-uninstall-dry.elf: uninstall.c ../remove_files/main.c $(COMMON)/poc_log.h
	$(CC) $(CFLAGS) -DDRY_RUN -o $@ uninstall.c -lSceSystemService

.PHONY: all dry
