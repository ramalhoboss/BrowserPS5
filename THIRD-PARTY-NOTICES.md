# Third-party notices

BrowserPS5 RN is free software under the GNU General Public License, version 3 or later (`LICENSE`). It is built on the projects below, which keep their own copyright and licence terms. This list names what the release carries and where each one comes from; it does not replace the licence texts, which are in each project's sources and, for the files installed on the console, beside them (`/user/homebrew/etc/ssl/LICENSE`, the `LICENSE` files in the fonts' folders and in the PDF reader's folder).

## In the engine (`eboot.bin`, `libWebKitFull.so`, `WebProcess.elf`, `NetworkProcess.elf`)

| Component | Licence | Project |
| --- | --- | --- |
| WebKit 627.1.7 (WebCore, JavaScriptCore, WTF, WebKit), with this project's patches (`poc/webkit/`) | LGPL-2.1-or-later and BSD 2-Clause | https://webkit.org |
| Parallax Browser (rikleigh), another WebKit browser for the PS5: `ps5locks` (the engine's locks, on futex words), the idea of refusing a page's connections to the console itself, `dnr-to-webkit.js`, which made the ad blocker's list from uBlock Origin Lite's rules, and the way the title loads the system keyboard on firmware 13.60 (`titleime.c`, its table of exports) | GPL-3.0-or-later | https://github.com/rikleigh/parallax-browser |
| XPS5 (ramalhoboss), an X client for the PS5: how a game's name, kind and version, the user's trophies and the PS5's captures are read (`xps5/game.c`, `json.c`, `mem.c`, `trophy.c`, `captures.c` in the browser's sources); the trophy files' layout was worked out by OpenPSApp (marcelorsdd) | GPL-3.0-or-later (OpenPSApp: GPL-3.0) |
| PlayBridge (ramalhoboss), a PS5 payload: how the console's temperatures, power use, clock and fan target are read (`health.c` in the browser's sources, written again and only reading) | GPL-3.0 |  |
| uBlock Origin Lite's rulesets (ublock-filters, EasyList, Peter Lowe's list), converted: `adblock/content-rules.json` (what is not loaded), `adblock/cosmetic-rules.json` and `adblock/cosmetic-entities.json` (what is hidden in the pages), `adblock/strict-hosts.txt` (its strict blocking's hosts); `adblock/info.txt` says from which release and files | GPL-3.0 (the lists themselves: GPL-3.0 or CC BY-SA 3.0 for EasyList, McRae GPL for Peter Lowe's) | https://github.com/uBlockOrigin/uBOL-home |
| ANGLE (WebGL) | BSD 3-Clause | https://chromium.googlesource.com/angle/angle |
| libwpe | BSD 2-Clause | https://github.com/WebPlatformForEmbedded/libwpe |
| cairo | LGPL-2.1 or MPL-1.1 | https://www.cairographics.org |
| pixman | MIT | https://www.pixman.org |
| FreeType | FreeType License (FTL) or GPL-2.0 | https://freetype.org |
| Fontconfig | MIT-style | https://www.freedesktop.org/wiki/Software/fontconfig |
| HarfBuzz | MIT (Old MIT) | https://harfbuzz.github.io |
| ICU | Unicode License | https://icu.unicode.org |
| libjpeg-turbo | IJG and BSD 3-Clause | https://libjpeg-turbo.org |
| libpng | PNG Reference Library License | http://www.libpng.org |
| libwebp | BSD 3-Clause | https://chromium.googlesource.com/webm/libwebp |
| zlib | zlib | https://zlib.net |
| bzip2 | bzip2 licence (BSD-style) | https://sourceware.org/bzip2 |
| XZ Utils (liblzma; also in the installer) | 0BSD and public domain | https://tukaani.org/xz |
| Zstandard | BSD 3-Clause (or GPL-2.0) | https://github.com/facebook/zstd |
| Brotli | MIT | https://github.com/google/brotli |
| curl | curl licence (MIT-style) | https://curl.se |
| nghttp2 1.64.0 | MIT | https://nghttp2.org |
| libpsl | MIT | https://github.com/rockdaboot/libpsl |
| OpenSSL | Apache-2.0 | https://www.openssl.org |
| Expat | MIT | https://libexpat.github.io |
| libxml2 | MIT | https://gitlab.gnome.org/GNOME/libxml2 |
| SQLite | public domain | https://sqlite.org |
| GLib | LGPL-2.1-or-later | https://gitlab.gnome.org/GNOME/glib |
| libffi | MIT | https://sourceware.org/libffi |
| PCRE2 | BSD 3-Clause | https://www.pcre.org |
| GNU libiconv, libintl (gettext) | LGPL-2.1-or-later | https://www.gnu.org/software/libiconv, https://www.gnu.org/software/gettext |
| GStreamer, with gst-plugins-base, -good, -bad and gst-libav | LGPL-2.1-or-later | https://gstreamer.freedesktop.org |
| FFmpeg (libavcodec, libavformat, libavutil, libavfilter, libswresample) | LGPL-2.1-or-later (some parts GPL-2.0-or-later, depending on the build) | https://ffmpeg.org |
| Opus | BSD 3-Clause | https://opus-codec.org |

