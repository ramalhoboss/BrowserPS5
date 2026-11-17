#!/usr/bin/env bash
#   Copyright (C) 2026 BrowserPS5 contributors
#
# This file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# The GPU broker over TCP, step 2 of docs/02 ("GPU com o PS5_Vulkan"):
#
#   bash build.sh
#
# makes, next to this file:
#   owner.tar            the owner title PPSA97010, for install_tar
#                        (data/homebrew/), registered by ShadowMountPlus;
#   gpu_client.elf       the payload that draws through it (waits up to
#                        600 s for the title);
#   gpu_client-probe.elf the same, waiting 3 s: a load check with no title.
#
# From PS5_Vulkan where it was built (~/browserps5-build/ext/PS5_Vulkan,
# outside this project): its RADV release archive, its pinned payload SDK
# fork, its title tooling, and its GPU broker (tooling/gpu-broker,
# LGPL-2.1-or-later) at BROKER_REVISION, copied out with git archive (their
# tree is not changed). That revision is the broker's last whose owner
# builds from the repository alone: the next one, 76fedd2, includes
# PS5_Proton headers (pw_window_scene.h and others) that are not in it. Its
# packet_io.c is replaced with packet_io_tcp.c here, in the owner and in the
# client archive.

set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
vk=${PS5_VULKAN:-$HOME/browserps5-build/ext/PS5_Vulkan}
work=${WORK:-$HOME/browserps5-build/gpu_broker}
BROKER_REVISION=baaf3ad07f280592459be8e07598b1ab10f72a96
sdk=$vk/.deps/native/ps5-payload-sdk
native=$vk/tooling/native
tool=$vk/build/host/ps5-native-tool
release=$vk/.deps/native/radv-release
winsys=$vk/.deps/work/radv-src/src/amd/vulkan/winsys/ps5
export PS5_CLANG=${PS5_CLANG:-clang-18}

for file in "$release/lib/libvulkan_radeon.ps5.a" "$sdk/bin/prospero-lld" "$tool" "$vk/runtime/libc.prx" \
        "$winsys/radv_ps5_platform.h" "$vk/.deps/work/radv-build-ps5-release/compile_commands.json"; do
    [[ -e $file ]] || { echo "missing $file (build PS5_Vulkan's release RADV and tooling first)" >&2; exit 2; }
done
command -v glslangValidator >/dev/null || { echo "glslangValidator was not found" >&2; exit 2; }
(cd "$vk/runtime" && sha256sum --check --strict --quiet libc.prx.sha256)

# The broker's sources at its revision, a copy of their own.
up=$work/upstream
rm -rf -- "$up"
mkdir -p "$up" "$work/obj" "$work/gen"
git -C "$vk" archive "$BROKER_REVISION" tooling/gpu-broker tools/build-gpu-broker-client.sh | tar -x -C "$up"
broker=$up/tooling/gpu-broker

# The client archive: their script, less its pin of the RADV archive's hash
# (made on their machine; ours is the one built here, its hash in
# PROVENANCE.txt and in the manifest below). Then our transport in place of
# theirs: the member keeps its name, packet_io.o.
sed -i '/RADV archive pin differs/d' "$up/tools/build-gpu-broker-client.sh"
rm -rf -- "$work/client"
PS5_VULKAN_BUILD_ROOT=$vk bash "$up/tools/build-gpu-broker-client.sh" "$work/client" "$sdk" 1 0
"$sdk/bin/prospero-clang" -std=c11 -O2 -Wall -Wextra -Werror -fPIC -I"$broker" -I"$here" \
    -c "$here/packet_io_tcp.c" -o "$work/client/obj/packet_io.o"
