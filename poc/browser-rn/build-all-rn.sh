# 2026-11-11: every menu of BrowserPS5 RN from the sources as they are now,
# one after the other (each is the same main.cpp with other switches, so a
# change must go into all of them before the console sees any): the normal
# one last, so the WSL copy is left as the normal build.
#   wsl -e bash build-all-rn.sh
# Out (browser-rn/out): menu-rn-tour.tar, menu-rn-feel.tar, the "plus"
# engine's menu-rn-plus.tar and menu-rn-plus-tour.tar, the Skia engine's
# menu-rn-skia.tar and menu-rn-skia-feel.tar (when those engines are built),
# and menu-rn.tar. The titles and the libraries are not touched.
set -e
here=$(cd "$(dirname "$0")" && pwd)
one() {  # <menu name> <make switches> [engine build dir]
    if [ -n "${3:-}" ] && [ ! -f "$HOME/browserps5-build/$3/lib/libWebKit.so.SOVERSION" ]; then
        echo "$1: skipped ($3 is not built)"; return 0
    fi
    MENU_NAME=$1 EXTRA_MAKE="$2" RN_WEBKIT_BUILD=${3:+$HOME/browserps5-build/$3} bash "$here/build-rn.sh" 2>&1 | tail -3
    test -f "$here/out/$1"
}
one menu-rn-tour.tar      "SPEED_TEST=1 SPEED_PLAN=tour_plan.h"
one menu-rn-feel.tar      "SPEED_TEST=1 SPEED_PLAN=feel_plan.h"
one menu-rn-plus.tar      "ENGINE_PLUS=1" webkit-build-plus
one menu-rn-plus-tour.tar "ENGINE_PLUS=1 SPEED_TEST=1 SPEED_PLAN=tour_plan.h" webkit-build-plus
one menu-rn-plus-feel.tar "ENGINE_PLUS=1 SPEED_TEST=1 SPEED_PLAN=feel_plan.h" webkit-build-plus
one menu-rn-plus-local.tar "ENGINE_PLUS=1 SPEED_TEST=1 SPEED_PLAN=local_plan.h" webkit-build-plus
one menu-rn-plus-dl.tar "ENGINE_PLUS=1 SPEED_TEST=1 SPEED_PLAN=dl_plan.h" webkit-build-plus
one menu-rn-plus-show.tar "ENGINE_PLUS=1 SHOW=1" webkit-build-plus
one menu-rn-skia.tar      "ENGINE_DIR=skia" webkit-build-skia
one menu-rn-skia-feel.tar "ENGINE_DIR=skia SPEED_TEST=1 SPEED_PLAN=feel_plan.h" webkit-build-skia
one menu-rn.tar           ""
# What each one loads: a menu must never be linked with another engine's library.
for m in menu-rn.tar menu-rn-tour.tar menu-rn-feel.tar menu-rn-plus.tar menu-rn-plus-tour.tar menu-rn-skia.tar menu-rn-skia-feel.tar; do
    [ -f "$here/out/$m" ] || continue
    tar -xOf "$here/out/$m" data/homebrew/PPSA97024/browser.elf > /tmp/rn-menu-check.elf
    echo "$m: $(md5sum < /tmp/rn-menu-check.elf | cut -c1-8) $(strings -a /tmp/rn-menu-check.elf | grep -m1 'libWebKit.*SOVERSION') $(strings -a /tmp/rn-menu-check.elf | grep -m1 'NetworkProcess.elf')"
done
rm -f /tmp/rn-menu-check.elf
