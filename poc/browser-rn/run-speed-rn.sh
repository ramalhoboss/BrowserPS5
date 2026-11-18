# 2026-11-10: the speed test (tests-v1/run-speed-v1.sh) on BrowserPS5 RN
# (PPSA97025): WhatsApp Web, Telegram K, YouTube, Público, Wikipedia, log
# only. Needs no app open. Installs out/title-rn.tar when TITLE=1, then
# out/menu-rn-speed.tar, opens the title, waits for the test to quit by
# itself, pulls the logs, puts out/menu-rn.tar back.
#   bash run-speed-rn.sh        (Git Bash on the PC)
poc=/c/Users/<you>/Desktop/BrowserPS5/BrowserPS5-WebKit/poc; ps5=192.168.1.50; ID=PPSA97024
rn=$poc/browser-rn/out
out=$poc/results/rn-speed-$(date +%Y%m%d-%H%M%S); mkdir -p "$out"; work=$(mktemp -d)
send() { MSYS_NO_PATHCONV=1 powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(cygpath -w "$poc/send.ps1")" \
  -Ps5Host $ps5 -Payload "$(cygpath -w "$poc/$1")" -InputFile "$(cygpath -w "$2")" -TimeoutSec "${3:-120}" 2>&1 | tr -d '\r' | sed 's/^\xef\xbb\xbf//'; }
pull() { echo "$1" > "$work/p.txt"; send read_file/read_file.elf "$work/p.txt" 60 |
  sed -n "\#^\[read_file\] ---- $1\$#,\#^\[read_file\] ---- end of#p" | sed '1d;$d' > "$2"; }
closed() { echo "$ID check" > "$work/c.txt"; send launch_title/launch_title.elf "$work/c.txt" 60 | grep -q 'PASS check'; }
closed || { echo "an app is open: stop"; exit 1; }
[ "${TITLE:-0}" = 1 ] && send install_tar/install_tar.elf "$rn/title-rn.tar" 600 | grep "PASS\|FAIL"
send install_tar/install_tar.elf "$rn/menu-rn-speed.tar" 120 | grep 'PASS\|FAIL'
echo "$ID" > "$work/l.txt"; send launch_title/launch_title.elf "$work/l.txt" 120 | grep launch_title
for i in $(seq 1 40); do sleep 10; [ $i -ge ${MIN:-20} ] && closed && break; done
pull /data/homebrew/$ID/browser-run.txt "$out/browser-run.txt"
pull /data/homebrew/$ID/browser-title.txt "$out/browser-title.txt"
grep "speed\|page loaded" "$out/browser-run.txt" | cut -c1-200
grep "memory:\|flexible memory after the title\|CRASH\|crashed" "$out/browser-title.txt" | cut -c1-260 | tail -40
if closed; then send install_tar/install_tar.elf "$rn/menu-rn.tar" 120 | grep 'PASS\|FAIL'; echo "normal RN menu back"
else echo "the title is still open: the normal menu NOT put back"; fi
echo "OUT $out"
