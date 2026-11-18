#!/usr/bin/env bash
#   Copyright (C) 2026 BrowserPS5 contributors
#
# This file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# The browser's title, PPSA97020 (title.cpp; docs/03-video-e-gpu.md, G1):
#
#   bash build.sh
#
# makes title.tar next to this file, for ../../install_tar (data/homebrew/),
# registered by ShadowMountPlus. It is ../../gpu_broker's owner title plus
# the screen, the controller and the keyboard (hello_page's display.c,
# input.c and ime.c), so it is built the same way: PS5_Vulkan's SDK fork,
# RADV release archive and title tooling (~/browserps5-build/ext/PS5_Vulkan,
# outside this project), and the broker sources ../../gpu_broker/build.sh
# copied out (run that first).
#
# The pages' video is decoded here too (video.c, docs/08-4k.md D3), with
# the FFmpeg that ../../media/build-media.sh builds for the payloads
# (FFMPEG_STAGE). OUT_TAR names the tar (title.tar by default). The title
# also records its screen and sound when the UIProcess asks (capture.c,
# docs/09-video.md).
#
# WEBKIT=1 also links all of WebKit into the title, for the WebProcess to
# run in it (2c-2, docs/05-webprocess-no-titulo.md; web_host.cpp): the
# objects and libraries of libWebKit.so in WEBKIT_BUILD (a finished
# MEDIA=1 JIT=1 IPC_TCP=1 build-webkit.sh), as ../../webkit_title does
# (its compat.c, posix.c, title_memory.c, the pthread_create chain and
# libc++ 20 in place of the SDK fork's 18), Mesa as one object
# (../../gl_zink/title-gl.sh, MESA_TITLE) with title_vulkan.c, and the
# DejaVu fonts with a fonts.conf for /app0.
#
# FLEXIBLE_MIB gives the title that much "flexible" memory through
# param.json's "kernel": {"flexibleMemorySize": <bytes>} (448 MiB without
# it; ../../title_probe proved the format on 2026-11-06 13:51: 1073741824
# gave 1024 MiB). WEBKIT=1 builds ask for 1024 by default: the WebProcess
# had 219 MiB of the 448 left when it started, and 40 after 50 s of pages
# (2c2n). FLEXIBLE_MIB=0 leaves param.json as it is.

set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
poc=$(cd -- "$here/../.." && pwd)
vk=${PS5_VULKAN:-$HOME/browserps5-build/ext/PS5_Vulkan}
gpu=${GPU_BROKER_WORK:-$HOME/browserps5-build/gpu_broker}
work=${WORK:-$HOME/browserps5-build/browser_title}
ff=${FFMPEG_STAGE:-$HOME/browserps5-build/media/stage-ffmpeg/user/homebrew}
out_tar=$(realpath -m -- "${OUT_TAR:-$here/title.tar}")
webkit=${WEBKIT:-0}
if [[ $webkit == 1 ]]; then flexible_mib=${FLEXIBLE_MIB:-1024}; else flexible_mib=${FLEXIBLE_MIB:-0}; fi
[[ $flexible_mib =~ ^[0-9]+$ ]] || { echo "FLEXIBLE_MIB: a number of MiB" >&2; exit 2; }
J=${WEBKIT_BUILD:-$HOME/browserps5-build/webkit-build-media-jit}
mesa_title=${MESA_TITLE:-$HOME/browserps5-build/mesa-browser/title/libEGL-title.o}
fonts=${FONTS:-/usr/share/fonts/truetype/dejavu}
opt=/opt/ps5-payload-sdk
B=$HOME/browserps5-build
sdk=$vk/.deps/native/ps5-payload-sdk
native=$vk/tooling/native
tool=$vk/build/host/ps5-native-tool
release=$vk/.deps/native/radv-release
winsys=$vk/.deps/work/radv-src/src/amd/vulkan/winsys/ps5
broker=$gpu/upstream/tooling/gpu-broker
export PS5_CLANG=${PS5_CLANG:-clang-18}

for file in "$release/lib/libvulkan_radeon.ps5.a" "$sdk/bin/prospero-lld" "$tool" "$vk/runtime/libc.prx" \
        "$winsys/radv_ps5_platform.h" "$broker/server.cpp"; do
    [[ -e $file ]] || { echo "missing $file (build PS5_Vulkan, then ../../gpu_broker/build.sh)" >&2; exit 2; }
