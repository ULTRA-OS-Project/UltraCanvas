// core/UltraNet/UltraNetWebSocket.cpp
// WebSocket implementation on libcurl's curl_ws_* API (libcurl >= 7.86).
//
// Each connection owns:
//   - one CURL* easy handle, configured with CURLOPT_CONNECT_ONLY=2
//     (libcurl's "WebSocket connect-only" mode — it performs the upgrade
//     handshake on curl_easy_perform and then exposes ws_send/ws_recv).
//   - one receiver thread that loops curl_ws_recv and dispatches frames
//     to the global UltraNetWebSocketCallbacks bag.
//   - a send mutex so concurrent SendText/SendBinary/Close/Ping calls
//     don't interleave on the same easy handle.
// Version: 0.3.0 (Stage 3)
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraNet/UltraNetWebSocket.h"
#include "UltraNet/UltraNetCore.h"

#include <curl/curl.h>
#include <curl/websockets.h>

#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

struct WsConnection {
    UltraNetHandle handle = UltraNetInvalidHandle;
    CURL* easy = nullptr;
    curl_slist* slist = nullptr;
    std::mutex sendMutex;
    std::atomic<UltraNetWebSocketState> state{UltraNetWebSocketState::Closed};
    std::atomic<bool> running{false};
    std::thread receiver;

    ~WsConnection() {
        Stop();
        if (slist) curl_slist_free_all(slist);
        if (easy)  curl_easy_cleanup(easy);
    }

    void Stop() {
        running.store(false, std::memory_order_release);
        if (receiver.joinable()) receiver.join();
    }
};

std::mutex g_regMutex;
std::unordered_map<UltraNetHandle, std::shared_ptr<WsConnection>> g_connections;
std::atomic<UltraNetHandle> g_nextHandle{1};

std::mutex g_cbMutex;
UltraNetWebSocketCallbacks g_callbacks;

UltraNetWebSocketCallbacks CallbacksSnapshot() {
    std::lock_guard<std::mutex> lk(g_cbMutex);
    return g_callbacks;
}

std::shared_ptr<WsConnection> FindConnection(UltraNetHandle h) {
    std::lock_guard<std::mutex> lk(g_regMutex);
    auto it = g_connections.find(h);
    return it == g_connections.end() ? nullptr : it->second;
}

void ReceiverLoop(std::shared_ptr<WsConnection> c) {
    // Buffer for partial fragments; libcurl returns per-chunk metadata.
    std::vector<uint8_t> textAcc, binaryAcc;
    bool inText = false, inBinary = false;

    char buf[8192];
    while (c->running.load(std::memory_order_acquire)) {
        std::size_t nread = 0;
        const struct curl_ws_frame* meta = nullptr;
        CURLcode rc = curl_ws_recv(c->easy, buf, sizeof buf, &nread, &meta);
        if (rc == CURLE_AGAIN) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }
        if (rc != CURLE_OK) {
            const auto cbs = CallbacksSnapshot();
            c->state.store(UltraNetWebSocketState::Closed, std::memory_order_release);
            if (cbs.onError) cbs.onError(c->handle, curl_easy_strerror(rc));
            break;
        }
        if (!meta) continue;

        const unsigned flags = meta->flags;
        const auto cbs = CallbacksSnapshot();

        if (flags & CURLWS_PING) {
            std::vector<uint8_t> payload(buf, buf + nread);
            if (cbs.onPing) cbs.onPing(c->handle, payload);
            // libcurl auto-replies with pong by default; explicit Pong send
            // is omitted to match that behaviour.
            continue;
        }
        if (flags & CURLWS_PONG) {
            std::vector<uint8_t> payload(buf, buf + nread);
            if (cbs.onPong) cbs.onPong(c->handle, payload);
            continue;
        }
        if (flags & CURLWS_CLOSE) {
            int code = 1005;
            std::string reason;
            if (nread >= 2) {
                code = (static_cast<unsigned char>(buf[0]) << 8) |
                        static_cast<unsigned char>(buf[1]);
                if (nread > 2) reason.assign(buf + 2, buf + nread);
            }
            c->state.store(UltraNetWebSocketState::Closed, std::memory_order_release);
            if (cbs.onClose) cbs.onClose(c->handle, code, reason);
            break;
        }

        if (flags & CURLWS_TEXT) {
            if (!inText) { textAcc.clear(); inText = true; }
            textAcc.insert(textAcc.end(), buf, buf + nread);
            if (!(flags & CURLWS_CONT)) {
                if (cbs.onText) cbs.onText(c->handle,
                    std::string(textAcc.begin(), textAcc.end()));
                textAcc.clear();
                inText = false;
            }
        } else if (flags & CURLWS_BINARY) {
            if (!inBinary) { binaryAcc.clear(); inBinary = true; }
            binaryAcc.insert(binaryAcc.end(), buf, buf + nread);
            if (!(flags & CURLWS_CONT)) {
                if (cbs.onBinary) cbs.onBinary(c->handle, binaryAcc);
                binaryAcc.clear();
                inBinary = false;
            }
        }
    }
}