"$sdk/bin/llvm-ar" r "$work/client/client.a" "$work/client/obj/packet_io.o"
python3 -I - "$work/client/foundation-manifest.json" "$here/packet_io_tcp.c" "$release/PROVENANCE.txt" <<'EOF'
import hashlib, json, sys
path, transport, provenance = sys.argv[1:]
manifest = json.load(open(path))
manifest['browserps5'] = {
    'broker_revision': 'baaf3ad07f280592459be8e07598b1ab10f72a96',
    'packet_io_tcp_sha256': hashlib.sha256(open(transport, 'rb').read()).hexdigest(),
    'radv_release': open(provenance).read().strip().splitlines(),
}
json.dump(manifest, open(path, 'w'), indent=2)
EOF

# The triangle's shaders.
: > "$work/gen/triangle_shaders.h.tmp"
for stage in vert frag; do
    glslangValidator -V --target-env vulkan1.0 --vn "triangle_$stage" \
        "$here/triangle.$stage" -o "$work/gen/triangle_$stage.h" > /dev/null
    cat "$work/gen/triangle_$stage.h" >> "$work/gen/triangle_shaders.h.tmp"
done
mv "$work/gen/triangle_shaders.h.tmp" "$work/gen/triangle_shaders.h"

# The client payload: their link recipe's libraries and flags (the SDK's
# platform layer standing in for what libc lacks), with the payload SDK's
# own start files and linker script.
# shellcheck source=/dev/null
source "$vk/tools/radv-link.sh"
radv_link_recipe "$vk" "$sdk" "$work/client/client.a"
payload_flags=()
for flag in "${radv_link_flags[@]}"; do payload_flags+=("-Wl,$flag"); done
payload_inputs=()
for input in "${radv_link_inputs[@]}"; do
    case $input in -*) payload_inputs+=("-Wl,$input") ;; *) payload_inputs+=("$input") ;; esac
done
for variant in gpu_client gpu_client-probe; do
    wait_flag=()
    [[ $variant == gpu_client-probe ]] && wait_flag=(-DWAIT_SECONDS=3)
    "$sdk/bin/prospero-clang" -std=gnu11 -O2 -Wall -Wextra -Werror "${wait_flag[@]}" \
        -I"$broker" -I"$release/include" -I"$work/gen" \
        -c "$here/gpu_client.c" -o "$work/obj/$variant.o"
    # The SDK's libc.a after the archive, not before (where the driver puts
    # it): it has C11 threads of its own (thrd.o, cnd.o, ...), which the
    # archive's Mesa defines too; taken first, one of the archive's members
    # pulled them in and they were defined twice.
    "$sdk/bin/prospero-clang" -nodefaultlibs -o "$work/$variant.elf" "$work/obj/$variant.o" \
        "${payload_inputs[@]}" "${payload_flags[@]}" -lc -lSceLibcInternal -lSceNet -lkernel_web
    # Without its debug information: 26 MB to send, not 102.
    "$sdk/bin/llvm-strip" --strip-debug -o "$here/$variant.elf" "$work/$variant.elf"
done

# The owner title: the broker's server and display, our transport and
# owner.cpp, the title CRT, and RADV's platform layer from the release
# archive (only the members it needs: no Vulkan runs here).
cc() { PS5_PAYLOAD_SDK="$sdk" sh "$vk/tooling/prospero-clang18" "$@"; }
cxxflags=(-std=c++20 -O2 -Wall -Wextra -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections
          -I"$broker" -I"$winsys" -I"$here")
cc "${cxxflags[@]}" -c "$broker/server.cpp" -o "$work/obj/server.o"
cc "${cxxflags[@]}" -c "$broker/display.cpp" -o "$work/obj/display.o"
cc "${cxxflags[@]}" -Werror -c "$here/owner.cpp" -o "$work/obj/owner.o"
cc -std=c11 -O2 -Wall -Wextra -Werror -I"$broker" -I"$here" -c "$here/packet_io_tcp.c" -o "$work/obj/packet_io_tcp.o"
cc -std=c++20 -O2 -Wall -Wextra -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections \
    -c "$native/app_crt.cpp" -o "$work/obj/app_crt.o"