done
(cd "$vk/runtime" && sha256sum --check --strict --quiet libc.prx.sha256)

rm -rf -- "$work"
mkdir -p "$work/obj" "$work/stubs"

cc() { PS5_PAYLOAD_SDK="$sdk" sh "$vk/tooling/prospero-clang18" "$@"; }
cxxflags=(-std=c++20 -O2 -Wall -Wextra -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections)
# hello_page's modules print with poc_log.h, whose klog_printf the shim
# declares and title.cpp defines.
cflags=(-std=gnu11 -O2 -Wall -Werror -ffunction-sections -fdata-sections
        -DPOC_NAME='"browser_title"' -I"$here/shim" -I"$here" -I"$poc/hello_page" -I"$poc/common")
# TITLE_EXTRA_CFLAGS: more of them for one title (BrowserPS5 RN: -DINPUT_SQUARE_REPEAT).
read -r -a extra_cflags <<< "${TITLE_EXTRA_CFLAGS:-}"
cflags+=("${extra_cflags[@]}")
# TITLE_SYSIME=1 (2026-11-14, BrowserPS5 RN's titles): the system keyboard's library loaded and bound at run
# time (sysime.c, from Parallax Browser, GPL-3.0-or-later; hello_page/ime.c calls through it).
sysime_objs=()
if [[ ${TITLE_SYSIME:-0} == 1 ]]; then
    cflags+=(-DTITLE_SYSIME)
    sysime_objs=("$work/obj/sysime.o")
fi
# TITLE_MIC=1 (2026-11-15, BrowserPS5 RN): the controller's microphone, a proof for the voice search (mic.c, run
# only with the file mic-probe in the title's folder); libSceAudioIn's three functions are imported through a
# link-only stub (audioin_link_stub.c), as the video decoder's are.
mic_objs=()
mic_stubs=()
mic_stub_flags=()
if [[ ${TITLE_MIC:-0} == 1 ]]; then
    cflags+=(-DTITLE_MIC)
    mic_objs=("$work/obj/mic.o")
    mic_stubs=("$work/stubs/libSceAudioIn.so")
    mic_stub_flags=(--stub "$work/stubs/libSceAudioIn.so")
fi

cc "${cxxflags[@]}" -I"$broker" -I"$winsys" -c "$broker/server.cpp" -o "$work/obj/server.o"
cc "${cxxflags[@]}" -I"$broker" -I"$winsys" -c "$broker/display.cpp" -o "$work/obj/broker_display.o"
# hello_page first: the broker has a display.h of its own.
title_defines=()
(( webkit )) && title_defines=(-DWEBKIT_IN_TITLE)
[[ ${TITLE_SYSIME:-0} == 1 ]] && title_defines+=(-DTITLE_SYSIME)
[[ ${TITLE_MIC:-0} == 1 ]] && title_defines+=(-DTITLE_MIC)
[[ ${TITLE_KBM:-0} == 1 ]] && title_defines+=(-DTITLE_KBM)
[[ " ${TITLE_EXTRA_CFLAGS:-} " == *" -DTITLE_RN "* ]] && title_defines+=(-DTITLE_RN)  # (2026-11-14: title.cpp's RN parts)
[[ $webkit == 1 && ${TITLE_MALLOC:-0} == 1 ]] && title_defines+=(-DTITLE_MALLOC)  # (2026-11-14: malloc on libpas, below)
cc "${cxxflags[@]}" -Werror -I"$poc/hello_page" -I"$broker" -I"$poc/gpu_broker" -I"$here" -I"$poc/common" \
    "${title_defines[@]}" -c "$here/title.cpp" -o "$work/obj/title.o"
cc -std=c11 -O2 -Wall -Wextra -Werror -I"$broker" -I"$poc/gpu_broker" \
    -c "$poc/gpu_broker/packet_io_tcp.c" -o "$work/obj/packet_io_tcp.o"
for module in display input ime; do
    cc "${cflags[@]}" -c "$poc/hello_page/$module.c" -o "$work/obj/$module.o"
