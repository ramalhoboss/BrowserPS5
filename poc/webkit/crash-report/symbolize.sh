#!/usr/bin/env bash
#   Copyright (C) 2026 BrowserPS5 contributors
#
# This file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# The stacks crash-report.c writes ("[WebKit] STACK of ..." and "CRASH"),
# with function names, in WSL:
#
#   bash poc/webkit/crash-report/symbolize.sh LOG [PID]
#
# "caller? 0x..." lines are offsets into libWebKit: looked up in the
# unstripped build ($WEBKIT_LIB). "other library? 0x..." lines are
# addresses: inside libEGL.so.1 or libvulkan.so.1 ($MESA_LIBS), whose load
# addresses come from the same log, for PID (by default the pid of the
# first STACK or CRASH line):
#   libEGL     "libEGL's glGetString at <address>" (the compositor's line)
#   libvulkan  "[libvulkan] pid <PID>: ... this library is at <address>"
# The libraries here must be the ones on the console (the md5s are printed).
# After a new WebKit build, WEBKIT_FUNCTIONS=<file> names the function table
# kept for the WebKit on the console (e.g. webkit-guard-cc38c188.functions in
# ~/browserps5-build, for webkit-media-jit-video-guard-runtime.tar).

set -euo pipefail

log=${1:?usage: symbolize.sh LOG [PID]}
build=$HOME/browserps5-build
webkit_lib=${WEBKIT_LIB:-$build/webkit-build-media-jit/lib/libWebKit.so.SOVERSION}
mesa_libs=${MESA_LIBS:-$build/mesa-browser/stage/user/homebrew/lib}
egl_lib=$mesa_libs/libEGL.so.1
vk_lib=$mesa_libs/libvulkan.so.1

pid=${2:-$(grep -m1 -oE '(STACK of .*|CRASH.*)\(pid [0-9]+' "$log" | grep -oE '[0-9]+$' || true)}
[[ -n $pid ]] || { echo "no STACK or CRASH line in $log"; exit 1; }

# A library's size in memory: the end of its last LOAD segment.
span() {  # <library>
    local max=0 end vaddr memsz
    while read -r _ _ vaddr _ _ memsz _; do
        end=$(( vaddr + memsz ))
        (( end > max )) && max=$end
    done < <(readelf -lW "$1" | grep "^ *LOAD")
    echo "$max"
}

egl_at=$(grep -m1 -oE "libEGL's glGetString at [0-9a-fx]+" "$log" | awk '{ print $NF }' || true)
egl_base=0
if [[ -n $egl_at ]]; then
    sym=$(nm -D --defined-only "$egl_lib" | awk '$3 == "glGetString" { print $1 }')
    egl_base=$(( 0x${egl_at#0x} - 0x$sym ))
fi
vk_at=$(grep -m1 -oE "\[libvulkan\] pid $pid: .*this library is at [0-9a-fx]+" "$log" |
        awk '{ print $NF }' || true)
vk_base=0
[[ -n $vk_at ]] && vk_base=$(( 0x${vk_at#0x} ))
egl_end=$(( egl_base + $(span "$egl_lib") ))
vk_end=$(( vk_base + $(span "$vk_lib") ))

printf 'pid %s; libWebKit %s; libEGL %s at 0x%x; libvulkan %s at 0x%x\n' "$pid" \
    "$(md5sum "$webkit_lib" | cut -c1-8)" "$(md5sum "$egl_lib" | cut -c1-8)" "$egl_base" \
    "$(md5sum "$vk_lib" | cut -c1-8)" "$vk_base"

# One name per address: "function+0xoffset", from the library's functions
# in address order (nm), kept next to it in <library>.functions until the
# library changes. Not addr2line: on libWebKit (1.4 GB, clang's DWARF) it
# ran 33 s and said nothing (2026-11-05).
functions() {  # <library>: the cache file's name
    local cache=$1.functions
    if [[ $1 == "$webkit_lib" && -n ${WEBKIT_FUNCTIONS:-} ]]; then
        echo "$WEBKIT_FUNCTIONS"  # a table kept for the WebKit on the console
        return
    fi
    if [[ ! -s $cache || $1 -nt $cache ]]; then
        nm -n -C --defined-only "$1" | awk '$2 ~ /^[tTwWiI]$/' > "$cache.tmp"
        mv "$cache.tmp" "$cache"
    fi
    echo "$cache"
}

name() {  # <library> <offset>
    awk -v want="$2" '
        { v = 0; h = $1
          for (i = 1; i <= length(h); i++) v = v * 16 + index("0123456789abcdef", substr(h, i, 1)) - 1
          if (v > want) exit
          at = v; $1 = ""; $2 = ""; fn = substr($0, 3) }
        END { if (fn != "") printf "%s+0x%x\n", fn, want - at; else print "?" }' "$(functions "$1")"
}

grep -E '^\[WebKit\] (STACK|CRASH)|^\[WebKit\]   (caller\?|other library\?|rip|at 0x)' "$log" |
while IFS= read -r line; do
    case $line in
    *"caller? 0x"*)
        off=$(( $(grep -oE '0x[0-9a-f]+' <<< "$line" | head -1) ))
        echo "$line  $(name "$webkit_lib" "$off")"
        ;;
    *"other library? 0x"*)
        addr=$(( $(grep -oE '0x[0-9a-f]+' <<< "$line" | head -1) ))
        if (( egl_base && addr >= egl_base && addr < egl_end )); then
            echo "$line  libEGL: $(name "$egl_lib" $(( addr - egl_base )))"
        elif (( vk_base && addr >= vk_base && addr < vk_end )); then
            echo "$line  libvulkan: $(name "$vk_lib" $(( addr - vk_base )))"
        else
            echo "$line  (another library)"
        fi
        ;;
    *)
        echo "$line"
        ;;
    esac
done
