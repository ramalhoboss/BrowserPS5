#!/usr/bin/env bash
#   Copyright (C) 2026 BrowserPS5 contributors
#
# This file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# Build WebKit (WebCore + WebKit2: libWebKit, WebProcess, NetworkProcess)
# for the PS5 with the upstream PlayStation port and the open
# ps5-payload-sdk. Milestone M0 of docs/02-desenho-sem-gpu.md.
#
# EGL and GLES are the browser's Mesa (poc/gl_zink/build.sh BROWSER=1): one
# libEGL.so.1 with both, and zink and softpipe behind them (merge-gl.sh says
# why one). The headers come from the Mesa softpipe build
# (poc/gl_soft/build-mesa-soft.sh).
#
# Run inside WSL/Linux, with PS5_PAYLOAD_SDK set, after build-icu.sh,
# build-libcxx.sh and the dependencies (cairo, curl, harfbuzz, ...) are in
# the SDK sysroot. Needs: cmake >= 3.20, ninja, perl, python3, ruby, git.
#
# What it changes in the WebKit tree, on top of ../jsc_shell/build-jsc.sh:
#   - DNS.cpp: include sys/socket.h for AF_INET
#   - WebKit/PlatformPlayStation.cmake: WKRunLoop file name case
#   - WebProcessMain.cpp, NetworkProcessMain.cpp: no dlopen() of the
#     dependencies (Sony ships each one as a .sprx; here they are linked
#     statically into libWebKit)
#
# NINJA_ARGS="-k 0" keeps going after errors, to collect them all at once.
# CONFIGURE_ONLY=1 stops after cmake.
# MEDIA=1 builds WebKit with video (docs/03-video-e-gpu.md, V2) in its own
# directory, webkit-build-media: GStreamer and FFmpeg from
# ../media/build-media.sh, the PS5's sound from ../media/ps5audiosink. The
# source patches for it are applied in both builds, and change nothing
# without MEDIA. VIDEO_DEBUG=1 adds logs on a video frame's way to the GPU
# compositor (V4).
# JIT=1 turns on JavaScriptCore's JIT (docs/04-jit.md): the baseline JIT, the
# DFG and the regular-expression JIT, not the FTL nor the WebAssembly JITs.
# It builds in its own directory (webkit-build-media-jit with MEDIA,
# webkit-build-jit without). The jit/ patches are applied in every build and
# change nothing without it. "ninja -C <dir> jsc" there builds the jsc shell
# with the same settings (../jsc_shell/bench.js).
# IPC_TCP=1 links libWebKit with ipc-transport/ (docs/05-webprocess-no-
# titulo.md, 2c-1): WebKit's IPC over TCP and named shared memory, so a
# title can be one of the processes. Same build directory: only libWebKit
# is linked again, and again without it. BROWSERPS5_IPC_TCP=0 in env.txt
# turns it off at run time, in every process.
# HTTP2=1 links the libcurl of http2/build-http2.sh (docs/22-http2.md):
# the SDK's 8.18.0 with nghttp2, from $H2_ROOT, in place of the SDK's (which
# has no HTTP/2). WebKit's CurlContext then asks for HTTP/2 on every https:
# by itself. Same build directory: only libWebKit is linked again, and again
# without it.

set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
JSC_SHELL="$HERE/../jsc_shell"
WORK=${WORK:-$HOME/browserps5-build}
WEBKIT_SRC=${WEBKIT_SRC:-$WORK/WebKit}
# The revision docs/01-inventario-port-webkit.md was written against.
WEBKIT_REV=${WEBKIT_REV:-95075894acb3349f591612e69cb586e157acb09f}
MEDIA=${MEDIA:-}
JIT=${JIT:-}
IPC_TCP=${IPC_TCP:-}
HTTP2=${HTTP2:-}
H2_ROOT=${H2_ROOT:-$WORK/ext/http2/root/user/homebrew}
JIT_SUFFIX=${JIT:+-jit}
if [[ -n "$MEDIA" ]]; then
    BUILD=${BUILD:-$WORK/webkit-build-media$JIT_SUFFIX}
else
    BUILD=${BUILD:-$WORK/webkit-build$JIT_SUFFIX}
fi
if [[ -n "$JIT" ]]; then
    JIT_OPTIONS=(-DENABLE_JIT=ON -DENABLE_DFG_JIT=ON -DENABLE_FTL_JIT=OFF
                 -DENABLE_WEBASSEMBLY_BBQJIT=OFF -DENABLE_WEBASSEMBLY_OMGJIT=OFF)
else
    JIT_OPTIONS=(-DENABLE_JIT=OFF)
fi
MESA_SOFT=${MESA_SOFT:-$WORK/mesa-soft/stage/user/homebrew}
MESA_GL=${MESA_GL:-$WORK/mesa-browser/stage/user/homebrew/lib/libEGL.so.1}
JOBS=${JOBS:-$(nproc)}

if [[ -z "${PS5_PAYLOAD_SDK:-}" ]]; then
    echo "error: PS5_PAYLOAD_SDK is not set" >&2
    exit 1
fi
for tool in cmake ninja perl python3 ruby git; do
    command -v "$tool" >/dev/null || { echo "error: $tool not found" >&2; exit 1; }
done

source "$PS5_PAYLOAD_SDK/toolchain/prospero.sh"

HBLIB="$PS5_SYSROOT$PS5_HBROOT/lib"

# Static dependencies, in the SDK sysroot. libWebKit links them all in one
# group, because static libraries do not bring their own dependencies.
DEPS=(cairo pixman-1 fontconfig expat freetype harfbuzz-icu harfbuzz
      png16 jpeg webp webpdemux sharpyuv xml2 lzma z bz2
      brotlidec brotlicommon zstd curl ssl crypto psl sqlite3 wpe-1.0
      icui18n icuuc icudata)
DEP_GROUP="-Wl,--start-group"
for dep in "${DEPS[@]}"; do
    lib="$HBLIB/lib$dep.a"
    if [[ "$dep" == curl && -n "$HTTP2" ]]; then
        lib="$H2_ROOT/lib/libcurl.a"  # with nghttp2 (HTTP2=1, below)
    fi
    if [[ ! -f "$lib" ]]; then
        echo "error: $lib not found; build the dependencies first" >&2
        exit 1
    fi
    DEP_GROUP+=" $lib"
done
# With MEDIA, GStreamer's static libraries and plugins, FFmpeg and GLib, as
# gstreamer-full-1.0.pc lists them (once each: the group needs no order),
# plus GIO, which WebCore's GStreamer code also calls, and its libresolv.
CMAKE_MEDIA=()
SHARED_MEDIA=""
if [[ -n "$MEDIA" ]]; then
    PKG_CONFIG="$PS5_PAYLOAD_SDK/bin/prospero-pkg-config"
    if ! "$PKG_CONFIG" --exists gstreamer-full-1.0; then
        echo "error: no gstreamer-full-1.0, run poc/media/build-media.sh first" >&2
        exit 1
    fi
    gst_libs=$("$PKG_CONFIG" --libs --static gstreamer-full-1.0 gio-2.0 \
               | tr ' ' '\n' | awk 'NF && !seen[$0]++')
    # Its -Wl,--undefined=gst_init_static_plugins (the plugins' table) goes
    # on libWebKit's link only: on every link line it pulls all of GStreamer
    # into WebProcess.elf and NetworkProcess.elf too (15 MB each).
    DEP_GROUP+=" $(grep -v -- '^-Wl,--undefined=' <<< "$gst_libs" | tr '\n' ' ' || true)"
    SHARED_MEDIA=" $(grep -- '^-Wl,--undefined=' <<< "$gst_libs" | tr '\n' ' ' || true)"
    DEP_GROUP+=" -lresolv -lSceAudioOut"
    gst_dirs=$("$PKG_CONFIG" --cflags-only-I gstreamer-full-1.0 \
               | tr ' ' '\n' | sed -n 's/^-I//p' | awk '!seen[$0]++' | paste -sd ';')
    CMAKE_MEDIA=(-DBROWSERPS5_MEDIA=ON "-DBROWSERPS5_GSTREAMER_INCLUDE_DIRS=$gst_dirs")
fi
# Closed in step 1, after our replacements for Sony's libraries.

if [[ ! -e "$MESA_SOFT/include/EGL/egl.h" ]]; then
    echo "error: $MESA_SOFT/include/EGL/egl.h not found, run" \
         "poc/gl_soft/build-mesa-soft.sh first" >&2
    exit 1
fi
if [[ ! -e "$MESA_GL" ]]; then
    echo "error: $MESA_GL not found, run BROWSER=1 poc/gl_zink/build.sh first" >&2
    exit 1
fi

mkdir -p "$WORK"


