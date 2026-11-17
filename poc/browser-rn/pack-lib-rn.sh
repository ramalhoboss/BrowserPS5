# 2026-11-11: a libWebKit.so as a tar for install_tar, into browser-rn/out
# (tests-v1/pack-lib.sh's, for any build directory). The file on the console
# is /user/homebrew/lib/<the library's soname>: v1 and RN share the usual one
# (libWebKit.so.SOVERSION), so an engine built with other switches (the
# "plus" one) gets a soname of its own (build.ninja's SONAME) and lives next
# to it; only the UI linked with it loads it.
#   wsl -e bash pack-lib-rn.sh <build dir under ~/browserps5-build> <name.tar>
set -e
B=$HOME/browserps5-build/$1; lib=$B/lib/libWebKit.so.SOVERSION
soname=$(readelf -d $lib | sed -n 's/.*Library soname: \[\(.*\)\]/\1/p')
STRIP=$(ls /opt/ps5-payload-sdk/bin/*strip* 2>/dev/null | head -1); [ -n "$STRIP" ] || STRIP=llvm-strip-20
# STRIP_MODE=unneeded (2026-11-11): only the symbols the loader needs (the plus library: 184 -> 150 MiB;
# a crash's lines get their names from the build's own file on the PC). THE DEFAULT since 2026-11-11 23:45: the
# full library so packed (203 -> 166 MB) passed the probe and the tour on the console. STRIP_MODE=debug: as before.
T=$(mktemp -d); mkdir -p $T/user/homebrew/lib; $STRIP --strip-${STRIP_MODE:-unneeded} -o $T/user/homebrew/lib/$soname $lib
files=user/homebrew/lib/$soname
# A library with a soname of its own comes with its network process and
# WebProcess (they load it by that soname), in a folder of RN's: main.cpp
# with ENGINE_DIR starts those (WEB_PROCESS_PATH, NETWORK_PROCESS_PATH). The
# folder is the soname's own word in small letters: libWebKitPlus -> plus,
# libWebKitSkia -> skia.
if [ "$soname" != libWebKit.so.SOVERSION ]; then
    engine=$(echo "$soname" | sed -E 's/^libWebKit([A-Za-z0-9]+)\.so.*$/\1/' | tr 'A-Z' 'a-z')
    case "$engine" in *[!a-z0-9]*|'') echo "no folder name in the soname $soname"; exit 1;; esac
    mkdir -p $T/user/homebrew/browserps5-rn/$engine
    for exe in WebProcess NetworkProcess; do
        readelf -d $B/bin/$exe | grep -q "\[$soname\]" || { echo "$B/bin/$exe does not load $soname: link it again (plus-soname.sh)"; exit 1; }
        $STRIP --strip-debug -o $T/user/homebrew/browserps5-rn/$engine/$exe.elf $B/bin/$exe
        files="$files user/homebrew/browserps5-rn/$engine/$exe.elf"
    done
    # 2026-11-11: the settings RN's own engines come with (crash-report.c
    # reads the file): SQLite's log in blocks (webkit/storage/sqlite-batch.c),
    # no page's connection to the console's own services (webkit/security).
    printf '%s\n' '# BrowserPS5 RN: the settings its own engines come with (pack-lib-rn.sh)' \
        'BROWSERPS5_SQLITE_BATCH=1' 'BROWSERPS5_NO_LOCAL=1'         '# 2026-11-12: up to 12 connections to a site at once (6 before) and 28 in all (17): a download in several'         '# parts has up to 8 of its own (browser/main.cpp multi_*), and its site still needs some for its pages.'         'WEBKIT_CURL_MAX_HOST_CONNECTIONS=12' 'WEBKIT_CURL_MAX_TOTAL_CONNECTIONS=28' > $T/user/homebrew/browserps5-rn/engine-env.txt
    files="$files user/homebrew/browserps5-rn/engine-env.txt"
fi
# 2026-11-14: the build machine's user names out of what is packed (../common/scrub-paths.py: the paths of the
# sources the compiler leaves in a binary; the name's letters are changed in place, nothing else).
python3 -I /mnt/c/Users/<you>/Desktop/BrowserPS5-WebKit/poc/common/scrub-paths.py $(cd $T && find user -type f \( -name "*.SOVERSION" -o -name "*.elf" \) | sed "s|^|$T/|")
(cd $T && tar --format=ustar --owner=0 --group=0 -cf /mnt/c/Users/<you>/Desktop/BrowserPS5-WebKit/poc/browser-rn/out/$2 $files)
echo "$2: $soname $(md5sum $T/user/homebrew/lib/$soname | cut -c1-8) $(stat -c %s $T/user/homebrew/lib/$soname) bytes"
tar -tvf /mnt/c/Users/<you>/Desktop/BrowserPS5-WebKit/poc/browser-rn/out/$2 | awk '{print "  " $3, $6}'; rm -rf $T
