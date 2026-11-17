#!/usr/bin/env bash
#   Copyright (C) 2026 BrowserPS5 contributors
#
# This file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# OpenGL ES on the console's GPU, for payloads: Mesa's zink (OpenGL on
# Vulkan) over the RADV broker client, which draws through the owner title
# (../gpu_broker, PPSA97010). Step 3 of docs/02 ("GPU com o PS5_Vulkan").
#
#   PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk bash build.sh
#
# Run ../gpu_broker/build.sh first: it makes the client archive this links.
# Output, in $OUT (~/browserps5-build/mesa-zink):
#   vulkan/libvulkan.so.1          RADV and the broker client in one library
#                                  (vulkan_client.c), built with PS5_Vulkan's
#                                  SDK fork as the client payload is;
#   stage/user/homebrew/{include,lib}
#                                  Mesa with zink only, EGL on the surfaceless
#                                  platform, built with PS5_PAYLOAD_SDK as
#                                  ../gl_soft's Mesa is;
#   mesa-zink-libs.tar             the four libraries for /user/homebrew/lib
#                                  (../install_tar).
#
# Mesa's libraries get names of their own (libEGL_zink.so.1,
# libGLESv2_zink.so.2, libgallium-zink-26.2.0.so), so they sit next to the
# softpipe ones WebKit links today (libEGL.so.1, libGLESv2.so.2,
# libgallium-26.2.0.so) without replacing them.
#
# BROWSER=1 builds the browser's Mesa instead (docs/03-video-e-gpu.md, G2),
# in ~/browserps5-build/mesa-browser: softpipe and zink in one libgallium,
# and EGL, GLES and that libgallium merged into one libEGL.so.1
# (merge-gl.sh), in mesa-browser-libs.tar with libvulkan.so.1. EGL tries
# zink first; with no owner title to bind to (vulkan_client.c waits for it)
# it falls back to softpipe. Every process that loads WebKit then loads
# libvulkan.so.1 too, which reserves the broker's ranges as it loads.

set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
WORK=${WORK:-$HOME/browserps5-build}
PACBREW=${PACBREW:-$WORK/pacbrew-repo}
BROWSER=${BROWSER:-0}
if (( BROWSER )); then
    OUT=${OUT:-$WORK/mesa-browser}
else
    OUT=${OUT:-$WORK/mesa-zink}
fi
JOBS=${JOBS:-$(nproc)}
VK=${PS5_VULKAN:-$WORK/ext/PS5_Vulkan}
FORK_SDK=$VK/.deps/native/ps5-payload-sdk
RELEASE=$VK/.deps/native/radv-release
CLIENT=${CLIENT:-$WORK/gpu_broker/client/client.a}
BROKER=$WORK/gpu_broker/upstream/tooling/gpu-broker
export PS5_CLANG=${PS5_CLANG:-clang-18}

MESA_VER=26.2.0
MESA_SHA256=efd4bb08cdb7c365a812cd4e6c9202ab55b2f22cdcd13c7d6c4f9647b799a4ef
MESA_TAR=$WORK/deps/sources/mesa-$MESA_VER.tar.xz
SRC=$OUT/mesa-$MESA_VER

if [[ -z "${PS5_PAYLOAD_SDK:-}" ]]; then
    echo "error: PS5_PAYLOAD_SDK is not set" >&2
    exit 1
fi
for file in "$CLIENT" "$BROKER/client.h" "$BROKER/protocol.h" "$FORK_SDK/bin/prospero-clang" \
        "$VK/tools/radv-link.sh" "$RELEASE/include/vulkan/vulkan.h"; do
    [[ -e $file ]] || { echo "error: missing $file (run ../gpu_broker/build.sh first)" >&2; exit 1; }
done
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


