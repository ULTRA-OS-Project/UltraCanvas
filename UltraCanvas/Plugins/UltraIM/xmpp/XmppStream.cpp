// Plugins/UltraIM/xmpp/XmppStream.cpp
// XMPP - an XML stream over UltraNet's WebSocket (RFC 7395), with the
// process-wide WebSocket callbacks shared with whoever installed them before,
// or over TCP (RFC 6120) with direct TLS or STARTTLS, read and written by a
// thread of its own
// Version: 0.3.0 - TCP: an XMPP ping (XEP-0199) after a silence; no answer in time ends the stream
// Last Modified: 2026-11-17
// Version: 0.2.0 - the TCP binding: ConnectTcp, StartTls, OpenStream, the whitespace keepalive
// Version: 0.1.0
// Author: UltraCanvas Framework

#include "XmppStream.h"

#include "XmppTcp.h"

#include "UltraNet/UltraNetWebSocket.h"

#include <chrono>
#include <condition_variable>
#include <list>
#include <map>
#include <mutex>

namespace UltraIMXmpp {

// ============================================================================
// SHARED STATE
// ============================================================================

struct XmppStream::Shared {
    struct Waiter {
        std::function<bool(const XmlElement&)> match;
        bool done = false;
        XmlElement got;
    };

    std::mutex mutex;
    std::condition_variable changed;
    UltraNetHandle socket = UltraNetInvalidHandle;
    bool stopping = false;          // Close() was called
    bool ended = false;             // the socket or the stream is gone
    bool droppedTold = false;
    int inFlight = 0;               // callbacks running now
    std::string connectError;
    bool tls = false;               // wss://, direct TLS, STARTTLS done
    std::list<Waiter*> waiters;
    // TCP.
    bool tcp = false;
    bool connectDone = false;       // the connection's thread has connected (or failed to)
    bool certificateRefused = false;
    std::string outbox;             // what the connection's thread writes next
    std::string header;             // the server's stream header (its namespaces)
    bool tlsArmed = false;          // <starttls/> sent: TLS when <proceed/> comes
    std::string tlsHost;
    UltraNetTlsOptions tlsOptions;
    bool tlsDone = false;
    std::string tlsError;
    bool tlsCertificateRefused = false;
    std::string pingTo;             // TCP: whom to ping after a silence ("" = not yet: before sign-in)
    int pingTimeoutMs = 0;
    std::function<void(const XmlElement&)> onElement;
    std::function<void(const std::string&)> onDropped;

