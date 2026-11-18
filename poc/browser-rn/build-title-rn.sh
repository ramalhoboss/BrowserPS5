# BrowserPS5 RN's title eboot (2026-11-10): v1's title (../browser/title/build.sh,
# WEBKIT=1, as tests-v1/build-hw.sh) built anew, for title_memory.c's joined
# ranges and the "memory:" line every 5 s. Out: out/title-rn-base.tar (PPSA97020
# paths; build-rn.sh title renames it). v1's title-hw.tar is not touched.
#
# TITLE_WORK=<dir>, TITLE_BASE_OUT=<name.tar> and WEBKIT_BUILD=<build dir>
# (build.sh's): a title with another engine next to it (2026-11-11:
# title-rn-async-base.tar, title-rn-plus-base.tar).
set -uo pipefail
P=/mnt/c/Users/<you>/Desktop/BrowserPS5/BrowserPS5-WebKit/poc
base=${TITLE_BASE_OUT:-title-rn-base.tar}
log=/tmp/build-${base%.tar}.log
# 2026-11-11 16:20: every title of RN's keeps its WebProcess's log in memory
# (TITLE_QUIET_LOG: the writes to the disk, line by line, stopped the pages
# for up to a minute) and has the futex locks (TITLE_LOCKS), unless the caller
# says 0.
# 2026-11-14: and malloc on libpas (TITLE_MALLOC, build.sh), and -DTITLE_RN for what is RN's alone in the
# title's C files (video.c: the 1080p picture's rows, the ring's checks, the queues let go at the end).
# (TITLE_MALLOC stays 0 until ps5malloc.c is made to live with the SDK's own heap: the title's malloc is
# wrapped already, by libps5platform.a, and the two wrappers do not link together as they are.)
export TITLE_QUIET_LOG=${TITLE_QUIET_LOG-1} TITLE_LOCKS=${TITLE_LOCKS-1} TITLE_MALLOC=${TITLE_MALLOC-0}
# 2026-11-14: the system keyboard's loader (TITLE_SYSIME, build.sh); it does nothing on the console without
# the file sysime in the title's folder.
export TITLE_SYSIME=${TITLE_SYSIME-1}
# 2026-11-15: the microphone's proof for the voice search (TITLE_MIC, build.sh, title/mic.c): off unless asked for.
export TITLE_MIC=${TITLE_MIC-0}
# 2026-11-18: pipe() and pipe2() in the title on pairs of sockets (TITLE_PIPES, build.sh); TITLE_PIPES=0 builds one without.
export TITLE_PIPES=${TITLE_PIPES-1}
[ "$TITLE_QUIET_LOG" = 1 ] || unset TITLE_QUIET_LOG
# 2026-11-17: the touch pad's surface as a laptop's (hello_page/input.c INPUT_TOUCH_SURFACE, touch_surface.h);
# TITLE_TOUCH=0 builds a title without it.
touch=; [ "${TITLE_TOUCH-1}" = 1 ] && touch=" -DINPUT_TOUCH_SURFACE"
cd $P/browser/title && TITLE_EXTRA_CFLAGS="-DINPUT_SQUARE_REPEAT -DTITLE_RN$touch" WORK=${TITLE_WORK:-$HOME/browserps5-build/browser_title_rn} WEBKIT=1 \
    OUT_TAR=$P/browser-rn/out/$base bash build.sh > $log 2>&1
echo "exit $?"
grep -E " error|error:" $log | head -20
ls -la $P/browser-rn/out/$base 2>/dev/null && md5sum $P/browser-rn/out/$base | cut -c1-8
