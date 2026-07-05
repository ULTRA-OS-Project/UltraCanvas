// include/UltraNet/UltraNetWebSocket.h
// WebSocket client surface (ws:// and wss://). Backed by libcurl's native
// curl_ws_* API (libcurl >= 7.86; the Stage-3 CI/dev requirement).
// Version: 0.3.0 (Stage 3)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraNetCore.h"
#include "UltraNetHttp.h"   // UltraNetHttpHeaders

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
    int pingIntervalMs = 0;             // 0 = disabled
    bool autoReconnect = false;
    int maxReconnectAttempts = 0;
    int reconnectDelayMs = 2000;
    int maxFrameSize = 0;               // 0 = libcurl default (~64KB)
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
