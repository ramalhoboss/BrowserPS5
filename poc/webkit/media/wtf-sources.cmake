
# BrowserPS5: the GLib helpers WebCore's GStreamer code uses (USE_GSTREAMER,
# see OptionsPlayStation.cmake), without GLib's event loop.
if (USE_GSTREAMER)
    list(APPEND WTF_SOURCES
        glib/GMallocString.cpp
        glib/GRefPtr.cpp
        glib/GSpanExtras.cpp
    )

    list(APPEND WTF_PUBLIC_HEADERS
        glib/GLibExtras.h
        glib/GMallocString.h
        glib/GRefPtr.h
        glib/GSpanExtras.h
        glib/GThreadSafeWeakPtr.h
        glib/GTypedefs.h
        glib/GUniquePtr.h
        glib/GWeakPtr.h
        glib/RunLoopSourcePriority.h
        glib/WTFGType.h
    )
endif ()
