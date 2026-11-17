# Copyright (C) 2026 BrowserPS5 contributors
#
# This file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# Open stand-in for Sony's WebKitRequirements package, which
# Source/cmake/OptionsPlayStation.cmake loads with
#   find_package(WebKitRequirements REQUIRED COMPONENTS ...
#                OPTIONAL_COMPONENTS JPEG LibPSL LibXml2 SQLite3 WebP ZLIB)
#
# Sony's package ships prebuilt libraries. Ours provides only the pieces
# that are not ordinary open source libraries; the optional components are
# left unfound, so the port looks them up with its normal find modules in
# the SDK sysroot (built by the BrowserPS5 deps scripts).
#
# Components:
#   WebKitResources  Empty: on PlayStation loadPlatformResource() needs no
#                    data (it only logs and returns nullptr).
#   ProcessLauncher  ../../process-launcher (built by build-webkit.sh,
#                    path in BROWSERPS5_PROCESS_LAUNCHER_LIB).
#   libwpe           Open source libwpe, as WPE::libwpe.
#
# Sony's CertificateStore header is not here: build-webkit.sh installs ours
# (../../certificate-store) into the SDK sysroot, which is already on
# WebCore's include path.

set(_wkr_launcher_dir "${CMAKE_CURRENT_LIST_DIR}/../../process-launcher")

foreach (_wkr_comp IN LISTS WebKitRequirements_FIND_COMPONENTS)
    set(WebKitRequirements_${_wkr_comp}_FOUND FALSE)

    if (_wkr_comp STREQUAL "WebKitResources")
        if (NOT TARGET WebKitRequirements::WebKitResources)
            add_library(WebKitRequirements::WebKitResources INTERFACE IMPORTED)
        endif ()
        set(WebKitRequirements_WebKitResources_FOUND TRUE)

    elseif (_wkr_comp STREQUAL "ProcessLauncher")
        if (NOT BROWSERPS5_PROCESS_LAUNCHER_LIB OR NOT EXISTS "${BROWSERPS5_PROCESS_LAUNCHER_LIB}")
            message(FATAL_ERROR "BROWSERPS5_PROCESS_LAUNCHER_LIB must point at libProcessLauncher.a (build-webkit.sh builds it)")
        endif ()
        if (NOT TARGET WebKitRequirements::ProcessLauncher)
            add_library(WebKitRequirements::ProcessLauncher STATIC IMPORTED)
            set_target_properties(WebKitRequirements::ProcessLauncher PROPERTIES
                IMPORTED_LOCATION "${BROWSERPS5_PROCESS_LAUNCHER_LIB}"
                INTERFACE_INCLUDE_DIRECTORIES "${_wkr_launcher_dir}/include"
            )
        endif ()
        set(WebKitRequirements_ProcessLauncher_FOUND TRUE)

    elseif (_wkr_comp STREQUAL "libwpe")
        # WebKit's own Source/cmake/FindWPE.cmake creates WPE::libwpe. It
        # finds the headers through pkg-config, which the port disables, so
        # point it at libwpe's versioned include directory.
        find_path(WPE_INCLUDE_DIR NAMES wpe/wpe.h PATH_SUFFIXES wpe-1.0)
        find_package(WPE 1.14.0)
        if (TARGET WPE::libwpe)
            set(WebKitRequirements_libwpe_FOUND TRUE)
        endif ()
    endif ()
endforeach ()

foreach (_wkr_comp IN LISTS WebKitRequirements_FIND_COMPONENTS)
    if (WebKitRequirements_FIND_REQUIRED_${_wkr_comp} AND NOT WebKitRequirements_${_wkr_comp}_FOUND)
        set(WebKitRequirements_FOUND FALSE)
        set(WebKitRequirements_NOT_FOUND_MESSAGE "missing component ${_wkr_comp}")
    endif ()
endforeach ()
