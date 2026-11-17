#!/usr/bin/env bash
#   Copyright (C) 2026 BrowserPS5 contributors
#
# This file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# Cross-compile ICU (static libraries, data linked in) for the PS5 and
# install it into the SDK sysroot, where WebKit's find_package(ICU) finds it.
#
# ICU has to be built twice: once for the PC, because the cross build runs
# ICU's own tools to package its data, and once for the PS5.
#
# Run inside WSL/Linux, with PS5_PAYLOAD_SDK set.

set -euo pipefail

ICU_VERSION=74.2
ICU_TAG=release-${ICU_VERSION//./-}
ICU_TGZ=icu4c-${ICU_VERSION//./_}-src.tgz
ICU_URL=https://github.com/unicode-org/icu/releases/download/${ICU_TAG}/${ICU_TGZ}

WORK=${WORK:-$HOME/browserps5-build}
JOBS=${JOBS:-$(nproc)}

if [[ -z "${PS5_PAYLOAD_SDK:-}" ]]; then
    echo "error: PS5_PAYLOAD_SDK is not set" >&2
    exit 1
fi

mkdir -p "$WORK/icu"
cd "$WORK/icu"

if [[ ! -d src ]]; then
    wget -q --show-progress -O "$ICU_TGZ" "$ICU_URL"
    mkdir src
    tar xzf "$ICU_TGZ" -C src --strip-components=1
fi

# 1. Host build (only the tools and data are used).
if [[ ! -f host/bin/icupkg ]]; then
    mkdir -p host
    (cd host && ../src/source/runConfigureICU Linux \
        --disable-tests --disable-samples \
        && make -j"$JOBS")
fi

# 2. PS5 build. prospero.sh exports CC, CXX, AR, RANLIB, PS5_SYSROOT and
#    PS5_HBROOT (/user/homebrew, the prefix homebrew libraries live under).
source "$PS5_PAYLOAD_SDK/toolchain/prospero.sh"

rm -rf ps5
mkdir ps5
(cd ps5 && ../src/source/configure \
    --host=x86_64-pc-freebsd \
    --with-cross-build="$WORK/icu/host" \
    --prefix="$PS5_HBROOT" \
    --enable-static --disable-shared \
    --with-data-packaging=static \
    --disable-tests --disable-samples --disable-tools --disable-extras \
    --disable-layoutex --disable-dyload \
    CC="$CC" CXX="$CXX" AR="$AR" RANLIB="$RANLIB" \
    CFLAGS="-O2 -fPIC" CXXFLAGS="-O2 -fPIC -std=c++17" \
    && make -j"$JOBS" \
    && make DESTDIR="$PS5_SYSROOT" install)

echo
echo "ICU $ICU_VERSION installed into $PS5_SYSROOT$PS5_HBROOT"
ls "$PS5_SYSROOT$PS5_HBROOT/lib" | grep -i icu
