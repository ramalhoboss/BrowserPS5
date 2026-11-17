
# BrowserPS5: GStreamer and GLib for USE_GSTREAMER (BROWSERPS5_MEDIA above).
# This port turns pkg-config off, so build-webkit.sh passes their include
# directories (BROWSERPS5_GSTREAMER_INCLUDE_DIRS) and links their static
# libraries in its dependency group. GLib's types and GObject, but not its
# event loop: WTF keeps RunLoopGeneric, and GStreamer's bus messages reach
# the main thread through RunLoop (media/GStreamerCommon.cpp.patch).
if (USE_GSTREAMER)
    SET_AND_EXPOSE_TO_BUILD(USE_GSTREAMER_FULL ON)
    include_directories(SYSTEM ${BROWSERPS5_GSTREAMER_INCLUDE_DIRS})
    add_definitions(-DGST_STATIC_COMPILATION -DG_INTL_STATIC_COMPILATION -DPCRE2_STATIC)
    # platform/GStreamer.cmake links WebCore with it.
    add_library(GLib::GioUnix INTERFACE IMPORTED GLOBAL)
    set(GSTREAMER_FULL_INCLUDE_DIRS ${BROWSERPS5_GSTREAMER_INCLUDE_DIRS})
    set(GSTREAMER_FULL_LIBRARIES "")
endif ()
