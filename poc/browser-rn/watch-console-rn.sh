# 2026-11-11 01:35: after the system keyboard's test took the console down with
# the test menu (menu-rn-ime.tar) still installed: waits for the console's
# payload loader (port 9021) to answer again, then at once puts the normal,
# safe menu back (out/menu-rn-safe.tar: no system keyboard) and pulls RN's
# logs (the crashed session's are the newest, or .1 if RN was opened since).
#   bash watch-console-rn.sh        (runs until the console is back)
poc=/c/Users/<you>/Desktop/BrowserPS5-WebKit/poc; ps5=192.168.1.50; ID=PPSA97024
rn=$poc/browser-rn/out
send() { MSYS_NO_PATHCONV=1 powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(cygpath -w "$poc/send.ps1")" \
  -Ps5Host $ps5 -Payload "$(cygpath -w "$poc/$1")" -InputFile "$(cygpath -w "$2")" -TimeoutSec "${3:-120}" 2>&1 | tr -d '\r' | sed 's/^\xef\xbb\xbf//'; }
echo "waiting for $ps5:9021 since $(date +%H:%M:%S)"
until timeout 3 bash -c "echo > /dev/tcp/$ps5/9021" 2>/dev/null; do sleep 5; done
echo "the loader answers at $(date +%H:%M:%S)"
for try in 1 2 3 4 5 6; do
  r=$(send install_tar/install_tar.elf "$rn/menu-rn-safe.tar" 120 | grep 'PASS\|FAIL')
  echo "menu-rn-safe.tar: $r"
  echo "$r" | grep -q PASS && break
  sleep 5
done
r=$(send install_tar/install_tar.elf "$rn/web-env-normal.tar" 60 | grep 'PASS\|FAIL'); echo "web-env-normal.tar: $r"
bash "$poc/browser-rn/pull-logs-rn.sh" 2>&1 | tail -3
echo "DONE at $(date +%H:%M:%S)"
