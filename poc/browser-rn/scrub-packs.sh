#!/bin/bash
# In WSL: bash scrub-packs.sh          (2026-11-14)
#
# The build machine's user names out of the two packs of binaries a release is made of, WITHOUT building anything
# again (../common/scrub-paths.py says what is changed: the name's letters, in place, nothing else):
#   out/lib-rn-full.tar     the engine's library and its two processes
#   out/title-rn-full.tar   the title: its eboot.bin is made again from the ELF it was made of (the work folder's
#                           eboot.elf, checked first to give this very eboot.bin), scrubbed, by the same tool
# The packs of before stay beside them as *-before-scrub.tar. Every member keeps its place in its tar.
set -euo pipefail
POC=/mnt/c/Users/<you>/Desktop/BrowserPS5/BrowserPS5-WebKit/poc; RN=$POC/browser-rn/out; ID=${RN_ID:-PPSA97024}
VK=${PS5_VULKAN:-$HOME/browserps5-build/ext/PS5_Vulkan}; TOOL=$VK/build/host/ps5-native-tool
WORK=${TITLE_WORK:-$HOME/browserps5-build/browser_title_rn_full}
SCRUB="python3 -I $POC/common/scrub-paths.py"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT

again() {  # <tar> <folder it was unpacked into>: the tar made again of that folder, each member in its place
    tar -tf "$RN/$1" > "$T/$1.list"
    (cd "$2" && tar --format=ustar --owner=0 --group=0 --numeric-owner --no-recursion -cf "$T/$1" -T "$T/$1.list")
    [[ "$(tar -tf "$T/$1" | md5sum)" == "$(md5sum < "$T/$1.list")" ]] || { echo "error: $1 made again has other members" >&2; exit 1; }
    [[ -f "$RN/${1%.tar}-before-scrub.tar" ]] || cp -p "$RN/$1" "$RN/${1%.tar}-before-scrub.tar"
    cp "$T/$1" "$RN/$1"
}

echo "== the engine (lib-rn-full.tar)"
mkdir "$T/lib"; tar -xf "$RN/lib-rn-full.tar" -C "$T/lib"
$SCRUB $(cd "$T/lib" && find . -type f \( -name "*.SOVERSION" -o -name "*.elf" \) | sed "s|^\./|$T/lib/|")
again lib-rn-full.tar "$T/lib"

echo "== the title (title-rn-full.tar)"
mkdir "$T/title"; tar -xf "$RN/title-rn-full.tar" -C "$T/title"
eboot=$T/title/data/homebrew/$ID/eboot.bin
if $SCRUB --check "$eboot" > /dev/null 2>&1; then
    echo "its eboot.bin has no name in it already"
else
    cp "$WORK/eboot.elf" "$T/eboot.elf"
    "$TOOL" self --sign --in "$T/eboot.elf" --out "$T/same.bin" --magic 0x1D3D154F > "$T/tool.log" 2>&1
    cmp -s "$T/same.bin" "$eboot" || { echo "error: $WORK/eboot.elf is not what this title's eboot.bin was made of: build the title again (title-full-rn.sh, with TITLE_SCRUB=1)" >&2; exit 1; }
    $SCRUB "$T/eboot.elf"
    "$TOOL" self --sign --in "$T/eboot.elf" --out "$T/new.bin" --magic 0x1D3D154F > "$T/tool.log" 2>&1
    [[ $(stat -c %s "$T/new.bin") == $(stat -c %s "$eboot") ]] || { echo "error: the new eboot.bin has another size" >&2; exit 1; }
    cat "$T/new.bin" > "$eboot"  # (the file's mode and place stay)
fi
others=$(cd "$T/title" && find . -type f ! -name eboot.bin | sed "s|^\./|$T/title/|")
$SCRUB $others
again title-rn-full.tar "$T/title"

echo "== checked again, from the tars as they are now"
mkdir "$T/check"; tar -xf "$RN/lib-rn-full.tar" -C "$T/check"; tar -xf "$RN/title-rn-full.tar" -C "$T/check"
$SCRUB --check $(find "$T/check" -type f)
for t in lib-rn-full.tar title-rn-full.tar; do
    echo "$t: $(stat -c %s "$RN/$t") bytes (before: $(stat -c %s "$RN/${t%.tar}-before-scrub.tar")), $(tar -tf "$RN/$t" | wc -l) members"
done
