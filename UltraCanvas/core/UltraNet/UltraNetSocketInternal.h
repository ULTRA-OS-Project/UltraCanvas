// core/UltraNet/UltraNetSocketInternal.h
// Internal hook layer that lets the TLS implementation upgrade a TCP socket
// (created by UltraNet_TcpConnect) into a TLS-wrapped one. Not part of the
// public include surface — only files inside the UltraNet target use it.
// Version: 0.4.0 - TlsReadFn answers kTlsReadTimedOut for a read that waited out its time
// Last Modified: 2026-11-17
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraNet/UltraNetCore.h"

#include <cstddef>
#include <cstdint>

namespace ultranet_internal {

    // Vtable supplied by UltraNetTls. Read returns a positive byte count on
    // success, 0 when the peer closed the connection, kTlsReadTimedOut when
    // nothing came within the socket's receive timeout (the connection is
    // still there), and -1 when it broke. Write returns the bytes written or
    // -1, and never raises SIGPIPE. tlsClose is responsible for freeing
    // its own ctx (so AttachTlsToSocket caller can stop tracking it).
    constexpr int kTlsReadTimedOut = -2;

    using TlsReadFn  = int  (*)(void* ctx, char* buf, std::size_t len, int timeoutMs);
    using TlsWriteFn = int  (*)(void* ctx, const char* buf, std::size_t len);
    using TlsCloseFn = void (*)(void* ctx);

    // Returns -1 if the handle isn't a TCP stream (or doesn't exist).
    // Use this from UltraNetTls.cpp to grab the underlying OS fd.
    std::intptr_t GetTcpFd(UltraNetHandle handle);

    // Marks the socket entry as TLS-wrapped. After this returns, any
    // UltraNet_TcpSend / UltraNet_TcpReceive / UltraNet_SocketClose call on
    // `handle` is routed through `tlsCtx` via the supplied function table.
    // Returns false if the handle isn't a TCP stream.
    bool AttachTlsToSocket(UltraNetHandle handle,
                           void* tlsCtx,
                           TlsReadFn  readFn,
                           TlsWriteFn writeFn,
                           TlsCloseFn closeFn);

    // Returns true if the socket has TLS hooks attached.
    bool IsTlsAttached(UltraNetHandle handle);

    // The TLS context attached to `handle`, or nullptr when the socket has
    // no TLS layer, was never a TCP stream, or has been closed. The socket
    // entry is the one owner of that pointer and clears it in
    // UltraNet_SocketClose, so a handle whose socket is gone answers
    // nullptr here instead of a context that has already been freed.
    void* GetTlsCtx(UltraNetHandle handle);

} // namespace ultranet_internal
