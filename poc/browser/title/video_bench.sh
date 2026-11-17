#!/usr/bin/env bash
#   Copyright (C) 2026 BrowserPS5 contributors
#
# This file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# video.c's conversion and mixing on the PC (docs/08-4k.md, "A conversão
# e a mistura"): the time per flip when every flip converts the picture
# again (video_test's bench mode), for the clips and places that matter.
# With OLD (another video.c, e.g. an older one), it also checks that both
# give the same pixels, bit for bit, for one paused picture of every clip
# in 4 places, and measures both.
#
#   bash video_bench.sh OUT [OLD]
#
# Clips in CLIPS (../../decode_probe/make-clips.sh, and two 10-bit ones:
# vp9-1080p-10bit.ivf and vp9-2160p-10bit.ivf, 40 frames of testsrc2,
# libvpx-vp9 -profile:v 2 -pix_fmt yuv420p10le).

set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
out=$(realpath -m -- "${1:?usage: video_bench.sh OUT [OLD]}")
old=${2:+$(realpath -- "$2")}
ff=${HOST_FFMPEG:-$HOME/browserps5-build/host-ffmpeg/stage}
clips=${CLIPS:-$HOME/browserps5-build/media/clips}
mkdir -p "$out/new"

build() {  # <video.c> <binary>
    gcc -std=gnu11 -O3 -mavx2 -Wall -Wextra -Werror -DVIDEO_HOST_TEST -I"$here" -I"$here/../../common" \
        -I"$ff/include" -o "$2" "$here/video_test.c" "$1" "$ff/lib/libavcodec.a" "$ff/lib/libavutil.a" \
        -lpthread -lm -lrt
}
build "$here/video.c" "$out/new/video_test"
versions=new
if [ -n "$old" ]; then
    mkdir -p "$out/old"
    build "$old" "$out/old/video_test"
    versions="old new"
    differences=0
    for clip in vp9-2160p60 vp9-1440p60 vp9-1080p60 vp9-1080p-10bit vp9-2160p-10bit; do
        for mode in paused paused,full paused,cut paused,full,cut; do
            name=$clip-${mode//,/-}
            for v in old new; do
                "$out/$v/video_test" "$clips/$clip.ivf" "$out/$v/$name" "$mode" > "$out/$v/$name.log" 2>&1
            done
            for f in 20 59; do
                a=$(md5sum < "$out/old/$name-$f.ppm" | cut -c1-8)
                b=$(md5sum < "$out/new/$name-$f.ppm" | cut -c1-8)
                if [ "$a" = "$b" ]; then r=same; else r=DIFF; differences=$((differences + 1)); fi
                echo "$name, flip $f: old $a, new $b: $r"
            done
        done
    done
    echo "pictures that differ: $differences"
fi

for v in $versions; do
    for spec in "vp9-2160p60 paused,full,bench" "vp9-2160p60 paused,bench" \
                "vp9-1440p60 paused,full,bench" "vp9-1080p60 paused,full,bench" \
                "vp9-1080p60 paused,bench" "vp9-1080p60 paused,full,cut,bench" \
                "vp9-1080p-10bit paused,full,bench" "vp9-2160p-10bit paused,full,bench"; do
        set -- $spec
        line=$("$out/$v/video_test" "$clips/$1.ivf" "$out/$v/bench" "$2" 2>&1 | grep "convert" | tail -1 \
               | sed 's/.*; convert \([0-9.]*\) ms.*mix \([0-9.]*\) ms.*/convert \1 ms, mix \2 ms/')
        echo "$v $1 $2: $line"
    done
done
