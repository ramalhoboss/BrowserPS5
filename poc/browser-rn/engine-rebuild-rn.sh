#!/bin/bash
# In WSL: engine_rebuild.sh <full|plus>...: that engine built again from the tree as it is, with its soname and its
# locks, its library packed, its title's base built and the title packed (no menu is touched).
W=$HOME/browserps5-build; R=$(cd "$(dirname "$0")" && pwd)
for e in "$@"; do
  case $e in full) N=Full;; plus) N=Plus;; *) echo "which engine?"; exit 1;; esac
  log=$W/build-$e-again.log
  (env -u CC ninja -C $W/webkit-build-$e -k 0 WebKit WebProcess NetworkProcess > $log 2>&1 || true)
  echo "== $e: $(tail -1 $log | cut -c1-90); failed: $(grep -c '^FAILED' $log)"
  grep " error: \| error:\|undefined symbol" $log | cut -c1-220 | sort | uniq -c | sort -rn | head -12
  [ "$(grep -c '^FAILED' $log)" = 0 ] || exit 1
  cd $R && bash plus-soname.sh webkit-build-$e $N | tail -1 && bash plus-locks.sh webkit-build-$e | tail -1 && bash pack-lib-rn.sh webkit-build-$e lib-rn-$e.tar 2>/dev/null | head -1
  [ -n "${LIB_ONLY:-}" ] && continue  # LIB_ONLY=1: the library alone (a change in the network process: the title has none of it in use)
  TITLE_QUIET_LOG=1 TITLE_LOCKS=1 WEBKIT_BUILD=$W/webkit-build-$e TITLE_WORK=$W/browser_title_rn_$e TITLE_BASE_OUT=title-rn-$e-base.tar bash build-title-rn.sh 2>&1 | tail -3
  grep "undefined symbol\|error:" /tmp/build-title-rn-$e-base.log 2>/dev/null | sed "s/>>> .*//" | cut -c1-200 | sort | uniq -c | sort -rn | head -10
  # the title's package: build-rn.sh's "title" step, into a menu name of its own so that no menu in out/ changes
  [ $e = full ] && X="ENGINE_PLUS=1 ENGINE_DIR=full" || X="ENGINE_PLUS=1"
  MENU_NAME=menu-rn-$e-scratch.tar EXTRA_MAKE="$X" RN_WEBKIT_BUILD=$W/webkit-build-$e TITLE_BASE=title-rn-$e-base.tar TITLE_OUT=title-rn-$e.tar bash build-rn.sh title 2>&1 | grep -v "warning: unused" | tail -3
  rm -f $R/out/menu-rn-$e-scratch.tar
done
