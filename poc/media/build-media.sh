#!/usr/bin/env bash
#   Copyright (C) 2026 BrowserPS5 contributors
#
# This file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# The media stack WebKit's GStreamer player needs, for PS5 payloads
# (docs/03-video-e-gpu.md, part 1):
#
#   FFmpeg 8.1   decoders only (libavcodec and the bits gst-libav links to)
#   GStreamer    1.28 monorepo, as one static libgstreamer-full-1.0 with the
#                plugins below, and GLib 2.86, libffi, PCRE2 and
#                proxy-libintl as its subprojects
#
# Nothing in it makes code at run time: ORC is off (GStreamer's SIMD
# compiler) and PCRE2 is built without its JIT. GStreamer has no plugin
# registry: plugins are registered from the static library, so it never
# starts gst-plugin-scanner (no process is launched) and never dlopen()s.
#
# Run inside WSL, with PS5_PAYLOAD_SDK set (WebKit's SDK, /opt/ps5-payload-sdk).
# Installs into the SDK sysroot under /user/homebrew, as the other WebKit
# dependencies are, and lists what it installed in $OUT/installed-*.txt.
#
#   STEPS="nasm ffmpeg libiconv libffi resolv opus gstreamer"   the default;
#                                                   any subset, in this order

set -euo pipefail

WORK=${WORK:-$HOME/browserps5-build}
OUT=${OUT:-$WORK/media}
SOURCES=$WORK/deps/sources
GST_SRC=${GST_SRC:-$WORK/ext/gstreamer}
TOOLS=$WORK/ext/tools
JOBS=${JOBS:-$(nproc)}
STEPS=${STEPS:-nasm ffmpeg libiconv libffi resolv opus gstreamer}
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

NASM_VER=2.16.03
NASM_SHA256=1412a1c760bbd05db026b6c0d1657affd6631cd0a63cddb6f73cc6d4aa616148
FFMPEG_VER=8.1.3
FFMPEG_SHA256=7138d28c96d9d3e3af4ee3d8cad72741f8ffb40da90c1112235dea3ecd3178a3
ICONV_VER=1.18
ICONV_SHA256=3b08f5f4f9b4eb82f151a7040bfd6fe6c6fb922efe4b1659c66ea933276965e8
FFI_VER=3.4.8
FFI_SHA256=bc9842a18898bfacb0ed1252c4febcc7e78fa139fd27fdc7a3e30d9d9356119b
OPUS_VER=1.5.2
OPUS_SHA256=65c1d2f78b9f2fb20082c38cbe47c951ad5839345876e46941612ee87f9a7ce1
GST_VER=1.28.8
GLIB_VER=2.86.3   # the GLib wrap of that GStreamer

if [[ -z "${PS5_PAYLOAD_SDK:-}" ]]; then
    echo "error: PS5_PAYLOAD_SDK is not set" >&2
    exit 1
fi
if [[ -x "$WORK/meson-venv/bin/meson" ]]; then
    export PATH="$WORK/meson-venv/bin:$PATH"
fi
export PATH="$TOOLS/bin:$PATH"
source "$PS5_PAYLOAD_SDK/toolchain/prospero.sh"
SYSROOT=$PS5_SYSROOT
unset DESTDIR    # prospero.sh sets it to the sysroot; each step installs to its own stage

mkdir -p "$OUT"

fetch() {  # <url> <file> <sha256>
    if [[ ! -f "$SOURCES/$2" ]]; then
        mkdir -p "$SOURCES"
        curl -sSL -o "$SOURCES/$2" "$1"
    fi
    echo "$3  $SOURCES/$2" | sha256sum -c -
}

install_stage() {  # <stage dir> <name>: copy a stage into the sysroot, list the files
    (cd "$1" && find . -type f -o -type l | sed 's|^\./||' | sort) > "$OUT/installed-$2.txt"
    cp -a "$1/." "$SYSROOT/"
    echo "installed $(wc -l < "$OUT/installed-$2.txt") files of $2 into $SYSROOT"
}


