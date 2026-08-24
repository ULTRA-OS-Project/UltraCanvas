// UltraAI/adapters/_shared/src/UltraNetTransport.cpp
// Implementation of UltraNetTransport. Compiled only when the module is
// built with ULTRAAI_USE_ULTRANET=ON and the UltraNet target is visible.
// Version: 0.2.0
// Last Modified: 2026-08-24
// Author: UltraAI Module

#ifdef ULTRAAI_HAS_ULTRANET

#include "UltraAIUltraNetTransport.h"
#include "UltraAIHttpError.h"

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetHttp.h>
#include <UltraNet/UltraNetSse.h>
#include <UltraNet/UltraNetWebSocket.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <map>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace UltraAI {

namespace {

std::string UpperCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return s;
}

UltraNetHttpRequest BuildNetRequest(const TransportRequest& request) {
    UltraNetHttpRequest net;
    net.url = request.url;

    const std::string method = UpperCopy(request.method);
    if      (method == "GET")     net.method = UltraNetHttpMethod::Get;
    else if (method == "POST")    net.method = UltraNetHttpMethod::Post;
    else if (method == "PUT")     net.method = UltraNetHttpMethod::Put;
    else if (method == "DELETE")  net.method = UltraNetHttpMethod::Delete;
    else if (method == "HEAD")    net.method = UltraNetHttpMethod::Head;
    else if (method == "PATCH")   net.method = UltraNetHttpMethod::Patch;
    else if (method == "OPTIONS") net.method = UltraNetHttpMethod::Options;
    else {
        net.method       = UltraNetHttpMethod::Custom;
        net.customMethod = method;
    }

    for (const auto& kv : request.headers) {
        net.headers.Add(kv.first, kv.second);
    }
    net.body.assign(request.body.begin(), request.body.end());
    net.options.timeoutMs = request.timeoutMs;
    return net;
}

TransportResponse ConvertResponse(const UltraNetResponse& netResp) {
    TransportResponse out;
    out.statusCode = netResp.statusCode;
    out.headers    = netResp.headers.Entries();
    out.body       = netResp.GetBodyAsString();
    return out;
}

Error MapNetError(const UltraNetResult& result) {
    Error e;
    switch (result.code) {
        case UltraNetResultCode::Success:
            return e;
        case UltraNetResultCode::Cancelled:
            e.code = ErrorCode::Cancelled; break;
        case UltraNetResultCode::Timeout:
        case UltraNetResultCode::ConnectionTimeout:
            e.code = ErrorCode::Timeout; break;
        case UltraNetResultCode::AuthenticationRequired:
        case UltraNetResultCode::AuthenticationFailed:
        case UltraNetResultCode::AccessDenied:
            e.code = ErrorCode::AuthenticationFailed; break;
        case UltraNetResultCode::InvalidUrl:
        case UltraNetResultCode::UnsupportedScheme:
            e.code = ErrorCode::InvalidRequest; break;
        case UltraNetResultCode::HostNotFound:
        case UltraNetResultCode::ConnectionRefused:
        case UltraNetResultCode::ConnectionReset:
        case UltraNetResultCode::SendFailed:
        case UltraNetResultCode::ReceiveFailed:
        case UltraNetResultCode::TlsHandshakeFailed:
        case UltraNetResultCode::TlsCertificateInvalid:
        case UltraNetResultCode::TlsCertificateExpired:
        case UltraNetResultCode::NotFound:
            e.code = ErrorCode::NetworkError; break;
        case UltraNetResultCode::HttpError:
            // Callers handle completed HTTP-error exchanges before calling
            // MapNetError; reaching here means an unexpected path.
            e.code = ErrorCode::ProviderError; break;
        default:
            e.code = ErrorCode::ProviderError; break;
    }
    e.message = result.message;
    return e;
}

// ---------------------------------------------------------------------
// WebSocket plumbing
//
// UltraNet dispatches WebSocket events through one process-wide callback
// bag, so this dispatcher owns that bag and routes by handle: frames for a
// handle we opened go to its sink, everything else is forwarded to the bag
// installed before us (an application with its own WebSocket callbacks
// keeps working).
//
// The receiver thread can deliver frames before UltraNet_WebSocketConnect
// has even returned the handle to us, so frames arriving for an unknown
// handle while a connect is in flight are buffered and flushed on
// registration instead of being dropped or misrouted.
// ---------------------------------------------------------------------