# 1. Open replacements for Sony's libraries: memory-extra (same as the jsc
#    shell), process-launcher/process-initialization, CertificateStore,
#    library-bundle and libwpe's backend loader. Plus libc-fixes, for libc
#    calls that misbehave on the PS5.
mkdir -p "$WORK/shims" "$WORK/process-launcher" "$WORK/wpe-loader"
"$CXX" -O2 -fPIC -std=c++17 -I"$JSC_SHELL/shims/include" \
    -c "$JSC_SHELL/shims/src/vss.cpp" -o "$WORK/shims/vss.o"
rm -f "$WORK/shims/libMemoryExtra.a"
"$AR" rcs "$WORK/shims/libMemoryExtra.a" "$WORK/shims/vss.o"

"$CXX" -O2 -fPIC -std=c++17 -Wall -Werror \
    -I"$HERE/process-launcher/include" \
    -c "$HERE/process-launcher/process-launcher.cpp" \
    -o "$WORK/process-launcher/process-launcher.o"
rm -f "$WORK/process-launcher/libProcessLauncher.a"
"$AR" rcs "$WORK/process-launcher/libProcessLauncher.a" \
    "$WORK/process-launcher/process-launcher.o"

"$CC" -O2 -fPIC -Wall -Werror -isystem "$HBLIB/../include/wpe-1.0" \
    -c "$HERE/wpe-loader/wpe-loader.c" -o "$WORK/wpe-loader/wpe-loader.o"
rm -f "$WORK/wpe-loader/libWPELoader.a"
"$AR" rcs "$WORK/wpe-loader/libWPELoader.a" "$WORK/wpe-loader/wpe-loader.o"

mkdir -p "$WORK/crash-report"
"$CC" -O2 -fPIC -Wall -Werror -I"$HERE/../common" \
    -c "$HERE/crash-report/crash-report.c" -o "$WORK/crash-report/crash-report.o"
# 2026-11-11: SQLite's log written in blocks (storage/sqlite-batch.c; a write
# costs 0.75 ms on the PS5 whatever its size), in the same archive.
"$CC" -O2 -fPIC -Wall -Werror -I"$HBLIB/../include" \
    -c "$HERE/storage/sqlite-batch.c" -o "$WORK/crash-report/sqlite-batch.o"
rm -f "$WORK/crash-report/libCrashReport.a"
"$AR" rcs "$WORK/crash-report/libCrashReport.a" "$WORK/crash-report/crash-report.o" \
    "$WORK/crash-report/sqlite-batch.o"

mkdir -p "$WORK/libc-fixes"
"$CC" -O2 -fPIC -Wall -Werror \
    -c "$HERE/libc-fixes/libc-fixes.c" -o "$WORK/libc-fixes/libc-fixes.o"
rm -f "$WORK/libc-fixes/libLibcFixes.a"
"$AR" rcs "$WORK/libc-fixes/libLibcFixes.a" "$WORK/libc-fixes/libc-fixes.o"

# libWebKit itself calls memory-extra (WTF, bmalloc) and launchProcess (the
# UIProcess), but the port links them only into the executables, libwpe
# needs the loader, every CRASH() calls the crash report (step 3) and the
# --wrap'ed libc calls (step 4) go to libc-fixes: all of them join the
# dependency group.
DEP_GROUP+=" $WORK/shims/libMemoryExtra.a"
DEP_GROUP+=" $WORK/process-launcher/libProcessLauncher.a"
DEP_GROUP+=" $WORK/wpe-loader/libWPELoader.a"
DEP_GROUP+=" $WORK/crash-report/libCrashReport.a"
DEP_GROUP+=" $WORK/libc-fixes/libLibcFixes.a"
SHIMS=("$WORK/shims/libMemoryExtra.a"
       "$WORK/process-launcher/libProcessLauncher.a"
       "$WORK/wpe-loader/libWPELoader.a"
       "$WORK/crash-report/libCrashReport.a"
       "$WORK/libc-fixes/libLibcFixes.a")
SHIMS_SUM="$WORK/shims.sum"

# With MEDIA, the PS5's sound as a GStreamer element, which WebCore
# registers (media/GStreamerCommon.cpp.patch).
if [[ -n "$MEDIA" ]]; then
    mkdir -p "$WORK/ps5audiosink"
    # BROWSERPS5_CRASH_HOOK: a WebProcess that crashes closes the sink's
    # sound ports first (crash-report.c).
    # shellcheck disable=SC2046 # one word per flag
    # ../common: audio_ring.h, the title's rings that it writes into.
    "$CC" -O2 -fPIC -Wall -Werror -DBROWSERPS5_CRASH_HOOK -I"$HERE/../common" \
        $("$PKG_CONFIG" --cflags gstreamer-audio-1.0) \
        -c "$HERE/../media/ps5audiosink/ps5audiosink.c" \
        -o "$WORK/ps5audiosink/ps5audiosink.o"
    # And the video's sink for the title (docs/08-4k.md, D2): video_ring.h.
    # shellcheck disable=SC2046 # one word per flag
    "$CC" -O2 -fPIC -Wall -Werror -I"$HERE/../common" \
        $("$PKG_CONFIG" --cflags gstreamer-base-1.0) \
        -c "$HERE/../media/ps5audiosink/ps5videosink.c" \
        -o "$WORK/ps5audiosink/ps5videosink.o"
    rm -f "$WORK/ps5audiosink/libPs5AudioSink.a"
    "$AR" rcs "$WORK/ps5audiosink/libPs5AudioSink.a" "$WORK/ps5audiosink/ps5audiosink.o" \
        "$WORK/ps5audiosink/ps5videosink.o"
    DEP_GROUP+=" $WORK/ps5audiosink/libPs5AudioSink.a"
    SHIMS+=("$WORK/ps5audiosink/libPs5AudioSink.a")
    SHIMS_SUM="$WORK/shims-media.sum"
fi
# With IPC_TCP, WebKit's IPC over TCP and named shared memory (2c-1).
IPC_WRAP=""
if [[ -n "$IPC_TCP" ]]; then
    mkdir -p "$WORK/ipc-transport"
    "$CC" -O2 -fPIC -Wall -Werror \
        -c "$HERE/ipc-transport/ipc_transport.c" -o "$WORK/ipc-transport/ipc_transport.o"
    rm -f "$WORK/ipc-transport/libIpcTransport.a"
    "$AR" rcs "$WORK/ipc-transport/libIpcTransport.a" "$WORK/ipc-transport/ipc_transport.o"
    DEP_GROUP+=" $WORK/ipc-transport/libIpcTransport.a"
    SHIMS+=("$WORK/ipc-transport/libIpcTransport.a")
    # dup: only the title (2c-2) does more than the plain call.
    for call in socketpair sendmsg recvmsg shm_open shm_unlink dup; do
        IPC_WRAP+=" -Wl,--wrap=$call -Wl,-u,__wrap_$call"
    done
fi
# With HTTP2, nghttp2 for the libcurl above, and CMake's FindCURL pointed at
# the same libcurl (else CURL::libcurl links the SDK's first). Only the
# library: the headers are the same curl 8.18.0's, and another include
# directory would change every WebCore compile line (a whole rebuild).
# Without HTTP2 after a build with it, the SDK's libcurl again (CMake keeps
# the value in its cache).
CMAKE_CURL=()
if [[ -n "$HTTP2" ]]; then
    if [[ ! -f "$H2_ROOT/lib/libnghttp2.a" ]]; then
        echo "error: $H2_ROOT/lib/libnghttp2.a not found; run http2/build-http2.sh first" >&2
        exit 1
    fi
    DEP_GROUP+=" $H2_ROOT/lib/libnghttp2.a"
    SHIMS+=("$H2_ROOT/lib/libcurl.a" "$H2_ROOT/lib/libnghttp2.a")
    CMAKE_CURL=(-DCURL_LIBRARY_RELEASE="$H2_ROOT/lib/libcurl.a")
elif grep -q '^CURL_LIBRARY_RELEASE:FILEPATH=.*/ext/http2/' "$BUILD/CMakeCache.txt" 2>/dev/null; then
    CMAKE_CURL=(-DCURL_LIBRARY_RELEASE="$HBLIB/libcurl.a")
fi
DEP_GROUP+=" -Wl,--end-group"

# Ninja does not see these archives (they reach the link through
# CMAKE_CXX_STANDARD_LIBRARIES), so when one of them changes, delete
# libWebKit to have it linked again. Each build keeps its own sum.
shim_sum=$(cat "${SHIMS[@]}" | sha256sum)
if [[ "$(cat "$SHIMS_SUM" 2>/dev/null)" != "$shim_sum" ]]; then
    rm -f "$BUILD"/lib/libWebKit.so*
    echo "$shim_sum" > "$SHIMS_SUM"
fi