# 1. libvulkan.so.1: the broker client archive whole (RADV, its winsys over
#    the broker, our TCP transport), vulkan_client.c, and the libraries and
#    flags of PS5_Vulkan's link recipe, as the client payload links them. Its
#    own version script, which keeps everything but the two entry points
#    local, takes the place of the recipe's (that one only lists the names
#    the recipe binds to the platform, which ours hides too).
VULKAN=$OUT/vulkan
rm -rf "$VULKAN"
mkdir -p "$VULKAN"
# shellcheck source=/dev/null
source "$VK/tools/radv-link.sh"
radv_link_recipe "$VK" "$FORK_SDK" "$CLIENT"
so_flags=()
skip=0
for flag in "${radv_link_flags[@]}"; do
    if (( skip )); then skip=0; continue; fi
    if [[ $flag == --version-script ]]; then skip=1; continue; fi
    so_flags+=("-Wl,$flag")
done
so_inputs=()
for input in "${radv_link_inputs[@]}"; do
    case $input in -*) so_inputs+=("-Wl,$input") ;; *) so_inputs+=("$input") ;; esac
done
"$FORK_SDK/bin/prospero-clang" -std=gnu11 -O2 -Wall -Wextra -Werror -fPIC \
    -I"$BROKER" -I"$RELEASE/include" \
    -c "$HERE/vulkan_client.c" -o "$VULKAN/vulkan_client.o"
# libc.a after the archive, as for the client payload (../gpu_broker/build.sh).
"$FORK_SDK/bin/prospero-clang" -shared -nodefaultlibs -o "$VULKAN/libvulkan.so.1.debug" \
    "$VULKAN/vulkan_client.o" "${so_inputs[@]}" "${so_flags[@]}" \
    -Wl,-soname,libvulkan.so.1 -Wl,--version-script,"$HERE/vulkan_client.map" -Wl,-z,defs \
    -lc -lSceLibcInternal -lSceNet -lkernel_web
"$FORK_SDK/bin/llvm-strip" --strip-debug -o "$VULKAN/libvulkan.so.1" "$VULKAN/libvulkan.so.1.debug"
exports=$("$FORK_SDK/bin/llvm-nm" -D --defined-only "$VULKAN/libvulkan.so.1" | awk '{ print $NF }' | sort | tr '\n' ' ')
if [[ $exports != "vkGetDeviceProcAddr vkGetInstanceProcAddr " ]]; then
    echo "error: libvulkan.so.1 exports: $exports" >&2
    exit 1
fi
echo "libvulkan.so.1: $(stat -c %s "$VULKAN/libvulkan.so.1") bytes, exports: $exports"


source "$PS5_PAYLOAD_SDK/toolchain/prospero.sh"

# 2. Mesa's sources, pacbrew's PS5 patches, and ../gl_soft's changes to
#    them (build-mesa-soft.sh says why each one).
if [[ ! -f "$MESA_TAR" ]]; then
    mkdir -p "$(dirname "$MESA_TAR")"
    wget -O "$MESA_TAR" "https://mesa.freedesktop.org/archive/mesa-$MESA_VER.tar.xz"
fi
echo "$MESA_SHA256  $MESA_TAR" | sha256sum -c -

rm -rf "$SRC" "$OUT/build" "$OUT/stage"
tar -xJf "$MESA_TAR" -C "$OUT"
for p in "$PACBREW"/mesa/[0-9][0-9][0-9][0-9]-*.patch "$HERE/mesa-zink-ps5.patch"; do
    echo "applying $(basename "$p")"
    patch -d "$SRC" -Np1 -s -i "$p"
done

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

# zink on the PS5 without RADV in this build: pacbrew's condition asks for
# RADV's PS5 winsys too, because there zink links the RADV it builds.
patch_source meson.build \
    "s|^with_ps5_zink = with_gallium_zink and .*$|with_ps5_zink = with_gallium_zink and host_machine.system() != 'linux' and cc.get_define('__PROSPERO__') != ''|" \
    "zink without RADV"
patch_source src/gallium/drivers/zink/meson.build \
    "s|^  zink_deps += libvulkan_radeon$|  zink_link_args += ['$VULKAN/libvulkan.so.1']|" \
    "zink linking our libvulkan.so.1"
