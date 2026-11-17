#!/bin/bash
# In WSL: menus_full.sh <name=switches>...: menus of the "full" engine from the sources as they are; the normal one last.
R=$(cd "$(dirname "$0")" && pwd)
one() {
    rm -f $R/out/$1  # a build that fails must not leave the menu of before under this name
    MENU_NAME=$1 EXTRA_MAKE="ENGINE_PLUS=1 ENGINE_DIR=full $2" RN_WEBKIT_BUILD=$HOME/browserps5-build/webkit-build-full bash $R/build-rn.sh 2>&1 | tail -2
    tar -xOf $R/out/$1 data/homebrew/${RN_ID:-PPSA97024}/browser.elf > /tmp/rn-menu-check.elf
    echo "$1: $(md5sum < /tmp/rn-menu-check.elf | cut -c1-8) $(strings -a /tmp/rn-menu-check.elf | grep -m1 'libWebKit.*SOVERSION')"
}
for a in "$@"; do one "${a%%=*}" "$(echo "${a#*=}" | tr ',' ' ')"; done
[ -n "${NO_NORMAL:-}" ] || one menu-rn-full.tar ""  # NO_NORMAL=1: the normal menu is left as it is (a test may be installing it)