# The headers WebKit includes as <process-launcher.h>,
# <process-initialization/nk-*.h>, <CertificateStore/CertificateStore.h> and
# "library-bundle.h" go into the SDK sysroot, like Sony's do. That directory
# is already on every WebCore and WebKit compile line; an include directory
# of their own would not reach WebKit's object libraries, and adding one
# would rebuild all of WebCore.
cp -r "$HERE/process-launcher/include/." "$HERE/certificate-store/include/." \
      "$HERE/library-bundle/include/." \
   "$PS5_SYSROOT$PS5_HBROOT/include/"


# 2. WebKit sources at the pinned revision (see build-jsc.sh). Only fetched
#    when that commit is not in the tree yet, and only checked out when the
#    tree is at another one: a checkout rewrites the patched files, and a
#    new date on a header such as Assertions.h rebuilds all of WebKit.
if [[ ! -d "$WEBKIT_SRC/.git" ]]; then
    git init -q "$WEBKIT_SRC"
    git -C "$WEBKIT_SRC" remote add origin https://github.com/WebKit/WebKit.git
    git -C "$WEBKIT_SRC" sparse-checkout set --no-cone \
        '/*' '!/LayoutTests/' '!/JSTests/' '!/PerformanceTests/' \
        '!/Websites/' '!/ManualTests/'
fi
if ! git -C "$WEBKIT_SRC" cat-file -e "$WEBKIT_REV^{commit}" 2>/dev/null; then
    git -C "$WEBKIT_SRC" fetch --depth 1 --filter=blob:none origin "$WEBKIT_REV"
fi
if [[ "$(git -C "$WEBKIT_SRC" rev-parse HEAD 2>/dev/null)" != "$WEBKIT_REV" ]]; then
    git -C "$WEBKIT_SRC" checkout -q --force "$WEBKIT_REV"
fi


# 3. Adapt the PlayStation port to the open SDK. Files are only written
#    when their content changes, so a rerun rebuilds nothing it need not.
copy_if_changed() {  # <source> <destination file>
    cmp -s "$1" "$2" || cp "$1" "$2"
}
copy_if_changed "$JSC_SHELL/cmake/PlayStationModule.cmake" \
                "$WEBKIT_SRC/Source/cmake/PlayStationModule.cmake"
copy_if_changed "$JSC_SHELL/cmake/Sign.cmake" "$WEBKIT_SRC/Source/cmake/Sign.cmake"
copy_if_changed "$JSC_SHELL/cmake/shell-PlatformPlayStation.cmake" \
                "$WEBKIT_SRC/Source/JavaScriptCore/shell/PlatformPlayStation.cmake"

# Source patches, applied to the file as it is at WEBKIT_REV. Each one must
# change it, or the port changed and the patch needs a new look.
patch_source() {  # <file under Source/> <sed expression> <what it fixes>
    local file="$WEBKIT_SRC/Source/$1"
    local tmp

    tmp=$(mktemp)
    git -C "$WEBKIT_SRC" show "$WEBKIT_REV:Source/$1" > "$tmp.orig"
    sed "$2" "$tmp.orig" > "$tmp"
    if cmp -s "$tmp" "$tmp.orig"; then
        echo "error: the patch for $3 did not apply to $1" >&2
        rm -f "$tmp" "$tmp.orig"
        exit 1
    fi
    copy_if_changed "$tmp" "$file"
    rm -f "$tmp" "$tmp.orig"
}

# Sony's clang option; the SDK's clang does not know it. And GStreamer's
# options and directories for MEDIA (media/options-*.cmake).
patch_source cmake/OptionsPlayStation.cmake "/-sce-stdlib=v2/d
/^WEBKIT_OPTION_BEGIN()\$/r $HERE/media/options-begin.cmake
\$r $HERE/media/options-end.cmake" "-sce-stdlib=v2"

# CRASH() and RELEASE_ASSERT execute an int3, which inside an app makes the
# system's debugger freeze the whole process. Call the crash report instead
# (crash-report/crash-report.c): it prints file:line and exits.
patch_source WTF/wtf/Assertions.h \
    's|^#define WTF_FATAL_CRASH_INST "int3"$|#define WTF_FATAL_CRASH_INST "call browserps5_wtf_crash" // BrowserPS5: poc/webkit/crash-report|' \
    "int3 in CRASH()"

# Faults in the WebProcess and the NetworkProcess go to the crash report
# too (browserps5_crash_signals); before, the UIProcess only saw them go.
patch_source WebKit/WebProcess/playstation/WebProcessMainPlayStation.cpp \
    's|^int WebProcessMain(int argc, char\*\* argv)$|extern "C" void browserps5_crash_signals(); // BrowserPS5: poc/webkit/crash-report\n\n&|;
     s|^    return AuxiliaryProcessMain<WebProcessMainPlayStation>(argc, argv);$|    browserps5_crash_signals(); // BrowserPS5\n&|' \
    "the WebProcess's crash report"
patch_source WebKit/NetworkProcess/curl/NetworkProcessMainCurl.cpp \
    's|^int NetworkProcessMain(int argc, char\*\* argv)$|extern "C" void browserps5_crash_signals(); // BrowserPS5: poc/webkit/crash-report\n\n&|;
     s|^    return AuxiliaryProcessMain<NetworkProcessMainCurl>(argc, argv);$|    browserps5_crash_signals(); // BrowserPS5\n&|' \
    "the NetworkProcess's crash report"
# Answers of 400 and up, and requests that fail, in the log: the host and
# the path only, the query can carry tokens (V3, 2026-11-04: YouTube's
# player gave up after 50 s with nothing in the log to say why).
# (2026-11-12: the patches of NetworkDataTaskCurl.cpp/.h, sed lines until then, are applied further down, after apply_patch().)

# Graphics threads (the WebProcess's org.webkit.ThreadedCompositor) got the
# system's default stack, and Mesa compiling and linking the compositor's
# shaders on it overflowed it (G2, 2026-11-04: SIGSEGV at rsp, below the
# stack). Mesa's own threads already get 2 MiB (gl_zink/mesa-zink-ps5.patch).
# The JIT's compiler threads (JIT=1, docs/04-jit.md) got libc-fixes' 512 KiB,
# and the DFG compiling YouTube's big functions overflowed it (J2,
# 2026-11-04 19:28: SIGSEGV at rsp in "JIT Worklist Helper Thread"): 4 MiB too.
patch_source WTF/wtf/Threading.cpp \
    "/^#if PLATFORM(PLAYSTATION)\$/{n;n;s|^        return 512 \\* KB;\$|&\\n    // BrowserPS5: Mesa compiles and links the compositor's shaders on its\\n    // thread, which overflowed the system's default stack (G2, 2026-11-04).\\n    if (threadType == ThreadType::Graphics)\\n        return 4 * MB;\\n    // BrowserPS5: the JIT's compiler threads (DFG) overflowed 512 KiB on\\n    // YouTube's big functions (docs/04-jit.md, J2).\\n    if (threadType == ThreadType::Compiler)\\n        return 4 * MB;|}" \
    "the graphics and compiler threads' stack"

# The generic RunLoop's timers sit in a RedBlackTree whose links are
# CheckedPtrs, and RedBlackTree::remove() leaves the removed node's links to
# its old children. A timer task out of the tree kept pointing into it; when
# one of those was freed (correctly, out of the tree itself), the CheckedPtr
# marked it dead, and the first task's next insert(), whose reset() lets go
# of those links, crashed in decrementCheckedPtrCount() (V4, 2026-11-04: the
# GPU browser's UIProcess as YouTube's video started, the WebProcess in
# ThreadedDisplayRefreshMonitor). Only this run loop puts timers in a
# RedBlackTree (the GLib ports use GLib's), and RedBlackTree.h is behind
# RunLoop.h, in nearly every file; so the task leaves through an empty tree
# here, whose insert() clears its links while their targets still live.
# (WTF/wtf/generic/RunLoopGeneric.cpp: runtime/RunLoopGeneric.cpp.patch, applied
# further down, once apply_patch is defined, has this change and 2026-11-11's.)

# The same four as build-jsc.sh (see the comments there).
patch_source bmalloc/libpas/src/libpas/pas_lock.c \
    's|^#elif PAS_PLATFORM(PLAYSTATION)$|#elif 0 // BrowserPS5: Sony 6-argument _umtx_op|' \
    "_umtx_op"
patch_source bmalloc/libpas/src/libpas/pas_thread_local_cache.c \
    's|pthread_get_name_np(thread, thread_name);|(pthread_get_name_np(thread, thread_name, sizeof(thread_name)), 0);|' \
    "pthread_get_name_np"
patch_source WTF/wtf/playstation/UniStdExtrasPlayStation.cpp \
    's|#include <sys/socket.h>|#include <fcntl.h>\n#include <sys/socket.h>|;
     s|return setsockopt(fileDescriptor, SOL_SOCKET, SO_NBIO, &nonBlocking, sizeof(int)) != -1;|(void)nonBlocking;\n    return fcntl(fileDescriptor, F_SETFL, fcntl(fileDescriptor, F_GETFL) \| O_NONBLOCK) != -1;|' \
    "SO_NBIO"
