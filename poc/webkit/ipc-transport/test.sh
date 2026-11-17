#!/bin/bash
# test.sh: the transport's PC tests (Linux or WSL, the host's cc): ipc_transport_test.c (a connection the way
# WebKit's ConnectionUnix uses it, shared memory, a second pair, the reaper) and ipc_reap_race_test.c (2026-11-17:
# no name taken while a descriptor is on it). Prints PASS or FAIL for each; exit 1 if one failed.
#   bash poc/webkit/ipc-transport/test.sh [rounds of the race test, 3 by default]
cd "$(dirname "$0")" || exit 1
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
wrap="-Wl,--wrap=shm_open -Wl,--wrap=shm_unlink -Wl,--wrap=dup -Wl,--wrap=socketpair -Wl,--wrap=sendmsg"
wrap+=" -Wl,--wrap=recvmsg"
cc -O2 -Wall -pthread ipc_transport_test.c ipc_transport.c $wrap -o "$out/transport_test" || exit 1
cc -O2 -Wall -Werror -pthread ipc_reap_race_test.c ipc_transport.c $wrap -o "$out/reap_race" || exit 1
failed=0
if "$out/transport_test" 2>/dev/null | tail -1 | grep -q "all passed"; then
    echo "PASS ipc_transport_test"
else
    echo "FAIL ipc_transport_test"
    failed=1
fi
for k in $(seq "${1:-3}"); do
    if "$out/reap_race" 2>/dev/null | tail -1 | grep -q "^PASS"; then
        echo "PASS ipc_reap_race_test ($k)"
    else
        echo "FAIL ipc_reap_race_test ($k)"
        failed=1
    fi
done
exit $failed
