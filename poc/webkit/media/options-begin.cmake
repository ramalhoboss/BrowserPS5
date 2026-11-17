
# BrowserPS5: video through GStreamer (docs/03-video-e-gpu.md, V2), only in
# the build that asks for it (build-webkit.sh MEDIA=1), so the other build's
# cmakeconfig.h stays as it was. One static GStreamer with its plugins
# (poc/media/build-media.sh), no GStreamer GL; WebAudio stays off below.
if (BROWSERPS5_MEDIA)
    include(GStreamerDefinitions)
    WEBKIT_OPTION_DEFAULT_PORT_VALUE(USE_GSTREAMER_FULL PRIVATE ON)
    WEBKIT_OPTION_DEFAULT_PORT_VALUE(USE_GSTREAMER_GL PRIVATE OFF)
endif ()