if (( ! BROWSER )); then
    patch_source src/gallium/targets/dri/meson.build \
        "s|^  libgallium_name = 'gallium-@0@'|  libgallium_name = 'gallium-zink-@0@'|" \
        "libgallium's own name"
fi
# Mesa's C11 threads under their own names on any PS5 build, not only with
# RADV: otherwise libgallium links the SDK libc.a's thrd_create, whose
# success is 4, not Mesa's 0. The first console run (2026-11-04) took a
# created thread for a failure ("zink: Failed to create flush queue") and
# then hit a heap error, with mtx_init from libc and cnd_init from Mesa.
patch_source meson.build \
    "/^# own, so that a link never takes the libc's in their place.$/{n;s|^if with_radv_ps5_console$|if with_radv_ps5_console or with_ps5|}" \
    "Mesa's own C11 threads"

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
patch_source src/gallium/auxiliary/translate/translate.c \
    's|^#if DETECT_ARCH_X86 \|\| DETECT_ARCH_X86_64$|#if (DETECT_ARCH_X86 \|\| DETECT_ARCH_X86_64) \&\& !defined(__PROSPERO__)|' \
    "the C vertex translation"


# 3. Configure and build: zink only (or softpipe and zink), EGL on the
#    surfaceless platform (the one WebKit's WebProcess uses: it draws to
#    framebuffer objects).
if (( BROWSER )); then
    DRIVERS=softpipe,zink
    NAMES=()
else
    DRIVERS=zink
    NAMES=(-Degl-lib-suffix=_zink -Dgles-lib-suffix=_zink)
fi
$MESON setup "$OUT/build" "$SRC" \
    -Ddefault_library=shared \
    -Dbuildtype=release \
    -Db_ndebug=true \
    -Dgallium-drivers=$DRIVERS \
    -Dvulkan-drivers= \
    -Dvulkan-layers= \
    -Dplatforms= \
    -Degl=enabled \
    -Dopengl=true \
    -Dgles2=enabled \
    -Dgles1=disabled \
    -Dglx=disabled \
    -Dgbm=disabled \
    -Dllvm=disabled \
    -Dshader-cache=disabled \
    -Dzstd=disabled \
    -Dzlib=enabled \
    -Dvalgrind=disabled \
    -Dlibunwind=disabled \
    -Dexpat=disabled \
    -Dxmlconfig=disabled \
    -Dvideo-codecs= \
    -Dbuild-tests=false \
    "${NAMES[@]}"

$MESON compile -C "$OUT/build" -j "$JOBS"
$MESON install -C "$OUT/build" --destdir "$OUT/stage"


# 4. The libraries for the console, as real files under their sonames (the
#    payload loader opens /user/homebrew/lib/<soname>). libGL is not needed.
LIB=user/homebrew/lib
cp "$VULKAN/libvulkan.so.1" "$OUT/stage/$LIB/libvulkan.so.1"
LIBS=("$LIB/libvulkan.so.1")
if (( BROWSER )); then
    bash "$HERE/merge-gl.sh" "$OUT"
    TAR=$OUT/mesa-browser-libs.tar
    BUILT=("$OUT/stage/$LIB"/libEGL.so.1)
else
    TAR=$OUT/mesa-zink-libs.tar
    BUILT=("$OUT/stage/$LIB"/libEGL_zink.so.1 "$OUT/stage/$LIB"/libGLESv2_zink.so.2
           "$OUT/stage/$LIB"/libgallium-zink-*.so)
fi
for f in "${BUILT[@]}"; do
    [[ -e "$f" ]] || { echo "error: $f was not built" >&2; exit 1; }
    LIBS+=("$LIB/$(basename "$f")")
done
tar -chf "$TAR" -C "$OUT/stage" "${LIBS[@]}"

echo
tar -tvf "$TAR"
for f in "${LIBS[@]}"; do
    echo "$(basename "$f") needs: $(readelf -d "$OUT/stage/$f" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p' | tr '\n' ' ')"
done
echo "built $TAR"
