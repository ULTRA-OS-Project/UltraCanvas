// include/UltraNet/UltraNetSocket.h
// Raw TCP and UDP sockets — POSIX sockets on Linux/macOS, Winsock on Windows.
// Independent of libcurl. Sync-only in Stage 3.
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

UltraNetHandle UltraNet_TcpListen(
    int port,
    const UltraNetSocketOptions& options = UltraNetSocketOptions::Default());

UltraNetHandle UltraNet_TcpAccept(
    UltraNetHandle listener,
    UltraNetEndpoint& outRemote);

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

UltraNetResult UltraNet_SocketSetTimeout(
    UltraNetHandle handle,
    int sendTimeoutMs,
    int recvTimeoutMs);