done
[[ ${TITLE_SYSIME:-0} == 1 ]] && cc "${cflags[@]}" -c "$here/sysime.c" -o "$work/obj/sysime.o"
# TITLE_KBM=1 (2026-11-18, BrowserPS5 RN): a USB or Bluetooth mouse and keyboard (kbm.c), their system libraries
# loaded and bound at run time as the system keyboard's are.
kbm_objs=()
if [[ ${TITLE_KBM:-0} == 1 ]]; then
    cc "${cflags[@]}" -c "$here/kbm.c" -o "$work/obj/kbm.o"
    kbm_objs=("$work/obj/kbm.o")
fi
if [[ ${TITLE_MIC:-0} == 1 ]]; then
    cc "${cflags[@]}" -c "$here/mic.c" -o "$work/obj/mic.o"
    cc -std=c11 -O2 -fPIC -c "$here/audioin_link_stub.c" -o "$work/obj/libSceAudioIn_stub.o"
    "$sdk/bin/prospero-lld" --shared -soname libSceAudioIn.prx -o "$work/stubs/libSceAudioIn.so" \
        "$work/obj/libSceAudioIn_stub.o"
fi
cc "${cflags[@]}" -c "$here/feel.c" -o "$work/obj/feel.o"
# 2026-11-09: the starting screen (splash.c, as NK's) with PlutoVG (plutovg/, MIT)
# in the colours of the app's icon and background.
cc "${cflags[@]}" -DPLUTOVG_BUILD_STATIC -I"$here/plutovg/include" -I"$here/.."     -c "$here/splash.c" -o "$work/obj/splash.o"
splash_objs=("$work/obj/splash.o")
for src in "$here"/plutovg/source/*.c; do
    cc -std=gnu11 -O2 -ffunction-sections -fdata-sections -DPLUTOVG_BUILD -DPLUTOVG_BUILD_STATIC         -I"$here/plutovg/include" -c "$src" -o "$work/obj/$(basename "$src" .c).o"
    splash_objs+=("$work/obj/$(basename "$src" .c).o")
done
# The conversion and the mix are AVX2 (the PS5's Zen 2 has it).
# 2026-11-09: VIDEO_HWDEC, the pages' video on the PS5's decoder (video_hw.h)
cc "${cflags[@]}" -O3 -mavx2 -DVIDEO_HWDEC -I"$ff/include" -c "$here/video.c" -o "$work/obj/video.o"
cc "${cflags[@]}" -O3 -mavx2 -c "$here/capture.c" -o "$work/obj/capture.o"
cc "${cxxflags[@]}" -c "$native/app_crt.cpp" -o "$work/obj/app_crt.o"
cc "${cxxflags[@]}" -c "$native/app_cpp_runtime.cpp" -o "$work/obj/app_cpp_runtime.o"

stub() {
    cc -std=c11 -O2 -fPIC -c "$vk/$2" -o "$work/obj/$1_stub.o"
    "$sdk/bin/prospero-lld" --shared -soname "$1.prx" -o "$work/stubs/$1.so" "$work/obj/$1_stub.o"
}
stub libSceAgc vendor/ps5/sdk/stubs/agc_canary_link_stub.c
stub libSceAgcDriver vendor/ps5/sdk/stubs/agc_driver_canary_link_stub.c
# 2026-11-09: the PS5's video decoder, imported (a link-only stub, as the NK player)
cc -std=c11 -O2 -fPIC -c "$here/videodec2_link_stub.c" -o "$work/obj/libSceVideodec2_stub.o"
"$sdk/bin/prospero-lld" --shared -soname libSceVideodec2.prx -o "$work/stubs/libSceVideodec2.so" \
    "$work/obj/libSceVideodec2_stub.o"
# 2026-11-09: the PS5's video decoder, imported (a link-only stub, as the NK player)
cc -std=c11 -O2 -fPIC -c "$here/videodec2_link_stub.c" -o "$work/obj/libSceVideodec2_stub.o"
"$sdk/bin/prospero-lld" --shared -soname libSceVideodec2.prx -o "$work/stubs/libSceVideodec2.so" "$work/obj/libSceVideodec2_stub.o"

source "$vk/tools/radv-link.sh"
radv_link_recipe "$vk" "$sdk" "$release/lib/libvulkan_radeon.ps5.a"
title_inputs=()
for input in "${radv_link_inputs[@]}"; do
    [[ $input == --whole-archive || $input == --no-whole-archive ]] || title_inputs+=("$input")
done
# WEBKIT=1: zink reaches RADV through the Vulkan API, whose dispatch tables
# name every entry point by a weak reference, and a weak reference pulls no
# archive member in. 38 entry points were NULL (vk_common_GetPhysicalDevice-
# Properties2, radv_CreateDescriptorPool, vk_common_CmdCopyBuffer...): the
# WebProcess called the first inside wsi_device_init (2026-11-05 23:15,
# docs/05). The recipe links the archive whole for this, but here that
# brings RADV's zlib next to WebKit's (duplicate symbols): the entry points
# are asked for by name instead (radv-entry-points.txt), and the link is
# checked below. The title alone uses RADV's winsys, not its API.
if (( webkit )); then
    while read -r name; do
        [[ -n $name ]] && title_inputs+=("--undefined=$name")
    done < "$here/radv-entry-points.txt"
fi
# The system modules, but libScePosixForWebKit: it does not load in a title
# (poc/webkit_title), and FFmpeg's isatty would take it in (video.c has one).
modules=()
for module in "$sdk"/target/lib/*.so; do
    case $module in
        */libScePosixForWebKit.so) ;;
        # Sony's GL does not load in an app (docs/02): Mesa's object has
        # the EGL and GL functions.
        */libSceGLSlimVSH.so) (( webkit )) || modules+=("$module") ;;
        *) modules+=("$module") ;;
    esac
