# 2026-11-11: the morning's tests of BrowserPS5 RN (PPSA97025), one step a
# call, each with no person at the controller (the test builds open what they
# test and quit by themselves; run-feel-rn.sh waits, pulls the logs and the
# screens and puts the normal menu back). The night's work is all untested on
# the console: the steps go from the surest to the least sure.
#
#   bash morning-rn.sh safe     the normal menu and web-env (the console still has
#                               the system keyboard's TEST menu from 01:31), and
#                               the logs of the session that took the console down
#   bash morning-rn.sh tour     the new functions, opened one by one (tour_plan.h), ~3 min
#   bash morning-rn.sh title    the new title (L1, square held) + the feel test, ~5 min
#   bash morning-rn.sh split    the interface in a WebProcess of its own + feel, ~5 min
#   bash morning-rn.sh splittour  the tour with the interface in its own WebProcess (the pages' confirm() window)
#   bash morning-rn.sh plus     the "plus" engine (content rules, dark, variable fonts) + tour
#   bash morning-rn.sh async    the engine with asynchronous scrolling + feel
#   bash morning-rn.sh skia     the Skia engine (tiles painted on several threads) + feel
#   bash morning-rn.sh rnplus   the standard set of before (the plus engine), as the way back
#   bash morning-rn.sh rn = full since 2026-11-11 22:45. Before (16:20): the plus engine with the day's fixes (library with the futex
#                               locks, the databases cache, the log's writes in blocks and the transport
#                               fixes; title with the log in memory), menu-rn-plus.tar, no test markers.
#                               "safe" and "normal" now do this too.
#   bash morning-rn.sh full     the FULL engine's set (plus + Web Audio, WebGL, notifications): on the console
#                               since 2026-11-11 21:13
#   bash morning-rn.sh old      the set of before (v1's engine: title-rn.tar + menu-rn.tar), which has the
#                               stalls; it takes the plus and Skia libraries away
#
# A step that ends with "the title is still open" or no answer: stop, look at
# the TV, tell the user (never close the app from here).
here=/c/Users/<you>/Desktop/BrowserPS5/BrowserPS5-WebKit/poc/browser-rn
poc=/c/Users/<you>/Desktop/BrowserPS5/BrowserPS5-WebKit/poc; ps5=192.168.1.50; ID=PPSA97024
rn=$here/out; work=$(mktemp -d)
send() { MSYS_NO_PATHCONV=1 powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(cygpath -w "$poc/send.ps1")" \
  -Ps5Host $ps5 -Payload "$(cygpath -w "$poc/$1")" -InputFile "$(cygpath -w "$2")" -TimeoutSec "${3:-120}" 2>&1 | tr -d '\r' | sed 's/^\xef\xbb\xbf//'; }
closed() { echo "$ID check" > "$work/c.txt"; send launch_title/launch_title.elf "$work/c.txt" 60 | grep -q 'PASS check'; }
install() { send install_tar/install_tar.elf "$1" "${2:-300}" | grep 'PASS\|FAIL'; }
unmark() { printf '%s\n' "$@" > "$work/r.txt"; send remove_files/remove_files.elf "$work/r.txt" 60 | grep 'PASS\|FAIL\|removed' | head -5; }
need() { for f in "$@"; do [ -f "$f" ] || { echo "missing: $f"; exit 1; }; done; }

case "${1:-}" in
rnplus)  # the standard set until 2026-11-11 22:45: the plus engine (kept as the way back)
  closed || { echo "the console does not answer, or an app is open: stop"; exit 1; }
  need "$rn/lib-rn-plus.tar" "$rn/title-rn-plus.tar" "$rn/menu-rn-plus.tar" "$rn/web-env-normal.tar"
  install "$rn/lib-rn-plus.tar" 600
  install "$rn/title-rn-plus.tar" 600
  install "$rn/menu-rn-plus.tar"; install "$rn/web-env-normal.tar" 60
  # 2026-11-11 17:15: the clean exit (the title ends with LoadExec("exit"), no error box of the system):
  # proven with and without a video played (rn-feel-20261111-171359 and -171458).
  install "$rn/exit-loadexec-marker.tar" 60
  unmark /data/homebrew/$ID/ui-process /data/homebrew/$ID/quit-request /data/homebrew/$ID/dump-on-stall \
         /data/homebrew/$ID/dump-request /user/homebrew/browserps5-rn/data/env.txt
  echo "rn: the standard set is in (the plus engine; menu $(tar -xOf "$rn/menu-rn-plus.tar" data/homebrew/$ID/browser.elf | md5sum | cut -c1-8))"
  ;;
