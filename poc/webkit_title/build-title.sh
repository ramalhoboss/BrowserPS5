#!/usr/bin/env bash
#   Copyright (C) 2026 BrowserPS5 contributors
#
# This file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# Link main.cpp with all of WebKit (the objects and static libraries that
# make libWebKit.so in a finished WebKit build) into a directory title
# (PPSA97022), and pack it for install_tar under data/homebrew/:
#
#   bash build-title.sh title.tar
#
# The title side is PS5_Vulkan's tooling, as in poc/title_probe: its CRT,
# its platform layer and link flags (tools/radv-link.sh: malloc and thread
# stacks in direct memory, the libc functions a title lacks),
# ps5-native-tool and the libc.prx its generator makes. The C++ library is
# the one WebKit was compiled with (libc++ 20 in /opt/ps5-payload-sdk; the
# SDK fork PS5_Vulkan pins has 18).
#
# Both WebKit and the platform layer wrap pthread_create (larger stacks for
# WebKit's 64 KiB threads, docs/03; stacks in direct memory for the
# platform). Here they are chained: WebKit's calls go to libc-fixes'
# wrapper, which calls the platform's, which calls libc's.
#
# Mesa's libEGL.so.1 is not linked (a title loads no .so): its symbols are
# stood in for by functions that log their name and return 0, which is
# fine while nothing calls them (this test runs JavaScriptCore only).

set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
out=$(realpath -m -- "${1:?usage: build-title.sh OUTPUT.tar}")
vk=${PS5_VULKAN:-$HOME/browserps5-build/ext/PS5_Vulkan}
work=${WORK:-$HOME/browserps5-build/webkit_title}
J=${WEBKIT_BUILD:-$HOME/browserps5-build/webkit-build-media-jit}
B=$HOME/browserps5-build
sdk=$vk/.deps/native/ps5-payload-sdk
opt=/opt/ps5-payload-sdk
native=$vk/tooling/native
tool=$vk/build/host/ps5-native-tool
export PS5_CLANG=${PS5_CLANG:-clang-18}

title_id=$(python3 -I -c 'import json,sys; print(json.load(open(sys.argv[1]))["titleId"])' \
	   "$here/param.json")
for file in "$sdk/bin/prospero-lld" "$tool" "$vk/runtime/libc.prx" "$native/app_crt.cpp" \
            "$J/build.ninja" "$J/lib/libWebKit.so.SOVERSION"; do
    [[ -e $file ]] || { echo "missing $file" >&2; exit 2; }
done
(cd "$vk/runtime" && sha256sum --check --strict --quiet libc.prx.sha256)

cc() { PS5_PAYLOAD_SDK="$sdk" sh "$vk/tooling/prospero-clang18" "$@"; }

mkdir -p "$work/obj"

# 1. What libWebKit.so is made of (build.ninja): objects, then libraries.
awk '/^build lib\/libWebKit.so.SOVERSION/{f=1} f&&/^  LINK_LIBRARIES/{print; exit}' "$J/build.ninja" |
    sed 's/^  LINK_LIBRARIES = //' | tr ' ' '\n' | grep -v '^$' > "$work/libs.raw"
grep '\.o$' "$work/libs.raw" | sed "s|^|$J/|" > "$work/objects.txt"
# The WebKit target's own objects: the build line's explicit inputs.
grep -m1 '^build lib/libWebKit.so.SOVERSION:' "$J/build.ninja" |
    sed -e 's/^build [^:]*: [^ ]* //' -e 's/ |.*$//' | tr ' ' '\n' | grep '\.o$' |
    sed "s|^|$J/|" >> "$work/objects.txt"
for part in UIProcess WebProcess GPUProcess NetworkProcess Shared; do
    tr ' ' '\n' < "$J/Source/WebKit/CMakeFiles/WebKit$part.dir/Release/objects.rsp" | grep -v '^$'
done >> "$work/objects.txt"
# The libraries, minus: the rpath, Mesa's libEGL.so.1, the system modules'
# stubs (all of the SDK's are given below), the driver's own flags.
grep -v '\.o$' "$work/libs.raw" |
    grep -v -e '^-Wl,-rpath' -e 'libEGL\.so' -e '^/opt/ps5-payload-sdk/target/lib/.*\.so$' \
            -e '^-pthread$' |
    sed -e 's/^-Wl,--start-group$/--start-group/' -e 's/^-Wl,--end-group$/--end-group/' \
    > "$work/libraries.txt"
echo "$(wc -l < "$work/objects.txt") objects, $(wc -l < "$work/libraries.txt") library arguments"

# 2. The pthread_create chain (see the header).
cp "$sdk/target/lib/libps5platform.a" "$work/libps5platform.a"
"$opt/bin/llvm-objcopy" --redefine-sym __wrap_pthread_create=ps5platform_pthread_create \
    "$work/libps5platform.a"
cp "$B/libc-fixes/libLibcFixes.a" "$work/libLibcFixes.a"
"$opt/bin/llvm-objcopy" --redefine-sym __real_pthread_create=ps5platform_pthread_create \
    --redefine-sym __real_readlink=bps5_readlink "$work/libLibcFixes.a"
sed -i "s|^$B/libc-fixes/libLibcFixes.a\$|$work/libLibcFixes.a|" "$work/libraries.txt"

# 3. Compile the title's own code.
cc -std=c++20 -O2 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
    -c "$here/main.cpp" -o "$work/obj/main.o"
for part in compat posix title_memory; do
    cc -std=c11 -O2 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
        -c "$here/$part.c" -o "$work/obj/$part.o"
