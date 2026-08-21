// UltraAI/adapters/_shared/include/UltraAITransport.h
// Transport seam for network adapters. Adapters speak this small interface
// instead of calling UltraNet directly, so unit tests can substitute an
// in-process ScriptedTransport and CI never touches a live provider
// (Docs/UltraNetIntegration.md §10-§11). The production implementation is
// UltraNetTransport (UltraAIUltraNetTransport.h), compiled in when the
// module is built with ULTRAAI_USE_ULTRANET=ON.
// Version: 0.1.0
// Last Modified: 2026-08-21
// Author: UltraAI Module
#pragma once

#include "UltraAICommon.h"

#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace UltraAI {

struct TransportRequest {
    std::string method = "POST";
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    int timeoutMs = 60000;
};

struct TransportResponse {
    int statusCode = 0;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;

    // First header matching `name`, case-insensitive; empty if absent.
    std::string GetHeader(const std::string& name) const;
};

// One parsed Server-Sent Event. Mirrors UltraNetSseEvent so the production
// transport converts field-for-field.
struct TransportSseEvent {
    std::string event;      // "" == "message"
    std::string data;       // concatenated data: lines, joined by '\n'
    std::string id;
    int retryMs = 0;        // 0 = unset
};

using SseEventCallback = std::function<void(const TransportSseEvent&)>;
// Terminal callback: error.IsOk() on clean end of stream; statusCode is the
// HTTP status when known (0 otherwise).
using SseCompleteCallback = std::function<void(const Error& error,
                                               int statusCode)>;
// Returned by SseStream; invoking it cancels the stream. Idempotent.
using CancelFn = std::function<void()>;

class ITransport {
public:
    virtual ~ITransport() = default;

    // Blocking request/response exchange. A completed exchange with an HTTP
    // error status (>= 400) is NOT a transport error: the response comes
    // back for the adapter to map (MapHttpStatus + provider error body).
    // *outError is set only for transport-level failures (DNS, TLS,
    // connect, timeout, cancel), where the returned response is meaningless.
    virtual TransportResponse Request(const TransportRequest& request,
                                      Error* outError) = 0;

    // Streaming SSE exchange. onEvent fires once per parsed event,
    // onComplete exactly once afterwards. Production transports fire both
    // on a worker thread (rules in Docs/UltraNetIntegration.md §5);
    // ScriptedTransport fires them inline before returning.
    virtual CancelFn SseStream(const TransportRequest& request,
                               SseEventCallback onEvent,
                               SseCompleteCallback onComplete) = 0;
};

// ============================================================================
// ScriptedTransport — the in-process test double. Script exchanges in the
// order the code under test will issue them; every issued request is
// recorded for assertions. Unscripted calls fail with
// ErrorCode::ProviderError so tests catch extra traffic.
// ============================================================================
class ScriptedTransport : public ITransport {
public:
    // Script a request/response exchange (statusCode >= 400 exercises the
    // adapter's HTTP error mapping) or a transport-level failure.
    void ScriptResponse(TransportResponse response);
    void ScriptError(Error error);

    // Script an SSE exchange: the events, then the terminal callback.
    void ScriptSse(std::vector<TransportSseEvent> events,
                   Error finalError = {}, int statusCode = 200);

    const std::vector<TransportRequest>& Requests() const { return requests_; }
    bool WasCancelled() const { return cancelled_; }

    TransportResponse Request(const TransportRequest& request,
                              Error* outError) override;
    CancelFn SseStream(const TransportRequest& request,
                       SseEventCallback onEvent,
                       SseCompleteCallback onComplete) override;

private:
    struct Exchange {
        bool isError = false;
        bool isSse   = false;
        TransportResponse response;
        Error error;
        std::vector<TransportSseEvent> events;
        int sseStatusCode = 200;
    };

    std::deque<Exchange> script_;
    std::vector<TransportRequest> requests_;
    bool cancelled_ = false;
};

} // namespace UltraAI
