// core/UltraNet/UltraNetWebSocket.cpp
// WebSocket implementation on libcurl's curl_ws_* API (libcurl >= 7.86).
//
// Each connection owns:
//   - one CURL* easy handle, configured with CURLOPT_CONNECT_ONLY=2
//     (libcurl's "WebSocket connect-only" mode — it performs the upgrade
//     handshake on curl_easy_perform and then exposes ws_send/ws_recv).
//   - one receiver thread that loops curl_ws_recv and dispatches frames
//     to the global UltraNetWebSocketCallbacks bag.
//   - one I/O mutex around every curl_ws_recv and curl_ws_send: an easy
//     handle is not thread-safe, and the receiver reads while other threads
//     send. It is held for the call only, never while a callback runs (a
//     callback may send).
//
// Keeping it alive. A PING from the server is answered with a PONG carrying
// its payload - here, not by libcurl: in connect-only mode libcurl 8.5 does
// not, and a server that pings (uvicorn: every 20 s, closing after 20 s more)
// dropped every idle connection after about 40 s; a libcurl that would answer
// by itself is told not to (CURLWS_NOAUTOPONG), so there is one PONG each.
// With options.pingIntervalMs the client pings too, and a peer that sends
// nothing at all - not even the PONG - for two intervals is taken as gone:
// onError once, the state Closed, so its owner reconnects instead of waiting
// on a dead connection.
//
// A message is assembled from however many reads it takes, up to
// options.maxMessageBytes (64 MB unless set): past it the connection ends
// with close 1009 before the memory is spent. Reconnecting is the owner's.
// Version: 0.4.1 - options.maxMessageBytes: a message past it ends the connection (1009) before it is
//                  assembled; the reconnect options, which nothing read, removed
// Version: 0.4.0 - PINGs answered; pingIntervalMs pings and notices a silent peer; reads and writes
//                  of the one easy handle under one mutex; a frame larger than a read is delivered
//                  whole, not as one message per read
// Version: 0.3.0 (Stage 3)
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraNet/UltraNetWebSocket.h"
#include "UltraNet/UltraNetCore.h"

#include <curl/curl.h>
#include <curl/websockets.h>

#include <atomic>
#include <chrono>
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
    std::mutex ioMutex;                 // every curl_ws_recv and curl_ws_send
    int pingIntervalMs = 0;             // 0: the client does not ping
    std::size_t maxMessageBytes = 0;    // 0: no limit
    std::atomic<UltraNetWebSocketState> state{UltraNetWebSocketState::Closed};
    std::atomic<bool> running{false};
    // The receiver holds a shared_ptr to its connection, so the last
    // reference - and this destructor - can end up on the receiver itself.
    // threadMutex makes Start/Stop race-free; Stop never joins the calling
    // thread, it detaches it.
    std::mutex threadMutex;
    bool stopRequested = false;
    std::thread receiver;

    ~WsConnection() {
        Stop();
        if (slist) curl_slist_free_all(slist);
        if (easy)  curl_easy_cleanup(easy);
    }

    template <typename Fn>
    void Start(Fn&& body) {
        std::lock_guard<std::mutex> lk(threadMutex);
        if (stopRequested) return;
        running.store(true, std::memory_order_release);
        receiver = std::thread(std::forward<Fn>(body));
    }

    // Returns once the receiver has finished - unless called from the
    // receiver itself (a callback closing its own socket), which cannot wait
    // for itself: the loop then ends when that callback returns.
    void Stop() {
        running.store(false, std::memory_order_release);
        std::thread t;
        {
            std::lock_guard<std::mutex> lk(threadMutex);
            stopRequested = true;
            t = std::move(receiver);
        }
        if (!t.joinable()) return;
        if (t.get_id() == std::this_thread::get_id()) t.detach();
        else t.join();
    }
};

// Never destroyed: a receiver can still be running during static destruction
// at exit (no UltraNet_Shutdown), and it reads the callbacks and the table.
struct Globals {
    std::mutex regMutex;
    std::unordered_map<UltraNetHandle, std::shared_ptr<WsConnection>> connections;
    std::mutex cbMutex;
    UltraNetWebSocketCallbacks callbacks;
};
Globals& G() {
    static Globals* g = new Globals;
    return *g;
}
std::atomic<UltraNetHandle> g_nextHandle{1};