patch_source JavaScriptCore/inspector/remote/socket/posix/RemoteInspectorSocketPOSIX.cpp \
    '/^#if PLATFORM(PLAYSTATION)$/{N;s|^#if PLATFORM(PLAYSTATION)\n\(.*SO_USE_DEVLAN\)|#if 0 // BrowserPS5: SO_USE_DEVLAN is a Sony dev-kit socket option\n\1|}' \
    "SO_USE_DEVLAN"

# Sony's libc declares AF_INET through another header; FreeBSD's is in
# sys/socket.h, which QNX already includes here.
patch_source WebCore/platform/network/DNS.cpp \
    's|^#if OS(QNX)$|#if OS(QNX) \|\| PLATFORM(PLAYSTATION) // BrowserPS5: AF_INET is in sys/socket.h|' \
    "AF_INET"

# A local page (file://) asking for another local file: WebCore first checks
# both are on the same volume, by stat()ing the two paths in the WebProcess
# (FileSystem::filesHaveSameVolume). With the WebProcess in the browser's
# title (2c-2) that cannot work: a title does not see /user, so every
# file:///user/... subresource of a local page was refused (2026-11-06
# 14:46: the video test's clip, "xhr-error"; the same page with its clip
# under /data played). WebKit already leaves the check out where its
# WebProcess is sandboxed (iOS, bubblewrap); the PlayStation port does too.
# Web pages are not concerned: they cannot load file:// URLs either way.
patch_source WebCore/page/SecurityOrigin.cpp \
    's|^#if !PLATFORM(IOS_FAMILY) \&\& !ENABLE(BUBBLEWRAP_SANDBOX)$|#if !PLATFORM(IOS_FAMILY) \&\& !ENABLE(BUBBLEWRAP_SANDBOX) \&\& !PLATFORM(PLAYSTATION) // BrowserPS5: a title does not see /user|' \
    "local files from local pages in the title"

# Sony builds on Windows, where file names ignore case: the file is
# WKRunLoop.cpp, the CMake list says WKRunloop.cpp.
#
# 2026-11-11 (BrowserPS5 RN): the same file gets ScrollingCoordinatorCoordinated.cpp
# when ENABLE_ASYNC_SCROLLING is on (cpu/webkit-async-sources.cmake, put in
# before "if (USE_GRAPHICS_LAYER_WC)"): the build with it
# (~/browserps5-build/webkit-build-async) did not link without. One sed
# script for both: patch_source starts from the file as WebKit has it.
patch_source WebKit/PlatformPlayStation.cmake \
    "s|playstation/WKRunloop\\.|playstation/WKRunLoop.|
/^if (USE_GRAPHICS_LAYER_WC)\$/e cat $HERE/cpu/webkit-async-sources.cmake" \
    "WKRunLoop file name case, the async scrolling coordinator"

# 2026-11-11 (BrowserPS5 RN, the "plus" engine: ENABLE_CONTENT_EXTENSIONS):
# the content rule store did not compile off Cocoa and GLib (a span of
# Latin-1 characters is not one of bytes for NetworkCache::Data).
patch_source WebKit/UIProcess/API/APIContentRuleListStore.cpp \
    's|NetworkCache::Data(sourceJSON.span8())|NetworkCache::Data(asBytes(sourceJSON.span8()))|' \
    "the content rule store's source as bytes"
# ... and did not link: this port had no NetworkCache::Data::tryCreateSharedMemory()
# (cpu/NetworkCacheDataCurl.shared.inc: a copy in ordinary shared memory).
patch_source WebKit/NetworkProcess/cache/NetworkCacheDataCurl.cpp \
    "/^} \/\/ namespace NetworkCache\$/e cat $HERE/cpu/NetworkCacheDataCurl.shared.inc" \
    "shared memory for the content rule store"

# The port launches its children from the app package (/app0/*.self) and
# never uses the paths in WKContextConfiguration (Sony's own code must set
# them; WebProcessPool::platformResolvePathsForSandboxExtensions is empty).
# Ours are where package.sh installs them.
# 2026-11-11: SQLite's log in blocks when BROWSERPS5_SQLITE_BATCH=1
# (storage/sqlite-batch.c): its VFS is registered right after SQLite starts.
patch_source WebCore/platform/sql/SQLiteDatabase.cpp \
    's|^static void initializeSQLiteIfNecessary()$|extern "C" void browserps5_sqlite_batch(); // BrowserPS5: poc/webkit/storage/sqlite-batch.c\n\n&|
s|^        int ret = sqlite3_initialize();$|&\n        browserps5_sqlite_batch(); // BrowserPS5|' \
    "SQLite's log in blocks"

# 2026-11-11 (BrowserPS5 RN): BROWSERPS5_PROCESS_DIR names another folder for
# them, for an engine variant's own processes (runtime/
# ProcessLauncherPlayStation.own.inc; browser/main.cpp sets it with ENGINE_DIR).
patch_source WebKit/UIProcess/Launcher/playstation/ProcessLauncherPlayStation.cpp \
    's|^#define MAKE_PROCESS_PATH(x) "/app0/" #x "Process.self"$|#define MAKE_PROCESS_PATH(x) "/user/homebrew/browserps5/" #x "Process.elf" // BrowserPS5: poc/webkit/package.sh|
/^#define MAKE_PROCESS_PATH(x)/r '"$HERE"'/runtime/ProcessLauncherPlayStation.own.inc
s|^        return MAKE_PROCESS_PATH(Network);$|        return browserps5OwnProcess("Network", MAKE_PROCESS_PATH(Network)); // BrowserPS5 RN|
s|^        return MAKE_PROCESS_PATH(Web);$|        return browserps5OwnProcess("Web", MAKE_PROCESS_PATH(Web)); // BrowserPS5 RN|' \
    "the child process paths"

# The PlayStation "quirk" user agent for any URL is the fixed standard one,
# and WebPage::userAgent() prefers it to the page's, so
# WKPageSetCustomUserAgent never reached the requests. No quirk, like the
# other ports: the page's user agent is used.
patch_source WebCore/platform/playstation/UserAgentPlayStation.cpp \
    '/^String standardUserAgentForURL(const URL&)$/,/^}$/s|return standardUserAgentStatic();|return { }; // BrowserPS5: no per-site user agent|' \
    "the custom user agent"

# WKPageHandleKeyboardEvent gave every key event an empty text, not a null
# one, so a key made no character (no keypress, no Enter in a form). Use the
# event's text; with none, the text comes from the key code as on WPE.
# And WKPageGetWebPageIDForBrowserPS5 (gpu/), for the frames of the GPU
# compositor (below).
# 2026-11-17: and the key's hardware key code was its keysym, so every key
# came to the page with KeyboardEvent.code "Unidentified" (a page that reads
# the code, as games and emulators do, did not see the controller's keys):
# ui/hardwareKeyCode.cpp gives the key code of that key on a US keyboard.
patch_source WebKit/UIProcess/API/C/playstation/WKPagePrivatePlayStation.cpp \
    "s|NativeWebKeyboardEvent::create(&wpeEvent, \"\"_s, |NativeWebKeyboardEvent::create(\\&wpeEvent, /* BrowserPS5 */ event.text ? String::fromUTF8(unsafeMakeSpan(event.text, static_cast<size_t>(event.length))) : String(), |
s|^void WKPageHandleKeyboardEvent(WKPageRef pageRef, WKKeyboardEvent event)\$|static unsigned browserPS5HardwareKeyCode(unsigned keysym); // BrowserPS5: below\n\n&|
s|wpeEvent.hardware_key_code = event.virtualKeyCode;|wpeEvent.hardware_key_code = browserPS5HardwareKeyCode(event.virtualKeyCode); // BrowserPS5: KeyboardEvent.code|
\$r $HERE/gpu/WKPageGetWebPageIDForBrowserPS5.cpp
\$r $HERE/ui/hardwareKeyCode.cpp" \
    "the text of key events"
patch_source WebKit/UIProcess/API/C/playstation/WKPagePrivatePlayStation.h \
    's|^WK_EXPORT void WKPagePaint(.*);$|&\n\n// BrowserPS5: poc/webkit/gpu/bps5_layer.h\nWK_EXPORT uint64_t WKPageGetWebPageIDForBrowserPS5(WKPageRef);|' \
    "WKPageGetWebPageIDForBrowserPS5"

# And WebPage::handleEditingKeyboardEvent was a stub, so even with its text
# a key inserted nothing into a text field. Ours is appended to the file
# (editing/handleEditingKeyboardEvent.cpp).
patch_source WebKit/WebProcess/WebPage/playstation/WebPagePlayStation.cpp \
    "/^bool WebPage::handleEditingKeyboardEvent(WebCore::KeyboardEvent& event)\$/,/^}\$/d