    // The stream ended by itself: the waiters wake, the owner hears it once.
    void Ended(const std::string& why) {
        std::function<void(const std::string&)> tell;
        {
            std::lock_guard<std::mutex> lock(mutex);
            ended = true;
            changed.notify_all();
            if (stopping || droppedTold) return;
            droppedTold = true;
            tell = onDropped;
        }
        if (tell) tell(why);
    }
};

namespace {

// ---- the process-wide WebSocket callbacks ------------------------------------

std::mutex g_routeMutex;
std::map<UltraNetHandle, std::weak_ptr<XmppStream::Shared>>& Routes() {
    static auto* routes = new std::map<UltraNetHandle, std::weak_ptr<XmppStream::Shared>>();
    return *routes;
}
UltraNetWebSocketCallbacks g_previous;
bool g_routerInstalled = false;
// The stream connecting on this thread: onOpen (and a failed connect's
// onError) arrive on the connecting thread before the handle is returned.
thread_local std::weak_ptr<XmppStream::Shared> t_connecting;
// Set while this thread runs a stream's callback: Close() must not wait for
// the callback it is called from.
thread_local bool t_inCallback = false;

std::shared_ptr<XmppStream::Shared> RouteFor(UltraNetHandle handle) {
    std::lock_guard<std::mutex> lock(g_routeMutex);
    auto it = Routes().find(handle);
    return it == Routes().end() ? nullptr : it->second.lock();
}

void Unroute(UltraNetHandle handle) {
    std::lock_guard<std::mutex> lock(g_routeMutex);
    Routes().erase(handle);
}

void DeliverElement(const std::shared_ptr<XmppStream::Shared>& shared, const XmlElement& element) {
    std::function<void(const XmlElement&)> handler;
    bool claimed = false;
    {
        std::lock_guard<std::mutex> lock(shared->mutex);
        if (shared->stopping) return;
        ++shared->inFlight;
        handler = shared->onElement;
        if (element.Is("iq") && (element.Attr("type") == "result" || element.Attr("type") == "error")) {
            for (auto* w : shared->waiters) {
                if (!w->done && w->match(element)) claimed = true;
            }
        }
    }
    t_inCallback = true;
    // First the owner (a presence that completes a room's join is in its
    // list before the worker waiting for it goes on), then the waiters.
    if (handler && !claimed) handler(element);
    t_inCallback = false;
    const bool streamEnds = (element.Is("close", Ns::Framing)) || element.Is("error", Ns::Streams);
    {
        std::lock_guard<std::mutex> lock(shared->mutex);
        for (auto* w : shared->waiters) {
            if (!w->done && w->match(element)) {
                w->done = true;
                w->got = element;
            }
        }
        shared->changed.notify_all();
    }
    if (streamEnds) {
        std::string why = "the server ended the stream";
        if (element.Is("error", Ns::Streams) && !element.children.empty()) why += " (" + element.children.front().name + ")";
        t_inCallback = true;
        shared->Ended(why);
        t_inCallback = false;
    }
    std::lock_guard<std::mutex> lock(shared->mutex);
    if (--shared->inFlight == 0) shared->changed.notify_all();
}

void Deliver(const std::shared_ptr<XmppStream::Shared>& shared, const std::string& text) {
    XmlElement element;
    if (XmlParse(text, element)) DeliverElement(shared, element);
}

// The stream ended by itself (not by Close()): the owner told, as from a callback.
void EndedByItself(const std::shared_ptr<XmppStream::Shared>& shared, const std::string& why) {
    {
        std::lock_guard<std::mutex> lock(shared->mutex);
        ++shared->inFlight;
    }
    t_inCallback = true;
    shared->Ended(why);
    t_inCallback = false;
    std::lock_guard<std::mutex> lock(shared->mutex);
    if (--shared->inFlight == 0) shared->changed.notify_all();
}

void SocketGone(const std::shared_ptr<XmppStream::Shared>& shared, UltraNetHandle handle, const std::string& why) {
    // (The handle stays: Close() hands it back to UltraNet, which keeps a
    // connection until it is closed.)
    Unroute(handle);
    EndedByItself(shared, why);
}

void InstallRouter() {
    std::lock_guard<std::mutex> lock(g_routeMutex);
    if (g_routerInstalled) return;
    g_routerInstalled = true;
    UltraNetWebSocketCallbacks router;
    router.onOpen = [](UltraNetHandle handle) {
        if (auto shared = t_connecting.lock()) {
            std::lock_guard<std::mutex> routeLock(g_routeMutex);
            Routes()[handle] = shared;
            return;
        }
        if (g_previous.onOpen) g_previous.onOpen(handle);
    };
    router.onText = [](UltraNetHandle handle, const std::string& text) {
        if (auto shared = RouteFor(handle)) {
            Deliver(shared, text);
            return;
        }
        if (g_previous.onText) g_previous.onText(handle, text);
    };
    router.onBinary = [](UltraNetHandle handle, const std::vector<uint8_t>& data) {
        if (RouteFor(handle)) return;       // RFC 7395 has no binary frames
        if (g_previous.onBinary) g_previous.onBinary(handle, data);
    };
    router.onClose = [](UltraNetHandle handle, int code, const std::string& reason) {
        if (auto shared = RouteFor(handle)) {
            SocketGone(shared, handle, "the connection closed (" + std::to_string(code) + (reason.empty() ? "" : " " + reason) + ")");
            return;
        }
        if (g_previous.onClose) g_previous.onClose(handle, code, reason);
    };
    router.onError = [](UltraNetHandle handle, const std::string& message) {
        if (auto shared = RouteFor(handle)) {
            SocketGone(shared, handle, message);
            return;
        }
        // A connect of ours that failed: its handle was never routed.
        if (auto shared = t_connecting.lock()) {
            std::lock_guard<std::mutex> sharedLock(shared->mutex);
            shared->connectError = message;
            return;
        }
        if (g_previous.onError) g_previous.onError(handle, message);
    };
    router.onPing = [](UltraNetHandle handle, const std::vector<uint8_t>& data) {
        if (!RouteFor(handle) && g_previous.onPing) g_previous.onPing(handle, data);
    };
    router.onPong = [](UltraNetHandle handle, const std::vector<uint8_t>& data) {
        if (!RouteFor(handle) && g_previous.onPong) g_previous.onPong(handle, data);
    };
    g_previous = UltraNet_WebSocketSetCallbacks(router);
}

// ---- TCP: the connection's own thread ------------------------------------------

// How long one read waits; what is to be written goes between reads.
constexpr int kReadSliceMs = 25;
// The ids of the stream's own pings (their answers are not handed on).
constexpr const char* kPingIdPrefix = "cc-tcp-ping-";

struct TcpTarget {
    std::string host;
    int port = 0;
    bool directTls = false;
    std::string tlsHost;
    UltraNetTlsOptions tlsOptions;
    int timeoutMs = 15000;
    int keepaliveMs = 0;
};

void TcpLoop(std::shared_ptr<XmppStream::Shared> shared, TcpTarget target) {
    XmppTcpConnection conn;
    std::string error;
    bool refused = false;
    const bool opened = conn.Open(target.host, target.port, target.directTls, target.tlsHost, target.tlsOptions,
                                  target.timeoutMs, error, refused);
    {
        std::lock_guard<std::mutex> lock(shared->mutex);
        shared->connectDone = true;
        shared->connectError = opened ? std::string() : error;
        shared->certificateRefused = refused;
        shared->tls = opened && target.directTls;
        if (!opened) shared->ended = true;
        shared->changed.notify_all();
    }
    if (!opened) return;
    using Clock = std::chrono::steady_clock;
    XmppSplitter splitter;
    auto lastSent = Clock::now();
    auto lastReceived = Clock::now();
    std::string pingId;             // the ping waiting for its answer ("" = none)
    auto pingSentAt = Clock::now();
    int pingCount = 0;
    std::string why;                // why the stream ended by itself ("": Close())
    bool sayGoodbye = true;         // </stream:stream> before the socket closes
    for (;;) {
        std::string out;
        bool stopping = false, ended = false;
        std::string pingTo;
        int pingTimeoutMs = 0;
        {
            std::lock_guard<std::mutex> lock(shared->mutex);
            out.swap(shared->outbox);
            stopping = shared->stopping;
            ended = shared->ended;
            pingTo = shared->pingTo;
            pingTimeoutMs = shared->pingTimeoutMs;
        }
        if (!out.empty()) {
            if (!conn.Send(out)) {
                why = "the connection broke (a write failed)";
                sayGoodbye = false;
                break;
            }
            lastSent = Clock::now();
        }
        if (stopping || ended) break;
        if (target.keepaliveMs > 0 &&
            Clock::now() - lastSent >= std::chrono::milliseconds(target.keepaliveMs)) {
            // RFC 6120 4.6.1: a single space keeps the connection (and tells one that is gone).
            if (!conn.Send(" ")) {
                why = "the connection broke (the keepalive could not be written)";
                sayGoodbye = false;
                break;
            }
            lastSent = Clock::now();
        }
        // A server that has said nothing for a while is asked (XEP-0199): the
        // whitespace keepalive only finds a connection that is gone when a
        // write fails, which a server that hangs, or a path that drops
        // packets silently, never makes happen.
        if (!pingId.empty() && Clock::now() - pingSentAt >= std::chrono::milliseconds(pingTimeoutMs)) {
            why = "the server stopped answering (no answer to a ping within " +
                  std::to_string((pingTimeoutMs + 999) / 1000) + " s)";
            sayGoodbye = false;
            break;
        }
        if (pingId.empty() && !pingTo.empty() && pingTimeoutMs > 0 && target.keepaliveMs > 0 &&
            Clock::now() - lastReceived >= std::chrono::milliseconds(target.keepaliveMs)) {
            pingId = std::string(kPingIdPrefix) + std::to_string(++pingCount);
            XmlElement iq("iq", Ns::Client);
            iq.Set("type", "get").Set("id", pingId).Set("to", pingTo);
            iq.Add("ping", Ns::Ping);
            if (!conn.Send(iq.Serialize(Ns::Client))) {
                why = "the connection broke (a ping could not be written)";
                sayGoodbye = false;
                break;
            }
            pingSentAt = lastSent = Clock::now();
        }
        std::string data;
        const XmppTcpConnection::ReadStatus status = conn.Read(kReadSliceMs, data);
        if (status == XmppTcpConnection::ReadStatus::Closed) {
            why = "the connection closed";
            sayGoodbye = false;
            break;
        }
        if (status == XmppTcpConnection::ReadStatus::Idle) continue;
        // Anything from the server shows it is there.
        lastReceived = Clock::now();
        pingId.clear();
        std::vector<XmppSplitter::Item> items;
        splitter.Feed(data, items);
        for (const XmppSplitter::Item& item : items) {
            if (item.kind == XmppSplitter::Kind::Header) {
                std::lock_guard<std::mutex> lock(shared->mutex);
                shared->header = item.text;
                continue;
            }
            if (item.kind == XmppSplitter::Kind::End) {
                why = "the server ended the stream";
                break;
            }
            if (item.kind == XmppSplitter::Kind::Error) {
                why = "the server's XML is not well-formed (" + item.text + ")";
                break;
            }
            std::string header;
            bool armed = false;
            {
                std::lock_guard<std::mutex> lock(shared->mutex);
                header = shared->header;
                armed = shared->tlsArmed;
            }
            XmlElement element;
            std::string parseError;
            if (!XmppParseStreamElement(header, item.text, element, &parseError)) {
                why = "the server's XML is not well-formed (" + parseError + ")";
                break;
            }
            // The answers to our own pings are ours alone.
            if (element.Is("iq") && element.Attr("id").rfind(kPingIdPrefix, 0) == 0) continue;
            if (armed && element.Is("proceed", Ns::Tls)) {
                // TLS before anything else is read: whatever came with the
                // <proceed/> is not trusted (and dropped).
                std::string tlsHost, tlsError;
                UltraNetTlsOptions tlsOptions;
                {
                    std::lock_guard<std::mutex> lock(shared->mutex);
                    tlsHost = shared->tlsHost;
                    tlsOptions = shared->tlsOptions;
                    shared->tlsArmed = false;
                }
                bool tlsRefused = false;
                const bool ok = conn.StartTls(tlsHost, tlsOptions, tlsError, tlsRefused);
                splitter.Reset();
                {
                    std::lock_guard<std::mutex> lock(shared->mutex);
                    shared->tlsDone = true;
                    shared->tls = ok;
                    shared->tlsError = ok ? std::string() : tlsError;
                    shared->tlsCertificateRefused = tlsRefused;
                }
                if (!ok) {
                    why = tlsError;
                    sayGoodbye = false;
                    break;
                }
                lastSent = Clock::now();
                DeliverElement(shared, element);
                break;
            }
            DeliverElement(shared, element);
        }
        if (!why.empty()) break;
    }
    // Closed, or ended by the server (its end tag, a stream error): ours too.
    if (sayGoodbye) conn.Send("</stream:stream>");
    conn.Close();
    if (!why.empty()) EndedByItself(shared, why);
}

} // namespace

// ============================================================================
// STREAM
// ============================================================================

XmppStream::XmppStream() : shared_(std::make_shared<Shared>()) {}

XmppStream::~XmppStream() {
    Close();
}

bool XmppStream::Connect(const std::string& url, int connectTimeoutMs, int pingIntervalMs, std::string& error) {
    InstallRouter();
    {
        std::lock_guard<std::mutex> lock(shared_->mutex);
        shared_->onElement = onElement;
        shared_->onDropped = onDropped;
        shared_->connectError.clear();
        shared_->tcp = false;
        shared_->tls = url.rfind("wss://", 0) == 0;
    }
    UltraNetWebSocketOptions options;
    options.subprotocols = {"xmpp"};
    options.connectTimeoutMs = connectTimeoutMs;
    options.pingIntervalMs = pingIntervalMs;
    options.maxMessageBytes = 16 * 1024 * 1024;     // a stanza, not a file
    t_connecting = shared_;
    const UltraNetHandle handle = UltraNet_WebSocketConnect(url, options);
    t_connecting.reset();
    std::lock_guard<std::mutex> lock(shared_->mutex);
    if (handle == UltraNetInvalidHandle) {
        error = shared_->connectError.empty() ? "could not connect to " + url : shared_->connectError;
        return false;
    }
    shared_->socket = handle;       // (closed by Close(), whatever happens next)
    if (shared_->stopping || shared_->ended) {
        // Gone already (a close right after the upgrade).
        error = "the connection closed at once";
        return false;
    }
    return true;
}

void XmppStream::StartPinging(const std::string& to, int timeoutMs) {
    std::lock_guard<std::mutex> lock(shared_->mutex);
    shared_->pingTo = to;
    shared_->pingTimeoutMs = timeoutMs;
}

bool XmppStream::ConnectTcp(const std::string& host, int port, bool directTls, const std::string& tlsHost,
                            const UltraNetTlsOptions& tlsOptions, int connectTimeoutMs, int keepaliveMs,
                            std::string& error, bool& certificateRefused) {
    certificateRefused = false;
    TcpTarget target;
    target.host = host;
    target.port = port;
    target.directTls = directTls;
    target.tlsHost = tlsHost;
    target.tlsOptions = tlsOptions;
    target.timeoutMs = connectTimeoutMs;
    target.keepaliveMs = keepaliveMs;
    std::unique_lock<std::mutex> lock(shared_->mutex);
    if (shared_->stopping || io_.joinable()) {
        error = shared_->stopping ? "the stream was closed" : "the stream is in use";
        return false;
    }
    shared_->onElement = onElement;
    shared_->onDropped = onDropped;
    shared_->connectError.clear();
    shared_->tcp = true;
    shared_->tls = false;
    shared_->connectDone = false;
    // (Started under the lock: a Close() from another thread finds it, or stops it starting.)
    io_ = std::thread(TcpLoop, shared_, target);
    // (The connect and the handshake are bounded by connectTimeoutMs each.)
    shared_->changed.wait_for(lock, std::chrono::milliseconds(2 * connectTimeoutMs + 5000),
                              [this]() { return shared_->connectDone || shared_->stopping; });
    if (!shared_->connectDone) {
        error = "no connection within " + std::to_string(connectTimeoutMs / 1000) + " s";
        return false;
    }
    if (!shared_->connectError.empty() || shared_->ended) {
        error = shared_->connectError.empty() ? "the connection closed at once" : shared_->connectError;
        certificateRefused = shared_->certificateRefused;
        return false;
    }
    return true;
}

bool XmppStream::IsOpen() const {
    std::lock_guard<std::mutex> lock(shared_->mutex);
    const bool connected = shared_->tcp ? shared_->connectDone : shared_->socket != UltraNetInvalidHandle;
    return connected && !shared_->ended && !shared_->stopping;
}

bool XmppStream::IsTcp() const {
    std::lock_guard<std::mutex> lock(shared_->mutex);
    return shared_->tcp;
}

bool XmppStream::IsTls() const {
    std::lock_guard<std::mutex> lock(shared_->mutex);
    return shared_->tls;
}

namespace {

// Text onto the stream: a WebSocket frame, or the TCP connection's next write.
bool SendText(const std::shared_ptr<XmppStream::Shared>& shared, const std::string& text) {
    UltraNetHandle handle;
    {
        std::lock_guard<std::mutex> lock(shared->mutex);
        if (shared->ended || shared->stopping) return false;
        if (shared->tcp) {
            shared->outbox += text;
            return true;
        }
        handle = shared->socket;
    }
    if (handle == UltraNetInvalidHandle) return false;
    return static_cast<bool>(UltraNet_WebSocketSendText(handle, text));
}

// Sends `text` and waits for the first element `match` accepts.
bool ExchangeText(const std::shared_ptr<XmppStream::Shared>& shared, const std::string& text,
                  const std::function<bool(const XmlElement&)>& match, XmlElement& reply, int timeoutMs) {
    XmppStream::Shared::Waiter waiter;
    waiter.match = match;
    {
        std::lock_guard<std::mutex> lock(shared->mutex);
        if (shared->ended || shared->stopping) return false;
        shared->waiters.push_back(&waiter);
    }
    bool ok = SendText(shared, text);
    std::unique_lock<std::mutex> lock(shared->mutex);
    if (ok) {
        shared->changed.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                                 [&]() { return waiter.done || shared->ended || shared->stopping; });
        ok = waiter.done;
        if (ok) reply = std::move(waiter.got);
    }
    shared->waiters.remove(&waiter);
    return ok;
}

} // namespace

