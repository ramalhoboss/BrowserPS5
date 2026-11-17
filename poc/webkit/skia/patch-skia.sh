# 2026-11-11: what the WebKit tree needs to build with USE_SKIA=ON for the PS5
# (~/browserps5-build/webkit-build-skia: the experiment of painting the pages'
# tiles on several threads; the normal builds have USE_SKIA=OFF and none of
# this touches what they compile). Each change is put in once (it looks for
# its own mark first), so this can be run again after a fresh checkout or
# after build-webkit.sh rewrote a file. Not in build-webkit.sh until the Skia
# engine has run on the console.
#   wsl -e bash patch-skia.sh
set -e
W=$HOME/browserps5-build/WebKit/Source
once() {  # <file under Source/> <mark that says it is in> <sed script>
    if grep -q "$2" "$W/$1"; then echo "in already: $1"; else sed -i "$3" "$W/$1"; echo "patched:    $1"; fi
    grep -q "$2" "$W/$1" || { echo "the patch did not apply to $1"; exit 1; }
}

# The SDK's <malloc.h> is an #error ("replaced by <stdlib.h>") and its libc
# has no malloc_usable_size(): the block's size is the size asked for.
once ThirdParty/skia/src/ports/SkMemory_malloc.cpp 'SK_BUILD_FOR_UNIX)) && !defined(__PROSPERO__)' \
    's|^#elif defined(SK_BUILD_FOR_ANDROID) \|\| defined(SK_BUILD_FOR_UNIX)$|#elif (defined(SK_BUILD_FOR_ANDROID) \|\| defined(SK_BUILD_FOR_UNIX)) \&\& !defined(__PROSPERO__) // BrowserPS5|; s|^    #elif defined(SK_BUILD_FOR_UNIX)$|    #elif defined(SK_BUILD_FOR_UNIX) \&\& !defined(__PROSPERO__) // BrowserPS5|'

# PlatformDisplaySkia.cpp leaves the Skia GL context out for PlayStation with
# coordinated graphics (its own comment: Skia cannot use OpenGL there), but
# six functions further down still use it: they get empty bodies in that
# configuration (PlatformDisplaySkia.stubs.inc).
H=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
once WebCore/platform/graphics/skia/PlatformDisplaySkia.cpp 'BrowserPS5: no Skia GL context here' \
    "/^void PlatformDisplay::setSkiaGLContextForCurrentThread/i #if !(PLATFORM(PLAYSTATION) \&\& USE(COORDINATED_GRAPHICS)) // BrowserPS5: no Skia GL context here
/^} \/\/ namespace WebCore\$/e cat $H/PlatformDisplaySkia.stubs.inc"

# Page.cpp asks Settings::hardwareAccelerationEnabled() for accelerated
# filters with Skia, a setting only GTK and WPE have: not on PlayStation (the
# filters stay the software ones).
once WebCore/page/Page.cpp 'ENABLE(WPE_PLATFORM)) && !PLATFORM(PLAYSTATION)' \
    's|^#if USE(SKIA) && !PLATFORM(WIN) && (!PLATFORM(WPE) \|\| ENABLE(WPE_PLATFORM))$|& \&\& !PLATFORM(PLAYSTATION) // BrowserPS5|'