done

# WEBKIT=1 (see the top): WebKit, Mesa and what they need.
webkit_flags=()
webkit_inputs=()
if (( webkit )); then
    for file in "$J/build.ninja" "$J/lib/libWebKit.so.SOVERSION" "$mesa_title" \
            "$fonts/DejaVuSans.ttf" "$B/libc-fixes/libLibcFixes.a"; do
        [[ -e $file ]] || { echo "missing $file (WEBKIT=1)" >&2; exit 2; }
    done
    wt=$poc/webkit_title
    # TITLE_QUIET_LOG=1 (2026-11-11): the WebProcess's log lines wait in memory
    # (web_host.cpp); BrowserPS5 RN's titles.
    cc -std=c++20 -O2 -Wall -Wextra -Werror -fno-exceptions -fno-rtti -ffunction-sections \
        -fdata-sections ${TITLE_QUIET_LOG:+-DTITLE_QUIET_LOG} -c "$here/web_host.cpp" -o "$work/obj/web_host.o"
    cc -std=c11 -O2 -Wall -Wextra -Werror -I"$release/include" \
        -c "$here/title_vulkan.c" -o "$work/obj/title_vulkan.o"
    cc -std=c11 -O2 -Wall -Wextra -Werror -c "$here/web_compat.c" -o "$work/obj/web_compat.o"
    for part in compat posix title_memory; do
        cc -std=c11 -O2 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
            ${TITLE_MALLOC:+$([[ $TITLE_MALLOC == 1 ]] && echo -DTITLE_MALLOC)} \
            -c "$wt/$part.c" -o "$work/obj/$part.o"
    done
    # video.c has its own isatty (for FFmpeg): that one wins.
    "$opt/bin/llvm-objcopy" --weaken-symbol=isatty "$work/obj/posix.o"
    # What libWebKit.so is made of (its build.ninja), as ../../webkit_title.
    awk '/^build lib\/libWebKit.so.SOVERSION/{f=1} f&&/^  LINK_LIBRARIES/{print; exit}' "$J/build.ninja" |
        sed 's/^  LINK_LIBRARIES = //' | tr ' ' '\n' | grep -v '^$' > "$work/libs.raw"
    grep '\.o$' "$work/libs.raw" | sed "s|^|$J/|" > "$work/objects.txt"
    grep -m1 '^build lib/libWebKit.so.SOVERSION:' "$J/build.ninja" |
        sed -e 's/^build [^:]*: [^ ]* //' -e 's/ |.*$//' | tr ' ' '\n' | grep '\.o$' |
        sed "s|^|$J/|" >> "$work/objects.txt"
    for part in UIProcess WebProcess GPUProcess NetworkProcess Shared; do
        tr ' ' '\n' < "$J/Source/WebKit/CMakeFiles/WebKit$part.dir/Release/objects.rsp" | grep -v '^$'
    done >> "$work/objects.txt"
    grep -v '\.o$' "$work/libs.raw" |
        grep -v -e '^-Wl,-rpath' -e 'libEGL\.so' -e '^/opt/ps5-payload-sdk/target/lib/.*\.so$' \
                -e '^-pthread$' -e '^-lSce' |
        sed -e 's/^-Wl,--start-group$/--start-group/' -e 's/^-Wl,--end-group$/--end-group/' \
            -e "s|^lib/|$J/lib/|" \
        > "$work/libraries.txt"  # lib/: a library of the build itself (libSkia.a with USE_SKIA=ON)
    # The pthread_create chain: WebKit's calls go to libc-fixes' wrapper (64
    # KiB stacks made larger), then to web_host.cpp's (each thread in the
    # WebProcess's log as it is made and as it runs; 2026-11-05 20:40), then
    # to the platform's (stacks in direct memory), then to libc's.
    cp "$sdk/target/lib/libps5platform.a" "$work/libps5platform.a"
    "$opt/bin/llvm-objcopy" --redefine-sym __wrap_pthread_create=ps5platform_pthread_create \
        "$work/libps5platform.a"
    cp "$B/libc-fixes/libLibcFixes.a" "$work/libLibcFixes.a"
    "$opt/bin/llvm-objcopy" --redefine-sym __real_pthread_create=web_host_pthread_create \
        --redefine-sym __real_readlink=bps5_readlink "$work/libLibcFixes.a"
    sed -i "s|^$B/libc-fixes/libLibcFixes.a\$|$work/libLibcFixes.a|" "$work/libraries.txt"
    # libc++ 20 (WebKit's and Mesa's) in place of the fork's 18, and the
    # platform with the chain.
    for i in "${!title_inputs[@]}"; do
        case ${title_inputs[$i]} in
            */libc++.a) title_inputs[$i]=$opt/target/lib/libc++.a ;;
            */libc++abi.a) title_inputs[$i]=$opt/target/lib/libc++abi.a ;;
            */libunwind.a) title_inputs[$i]=$opt/target/lib/libunwind.a ;;
            */libps5platform.a) title_inputs[$i]=$work/libps5platform.a ;;
        esac
    done
    webkit_flags=(--wrap=readlink --wrap=mmap --wrap=munmap --wrap=sceKernelReserveVirtualRange
                  --wrap=browserps5_crash_signals --wrap=exit --wrap=_exit --wrap=select --wrap=pthread_setname_np --wrap=fcntl -u gst_init_static_plugins
                  --error-limit=0)
    # WebKit's IPC over TCP (ipc_transport.c), in title mode once web_host
    # starts the WebProcess.
    for call in socketpair sendmsg recvmsg shm_open shm_unlink dup; do
        webkit_flags+=("--wrap=$call" "-u" "__wrap_$call")
    done
    # TITLE_PIPES=1 (2026-11-18, BrowserPS5 RN's titles): pipe() and pipe2() of the code in the title (GLib's
    # and GStreamer's, in the WebProcess) on libc-fixes' wrappers, as in the library: a pair of sockets, and
    # the pipe only when there is none (pipe2 then goes to compat.c's, through its --defsym below). A pipe's
    # buffer comes from a pool the whole console shares; used up, a write fails and the other end never wakes.
    if [[ ${TITLE_PIPES:-0} == 1 ]]; then
        webkit_flags+=(--wrap=pipe --wrap=pipe2 -u __wrap_pipe -u __wrap_pipe2)
    fi
    # compat.c: the libc functions a title has no import for.
    for name in thr_self pipe2 ppoll mkostemp getpwuid getpwnam_r clock_getcpuclockid2 uselocale \
            ___mb_cur_max timegm; do
        webkit_flags+=("--defsym=$name=bps5_$name")
    done
    # The imports only libkernel_sys has, NULL in a title (2026-11-06 11:47,
    # 2c2l: fontconfig's fstatfs crashed the WebProcess at pc 0): to compat.c's
    # through --wrap, as a --defsym of the name itself would export it (the
    # SDK's libkernel_sys stub defines it) and the converter refuses exports.
    # readlink: libc-fixes' __real_readlink, above.
    for name in fstatfs fchown link symlink; do
        webkit_flags+=("--wrap=$name" "--defsym=__wrap_$name=bps5_$name")
    done
    webkit_inputs=("$work/obj/web_host.o" "$work/obj/title_vulkan.o" "$work/obj/web_compat.o"
                   "$work/obj/compat.o"
                   "$work/obj/posix.o" "$work/obj/title_memory.o" "$mesa_title"
                   @"$work/objects.txt"
                   -L "$opt/target/user/homebrew/lib" -L "$opt/target/user/homebrew/lib/gstreamer-1.0"
                   @"$work/libraries.txt")
    # TITLE_LOCKS=1 (2026-11-11, BrowserPS5 RN's own engines): pthread mutexes,
    # condition variables and rwlocks on futex words (webkit/locks/ps5locks.c,
    # from Parallax Browser, GPL-3.0-or-later). The console's own hand a
    # contended lock over at 4-20 us and convoy as soon as two threads meet;
    # the title has WebKit's WebProcess, Mesa, GStreamer and FFmpeg all in
    # one executable, and every one of their lock calls goes through the
    # wrappers (none is imported unwrapped: checked after the link). Off on
    # the console, for a comparison: the file no-fastlock in the title's
    # folder (the payload processes look for the same file by its real path).
    if [[ ${TITLE_LOCKS:-0} == 1 ]]; then
        lock_functions=(pthread_mutex_init pthread_mutex_destroy pthread_mutex_lock pthread_mutex_trylock
            pthread_mutex_timedlock pthread_mutex_unlock pthread_mutexattr_init pthread_mutexattr_destroy
            pthread_mutexattr_settype pthread_mutexattr_gettype pthread_cond_init pthread_cond_destroy
            pthread_cond_wait pthread_cond_timedwait pthread_cond_signal pthread_cond_broadcast
            pthread_condattr_init pthread_condattr_destroy pthread_condattr_setclock pthread_condattr_getclock
            pthread_rwlock_init pthread_rwlock_destroy pthread_rwlock_rdlock pthread_rwlock_tryrdlock
            pthread_rwlock_timedrdlock pthread_rwlock_wrlock pthread_rwlock_trywrlock
            pthread_rwlock_timedwrlock pthread_rwlock_unlock)
        cc -std=gnu11 -O2 -Wall -Werror -Wno-unused-function -ffunction-sections -fdata-sections \
            -DPS5LOCKS_OFF_FILE='"/app0/no-fastlock"' \
            -c "$poc/webkit/locks/ps5locks.c" -o "$work/obj/ps5locks.o"
        for name in "${lock_functions[@]}"; do
            webkit_flags+=("--wrap=$name")
        done
        webkit_inputs=("$work/obj/ps5locks.o" "${webkit_inputs[@]}")
        echo "TITLE_LOCKS=1: ${#lock_functions[@]} lock functions wrapped (ps5locks.c)"
    fi
    # TITLE_MALLOC=1 (2026-11-14, BrowserPS5 RN's titles): malloc and its family on WebKit's libpas
    # (webkit/malloc/ps5malloc.c, from Parallax Browser, GPL-3.0-or-later). The title's malloc is the
    # console's libc's, whose heap stops at about 256 MiB however much flexible memory is left: GLib,
    # GStreamer (the films' compressed frames), FFmpeg and Mesa filled it on the second film of a session,
    # and GLib ends the process when malloc gives NULL. The names are the ones the title imports (a name
    # no module has would not link). Off on the console: the file no-pasmalloc in the title's folder.
    if [[ ${TITLE_MALLOC:-0} == 1 ]]; then
        malloc_functions=(malloc calloc realloc free posix_memalign strdup strndup vasprintf asprintf)
        cc -std=gnu11 -O2 -Wall -Werror -ffunction-sections -fdata-sections \
            -DPS5MALLOC_OFF_FILE='"/app0/no-pasmalloc"' \
            -c "$poc/webkit/malloc/ps5malloc.c" -o "$work/obj/ps5malloc.o"
        for name in "${malloc_functions[@]}"; do
            webkit_flags+=("--wrap=$name")
        done
        webkit_inputs=("$work/obj/ps5malloc.o" "${webkit_inputs[@]}")
        echo "TITLE_MALLOC=1: ${#malloc_functions[@]} functions of malloc's family on libpas (ps5malloc.c)"
    fi
    echo "WEBKIT=1: $(wc -l < "$work/objects.txt") WebKit objects, $(wc -l < "$work/libraries.txt") library arguments"
