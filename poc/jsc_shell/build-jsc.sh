#!/usr/bin/env bash
#   Copyright (C) 2026 BrowserPS5 contributors
#
# This file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# Build WebKit's JavaScriptCore shell (jsc) as a PS5 payload, using the
# upstream PlayStation port with the open ps5-payload-sdk in place of Sony's
# SDK. This is the first real piece of WebKit on the console: it exercises
# the toolchain, ICU, WTF, bmalloc/libpas and the JS interpreter (no JIT).
#
# Run inside WSL/Linux, with PS5_PAYLOAD_SDK set, after build-icu.sh.
# Needs: cmake >= 3.20, ninja, perl, python3, ruby, git.
#
# What it changes in the WebKit tree (all listed in README.md):
#   - adds Source/cmake/PlayStationModule.cmake and Sign.cmake (open stand-ins)
#   - replaces Source/JavaScriptCore/shell/PlatformPlayStation.cmake
#   - drops the Sony-only compiler flag -sce-stdlib=v2

set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
WORK=${WORK:-$HOME/browserps5-build}
WEBKIT_SRC=${WEBKIT_SRC:-$WORK/WebKit}
# The revision docs/01-inventario-port-webkit.md was written against.
WEBKIT_REV=${WEBKIT_REV:-95075894acb3349f591612e69cb586e157acb09f}
BUILD=${BUILD:-$WORK/jsc-build}
JOBS=${JOBS:-$(nproc)}

if [[ -z "${PS5_PAYLOAD_SDK:-}" ]]; then
    echo "error: PS5_PAYLOAD_SDK is not set" >&2
    exit 1
fi
for tool in cmake ninja perl python3 ruby git; do
    command -v "$tool" >/dev/null || { echo "error: $tool not found" >&2; exit 1; }
done

source "$PS5_PAYLOAD_SDK/toolchain/prospero.sh"

if ! ls "$PS5_SYSROOT$PS5_HBROOT"/lib/libicuuc.a >/dev/null 2>&1; then
    echo "error: ICU not found in the SDK sysroot, run build-icu.sh first" >&2
    exit 1
fi

mkdir -p "$WORK"


# 1. Open replacement for Sony's memory-extra library.
mkdir -p "$WORK/shims"
"$CXX" -O2 -fPIC -std=c++17 -I"$HERE/shims/include" \
    -c "$HERE/shims/src/vss.cpp" -o "$WORK/shims/vss.o"
rm -f "$WORK/shims/libMemoryExtra.a"
"$AR" rcs "$WORK/shims/libMemoryExtra.a" "$WORK/shims/vss.o"


# 2. WebKit sources: Source/ and Tools/ at the pinned revision. The test
#    suites (LayoutTests alone is several GB) are left out. Only that one
#    commit is fetched, and its files on checkout: a full clone would bring
#    the whole history.
if [[ ! -d "$WEBKIT_SRC/.git" ]]; then
    git init -q "$WEBKIT_SRC"
    git -C "$WEBKIT_SRC" remote add origin https://github.com/WebKit/WebKit.git
    git -C "$WEBKIT_SRC" sparse-checkout set --no-cone \
        '/*' '!/LayoutTests/' '!/JSTests/' '!/PerformanceTests/' \
        '!/Websites/' '!/ManualTests/'
fi
git -C "$WEBKIT_SRC" fetch --depth 1 --filter=blob:none origin "$WEBKIT_REV"
git -C "$WEBKIT_SRC" checkout --force FETCH_HEAD


# 3. Adapt the PlayStation port to the open SDK.
cp "$HERE/cmake/PlayStationModule.cmake" "$HERE/cmake/Sign.cmake" \
   "$WEBKIT_SRC/Source/cmake/"
cp "$HERE/cmake/shell-PlatformPlayStation.cmake" \
   "$WEBKIT_SRC/Source/JavaScriptCore/shell/PlatformPlayStation.cmake"

OPTIONS="$WEBKIT_SRC/Source/cmake/OptionsPlayStation.cmake"
if ! grep -q -- '-sce-stdlib=v2' "$OPTIONS"; then
    echo "warning: -sce-stdlib=v2 not found in $OPTIONS; the port changed," \
         "check the remaining steps by hand" >&2
fi
sed -i '/-sce-stdlib=v2/d' "$OPTIONS"

# Source patches, for code that relies on Sony's libc. Each one must apply,
# or the port changed and the patch needs a new look.
patch_source() {  # <file under Source/> <sed expression> <what it fixes>
    local file="$WEBKIT_SRC/Source/$1"
    local before

    before=$(md5sum < "$file")
    sed -i "$2" "$file"
    if [[ "$(md5sum < "$file")" == "$before" ]]; then
        echo "error: the patch for $3 did not apply to $1" >&2
        exit 1
    fi
}

# Sony's libc declares _umtx_op with a sixth argument. The SDK has the
# FreeBSD one, which libpas already calls in its FreeBSD branch.
patch_source bmalloc/libpas/src/libpas/pas_lock.c \
    's|^#elif PAS_PLATFORM(PLAYSTATION)$|#elif 0 // BrowserPS5: Sony 6-argument _umtx_op|' \
    "_umtx_op"

