
# BrowserPS5: video through GStreamer (USE_GSTREAMER, see
# OptionsPlayStation.cmake), with video frames drawn by Cairo
# (poc/webkit/media/ImageGStreamerCairo.cpp).
if (USE_GSTREAMER)
    include(platform/GStreamer.cmake)

    # The GLib ports add these themselves. platform/audio/gstreamer:
    # GStreamer.cmake adds it only with WebAudio, but AudioDecoder.cpp wants
    # its AudioDecoderGStreamer.h with USE_GSTREAMER alone; the other two hold
    # headers the player includes (ApplicationGLib.h, empty without
    # USE(GLIB), and GStreamerCaptureDeviceManager.h, without MediaStream).
    list(APPEND WebCore_PRIVATE_INCLUDE_DIRECTORIES
        "${WEBCORE_DIR}/platform/audio/gstreamer"
        "${WEBCORE_DIR}/platform/glib"
        "${WEBCORE_DIR}/platform/mediastream/gstreamer"
    )

    list(APPEND WebCore_SOURCES
        platform/graphics/gstreamer/ImageGStreamerCairo.cpp
    )
endif ()

# 2026-11-11 (the "full" engine): what WebGL and Web Audio need and the
# port's lists leave out (GLFence comes with Skia only; the two files of our
# own: webgl/GraphicsContextGLCoordinatedPlayStation.cpp, audio/AudioBusPlayStation.cpp).
if (ENABLE_WEBGL AND NOT USE_SKIA)
    list(APPEND WebCore_SOURCES
        platform/graphics/egl/GLFence.cpp
        platform/graphics/egl/GLFenceEGL.cpp
        platform/graphics/egl/GLFenceGL.cpp
    )
endif ()
if (ENABLE_WEBGL)
    list(APPEND WebCore_SOURCES platform/graphics/playstation/GraphicsContextGLCoordinatedPlayStation.cpp)
endif ()
if (ENABLE_WEB_AUDIO)
    list(APPEND WebCore_SOURCES platform/audio/playstation/AudioBusPlayStation.cpp)
endif ()