# 1. nasm for the PC: FFmpeg's x86 SIMD is written for it. Ubuntu's would
#    need root; this one lives in ~/browserps5-build/ext/tools.
step_nasm() {
    if [[ -x "$TOOLS/bin/nasm" ]] && "$TOOLS/bin/nasm" -v | grep -q "$NASM_VER"; then
        echo "nasm $NASM_VER is there"
        return
    fi
    fetch "https://www.nasm.us/pub/nasm/releasebuilds/$NASM_VER/nasm-$NASM_VER.tar.xz" \
          "nasm-$NASM_VER.tar.xz" "$NASM_SHA256"
    rm -rf "$OUT/nasm-$NASM_VER"
    tar -xJf "$SOURCES/nasm-$NASM_VER.tar.xz" -C "$OUT"
    (cd "$OUT/nasm-$NASM_VER" &&
        env -u CC -u CXX -u AR -u RANLIB -u LD -u AS -u NM -u STRIP ./configure --prefix="$TOOLS" &&
        env -u CC -u CXX -u AR -u RANLIB -u LD -u AS -u NM -u STRIP make -j "$JOBS" &&
        make install)
    "$TOOLS/bin/nasm" -v
}


# 2. FFmpeg: pacbrew-repo's PS5 configuration (FreeBSD target, the SDK's
#    tools), cut down to the decoders the web needs. LGPL parts only.
step_ffmpeg() {
    local src=$OUT/ffmpeg-$FFMPEG_VER
    local stage=$OUT/stage-ffmpeg

    fetch "https://ffmpeg.org/releases/ffmpeg-$FFMPEG_VER.tar.xz" \
          "ffmpeg-$FFMPEG_VER.tar.xz" "$FFMPEG_SHA256"
    rm -rf "$src" "$stage"
    tar -xJf "$SOURCES/ffmpeg-$FFMPEG_VER.tar.xz" -C "$OUT"

    (cd "$src" && ./configure \
        --prefix=/user/homebrew \
        --enable-cross-compile --arch=x86_64 --target-os=freebsd \
        --cc="$CC" --cxx="$CXX" --nm="$NM" --ar="$AR" --ranlib="$RANLIB" \
        --strip="$STRIP" --pkg-config="$PKG_CONFIG" --x86asmexe="$TOOLS/bin/nasm" \
        --enable-static --disable-shared --enable-pic \
        --disable-debug --disable-doc --disable-programs \
        --disable-autodetect --disable-network --disable-everything \
        --enable-pthreads \
        --enable-avcodec --enable-avformat --enable-avutil --enable-avfilter \
        --enable-swresample --disable-swscale --disable-avdevice \
        --enable-decoder=h264,hevc,vp8,vp9,aac,aac_latm,mp3,mp3float,opus,vorbis,flac \
        --enable-decoder=pcm_s16le,pcm_s16be,pcm_f32le,pcm_u8,alac \
        --enable-parser=h264,hevc,vp8,vp9,aac,aac_latm,mpegaudio,opus,vorbis,flac \
        --enable-bsf=h264_mp4toannexb,hevc_mp4toannexb,vp9_superframe_split,aac_adtstoasc)

    make -C "$src" -j "$JOBS"
    make -C "$src" install DESTDIR="$stage"
    install_stage "$stage" ffmpeg
}


