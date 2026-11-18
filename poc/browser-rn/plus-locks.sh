# 2026-11-11: an engine variant's library with pthread mutexes, condition
# variables and rwlocks on futex words (webkit/locks/ps5locks.c, from Parallax
# Browser, GPL-3.0-or-later: the console's own hand a contended lock over at
# 4-20 us and convoy as soon as two threads meet). The object is in
# libCrashReport.a (build-webkit.sh; apply_batch below compiles it too) and
# is only linked in where the link line wraps the calls: this adds the
# --wrap options to the library's link line in the build directory's
# build.ninja (as plus-soname.sh does the soname) and links again. Never for
# the usual library (v1's): only for the RN engines with a soname of their own.
#   wsl -e bash plus-locks.sh [build dir under ~/browserps5-build, default webkit-build-plus]
# Off on the console, for a comparison: the file /data/homebrew/PPSA97025/no-fastlock.
set -e
W=$HOME/browserps5-build
B=$W/${1:-webkit-build-plus}
H=/mnt/c/Users/<you>/Desktop/BrowserPS5/BrowserPS5-WebKit/poc/webkit
FUNCTIONS="pthread_mutex_init pthread_mutex_destroy pthread_mutex_lock pthread_mutex_trylock pthread_mutex_timedlock pthread_mutex_unlock
pthread_mutexattr_init pthread_mutexattr_destroy pthread_mutexattr_settype pthread_mutexattr_gettype
pthread_cond_init pthread_cond_destroy pthread_cond_wait pthread_cond_timedwait pthread_cond_signal pthread_cond_broadcast
pthread_condattr_init pthread_condattr_destroy pthread_condattr_setclock pthread_condattr_getclock
pthread_rwlock_init pthread_rwlock_destroy pthread_rwlock_rdlock pthread_rwlock_tryrdlock pthread_rwlock_timedrdlock
pthread_rwlock_wrlock pthread_rwlock_trywrlock pthread_rwlock_timedwrlock pthread_rwlock_unlock"
soname=$(readelf -d "$B/lib/libWebKit.so.SOVERSION" | sed -n 's/.*Library soname: \[\(.*\)\]/\1/p')
[ "$soname" != libWebKit.so.SOVERSION ] || { echo "$B has the usual soname (v1's library): not here"; exit 1; }

# The object, in the archive every library links.
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk; source $PS5_PAYLOAD_SDK/toolchain/prospero.sh
"$CC" -O2 -fPIC -Wall -Werror -Wno-unused-function -DPS5LOCKS_OFF_FILE='"/data/homebrew/PPSA97024/no-fastlock"' \
    -c "$H/locks/ps5locks.c" -o "$W/crash-report/ps5locks.o"
members="$W/crash-report/crash-report.o"
[ -f "$W/crash-report/sqlite-batch.o" ] && members="$members $W/crash-report/sqlite-batch.o"
rm -f "$W/crash-report/libCrashReport.a"
"$AR" rcs "$W/crash-report/libCrashReport.a" $members "$W/crash-report/ps5locks.o"

wraps=""
for f in $FUNCTIONS; do wraps="$wraps -Wl,--wrap=$f"; done
python3 - "$B/build.ninja" "$wraps" <<'EOF'
import sys
path, wraps = sys.argv[1], sys.argv[2]
lines = open(path, encoding='utf-8').read().split('\n')
start = [i for i, l in enumerate(lines) if l.startswith('build lib/libWebKit.so.SOVERSION:')]
assert len(start) == 1, len(start)
i = start[0] + 1
while not lines[i].startswith('  LINK_FLAGS = '):
    assert lines[i].startswith('  '), 'no LINK_FLAGS in the library\'s build statement'
    i += 1
if '--wrap=pthread_mutex_lock' in lines[i]:
    print('build.ninja: the lock calls are wrapped already')
else:
    lines[i] += wraps
    open(path, 'w', encoding='utf-8').write('\n'.join(lines))
    print('build.ninja: the lock calls wrapped on the library\'s link line')
EOF
rm -f "$B/lib/libWebKit.so.SOVERSION"
env -u CC ninja -C "$B" WebKit WebProcess NetworkProcess | tail -2
READELF=$(command -v llvm-readelf || ls /usr/bin/llvm-readelf-* | tail -1) bash "$H/locks/check-lock-imports.sh" "$B/lib/libWebKit.so.SOVERSION" $FUNCTIONS
echo "wrapped lock functions in the library: $(nm "$B/lib/libWebKit.so.SOVERSION" 2>/dev/null | grep -c ' [tT] __wrap_pthread_')"
