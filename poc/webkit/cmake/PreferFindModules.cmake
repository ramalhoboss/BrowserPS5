# Copyright (C) 2026 BrowserPS5 contributors
#
# This file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# Included right after WebKit's project() (CMAKE_PROJECT_INCLUDE).
#
# The ps5-payload-sdk toolchain file sets CMAKE_FIND_PACKAGE_PREFER_CONFIG,
# so find_package() would pick the CMake configs some dependencies install
# in the sysroot (OpenSSL, harfbuzz, libxml2, libjpeg-turbo, ...) instead of
# the find modules WebKit's port is written against. WebKitRequirements has
# no find module, so it is still found through its config.
set(CMAKE_FIND_PACKAGE_PREFER_CONFIG FALSE)
