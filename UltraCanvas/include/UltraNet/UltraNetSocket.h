// include/UltraNet/UltraNetSocket.h
// Raw TCP and UDP sockets — POSIX sockets on Linux/macOS, Winsock on Windows.
// Independent of libcurl. Sync-only in Stage 3.
// Version: 0.4.0 - a TCP connection through a proxy (UltraNetSocketOptions::proxy /
//                  useProxySetting: SOCKS 5, 4, 4a or an HTTP CONNECT tunnel),
//                  UltraNet_TcpOpen with the reason, UltraNet_SocketDetach
// Version: 0.3.0 (Stage 3)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraNetCore.h"

#include <cstdint>
#include <string>
#include <vector>

struct UltraNetSocketOptions {
    int connectTimeoutMs = 10000;
    int sendTimeoutMs = 0;              // 0 = no timeout
    int recvTimeoutMs = 0;              // 0 = no timeout
    bool keepAlive = false;
    int keepAliveIntervalMs = 0;
    bool noDelay = false;               // TCP_NODELAY
    bool reuseAddress = false;          // SO_REUSEADDR
    int sendBufferSize = 0;             // 0 = OS default
    int recvBufferSize = 0;
    bool useIpv6 = false;
    // Literal address to bind listeners / UDP sockets to. Empty = any
    // interface. Use "127.0.0.1" (or "::1" with useIpv6) to accept only
    // loopback connections — e.g. an OAuth2 redirect listener.
    std::string bindAddress;
    // A TCP connection through a proxy: `proxy` when it is enabled, else -
    // with useProxySetting - the module-wide setting resolved for the host
    // (UltraNet_ResolveProxy: the operating system's SOCKS proxy or
    // all_proxy under System, the proxy chosen otherwise, never for the
    // machine itself or an exception). Off by default: a raw socket is
    // mostly a device or a peer on the local network. SOCKS 5 (the proxy
    // looks the name up unless remoteDns is off), SOCKS 4 / 4a, or an HTTP
    // proxy as a CONNECT tunnel; a proxy reached over TLS (Https) is not
    // supported here. connectTimeoutMs bounds the proxy's handshake.
    UltraNetProxyConfig proxy;
    bool useProxySetting = false;

    static UltraNetSocketOptions Default() { return {}; }
};

struct UltraNetEndpoint {
    std::string address;
    int port = 0;
    bool isIpv6 = false;

    std::string ToString() const;
};

// ============================================================================
// TCP
// ============================================================================
UltraNetHandle UltraNet_TcpConnect(
    const std::string& host,
    int port,
    const UltraNetSocketOptions& options = UltraNetSocketOptions::Default());

// UltraNet_TcpConnect with the reason it failed: HostNotFound,
// ConnectionRefused, Timeout, AuthenticationRequired / AuthenticationFailed
// (the proxy's sign-in), Unsupported (a TLS proxy), with the proxy named in
// the message when the connection went through one.
UltraNetResult UltraNet_TcpOpen(
    const std::string& host,
    int port,
    const UltraNetSocketOptions& options,
    UltraNetHandle& outHandle);

UltraNetHandle UltraNet_TcpListen(
    int port,
    const UltraNetSocketOptions& options = UltraNetSocketOptions::Default());

// timeoutMs 0 = block until a client connects; > 0 = return
// UltraNetInvalidHandle if nobody connected within the window.
UltraNetHandle UltraNet_TcpAccept(
    UltraNetHandle listener,
    UltraNetEndpoint& outRemote,
    int timeoutMs = 0);

UltraNetResult UltraNet_TcpSend(
    UltraNetHandle handle,
    const std::vector<uint8_t>& data,
    int& outBytesSent);

UltraNetResult UltraNet_TcpReceive(
    UltraNetHandle handle,
    std::vector<uint8_t>& outData,
    int maxBytes = 65536,
    int timeoutMs = 0);

// ============================================================================
// UDP
// ============================================================================
UltraNetHandle UltraNet_UdpOpen(
    int localPort = 0,                  // 0 = ephemeral
    const UltraNetSocketOptions& options = UltraNetSocketOptions::Default());

UltraNetResult UltraNet_UdpSend(
    UltraNetHandle handle,
    const std::string& host,
    int port,
    const std::vector<uint8_t>& data);

UltraNetResult UltraNet_UdpReceive(
    UltraNetHandle handle,
    std::vector<uint8_t>& outData,
    UltraNetEndpoint& outRemote,
    int timeoutMs = 0);

// ============================================================================
// Common
// ============================================================================
UltraNetResult UltraNet_SocketClose(UltraNetHandle handle);

// Takes a connected TCP socket out of UltraNet and hands its operating-system
// socket (a file descriptor, or a SOCKET on Windows) to the caller, who
// closes it from then on - a library that runs its own protocol over a
// connection made here (libssh, through a proxy). -1 for a handle that is
// not a TCP stream, or carries TLS.
std::intptr_t UltraNet_SocketDetach(UltraNetHandle handle);

// Address + port the socket is actually bound to (getsockname). Lets a
// listener opened with port 0 discover its ephemeral port.
UltraNetResult UltraNet_SocketLocalEndpoint(
    UltraNetHandle handle,
    UltraNetEndpoint& outLocal);

UltraNetResult UltraNet_SocketSetTimeout(
    UltraNetHandle handle,
    int sendTimeoutMs,
    int recvTimeoutMs);
