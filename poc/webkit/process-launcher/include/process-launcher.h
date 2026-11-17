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

// Open stand-in for Sony's process-launcher library, as used by
// Source/WebKit/UIProcess/Launcher/playstation/ProcessLauncherPlayStation.cpp
// (the branch without WPEBackend-PlayStation). The processes are started by
// the launcher payload, see ../launcher_proto.h.

#pragma once

#include <stdint.h>

namespace PlayStation {

struct LaunchParam {
    int32_t ipcSocket; // WebKit's end of the IPC socketpair, for the child
    int32_t userId;
};

// Start the ELF at path. argv[0] is WebKit's core process identifier.
// Returns the new process id, or a negative value on failure.
int32_t launchProcess(const char* path, char* const argv[], const LaunchParam&);

void terminateProcess(int32_t pid);

// BrowserPS5, 2c-2 (docs/05-webprocess-no-titulo.md): the pid of the title
// while it runs the WebProcess launchProcess() gave it (0: none). That pid
// is a title's, never a payload's: terminateProcess() leaves it alone, and
// so must the app (the pause).
int32_t titleWebProcessId();

// In the title: the descriptor getConnectionIdentifier() gives the
// WebProcess (its end, met from the UIProcess's line), in place of the
// launcher's BROWSERPS5_CHILD_IPC_FD.
void setConnectionIdentifier(int fd);

} // namespace PlayStation
