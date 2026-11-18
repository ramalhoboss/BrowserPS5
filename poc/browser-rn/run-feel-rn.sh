# 2026-11-11 00:35: BrowserPS5 RN's "feel" test (browser/feel_plan.h), no user
# needed: installs the menu given (default menu-rn-feel.tar), opens the title,
# waits for it to quit by itself (~240 s), pulls the logs, writes the screens
# (SHOT: lines) as PNGs and prints the numbers. ${BACK:-menu-rn.tar} goes back
# at the end (never the probe build: it broke Telegram, 2026-11-10 23:15).
#   bash run-feel-rn.sh [menu.tar] [seconds to wait, default 300]
#   TITLE=1: installs out/title-rn.tar first.   LIB=name.tar: a libWebKit from results/.
#   TITLE_TAR=name.tar: another title from out/.   EXTRA_TAR=name.tar: one more tar after the menu.
poc=/c/Users/<you>/Desktop/BrowserPS5/BrowserPS5-WebKit/poc; ps5=192.168.1.50; ID=${RN_ID:-PPSA97024}  # the games tab's title again since 2026-11-14 (PPSA97025, the media tab's: 4 GiB of memory in all)
rn=$poc/browser-rn/out; work=$(mktemp -d); menu=${1:-menu-rn-plus-feel.tar}; wait_s=${2:-300}
# 2026-11-11 16:25: RN's standard set is the plus engine: its menus by default (a menu of the old
# engine over the plus title has no pages: BACK=menu-rn.tar and the old title for those).
BACK=${BACK:-menu-rn-plus.tar}
send() { MSYS_NO_PATHCONV=1 powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(cygpath -w "$poc/send.ps1")" \
  -Ps5Host $ps5 -Payload "$(cygpath -w "$poc/$1")" -InputFile "$(cygpath -w "$2")" -TimeoutSec "${3:-120}" 2>&1 | tr -d '\r' | sed 's/^\xef\xbb\xbf//'; }
pull() { echo "$1" > "$work/p.txt"; send read_file/read_file.elf "$work/p.txt" 60 |
  sed -n "\#^\[read_file\] ---- $1\$#,\#^\[read_file\] ---- end of#p" | sed '1d;$d' > "$2"; }
closed() { echo "$ID check" > "$work/c.txt"; send launch_title/launch_title.elf "$work/c.txt" 60 | grep -q 'PASS check'; }
closed || { echo "an app is open: stop"; exit 1; }
out=$poc/results/rn-feel-$(date +%Y%m%d-%H%M%S)-${menu%.tar}; mkdir -p "$out"
[ "${TITLE:-0}" = 1 ] && send install_tar/install_tar.elf "$rn/title-rn.tar" 600 | grep 'PASS\|FAIL'
# TITLE_TAR=name.tar (in out/): that title instead (title-rn-plus.tar, title-rn-async.tar).
[ -n "${TITLE_TAR:-}" ] && send install_tar/install_tar.elf "$rn/$TITLE_TAR" 600 | grep 'PASS\|FAIL'
[ -n "${LIB:-}" ] && send install_tar/install_tar.elf "$poc/results/$LIB" 600 | grep 'PASS\|FAIL'
send install_tar/install_tar.elf "$rn/$menu" 120 | grep -q 'PASS' || { echo "$menu: install failed"; exit 1; }
# EXTRA_TAR=name.tar (in out/): one more tar after the menu, e.g. a web-env.txt for the run.
[ -n "${EXTRA_TAR:-}" ] && send install_tar/install_tar.elf "$rn/$EXTRA_TAR" 60 | grep 'PASS\|FAIL'
# IME=1: the keyboard test's marker (the title closes the system keyboard by itself after 4 s).
[ "${IME:-0}" = 1 ] && send install_tar/install_tar.elf "$rn/ime-test-marker.tar" 60 | grep 'PASS\|FAIL'
echo "$ID" > "$work/l.txt"; send launch_title/launch_title.elf "$work/l.txt" 120 | grep "launch_title\]"
n=$((wait_s / 10)); first=$((n * 2 / 3))
for i in $(seq 1 $n); do sleep 10; [ $i -ge $first ] && closed && break; done
for f in browser-run.txt browser-title.txt web-stderr.txt; do pull /data/homebrew/$ID/$f "$out/$f"; done
python "$poc/browser-rn/shots.py" "$out"
grep -a "speed: \|find: \|reader: \|translate: \|notify\|content rules\|private\|upload: \|ask: \|overlay: \|page watch: \|payload: \|start page: \|STOPPED\|CRASH\|crashed\|not answered\|FAIL\|keyboard\|away\|back:" "$out/browser-run.txt" "$out/browser-title.txt" | grep -v "SHOT:" | sed 's/^[^:]*browser-[a-z]*.txt://' | cut -c1-330
if closed; then send install_tar/install_tar.elf "$rn/${BACK:-menu-rn.tar}" 120 | grep 'PASS\|FAIL'; echo "${BACK:-menu-rn.tar} back"
else echo "the title is still open: the menu NOT put back"; fi
echo "OUT $out"