\$r $HERE/editing/handleEditingKeyboardEvent.cpp" \
    "editing keys in text fields"

# Without accelerated compositing (the port's default: no GL), a view
# transition (document.startViewTransition, YouTube's watch page) still
# turned compositing on, and the WebProcess crashed creating a GL surface
# in PlatformDisplay::sharedDisplay() (2026-11-03). The transition still
# runs, without its animation.
patch_source WebCore/rendering/RenderLayerCompositor.cpp \
    '/^void RenderLayerCompositor::enableCompositingMode(bool enable \/\* = true \*\/)$/{
n
a\    if (enable \&\& !m_renderView.settings().acceleratedCompositingEnabled()) // BrowserPS5\
        return;
}' \
    "compositing turned on without accelerated compositing"

# Compositing on the GPU (docs/03-video-e-gpu.md, G2), when the UIProcess
# sets BROWSERPS5_GPU before it makes its preferences (poc/browser). The
# port's own way out of the WebProcess is Sony's WPE backend; without it
# the compositor draws into a framebuffer object and reads each frame back
# into named shared memory (gpu/bps5_layer.h), and the WebProcess makes its
# surfaceless EGL display when the compositor first asks for it (until then
# no process had one, hence the crash above). The compositor thread also
# raises itself a little above the page's threads (priority 690,
# BROWSERPS5_COMPOSITOR_PRIORITY), so busy script does not cost TV frames.
# The patches are unified diffs of one file each, applied like the ones above
# to the file at WEBKIT_REV.
apply_patch() {  # <file under Source/> <patch file>
    local file="$WEBKIT_SRC/Source/$1"
    local tmp

    tmp=$(mktemp)
    git -C "$WEBKIT_SRC" show "$WEBKIT_REV:Source/$1" > "$tmp"
    if ! patch -s --no-backup-if-mismatch -r - "$tmp" "$2"; then
        echo "error: $2 did not apply to $1" >&2
        rm -f "$tmp"
        exit 1
    fi
    copy_if_changed "$tmp" "$file"
    rm -f "$tmp"
}
apply_patch WebKit/WebProcess/WebPage/CoordinatedGraphics/AcceleratedSurfacePlayStation.h \
    "$HERE/gpu/AcceleratedSurfacePlayStation.h.patch"
apply_patch WebKit/WebProcess/WebPage/CoordinatedGraphics/AcceleratedSurfacePlayStation.cpp \
    "$HERE/gpu/AcceleratedSurfacePlayStation.cpp.patch"
copy_if_changed "$HERE/gpu/bps5_layer.h" \
    "$WEBKIT_SRC/Source/WebKit/WebProcess/WebPage/CoordinatedGraphics/bps5_layer.h"
apply_patch WebCore/platform/graphics/PlatformDisplay.cpp "$HERE/gpu/PlatformDisplay.cpp.patch"
apply_patch WebCore/platform/graphics/egl/PlatformDisplaySurfaceless.cpp \
    "$HERE/gpu/PlatformDisplaySurfaceless.cpp.patch"
# A page that committed a frame waited for the compositor's "done" for good
# when nothing else asked for a display refresh: the loading bar stopped
# drawing at 30-50% (docs/04-jit.md, "A barra de loading parada"). And a
# page closed while it had an animation running crashed the WebProcess: the
# compositor's timers now stop on their own thread first
# (docs/06-teclado-e-favoritos.md).
apply_patch WebKit/WebProcess/WebPage/CoordinatedGraphics/ThreadedCompositorPlayStation.cpp \
    "$HERE/gpu/ThreadedCompositorPlayStation.cpp.patch"
# 2026-11-09: CompositingRunLoop::suspend() no longer stops the compositing
# thread's update timer from the main thread (a RELEASE_ASSERT when active);
# with the ThreadedCompositor's suspend()/renderLayerTree() changes above, a
# page hidden mid-update (the PS button) draws again when shown.
apply_patch WebKit/WebProcess/WebPage/CoordinatedGraphics/CompositingRunLoop.cpp \
    "$HERE/gpu/CompositingRunLoop.cpp.patch"
# 2026-11-09: a page hidden mid-frame (the PS button) lost the refresh its
# WebCore clients waited for and ran no rendering update again once shown
# (YouTube kept its full-screen player); ThreadedCompositor::resume() now
# sends one (ThreadedDisplayRefreshMonitor::refreshAfterResume()).
apply_patch WebKit/WebProcess/WebPage/CoordinatedGraphics/ThreadedDisplayRefreshMonitorPlayStation.h \
    "$HERE/gpu/ThreadedDisplayRefreshMonitorPlayStation.h.patch"
apply_patch WebKit/WebProcess/WebPage/CoordinatedGraphics/ThreadedDisplayRefreshMonitorPlayStation.cpp \
    "$HERE/gpu/ThreadedDisplayRefreshMonitorPlayStation.cpp.patch"

# The CPU (docs/03-video-e-gpu.md, "Falta de CPU"). While a title is in
# front, the payloads together get about 2 cores, and software filters split
# their work over 16 "Parallel worker" threads at the priority of the video
# and the compositor: YouTube's video stood still for 20 s while they ran.
# WTF gives them at most 2 jobs and a lower priority
# (cpu/ParallelJobsGeneric.cpp.patch, BROWSERPS5_PARALLEL_JOBS and
# BROWSERPS5_PARALLEL_PRIORITY in env.txt). With BROWSERPS5_FILTER_LOG=1 the
# CSS filters and blurs drawn in software are counted once a second, with the
# element (cpu/RenderLayerFilters.cpp.patch,
# cpu/FEGaussianBlurSoftwareApplier.cpp.patch). That log showed YouTube's
# blurred picture behind the player blurred ~10 times a second on the main
# thread: an element whose filter moves pixels now gets its own layer, and the
# compositor blurs it on the GPU (cpu/RenderLayerCompositor.cpp.patch,
# BROWSERPS5_COMPOSITE_FILTERS=0 to turn it off).
apply_patch WTF/wtf/ParallelJobsGeneric.cpp "$HERE/cpu/ParallelJobsGeneric.cpp.patch"
apply_patch WebCore/rendering/RenderLayerCompositor.cpp "$HERE/cpu/RenderLayerCompositor.cpp.patch"
apply_patch WebCore/rendering/RenderLayerFilters.cpp "$HERE/cpu/RenderLayerFilters.cpp.patch"
# 2026-11-11 (BrowserPS5 RN, docs/28): a scroll step no longer lays out the
# sticky renderers again (Wikipedia's article: the page's grid was laid out
# and the whole article painted again at every step, 29 -> 57 frames a second)
# and only places the fixed ones again (publico.pt: 25 -> 42);
# RenderLayerCompositor.cpp.patch also gives sticky renderers a layer of
# their own in the main frame. BROWSERPS5_PAINT_STATS=1 (web-env.txt, env.txt)
# writes what is painted each second, who asks for big repaints and for
# layouts (the three files after LocalFrameView.cpp).
apply_patch WebCore/page/LocalFrameView.cpp "$HERE/cpu/LocalFrameView.cpp.patch"
apply_patch WebCore/platform/graphics/coordinated/CoordinatedBackingStoreProxy.cpp     "$HERE/cpu/CoordinatedBackingStoreProxy.cpp.patch"
apply_patch WebCore/platform/graphics/coordinated/GraphicsLayerCoordinated.cpp     "$HERE/cpu/GraphicsLayerCoordinated.cpp.patch"
apply_patch WebCore/rendering/RenderObject.cpp "$HERE/cpu/RenderObject.cpp.patch"
# 2026-11-14: the video's hole painted into an intermediate surface (a backdrop-filter over the video: a
# player's subtitles and its seek bar's label; a filter, a mask or an opacity around it): the sink was told
# a place moved by the surface's offset, and the hole stayed in the surface (black over the video).
apply_patch WebCore/platform/graphics/texmap/TextureMapper.h "$HERE/media/TextureMapper.h.patch"
apply_patch WebCore/platform/graphics/texmap/TextureMapperLayer.cpp "$HERE/media/TextureMapperLayer.cpp.patch"
apply_patch WebCore/platform/graphics/coordinated/CoordinatedPlatformLayerBufferHolePunch.cpp     "$HERE/media/CoordinatedPlatformLayerBufferHolePunch.cpp.patch"
apply_patch WebCore/platform/graphics/filters/software/FEGaussianBlurSoftwareApplier.cpp \
    "$HERE/cpu/FEGaussianBlurSoftwareApplier.cpp.patch"