# 3. GNU libiconv: GLib needs iconv, and the SDK's libc has none
#    (pacbrew-repo's recipe).
step_libiconv() {
    local src=$OUT/libiconv-$ICONV_VER
    local stage=$OUT/stage-libiconv

    fetch "https://ftp.gnu.org/pub/gnu/libiconv/libiconv-$ICONV_VER.tar.gz" \
          "libiconv-$ICONV_VER.tar.gz" "$ICONV_SHA256"
    rm -rf "$src" "$stage"
    tar -xzf "$SOURCES/libiconv-$ICONV_VER.tar.gz" -C "$OUT"
    (cd "$src" && ./configure --prefix=/user/homebrew --host=x86_64-pc-freebsd \
        --enable-static --disable-shared --disable-nls)
    make -C "$src" -j "$JOBS"
    make -C "$src" install DESTDIR="$stage"
    rm -f "$stage"/user/homebrew/lib/*.la
    install_stage "$stage" libiconv
}


# 4. libffi for GObject: GStreamer's meson port of it has no FreeBSD x86_64
#    ("Unsupported pair"), so the upstream release, found by GLib through
#    its libffi.pc. GObject only uses ffi_call; without the static
#    trampolines nothing in it maps executable memory unless a closure is
#    asked for, and GLib asks for none.
step_libffi() {
    local src=$OUT/libffi-$FFI_VER
    local stage=$OUT/stage-libffi

    fetch "https://github.com/libffi/libffi/releases/download/v$FFI_VER/libffi-$FFI_VER.tar.gz" \
          "libffi-$FFI_VER.tar.gz" "$FFI_SHA256"
    rm -rf "$src" "$stage"
    tar -xzf "$SOURCES/libffi-$FFI_VER.tar.gz" -C "$OUT"
    # The PS5 target refuses the ms_abi attribute. It is only on the inner
    # function of Windows-ABI closures (FFI_WIN64 inside an x86_64 Unix
    # build), which nothing here uses: without it ffiw64.c compiles.
    grep -q "__attribute__((ms_abi))" "$src/src/x86/ffiw64.c"
    sed -i 's/__attribute__((ms_abi))//' "$src/src/x86/ffiw64.c"
    (cd "$src" && ./configure --prefix=/user/homebrew --host=x86_64-pc-freebsd \
        --enable-static --disable-shared --disable-docs --disable-exec-static-tramp \
        --disable-multi-os-directory)
    make -C "$src" -j "$JOBS"
    make -C "$src" install DESTDIR="$stage"
    rm -f "$stage"/user/homebrew/lib/*.la
    install_stage "$stage" libffi
}


# 5. libresolv.a for GIO (gio_libc_stubs.c): res_query() and dn_expand(),
#    which find nothing, and the fstab functions, with no fstab. The SDK's
#    libc has none of them; GIO links libresolv because res_query is not in
#    libc, so the fstab ones come along on every link that needs them.
step_resolv() {
    local stage=$OUT/stage-resolv

    rm -rf "$stage"
    mkdir -p "$stage/user/homebrew/lib" "$OUT/resolv"
    $CC -O2 -Wall -Werror -c "$HERE/gio_libc_stubs.c" -o "$OUT/resolv/gio_libc_stubs.o"
    rm -f "$stage/user/homebrew/lib/libresolv.a"
    $AR rcs "$stage/user/homebrew/lib/libresolv.a" "$OUT/resolv/gio_libc_stubs.o"
    install_stage "$stage" resolv
}


# 6. libopus (BSD) for GStreamer's opusparse, which WebKit's MSE puts after
#    the demuxer for Opus audio (YouTube's WebM), and opusdec.
step_opus() {
    local src=$OUT/opus-$OPUS_VER
    local stage=$OUT/stage-opus

    fetch "https://downloads.xiph.org/releases/opus/opus-$OPUS_VER.tar.gz" \
          "opus-$OPUS_VER.tar.gz" "$OPUS_SHA256"
    rm -rf "$src" "$stage"
    tar -xzf "$SOURCES/opus-$OPUS_VER.tar.gz" -C "$OUT"
    (cd "$src" && ./configure --prefix=/user/homebrew --host=x86_64-pc-freebsd \
        --enable-static --disable-shared --disable-doc --disable-extra-programs)
    make -C "$src" -j "$JOBS"
    make -C "$src" install DESTDIR="$stage"
    rm -f "$stage"/user/homebrew/lib/*.la
    # The SDK's pkg-config only looks in lib/pkgconfig (see step 7).
    if [[ -d "$stage/user/homebrew/libdata/pkgconfig" ]]; then
        mkdir -p "$stage/user/homebrew/lib/pkgconfig"
        mv "$stage"/user/homebrew/libdata/pkgconfig/*.pc "$stage/user/homebrew/lib/pkgconfig/"
        rm -rf "${stage:?}/user/homebrew/libdata"
    fi
    install_stage "$stage" opus
}


# GLib's checks that go wrong with the SDK's libc. Each sed leaves lines
# its own pattern no longer matches, so a rerun changes nothing.
#  - memalign: FreeBSD's malloc.h is an #error, and gmem.c includes it when
#    HAVE_MEMALIGN is set. The function list finds memalign in libc, and the
#    check with malloc.h in its prefix finds it too (meson's has_function
#    retries without the prefix when the prefix fails to compile). Take it
#    out of the list and ask for a usable malloc.h first; gmem.c then uses
#    posix_memalign.
fix_glib() {  # <GLib source directory>
    sed -i "/^  'memalign',\$/d;
            s|^if cc.has_function('memalign', prefix: |if cc.check_header('malloc.h') and cc.has_function('memalign', prefix: |" \
        "$1/meson.build"
    grep -q "^if cc.check_header('malloc.h') and cc.has_function('memalign'" "$1/meson.build" || {
        echo "error: the memalign check in $1/meson.build changed" >&2
        exit 1
    }
}


# 7. GStreamer, its plugins and GLib, as one static libgstreamer-full-1.0.
#    Besides the demuxers, parsers and decoders: playbin's video chain
#    (deinterlace, videobalance from videofilter; a missing one is a
#    missing-plugin message, which WebKit takes for an unsupported format),
#    scaletempo (audiofx, for playbackRate), the WebVTT encoder (subenc) and
#    Opus.
#    The include path finds libiconv's iconv.h (meson looks for iconv with
#    the compiler alone, and the SDK's compiler only sees the sysroot's
#    include/, not user/homebrew/include).
step_gstreamer() {
    local build=$OUT/gst-build
    local stage=$OUT/stage-gstreamer

    if [[ ! -d "$GST_SRC/subprojects/gstreamer" ]]; then
        git clone -q --depth 1 --branch "$GST_VER" \
            https://gitlab.freedesktop.org/gstreamer/gstreamer.git "$GST_SRC"
    fi
    (cd "$GST_SRC" && meson subprojects download glib pcre2 libffi proxy-libintl)
    # FFmpeg's Vorbis decoder in gst-libav (2026-11-18): gst-libav-vorbis.py.
    python3 "$HERE/gst-libav-vorbis.py" "$GST_SRC"
    fix_glib "$GST_SRC/subprojects/glib-$GLIB_VER"

    rm -rf "$build" "$stage"
    local libs=gstreamer-base-1.0,gstreamer-app-1.0,gstreamer-audio-1.0
    libs+=,gstreamer-video-1.0,gstreamer-pbutils-1.0,gstreamer-tag-1.0
    libs+=,gstreamer-fft-1.0,gstreamer-allocators-1.0,gstreamer-codecparsers-1.0

    # GLib, PCRE2 and libintl from their subprojects even when an earlier
    # run left them in the SDK: GStreamer takes glib-mkenums from the glib
    # subproject, which then cannot override the glib-2.0 found in the SDK.
    $MESON setup "$build" "$GST_SRC" \
        --force-fallback-for=glib,pcre2,proxy-libintl \
        -Dc_args="-I$SYSROOT/user/homebrew/include" \
        -Dcpp_args="-I$SYSROOT/user/homebrew/include" \
        -Dbuildtype=release \
        -Ddefault_library=static \
        -Dauto_features=disabled \
        -Dgst-full=enabled \
        -Dgst-full-target-type=static_library \
        -Dgst-full-libraries="$libs" \
        -Dbase=enabled -Dgood=enabled -Dbad=enabled -Dlibav=enabled \
        -Dugly=disabled -Ddevtools=disabled -Dges=disabled \
        -Drtsp_server=disabled -Dgst-examples=disabled -Dpython=disabled \
        -Dtls=disabled -Dlibnice=disabled -Dwebrtc=disabled \
        -Dqt5=disabled -Dqt6=disabled -Dgtk=disabled \
        -Dorc=disabled -Dtools=disabled -Dtests=disabled -Dexamples=disabled \
        -Dbenchmarks=disabled -Dintrospection=disabled -Dnls=disabled \
        -Ddoc=disabled -Dgpl=disabled \
        -Dgstreamer:registry=false \
        -Dgstreamer:ptp-helper=disabled \
        -Dgst-plugins-base:app=enabled \
        -Dgst-plugins-base:playback=enabled \
        -Dgst-plugins-base:typefind=enabled \
        -Dgst-plugins-base:pbtypes=enabled \
        -Dgst-plugins-base:videoconvertscale=enabled \
        -Dgst-plugins-base:audioconvert=enabled \
        -Dgst-plugins-base:audioresample=enabled \
        -Dgst-plugins-base:volume=enabled \
        -Dgst-plugins-base:audiotestsrc=enabled \
        -Dgst-plugins-base:videotestsrc=enabled \
        -Dgst-plugins-base:opus=enabled \
        -Dgst-plugins-good:deinterlace=enabled \
        -Dgst-plugins-good:videofilter=enabled \
        -Dgst-plugins-good:audiofx=enabled \
        -Dgst-plugins-bad:subenc=enabled \
        -Dgst-plugins-bad:opus=enabled \
        -Dgst-plugins-good:isomp4=enabled \
        -Dgst-plugins-good:matroska=enabled \
        -Dgst-plugins-good:audioparsers=enabled \
        -Dgst-plugins-good:autodetect=enabled \
        -Dgst-plugins-good:id3demux=enabled \
        -Dgst-plugins-good:wavparse=enabled \
        -Dgst-plugins-bad:videoparsers=enabled \
        -Dglib:tests=false -Dglib:installed_tests=false \
        -Dglib:xattr=false -Dglib:man-pages=disabled -Dglib:documentation=false \
        -Dpcre2:jit=disabled -Dpcre2:grep=false -Dpcre2:test=false

    $MESON compile -C "$build" -j "$JOBS"
    $MESON install -C "$build" --destdir "$stage"

    # The SDK's pkg-config only looks in lib/pkgconfig. meson puts the .pc
    # files of a FreeBSD build in libdata/pkgconfig, and the plugins' ones
    # (gstreamer-full-1.0.pc requires them) in lib/gstreamer-1.0/pkgconfig.
    # GLib's programs are of no use in the sysroot.
    local hb=${stage:?}/user/homebrew
    mkdir -p "$hb/lib/pkgconfig"
    mv "$hb"/libdata/pkgconfig/*.pc "$hb"/lib/gstreamer-1.0/pkgconfig/*.pc "$hb/lib/pkgconfig/"
    rm -rf "${hb:?}/libdata" "${hb:?}/lib/gstreamer-1.0/pkgconfig" "${hb:?}/bin" \
           "${hb:?}/libexec" "${hb:?}/share/bash-completion"
    install_stage "$stage" gstreamer
}


for step in $STEPS; do
    echo "=== $step"
    "step_$step"
done

echo
ls -la "$SYSROOT/user/homebrew/lib"/libgstreamer-full-1.0.a "$SYSROOT/user/homebrew/lib"/libavcodec.a 2>/dev/null || true
echo "done: $STEPS"
