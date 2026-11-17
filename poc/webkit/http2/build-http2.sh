#!/bin/bash
#   Copyright (C) 2026 BrowserPS5 contributors
#
# This file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# HTTP/2 for WebKit (docs/22-http2.md). The SDK's libcurl (8.18.0) has no
# nghttp2, so WebKit's curl backend speaks HTTP/1.1 only: CurlContext asks
# for HTTP/2 on https: by itself (CURLOPT_HTTP_VERSION 2TLS, PIPEWAIT) once
# curl_version_info() says CURL_VERSION_HTTP2. This builds, with the SDK's
# compiler and libraries:
#   - nghttp2's library (static, no tools);
#   - libcurl 8.18.0 (the SDK's version), configured as the SDK's was
#     (curl-config --configure) plus --with-nghttp2.
# Both go into $ROOT (default ~/browserps5-build/ext/http2/root), never into
# the SDK: prospero.sh exports DESTDIR as the SDK's sysroot, so every install
# here names its own DESTDIR. build-webkit.sh HTTP2=1 then links them.
#
# The sources come from their projects' own sites, into $SRC; their SHA-256
# are checked (pinned 2026-11-06, the first download).
#
# Run inside WSL/Linux with PS5_PAYLOAD_SDK set.

set -euo pipefail

WORK=${WORK:-$HOME/browserps5-build}
H2=${H2:-$WORK/ext/http2}
SRC=$H2/src
ROOT=${ROOT:-$H2/root}
NGHTTP2_VERSION=1.64.0
CURL_VERSION=8.18.0
NGHTTP2_URL=https://github.com/nghttp2/nghttp2/releases/download/v$NGHTTP2_VERSION/nghttp2-$NGHTTP2_VERSION.tar.xz
CURL_URL=https://curl.se/download/curl-$CURL_VERSION.tar.xz
NGHTTP2_SHA256=${NGHTTP2_SHA256:-}
CURL_SHA256=${CURL_SHA256:-}
JOBS=${JOBS:-$(nproc)}

if [[ -z "${PS5_PAYLOAD_SDK:-}" ]]; then
    echo "error: PS5_PAYLOAD_SDK is not set" >&2
    exit 1
fi
source "$PS5_PAYLOAD_SDK/toolchain/prospero.sh"
SDK_ROOT="$PS5_SYSROOT"
unset DESTDIR  # never the SDK's sysroot
export PKG_CONFIG="$PS5_PAYLOAD_SDK/bin/prospero-pkg-config"

mkdir -p "$SRC" "$ROOT"

fetch() {  # url, file, sha256 (empty: print it)
    local url=$1 file=$SRC/$2 want=$3
    if [[ ! -f "$file" ]]; then
        echo "fetching $url"
        curl -fL --proto '=https' --tlsv1.2 -o "$file.part" "$url"
        mv "$file.part" "$file"
    fi
    local got
    got=$(sha256sum "$file" | cut -d' ' -f1)
    if [[ -n "$want" && "$got" != "$want" ]]; then
        echo "error: $file has SHA-256 $got, not $want" >&2
        exit 1
    fi
    echo "$2 sha256 $got"
}

fetch "$NGHTTP2_URL" "nghttp2-$NGHTTP2_VERSION.tar.xz" "$NGHTTP2_SHA256"
fetch "$CURL_URL" "curl-$CURL_VERSION.tar.xz" "$CURL_SHA256"

# --- nghttp2: the library only.
B=$H2/build-nghttp2
rm -rf "${B:?}"
mkdir -p "$B"
tar -xf "$SRC/nghttp2-$NGHTTP2_VERSION.tar.xz" -C "$B" --strip-components=1
(
    cd "$B"
    ./configure --host=x86_64-pc-freebsd --prefix="$PS5_HBROOT" \
        --enable-lib-only --enable-static --disable-shared \
        CC="$CC" AR="$AR" RANLIB="$PS5_PAYLOAD_SDK/bin/prospero-ranlib" >configure.log 2>&1 \
        || { tail -30 configure.log; exit 1; }
    make -j"$JOBS" >make.log 2>&1 || { tail -30 make.log; exit 1; }
    make install DESTDIR="$ROOT" >install.log 2>&1 || { tail -30 install.log; exit 1; }
)
echo "nghttp2: $(ls "$ROOT$PS5_HBROOT/lib/libnghttp2.a")"

# --- libcurl: the SDK's configure line, plus nghttp2. It finds OpenSSL,
# brotli, libpsl and zlib in the SDK (prospero-clang and its pkg-config).
B=$H2/build-curl
rm -rf "${B:?}"
mkdir -p "$B"
tar -xf "$SRC/curl-$CURL_VERSION.tar.xz" -C "$B" --strip-components=1
(
    cd "$B"
    PKG_CONFIG_PATH="$ROOT$PS5_HBROOT/lib/pkgconfig" \
    ./configure --prefix="$PS5_HBROOT" --host=x86_64-pc-freebsd \
        --enable-static --disable-shared --with-openssl --with-brotli \
        --with-ca-bundle="$PS5_HBROOT/etc/ca-bundle.crt" --disable-docs \
        --with-nghttp2="$ROOT$PS5_HBROOT" \
        CC="$CC" AR="$AR" RANLIB="$PS5_PAYLOAD_SDK/bin/prospero-ranlib" >configure.log 2>&1 \
        || { tail -40 configure.log; exit 1; }
    grep -E "HTTP2:|SSL:|brotli:|PSL:" configure.log | sed 's/^ */configure: /' || true
    make -j"$JOBS" -C lib >make.log 2>&1 || { tail -30 make.log; exit 1; }
    make -C lib install DESTDIR="$ROOT" >install.log 2>&1 || { tail -30 install.log; exit 1; }
    make -C include install DESTDIR="$ROOT" >>install.log 2>&1 || { tail -30 install.log; exit 1; }
    make install-binSCRIPTS install-pkgconfigDATA DESTDIR="$ROOT" >>install.log 2>&1 || true
)

LIB="$ROOT$PS5_HBROOT/lib/libcurl.a"
echo "libcurl: $LIB"
# The features as built (the static curl-config says them).
if [[ -x "$ROOT$PS5_HBROOT/bin/curl-config" ]]; then
    echo "features: $("$ROOT$PS5_HBROOT/bin/curl-config" --features | tr '\n' ' ')"
fi
nghttp2_refs=$("$PS5_PAYLOAD_SDK/bin/prospero-nm" -u "$LIB" 2>/dev/null | grep -c ' nghttp2_session_' || true)
echo "nghttp2 calls in libcurl.a: $nghttp2_refs (must be > 0)"
[[ "$nghttp2_refs" -gt 0 ]]
echo "SDK libcurl untouched: $(md5sum "$SDK_ROOT$PS5_HBROOT/lib/libcurl.a" | cut -c1-8)"
echo "done"