# Sony's pthread_get_name_np takes 2 arguments and returns an int; FreeBSD's
# takes the buffer size too and returns nothing.
patch_source bmalloc/libpas/src/libpas/pas_thread_local_cache.c \
    's|pthread_get_name_np(thread, thread_name);|(pthread_get_name_np(thread, thread_name, sizeof(thread_name)), 0);|' \
    "pthread_get_name_np"

# SO_NBIO is a Sony socket option; FreeBSD sets O_NONBLOCK with fcntl.
patch_source WTF/wtf/playstation/UniStdExtrasPlayStation.cpp \
    's|#include <sys/socket.h>|#include <fcntl.h>\n#include <sys/socket.h>|;
     s|return setsockopt(fileDescriptor, SOL_SOCKET, SO_NBIO, &nonBlocking, sizeof(int)) != -1;|(void)nonBlocking;\n    return fcntl(fileDescriptor, F_SETFL, fcntl(fileDescriptor, F_GETFL) \| O_NONBLOCK) != -1;|' \
    "SO_NBIO"

# SO_USE_DEVLAN binds to the second network port of Sony's dev kits; a
# retail PS5 uses its normal network. Only that one #if is turned off.
patch_source JavaScriptCore/inspector/remote/socket/posix/RemoteInspectorSocketPOSIX.cpp \
    '/^#if PLATFORM(PLAYSTATION)$/{N;s|^#if PLATFORM(PLAYSTATION)\n\(.*SO_USE_DEVLAN\)|#if 0 // BrowserPS5: SO_USE_DEVLAN is a Sony dev-kit socket option\n\1|}' \
    "SO_USE_DEVLAN"

# OptionsPlayStation.cmake copies these two folders next to every
# executable; with nothing to copy they just need to exist.
mkdir -p "$WORK/webkit-libraries/tools/sce_sys" \
         "$WORK/webkit-libraries/tools/scripts"


# 4. Configure. Notes on the less obvious settings:
#    - MEMORY_EXTRA_*: point the port's find_library/find_path at our shim.
#    - C_STD_LIBRARY: the port appends it to every target's link line; using
#      libScePosixForWebKit there provides __wrap_mmap (the port links with
#      --wrap=mmap) and getenv_np/clock_gettime_np/pthread_setname_np.
#    - KERNEL_LIBRARY: the SDK links libkernel_web by default; use the same.
#    - -include ps5-posix-webkit.h: declarations for those POSIX extras.
#    - CMAKE_PREFIX_PATH is re-rooted into the SDK sysroot by the toolchain
#      file, which is where build-icu.sh installed ICU.
#    - LD_PACK_RELATIVE_RELOCS=OFF: WebKit packs relative relocations into
#      DT_RELR whenever lld can, but elfldr only applies DT_RELA; with RELR
#      every pointer in data stays unrelocated and jsc crashes at startup.
POSIX_HDR="$HERE/shims/include/ps5-posix-webkit.h"

cmake -S "$WEBKIT_SRC" -B "$BUILD" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$PS5_PAYLOAD_SDK/toolchain/prospero.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DPORT=PlayStation \
    -DENABLE_WEBCORE=OFF \
    -DENABLE_WEBKIT=OFF \
    -DENABLE_STATIC_JSC=ON \
    -DDEVELOPER_MODE=OFF \
    -DLD_PACK_RELATIVE_RELOCS=OFF \
    -DWEBKIT_LIBRARIES_DIR="$WORK/webkit-libraries" \
    -DCMAKE_PREFIX_PATH="$PS5_HBROOT" \
    -DMEMORY_EXTRA_LIB="$WORK/shims/libMemoryExtra.a" \
    -DMEMORY_EXTRA_INCLUDE_DIR="$HERE/shims/include" \
    -DC_STD_LIBRARY="$PS5_SYSROOT/lib/libScePosixForWebKit.so" \
    -DKERNEL_LIBRARY="$PS5_SYSROOT/lib/libkernel_web.so" \
    -DCMAKE_C_FLAGS="-include $POSIX_HDR" \
    -DCMAKE_CXX_FLAGS="-include $POSIX_HDR"


# 5. Build only the shell.
# NINJA_ARGS="-k 0" keeps going after errors, to collect them all at once.
# CC is unset because WebKit's code generators run "$CC -E" as a plain
# preprocessor, and the SDK's prospero-clang appends crt1.o and libraries
# to such a command line; without CC they use the host's gcc.
env -u CC ninja -C "$BUILD" -j"$JOBS" ${NINJA_ARGS:-} jsc

cp "$BUILD/bin/jsc" "$WORK/jsc.elf"
echo
echo "built $WORK/jsc.elf ($(du -h "$WORK/jsc.elf" | cut -f1))"
echo "send it with:   (cat $WORK/jsc.elf; cat) | nc \$PS5_HOST 9021"
echo "then type JavaScript, e.g.  print(1 + 1)"
