// core/UltraNet/UltraNetTls.cpp
// Public TLS-wrap entry points. Delegates the actual TLS work to the
// per-platform backend in ultranet_tls_platform:: (declared in
// UltraNetTlsImpl.h; implemented in OS/<Platform>/UltraNetTlsImpl.*).
// Version: 0.3.1 (Stage 3 hardening)
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraNet/UltraNetTls.h"
#include "UltraNetSocketInternal.h"
#include "UltraNetTlsImpl.h"

#include <mutex>
#include <unordered_map>

namespace {
    // Tracks the per-handle TLS context pointer so UltraNet_TlsGetInfo and
    // UltraNet_TlsHandshake can find the right backend context. The pointer
    // is also held by the socket entry vtable (for Read/Write/Close), but
    // GetInfo / Handshake go through this side table.
    std::mutex g_mutex;
    std::unordered_map<UltraNetHandle, void*> g_ctxByHandle;
}

UltraNetHandle UltraNet_TlsWrap(UltraNetHandle tcpHandle,
                                const std::string& serverName,
                                const UltraNetTlsOptions& options) {
    const std::intptr_t fd = ultranet_internal::GetTcpFd(tcpHandle);
    if (fd < 0) return UltraNetInvalidHandle;

    void* ctx = nullptr;
    UltraNetResult r = ultranet_tls_platform::Wrap(fd, serverName, options, &ctx);
    if (!r || !ctx) return UltraNetInvalidHandle;

    if (!ultranet_internal::AttachTlsToSocket(
            tcpHandle, ctx,
            &ultranet_tls_platform::Read,
            &ultranet_tls_platform::Write,
            &ultranet_tls_platform::Close)) {
        ultranet_tls_platform::Close(ctx);
        return UltraNetInvalidHandle;
    }
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        g_ctxByHandle[tcpHandle] = ctx;
    }
    return tcpHandle;
}

UltraNetResult UltraNet_TlsHandshake(UltraNetHandle handle) {
    void* ctx = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        auto it = g_ctxByHandle.find(handle);
        if (it == g_ctxByHandle.end()) {
            return UltraNetResult::Error(UltraNetResultCode::InvalidHandle,
                                         "no TLS context attached to handle");
        }
        ctx = it->second;
    }
    return ultranet_tls_platform::Handshake(ctx);
}

UltraNetTlsInfo UltraNet_TlsGetInfo(UltraNetHandle handle) {
    void* ctx = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        auto it = g_ctxByHandle.find(handle);
        if (it == g_ctxByHandle.end()) return {};
        ctx = it->second;
    }
    return ultranet_tls_platform::GetInfo(ctx);
}

UltraNetResult UltraNet_TlsSetCABundle(const std::string& caBundlePath) {
    ultranet_tls_platform::SetGlobalCABundle(caBundlePath);
    return UltraNetResult::Ok();
}

UltraNetResult UltraNet_TlsAddTrustedCert(const std::string& certPemData) {
    ultranet_tls_platform::AddGlobalTrustedCertPem(certPemData);
    return UltraNetResult::Ok();
}