curl_slist* BuildSlist(const UltraNetHttpHeaders& headers) {
    curl_slist* head = nullptr;
    for (const auto& [name, value] : headers.Entries()) {
        std::string line = name + ": " + value;
        head = curl_slist_append(head, line.c_str());
    }
    return head;
}

} // namespace

namespace {
    bool LibcurlHasWebSockets() {
        const curl_version_info_data* v = curl_version_info(CURLVERSION_NOW);
        if (!v || !v->protocols) return false;
        for (const char* const* p = v->protocols; *p; ++p) {
            if (std::strcmp(*p, "ws") == 0 || std::strcmp(*p, "wss") == 0) {
                return true;
            }
        }
        return false;
    }
}

UltraNetHandle UltraNet_WebSocketConnect(
    const std::string& url,
    const UltraNetWebSocketOptions& options) {

    if (!UltraNet_IsInitialized()) UltraNet_Initialize();
    if (url.empty()) return UltraNetInvalidHandle;

    if (!LibcurlHasWebSockets()) {
        const auto cbs = CallbacksSnapshot();
        if (cbs.onError) {
            cbs.onError(UltraNetInvalidHandle,
                "libcurl was built without WebSocket support — rebuild with "
                "--enable-websockets (curl >= 7.86), or install a libcurl "
                "package whose Protocols list includes ws/wss");
        }
        return UltraNetInvalidHandle;
    }

    auto c = std::make_shared<WsConnection>();
    c->easy = curl_easy_init();
    if (!c->easy) return UltraNetInvalidHandle;
    c->handle = g_nextHandle.fetch_add(1, std::memory_order_relaxed);
    c->state.store(UltraNetWebSocketState::Connecting, std::memory_order_release);

    curl_easy_setopt(c->easy, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c->easy, CURLOPT_CONNECT_ONLY, 2L);   // WebSocket connect-only
    curl_easy_setopt(c->easy, CURLOPT_CONNECTTIMEOUT_MS,
                     static_cast<long>(options.connectTimeoutMs));
    curl_easy_setopt(c->easy, CURLOPT_SSL_VERIFYPEER, options.verifyTls ? 1L : 0L);
    curl_easy_setopt(c->easy, CURLOPT_SSL_VERIFYHOST, options.verifyTls ? 2L : 0L);
    curl_easy_setopt(c->easy, CURLOPT_NOSIGNAL, 1L);

    UltraNetHttpHeaders headers = options.headers;
    if (!options.subprotocols.empty()) {
        std::string list;
        for (std::size_t i = 0; i < options.subprotocols.size(); ++i) {
            if (i) list += ", ";
            list += options.subprotocols[i];
        }
        headers.Set("Sec-WebSocket-Protocol", list);
    }
    c->slist = BuildSlist(headers);
    if (c->slist) curl_easy_setopt(c->easy, CURLOPT_HTTPHEADER, c->slist);

    CURLcode rc = curl_easy_perform(c->easy);
    if (rc != CURLE_OK) {
        const auto cbs = CallbacksSnapshot();
        c->state.store(UltraNetWebSocketState::Error, std::memory_order_release);
        if (cbs.onError) cbs.onError(c->handle, curl_easy_strerror(rc));
        return UltraNetInvalidHandle;
    }
    c->state.store(UltraNetWebSocketState::Open, std::memory_order_release);

    UltraNetHandle h = c->handle;
    {
        std::lock_guard<std::mutex> lk(g_regMutex);
        g_connections[h] = c;
    }

    const auto cbs = CallbacksSnapshot();
    if (cbs.onOpen) cbs.onOpen(h);

    c->running.store(true, std::memory_order_release);
    c->receiver = std::thread(ReceiverLoop, c);
    return h;
}

