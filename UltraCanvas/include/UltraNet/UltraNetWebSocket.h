// include/UltraNet/UltraNetWebSocket.h
// WebSocket client surface (ws:// and wss://). Backed by libcurl's native
// curl_ws_* API (libcurl >= 7.86; the Stage-3 CI/dev requirement).
// Version: 0.4.1 - maxMessageBytes (the incoming message's limit, 64 MB) in place of maxFrameSize, which
//                  nothing read; autoReconnect, maxReconnectAttempts and reconnectDelayMs removed (nothing
//                  read them either - an owner reconnects on onClose / onError)
// Version: 0.4.0 - pingIntervalMs pings and notices a silent peer; the server's PINGs are answered
// Version: 0.3.0 (Stage 3)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraNetCore.h"
#include "UltraNetHttp.h"   // UltraNetHttpHeaders

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

enum class UltraNetWebSocketState {
    Connecting,
    Open,
    Closing,
    Closed,
    Error
};

struct UltraNetWebSocketOptions {
    UltraNetHttpHeaders headers;
    std::vector<std::string> subprotocols;
    int connectTimeoutMs = 10000;
    // The client PINGs every pingIntervalMs (0: never), and a peer that sends
    // nothing at all - not even the PONG - for two intervals is taken as gone:
    // onError once and the state Closed, so the owner can reconnect. The
    // server's PINGs are answered either way.
    int pingIntervalMs = 0;
    // The largest message accepted from the other side, in bytes (0: no
    // limit). A message is assembled from however many reads it takes; one
    // that grows past this ends the connection - close 1009, "message too
    // big", and onError once - before the memory is spent. Reconnecting is the
    // owner's: on onClose / onError it connects again (a handle is one
    // connection).
    std::size_t maxMessageBytes = 64 * 1024 * 1024;
    bool verifyTls = true;

    static UltraNetWebSocketOptions Default() { return {}; }
};

// ============================================================================
// Connection lifecycle
// ============================================================================
UltraNetHandle UltraNet_WebSocketConnect(
    const std::string& url,
    const UltraNetWebSocketOptions& options = UltraNetWebSocketOptions::Default());

UltraNetResult UltraNet_WebSocketSendText(
    UltraNetHandle handle,
    const std::string& message);

UltraNetResult UltraNet_WebSocketSendBinary(
    UltraNetHandle handle,
    const std::vector<uint8_t>& data);

UltraNetResult UltraNet_WebSocketPing(
    UltraNetHandle handle,
    const std::vector<uint8_t>& payload = {});

UltraNetResult UltraNet_WebSocketClose(
    UltraNetHandle handle,
    int code = 1000,
    const std::string& reason = "");

bool                   UltraNet_WebSocketIsOpen(UltraNetHandle handle);
UltraNetWebSocketState UltraNet_WebSocketGetState(UltraNetHandle handle);

// ============================================================================
// Global callback bag — assignable by the application, dispatched by the
// per-connection receiver thread. (Mirrors the master registry's "WebSocket
// Callbacks" section. Per-connection handlers can be implemented on top by
// dispatching on the UltraNetHandle argument.)
// ============================================================================
struct UltraNetWebSocketCallbacks {
    std::function<void(UltraNetHandle)>                                       onOpen;
    std::function<void(UltraNetHandle, const std::string&)>                   onText;
    std::function<void(UltraNetHandle, const std::vector<uint8_t>&)>          onBinary;
    std::function<void(UltraNetHandle, int, const std::string&)>              onClose;
    std::function<void(UltraNetHandle, const std::vector<uint8_t>&)>          onPing;
    std::function<void(UltraNetHandle, const std::vector<uint8_t>&)>          onPong;
    std::function<void(UltraNetHandle, const std::string&)>                   onError;
};

// Replace the global callback bag. Returns the previous bag.
UltraNetWebSocketCallbacks UltraNet_WebSocketSetCallbacks(
    const UltraNetWebSocketCallbacks& cb);
UltraNetWebSocketCallbacks UltraNet_WebSocketGetCallbacks();
