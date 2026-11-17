#!/bin/bash
# In WSL: bash build-release-rn.sh [version]   (2026-11-14)
#
# BrowserPS5's release, from the packages the tests install (browser-rn/out):
#   out/release/BrowserPS5-<version>/
#     BrowserPS5.elf                 the installer: everything, packed with xz, in one ELF (install.c)
#     BrowserPS5-uninstall.elf       takes the browser off the console; the user's data stays (uninstall.c)
#     BrowserPS5-uninstall-all.elf   the same, and the user's data
#     send.ps1                       sends an ELF to the console from Windows, for who has no sender
#     README.md, CHANGELOG.md, LICENSE, THIRD-PARTY-NOTICES.md
#     SHA256SUMS.txt
#   out/release/BrowserPS5-<version>.zip
#
# The pack is one tar of what these tars hold, in this order (a later one's file takes an earlier one's place):
# lib-rn-full.tar (the engine's library and processes), title-rn-full.tar (the title), menu-rn-full.tar (the
# browser itself, in the title's folder), pdfjs-rn.tar (the PDF reader), web-env-normal.tar and
# exit-loadexec-marker.tar (the title's settings files), adblock-rn.tar (the ad blocker's list) and fonts-emoji.tar
# (Noto Emoji in the title's fonts). Nothing of the user's data folder may be in it.
set -euo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
POC=$(cd "$HERE/.." && pwd)
RN=$POC/browser-rn/out
W=$HOME/browserps5-build/release-rn
SDK=${PS5_PAYLOAD_SDK:-/opt/ps5-payload-sdk}
ID=PPSA97024  # (2026-11-14: the games tab's title again; PPSA97025, the media tab's, had 4 GiB of memory in all)
VERSION=${1:-$(cat "$HERE/VERSION")}
OUT=$RN/release/BrowserPS5-$VERSION

rm -rf "$W/stage"; mkdir -p "$W/stage" "$OUT"
# MENU_TAR=<another menu's tar in out/>: a trial pack with that menu in the place of the normal one (the next
# normal menu, before it takes the name: console/after-tests.sh).
# (2026-11-14: and adblock-rn.tar, the ad blocker's own list: browser-rn/adblock/make-rules.js, node on the PC)
# (2026-11-16: and fonts-emoji.tar, Noto Emoji in the title's fonts: browser-rn/fonts/pack-fonts.sh)
for t in lib-rn-full.tar title-rn-full.tar "${MENU_TAR:-menu-rn-full.tar}" pdfjs-rn.tar web-env-normal.tar exit-loadexec-marker.tar adblock-rn.tar fonts-emoji.tar; do
    [[ -f "$RN/$t" ]] || { echo "error: $RN/$t is missing" >&2; exit 1; }
    tar -xf "$RN/$t" -C "$W/stage"
done
# What other homebrew on a console shares with the browser, for a console that has none of it (install.c writes
# these only where there is none, and never over a copy that is there): Mesa (zink over RADV: libEGL.so.1, with
# GLES and gallium in it, and libvulkan.so.1: poc/gl_zink, BROWSER=1), the certificates (Mozilla's list, as
# Ubuntu's ca-certificates and curl's bundle) and the fonts and fonts.conf the engine's own processes read
# (poc/webkit/package.sh).
MESA=$HOME/browserps5-build/mesa-browser
RUNTIME=$HOME/browserps5-build/webkit-media-jit-runtime.tar
for f in "$MESA/build/merged/libEGL.so.1" "$MESA/vulkan/libvulkan.so.1" "$RUNTIME" "$POC/install_tar/ca-bundle.tar"; do
    [[ -f "$f" ]] || { echo "error: $f is missing" >&2; exit 1; }
done
install -D -m 755 "$MESA/build/merged/libEGL.so.1" "$W/stage/user/homebrew/lib/libEGL.so.1"
install -D -m 755 "$MESA/vulkan/libvulkan.so.1" "$W/stage/user/homebrew/lib/libvulkan.so.1"
tar -xf "$RUNTIME" -C "$W/stage" user/homebrew/etc user/homebrew/share
tar -xf "$POC/install_tar/ca-bundle.tar" -C "$W/stage"
# 2026-11-14: the build machine's user names are in no file of a release (common/scrub-paths.py: a compiler leaves
# the paths of its sources in what it makes, and the account's name with them). The engine's and the title's packs
# come without them (pack-lib-rn.sh; the title's build with TITLE_SCRUB=1; browser-rn/scrub-packs.sh for packs made
# before); Mesa's two libraries are done here; then every file of the pack is read: a name left, and no release.
python3 -I "$POC/common/scrub-paths.py" "$W/stage/user/homebrew/lib/libEGL.so.1" "$W/stage/user/homebrew/lib/libvulkan.so.1"
find "$W/stage" -type f -print0 | xargs -0 python3 -I "$POC/common/scrub-paths.py" --check \
    || { echo "error: a file of the pack has the build machine's user name in it (bash browser-rn/scrub-packs.sh)" >&2; exit 1; }