# The JIT (docs/04-jit.md, JIT=1). The PS5 runs code only from pages made
# executable with mprotect(): the port's allocator now does that for the JIT
# pool (jit/OSAllocatorPlayStation.cpp.patch), the pool stays
# read+write+execute (ENABLE_MPROTECT_RX_TO_RWX off, which crashed the JIT
# threads in J2: jit/PlatformEnablePlayStation.h.patch). If the pool cannot
# be made executable, JSC runs without the JIT. Its pool is 128 MiB and its
# VM traps poll (jit/Options.cpp.patch).
apply_patch WTF/wtf/playstation/OSAllocatorPlayStation.cpp "$HERE/jit/OSAllocatorPlayStation.cpp.patch"
apply_patch WTF/wtf/PlatformEnablePlayStation.h "$HERE/jit/PlatformEnablePlayStation.h.patch"
apply_patch JavaScriptCore/runtime/Options.cpp "$HERE/jit/Options.cpp.patch"
# A view size change reaches the page (ui/PlayStationWebView.cpp.patch): the
# bar hidden for a full screen gave the page the whole 1080 rows only then.
apply_patch WebKit/UIProcess/playstation/PlayStationWebView.cpp "$HERE/ui/PlayStationWebView.cpp.patch"
# Service workers run in the page's own WebProcess when it has no other
# site's workers, else in a process of their site's (ui/WebProcessPool.cpp.patch,
# 2026-11-10): a WebProcess runs one site's workers at a time.
apply_patch WebKit/UIProcess/WebProcessPool.cpp "$HERE/ui/WebProcessPool.cpp.patch"
# A Cross-Origin-Opener-Policy header keeps the page in the one WebProcess,
# with its service worker (ui/WebPageProxy.cpp.patch, 2026-11-10): WhatsApp
# and Telegram Web moved to a new process and the page stopped there.
apply_patch WebKit/UIProcess/WebPageProxy.cpp "$HERE/ui/WebPageProxy.cpp.patch"
# A worker's loop turns the thread's RunLoop, as on Windows
# (runtime/WorkerRunLoop.cpp.patch, 2026-11-10): the JS run loop timers
# never fired in workers, and WebAssembly.instantiate() there never ended.
apply_patch WebCore/workers/WorkerRunLoop.cpp "$HERE/runtime/WorkerRunLoop.cpp.patch"
# The JIT's compiler threads run below the video and the compositor
# (BROWSERPS5_JIT_PRIORITY, 750: jit/JITWorklistThread.cpp.patch).
apply_patch JavaScriptCore/jit/JITWorklistThread.cpp "$HERE/jit/JITWorklistThread.cpp.patch"
# Temporary files go to the browser's data folder (storage/FileSystemPOSIX.cpp.patch,
# 2026-11-10): the PS5 has no /tmp and WebKit's processes have no TMPDIR, so IndexedDB
# took no Blob and WhatsApp Web stopped at "database error".
apply_patch WTF/wtf/posix/FileSystemPOSIX.cpp "$HERE/storage/FileSystemPOSIX.cpp.patch"

# 2026-11-11: the last sync messages of each process, printed when one has no
# reply after 2 s and at BROWSERPS5_STACK_DUMPS's seconds (a page waited for
# minutes for BlobSize's reply with the NetworkProcess idle).
apply_patch WebKit/Platform/IPC/Connection.cpp "$HERE/ipc-transport/Connection.cpp.patch"

# 2026-11-11: web content does not connect to this console itself (the
# jailbreak's FTP, ELF loader and websrv take commands from whoever connects:
# a page's fetch() to 127.0.0.1:2121 was read as FTP commands). On with
# BROWSERPS5_NO_LOCAL=1 (RN's engines). The idea is Parallax Browser's patch 0107.
apply_patch WebCore/platform/network/curl/CurlContext.h "$HERE/security/CurlContext.h.patch"
apply_patch WebCore/platform/network/curl/CurlContext.cpp "$HERE/security/CurlContext.cpp.patch"
apply_patch WebCore/platform/network/curl/CurlRequest.cpp "$HERE/security/CurlRequest.cpp.patch"
apply_patch WebCore/platform/network/curl/CurlStream.cpp "$HERE/security/CurlStream.cpp.patch"

# 2026-11-11: the wall clock with an offset (the console's clock was 23 min
# ahead: the title asks the network for the time, title/web_host.cpp).
apply_patch WTF/wtf/CurrentTime.cpp "$HERE/runtime/CurrentTime.cpp.patch"
apply_patch WTF/wtf/DateMath.h "$HERE/runtime/DateMath.h.patch"
# (2026-11-12: these were sed lines here; with a download's totals and its writes a MiB at a time, the two files
# are patches: network/NetworkDataTaskCurl.cpp.patch has the log lines, the redirect's check and the downloads'.)
apply_patch WebKit/NetworkProcess/curl/NetworkDataTaskCurl.cpp "$HERE/network/NetworkDataTaskCurl.cpp.patch"
apply_patch WebKit/NetworkProcess/curl/NetworkDataTaskCurl.h "$HERE/network/NetworkDataTaskCurl.h.patch"

# 2026-11-11 (the "full" engine, ENABLE_WEBGL): ANGLE with its OpenGL ES
# backend on the system's EGL (webgl/angle-playstation.cmake), and the two
# places of its Linux code the console's libc has not.
patch_source ThirdParty/ANGLE/PlatformPlayStation.cmake "\$r $HERE/webgl/angle-playstation.cmake" \
    "ANGLE's backend for the console"
patch_source ThirdParty/ANGLE/src/common/system_utils_linux.cpp \
    's|CLOCK_MONOTONIC_RAW|CLOCK_MONOTONIC|g;
     s|^    pthread_setname_np(pthread_self(), name);$|#if !defined(__PROSPERO__) // BrowserPS5: the console'"'"'s takes other arguments\n&\n#endif|' \
    "ANGLE's clock and thread name"
patch_source ThirdParty/ANGLE/src/common/SimpleMutex.h \
    '0,/^#    if defined(ANGLE_PLATFORM_LINUX) || defined(ANGLE_PLATFORM_ANDROID)$/s,,#    if (defined(ANGLE_PLATFORM_LINUX) || defined(ANGLE_PLATFORM_ANDROID)) \&\& !defined(__PROSPERO__) // BrowserPS5: the console has no linux/futex.h,' \
    "ANGLE's mutex without futex.h"
apply_patch ThirdParty/ANGLE/src/libANGLE/renderer/gl/egl/FunctionsEGLDL.cpp "$HERE/webgl/FunctionsEGLDL.cpp.patch"
patch_source ThirdParty/ANGLE/src/libANGLE/Context.cpp \
    's|^thread_local Context \*gCurrentValidContext = nullptr;$|&\nextern "C" void _ZTHN2gl20gCurrentValidContextE() { } // BrowserPS5 (poc/webkit/webgl): the initializer function every other file asks for (a weak symbol; the title, with the engine linked in, may have none left to find)|' \
    "ANGLE's thread_local initializer (_ZTHN2gl20gCurrentValidContextE)"
patch_source ThirdParty/ANGLE/src/libGLESv2/global_state.cpp \
    's|^thread_local Thread \*gCurrentThread = nullptr;$|&\nextern "C" void _ZTHN3egl14gCurrentThreadE() { } // BrowserPS5 (poc/webkit/webgl): the initializer function every other file asks for (a weak symbol; the title, with the engine linked in, may have none left to find)|' \
    "ANGLE's thread_local initializer (_ZTHN3egl14gCurrentThreadE)"
mkdir -p "$WEBKIT_SRC/Source/WebCore/platform/graphics/playstation" "$WEBKIT_SRC/Source/WebCore/platform/audio/playstation"
copy_if_changed "$HERE/webgl/GraphicsContextGLCoordinatedPlayStation.cpp" \
                "$WEBKIT_SRC/Source/WebCore/platform/graphics/playstation/GraphicsContextGLCoordinatedPlayStation.cpp"
copy_if_changed "$HERE/audio/AudioBusPlayStation.cpp" \
                "$WEBKIT_SRC/Source/WebCore/platform/audio/playstation/AudioBusPlayStation.cpp"

# 2026-11-11: navigator.platform was "" (the port has none): a page that
# checks the browser against its user agent (a Mac's Safari) saw a browser
# that does not say what it runs on. "MacIntel", as that Safari (and an
# iPad's) says.
patch_source WebCore/page/NavigatorBase.cpp \
    's|^    return ""_s;$|    return "MacIntel"_s; // BrowserPS5: as the user agent it sends (a Mac'"'"'s Safari; an iPad'"'"'s says the same)|' \
    "navigator.platform"

# The links a timer keeps after leaving the run loop's tree (2026-11-04, see
# above), and 2026-11-11: the loop's sleep is 50 ms at most. A function given
# to a run loop could wait for a wake-up that did not come: the
# NetworkProcess's connection queue kept the reply to a page's sync message
# for 3 to more than 75 s, and every page stood still with it.
apply_patch WTF/wtf/generic/RunLoopGeneric.cpp "$HERE/runtime/RunLoopGeneric.cpp.patch"

