/* Copyright (C) 2026 BrowserPS5 contributors

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING. If not, see
<http://www.gnu.org/licenses/>.  */

// Open stand-in for Sony's process-initialization headers, included by
// Source/WebKit/*/EntryPoint/playstation/*ProcessMain.cpp.

#pragma once

namespace PlayStation {

// The child's end of WebKit's IPC socket (BROWSERPS5_CHILD_IPC_FD).
int getConnectionIdentifier();

} // namespace PlayStation
