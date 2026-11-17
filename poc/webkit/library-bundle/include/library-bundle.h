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

// Open replacement for Sony's private library-bundle.h, which
// Source/WebKit/WebProcess/InjectedBundle/playstation/InjectedBundlePlayStation.cpp
// uses to load an injected bundle:
//
//   auto bundle = LibraryBundle::create(path);   // stored as a void*
//   bundle->resolve("WKBundleInitialize");
//
// Only used when the app sets an injected bundle path, which BrowserPS5
// does not. Plain dlopen()/dlsym(); the bundle is never unloaded, like in
// WebKit.

#pragma once

#include <dlfcn.h>

class LibraryBundle {
public:
  static LibraryBundle* create(const char* path) {
    void* handle = path ? dlopen(path, RTLD_NOW | RTLD_LOCAL) : nullptr;
    return handle ? new LibraryBundle(handle) : nullptr;
  }

  void* resolve(const char* symbol) {
    return dlsym(m_handle, symbol);
  }

private:
  explicit LibraryBundle(void* handle) : m_handle(handle) {}

  void* m_handle;
};
