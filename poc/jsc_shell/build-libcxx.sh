#!/usr/bin/env bash
#   Copyright (C) 2024 John Törnblom (the ps5-payload-sdk's libcxx.sh)
#   Copyright (C) 2026 BrowserPS5 contributors
#
# This file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# Rebuild libc++ (with libc++abi and libunwind) for the PS5 from a newer
# LLVM than the 18.1.8 the ps5-payload-sdk release ships, and install it
# into the SDK sysroot. WebKit needs libc++ 19 or later (std::atomic_ref)
# and clang 19 or later. The SDK compiles with the newest clang it finds
# (llvm-config-NN), so install clang-20 first.
#
# Same configuration as the SDK's libcxx.sh; only the version changes.
# Run inside WSL/Linux, with PS5_PAYLOAD_SDK set, before build-icu.sh.

set -euo pipefail

LLVM_VER=${LLVM_VER:-20.1.8}
LLVM_SRC=llvm-project-$LLVM_VER.src
LLVM_URL=https://github.com/llvm/llvm-project/releases/download/llvmorg-$LLVM_VER/$LLVM_SRC.tar.xz

WORK=${WORK:-$HOME/browserps5-build}
JOBS=${JOBS:-$(nproc)}

if [[ -z "${PS5_PAYLOAD_SDK:-}" ]]; then
    echo "error: PS5_PAYLOAD_SDK is not set" >&2
    exit 1
fi

source "$PS5_PAYLOAD_SDK/toolchain/prospero.sh"

clang_ver=$("$PS5_PAYLOAD_SDK/bin/prospero-llvm-config" --version)
if (( ${clang_ver%%.*} < ${LLVM_VER%%.*} )); then
    echo "error: the SDK compiles with clang $clang_ver; install" \
         "clang-${LLVM_VER%%.*} for libc++ $LLVM_VER" >&2
    exit 1
fi

mkdir -p "$WORK/libcxx"
cd "$WORK/libcxx"

if [[ ! -d $LLVM_SRC ]]; then
    wget -q --show-progress -O "$LLVM_SRC.tar.xz" "$LLVM_URL"
    tar xf "$LLVM_SRC.tar.xz"
fi

# libc++ assumes every FreeBSD has copy_file_range (FreeBSD 13 and later);
# the PS5's kernel and the SDK's headers are FreeBSD 11, so std::filesystem
# has to copy files through fstreams instead.
OPS=$LLVM_SRC/libcxx/src/filesystem/operations.cpp
sed -i 's|^#elif defined(__FreeBSD__)$|#elif 0 // BrowserPS5: no copy_file_range before FreeBSD 13|' "$OPS"
if ! grep -q 'BrowserPS5: no copy_file_range' "$OPS"; then
    echo "error: the copy_file_range patch did not apply to $OPS" >&2
    exit 1
fi

rm -rf build
"$CMAKE" -S "$LLVM_SRC/runtimes" -B build -G Ninja \
    -DCMAKE_INSTALL_PREFIX="$PS5_SYSROOT" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS="-nostdlib++" \
    -DLLVM_ENABLE_RUNTIMES="libunwind;libcxxabi;libcxx" \
    -DLIBCXX_ENABLE_STATIC=YES \
    -DLIBCXX_ENABLE_RTTI=YES \
    -DLIBCXX_ENABLE_EXCEPTIONS=YES \
    -DLIBCXX_ENABLE_THREADS=YES \
    -DLIBCXX_ENABLE_SHARED=NO \
    -DLIBCXX_ENABLE_ABI_LINKER_SCRIPT=NO \
    -DLIBCXX_CXX_ABI=libcxxabi \
    -DLIBCXXABI_ENABLE_STATIC=YES \
    -DLIBCXXABI_ENABLE_EXCEPTIONS=YES \
    -DLIBCXXABI_ENABLE_THREADS=YES \
    -DLIBCXXABI_USE_LLVM_UNWINDER=YES \
    -DLIBCXXABI_ENABLE_SHARED=NO \
    -DLIBCXXABI_USE_COMPILER_RT=NO \
    -DLIBUNWIND_ENABLE_STATIC=YES \
    -DLIBUNWIND_ENABLE_THREADS=YES \
    -DLIBUNWIND_IS_BAREMETAL=YES \
    -DLIBUNWIND_ENABLE_SHARED=NO \
    -DLIBUNWIND_USE_COMPILER_RT=NO
ninja -C build -j"$JOBS"

# Remove libc++ 18's headers first, or the ones 20 dropped would linger.
rm -rf "$PS5_SYSROOT/include/c++/v1"
DESTDIR=/ ninja -C build install

echo
echo "libc++ $LLVM_VER installed into $PS5_SYSROOT"
grep -m1 '_LIBCPP_VERSION ' "$PS5_SYSROOT/include/c++/v1/__config"
