# 2026-11-11: an engine variant's library under a soname of its own
# (libWebKitPlus.so.SOVERSION for the "plus" engine, libWebKitSkia.so.SOVERSION
# for the Skia one), so it lives in /user/homebrew/lib next to the usual
# libWebKit.so.SOVERSION that v1 and the normal RN load: an engine built with
# other switches (ENABLE_CONTENT_EXTENSIONS changes the numbers of the
# messages between the processes, USE_SKIA what goes in them) must have the
# UI's library, the network process and the title's WebProcess from the same
# build, and must not replace v1's library.
#
# CMake has no setting for the soname alone, so this edits the link line's
# SONAME in the build directory's build.ninja and links again. Run it again
# whenever CMake wrote build.ninja anew (it says what it did).
#   wsl -e bash plus-soname.sh [build dir under ~/browserps5-build, default webkit-build-plus] [name, default Plus]
set -e
B=$HOME/browserps5-build/${1:-webkit-build-plus}
soname=libWebKit${2:-Plus}.so.SOVERSION
if grep -q "^  SONAME = $soname\$" "$B/build.ninja"; then
    echo "build.ninja: the soname is $soname already"
else
    n=$(grep -c '^  SONAME = libWebKit.so.SOVERSION$' "$B/build.ninja")
    [ "$n" = 1 ] || { echo "build.ninja: $n SONAME lines for libWebKit.so.SOVERSION, expected 1"; exit 1; }
    sed -i "s|^  SONAME = libWebKit.so.SOVERSION\$|  SONAME = $soname|" "$B/build.ninja"
    echo "build.ninja: soname $soname"
fi
env -u CC ninja -C "$B" WebKit WebProcess NetworkProcess | tail -2
readelf -d "$B/lib/libWebKit.so.SOVERSION" | grep -i soname