namespace {
    UltraNetResult SendFrame(WsConnection& c, const void* data, std::size_t len,
                             unsigned flags) {
        if (c.state.load(std::memory_order_acquire) != UltraNetWebSocketState::Open) {
            return UltraNetResult::Error(UltraNetResultCode::InvalidState,
                                         "WebSocket not open");
        }
        std::lock_guard<std::mutex> lk(c.sendMutex);
        std::size_t sent = 0;
        CURLcode rc = curl_ws_send(c.easy, data, len, &sent, 0, flags);
        if (rc != CURLE_OK) {
            return UltraNetResult::Error(UltraNetResultCode::SendFailed,
                                         curl_easy_strerror(rc));
        }
        return UltraNetResult::Ok();
    }
}

UltraNetResult UltraNet_WebSocketSendText(UltraNetHandle handle,
                                          const std::string& message) {
    auto c = FindConnection(handle);
    if (!c) return UltraNetResult::Error(UltraNetResultCode::InvalidHandle,
                                         "no such WebSocket");
    return SendFrame(*c, message.data(), message.size(), CURLWS_TEXT);
}

UltraNetResult UltraNet_WebSocketSendBinary(UltraNetHandle handle,
                                            const std::vector<uint8_t>& data) {
    auto c = FindConnection(handle);
    if (!c) return UltraNetResult::Error(UltraNetResultCode::InvalidHandle,
                                         "no such WebSocket");
    return SendFrame(*c, data.data(), data.size(), CURLWS_BINARY);
}

UltraNetResult UltraNet_WebSocketPing(UltraNetHandle handle,
                                      const std::vector<uint8_t>& payload) {
    auto c = FindConnection(handle);
    if (!c) return UltraNetResult::Error(UltraNetResultCode::InvalidHandle,
                                         "no such WebSocket");
    return SendFrame(*c, payload.data(), payload.size(), CURLWS_PING);
}

UltraNetResult UltraNet_WebSocketClose(UltraNetHandle handle,
                                       int code,
                                       const std::string& reason) {
    auto c = FindConnection(handle);
    if (!c) return UltraNetResult::Error(UltraNetResultCode::InvalidHandle,
                                         "no such WebSocket");
    // Build close payload: 2-byte status code + UTF-8 reason.
    std::vector<uint8_t> payload;
    payload.reserve(2 + reason.size());
    payload.push_back(static_cast<uint8_t>((code >> 8) & 0xff));
    payload.push_back(static_cast<uint8_t>(code & 0xff));
    payload.insert(payload.end(), reason.begin(), reason.end());

    c->state.store(UltraNetWebSocketState::Closing, std::memory_order_release);
    UltraNetResult sendRes = SendFrame(*c, payload.data(), payload.size(),
                                       CURLWS_CLOSE);
    c->Stop();
    c->state.store(UltraNetWebSocketState::Closed, std::memory_order_release);

    {
        std::lock_guard<std::mutex> lk(g_regMutex);
        g_connections.erase(handle);
    }
    return sendRes;
}

bool UltraNet_WebSocketIsOpen(UltraNetHandle handle) {
    auto c = FindConnection(handle);
    return c && c->state.load(std::memory_order_acquire) ==
                    UltraNetWebSocketState::Open;
}

UltraNetWebSocketState UltraNet_WebSocketGetState(UltraNetHandle handle) {
    auto c = FindConnection(handle);
    if (!c) return UltraNetWebSocketState::Closed;
    return c->state.load(std::memory_order_acquire);
}

UltraNetWebSocketCallbacks UltraNet_WebSocketSetCallbacks(
    const UltraNetWebSocketCallbacks& cb) {
    std::lock_guard<std::mutex> lk(g_cbMutex);
    UltraNetWebSocketCallbacks prev = std::move(g_callbacks);
    g_callbacks = cb;
    return prev;
}

UltraNetWebSocketCallbacks UltraNet_WebSocketGetCallbacks() {
    return CallbacksSnapshot();
}