fi

"$sdk/bin/prospero-lld" "${radv_linker_script[@]}" --eh-frame-hdr "${radv_link_flags[@]}" \
    "${webkit_flags[@]}" \
    --version-script "$native/app-symbols.map" --exclude-libs=ALL --gc-sections \
    -e _start -o "$work/llvm-pie.elf" \
    "$work/obj/app_crt.o" "$work/obj/app_cpp_runtime.o" "$work/obj/title.o" "$work/obj/server.o" \
    "$work/obj/broker_display.o" "$work/obj/packet_io_tcp.o" \
    "$work/obj/display.o" "$work/obj/input.o" "$work/obj/ime.o" "${sysime_objs[@]}" "${mic_objs[@]}" "${kbm_objs[@]}" "$work/obj/feel.o" \
    "$work/obj/video.o" "$work/obj/capture.o" "${splash_objs[@]}" "${webkit_inputs[@]}" \
    "$ff/lib/libavcodec.a" "$ff/lib/libswresample.a" "$ff/lib/libavutil.a" \
    "$work/stubs/libSceAgc.so" "$work/stubs/libSceAgcDriver.so" "$work/stubs/libSceVideodec2.so" "${mic_stubs[@]}" \
    "${title_inputs[@]}" \
    --as-needed "${modules[@]}" > "$work/link.log" 2>&1 || true
{ grep -v "warning:" "$work/link.log" || true; } | head -60
[[ -s $work/llvm-pie.elf ]] || exit 1
if [[ ${TITLE_LOCKS:-0} == 1 ]]; then
    READELF=$(command -v llvm-readelf || ls /usr/bin/llvm-readelf-* | tail -1) \
        bash "$poc/webkit/locks/check-lock-imports.sh" "$work/llvm-pie.elf" "${lock_functions[@]}" || exit 1
