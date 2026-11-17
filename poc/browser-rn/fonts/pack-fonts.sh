#!/bin/bash
# In WSL: bash pack-fonts.sh   (2026-11-16)
# out/fonts-emoji.tar: Noto Emoji (monochrome, SIL OFL 1.1, from github.com/google/fonts ofl/notoemoji), for the
# emoji no other font of the console has (DejaVu has a few: the smileys, hearts, stars; 🎮 👍 🚀 🛒 were boxes).
# It goes in the title's own fonts (/app0/fonts = /data/homebrew/PPSA97024/fonts): the pages' WebProcess runs in the
# title and reads /app0/fonts/fonts.conf (title/web_host.cpp FONTS_CONF), whose <dir> is /app0/fonts.
# (A first try put it in /user/homebrew/share/fonts, which only the payloads' fontconfig reads: no change.)
# The release takes it too (release-rn/build-release-rn.sh).
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
out=$here/../out/fonts-emoji.tar
w=$HOME/browserps5-build/fonts-emoji
dir=data/homebrew/PPSA97024/fonts
rm -rf "$w"; mkdir -p "$w/$dir"
install -m 644 "$here/NotoEmoji.ttf" "$w/$dir/NotoEmoji.ttf"
install -m 644 "$here/NotoEmoji-OFL.txt" "$w/$dir/NotoEmoji-LICENSE.txt"
# only the files (install_tar refuses the folders above the title's own)
tar --format=ustar --owner=0 --group=0 --numeric-owner --mtime="2026-11-16 00:00:00" -C "$w" -cf "$out" \
    "$dir/NotoEmoji.ttf" "$dir/NotoEmoji-LICENSE.txt"
tar -tvf "$out"