## The title's runtime and its GPU driver (`eboot.bin`, `sce_module/libc.prx`)

The title is linked with the work of two PS5 homebrew projects, without which this browser would draw nothing:

| Component | Licence | Project |
| --- | --- | --- |
| PS5_Vulkan (Mihawk): the GPU broker, the title's link recipe, and the tools that make the PS5 executable | GPL-3.0-or-later | https://github.com/mihawk-99/PS5_Vulkan |
| RADV, Mesa's Vulkan driver, from Mihawk's fork of Mesa 26.2.0 with its PS5 window-system layer | MIT | https://github.com/mihawk-99/PS5_Mesa |
| ps5-native-app-boilerplate (BlackBearReloaded), PS5_Vulkan's foundation: the title's start-up code, the PS5 ELF converter and FSELF writer, and the generator of `sce_module/libc.prx` | GPL-3.0-or-later | https://github.com/blackbearreloaded/ps5-native-app-boilerplate |
| The PS5 payload SDK in PS5_Vulkan's fork (headers, C runtime, platform layer) | GPL-3.0-or-later | https://github.com/ps5-payload-dev/sdk |
| PlutoVG (Samuel Ugochukwu), which draws the starting screen; with the rasteriser from FreeType and the stb headers (Sean Barrett) it carries | MIT; FreeType License; MIT or public domain | https://github.com/sammycage/plutovg |

`sce_module/libc.prx` is a **generated** module (PS5_Vulkan's `runtime/README.md` says how): it holds no code or data copied from a console's module. No file of Sony's SDK or firmware is in this release.

## Beside the engine

| Component | Licence | Project |
| --- | --- | --- |
| Mesa 26.2 (`libEGL.so.1`: zink, GLES and gallium; `libvulkan.so.1`: RADV's client), with this project's patch (`poc/gl_zink/mesa-zink-ps5.patch`) | MIT | https://mesa3d.org |
| PDF.js (the PDF reader), the copy WebKit carries | Apache-2.0 | https://mozilla.github.io/pdf.js |
| Inside PDF.js, each with its licence file beside it in `pdfjs/web/`: Liberation Sans fonts (Liberation font licence), Foxit fonts (BSD 3-Clause, PDFium authors), Adobe CMap resources (BSD 3-Clause), OpenJPEG (BSD 2-Clause), the JBIG2 decoder from PDFium (BSD 3-Clause), qcms (MIT), QuickJS (MIT), ICC profiles (CC0 1.0) | as named | `pdfjs/web/standard_fonts/`, `cmaps/`, `wasm/`, `iccs/` |
| DejaVu fonts | Bitstream Vera and DejaVu licence (free) | https://dejavu-fonts.github.io |
| Noto Emoji (monochrome), the emoji in pages; its licence beside it, `fonts/NotoEmoji-LICENSE.txt` | SIL Open Font License 1.1 | https://github.com/google/fonts/tree/main/ofl/notoemoji |
| Mozilla's CA certificate list (`cert.pem`, as packaged by Ubuntu's ca-certificates; `ca-bundle.crt`, curl's extract) | MPL-2.0 | https://wiki.mozilla.org/CA |
| Fluent UI System Icons (the interface's icons) | MIT | https://github.com/microsoft/fluentui-system-icons |
| ps5-payload-sdk (the toolchain and its C runtime) | see the SDK | https://github.com/ps5-payload-dev/sdk |

The QR code generator in the interface follows the steps of Project Nayuki's QR Code generator (MIT), written again for this project.

Cheat and patch files are not in this release. The cheats panel fetches a game's file from github.com/etaHEN/PS5_Cheats, and the patches panel from github.com/illusionyy/PS-Game-Patch, only when its user asks for it; `patches-index.js` (in the start page) lists only the names and title IDs of that repository's files, so a game's patches are found without asking it first. The patch switches are written in the way etaHEN's patches plugin reads them (github.com/etaHEN/etaHEN-Plugins, `itemzflow_xml_patches`: the folders, and the number each switch file is named by), worked out from its sources and written again here.

## The LGPL components

WebKit, cairo, GLib, GStreamer, FFmpeg and the other components under the LGPL are linked into the engine. Their sources are the upstream ones named above; the changes this project makes to them are in `poc/webkit/`, `poc/media/` (gst-libav: `gst-libav-vorbis.py`) and `poc/gl_zink/`, with the scripts that build the whole engine from source (`poc/webkit/build-webkit.sh`), so that anyone can rebuild the engine with a changed copy of any of them.

## Trademarks

PlayStation, PS5 and DualSense are trademarks of Sony Interactive Entertainment. WebKit is a trademark of Apple Inc. BrowserPS5 RN is independent and is not endorsed by Sony, Apple or the WebKit project. The app's artwork was made for this project.
