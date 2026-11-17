#!/usr/bin/env bash
#   Copyright (C) 2026 BrowserPS5 contributors
#
# This file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# The console's freezes of 2026-11-05 (docs/08-4k.md, Estado), on the
# console, log only, from Git Bash on the PC, with the browser closed and
# a go-ahead:
#
#   bash poc/browser/run-nav.sh DIR
#
# DIR holds:
#   browser.elf   the navigation test browser (make NAV_TEST=4
#                 RUN_SECONDS=150): the first favorite (the start page's,
#                 then the bar's), then ⌂, four times, 15 s a step
#   *.tar         optional: installed first, in name order (the WebKit with
#                 the spin guard), then 100 s of waiting
#
#   0. the reports of the browser's last run are saved first (the run
#      overwrites them);
#   1. the tars, 100 s of waiting;
#   2. the run: the browser's log in poc/results/nav-<stamp>.log, the
#      title's report in nav-<stamp>-title.log. A stuck WebProcess shows as
#      "[WebKit] spin guard: ..." with the stacks after it.
# The tars stay on the console.

set -uo pipefail

dir=$(cd -- "${1:?usage: run-nav.sh DIR}" && pwd)
ps5=${PS5:-192.168.1.51}
poc=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
stamp=$(date +%Y%m%d-%H%M%S)
work=$(mktemp -d)
trap 'rm -rf -- "${work:?}"' EXIT

say() { printf '[run-nav %s] %s\n' "$(date +%H:%M:%S)" "$*"; }
die() { say "STOP: $*"; exit 1; }

send() {
    local payload=$1 input=${2:-} timeout=${3:-180}
    local args=(-Ps5Host "$ps5" -Payload "$(cygpath -w "$payload")" -TimeoutSec "$timeout")
    [[ -n $input ]] && args+=(-InputFile "$(cygpath -w "$input")")
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(cygpath -w "$poc/send.ps1")" "${args[@]}" 2>&1 |
        tr -d '\r' | sed 's/^\xef\xbb\xbf//'
}

answers() { curl -s -m 5 -o /dev/null "http://$ps5:10101/"; }

nothing_open() {
    echo "PPSA97021 check" > "$work/check.txt"
    local text
    text=$(send "$poc/launch_title/launch_title.elf" "$work/check.txt" 60)
    printf '%s\n' "$text" | grep '\[launch_title\]' | sed 's/^/    /'
    printf '%s\n' "$text" | grep -q '\[launch_title\] PASS check'
}

wait_closed() {
    local i
    for (( i = 0; i < 9; i++ )); do
        nothing_open && return 0
        sleep 20
    done
    return 1
}

read_to() {  # <file on the console> <out>
    echo "$1" > "$work/read.txt"
    send "$poc/read_file/read_file.elf" "$work/read.txt" 60 > "$2"
    say "$(wc -l < "$2") lines of $1 in ${2#"$poc/"}"
}

install() {
    local tar=$1 text
    say "installing $(basename "$tar") ($(md5sum "$tar" | cut -c1-8), $(( $(stat -c %s "$tar") / 1048576 )) MiB)"
    text=$(send "$poc/install_tar/install_tar.elf" "$tar" 600)
    printf '%s\n' "$text" | grep 'PASS\|FAIL' | sed 's/^/    /'
    printf '%s\n' "$text" | grep -q '\[install_tar\] PASS'
}

shopt -s nullglob
tars=("$dir"/*.tar)
[[ -e $dir/browser.elf ]] || die "no browser.elf in $dir"

answers || die "the console does not answer on $ps5:10101"
nothing_open || die "an app is open on the console: nothing done"

# 0. The last run's reports, before anything else.
read_to /data/homebrew/PPSA97020/browser-title.txt "$poc/results/last-run-$stamp-title.log"
read_to /data/homebrew/PPSA97020/browser-run.txt "$poc/results/last-run-$stamp-run.log"

# 1. The tars.
if (( ${#tars[@]} )); then
    for tar in "${tars[@]}"; do
        install "$tar" || die "install_tar did not PASS on $(basename "$tar")"
    done
    say "waiting 100 s after the installs"
    sleep 100
    nothing_open || die "an app was opened while waiting"
fi

# 2. The run.
out=$poc/results/nav-$stamp.log
say "the navigation test browser ($(md5sum "$dir/browser.elf" | cut -c1-8)), ~170 s"
send "$dir/browser.elf" "" 300 > "$out"
grep -E '^\[browser\] (PASS|FAIL)|nav test|spin guard|STACK|CRASH|crashed' "$out" |
    cut -c1-200 | tail -40 | sed 's/^/    /'
grep -E '^\[WebKit\] load pid' "$out" | awk '{ if ($8 + 0 >= 1.5) busy++ } END { printf "    load lines with 1.5 cores or more: %d\n", busy }'
say "browser log: ${out#"$poc/"}"
# Stacks (the spin guard's, or a crash's): with function names, in WSL
# (webkit/crash-report/symbolize.sh; FUNCTIONS= the WebKit's table, if the
# build tree has moved on since the WebKit on the console).
if grep -q '^\[WebKit\] \(STACK of\|CRASH\)' "$out"; then
    wsl_path() { local p; p=$(cygpath -m "$1"); printf '/mnt/%s%s\n' "${p:0:1}" "${p:2}" | sed 's|^/mnt/\(.\)|/mnt/\L\1|'; }
    MSYS_NO_PATHCONV=1 wsl -d Ubuntu-24.04 -e bash -lc \
        "WEBKIT_FUNCTIONS=${FUNCTIONS:-} bash '$(wsl_path "$poc/webkit/crash-report/symbolize.sh")' '$(wsl_path "$out")'" \
        > "${out%.log}-stacks.log" 2>&1
    say "stacks with names: results/$(basename "${out%.log}")-stacks.log" && sed -n '1,40p' "${out%.log}-stacks.log" | cut -c1-200 | sed 's/^/    /'
fi
answers || die "the console stopped answering after the run"
wait_closed || die "the browser is still open 3 min after the run"
read_to /data/homebrew/PPSA97020/browser-title.txt "${out%.log}-title.log"
answers && say "the console answers" || say "the console does not answer"
say "done"