# 2026-11-11: tried and taken out again, both kept beside the patches
# (runtime/ParkingLot.cpp.guard-100ms.tried: every wait of WTF's locks and
# conditions looking again every 100 ms; runtime/RunLoopGeneric.cpp.cap-50ms.tried:
# the run loops' sleep 50 ms at most). The reply that waited for good was not
# a lost wake-up: a message that could not be sent left the queue behind it
# (Connection.cpp.patch, ipc_transport.c).

# 2026-11-11: indexedDB.databases() opened every database of the site each
# time (35 ms each on the PS5, twelve for WhatsApp Web, asked over and over):
# the storage queue answered nothing else for up to 75 s and the pages stood
# still. What each file said is kept while the file stays as it was.
apply_patch WebCore/Modules/indexeddb/server/SQLiteIDBBackingStore.cpp \
    "$HERE/storage/SQLiteIDBBackingStore.cpp.patch"
# (At the end of this file go the GStreamer sources, for MEDIA below.)
patch_source WebCore/PlatformPlayStation.cmake \
    "s|^    platform/graphics/egl/GLDisplay.cpp\$|&\n    platform/graphics/egl/PlatformDisplaySurfaceless.cpp|
\$r $HERE/media/webcore-sources.cmake" \
    "the surfaceless EGL display"
patch_source WebKit/UIProcess/playstation/WebPreferencesPlayStation.cpp \
    's|^#include "WebPreferences.h"$|&\n#include <stdlib.h>|;
     s|^    setAcceleratedCompositingEnabled(false);$|    // BrowserPS5: on the GPU when the UIProcess asks (docs/03-video-e-gpu.md, G2).\n    bool gpu = getenv("BROWSERPS5_GPU");\n    setAcceleratedCompositingEnabled(gpu);|;
     s|^    setForceCompositingMode(false);$|    setForceCompositingMode(gpu);|' \
    "compositing on the GPU"

# Video through GStreamer (docs/03-video-e-gpu.md, V2), built with MEDIA=1.
# WebCore's GStreamer code expects GLib and Skia, and this port has neither.
# GLib's types and GObject come without its event loop: WTF's GLib smart
# pointers are let in with USE(GSTREAMER), with the type names Platform.h
# includes for USE(GLIB) (not Platform.h itself: that would rebuild all of
# the other build), and three of its GLib files are built
# (media/wtf-sources.cmake); bus messages go through RunLoop, with the time
# they were posted, and the PS5's sound is registered
# (GStreamerCommon.cpp.patch); a GLib-only timer priority goes
# (GStreamerVideoFrameConverter.cpp.patch). Frames become Cairo surfaces
# (ImageGStreamerCairo.cpp, VideoFrameGStreamer.cpp.patch). The player
# (MediaPlayerPrivateGStreamer.*.patch) puts the download buffer of a plain
# <video> in the browser's data folder (its /var/tmp is outside what a payload
# can write) and does not take a preroll's ASYNC_DONE for the end of the seek
# that preroll committed (V2: seeking back to 0 on 'ended' went round and
# round), and keeps WebKitMediaSrc's still-frame events away from avdec_*
# too (V3: their drain flushed the reference frames, and YouTube's next
# buffer, mid-GOP, failed in VP9 and drew on grey in H.264).
for glib in GRefPtr.h GRefPtr.cpp GUniquePtr.h GThreadSafeWeakPtr.h GWeakPtr.h \
            RunLoopSourcePriority.h; do
    patch_source "WTF/wtf/glib/$glib" \
        's|^#if USE(GLIB)$|#if USE(GLIB) \|\| USE(GSTREAMER) // BrowserPS5: GLib without its event loop\n#include <wtf/glib/GTypedefs.h>|' \
        "GLib types for GStreamer"
done
patch_source WTF/wtf/PlatformPlayStation.cmake "\$r $HERE/media/wtf-sources.cmake" \
    "the GLib helpers for GStreamer"
for gst in GStreamerCommon.cpp GStreamerVideoFrameConverter.cpp VideoFrameGStreamer.cpp \
           MediaPlayerPrivateGStreamer.cpp MediaPlayerPrivateGStreamer.h; do
    apply_patch "WebCore/platform/graphics/gstreamer/$gst" "$HERE/media/$gst.patch"
done
# The quirks (the title decodes the video: Westeros stop caps and hole punch)
# were for WPE only: on the console WEBKIT_GST_QUIRKS=westeros was set and the
# quirk manager returned before reading it, so avdec_vp9 still decoded in the
# WebProcess (2026-11-05 11:33). Here they load when crash-report.c sets that
# variable, never by automatic detection (media/GStreamerQuirks.cpp.patch).
apply_patch WebCore/platform/gstreamer/GStreamerQuirks.cpp "$HERE/media/GStreamerQuirks.cpp.patch"
# MSE (YouTube): WebKit's main thread keeps only 0.1-0.2 s of samples ahead
# of the pipeline; on ~2 cores a busy main thread (scrolling under the
# video) let it run dry and the video stopped (TV test 2026-11-05). 2 s now
# (media/TrackQueue.h.patch). MSE_QUEUE=0 builds it as upstream has it, for
# the comparison run of media/video-test/run-mse.sh.
if [[ ${MSE_QUEUE:-1} == 1 ]]; then
    apply_patch WebCore/platform/graphics/gstreamer/mse/TrackQueue.h "$HERE/media/TrackQueue.h.patch"
else
    upstream=$(mktemp)
    git -C "$WEBKIT_SRC" show "$WEBKIT_REV:Source/WebCore/platform/graphics/gstreamer/mse/TrackQueue.h" > "$upstream"
    copy_if_changed "$upstream" "$WEBKIT_SRC/Source/WebCore/platform/graphics/gstreamer/mse/TrackQueue.h"
    rm -f "$upstream"
fi
copy_if_changed "$HERE/media/ImageGStreamerCairo.cpp" \
    "$WEBKIT_SRC/Source/WebCore/platform/graphics/gstreamer/ImageGStreamerCairo.cpp"
