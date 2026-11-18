# 2026-11-10 21:40: WhatsApp Web's probe in BrowserPS5 RN (PPSA97025), no user
# needed (the login is in RN's data). For each menu given (default
# menu-rn-wa.tar = browser/wa_plan.h, then menu-rn-wa-moves.tar = the page in a
# process of its own): install, open the title, wait for it to quit by itself
# (~190 s), pull the logs. Then ${BACK:-menu-rn-probe.tar} goes back.
#   bash run-wa-rn.sh [menu.tar ...]        (Git Bash on the PC)
poc=/c/Users/<you>/Desktop/BrowserPS5/BrowserPS5-WebKit/poc; ps5=192.168.1.52; ID=PPSA97024
rn=$poc/browser-rn/out; work=$(mktemp -d)
send() { MSYS_NO_PATHCONV=1 powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(cygpath -w "$poc/send.ps1")" \
  -Ps5Host $ps5 -Payload "$(cygpath -w "$poc/$1")" -InputFile "$(cygpath -w "$2")" -TimeoutSec "${3:-120}" 2>&1 | tr -d '\r' | sed 's/^\xef\xbb\xbf//'; }
pull() { echo "$1" > "$work/p.txt"; send read_file/read_file.elf "$work/p.txt" 60 |
  sed -n "\#^\[read_file\] ---- $1\$#,\#^\[read_file\] ---- end of#p" | sed '1d;$d' > "$2"; }
closed() { echo "$ID check" > "$work/c.txt"; send launch_title/launch_title.elf "$work/c.txt" 60 | grep -q 'PASS check'; }
[ $# -gt 0 ] || set -- menu-rn-wa.tar menu-rn-wa-moves.tar
for menu in "$@"; do
  closed || { echo "an app is open: stop"; exit 1; }
  out=$poc/results/rn-wa-$(date +%H%M%S)-${menu%.tar}; mkdir -p "$out"
  send install_tar/install_tar.elf "$rn/$menu" 120 | grep -q 'PASS' || { echo "$menu: install failed"; exit 1; }
  echo "$ID" > "$work/l.txt"; send launch_title/launch_title.elf "$work/l.txt" 120 | grep launch_title
  for i in $(seq 1 40); do sleep 10; [ $i -ge 20 ] && closed && break; done
  pull /data/homebrew/$ID/browser-run.txt "$out/browser-run.txt"
  pull /data/homebrew/$ID/browser-title.txt "$out/browser-title.txt"
  pull /data/homebrew/$ID/web-stderr.txt "$out/web-stderr.txt"
  grep "^SHOT:" "$out/browser-run.txt" > /dev/null && echo "$menu: shots in the log"
  grep "speed: wa\|CRASH\|crashed" "$out/browser-run.txt" "$out/browser-title.txt" | cut -c1-900
  echo "OUT $out"
done
if closed; then send install_tar/install_tar.elf "$rn/${BACK:-menu-rn-probe.tar}" 120 | grep 'PASS\|FAIL'; echo "${BACK:-menu-rn-probe.tar} back"
else echo "the title is still open: the menu NOT put back"; fi
