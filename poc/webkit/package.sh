#!/usr/bin/env bash
#   Copyright (C) 2026 BrowserPS5 contributors
#
# This file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# Pack what WebKit needs on the PS5 into one tar for poc/install_tar:
#   user/homebrew/lib/<soname of libWebKit>       debug info stripped
#   user/homebrew/browserps5/WebProcess.elf       the paths hello_page asks
#   user/homebrew/browserps5/NetworkProcess.elf   the launcher to start
#   user/homebrew/etc/fonts/fonts.conf            runtime/fonts.conf
#   user/homebrew/share/fonts/dejavu/*.ttf        Ubuntu's fonts-dejavu-core
#   user/homebrew/etc/ssl/cert.pem                Ubuntu's ca-certificates
#                                                 (Mozilla's CA list)
# Each third-party file goes with its license.
#
# Mesa (libEGL.so.1, with GLES and gallium in it, and libvulkan.so.1) is a
# separate tar: BROWSER=1 poc/gl_zink/build.sh.
#
# Run inside WSL/Linux after build-webkit.sh, with PS5_PAYLOAD_SDK set.
# Usage: package.sh [output.tar]   (default: $WORK/webkit-runtime.tar)

set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
WORK=${WORK:-$HOME/browserps5-build}
BUILD=${BUILD:-$WORK/webkit-build}
OUT=${1:-$WORK/webkit-runtime.tar}
FONTS=${FONTS:-/usr/share/fonts/truetype/dejavu}
CA_BUNDLE=${CA_BUNDLE:-/etc/ssl/certs/ca-certificates.crt}

if [[ -z "${PS5_PAYLOAD_SDK:-}" ]]; then
    echo "error: PS5_PAYLOAD_SDK is not set" >&2
    exit 1
fi
source "$PS5_PAYLOAD_SDK/toolchain/prospero.sh"

need() {
    [[ -e "$1" ]] || { echo "error: $1 not found${2:+ ($2)}" >&2; exit 1; }
}
need "$BUILD/bin/WebProcess" "run build-webkit.sh first"
need "$BUILD/bin/NetworkProcess" "run build-webkit.sh first"
need "$FONTS/DejaVuSans.ttf" "apt-get install fonts-dejavu-core"
need /usr/share/doc/fonts-dejavu-core/copyright
need "$CA_BUNDLE" "apt-get install ca-certificates"
need /usr/share/doc/ca-certificates/copyright

webkit_lib=$(ls "$BUILD"/lib/libWebKit.so* 2>/dev/null | head -1)
need "$webkit_lib" "run build-webkit.sh first"
soname=$(readelf -d "$webkit_lib" | sed -n 's/.*Library soname: \[\(.*\)\]/\1/p')
soname=${soname:-$(basename "$webkit_lib")}

STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
HB="$STAGE/user/homebrew"
mkdir -p "$HB/lib" "$HB/browserps5" "$HB/etc/fonts" "$HB/etc/ssl" \
         "$HB/share/fonts/dejavu"

"$STRIP" --strip-debug -o "$HB/lib/$soname" "$webkit_lib"
"$STRIP" --strip-debug -o "$HB/browserps5/WebProcess.elf" "$BUILD/bin/WebProcess"
"$STRIP" --strip-debug -o "$HB/browserps5/NetworkProcess.elf" "$BUILD/bin/NetworkProcess"

cp "$HERE/runtime/fonts.conf" "$HB/etc/fonts/fonts.conf"
cp "$FONTS"/DejaVu*.ttf "$HB/share/fonts/dejavu/"
cp /usr/share/doc/fonts-dejavu-core/copyright "$HB/share/fonts/dejavu/LICENSE"
cp "$CA_BUNDLE" "$HB/etc/ssl/cert.pem"
cp /usr/share/doc/ca-certificates/copyright "$HB/etc/ssl/LICENSE"

# Regular files only: install_tar refuses directory entries such as user/.
(cd "$STAGE" && find user -type f | sort > "$STAGE/files")
tar --format=ustar -C "$STAGE" -cf "$OUT" -T "$STAGE/files"

echo "packed $(wc -l < "$STAGE/files") files into $OUT ($(du -h "$OUT" | cut -f1)):"
tar -tvf "$OUT" | awk '{printf "  %10s %s\n", $3, $6}'
