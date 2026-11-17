# Copyright (C) 2026 BrowserPS5 contributors
#
# This file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# Open stand-in for Sony's PlayStationModule.cmake, which ships with Sony's
# SDK (not with WebKit) and is included by Source/cmake/OptionsPlayStation.cmake.
# build-jsc.sh copies this file into WebKit's Source/cmake.
#
# Sony's version registers each dependency as a .sprx module that gets copied
# next to the app and dlopen()ed at startup (the *_LOAD_AT macros). The
# BrowserPS5 build links its dependencies statically into one payload ELF, so
# there is nothing to register or copy and these functions do nothing.

# playstation_module(<name> TARGET <target> FOLDER <folder>)
function(playstation_module _name)
endfunction()

# Sony uses these to pick its libc flavour and floating point mode per
# executable. The ps5-payload-sdk toolchain already sets both.
function(playstation_setup_libc _target)
endfunction()

function(playstation_setup_fp _target)
endfunction()