std::string XmppStream::Serialized(const XmlElement& element) const {
    XmlElement copy = element;
    // RFC 7395: each top-level stanza carries its namespace; over TCP
    // jabber:client is the stream's default namespace, not said again.
    if (copy.ns.empty()) copy.ns = Ns::Client;
    return IsTcp() ? copy.Serialize(Ns::Client) : copy.Serialize();
}

bool XmppStream::Send(const XmlElement& element) {
    return SendText(shared_, Serialized(element));
}

bool XmppStream::OpenStream(const std::string& domain, XmlElement& features, int timeoutMs) {
    const auto isFeatures = [](const XmlElement& e) { return e.Is("features", Ns::Streams); };
    if (IsTcp()) return ExchangeText(shared_, XmppStreamHeader(domain), isFeatures, features, timeoutMs);
    XmlElement open("open", Ns::Framing);
    open.Set("to", domain).Set("version", "1.0").Set("xml:lang", "en");
    return Exchange(open, isFeatures, features, timeoutMs);
}

bool XmppStream::StartTls(const std::string& tlsHost, const UltraNetTlsOptions& tlsOptions, int timeoutMs,
                          std::string& error, bool& certificateRefused) {
    certificateRefused = false;
    {
        std::lock_guard<std::mutex> lock(shared_->mutex);
        if (!shared_->tcp || shared_->tls) {
            error = shared_->tls ? "TLS is on already" : "STARTTLS is for TCP";
            return false;
        }
        shared_->tlsArmed = true;
        shared_->tlsHost = tlsHost;
        shared_->tlsOptions = tlsOptions;
        shared_->tlsDone = false;
        shared_->tlsError.clear();
        shared_->tlsCertificateRefused = false;
    }
    XmlElement reply;
    const bool answered = Exchange(
        XmlElement("starttls", Ns::Tls),
        [](const XmlElement& e) { return e.ns == Ns::Tls && (e.name == "proceed" || e.name == "failure"); }, reply,
        timeoutMs);
    std::lock_guard<std::mutex> lock(shared_->mutex);
    shared_->tlsArmed = false;
    if (shared_->tlsDone && !shared_->tls) {
        error = shared_->tlsError;
        certificateRefused = shared_->tlsCertificateRefused;
        return false;
    }
    if (!answered) {
        error = "the server did not answer STARTTLS";
        return false;
    }
    if (reply.name != "proceed") {
        error = "the server refused STARTTLS";
        return false;
    }
    return shared_->tls;
}

