# BrowserPS5 RN — source code

The complete source of the BrowserPS5 RN release of the same version: the browser, its title, its installer, the
changes made to WebKit, Mesa, GStreamer and the other libraries the engine is built from, and the scripts that build
all of it. BrowserPS5 RN is free software under the GNU General Public License, version 3 or later (`LICENSE`); the
projects it is built on keep their own licences (`poc/release-rn/THIRD-PARTY-NOTICES.md`).

The upstream projects themselves are not copied here: the build scripts download each one at a fixed version or
commit, and check it where a checksum is given (WebKit from github.com/WebKit/WebKit, Mesa from mesa.freedesktop.org,
FFmpeg, GStreamer and the others from their own sites). What this project changes in them is here, as patches and
replacement files.

A word of honesty: the folders keep the names and notes they had while the browser was being made (many notes are
in Portuguese, and dated), and the scripts were written for one build machine. Paths of that machine are written as
`<you>` and the console's address as `192.168.1.50`; set them for yours. This copy has not yet been built from
scratch on another machine.

## What is where

| Folder | What it is |
|---|---|
| `poc/browser/` | The browser itself (`main.cpp` and its parts: addresses, downloads, files, sessions, logins, phone page, settings, games, trophies, captures, cheats and patches, console health), its pages and its unit tests (`make test`) |
| `poc/browser/title/` | The title (`PPSA97024`): the screen, the GPU, the video decoder, the system keyboard, and the engine's page process living in it |
| `poc/browser-rn/` | This version's pages (`pages/`), languages, fonts, ad blocker (`adblock/`), art and the scripts that build the menus, the title and the packs |
| `poc/release-rn/` | The installer and the uninstallers (`install.c`, `uninstall.c`), `build-release-rn.sh`, the README, CHANGELOG and notices |
| `poc/webkit/` | WebKit 627.1.7 for the PS5: `build-webkit.sh` (downloads WebKit at a fixed commit and builds it), the patches and replacement files (locks, malloc, network, media, IPC over TCP, GPU compositing, storage...), `package.sh` |
| `poc/jsc_shell/` | JavaScriptCore's first port (build of ICU and libc++ for the SDK, the CMake platform files WebKit's build uses) |
| `poc/media/` | FFmpeg, GStreamer and their dependencies, static, for the SDK's sysroot (`build-media.sh`); the PS5 audio and video sinks |
| `poc/gl_soft/`, `poc/gl_zink/`, `poc/gpu_broker/` | OpenGL ES for the engine: Mesa (softpipe; zink over RADV on the console's GPU) and the GPU broker from PS5_Vulkan |
| `poc/webkit_title/`, `poc/hello_page/`, `poc/bigapp_gl/`, `poc/launcher/` | Pieces the title and the browser's link use (WebKit in a title, input, the process launcher) |
| `poc/play_after/` | The small payload that starts a game after the browser has closed ("Play") |
| `poc/install_tar/`, `poc/remove_files/`, `poc/common/` | What the installer and uninstaller are made of, and code shared by all |

## Building

Linux (or WSL on Windows) with the PS5 payload SDK (`PS5_PAYLOAD_SDK`, by default `/opt/ps5-payload-sdk`; the
PS5_Vulkan fork), cmake ≥ 3.20, ninja, perl, python3, ruby, git, node (for the ad blocker's list) and Python's PIL
(for the art). In order:

1. `poc/jsc_shell/build-libcxx.sh` and `build-icu.sh`, then the engine's dependencies (cairo, curl, harfbuzz...)
   into the SDK's sysroot, and `poc/media/build-media.sh`.
2. Mesa: `poc/gl_soft/build-mesa-soft.sh`, then `poc/gl_zink/build.sh` (with `BROWSER=1`).
3. The engine: `poc/browser-rn/build-engine-full.sh` (WebKit with media, JIT, IPC over TCP and HTTP/2, through
   `poc/webkit/build-webkit.sh`), then `pack-lib-rn.sh` and `pack-pdfjs-rn.sh`.
4. The title: `poc/browser-rn/title-full-rn.sh`.
5. The browser: `poc/browser-rn/menus-full-rn.sh "menu-rn-full.tar="`; the ad blocker's list
   (`poc/browser-rn/adblock/make-rules.js`) and the emoji font's pack (`poc/browser-rn/fonts/pack-fonts.sh`).
6. The release: `poc/release-rn/build-release-rn.sh` → `BrowserPS5.elf`, the uninstallers and the zip.

The browser's unit tests run on the PC: `make -C poc/browser test`. The IPC transport's (WebKit's messages over TCP
and named shared memory): `bash poc/webkit/ipc-transport/test.sh`. The touch pad's gestures:
`cc -O2 -Wall -Werror poc/hello_page/touch_surface_test.c -lm -o touch_test && ./touch_test`.