done
cc -std=c++20 -O2 -Wall -Wextra -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections \
    -c "$native/app_crt.cpp" -o "$work/obj/app_crt.o"
cc -std=c++20 -O2 -Wall -Wextra -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections \
    -c "$native/app_cpp_runtime.cpp" -o "$work/obj/app_cpp_runtime.o"
printf '!<arch>\n' > "$work/empty.a"
# Mesa (EGL, GLES) is not in this title yet: each function WebKit calls
# (gl-missing.txt: the undefined symbols of a link without them) logs its name once and returns 0.
{
    echo 'int sceKernelDebugOutText(int channel, const char* text);'
    echo 'static void missing(const char* line) { sceKernelDebugOutText(0, line); }'
    while read -r name; do
        [[ -n $name ]] || continue
        echo "long $name(void) { static int once; if (!once++) missing(\"[webkit_title] no Mesa: $name\\n\"); return 0; }"
    done < "$here/gl-missing.txt"
} > "$work/gl_missing.c"
cc -std=c11 -O2 -w -c "$work/gl_missing.c" -o "$work/obj/gl_missing.o"

# 4. Link: PS5_Vulkan's linker script and flags, WebKit's inputs.
# shellcheck source=/dev/null
source "$vk/tools/radv-link.sh"
radv_link_recipe "$vk" "$sdk" "$work/empty.a"
builtins="$("$PS5_CLANG" --print-resource-dir)/lib/linux/libclang_rt.builtins-x86_64.a"
# compat.c: the libc functions a title has no import for.
compat=()
for name in thr_self pipe2 ppoll mkostemp getpwuid getpwnam_r clock_getcpuclockid2 uselocale \
        ___mb_cur_max timegm; do
    compat+=("--defsym=$name=bps5_$name")
done
# The imports only libkernel_sys has, NULL in a title (2026-11-06, docs/05),
# through --wrap (a --defsym of the name would export it); readlink through
# libc-fixes' __real_readlink, above.
for name in fstatfs fchown link symlink; do
    compat+=("--wrap=$name" "--defsym=__wrap_$name=bps5_$name")
done
# Sony's GL (libSceGLSlimVSH) does not load in an app (docs/02, 0x80020063):
# its stub must not stand in for Mesa's EGL and GLES. Nor does
# libScePosixForWebKit load in a title (23:12): posix.c has what WebKit
# takes from it, and with its stub there the title would export those
# names, which the converter refuses.
stubs=()
for stub in "$sdk"/target/lib/*.so; do
    case $stub in
        */libSceGLSlimVSH.so|*/libScePosixForWebKit.so) ;;
        *) stubs+=("$stub") ;;
    esac
done
"$sdk/bin/prospero-lld" "${radv_linker_script[@]}" --eh-frame-hdr "${radv_link_flags[@]}" \
    --wrap=readlink --wrap=mmap --wrap=munmap --wrap=sceKernelReserveVirtualRange \
    -u gst_init_static_plugins --gc-sections \
    --error-limit=0 "${compat[@]}" \
    --version-script "$native/app-symbols.map" --exclude-libs=ALL \
    -e _start -o "$work/llvm-pie.elf" \
    "$work/obj/app_crt.o" "$work/obj/app_cpp_runtime.o" "$work/obj/main.o" "$work/obj/compat.o" \
    "$work/obj/posix.o" "$work/obj/title_memory.o" "$work/obj/gl_missing.o" \
    @"$work/objects.txt" \
    -L "$opt/target/user/homebrew/lib" -L "$opt/target/user/homebrew/lib/gstreamer-1.0" \
    -L "$sdk/target/lib" \
    @"$work/libraries.txt" \
    --start-group "$opt/target/lib/libc++.a" "$opt/target/lib/libc++abi.a" \
    "$opt/target/lib/libunwind.a" "$builtins" "$work/libps5platform.a" --end-group \
    --as-needed "${stubs[@]}" \
    > "$work/link.log" 2>&1 || true
{ grep -v "warning:" "$work/link.log" || true; } | head -50
[[ -s $work/llvm-pie.elf ]] || exit 1
ls -la "$work/llvm-pie.elf"
"$tool" link --in "$work/llvm-pie.elf" --out "$work/eboot.elf" \
    --stub-dir "$sdk/target/lib" --module-sdk 0x02000009 \
    --companion-sdk 0x08050001 --file-name eboot.elf

app=$work/$title_id
rm -rf -- "$app"
mkdir -p "$app/sce_sys" "$app/sce_module"
"$tool" self --sign --in "$work/eboot.elf" --out "$app/eboot.bin" --magic 0x1D3D154F
cp "$here/param.json" "$app/sce_sys/param.json"
cp "$vk/runtime/libc.prx" "$app/sce_module/libc.prx"
cp "$HOME/browserps5-build/title_probe/PPSA97021/sce_sys/icon0.png" "$app/sce_sys/icon0.png"
"$tool" self --inspect --file "$app/eboot.bin" > /dev/null

# param.json last: the title shows up only once the rest is there.
(cd "$work" && tar --format=ustar --owner=0 --group=0 --no-recursion \
    --transform "s,^,data/homebrew/," -cf "$out" \
    "$title_id" "$title_id/sce_module" "$title_id/sce_module/libc.prx" \
    "$title_id/sce_sys" "$title_id/sce_sys/icon0.png" \
    "$title_id/eboot.bin" "$title_id/sce_sys/param.json")
echo "title $title_id: $out ($(stat -c %s "$app/eboot.bin") bytes of eboot.bin)"