bool XmppStream::Exchange(const XmlElement& element, const std::function<bool(const XmlElement&)>& match,
                          XmlElement& reply, int timeoutMs) {
    return ExchangeText(shared_, Serialized(element), match, reply, timeoutMs);
}

bool XmppStream::Iq(XmlElement iq, XmlElement& reply, int timeoutMs) {
    if (iq.Attr("id").empty()) iq.Set("id", "uc" + RandomId(12));
    const std::string id = iq.Attr("id");
    const bool ok = Exchange(
        iq,
        [id](const XmlElement& e) {
            return e.Is("iq") && e.Attr("id") == id && (e.Attr("type") == "result" || e.Attr("type") == "error");
        },
        reply, timeoutMs);
    if (!ok) {
        reply = XmlElement("iq", Ns::Client);
        reply.Set("type", "error");
        reply.Add("error").Add("remote-server-timeout", Ns::Stanzas);
    }
    return ok;
}

void XmppStream::Close() {
    UltraNetHandle handle;
    bool ended = false, tcp = false;
    std::thread io;
    {
        std::lock_guard<std::mutex> lock(shared_->mutex);
        if (shared_->stopping) return;
        shared_->stopping = true;
        ended = shared_->ended;
        tcp = shared_->tcp;
        handle = shared_->socket;
        shared_->socket = UltraNetInvalidHandle;
        io = std::move(io_);
        shared_->changed.notify_all();
    }
    if (tcp) {
        // The connection's thread writes what is queued, says
        // </stream:stream> and closes the socket.
        if (io.joinable()) {
            if (io.get_id() == std::this_thread::get_id()) io.detach();
            else io.join();
        }
    } else if (handle != UltraNetInvalidHandle) {
        if (!ended && UltraNet_WebSocketIsOpen(handle)) {
            XmlElement close("close", Ns::Framing);
            UltraNet_WebSocketSendText(handle, close.Serialize());
        }
        // (Joins the socket's thread, unless this is it.)
        UltraNet_WebSocketClose(handle);
        Unroute(handle);
    }
    if (t_inCallback) return;
    std::unique_lock<std::mutex> lock(shared_->mutex);
    shared_->changed.wait(lock, [this]() { return shared_->inFlight == 0; });
    shared_->onElement = nullptr;
    shared_->onDropped = nullptr;
}

std::string IqErrorText(const XmlElement& iq) {
    const XmlElement* error = iq.Child("error");
    if (!error) return iq.Attr("type") == "error" ? "error" : std::string();
    std::string condition, text;
    for (const XmlElement& c : error->children) {
        if (c.name == "text") text = c.text;
        else if (condition.empty()) condition = c.name;
    }
    if (condition.empty()) condition = "error";
    return text.empty() ? condition : condition + ": " + text;
}

} // namespace UltraIMXmpp
