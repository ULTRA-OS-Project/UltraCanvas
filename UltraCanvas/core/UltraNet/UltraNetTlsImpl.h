// core/UltraNet/UltraNetTlsImpl.h
// Per-platform TLS backend interface. Each OS implements these six functions:
//   Linux   - OS/Linux/UltraNetTlsImpl.cpp     (OpenSSL)
//   Windows - OS/MSWindows/UltraNetTlsImpl.cpp (Schannel via SSPI)
//   macOS   - OS/MacOS/UltraNetTlsImpl.mm      (SecureTransport)
//
// Functions return UltraNetResult / UltraNetTlsInfo at the API surface and
// raw int for Read/Write (so they can plug straight into the socket layer's
// TlsReadFn / TlsWriteFn / TlsCloseFn vtable). `outCtx` from Wrap is opaque
// — it is owned by the platform impl and freed by Close.
// Version: 0.3.1 (Stage 3 hardening)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraNet/UltraNetCore.h"
#include "UltraNet/UltraNetTls.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace ultranet_tls_platform {

    UltraNetResult Wrap(std::intptr_t fd,
                        const std::string& serverName,
                        const UltraNetTlsOptions& options,
                        void** outCtx);

    UltraNetResult  Handshake(void* ctx);
    UltraNetTlsInfo GetInfo(void* ctx);

    // Match TlsReadFn / TlsWriteFn / TlsCloseFn from UltraNetSocketInternal.h
    int  Read(void* ctx, char* buf, std::size_t len, int timeoutMs);
    int  Write(void* ctx, const char* buf, std::size_t len);
    void Close(void* ctx);

    // Used by UltraNet_TlsSetCABundle / UltraNet_TlsAddTrustedCert so the
    // per-platform backend can mirror the global trust state into its own
    // context whenever a new Wrap happens.
    void SetGlobalCABundle(const std::string& path);
    void AddGlobalTrustedCertPem(const std::string& pemData);

} // namespace ultranet_tls_platform