struct WsSink {
    WsMessageCallback  onMessage;
    WsCompleteCallback onComplete;
    std::atomic<bool>  finished{false};
    std::atomic<bool>  cancelled{false};
};

class WsDispatcher {
public:
    static WsDispatcher& Instance() {
        static WsDispatcher instance;
        return instance;
    }

    // Bound on the buffer held for one not-yet-registered handle. ComfyUI
    // sends a status frame on connect; a flood before registration means
    // the frames are not ours.
    static constexpr size_t kMaxPendingPerHandle = 64;

    void EnsureInstalled() {
        std::call_once(installOnce_, [this] {
            UltraNetWebSocketCallbacks cb;
            cb.onText = [](UltraNetHandle h, const std::string& text) {
                TransportWsMessage msg;
                msg.text = text;
                Instance().OnFrame(h, std::move(msg));
            };
            cb.onBinary = [](UltraNetHandle h, const std::vector<uint8_t>& data) {
                TransportWsMessage msg;
                msg.binary = true;
                msg.bytes  = data;
                Instance().OnFrame(h, std::move(msg));
            };
            cb.onClose = [](UltraNetHandle h, int code, const std::string& reason) {
                Instance().OnClose(h, code, reason);
            };
            cb.onError = [](UltraNetHandle h, const std::string& message) {
                Instance().OnError(h, message);
            };
            cb.onOpen = [](UltraNetHandle h) {
                auto prev = Instance().Previous();
                if (prev.onOpen) prev.onOpen(h);
            };
            cb.onPing = [](UltraNetHandle h, const std::vector<uint8_t>& payload) {
                auto prev = Instance().Previous();
                if (prev.onPing) prev.onPing(h, payload);
            };
            cb.onPong = [](UltraNetHandle h, const std::vector<uint8_t>& payload) {
                auto prev = Instance().Previous();
                if (prev.onPong) prev.onPong(h, payload);
            };
            UltraNetWebSocketCallbacks previous =
                UltraNet_WebSocketSetCallbacks(cb);
            std::lock_guard<std::mutex> lock(mutex_);
            previous_ = std::move(previous);
        });
    }

    void BeginConnect() {
        std::lock_guard<std::mutex> lock(mutex_);
        ++connectsInFlight_;
    }

    // Attach `sink` to `handle` and replay whatever arrived before the
    // handle was known. Pass UltraNetInvalidHandle when the connect failed,
    // which just ends the in-flight window.
    void EndConnect(UltraNetHandle handle, std::shared_ptr<WsSink> sink) {
        std::vector<TransportWsMessage> buffered;
        Error terminal;
        bool terminalPending = false;
        std::vector<std::pair<UltraNetHandle, Pending>> orphans;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (connectsInFlight_ > 0) --connectsInFlight_;
            if (handle != UltraNetInvalidHandle && sink) {
                auto it = pending_.find(handle);
                if (it != pending_.end()) {
                    buffered        = std::move(it->second.messages);
                    terminal        = it->second.terminal;
                    terminalPending = it->second.terminalSeen;
                    pending_.erase(it);
                }
                if (!terminalPending) sinks_[handle] = sink;
            }
            // Nothing is still connecting, so any remaining buffered frames
            // belong to connections we do not own.
            if (connectsInFlight_ == 0 && !pending_.empty()) {
                for (auto& entry : pending_) {
                    orphans.emplace_back(entry.first, std::move(entry.second));
                }
                pending_.clear();
            }
        }
        for (const auto& msg : buffered) {
            if (sink && sink->onMessage) sink->onMessage(msg);
        }
        if (terminalPending) Complete(sink, terminal);
        ForwardOrphans(orphans);
    }

    void Cancel(UltraNetHandle handle, const std::shared_ptr<WsSink>& sink) {
        if (!sink) return;
        sink->cancelled.store(true);
        if (handle != UltraNetInvalidHandle) {
            UltraNet_WebSocketClose(handle, 1000, "cancelled");
        }
        Detach(handle);
        Error error;
        error.code    = ErrorCode::Cancelled;
        error.message = "WebSocket stream cancelled";
        Complete(sink, error);
    }