# Only the title's folder, the browser's own in /user/homebrew and that shared set; never the user's data.
bad=$(cd "$W/stage" && find . -type f | grep -v "^./data/homebrew/$ID/\|^./user/homebrew/browserps5-rn/\|^./user/homebrew/lib/libWebKitFull.so.SOVERSION$\|^./user/homebrew/lib/libEGL.so.1$\|^./user/homebrew/lib/libvulkan.so.1$\|^./user/homebrew/etc/\|^./user/homebrew/share/fonts/" || true)
[[ -z "$bad" ]] || { echo "error: files outside the browser's places: $bad" >&2; exit 1; }
[[ ! -e "$W/stage/user/homebrew/browserps5-rn/data" ]] || { echo "error: the pack has a data folder" >&2; exit 1; }
for f in data/homebrew/$ID/eboot.bin data/homebrew/$ID/browser.elf data/homebrew/$ID/sce_sys/param.json \
         user/homebrew/lib/libWebKitFull.so.SOVERSION user/homebrew/browserps5-rn/full/WebProcess.elf \
         user/homebrew/browserps5-rn/full/NetworkProcess.elf user/homebrew/lib/libEGL.so.1 user/homebrew/lib/libvulkan.so.1 \
         user/homebrew/etc/ssl/cert.pem user/homebrew/etc/ca-bundle.crt user/homebrew/etc/fonts/fonts.conf; do
    [[ -s "$W/stage/$f" ]] || { echo "error: the pack has no $f" >&2; exit 1; }
done
# (2026-11-14: a game again, category 0. A media title has 4 GiB of memory in all.)
grep -q '"applicationCategoryType": 0,' "$W/stage/data/homebrew/$ID/sce_sys/param.json" || { echo "error: the title is not a game (category 0)" >&2; exit 1; }
# param.json last: ShadowMountPlus takes a folder for a title once it is there (install_tar writes in the tar's order).
(cd "$W/stage" && { find user data -type f ! -path "data/homebrew/$ID/sce_sys/param.json" | LC_ALL=C sort; echo "data/homebrew/$ID/sce_sys/param.json"; } > "$W/files.txt")
tar --format=ustar --owner=0 --group=0 --numeric-owner --mtime="2026-11-14 00:00:00" -C "$W/stage" -cf "$W/pack.tar" -T "$W/files.txt"
# 8 MiB of dictionary (-6): the console unpacks it with about 9 MiB (as XPS5's installer does)
xz -6e -T0 -c "$W/pack.tar" > "$W/pack.tar.xz"
xz -t "$W/pack.tar.xz"
SHA=$(sha256sum "$W/pack.tar" | cut -d' ' -f1)
cat > "$W/release.h" <<EOF
#define RELEASE_VERSION "$VERSION"
#define RELEASE_PACK "$W/pack.tar.xz"
#define RELEASE_PACK_SHA256 "$SHA"
EOF
echo "pack: $(wc -l < "$W/files.txt") files, $(stat -c %s "$W/pack.tar") bytes, packed $(stat -c %s "$W/pack.tar.xz"), sha256 $SHA"

PS5_PAYLOAD_SDK=$SDK make -C "$HERE" WORK="$W" OUT="$OUT" all
python3 -I "$POC/common/scrub-paths.py" --check "$OUT"/BrowserPS5.elf "$OUT"/BrowserPS5-uninstall.elf "$OUT"/BrowserPS5-uninstall-all.elf \
    || { echo "error: the installer or an uninstaller has the build machine's user name in it" >&2; exit 1; }
for f in send.ps1 README.md CHANGELOG.md THIRD-PARTY-NOTICES.md LICENSE; do
    [[ -f "$HERE/$f" ]] && cp "$HERE/$f" "$OUT/"
done
(cd "$OUT" && rm -f SHA256SUMS.txt && sha256sum BrowserPS5.elf BrowserPS5-uninstall.elf BrowserPS5-uninstall-all.elf > SHA256SUMS.txt)
(cd "$RN/release" && rm -f "BrowserPS5-$VERSION.zip" && zip -q -r "BrowserPS5-$VERSION.zip" "BrowserPS5-$VERSION")
ls -la --time-style=+%H:%M "$OUT" | awk '{print $5, $7}'
echo "zip: $(stat -c %s "$RN/release/BrowserPS5-$VERSION.zip") bytes, sha256 $(sha256sum "$RN/release/BrowserPS5-$VERSION.zip" | cut -d' ' -f1)"
