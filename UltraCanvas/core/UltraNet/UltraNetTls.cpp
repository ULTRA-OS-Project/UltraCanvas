// core/UltraNet/UltraNetTls.cpp
// Stage-3 stubs for raw-TCP TLS layering. Returns Unknown with a clear
// "deferred to v1.1" message; the implementation needs three OS-native
// TLS backends (OpenSSL on Linux, Schannel on Windows, SecureTransport on
// macOS) and lives behind a v1.1 milestone. HTTPS / WSS / FTPS users are
// already covered by the libcurl-backed UltraNet_* entry points.
// Version: 0.3.0 (Stage 3)
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraNet/UltraNetTls.h"

#include <mutex>
#include <string>

namespace {
    constexpr const char* kDeferredMsg =
        "UltraNet_Tls* is reserved for v1.1; use UltraNet_HttpGet / "
        "UltraNet_WebSocketConnect / UltraNet_FtpDownload for TLS-protected "
        "transport in Stage 3";

    std::mutex   g_tlsConfigMutex;
    std::string  g_caBundlePath;
    std::string  g_extraTrustedPem;
}

UltraNetHandle UltraNet_TlsWrap(UltraNetHandle /*tcpHandle*/,
                                const std::string& /*serverName*/,
                                const UltraNetTlsOptions& /*options*/) {
    return UltraNetInvalidHandle;
}

UltraNetResult UltraNet_TlsHandshake(UltraNetHandle /*handle*/) {
    return UltraNetResult::Error(UltraNetResultCode::Unknown, kDeferredMsg);
}

UltraNetTlsInfo UltraNet_TlsGetInfo(UltraNetHandle /*handle*/) {
    return {};
}

UltraNetResult UltraNet_TlsSetCABundle(const std::string& caBundlePath) {
    // Stored for v1.1 — the libcurl-backed paths honour the per-request /
    // per-config caBundlePath fields today.
    std::lock_guard<std::mutex> lk(g_tlsConfigMutex);
    g_caBundlePath = caBundlePath;
    return UltraNetResult::Ok();
}

UltraNetResult UltraNet_TlsAddTrustedCert(const std::string& certPemData) {
    std::lock_guard<std::mutex> lk(g_tlsConfigMutex);
    g_extraTrustedPem += certPemData;
    if (!g_extraTrustedPem.empty() && g_extraTrustedPem.back() != '\n') {
        g_extraTrustedPem.push_back('\n');
    }
    return UltraNetResult::Ok();
}