fi
if (( webkit )); then
    # No RADV, Vulkan-runtime or WSI entry point left NULL: a name the title
    # leaves undefined (weak) that RADV's archive defines.
    left=$(comm -12 \
        <("$sdk/bin/llvm-nm" "$work/llvm-pie.elf" | awk '$1 == "U" || $1 == "w" || $1 == "v" {print $2}' | sort -u) \
        <("$sdk/bin/llvm-nm" --defined-only "$release/lib/libvulkan_radeon.ps5.a" 2>/dev/null |
          awk '$2 ~ /^[TDRBV]$/ {print $3}' | sort -u) | grep -E '^(radv|vk_common|wsi)_' || true)
    if [[ -n $left ]]; then
        echo "RADV entry points left NULL (add them to radv-entry-points.txt):" $left >&2
        exit 1
    fi
    echo "RADV entry points: none left NULL ($(grep -c . "$here/radv-entry-points.txt") asked for by name)"
fi
"$tool" link --in "$work/llvm-pie.elf" --out "$work/eboot.elf" \
    --stub-dir "$sdk/target/lib" --stub "$work/stubs/libSceAgc.so" \
    --stub "$work/stubs/libSceAgcDriver.so" --stub "$work/stubs/libSceVideodec2.so" "${mic_stub_flags[@]}" --module-sdk 0x02000009 \
    --companion-sdk 0x08050001 --file-name eboot.elf
