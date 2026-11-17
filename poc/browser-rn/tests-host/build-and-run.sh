#!/bin/bash
# In WSL: bash build-and-run.sh [rounds of the map test] [scenarios of the simulation] [first seed] [v]   (2026-11-14)
# The browser's own code that needs no console, cut out of main.cpp AS IT IS and run on the PC with
# AddressSanitizer and UndefinedBehaviorSanitizer:
#   make-map-test.py     the maps of unfinished downloads, and the text put into the page of a site that did not load
#   make-names-test.py   the names given to what is saved, the hosts read out of addresses, the console's language
#   make-asks-test.py    what the start page may ask the app to fetch or write for etaHEN (cheat files, patch files, switches)
#   files-fuzz.c         the files read at the start (settings, history, tabs, logins, extensions), broken
#   inputs-fuzz.c        what comes from outside: a .zip, the paths and headers the phone asks with, what is typed
#   make-multi-sim.py    the downloads' engine against a pretend network: several parts, a failing line, pauses, sessions
# With "v" as the fourth argument: one scenario (the seed given), with the browser's own log lines.
set -euo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
FLAGS="-std=c++17 -g -O1 -Wall -Wno-unused-function -Wno-format-truncation -Wno-unused-variable -fsanitize=address,undefined -fno-sanitize-recover=undefined"
if [[ "${1:-20000}" != 0 ]]; then
    python3 "$HERE/make-map-test.py" "$W/map_test.cpp"
    g++ $FLAGS -o "$W/map_test" "$W/map_test.cpp"
    mkdir -p "$W/partial"
    "$W/map_test" "$W/partial" "${1:-20000}"
fi
if [[ "${1:-20000}" != 0 ]]; then
    python3 "$HERE/make-names-test.py" "$W/names_test.cpp"
    g++ $FLAGS -o "$W/names_test" "$W/names_test.cpp"
    mkdir -p "$W/names"
    "$W/names_test" "$W/names" "$(( ${1:-20000} * 10 ))"
fi
if [[ "${1:-20000}" != 0 ]]; then
    python3 "$HERE/make-asks-test.py" "$W/asks_test.cpp"
    g++ $FLAGS -Wno-maybe-uninitialized -o "$W/asks_test" "$W/asks_test.cpp"
    mkdir -p "$W/asks/inner"  # (inside a folder of its own: anything made beside it is seen)
    "$W/asks_test" "$W/asks/inner" "$(( ${1:-20000} * 10 ))"
fi
if [[ "${1:-20000}" != 0 ]]; then
    B="$HERE/../../browser"
    gcc -DAPP_RN -g -O1 -Wall -Wno-unused-function -fsanitize=address,undefined -fno-sanitize-recover=undefined -I"$B" -I"$HERE" -o "$W/files_fuzz" "$HERE/files-fuzz.c" "$B/settings.c" "$B/session.c" "$B/logins.c"
    mkdir -p "$W/files"
    "$W/files_fuzz" "$W/files" "${1:-20000}"
    # what comes from outside: a .zip, the phone's paths and headers, what is typed. Twice: with the sanitizers, and
    # with glibc's own checks of every place's size (they see a size told wrong, which the sanitizers do not).
    SDK=${PS5_PAYLOAD_SDK:-/opt/ps5-payload-sdk}
    LIBZ=$( (ls /usr/lib/x86_64-linux-gnu/libz.so /usr/lib/x86_64-linux-gnu/libz.so.1 2>/dev/null || true) | head -1)
    if [[ -f "$SDK/target/user/homebrew/include/zlib.h" && -n "$LIBZ" ]]; then
        IN="-I$B -I$HERE -idirafter $SDK/target/user/homebrew/include $HERE/inputs-fuzz.c $B/unzip.c $B/files.c $B/address.c $LIBZ"
        gcc -g -O1 -Wall -Wno-unused-function -Wno-format-truncation -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 \
            -fsanitize=address,undefined -fno-sanitize-recover=undefined -o "$W/inputs_fuzz" $IN
        gcc -g -O1 -w -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3 -o "$W/inputs_fuzz_checked" $IN
        mkdir -p "$W/zips" "$W/work" "$W/work2"
        python3 -W ignore "$HERE/make-zips.py" "$W/zips" >/dev/null
        "$W/inputs_fuzz" "$W/zips" "$W/work" "$(( ${1:-20000} / 2 ))"
        "$W/inputs_fuzz_checked" "$W/zips" "$W/work2" "$(( ${1:-20000} / 2 ))"
    else
        echo "SKIPPED: inputs-fuzz.c (no zlib.h under $SDK, or no libz)"
    fi
    # the phone's server, asked by someone who is not the phone (the same two builds)
    PH="-pthread -I$B -I$HERE $HERE/phone-fuzz.c $B/phone.c $B/files.c"
    gcc -g -O1 -Wall -Wno-unused-function -Wno-format-truncation -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 \
        -fsanitize=address,undefined -fno-sanitize-recover=undefined -o "$W/phone_fuzz" $PH
    gcc -g -O1 -w -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3 -o "$W/phone_fuzz_checked" $PH
    mkdir -p "$W/phone" "$W/phone2"
    "$W/phone_fuzz" "$W/phone" "$(( ${1:-20000} / 4 ))"
    "$W/phone_fuzz_checked" "$W/phone2" "$(( ${1:-20000} / 4 ))"
    # the answers of services (the torrents' daemon, TorBox), read; what is written for them from a page's link
    RP="-pthread -DAPP_RN -DTORRENT_HOST_TEST -I$B -I$HERE $HERE/rpc-fuzz.c $B/torrent.c"
    gcc -g -O1 -Wall -Wno-unused-function -Wno-format-truncation -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 \
        -fsanitize=address,undefined -fno-sanitize-recover=undefined -o "$W/rpc_fuzz" $RP
    gcc -g -O1 -w -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3 -o "$W/rpc_fuzz_checked" $RP
    "$W/rpc_fuzz" "$(( ${1:-20000} * 4 ))"
    "$W/rpc_fuzz_checked" "$(( ${1:-20000} * 4 ))"
fi
python3 "$HERE/make-multi-sim.py" "$W/multi_sim.cpp"
g++ $FLAGS -o "$W/multi_sim" "$W/multi_sim.cpp" 2>&1 | grep -E "error|Error" -A4 | head -60 || true
[[ -x "$W/multi_sim" ]] || { echo "the simulation did not compile"; exit 1; }
mkdir -p "$W/sim"
# The fifth argument "duo": the scenarios of two downloads at once (alone, as with "v": bash build-and-run.sh 0 1 SEED v duo).
if [[ -n "${5:-}" || "${4:-}" == v ]]; then
    "$W/multi_sim" "$W/sim" "${2:-2000}" "${3:-1}" "${4:-q}" ${5:-}
else
    "$W/multi_sim" "$W/sim" "${2:-2000}" "${3:-1}" q
    mkdir -p "$W/duo"
    "$W/multi_sim" "$W/duo" "$(( (${2:-2000} + 1) / 2 ))" "${3:-1}" q duo
fi
