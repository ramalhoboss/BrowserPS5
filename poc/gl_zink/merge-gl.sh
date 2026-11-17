#!/usr/bin/env bash
# Copyright (C) 2026 BrowserPS5 contributors
#
# This file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# The browser's Mesa as one library: EGL, GLES and gallium linked together
# as libEGL.so.1, from Mesa's own three links. build.sh runs it (BROWSER=1)
# after Mesa is built and installed in <out>/stage; it can run again alone.
#
# Why: the processes our launcher starts load a library that two others need
# once for each. libEGL and libGLESv2 each need libgallium, so the context
# EGL made current was in one copy of it and the gl* functions read the other
# (docs/03-video-e-gpu.md, G2: "composited by ?"). With one library, WebKit
# needs only it, and it needs only libvulkan.so.1.
#
# Usage: merge-gl.sh <out>   (build.sh's OUT, ~/browserps5-build/mesa-browser)

set -euo pipefail

OUT=${1:?usage: merge-gl.sh <out>}
BUILD=$OUT/build
LIB=$OUT/stage/user/homebrew/lib
EGL=src/egl/libEGL.so.1.0.0
GLES=src/mesa/glapi/es2api/libGLESv2.so.2.0.0
GALLIUM=$(cd "$BUILD" && ls src/gallium/targets/dri/libgallium-*.so)

link_line() { ninja -C "$BUILD" -t commands "$1" | tail -1; }

# The exports: EGL's (egl.sym), each function libGLESv2 exports, and
# libgallium's (dri.sym).
mkdir -p "$BUILD/merged"
{
    echo '{'
    echo '  global:'
    echo '    egl*;'
    echo '    MesaGLInteropEGL*;'
    nm -D --defined-only "$BUILD/$GLES" | awk '{print "    " $3 ";"}' | sort -u
    sed -n '/global:/,/local:/{/global:/d;/local:/d;p}' "$BUILD/src/gallium/targets/dri/dri.sym"
    echo '  local:'
    echo '    *;'
    echo '};'
} > "$BUILD/merged/merged.sym"

# libgallium's link with EGL's and GLES's objects added. Their static
# libraries are all in it already.
objects=$( (link_line "$EGL"; link_line "$GLES") | tr ' ' '\n' | grep '\.o$' | tr '\n' ' ')
cmd=" $(link_line "$GALLIUM") "
replace() {  # <what> <from> <to>, in $cmd
    local before=$cmd
    cmd=${cmd/"$2"/"$3"}
    if [[ "$cmd" == "$before" ]]; then
        echo "error: $1 is not in libgallium's link line" >&2
        exit 1
    fi
}
replace "the output" " -o $GALLIUM " " -o merged/libEGL.so.1 $objects "
replace "the soname" " -Wl,-soname,$(basename "$GALLIUM") " " -Wl,-soname,libEGL.so.1 -Wl,-Bsymbolic "
replace "the version script" " -Wl,--version-script $BUILD/src/gallium/targets/dri/dri.sym " \
    " -Wl,--version-script $BUILD/merged/merged.sym "
(cd "$BUILD" && eval "$cmd")

# Every export of the three, and nothing new but one of libgallium's own.
missing=$(comm -23 \
    <(for f in "$EGL" "$GLES" "$GALLIUM"; do nm -D --defined-only "$BUILD/$f" | awk '{print $3}'; done | sort -u) \
    <(nm -D --defined-only "$BUILD/merged/libEGL.so.1" | awk '{print $3}' | sort -u))
if [[ -n "$missing" ]]; then
    echo "error: the merged library does not export: $missing" >&2
    exit 1
fi

rm -f "$LIB"/libEGL.so* "$LIB"/libGLESv2.so* "$LIB"/libgallium-*.so
cp "$BUILD/merged/libEGL.so.1" "$LIB/libEGL.so.1"
ln -s libEGL.so.1 "$LIB/libEGL.so"
echo "merged EGL, GLES and gallium into $LIB/libEGL.so.1" \
     "($(stat -c %s "$LIB/libEGL.so.1") bytes; needs:" \
     "$(readelf -d "$LIB/libEGL.so.1" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p' | tr '\n' ' '))"
