#!/bin/bash
# 2026-11-13: the "full" engine's title from the sources as they are (the engine itself is not built again):
# out/title-rn-full.tar, the title of the PS5's media tab (PPSA97025, category 65536) since that night.
# RN_ID=PPSA97024 RN_MEDIA=0 gives the one of before (the games tab). No menu in out/ is touched.
#   wsl -e bash title-full-rn.sh
W=$HOME/browserps5-build; R=$(cd "$(dirname "$0")" && pwd); ID=${RN_ID:-PPSA97024}
cd $R
TITLE_SCRUB=1 TITLE_QUIET_LOG=1 TITLE_LOCKS=1 WEBKIT_BUILD=$W/webkit-build-full TITLE_WORK=$W/browser_title_rn_full TITLE_BASE_OUT=title-rn-full-base.tar bash build-title-rn.sh 2>&1 | tail -3
grep "undefined symbol\|error:" /tmp/build-title-rn-full-base.log 2>/dev/null | sed "s/>>> .*//" | cut -c1-200 | sort | uniq -c | sort -rn | head -10
X="ENGINE_PLUS=1 ENGINE_DIR=full"
MENU_NAME=menu-rn-full-scratch.tar EXTRA_MAKE="$X" RN_WEBKIT_BUILD=$W/webkit-build-full TITLE_BASE=title-rn-full-base.tar TITLE_OUT=title-rn-full.tar bash build-rn.sh title 2>&1 | grep -v "warning: unused" | tail -2
rm -f $R/out/menu-rn-full-scratch.tar
echo "title-rn-full.tar: $(tar -xOf out/title-rn-full.tar data/homebrew/$ID/sce_sys/param.json | grep -o '"titleId": "[A-Z0-9]*"\|"applicationCategoryType": [0-9]*' | tr '\n' ' ')"