cc -std=c++20 -O2 -Wall -Wextra -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections \
    -c "$native/app_cpp_runtime.cpp" -o "$work/obj/app_cpp_runtime.o"
mkdir -p "$work/stubs"
stub() {
    cc -std=c11 -O2 -fPIC -c "$vk/$2" -o "$work/obj/$1_stub.o"
    "$sdk/bin/prospero-lld" --shared -soname "$1.prx" -o "$work/stubs/$1.so" "$work/obj/$1_stub.o"
}
stub libSceAgc vendor/ps5/sdk/stubs/agc_canary_link_stub.c
stub libSceAgcDriver vendor/ps5/sdk/stubs/agc_driver_canary_link_stub.c
radv_link_recipe "$vk" "$sdk" "$release/lib/libvulkan_radeon.ps5.a"
owner_inputs=()
for input in "${radv_link_inputs[@]}"; do
    [[ $input == --whole-archive || $input == --no-whole-archive ]] || owner_inputs+=("$input")
done
"$sdk/bin/prospero-lld" "${radv_linker_script[@]}" --eh-frame-hdr "${radv_link_flags[@]}" \
    --version-script "$native/app-symbols.map" --exclude-libs=ALL --gc-sections \
    -e _start -o "$work/llvm-pie.elf" \
    "$work/obj/app_crt.o" "$work/obj/app_cpp_runtime.o" "$work/obj/owner.o" "$work/obj/server.o" \
    "$work/obj/display.o" "$work/obj/packet_io_tcp.o" \
    "$work/stubs/libSceAgc.so" "$work/stubs/libSceAgcDriver.so" \
    "${owner_inputs[@]}" \
    --as-needed "$sdk"/target/lib/*.so
"$tool" link --in "$work/llvm-pie.elf" --out "$work/eboot.elf" \
    --stub-dir "$sdk/target/lib" --stub "$work/stubs/libSceAgc.so" \
    --stub "$work/stubs/libSceAgcDriver.so" --module-sdk 0x02000009 \
    --companion-sdk 0x08050001 --file-name eboot.elf

title_id=$(python3 -I -c 'import json,sys; print(json.load(open(sys.argv[1]))["titleId"])' "$here/param.json")
app=$work/$title_id
rm -rf -- "$app"
mkdir -p "$app/sce_sys" "$app/sce_module"
"$tool" self --sign --in "$work/eboot.elf" --out "$app/eboot.bin" --magic 0x1D3D154F
cp "$here/param.json" "$app/sce_sys/param.json"
cp "$vk/runtime/libc.prx" "$app/sce_module/libc.prx"
# A plain 512x512 RGBA icon (RGB ones never showed on the home screen).
python3 -I - "$app/sce_sys/icon0.png" <<'EOF'
import struct, sys, zlib
w = h = 512
row = b"\x00" + bytes((0x60, 0x20, 0xa0, 0xff)) * w
def chunk(kind, data):
    body = kind + data
    return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))
png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
       chunk(b"IDAT", zlib.compress(row * h, 9)) + chunk(b"IEND", b""))
open(sys.argv[1], "wb").write(png)
EOF
"$tool" self --inspect --file "$app/eboot.bin" > /dev/null
# param.json last: the title shows up only once the rest is there.
(cd "$work" && tar --format=ustar --owner=0 --group=0 --no-recursion \
    --transform "s,^,data/homebrew/," -cf "$here/owner.tar" \
    "$title_id" "$title_id/sce_module" "$title_id/sce_module/libc.prx" \
    "$title_id/sce_sys" "$title_id/sce_sys/icon0.png" \
    "$title_id/eboot.bin" "$title_id/sce_sys/param.json")
echo "owner $title_id: $here/owner.tar ($(stat -c %s "$app/eboot.bin") bytes of eboot.bin)"
ls -l "$here"/gpu_client*.elf
