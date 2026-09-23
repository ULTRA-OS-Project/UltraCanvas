// core/UltraNet/UltraNetTls.cpp
// Public TLS-wrap entry points. Delegates the actual TLS work to the
// per-platform backend in ultranet_tls_platform:: (declared in
// UltraNetTlsImpl.h; implemented in OS/<Platform>/UltraNetTlsImpl.*).
// Version: 0.3.1 (Stage 3 hardening)
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraNet/UltraNetTls.h"
#include "UltraNetSocketInternal.h"
#include "UltraNetTlsImpl.h"

// The TLS context belongs to the socket entry, which hands it to `Close`
// when the socket is closed, so Handshake and GetInfo below ask the socket
// for it (ultranet_internal::GetTlsCtx) rather than keeping a table of their
// own. One kept here would never learn that a connection had closed: it
// would grow by an entry per connection for the life of the process and
// answer later calls with a pointer that had already been freed.

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
    return tcpHandle;
}

UltraNetResult UltraNet_TlsHandshake(UltraNetHandle handle) {
    void* ctx = ultranet_internal::GetTlsCtx(handle);
    if (!ctx) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidHandle,
                                     "no TLS context attached to handle");
    }
    return ultranet_tls_platform::Handshake(ctx);
}

UltraNetTlsInfo UltraNet_TlsGetInfo(UltraNetHandle handle) {
    void* ctx = ultranet_internal::GetTlsCtx(handle);
    if (!ctx) return {};
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
