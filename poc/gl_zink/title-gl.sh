#!/usr/bin/env bash
# Copyright (C) 2026 BrowserPS5 contributors
#
# This file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# The browser's Mesa for the browser title (2c-2, docs/05-webprocess-no-
# titulo.md): EGL, GLES and gallium (softpipe and zink) as one relocatable
# object, from the same inputs as merge-gl.sh's libEGL.so.1. Every symbol in
# it is local but the EGL and GLES functions (as merge-gl.sh exports them), so
# Mesa's own utilities, zlib and the rest never meet the copies the title
# already has (its RADV, WebKit's zlib).
#
# zink's two Vulkan entry points, vkGetInstanceProcAddr and
# vkGetDeviceProcAddr, stay undefined: in the title they come from
# title_vulkan.c, over the title's own RADV (the GPU owner's), with no
# broker in between. A payload's libvulkan.so.1 cannot be used there: its
# RADV reserves the very ranges the owner maps its buffers at.
#
# Usage: title-gl.sh <out>   (build.sh's OUT, ~/browserps5-build/mesa-browser;
# BROWSER=1 build.sh and merge-gl.sh first). Writes <out>/title/libEGL-title.o.

set -euo pipefail

OUT=${1:?usage: title-gl.sh <out>}
BUILD=$OUT/build
SDK=${PS5_PAYLOAD_SDK:-/opt/ps5-payload-sdk}
EGL=src/egl/libEGL.so.1.0.0
GLES=src/mesa/glapi/es2api/libGLESv2.so.2.0.0
GALLIUM=$(cd "$BUILD" && ls src/gallium/targets/dri/libgallium-*.so)
SYM=$BUILD/merged/merged.sym
[[ -s $SYM ]] || { echo "error: $SYM is missing (run merge-gl.sh first)" >&2; exit 2; }

link_line() { ninja -C "$BUILD" -t commands "$1" | tail -1; }

mkdir -p "$OUT/title"
cd "$BUILD"
# EGL's and GLES's objects, then libgallium's inputs (its archives include
# what the other two link).
inputs=()
for word in $( (link_line "$EGL"; link_line "$GLES") | tr ' ' '\n' | grep '\.o$'); do
    inputs+=("$word")
done
for word in $(link_line "$GALLIUM"); do
    case $word in
        -Wl,--whole-archive|-Wl,--no-whole-archive|-Wl,--start-group|-Wl,--end-group)
            inputs+=("${word#-Wl,}") ;;
        *.o|*.a) inputs+=("$word") ;;
    esac
done

raw=$OUT/title/libEGL-title.raw.o
obj=$OUT/title/libEGL-title.o
"$SDK/bin/ld.lld" -r -o "$raw" "${inputs[@]}"

# What WebKit calls: EGL's functions (and the interop ones) and each one
# libGLESv2 exports. Not libgallium's dri.sym names, which are Mesa's own
# (driGetOptionsXml and the like are in the title's RADV too).
{
    echo 'egl*'
    echo 'MesaGLInteropEGL*'
    nm -D --defined-only "$BUILD/$GLES" | awk '{print $3}' | sort -u
} > "$OUT/title/keep.txt"
"$SDK/bin/llvm-objcopy" --wildcard --keep-global-symbols="$OUT/title/keep.txt" "$raw" "$obj"
rm -f -- "$raw"

defined=$("$SDK/bin/llvm-nm" --defined-only --extern-only "$obj" | wc -l)
"$SDK/bin/llvm-nm" --undefined-only "$obj" | awk '{print $NF}' | sort -u > "$OUT/title/undefined.txt"
echo "libEGL-title.o: $(stat -c %s "$obj") bytes, $defined global definitions," \
     "$(wc -l < "$OUT/title/undefined.txt") undefined names (title/undefined.txt)"
