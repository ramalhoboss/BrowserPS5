#!/usr/bin/env bash
#   Copyright (C) 2026 BrowserPS5 contributors
#
# This file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# Build Mesa with only its CPU renderer, for PS5 payloads: EGL on the
# surfaceless platform and OpenGL ES 2/3 drawn by softpipe (or llvmpipe,
# once LLVM is built for the PS5). Nothing here uses the console's GPU:
# WebKit's compositor gets OpenGL ES, and the CPU does the drawing.
#
# It starts from pacbrew-repo's PS5 Mesa package (ps5-payload-dev), whose
# patches make Mesa build and run on the console, and widens the few
# conditions of its meson files that only covered the RADV/zink build.
#
# Run inside WSL/Linux, with PS5_PAYLOAD_SDK set.
# Output:
#   $OUT/stage/user/homebrew/{include,lib}  headers and libraries to link with
#   $OUT/mesa-soft-libs.tar                 the libraries for the PS5, for
#                                           ../install_tar

set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
WORK=${WORK:-$HOME/browserps5-build}
PACBREW=${PACBREW:-$WORK/pacbrew-repo}
OUT=${OUT:-$WORK/mesa-soft}
DRIVERS=${DRIVERS:-softpipe}
JOBS=${JOBS:-$(nproc)}

MESA_VER=26.2.0
MESA_SHA256=efd4bb08cdb7c365a812cd4e6c9202ab55b2f22cdcd13c7d6c4f9647b799a4ef
MESA_TAR=$WORK/deps/sources/mesa-$MESA_VER.tar.xz
SRC=$OUT/mesa-$MESA_VER

if [[ -z "${PS5_PAYLOAD_SDK:-}" ]]; then
    echo "error: PS5_PAYLOAD_SDK is not set" >&2
    exit 1
fi
# Mesa 26 needs a newer meson than Ubuntu 24.04's; the SDK's prospero-meson
# runs whichever meson comes first in PATH.
if [[ -x "$WORK/meson-venv/bin/meson" ]]; then
    export PATH="$WORK/meson-venv/bin:$PATH"
fi
for tool in meson ninja python3 patch; do
    command -v "$tool" >/dev/null || { echo "error: $tool not found" >&2; exit 1; }
done
if ! grep -q "^pkgver=$MESA_VER$" "$PACBREW/mesa/PKGBUILD"; then
    echo "error: $PACBREW/mesa is not Mesa $MESA_VER; its patches may not apply" >&2
    exit 1
fi

source "$PS5_PAYLOAD_SDK/toolchain/prospero.sh"


# 1. Sources, and pacbrew's PS5 patches.
if [[ ! -f "$MESA_TAR" ]]; then
    mkdir -p "$(dirname "$MESA_TAR")"
    wget -O "$MESA_TAR" "https://mesa.freedesktop.org/archive/mesa-$MESA_VER.tar.xz"
fi
echo "$MESA_SHA256  $MESA_TAR" | sha256sum -c -

rm -rf "$SRC" "$OUT/build" "$OUT/stage"
mkdir -p "$OUT"
tar -xJf "$MESA_TAR" -C "$OUT"
for p in "$PACBREW"/mesa/[0-9][0-9][0-9][0-9]-*.patch; do
    echo "applying $(basename "$p")"
    patch -d "$SRC" -Np1 -s -i "$p"
done


# 2. The PS5 parts of those patches that are not about zink: no libdrm
#    (the console has no DRM), DRI without DRM (the 'pseudo-drm' platform),
#    and libgallium exporting glapi's functions instead of its thread-local
#    variables (a library on the PS5 cannot import another library's TLS).
#    pacbrew turns them on only with RADV and zink; here they follow the
#    target.
patch_source() {  # <file under the Mesa tree> <sed expression> <what it fixes>
    local file="$SRC/$1"
    local before

    before=$(md5sum < "$file")
    sed -i "$2" "$file"
    if [[ "$(md5sum < "$file")" == "$before" ]]; then
        echo "error: the patch for $3 did not apply to $1" >&2
        exit 1
    fi
}

