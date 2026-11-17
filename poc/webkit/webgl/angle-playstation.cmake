# BrowserPS5 (2026-11-11, poc/webkit/webgl): ANGLE's OpenGL ES backend on the
# system's EGL (Mesa, in the title's process), as WPE's port builds it; the
# port's own two lines above only said USE_SYSTEM_EGL, and ANGLE then had no
# backend at all ("No default ANGLE platform type"). The console says it is a
# FreeBSD: ANGLE_PLATFORM_LINUX picks ANGLE's EGL display for it
# (libANGLE/Display.cpp). No DMA-BUF.
list(APPEND ANGLE_DEFINITIONS ANGLE_PLATFORM_LINUX EGL_NO_PLATFORM_SPECIFIC_TYPES)
include(linux.cmake)
if (ENABLE_WEBGL)
    list(APPEND ANGLE_SOURCES
        ${gl_backend_sources}
        ${angle_dma_buf_sources}
        ${libangle_gl_egl_dl_sources}
        ${libangle_gl_egl_sources}
        ${libangle_gl_sources}
        ${libangle_gpu_info_util_sources}
        ${libangle_gpu_info_util_linux_sources}
    )
    list(APPEND ANGLE_DEFINITIONS ANGLE_ENABLE_OPENGL)
endif ()