full|rn|safe|normal)  # RN'S STANDARD SET since 2026-11-11 22:45 (tour, YouTube x14, probe: all passed on it)
  # 2026-11-11 21:40: THE FULL ENGINE'S SET (plus + Web Audio, WebGL, the engine's own Notification, media session,
  # MathML; menus built by menus-full-rn.sh, engine by engine-rebuild-rn.sh full). On the console since 21:13:
  # the user, 21:2x: "nao precisas repor nada amigo.. continua sempre a trabalhar".
  closed || { echo "the console does not answer, or an app is open: stop"; exit 1; }
  need "$rn/lib-rn-full.tar" "$rn/title-rn-full.tar" "$rn/menu-rn-full.tar" "$rn/web-env-normal.tar" "$rn/pdfjs-rn.tar"
  install "$rn/lib-rn-full.tar" 600
  install "$rn/title-rn-full.tar" 600
  install "$rn/menu-rn-full.tar"; install "$rn/web-env-normal.tar" 60
  install "$rn/exit-loadexec-marker.tar" 60; install "$rn/pdfjs-rn.tar" 120
  unmark /data/homebrew/$ID/ui-process /data/homebrew/$ID/quit-request /data/homebrew/$ID/dump-on-stall          /data/homebrew/$ID/dump-request /user/homebrew/browserps5-rn/data/env.txt          /data/homebrew/$ID/webgl-on /data/homebrew/$ID/webaudio-on /data/homebrew/$ID/bisect.txt
  echo "full: the full engine's set is in (menu $(tar -xOf "$rn/menu-rn-full.tar" data/homebrew/$ID/browser.elf | md5sum | cut -c1-8))"
  ;;
tour)  # RN's standard set (the plus engine) must be in: 'rn' first if in doubt
  need "$rn/menu-rn-plus-tour.tar"
  bash "$here/run-feel-rn.sh" menu-rn-plus-tour.tar 200
  ;;
feel)  # the four tabs (WhatsApp, Telegram and two more): fluidity and stalls, ~5 min
  need "$rn/menu-rn-plus-feel.tar"
  bash "$here/run-feel-rn.sh" menu-rn-plus-feel.tar 300
  ;;
title|split|splittour|async)
  # 2026-11-11 16:25: steps of the engine of before (v1's library). They put its title or menus in,
  # over RN's standard set (the plus engine): only on purpose, and 'rn' afterwards.
  [ "${OLD:-0}" = 1 ] || { echo "$1 is a step of the old engine: OLD=1 bash morning-rn.sh $1, after 'old', and 'rn' at the end"; exit 1; }
  ;;&
title)
  need "$rn/title-rn.tar" "$rn/menu-rn-feel.tar"
  TITLE=1 bash "$here/run-feel-rn.sh" menu-rn-feel.tar 300
  ;;
split)
  need "$rn/ui-process-marker.tar" "$rn/menu-rn-feel.tar"
  EXTRA_TAR=ui-process-marker.tar bash "$here/run-feel-rn.sh" menu-rn-feel.tar 300
  closed && unmark /data/homebrew/$ID/ui-process
  echo "split: the marker is gone again (install ui-process-marker.tar to keep the split)"
  ;;
splittour)
  need "$rn/ui-process-marker.tar" "$rn/menu-rn-tour.tar"
  EXTRA_TAR=ui-process-marker.tar bash "$here/run-feel-rn.sh" menu-rn-tour.tar 200
  closed && unmark /data/homebrew/$ID/ui-process
  echo "splittour: the marker is gone again"
  ;;
plus)
  need "$rn/title-rn-plus.tar" "$rn/menu-rn-plus-tour.tar" "$rn/menu-rn-plus.tar" "$rn/lib-rn-plus.tar"
  closed || { echo "an app is open: stop"; exit 1; }
  install "$rn/lib-rn-plus.tar" 600
  TITLE_TAR=title-rn-plus.tar BACK=menu-rn-plus.tar bash "$here/run-feel-rn.sh" menu-rn-plus-tour.tar 200
  echo "plus: the plus title, library and menu stay in (RN's standard set)"
  ;;
skia)
  need "$rn/title-rn-skia.tar" "$rn/menu-rn-skia-feel.tar" "$rn/menu-rn-skia.tar" "$rn/lib-rn-skia.tar"
  closed || { echo "an app is open: stop"; exit 1; }
  install "$rn/lib-rn-skia.tar" 600
  TITLE_TAR=title-rn-skia.tar BACK=menu-rn-skia.tar bash "$here/run-feel-rn.sh" menu-rn-skia-feel.tar 300
  echo "skia: the Skia title, library and menu stay in; 'rn' puts RN's standard set back"
  ;;
async)
  need "$rn/title-rn-async.tar" "$rn/menu-rn-feel.tar"
  TITLE_TAR=title-rn-async.tar bash "$here/run-feel-rn.sh" menu-rn-feel.tar 300
  echo "async: the async title stays in; 'rn' puts RN's standard set back"
  ;;
old)
  closed || { echo "the console does not answer, or an app is open: stop"; exit 1; }
  need "$rn/title-rn.tar" "$rn/menu-rn.tar" "$rn/web-env-normal.tar"
  install "$rn/title-rn.tar" 600
  install "$rn/menu-rn.tar"; install "$rn/web-env-normal.tar" 60
  # The plus library has a soname of its own (the usual one, v1's too, was never replaced): it goes.
  unmark /data/homebrew/$ID/ui-process /data/homebrew/$ID/quit-request /user/homebrew/lib/libWebKitPlus.so.SOVERSION /user/homebrew/browserps5-rn/plus          /user/homebrew/lib/libWebKitSkia.so.SOVERSION /user/homebrew/browserps5-rn/skia /user/homebrew/browserps5-rn/engine-env.txt
  echo "old: v1's engine with RN's title and menu (the usual library was never replaced); it has the stalls"
  ;;
*)
  sed -n '1,17p' "$0"
  ;;
esac