if [[ ${TITLE_SCRUB:-0} == 1 ]]; then  # (2026-11-14, RN's releases) the build machine's user names out of the title, before it is signed: common/scrub-paths.py
    python3 -I "$poc/common/scrub-paths.py" "$work/eboot.elf"
fi

title_id=$(python3 -I -c 'import json,sys; print(json.load(open(sys.argv[1]))["titleId"])' "$here/param.json")
app=$work/$title_id
mkdir -p "$app/sce_sys" "$app/sce_module"
"$tool" self --sign --in "$work/eboot.elf" --out "$app/eboot.bin" --magic 0x1D3D154F
if (( flexible_mib )); then
    python3 -I - "$here/param.json" "$app/sce_sys/param.json" "$flexible_mib" <<'EOF'
import json, sys
param = json.load(open(sys.argv[1]))
param["kernel"] = {"flexibleMemorySize": int(sys.argv[3]) << 20}
with open(sys.argv[2], "w") as f:
    json.dump(param, f, indent=2, sort_keys=True)
    f.write("\n")
EOF
    echo "param.json: flexibleMemorySize $flexible_mib MiB"
else
    cp "$here/param.json" "$app/sce_sys/param.json"
fi
cp "$vk/runtime/libc.prx" "$app/sce_module/libc.prx"
# 2026-11-09: the user's icon and background ("BrowserPS5 WK", art/: icon0.png
# 512x512, pic0.png 1920x1080, and the home screen's pic0/pic1.dds, BC7
# 3840x2160 made by ../../tests-v1/bc7dds.c); without art/, a plain icon.
art_entries=()
if [[ -f $here/art/icon0.png ]]; then
    for file in icon0.png pic0.png pic0.dds pic1.dds pic2.dds; do  # pic2.dds: the PS5 titles' home background (PPSA02154, PPSA02343, MOUU12023)
        if [[ -f $here/art/$file ]]; then
            cp "$here/art/$file" "$app/sce_sys/$file"
            [[ $file == icon0.png ]] || art_entries+=("$title_id/sce_sys/$file")
        fi
    done