private:
    struct Pending {
        std::vector<TransportWsMessage> messages;
        bool  terminalSeen = false;
        Error terminal;
    };

    UltraNetWebSocketCallbacks Previous() {
        std::lock_guard<std::mutex> lock(mutex_);
        return previous_;
    }

    static void Complete(const std::shared_ptr<WsSink>& sink,
                         const Error& error) {
        if (!sink || sink->finished.exchange(true)) return;
        if (sink->onComplete) sink->onComplete(error);
    }

    void Detach(UltraNetHandle handle) {
        std::lock_guard<std::mutex> lock(mutex_);
        sinks_.erase(handle);
    }

    void ForwardOrphans(
        const std::vector<std::pair<UltraNetHandle, Pending>>& orphans) {
        if (orphans.empty()) return;
        UltraNetWebSocketCallbacks prev = Previous();
        for (const auto& entry : orphans) {
            for (const auto& msg : entry.second.messages) {
                if (msg.binary) {
                    if (prev.onBinary) prev.onBinary(entry.first, msg.bytes);
                } else if (prev.onText) {
                    prev.onText(entry.first, msg.text);
                }
            }
        }
    }

    void OnFrame(UltraNetHandle handle, TransportWsMessage message) {
        std::shared_ptr<WsSink> sink;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = sinks_.find(handle);
            if (it != sinks_.end()) {
                sink = it->second;
            } else if (connectsInFlight_ > 0) {
                Pending& slot = pending_[handle];
                if (slot.messages.size() < kMaxPendingPerHandle) {
                    slot.messages.push_back(std::move(message));
                }
                return;
            }
        }
        if (sink) {
            if (sink->onMessage) sink->onMessage(message);
            return;
        }
        UltraNetWebSocketCallbacks prev = Previous();
        if (message.binary) {
            if (prev.onBinary) prev.onBinary(handle, message.bytes);
        } else if (prev.onText) {
            prev.onText(handle, message.text);
        }
    }

    void OnClose(UltraNetHandle handle, int code, const std::string& reason) {
        Error error;
        // A close initiated by us (Cancel) has already completed the sink;
        // an unexpected close code is reported as a provider error so the
        // caller does not read a truncated stream as a clean end.
        if (code != 0 && code != 1000 && code != 1001) {
            error.code    = ErrorCode::NetworkError;
            error.message = "WebSocket closed with code " +
                            std::to_string(code) +
                            (reason.empty() ? "" : ": " + reason);
        }
        if (TerminalFor(handle, error)) return;
        UltraNetWebSocketCallbacks prev = Previous();
        if (prev.onClose) prev.onClose(handle, code, reason);
    }

    void OnError(UltraNetHandle handle, const std::string& message) {
        Error error;
        error.code    = ErrorCode::NetworkError;
        error.message = message;
        if (TerminalFor(handle, error)) return;
        UltraNetWebSocketCallbacks prev = Previous();
        if (prev.onError) prev.onError(handle, message);
    }

    // True when the terminal event was consumed by (or buffered for) one of
    // our sinks; false when it belongs to a foreign connection.
    bool TerminalFor(UltraNetHandle handle, const Error& error) {
        std::shared_ptr<WsSink> sink;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = sinks_.find(handle);
            if (it != sinks_.end()) {
                sink = it->second;
                sinks_.erase(it);
            } else if (connectsInFlight_ > 0) {
                Pending& slot   = pending_[handle];
                slot.terminalSeen = true;
                slot.terminal     = error;
                return true;
            }
        }
        if (!sink) return false;
        if (sink->cancelled.load()) {
            Error cancelled;
            cancelled.code    = ErrorCode::Cancelled;
            cancelled.message = "WebSocket stream cancelled";
            Complete(sink, cancelled);
        } else {
            Complete(sink, error);
        }
        return true;
    }

    std::mutex mutex_;
    std::once_flag installOnce_;
    UltraNetWebSocketCallbacks previous_;
    std::map<UltraNetHandle, std::shared_ptr<WsSink>> sinks_;
    std::map<UltraNetHandle, Pending> pending_;
    int connectsInFlight_ = 0;
};