UltraNetWebSocketCallbacks CallbacksSnapshot() {
    std::lock_guard<std::mutex> lk(G().cbMutex);
    return G().callbacks;
}

std::shared_ptr<WsConnection> FindConnection(UltraNetHandle h) {
    std::lock_guard<std::mutex> lk(G().regMutex);
    auto it = G().connections.find(h);
    return it == G().connections.end() ? nullptr : it->second;
}

// One frame out, under the I/O mutex.
CURLcode SendLocked(WsConnection& c, const void* data, std::size_t len, unsigned flags) {
    std::lock_guard<std::mutex> lk(c.ioMutex);
    std::size_t sent = 0;
    return curl_ws_send(c.easy, data, len, &sent, 0, flags);
}

void ReceiverLoop(std::shared_ptr<WsConnection> c) {
    // Buffer for partial fragments; libcurl returns per-chunk metadata.
    std::vector<uint8_t> textAcc, binaryAcc;
    bool inText = false, inBinary = false;

    using Clock = std::chrono::steady_clock;
    Clock::time_point lastHeard = Clock::now();     // anything from the peer
    Clock::time_point lastPing = lastHeard;
    const auto interval = std::chrono::milliseconds(c->pingIntervalMs);

    char buf[8192];
    while (c->running.load(std::memory_order_acquire)) {
        std::size_t nread = 0;
        const struct curl_ws_frame* meta = nullptr;
        CURLcode rc;
        {
            std::lock_guard<std::mutex> lk(c->ioMutex);
            rc = curl_ws_recv(c->easy, buf, sizeof buf, &nread, &meta);
        }
        if (rc == CURLE_AGAIN) {
            if (c->pingIntervalMs > 0) {
                const Clock::time_point now = Clock::now();
                if (now - lastHeard > 2 * interval) {
                    // Not even the PONG: the connection is gone without a
                    // close (a network that dropped, a peer that died).
                    const auto cbs = CallbacksSnapshot();
                    c->state.store(UltraNetWebSocketState::Closed, std::memory_order_release);
                    if (cbs.onError)
                        cbs.onError(c->handle, "no answer from the other side for " +
                                                   std::to_string(2 * c->pingIntervalMs) + " ms");
                    break;
                }
                if (now - lastPing >= interval &&
                    c->state.load(std::memory_order_acquire) == UltraNetWebSocketState::Open) {
                    lastPing = now;
                    SendLocked(*c, "", 0, CURLWS_PING);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }
        if (rc != CURLE_OK) {
            const auto cbs = CallbacksSnapshot();
            c->state.store(UltraNetWebSocketState::Closed, std::memory_order_release);
            if (cbs.onError) cbs.onError(c->handle, curl_easy_strerror(rc));
            break;
        }
        lastHeard = Clock::now();
        if (!meta) continue;

        const unsigned flags = meta->flags;
        const auto cbs = CallbacksSnapshot();

        if (flags & CURLWS_PING) {
            std::vector<uint8_t> payload(buf, buf + nread);
            // Answered at once, with the same payload (RFC 6455 5.5.3).
            if (c->state.load(std::memory_order_acquire) == UltraNetWebSocketState::Open)
                SendLocked(*c, payload.data(), payload.size(), CURLWS_PONG);
            if (cbs.onPing) cbs.onPing(c->handle, payload);
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

        // A message is whole when this frame has no bytes left - libcurl
        // hands a frame larger than the buffer over in parts (bytesleft) -
        // and no fragment follows it (CURLWS_CONT).
        const bool messageDone = meta->bytesleft == 0 && !(flags & CURLWS_CONT);
        // A message past the limit is not assembled: the connection ends
        // (1009, message too big) before the memory is spent - checked by
        // what is already here plus what this frame still announces.
        if ((flags & (CURLWS_TEXT | CURLWS_BINARY)) && c->maxMessageBytes > 0) {
            const std::size_t held = (flags & CURLWS_TEXT) ? textAcc.size() : binaryAcc.size();
            const curl_off_t left = meta->bytesleft > 0 ? meta->bytesleft : 0;
            if (held + nread + static_cast<std::size_t>(left) > c->maxMessageBytes) {
                static const unsigned char tooBig[] = {0x03, 0xF1, 'm', 'e', 's', 's', 'a', 'g', 'e',
                                                       ' ', 't', 'o', 'o', ' ', 'b', 'i', 'g'};   // 1009
                SendLocked(*c, tooBig, sizeof tooBig, CURLWS_CLOSE);
                c->state.store(UltraNetWebSocketState::Closed, std::memory_order_release);
                if (cbs.onError)
                    cbs.onError(c->handle, "a message larger than " + std::to_string(c->maxMessageBytes) +
                                               " bytes (UltraNetWebSocketOptions::maxMessageBytes)");
                break;
            }
        }
        if (flags & CURLWS_TEXT) {
            if (!inText) { textAcc.clear(); inText = true; }
            textAcc.insert(textAcc.end(), buf, buf + nread);
            if (messageDone) {
                if (cbs.onText) cbs.onText(c->handle,
                    std::string(textAcc.begin(), textAcc.end()));
                textAcc.clear();
                inText = false;
            }
        } else if (flags & CURLWS_BINARY) {
            if (!inBinary) { binaryAcc.clear(); inBinary = true; }
            binaryAcc.insert(binaryAcc.end(), buf, buf + nread);
            if (messageDone) {
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
#ifdef CURLWS_NOAUTOPONG
    // The receiver answers PINGs itself (one PONG each, every libcurl).
    curl_easy_setopt(c->easy, CURLOPT_WS_OPTIONS, static_cast<long>(CURLWS_NOAUTOPONG));
#endif
    c->pingIntervalMs = options.pingIntervalMs > 0 ? options.pingIntervalMs : 0;
    c->maxMessageBytes = options.maxMessageBytes;

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
        std::lock_guard<std::mutex> lk(G().regMutex);
        G().connections[h] = c;
    }

    const auto cbs = CallbacksSnapshot();
    if (cbs.onOpen) cbs.onOpen(h);

    c->Start([c]() { ReceiverLoop(c); });
    return h;
}

namespace {
    UltraNetResult SendFrame(WsConnection& c, const void* data, std::size_t len,
                             unsigned flags) {
        if (c.state.load(std::memory_order_acquire) != UltraNetWebSocketState::Open) {
            return UltraNetResult::Error(UltraNetResultCode::InvalidState,
                                         "WebSocket not open");
        }
        CURLcode rc = SendLocked(c, data, len, flags);
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

    // Send the CLOSE frame *before* leaving the Open state. SendFrame() rejects
    // any send when state != Open, so storing Closing first (as this once did)
    // made the close handshake fail with "WebSocket not open". Only attempt the
    // send while still Open — the peer may already have closed us.
    UltraNetResult sendRes = UltraNetResult::Ok();
    if (c->state.load(std::memory_order_acquire) == UltraNetWebSocketState::Open) {
        sendRes = SendFrame(*c, payload.data(), payload.size(), CURLWS_CLOSE);
    }
    c->state.store(UltraNetWebSocketState::Closing, std::memory_order_release);
    c->Stop();
    c->state.store(UltraNetWebSocketState::Closed, std::memory_order_release);

    {
        std::lock_guard<std::mutex> lk(G().regMutex);
        G().connections.erase(handle);
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
    std::lock_guard<std::mutex> lk(G().cbMutex);
    UltraNetWebSocketCallbacks prev = std::move(G().callbacks);
    G().callbacks = cb;
    return prev;
}

UltraNetWebSocketCallbacks UltraNet_WebSocketGetCallbacks() {
    return CallbacksSnapshot();
}

namespace ultranet_internal {
    // Called by UltraNet_Shutdown before curl_global_cleanup: every receiver
    // is stopped and every easy handle freed while libcurl is still set up.
    // No close handshake - the process is going away; peers see the TCP close.
    void CloseAllWebSockets() {
        std::unordered_map<UltraNetHandle, std::shared_ptr<WsConnection>> all;
        {
            std::lock_guard<std::mutex> lk(G().regMutex);
            all.swap(G().connections);
        }
        for (auto& [handle, c] : all) {
            c->state.store(UltraNetWebSocketState::Closing, std::memory_order_release);
            c->Stop();
            c->state.store(UltraNetWebSocketState::Closed, std::memory_order_release);
        }
    }
}
