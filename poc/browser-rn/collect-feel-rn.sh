# 2026-11-11: the second half of run-feel-rn.sh by itself, for a run whose
# script stopped: pulls RN's logs into results/rn-feel-<time>-<name>, writes
# the screens as PNGs, prints the numbers and puts ${BACK:-menu-rn.tar} back
# if no app is open.   bash collect-feel-rn.sh [name]
poc=/c/Users/<you>/Desktop/BrowserPS5/BrowserPS5-WebKit/poc; ps5=192.168.1.52; ID=PPSA97024
rn=$poc/browser-rn/out; work=$(mktemp -d)
send() { MSYS_NO_PATHCONV=1 powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(cygpath -w "$poc/send.ps1")" \
  -Ps5Host $ps5 -Payload "$(cygpath -w "$poc/$1")" -InputFile "$(cygpath -w "$2")" -TimeoutSec "${3:-120}" 2>&1 | tr -d '\r' | sed 's/^\xef\xbb\xbf//'; }
pull() { echo "$1" > "$work/p.txt"; send read_file/read_file.elf "$work/p.txt" 60 |
  sed -n "\#^\[read_file\] ---- $1\$#,\#^\[read_file\] ---- end of#p" | sed '1d;$d' > "$2"; }
closed() { echo "$ID check" > "$work/c.txt"; send launch_title/launch_title.elf "$work/c.txt" 60 | grep -q 'PASS check'; }
out=$poc/results/rn-feel-$(date +%Y%m%d-%H%M%S)-${1:-collected}; mkdir -p "$out"
for f in browser-run.txt browser-title.txt web-stderr.txt; do pull /data/homebrew/$ID/$f "$out/$f"; done
python "$poc/browser-rn/shots.py" "$out"
grep -a "speed: \|STOPPED\|CRASH\|crashed\|not answered\|FAIL\|keyboard" "$out/browser-run.txt" "$out/browser-title.txt" | grep -v "SHOT:" | sed 's/^[^:]*browser-[a-z]*.txt://' | cut -c1-330
if closed; then send install_tar/install_tar.elf "$rn/${BACK:-menu-rn.tar}" 120 | grep 'PASS\|FAIL'; echo "${BACK:-menu-rn.tar} back"
else echo "the title is still open: the menu NOT put back"; fi
echo "OUT $out"
