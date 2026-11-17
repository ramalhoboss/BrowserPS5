# 2026-11-10 21:00: the worker probe in BrowserPS5 RN (PPSA97025). Starts
# card_test/worker_probe.elf (wprobe.html and w.js on 127.0.0.1:8120),
# installs out/menu-rn-wprobe.tar (SPEED_TEST=1 SPEED_PLAN=worker_plan.h),
# opens the title, waits for it to quit by itself (~130 s), pulls the logs,
# and puts back ${BACK:-menu-rn-probe.tar}. Needs no app open.
#   bash run-wprobe-rn.sh        (Git Bash on the PC)
poc=/c/Users/<you>/Desktop/BrowserPS5-WebKit/poc; ps5=192.168.1.50; ID=PPSA97024
rn=$poc/browser-rn/out
out=$poc/results/rn-wprobe-$(date +%Y%m%d-%H%M%S); mkdir -p "$out"; work=$(mktemp -d)
send() { MSYS_NO_PATHCONV=1 powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(cygpath -w "$poc/send.ps1")" \
  -Ps5Host $ps5 -Payload "$(cygpath -w "$poc/$1")" -InputFile "$(cygpath -w "$2")" -TimeoutSec "${3:-120}" 2>&1 | tr -d '\r' | sed 's/^\xef\xbb\xbf//'; }
pull() { echo "$1" > "$work/p.txt"; send read_file/read_file.elf "$work/p.txt" 60 |
  sed -n "\#^\[read_file\] ---- $1\$#,\#^\[read_file\] ---- end of#p" | sed '1d;$d' > "$2"; }
closed() { echo "$ID check" > "$work/c.txt"; send launch_title/launch_title.elf "$work/c.txt" 60 | grep -q 'PASS check'; }
closed || { echo "an app is open: stop"; exit 1; }
# TITLE=1: out/title-rn.tar first; LIB=name.tar: a libWebKit.so from results/.
[ "${TITLE:-0}" = 1 ] && send install_tar/install_tar.elf "$rn/title-rn.tar" 600 | grep 'PASS\|FAIL'
[ -n "${LIB:-}" ] && send install_tar/install_tar.elf "$poc/results/$LIB" 600 | grep 'PASS\|FAIL'
send install_tar/install_tar.elf "$rn/menu-rn-wprobe.tar" 120 | grep 'PASS\|FAIL'
: > "$work/empty.txt"
send card_test/worker_probe.elf "$work/empty.txt" 300 > "$out/server.txt" &
server=$!
sleep 3
grep "ready\|FAIL" "$out/server.txt"
echo "$ID" > "$work/l.txt"; send launch_title/launch_title.elf "$work/l.txt" 120 | grep launch_title
for i in $(seq 1 30); do sleep 10; [ $i -ge 13 ] && closed && break; done
pull /data/homebrew/$ID/browser-run.txt "$out/browser-run.txt"
pull /data/homebrew/$ID/browser-title.txt "$out/browser-title.txt"
grep "speed\|wprobe" "$out/browser-run.txt" | cut -c1-400
wait $server
grep "card_probe" "$out/server.txt" | cut -c1-400
if closed; then send install_tar/install_tar.elf "$rn/${BACK:-menu-rn-probe.tar}" 120 | grep 'PASS\|FAIL'; echo "${BACK:-menu-rn-probe.tar} back"
else echo "the title is still open: the menu NOT put back"; fi
echo "OUT $out"
