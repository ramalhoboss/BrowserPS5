#!/bin/bash
# The modules' own unit tests (the Makefile's "make test" lines), built as the RN app builds them (-DAPP_RN), in a
# folder of /tmp: the fixes of 2026-11-14 behind APP_RN must not break what these check.
B=/mnt/c/Users/<you>/Desktop/BrowserPS5/BrowserPS5-WebKit/poc/browser
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
cd "$B" || exit 1
ok=0; bad=0
run() {  # name, then the sources and flags
    local name=$1; shift
    if gcc -Wall -O2 -DAPP_RN -o "$W/$name" "$@" 2>"$W/$name.err" && (cd "$W" && "./$name" >"$W/$name.out" 2>&1); then
        ok=$((ok + 1)); echo "ok    $name: $(tail -1 "$W/$name.out" | cut -c1-90)"
    else
        bad=$((bad + 1)); echo "FAIL  $name"; grep -m3 "error" "$W/$name.err"; grep -m6 "FAIL" "$W/$name.out"; tail -2 "$W/$name.out"
    fi
}
run settings_test settings_test.c settings.c
run session_test session_test.c session.c
run address_test address_test.c address.c
run files_test files_test.c files.c
run logins_test logins_test.c logins.c
run phone_test -pthread phone_test.c phone.c files.c
run torrent_test -pthread -DTORRENT_HOST_TEST torrent_test.c torrent.c
run torbox_test -pthread -DTORBOX_HOST_TEST -DTORRENT_HOST_TEST torbox_test.c torbox.c torrent.c
echo "unit tests as RN builds them: $ok passed, $bad failed"
