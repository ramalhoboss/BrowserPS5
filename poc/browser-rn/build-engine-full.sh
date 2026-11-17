# 2026-11-11: BrowserPS5 RN's "full" engine, in its own build directory
# (~/browserps5-build/webkit-build-full), next to the "plus" one that the
# console runs. YouTube's check of the browser rejects ours in most sessions
# (ump.spsrejectfailure: the film stops at 60 s), and the engine lacks much of
# what the Safari it says it is has.
#
# The configuration is the plus engine's own (its CMakeCache.txt, entry by
# entry, with the directory's name changed), plus the switches below: the plus
# build was configured once from another folder's script, and this way
# nothing else differs. A full build (7500 steps); the messages between the
# processes change number with the switches, so it is a closed set again:
# its own library name (plus-soname.sh webkit-build-full Full), its own
# processes' folder (pack-lib-rn.sh), its own title and menus.
#   wsl -e bash build-engine-full.sh            configure (if not yet) and build, errors kept going (-k 0)
#   wsl -e bash build-engine-full.sh configure  only configure
set -e
W=$HOME/browserps5-build; from=$W/webkit-build-plus; B=$W/webkit-build-full
SWITCHES="-DENABLE_WEB_AUDIO=ON -DENABLE_WEBGL=ON -DENABLE_NOTIFICATIONS=ON -DENABLE_MEDIA_SESSION=ON -DENABLE_MATHML=ON"
if [ ! -f "$B/build.ninja" ]; then
    mkdir -p "$B"
    python3 - "$from/CMakeCache.txt" "$B/init.cmake" <<'EOF'
import re, sys
out = []
for line in open(sys.argv[1], encoding='utf-8'):
    m = re.match(r'^([A-Za-z_0-9.+-]+):(BOOL|STRING|FILEPATH|PATH|UNINITIALIZED)=(.*)$', line.rstrip('\n'))
    if not m:
        continue
    name, kind, value = m.groups()
    if name in ('CMAKE_HOME_DIRECTORY', 'CMAKE_CACHEFILE_DIR') or name.endswith('_BINARY_DIR') or name.endswith('_SOURCE_DIR'):
        continue
    if value.endswith('-NOTFOUND'):
        continue
    value = value.replace('webkit-build-plus', 'webkit-build-full').replace('\\', '\\\\').replace('"', '\\"')
    out.append('set(%s "%s" CACHE %s "" FORCE)' % (name, value, 'STRING' if kind == 'UNINITIALIZED' else kind))
open(sys.argv[2], 'w', encoding='utf-8').write('\n'.join(out) + '\n')
print(len(out), 'entries from the plus engine')
EOF
    cmake -S "$W/WebKit" -B "$B" -G Ninja -C "$B/init.cmake" $SWITCHES > "$W/full-configure.log" 2>&1 || { tail -30 "$W/full-configure.log"; exit 1; }
    tail -3 "$W/full-configure.log"
fi
diff <(grep -E "^[A-Za-z_0-9-]+:(BOOL|STRING|FILEPATH|PATH)=" "$from/CMakeCache.txt" | sed "s#webkit-build-plus#B#g") \
     <(grep -E "^[A-Za-z_0-9-]+:(BOOL|STRING|FILEPATH|PATH)=" "$B/CMakeCache.txt" | sed "s#webkit-build-full#B#g") | grep '^[<>]' | cut -c1-200 | head -60 || true
[ "${1:-}" = configure ] && exit 0
echo "building: $W/build-full.log"
env -u CC ninja -C "$B" -k 0 WebKit WebProcess NetworkProcess > "$W/build-full.log" 2>&1 || true
grep -c "^FAILED" "$W/build-full.log" | sed 's/^/failed steps: /'
tail -2 "$W/build-full.log" | cut -c1-200