else
# A plain icon: dark blue.
python3 -I - "$app/sce_sys/icon0.png" <<'EOF'
import struct, sys, zlib
w = h = 512
row = b"\x00" + bytes((0x10, 0x30, 0x70, 0xff)) * w
def chunk(kind, data):
    body = kind + data
    return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))
png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
       chunk(b"IDAT", zlib.compress(row * h, 9)) + chunk(b"IEND", b""))
open(sys.argv[1], "wb").write(png)
EOF
fi
"$tool" self --inspect --file "$app/eboot.bin" > /dev/null

# WEBKIT=1: the fonts, in /app0/fonts (the title cannot see /user).
font_entries=()
if (( webkit )); then
    mkdir -p "$app/fonts"
    cp "$fonts"/DejaVu*.ttf "$app/fonts/"
    cp /usr/share/doc/fonts-dejavu-core/copyright "$app/fonts/LICENSE"
    cat > "$app/fonts/fonts.conf" <<'EOF'
<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "urn:fontconfig:fonts.dtd">
<!-- The WebProcess in the browser title (web_host.cpp, FONTCONFIG_FILE):
     ../../webkit/runtime/fonts.conf with the title's paths. -->
<fontconfig>
  <dir>/app0/fonts</dir>
  <dir>/preinst/common/font</dir>
  <cachedir>/app0/web/fontconfig</cachedir>
  <alias><family>sans-serif</family><prefer><family>DejaVu Sans</family></prefer></alias>
  <alias><family>serif</family><prefer><family>DejaVu Serif</family></prefer></alias>
  <alias><family>monospace</family><prefer><family>DejaVu Sans Mono</family></prefer></alias>
</fontconfig>
EOF
    font_entries=("$title_id/fonts")
    for file in "$app/fonts"/*; do
        font_entries+=("$title_id/fonts/$(basename "$file")")
    done
fi

# ustar with param.json last (ShadowMountPlus registers a title once it is
# complete), file entries only after the directories.
(cd "$work" && tar --format=ustar --owner=0 --group=0 --no-recursion \
    --transform "s,^,data/homebrew/," -cf "$out_tar" \
    "$title_id" "$title_id/sce_module" "$title_id/sce_module/libc.prx" \
    "$title_id/sce_sys" "$title_id/sce_sys/icon0.png" "${art_entries[@]}" "${font_entries[@]}" \
    "$title_id/eboot.bin" "$title_id/sce_sys/param.json")
echo "title $title_id: $out_tar ($(stat -c %s "$app/eboot.bin") bytes of eboot.bin)"