# The video layer's header includes Skia for the ports without
# TextureMapper; this one has it. And its frame, mapped for the upload to
# the GPU, goes before m_buffer: m_buffer's initializer,
# createBufferIfNeeded(), maps it, and a member declared after that was then
# constructed empty on top of it. Without GStreamer's GL every frame was
# lost that way, and the <video> stayed black on the GPU (V4).
patch_source WebCore/platform/graphics/coordinated/CoordinatedPlatformLayerBufferVideo.h \
    's|^#include <skia/gpu/ganesh/GrContextThreadSafeProxy.h>$|#if USE(SKIA) // BrowserPS5: Cairo here\n&\n#endif|
     /^    const std::unique_ptr<CoordinatedPlatformLayerBuffer> m_buffer;$/{N;s|^\(.*\)\n\(    std::optional<GstMappedFrame> m_mappedVideoFrame;\)$|    // BrowserPS5: before m_buffer, whose initializer fills it in\n\2\n\1|}' \
    "Skia in the video layer, and the order of its members"
# MSE: a WEBCORE_EXPORT member of a WEBCORE_EXPORT class, which this port's
# dllexport refuses.
patch_source WebCore/platform/graphics/MediaSourcePrivate.h \
    's|^    WEBCORE_EXPORT static PlatformTimeRanges computeBufferedRanges(|    /* BrowserPS5: the class is exported */ static PlatformTimeRanges computeBufferedRanges(|' \
    "the export inside an exported class"
# SharedBuffer::createGBytes() is USE(GLIB)'s: the same bytes, copied.
patch_source WebCore/platform/graphics/gstreamer/ImageDecoderGStreamer.cpp \
    's|^    auto bytes = data->createGBytes();$|    GRefPtr bytes = adoptGRef(g_bytes_new(data->span().data(), data->size())); // BrowserPS5: no createGBytes()|' \
    "createGBytes"
# GStreamer's debug log takes backtrace() and backtrace_symbols() from the
# SDK's libc, and libpas has its own backtrace() for PlayStation: weak, the
# libc one wins when it is linked in.
patch_source bmalloc/libpas/src/libpas/pas_probabilistic_guard_malloc_allocator.c \
    's|^size_t backtrace(void\*\* buffer, size_t size)$|__attribute__((weak)) size_t backtrace(void** buffer, size_t size) // BrowserPS5: GStreamer links libc'"'"'s|' \
    "libpas backtrace()"

# VIDEO_DEBUG=1: logs along the way of a video frame to the GPU compositor
# (docs/03-video-e-gpu.md, V4: on the GPU the <video> stayed black). The
# first three of each, then one in 300. Without it, the files go back to
# WebKit's own.
video_debug() {  # <file under Source/> <sed expression>
    local tmp

    if [[ -n "${VIDEO_DEBUG:-}" ]]; then
        patch_source "$1" "$2" "VIDEO_DEBUG"
        return
    fi
    tmp=$(mktemp)
    git -C "$WEBKIT_SRC" show "$WEBKIT_REV:Source/$1" > "$tmp"
    copy_if_changed "$tmp" "$WEBKIT_SRC/Source/$1"
    rm -f "$tmp"
}
VDBG='static unsigned bps5Count; if (bps5Count++ % 300 < 3) fprintf(stderr, "BrowserPS5 video: '
video_debug WebCore/platform/graphics/coordinated/GraphicsLayerCoordinated.cpp \
    's|^#include "GraphicsLayerCoordinated.h"$|&\n#include <stdio.h> // BrowserPS5: VIDEO_DEBUG|;
     s|^    if (player \&\& !player->supportsAcceleratedRendering())$|    { '"$VDBG"'setContentsToMediaPlayer(%p), accelerated %d, proxy %p\\n", player, player ? player->supportsAcceleratedRendering() : -1, m_contentsBufferProxy.get()); }\n&|;
     s|^        player->setPlatformLayerBufferProxy(Ref { \*m_contentsBufferProxy });$|&\n        { '"$VDBG"'proxy %p given to the player\\n", m_contentsBufferProxy.get()); }|'
video_debug WebCore/platform/graphics/coordinated/CoordinatedPlatformLayerBufferProxy.cpp \
    's|^#include "CoordinatedPlatformLayerBufferProxy.h"$|&\n#include <stdio.h> // BrowserPS5: VIDEO_DEBUG|;
     s|^    m_pendingBuffer = WTF::move(buffer);$|    { '"$VDBG"'initial buffer %p\\n", buffer.get()); }\n&|;
     s|^    layer->requestComposition(CompositionReason::VideoFrame);$|&\n    { '"$VDBG"'buffer %u to the layer\\n", bps5Count); }|'
video_debug WebCore/platform/graphics/texmap/coordinated/CoordinatedPlatformLayerBufferVideoTextureMapper.cpp \
    's|^#include "TextureMapper.h"$|&\n#include <stdio.h> // BrowserPS5: VIDEO_DEBUG|;
     s|^        // If mapping failed, clear the GstMappedFrame holder.$|        { '"$VDBG"'frame %u not mapped\\n", bps5Count); }\n&|;
     s|^    texture->updateContents(srcData.data(), IntRect(origin, m_size), origin, stride, PixelFormat::BGRA8);$|&\n    { '"$VDBG"'frame %u uploaded: %dx%d, stride %d, %s, texture %u\\n", bps5Count, m_size.width(), m_size.height(), stride, gst_video_format_to_string(GST_VIDEO_INFO_FORMAT(m_mappedVideoFrame->info())), texture->id()); }|;
     s|^    createBufferFromMappedFrameIfNeeded();$|&\n    { '"$VDBG"'paint %u at %.0f,%.0f %.0fx%.0f, opacity %.2f, %s\\n", bps5Count, targetRect.x(), targetRect.y(), targetRect.width(), targetRect.height(), opacity, m_buffer ? "drawn" : "no buffer"); }|'

# The entry points dlopen() every dependency by its Sony module name
# (*_LOAD_AT, set by Sony's PlayStationModule.cmake). Here they are linked
# statically into libWebKit, so the calls go. And GLib has no user database
# here (HOME), and GStreamer's warnings go to stderr, which is the log.
for main in WebProcess/EntryPoint/playstation/WebProcessMain.cpp \
            NetworkProcess/EntryPoint/playstation/NetworkProcessMain.cpp; do
    patch_source "WebKit/$main" \
        '/^ *loadLibraryOrExit(.*);$/d;
         s|^static void loadLibraryOrExit|[[maybe_unused]] static void loadLibraryOrExit|;
         s|^    char\* coreProcessIdentifier = argv\[1\];$|    setenv("HOME", "/user/homebrew/browserps5/data", 0); // BrowserPS5\n    setenv("GST_DEBUG", "2", 0); // BrowserPS5\n&|' \
        "dlopen of the dependencies"
done

# OptionsPlayStation.cmake copies these two folders next to every
# executable; with nothing to copy they just need to exist.
mkdir -p "$WORK/webkit-libraries/tools/sce_sys" \
         "$WORK/webkit-libraries/tools/scripts"


# 4. Configure. On top of build-jsc.sh's settings:
#    - CMAKE_PROJECT_INCLUDE: undo the toolchain's preference for CMake
#      configs over find modules (see cmake/PreferFindModules.cmake).
#    - WebKitRequirements_DIR: our stand-in for Sony's package.
#    - EGL/OpenGLES2: Mesa, both from the one libEGL.so.1. The system GLSlim
#      does not load inside an app (0x80020063), and the app is where the
#      UIProcess runs.
#    - CMAKE_{C,CXX}_STANDARD_LIBRARIES: the static dependency group, at the
#      end of every link line.
#    - CMAKE_SHARED_LINKER_FLAGS: the SDK's prospero-lld adds -pie unless it
#      sees --shared among its arguments. libWebKit's link line is so long
#      that clang hands lld a response file instead, so -pie is added and
#      lld refuses "-shared and -pie". --no-pie comes later and wins.
#      libwebkit.ver keeps the exported symbols to the C API: the SDK's rtld
#      looks symbols up linearly, and with everything exported loading
#      libWebKit never finished. --wrap=readlink and --wrap=pthread_create
#      (threads' stacks, V3): see libc-fixes; lld renames the calls but
#      does not pull the __wrap_ functions out of the archive by itself,
#      hence -u.
POSIX_HDR="$JSC_SHELL/shims/include/ps5-posix-webkit.h"

cmake -S "$WEBKIT_SRC" -B "$BUILD" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$PS5_PAYLOAD_SDK/toolchain/prospero.cmake" \
    -DCMAKE_PROJECT_INCLUDE="$HERE/cmake/PreferFindModules.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DPORT=PlayStation \
    -DENABLE_WEBCORE=ON \
    -DENABLE_WEBKIT=ON \
    -DENABLE_STATIC_JSC=ON \
    "${JIT_OPTIONS[@]}" \
    -DENABLE_WEBGL=OFF \
    -DENABLE_MINIBROWSER=OFF \
    -DUSE_SKIA=OFF \
    -DUSE_LCMS=OFF \
    -DUSE_JPEGXL=OFF \
    -DUSE_AVIF=OFF \
    -DUSE_WOFF2=OFF \
    -DDEVELOPER_MODE=OFF \
    -DLD_PACK_RELATIVE_RELOCS=OFF \
    -DWEBKIT_LIBRARIES_DIR="$WORK/webkit-libraries" \
    -DWebKitRequirements_DIR="$HERE/cmake/WebKitRequirements" \
    -DBROWSERPS5_PROCESS_LAUNCHER_LIB="$WORK/process-launcher/libProcessLauncher.a" \
    -DCMAKE_PREFIX_PATH="$PS5_HBROOT" \
    -DMEMORY_EXTRA_LIB="$WORK/shims/libMemoryExtra.a" \
    -DMEMORY_EXTRA_INCLUDE_DIR="$JSC_SHELL/shims/include" \
    -DC_STD_LIBRARY="$PS5_SYSROOT/lib/libScePosixForWebKit.so" \
    -DKERNEL_LIBRARY="$PS5_SYSROOT/lib/libkernel_web.so" \
    -DEGL_LIBRARIES="$MESA_GL" \
    -DEGL_INCLUDE_DIRS="$MESA_SOFT/include" \
    -DOpenGLES2_LIBRARY="$MESA_GL" \
    -DOpenGLES2_INCLUDE_DIR="$MESA_SOFT/include" \
    -DCMAKE_C_FLAGS="-include $POSIX_HDR" \
    -DCMAKE_CXX_FLAGS="-include $POSIX_HDR" \
    -DCMAKE_SHARED_LINKER_FLAGS="-Wl,--no-pie -Wl,--version-script=$HERE/libwebkit.ver -Wl,--wrap=readlink -Wl,-u,__wrap_readlink -Wl,--wrap=pthread_create -Wl,-u,__wrap_pthread_create$IPC_WRAP$SHARED_MEDIA" \
    -DCMAKE_C_STANDARD_LIBRARIES="$DEP_GROUP" \
    -DCMAKE_CXX_STANDARD_LIBRARIES="$DEP_GROUP" \
    "${CMAKE_CURL[@]}" \
    "${CMAKE_MEDIA[@]}"

if [[ -n "${CONFIGURE_ONLY:-}" ]]; then
    exit 0
fi


# 5. Build libWebKit and the two child processes. CC is unset for the
#    reason given in build-jsc.sh (code generators run "$CC -E").
env -u CC ninja -C "$BUILD" -j"$JOBS" ${NINJA_ARGS:-} WebKit WebProcess NetworkProcess

echo
echo "built in $BUILD/lib and $BUILD/bin:"
ls -la "$BUILD"/lib/libWebKit* "$BUILD"/bin/*Process* 2>/dev/null || true
