// Plugins/UltraIM/direct/DirectRelayClient.h
// ChatCube Direct - the relay's REST API and its WebSocket, over UltraNet
// Version: 0.1.1 - an address without a scheme is https://; a redirect is followed with its body
// Last Modified: 2026-10-22
// Author: UltraCanvas Framework
#pragma once

#include "DataFormats/UltraCanvasJSON.h"
#include "UltraNet/UltraNetCore.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>

namespace UltraIMDirect {

struct RelayResponse {
    int                    status = 0;      // HTTP status; 0 = the request never completed
    UltraCanvas::JSONValue body;
    std::string            error;           // transfer error, or the relay's error_description

    bool Ok() const { return status >= 200 && status < 300; }
};

using RelayCallback = std::function<void(const RelayResponse& response)>;

// Callbacks run on UltraNet's threads (the HTTP worker, the socket's
// receiver). After Stop() returns, none runs any more.
class RelayClient {
public:
    RelayClient();
    ~RelayClient();

    // "https://relay.example.org" (or "http://127.0.0.1:8790" for a local test
    // relay). An address without a scheme ("relay.example.org") is https://.
    void SetBaseUrl(const std::string& url);
    std::string GetBaseUrl() const;
    void SetToken(const std::string& token);
    // "sha256//<base64>" for a self-hosted relay trusted by key.
    void SetPinnedPublicKey(const std::string& pin);

    // A relay that redirects (http:// to https://, or a new host) is followed
    // by this client, body and all, and its new address kept: GetBaseUrl()
    // says where it settled. libcurl's own following would drop a POST's body
    // and keep its method, and the relay would answer 422.
    void Request(const std::string& method, const std::string& path,
                 const UltraCanvas::JSONValue* body, RelayCallback onDone);

    // ---- WebSocket ----------------------------------------------------------
    std::function<void(const UltraCanvas::JSONValue& frame)> onFrame;
    std::function<void()> onSocketOpen;
    std::function<void()> onSocketClose;

    // Connects and keeps the socket up (reconnecting with backoff) until Stop.
    void StartSocket();
    bool SendFrame(const UltraCanvas::JSONValue& frame);
    bool IsSocketOpen() const { return socketOpen_.load(); }

    // Closes the socket, cancels every request, and waits for the socket thread.
    void Stop();

    // State shared with UltraNet's callbacks (defined in the .cpp).
    struct Shared;

private:
    void Send(const std::string& method, const std::string& path,
              std::shared_ptr<const UltraCanvas::JSONValue> body, RelayCallback onDone,
              int redirectsLeft);
    void SocketLoop();

    std::shared_ptr<Shared> shared_;
    std::thread socketThread_;
    std::atomic<bool> socketOpen_{false};
};

} // namespace UltraIMDirect