patch_source meson.build \
    "s|^\(if _vulkan_drivers.contains('amd') and get_option('radv-winsys') == 'ps5' and host_machine.system() != 'linux'\)$|\1 or cc.get_define('__PROSPERO__') != ''|" \
    "no libdrm"
patch_source meson.build \
    "s|^\(with_ps5_zink = .*\)$|\1\n# BrowserPS5: any PS5 build, with or without zink.\nwith_ps5 = host_machine.system() != 'linux' and cc.get_define('__PROSPERO__') != ''|" \
    "with_ps5"
patch_source meson.build \
    "/^if with_gallium and (system_has_kms_drm/s|or with_ps5_zink)$|or with_ps5)|" \
    "DRI without DRM"
patch_source meson.build \
    "/^elif host_machine.system() == 'gnu'/s|or with_ps5_zink$|or with_ps5|" \
    "the pseudo-drm platform"
patch_source src/gallium/meson.build \
    "s|^if with_ps5_zink$|if with_ps5|" \
    "glapi's exports"
# Mesa's C11 threads under names of their own (pacbrew's C11_THREADS_RENAME)
# on any PS5 build, not only with RADV: otherwise libgallium links the SDK
# libc.a's FreeBSD thrd_create, whose success is 4, not Mesa's 0, so Mesa
# takes a thread it created for a failure (zink's first run, ../gl_zink,
# 2026-11-04: "Failed to create flush queue", then a libc heap error).
patch_source meson.build \
    "/^# own, so that a link never takes the libc's in their place.$/{n;s|^if with_radv_ps5_console$|if with_radv_ps5_console or with_ps5|}" \
    "Mesa's own C11 threads"

# The only code Mesa's CPU renderer generates at run time is the SSE
# version of its vertex translation, which needs executable memory. On the
# PS5 it uses the portable C version, so Mesa never asks for such memory.
patch_source src/gallium/auxiliary/translate/translate.c \
    's|^#if DETECT_ARCH_X86 \|\| DETECT_ARCH_X86_64$|#if (DETECT_ARCH_X86 \|\| DETECT_ARCH_X86_64) \&\& !defined(__PROSPERO__)|' \
    "the C vertex translation"


# 3. Configure and build. The surfaceless platform is always there; with no
#    DRM device it falls back to the CPU renderer.
if [[ ",$DRIVERS," == *,llvmpipe,* ]]; then
    LLVM=enabled
else
    LLVM=disabled
fi

$MESON setup "$OUT/build" "$SRC" \
    -Ddefault_library=shared \
    -Dbuildtype=release \
    -Db_ndebug=true \
    -Dgallium-drivers="$DRIVERS" \
    -Dvulkan-drivers= \
    -Dvulkan-layers= \
    -Dplatforms= \
    -Degl=enabled \
    -Dopengl=true \
    -Dgles2=enabled \
    -Dgles1=disabled \
    -Dglx=disabled \
    -Dgbm=disabled \
    -Dllvm=$LLVM \
    -Dshader-cache=disabled \
    -Dzstd=disabled \
    -Dzlib=enabled \
    -Dvalgrind=disabled \
    -Dlibunwind=disabled \
    -Dexpat=disabled \
    -Dxmlconfig=disabled \
    -Dvideo-codecs= \
    -Dbuild-tests=false

$MESON compile -C "$OUT/build" -j "$JOBS"
$MESON install -C "$OUT/build" --destdir "$OUT/stage"


# 4. The libraries for the console, as real files under their sonames (the
#    payload loader opens /user/homebrew/lib/<soname>).
LIB=user/homebrew/lib
LIBS=()
for f in "$OUT/stage/$LIB"/libEGL.so.1 "$OUT/stage/$LIB"/libGLESv2.so.2 \
         "$OUT/stage/$LIB"/libGL.so.1 "$OUT/stage/$LIB"/libgallium-*.so; do
    [[ -e "$f" ]] && LIBS+=("$LIB/$(basename "$f")")
done
tar -chf "$OUT/mesa-soft-libs.tar" -C "$OUT/stage" "${LIBS[@]}"

echo
tar -tvf "$OUT/mesa-soft-libs.tar"
echo "built $OUT/mesa-soft-libs.tar (drivers: $DRIVERS)"
