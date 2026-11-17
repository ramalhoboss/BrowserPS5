#!/usr/bin/env bash
#   Copyright (C) 2026 BrowserPS5 contributors
#
# This file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# video.c on the PC (docs/08-4k.md, "Testes no PC"): builds video_test with
# a host FFmpeg (HOST_FFMPEG, the stage of a Linux build of the same
# FFmpeg with the h264, hevc, vp8 and vp9 decoders) and runs it on the
# 4K probe's clips (../../decode_probe/make-clips.sh): a 1080p60 video in a
# page, a 2160p60 one in full screen (paused: one picture, the 4K to 1080p
# scaling) and the 1080p60 one in full screen. Writes PNGs into OUT.
#
#   bash video_test.sh OUT

set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
out=$(realpath -m -- "${1:?usage: video_test.sh OUT}")
ff=${HOST_FFMPEG:-$HOME/browserps5-build/host-ffmpeg/stage}
clips=${CLIPS:-$HOME/browserps5-build/media/clips}
mkdir -p "$out"

gcc -std=gnu11 -O3 -mavx2 -Wall -Wextra -Werror -DVIDEO_HOST_TEST -I"$here/../../common" -I"$ff/include" \
    -o "$out/video_test" "$here/video_test.c" "$here/video.c" \
    "$ff/lib/libavcodec.a" "$ff/lib/libavutil.a" -lpthread -lm -lrt

run() {  # <name> <clip> [mode]
    echo "== $1"
    "$out/video_test" "$clips/$2" "$out/$1" "${3:-}"
}
run page vp9-1080p60.ivf
run full4k vp9-2160p60.ivf paused
run full vp9-1080p60.ivf full

# With a host GStreamer (HOST_GST, the stage of a static Linux build of the
# same GStreamer's core): the WebProcess's sink in a pipeline, with a pause
# and a seek (sink_test.c).
gst=${HOST_GST:-$HOME/browserps5-build/host-gst/stage}
if [[ -d "$gst" ]]; then
    pc=$(find "$gst" -name gstreamer-base-1.0.pc -printf '%h' -quit)
    # shellcheck disable=SC2046 # one word per flag
    gcc -std=gnu11 -O3 -mavx2 -Wall -Wextra -Werror -Wno-unused-parameter -DVIDEO_HOST_TEST \
        -I"$here/../../common" -I"$here/../../media/ps5audiosink" -I"$ff/include" \
        $(PKG_CONFIG_PATH=$pc pkg-config --static --cflags gstreamer-base-1.0) \
        -o "$out/sink_test" "$here/sink_test.c" "$here/video.c" \
        "$here/../../media/ps5audiosink/ps5videosink.c" \
        "$ff/lib/libavcodec.a" "$ff/lib/libavutil.a" \
        $(PKG_CONFIG_PATH=$pc pkg-config --static --libs gstreamer-base-1.0) -lpthread -lm -lrt
    echo "== sink"
    "$out/sink_test" "$clips/vp9-1080p60.ivf" "$out/sink"
    # Frames stopping 0.7 s (none may be dropped), and a start again after
    # the end (the title must not lose the caps to the flush after them).
    echo "== sink stall"
    "$out/sink_test" "$clips/vp9-1080p60.ivf" "$out/sink-stall" stall
    echo "== sink again"
    "$out/sink_test" "$clips/vp9-1080p60.ivf" "$out/sink-again" again
fi

# The PPMs as PNGs, to look at.
python3 -I - "$out" <<'EOF'
import glob, os, struct, sys, zlib

for ppm in sorted(glob.glob(os.path.join(sys.argv[1], "*.ppm"))):
    data = open(ppm, "rb").read()
    parts = data.split(b"\n", 3)
    w, h = map(int, parts[1].split())
    pixels = parts[3]
    raw = b"".join(b"\0" + pixels[y * w * 3:(y + 1) * w * 3] for y in range(h))
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))
    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))
    open(ppm[:-4] + ".png", "wb").write(png)
    os.remove(ppm)
    print("wrote", ppm[:-4] + ".png")
EOF
