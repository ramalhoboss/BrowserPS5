# Pulls BrowserPS5 RN (PPSA97025) logs: the current session and 3 old ones.
poc=/c/Users/<you>/Desktop/BrowserPS5-WebKit/poc; ps5=192.168.1.50
out=$poc/results/rn-live-$(date +%H%M%S); mkdir -p "$out"; work=$(mktemp -d)
send() { MSYS_NO_PATHCONV=1 powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(cygpath -w "$poc/send.ps1")" \
  -Ps5Host $ps5 -Payload "$(cygpath -w "$poc/$1")" -InputFile "$(cygpath -w "$2")" -TimeoutSec "${3:-120}" 2>&1 | tr -d '\r' | sed 's/^\xef\xbb\xbf//'; }
pull() { echo "$1" > "$work/p.txt"; send read_file/read_file.elf "$work/p.txt" 60 |
  sed -n "\#^\[read_file\] ---- $1\$#,\#^\[read_file\] ---- end of#p" | sed '1d;$d' > "$2"; }
for f in browser-run.txt browser-title.txt web-stderr.txt browser-run.1.txt browser-title.1.txt web-stderr.1.txt browser-run.2.txt browser-title.2.txt browser-run.3.txt browser-title.3.txt; do pull /data/homebrew/PPSA97024/$f "$out/$f"; done

wc -l $out/*; echo "OUT $out"
