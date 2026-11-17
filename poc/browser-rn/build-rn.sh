# BrowserPS5 RN (2026-11-10): the second app, title PPSA97024, next to v1
# (PPSA97020, untouched). The same WebKit engine (/user/homebrew/browserps5:
# WebProcess.elf, NetworkProcess.elf, the libs) with:
#   - its own data: /user/homebrew/browserps5-rn/data (make APP_RN=1, main.cpp)
#   - its own pages: pages/* here over ../browser's (the PS5 home look)
#   - service workers on (SW_ON=1: WhatsApp Web, Telegram Web)
#   - its own title: v1's title (results/title-hw.tar, the same eboot: it
#     only uses /app0) with param.json, icon0.png and the backgrounds here.
# Builds in a copy of ../browser (WSL ~/browserps5-build/rn), never in it.
#   wsl -e bash .../browser-rn/build-rn.sh [title]
# Out: rn/menu-rn.tar (browser.elf) and, with "title", rn/title-rn.tar.
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
poc=$(cd -- "$here/.." && pwd)
R=$HOME/browserps5-build/rn
# RN_ID=PPSA97025 (2026-11-13): the same app as another title (media-rn.sh: the media tab's). The objects that
# hold the ID are made again when it changes (the stamp says which one the copy was last built for).
# 2026-11-13 23:50: PPSA97025 is THE title now (the media tab, and the games
# one, PPSA97024, was removed). RN_ID=PPSA97024 RN_MEDIA=0: the title of before.
ID=${RN_ID:-PPSA97024}
mkdir -p "$R" "$here/out"
for d in bigapp_gl common gl_zink hello_page launcher play_after torrent webkit; do
    ln -sfn "$poc/$d" "$R/$d"
done
# The copy: sources only (objects, ELFs and tars are rebuilt or not needed).
rsync -a --delete --exclude '*.o' --exclude '*.elf' --exclude '*.tar' --exclude '*.built.html' \
      --exclude 'title/art' --exclude '*_test' "$poc/browser/" "$R/browser/"
cp -r "$here/pages/." "$R/browser/"
# The PS5 look goes after v1's themes (theme-rn.css), so v1's changes still come in.
cat "$here/pages/theme-rn.css" >> "$R/browser/theme.css"
# 2026-11-14: RN's own texts in English, at the end of the copy's lang.js (v1's file stays as it is).
cat "$here/pages/lang-rn.js" >> "$R/browser/lang.js"
# 2026-11-14: the tables of the other languages (pages/lang-more.js, made by lang/make-lang-more.py from lang/*.json)
[[ -f "$here/pages/lang-more.js" ]] && cat "$here/pages/lang-more.js" >> "$R/browser/lang.js"
# 2026-11-11: the start page as the PS5's home screen: RN's style and script
# go at the end of v1's own (pages/start-rn.css, start-rn.js), so v1's page
# stays as it is and its changes still come in.
# 2026-11-15: which file of the scene's patch repository serves which game (pages/patches-index.js, made by
# patches/make-index.js) goes before the page's script, which reads it (patchesPanel()).
python3 "$here/pages/start-rn.py" "$R/browser/start.html" "$here/pages/start-rn.css" "$here/pages/start-rn.js" "$here/pages/patches-index.js"
# 2026-11-11: the settings page said "BrowserPS5 NK" (it came from that app): RN, in the copy only.
grep -q "<b>BrowserPS5 <i>NK</i></b>" "$R/browser/settings.html" || { echo "settings.html: the name to change is not there"; exit 1; }
sed -i "s|<b>BrowserPS5 <i>NK</i></b>|<b>BrowserPS5 <i>RN</i></b>|" "$R/browser/settings.html"
# The keyboard is RN's own page (pages/keyboard.html, copied over the browser's above).
cd "$R/browser"
if [[ ${1:-} == pages ]]; then  # only the pages, to look at them on the PC
    PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk make start.built.html chrome.built.html menu.built.html settings.built.html phone.built.html >/dev/null
    mkdir -p "$here/out/pages"; cp start.built.html chrome.built.html menu.built.html settings.built.html phone.built.html "$here/out/pages/"
    echo "pages in $here/out/pages"; exit 0
