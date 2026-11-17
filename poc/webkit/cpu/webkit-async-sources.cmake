# BrowserPS5 RN (2026-11-11): the build with ENABLE_ASYNC_SCROLLING
# (~/browserps5-build/webkit-build-async) did not link without it.
if (USE_COORDINATED_GRAPHICS AND ENABLE_ASYNC_SCROLLING)
    list(APPEND WebKit_SOURCES
        WebProcess/WebPage/CoordinatedGraphics/ScrollingCoordinatorCoordinated.cpp
    )
endif ()

