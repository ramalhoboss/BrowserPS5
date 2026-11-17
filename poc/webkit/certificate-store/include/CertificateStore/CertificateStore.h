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

// Open stand-in for Sony's CertificateStore library, as used by
// Source/WebCore/platform/network/playstation/CurlSSLHandlePlayStation.cpp.
// Header only: WebCore is the one user.
//
// The trusted roots are Mozilla's CA list in PEM, read at startup from
// BROWSERPS5_CA_BUNDLE (installed from Ubuntu's ca-certificates package,
// see poc/webkit/package.sh). Updating it needs no rebuild.

#pragma once

#include <memory>
#include <span>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <vector>

#define BROWSERPS5_CA_BUNDLE "/user/homebrew/etc/ssl/cert.pem"

class CertificateData {
public:
    static std::unique_ptr<CertificateData> create() { return std::make_unique<CertificateData>(); }

    const uint8_t* data() const { return m_bytes.data(); }
    size_t size() const { return m_bytes.size(); }

    std::vector<uint8_t>& bytes() { return m_bytes; }

private:
    std::vector<uint8_t> m_bytes;
};

namespace CertificateStore {

// Leaves the data empty when the bundle is missing; curl then has no roots
// and refuses every https site, which is the safe failure.
inline void getTrustedRootCA(CertificateData& certificates)
{
    FILE* file = fopen(BROWSERPS5_CA_BUNDLE, "rb");
    if (!file) {
        fprintf(stderr, "[CertificateStore] %s not found: https will fail\n", BROWSERPS5_CA_BUNDLE);
        return;
    }

    uint8_t buffer[16384];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), file)) > 0)
        certificates.bytes().insert(certificates.bytes().end(), buffer, buffer + n);
    fclose(file);
}

// TLS 1.2 suites of Mozilla's "intermediate" configuration (TLS 1.3 keeps
// OpenSSL's defaults).
inline std::span<const char> cipherSuites()
{
    static constexpr char suites[] =
        "ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:"
        "ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:"
        "ECDHE-ECDSA-CHACHA20-POLY1305:ECDHE-RSA-CHACHA20-POLY1305:"
        "DHE-RSA-AES128-GCM-SHA256:DHE-RSA-AES256-GCM-SHA384";
    return { suites, sizeof(suites) - 1 };
}

inline std::span<const char> supportedGroups()
{
    static constexpr char groups[] = "X25519:P-256:P-384";
    return { groups, sizeof(groups) - 1 };
}

inline bool shouldIgnoreTLSErrors()
{
    return false;
}

} // namespace CertificateStore