fi
# RN_WEBKIT_BUILD=<build dir>: the UI linked with another libWebKit (the
# "plus" engine: ~/browserps5-build/webkit-build-plus with
# EXTRA_MAKE="ENGINE_PLUS=1"; its library has a soname of its own,
# browser-rn/pack-lib-rn.sh).
# EXTRA_MAKE="SPEED_TEST=1" MENU_NAME=menu-rn-speed.tar: a test build. The UI
# is linked anew each time (main.cpp is compiled in that step), so a change
# of flags always takes.
menu=${MENU_NAME:-menu-rn.tar}
rm -f title/browser_ui.elf browser_gpu.elf
if [[ "$(cat .rn-title-id 2>/dev/null)" != "$ID" ]]; then
    rm -f *.o title/*.o
    echo "$ID" > .rn-title-id
fi
PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk make APP_RN=1 SW_ON=1 RN_TITLE_ID=$ID ${EXTRA_MAKE:-} \
    WEBKIT_BUILD=${RN_WEBKIT_BUILD:-$HOME/browserps5-build/webkit-build-media-jit} browser_gpu.elf 2>&1 | { grep -E " error|Error |warning: " || true; } | head -20
test -f browser_gpu.elf
T=$(mktemp -d); mkdir -p "$T/data/homebrew/$ID"; cp browser_gpu.elf "$T/data/homebrew/$ID/browser.elf"
(cd "$T" && tar --format=ustar --owner=0 --group=0 -cf "$here/out/$menu" "data/homebrew/$ID/browser.elf")
rm -rf "$T"
echo "$menu: ELF $(md5sum browser_gpu.elf | cut -c1-8)"

if [[ ${1:-} == title ]]; then
    T=$(mktemp -d)
    # The title: out/title-rn-base.tar when built (build-title-rn.sh: title_memory
    # with joined ranges and its numbers in the log), else v1's title-hw.tar.
    # TITLE_BASE=<tar in out/> and TITLE_OUT=<name>: another title next to it
    # (2026-11-11: title-rn-async.tar, the WebKit built with ENABLE_ASYNC_SCROLLING).
    base=$here/out/${TITLE_BASE:-title-rn-base.tar}; [[ -f $base ]] || base=$poc/results/title-hw.tar
    title_out=$here/out/${TITLE_OUT:-title-rn.tar}
    echo "title from $(basename "$base")"
    tar -xf "$base" -C "$T"
    mv "$T/data/homebrew/PPSA97020" "$T/data/homebrew/$ID"
    S=$T/data/homebrew/$ID/sce_sys
    cp "$here/param.json" "$S/param.json"
    if [[ $ID != PPSA97024 ]]; then  # another title: its own ID, content ID and concept
        sed -i "s|PPSA97024|$ID|g; s|BROWSERPS5RN0024|BROWSERPS5RN00${ID: -2}|; s|\"conceptId\": \"97024\"|\"conceptId\": \"${ID#PPSA}\"|" "$S/param.json"
        grep -q "\"titleId\": \"$ID\"" "$S/param.json" || { echo "param.json: the title ID did not change"; exit 1; }
    fi
    # RN_MEDIA=1 (2026-11-12): the title of the media category (applicationCategoryType 65536, the
    # PS5's media tab). Such a title may not listen on a port: it calls the menu instead (common/title_link.h).
    # (2026-11-14: off by default again. A media application has 4 GiB in all, 3 of direct memory: a second
    # film in a session ran it out and the title closed, once with the console; .)
    if [[ ${RN_MEDIA:-0} == 1 ]]; then
        grep -q '"applicationCategoryType": 0,' "$S/param.json" || { echo "param.json: the category to change is not there"; exit 1; }
        sed -i 's|"applicationCategoryType": 0,|"applicationCategoryType": 65536,|' "$S/param.json"
    fi
    cp "$here/art/icon0.png" "$here/art/pic0.png" "$S/"
    gcc -O2 -o "$R/bc7dds" "$poc/tests-v1/bc7dds.c" -lm
    "$R/bc7dds" "$here/art/pic0.raw" "$S/pic0.dds"
    cp "$S/pic0.dds" "$S/pic1.dds"; cp "$S/pic0.dds" "$S/pic2.dds"
    # 2026-11-10 21:50: SharedArrayBuffer in the title's WebProcess (web_host
    # reads /app0/web-env.txt): WhatsApp Web stays there, crossOriginIsolated.
    cp "$here/web-env.txt" "$T/data/homebrew/$ID/web-env.txt"
    # 2026-11-10 22:50: the starting screen's picture (title/splash.c draws on it)
    cp "$here/art/pic0.raw" "$T/data/homebrew/$ID/splash-rn.raw"
    # 2026-11-11 (the "full" engine, Web Audio): the HRTF panner's impulse responses, WebKit's own file
    # (webkit/audio/AudioBusPlayStation.cpp reads /app0/audio/<name>.wav); an engine without Web Audio never asks.
    mkdir -p "$T/data/homebrew/$ID/audio"
    cp "$HOME/browserps5-build/WebKit/Source/WebCore/platform/audio/resources/Composite.wav" "$T/data/homebrew/$ID/audio/"
    (cd "$T" && tar --format=ustar --owner=0 --group=0 -cf "$title_out" "data/homebrew/$ID")
    rm -rf "$T"
    echo "$(basename "$title_out"): $(md5sum "$title_out" | cut -c1-8) ($(du -m "$title_out" | cut -f1) MiB)"
fi
