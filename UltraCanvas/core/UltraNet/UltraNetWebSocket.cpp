// core/UltraNet/UltraNetWebSocket.cpp
// WebSocket implementation on libcurl's curl_ws_* API (libcurl >= 7.86) -
// and, where the libcurl in use has no WebSockets, on UltraNet's own
// (UltraNetWebSocketOwn.h: its own sockets and TLS).
//
// Which one. A libcurl lists "ws" among its protocols or it does not: Apple's
// own, which the macOS build links, lists none through macOS 15, and several
// distributions build theirs without. UltraNet_WebSocketConnect used to
// refuse there - and with it everything on a WebSocket (ChatCube Direct's
// relay, Mattermost's events, XMPP over WebSocket) had no connection on such
// a system. It now connects by UltraNet's own client instead; what a caller
// sees - handles, callbacks, PINGs answered, the message limit, the proxy
// setting - is the same on both. ULTRANET_WEBSOCKET_CLIENT=own in the
// environment takes the own client where libcurl would do too (the tests run
// the whole suite on it that way; a diagnosis may want it).
//
// On libcurl:
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
// Version: 0.5.0 - UltraNet's own client where libcurl has no WebSockets (macOS through 15, some
//                  distributions), and with ULTRANET_WEBSOCKET_CLIENT=own
// Version: 0.4.2 - through the proxy setting, as a CONNECT tunnel (UltraNetCurlProxy.h)
// Version: 0.4.1 - options.maxMessageBytes: a message past it ends the connection (1009) before it is
//                  assembled; the reconnect options, which nothing read, removed
// Version: 0.4.0 - PINGs answered; pingIntervalMs pings and notices a silent peer; reads and writes
//                  of the one easy handle under one mutex; a frame larger than a read is delivered
//                  whole, not as one message per read
// Version: 0.3.0 (Stage 3)
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraNet/UltraNetWebSocket.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraNet/UltraNetCore.h"
#include "UltraNet/UltraNetCurlProxy.h"
#include "UltraNetWebSocketOwn.h"

#include <curl/curl.h>
#include <curl/websockets.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using ultranet_internal::OwnWebSocket;

