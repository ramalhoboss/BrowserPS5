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

// The WebProcess inside the browser title (2c-2,
// docs/05-webprocess-no-titulo.md). The title listens on 127.0.0.1:
// WEB_HOST_PORT; the UIProcess's process launcher (process-launcher.cpp)
// connects and sends one line,
//
//   "BPS5WEB1 <core process identifier> BPS5RV1 <port> <secret>\n"
//
// (the second half is the meeting line of WebKit's client end,
// ipc_transport_endpoint_line()), and the title answers one line:
//
//   "OK <pid>\n"     the WebProcess runs in the title, whose pid this is;
//   "NO <why>\n"     it does not (one runs there already, ...): the
//                    launcher payload starts it as before.
//
// Then both close the connection.

#pragma once

#define WEB_HOST_PORT  9060
#define WEB_HOST_MAGIC "BPS5WEB1 "
#define WEB_HOST_LINE_MAX 160
