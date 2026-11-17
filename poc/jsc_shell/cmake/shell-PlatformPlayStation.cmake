# Copyright (C) 2026 BrowserPS5 contributors
#
# This file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# Replaces Source/JavaScriptCore/shell/PlatformPlayStation.cmake
# (build-jsc.sh copies it over the upstream file).
#
# The upstream file builds the jsc shell as a test harness for Sony's
# private "libtestwrappers", and its Initializer.cpp dlopen()s Sony system
# libraries by their SDK names at startup. Neither exists outside Sony's SDK,
# so the BrowserPS5 build keeps the ordinary jsc.cpp main(), which reads
# scripts from argv or runs a REPL on stdin (the TCP socket the payload was
# sent over).

list(APPEND jsc_LIBRARIES ${MEMORY_EXTRA_LIB})

set(PLAYSTATION_jsc_PROCESS_NAME "JSCShell")
set(PLAYSTATION_jsc_MAIN_THREAD_NAME "JSCShell")