struct WsConnection {
    UltraNetHandle handle = UltraNetInvalidHandle;
    CURL* easy = nullptr;
    curl_slist* slist = nullptr;
    // UltraNet's own client, when this connection is not libcurl's (then
    // `easy` is null). Declared before the receiver: closed after it stopped.
    std::unique_ptr<OwnWebSocket> own;
    std::mutex ioMutex;                 // every read and write of the one connection
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

// One frame out on UltraNet's own client, under the I/O mutex.
bool OwnSendLocked(WsConnection& c, uint8_t opcode, const void* data, std::size_t len) {
    std::lock_guard<std::mutex> lk(c.ioMutex);
    return c.own && c.own->Send(opcode, data, len);
}

// The receiver of a connection on UltraNet's own client: what ReceiverLoop
// is for libcurl's - the same callbacks for the same things.
void OwnReceiverLoop(std::shared_ptr<WsConnection> c) {
    std::vector<uint8_t> message;       // a message's frames so far
    uint8_t messageOpcode = 0;
    bool inMessage = false;

    using Clock = std::chrono::steady_clock;
    Clock::time_point lastHeard = Clock::now();     // anything from the peer
    Clock::time_point lastPing = lastHeard;
    const auto interval = std::chrono::milliseconds(c->pingIntervalMs);

    // The connection ends here for a reason of this side's: the other side
    // is told with a close frame where there is still someone to tell.
    const auto fail = [&](int closeCode, const std::string& closeReason, const std::string& why) {
        if (closeCode > 0 && c->state.load(std::memory_order_acquire) == UltraNetWebSocketState::Open) {
            std::vector<uint8_t> payload = {static_cast<uint8_t>(closeCode >> 8), static_cast<uint8_t>(closeCode)};
            payload.insert(payload.end(), closeReason.begin(), closeReason.end());
            OwnSendLocked(*c, OwnWebSocket::Close, payload.data(), payload.size());
        }
        const auto cbs = CallbacksSnapshot();
        c->state.store(UltraNetWebSocketState::Closed, std::memory_order_release);
        if (cbs.onError) cbs.onError(c->handle, why);
    };

    while (c->running.load(std::memory_order_acquire)) {
        OwnWebSocket::Frame frame;
        std::string error;
        OwnWebSocket::Read read;
        {
            std::lock_guard<std::mutex> lk(c->ioMutex);
            // What the message may still grow by; past it the frame is not kept.
            std::size_t allowance = std::numeric_limits<std::size_t>::max();
            if (c->maxMessageBytes > 0)
                allowance = c->maxMessageBytes > message.size() ? c->maxMessageBytes - message.size() : 0;
            // A short wait: a sender takes the same mutex.
            read = c->own->Receive(10, allowance, frame, error);
        }
        if (read == OwnWebSocket::Read::Idle) {
            if (c->pingIntervalMs > 0) {
                const Clock::time_point now = Clock::now();
                if (now - lastHeard > 2 * interval) {
                    // Not even the PONG: the connection is gone without a
                    // close (a network that dropped, a peer that died).
                    fail(0, std::string(), "no answer from the other side for " +
                                               std::to_string(2 * c->pingIntervalMs) + " ms");
                    break;
                }
                if (now - lastPing >= interval &&
                    c->state.load(std::memory_order_acquire) == UltraNetWebSocketState::Open) {
                    lastPing = now;
                    OwnSendLocked(*c, OwnWebSocket::Ping, "", 0);
                }
            }
            // Out of the mutex for a moment, for whoever waits to send.
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        if (read == OwnWebSocket::Read::Partial) {
            lastHeard = Clock::now();
            std::this_thread::yield();
            continue;
        }
        if (read == OwnWebSocket::Read::Closed) {
            fail(0, std::string(), "the other side closed the connection without a close frame");
            break;
        }
        if (read == OwnWebSocket::Read::Broken) {
            fail(0, std::string(), error);
            break;
        }
        if (read == OwnWebSocket::Read::Malformed) {
            fail(1002, "protocol error", "not the WebSocket protocol: " + error);
            break;
        }
        if (read == OwnWebSocket::Read::TooBig) {
            // Not assembled: the connection ends (1009, message too big)
            // before the memory is spent.
            fail(1009, "message too big", "a message larger than " + std::to_string(c->maxMessageBytes) +
                                              " bytes (UltraNetWebSocketOptions::maxMessageBytes)");
            break;
        }

        lastHeard = Clock::now();
        const auto cbs = CallbacksSnapshot();
        if (frame.opcode == OwnWebSocket::Ping) {
            // Answered at once, with the same payload (RFC 6455 5.5.3).
            if (c->state.load(std::memory_order_acquire) == UltraNetWebSocketState::Open)
                OwnSendLocked(*c, OwnWebSocket::Pong, frame.payload.data(), frame.payload.size());
            if (cbs.onPing) cbs.onPing(c->handle, frame.payload);
            continue;
        }
        if (frame.opcode == OwnWebSocket::Pong) {
            if (cbs.onPong) cbs.onPong(c->handle, frame.payload);
            continue;
        }
        if (frame.opcode == OwnWebSocket::Close) {
            int code = 1005;
            std::string reason;
            if (frame.payload.size() >= 2) {
                code = (frame.payload[0] << 8) | frame.payload[1];
                reason.assign(frame.payload.begin() + 2, frame.payload.end());
            }
            // The close is answered with a close (5.5.1), its code back.
            if (c->state.load(std::memory_order_acquire) == UltraNetWebSocketState::Open)
                OwnSendLocked(*c, OwnWebSocket::Close, frame.payload.data(), std::min<std::size_t>(frame.payload.size(), 2));
            c->state.store(UltraNetWebSocketState::Closed, std::memory_order_release);
            if (cbs.onClose) cbs.onClose(c->handle, code, reason);
            break;
        }

        // Data: a message is its first frame (text or binary) and whatever
        // continuation frames follow, until one says it is the last.
        if (frame.opcode == OwnWebSocket::Continuation) {
            if (!inMessage) {
                fail(1002, "protocol error", "not the WebSocket protocol: a continuation of no message");
                break;
            }
        } else {
            if (inMessage) {
                fail(1002, "protocol error", "not the WebSocket protocol: a new message inside an unfinished one");
                break;
            }
            inMessage = true;
            messageOpcode = frame.opcode;
            message.clear();
        }
        if (message.empty() && frame.fin) message.swap(frame.payload);
        else message.insert(message.end(), frame.payload.begin(), frame.payload.end());
        if (!frame.fin) continue;
        inMessage = false;
        if (messageOpcode == OwnWebSocket::Text) {
            if (cbs.onText) cbs.onText(c->handle, std::string(message.begin(), message.end()));
        } else {
            if (cbs.onBinary) cbs.onBinary(c->handle, message);
        }
        message.clear();
    }
}

// Whether the environment asks for UltraNet's own client where libcurl has
// WebSockets too.
bool OwnClientAskedFor() {
    return UltraCanvas::GetEnvUtf8("ULTRANET_WEBSOCKET_CLIENT") == "own";
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

    // A libcurl without WebSockets (Apple's through macOS 15, some
    // distributions'): UltraNet's own client, on its own sockets and TLS.
    if (!LibcurlHasWebSockets() || OwnClientAskedFor()) {
        auto c = std::make_shared<WsConnection>();
        c->handle = g_nextHandle.fetch_add(1, std::memory_order_relaxed);
        c->state.store(UltraNetWebSocketState::Connecting, std::memory_order_release);
        c->pingIntervalMs = options.pingIntervalMs > 0 ? options.pingIntervalMs : 0;
        c->maxMessageBytes = options.maxMessageBytes;
        std::string error;
        c->own = OwnWebSocket::Open(url, options, error);
        if (!c->own) {
            const auto cbs = CallbacksSnapshot();
            c->state.store(UltraNetWebSocketState::Error, std::memory_order_release);
            if (cbs.onError) cbs.onError(c->handle, error);
            return UltraNetInvalidHandle;
        }
        c->state.store(UltraNetWebSocketState::Open, std::memory_order_release);
        const UltraNetHandle h = c->handle;
        {
            std::lock_guard<std::mutex> lk(G().regMutex);
            G().connections[h] = c;
        }
        const auto cbs = CallbacksSnapshot();
        if (cbs.onOpen) cbs.onOpen(h);
        c->Start([c]() { OwnReceiverLoop(c); });
        return h;
    }

    auto c = std::make_shared<WsConnection>();
    c->easy = curl_easy_init();
    if (!c->easy) return UltraNetInvalidHandle;
    c->handle = g_nextHandle.fetch_add(1, std::memory_order_relaxed);
    c->state.store(UltraNetWebSocketState::Connecting, std::memory_order_release);

    curl_easy_setopt(c->easy, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c->easy, CURLOPT_CONNECT_ONLY, 2L);   // WebSocket connect-only
    // The proxy setting; through an HTTP proxy the upgrade goes in a CONNECT
    // tunnel, which every proxy passes, rather than as a proxied GET.
    ultranet_curlproxy::ApplyFor(c->easy, url, /*tunnel=*/true);
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
        if (c.own) {
            const uint8_t opcode = (flags & CURLWS_TEXT)    ? OwnWebSocket::Text
                                   : (flags & CURLWS_BINARY) ? OwnWebSocket::Binary
                                   : (flags & CURLWS_PING)   ? OwnWebSocket::Ping
                                   : (flags & CURLWS_PONG)   ? OwnWebSocket::Pong
                                                             : OwnWebSocket::Close;
            if (!OwnSendLocked(c, opcode, data, len)) {
                return UltraNetResult::Error(UltraNetResultCode::SendFailed,
                                             "the connection did not take the frame");
            }
            return UltraNetResult::Ok();
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