// ws:// and wss:// are what UltraNet expects; adapters naturally hold the
// http(s) form of the same endpoint.
std::string ToWebSocketUrl(const std::string& url) {
    if (url.rfind("http://", 0) == 0)  return "ws://"  + url.substr(7);
    if (url.rfind("https://", 0) == 0) return "wss://" + url.substr(8);
    return url;
}

} // namespace

TransportResponse UltraNetTransport::Request(const TransportRequest& request,
                                             Error* outError) {
    UltraNetHttpRequest net = BuildNetRequest(request);

    UltraNetResponse netResp;
    UltraNetResult result = UltraNet_HttpRequest(net, netResp);

    // HTTP >= 400 is a completed exchange: return the response for the
    // adapter to map (MapHttpStatus + provider error body).
    if (result || result.code == UltraNetResultCode::HttpError) {
        if (outError) *outError = {};
        return ConvertResponse(netResp);
    }

    if (outError) *outError = MapNetError(result);
    return {};
}

CancelFn UltraNetTransport::SseStream(const TransportRequest& request,
                                      SseEventCallback onEvent,
                                      SseCompleteCallback onComplete) {
    UltraNetHttpRequest net = BuildNetRequest(request);

    UltraNetHandle handle = UltraNet_SseStreamAsync(
        net,
        [onEvent](const UltraNetSseEvent& ev) {
            if (!onEvent) return;
            TransportSseEvent out;
            out.event   = ev.event;
            out.data    = ev.data;
            out.id      = ev.id;
            out.retryMs = ev.retry;
            onEvent(out);
        },
        [onComplete](const UltraNetResult& final) {
            if (!onComplete) return;
            if (final) {
                onComplete(Error{}, final.httpStatus);
            } else if (final.code == UltraNetResultCode::HttpError) {
                onComplete(MapHttpStatus(final.httpStatus, final.message),
                           final.httpStatus);
            } else {
                onComplete(MapNetError(final), final.httpStatus);
            }
        });

    if (handle == UltraNetInvalidHandle) {
        // Enqueue failed synchronously; surface it through onComplete so
        // the caller has exactly one completion path.
        if (onComplete) {
            Error e;
            e.code    = ErrorCode::NetworkError;
            e.message = "UltraNet_SseStreamAsync refused the request";
            onComplete(e, 0);
        }
        return [] {};
    }
    return [handle] { UltraNet_CancelRequest(handle); };
}

CancelFn UltraNetTransport::WebSocketStream(const TransportRequest& request,
                                            WsMessageCallback onMessage,
                                            WsCompleteCallback onComplete) {
    auto sink        = std::make_shared<WsSink>();
    sink->onMessage  = std::move(onMessage);
    sink->onComplete = std::move(onComplete);

    WsDispatcher& dispatcher = WsDispatcher::Instance();
    dispatcher.EnsureInstalled();

    UltraNetWebSocketOptions options;
    for (const auto& kv : request.headers) {
        options.headers.Add(kv.first, kv.second);
    }
    if (request.timeoutMs > 0) options.connectTimeoutMs = request.timeoutMs;

    dispatcher.BeginConnect();
    UltraNetHandle handle =
        UltraNet_WebSocketConnect(ToWebSocketUrl(request.url), options);
    dispatcher.EndConnect(handle, sink);

    if (handle == UltraNetInvalidHandle) {
        Error error;
        error.code    = ErrorCode::NetworkError;
        error.message = "UltraNet_WebSocketConnect failed for " + request.url;
        if (!sink->finished.exchange(true) && sink->onComplete) {
            sink->onComplete(error);
        }
        return [] {};
    }
    return [handle, sink] {
        WsDispatcher::Instance().Cancel(handle, sink);
    };
}

} // namespace UltraAI

#endif // ULTRAAI_HAS_ULTRANET
