#!/bin/bash
# 2026-11-13: BrowserPS5 RN for the PS5's media tab. A title's tab is set when the system registers it, and
# PPSA97024 was registered as a game: so the media one is the same app under a title ID of its own (PPSA97025),
# of the media category (applicationCategoryType 65536), which ShadowMountPlus registers as a new title. The
# title cannot listen there and calls the menu instead (common/title_link.h). Same data folder, same engine.
#   wsl -e bash media-rn.sh [ID]
# Out (browser-rn/out): title-rn-full-<ID>.tar, menu-rn-full-<ID>.tar, menu-rn-full-probe-<ID>.tar,
# exit-loadexec-marker-<ID>.tar. Needs out/title-rn-full-base.tar (title-full-rn.sh).
set -e
W=$HOME/browserps5-build; R=$(cd "$(dirname "$0")" && pwd); ID=${1:-PPSA97025}
cd $R
export RN_ID=$ID
X="ENGINE_PLUS=1 ENGINE_DIR=full"
RN_MEDIA=1 MENU_NAME=menu-rn-full-$ID.tar EXTRA_MAKE="$X" RN_WEBKIT_BUILD=$W/webkit-build-full TITLE_BASE=title-rn-full-base.tar TITLE_OUT=title-rn-full-$ID.tar bash build-rn.sh title 2>&1 | grep -v "warning: unused" | tail -3
MENU_NAME=menu-rn-full-probe-$ID.tar EXTRA_MAKE="$X SHOW=1 SHOW_YT=1 SHOW_PROBE=1 SHOW_PROBE_GL=1 SHOW_PROBE_NOTIFY=1" RN_WEBKIT_BUILD=$W/webkit-build-full bash build-rn.sh 2>&1 | grep -v "warning: unused" | tail -1
T=$(mktemp -d); mkdir -p $T/data/homebrew/$ID
echo "the title ends with sceSystemServiceLoadExec(exit) (browser/title/title.cpp)" > $T/data/homebrew/$ID/exit_loadexec
(cd $T && tar --format=ustar --owner=0 --group=0 -cf $R/out/exit-loadexec-marker-$ID.tar data/homebrew/$ID/exit_loadexec)
rm -rf $T
echo "param.json: $(tar -xOf out/title-rn-full-$ID.tar data/homebrew/$ID/sce_sys/param.json | grep -o '"titleId": "[A-Z0-9]*"\|"applicationCategoryType": [0-9]*\|"contentId": "[^"]*"' | tr '\n' ' ')"
strings -a <(tar -xOf out/menu-rn-full-$ID.tar data/homebrew/$ID/browser.elf) | grep -m2 "/data/homebrew/PPSA970"
