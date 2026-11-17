// Plugins/UltraIM/xmpp/XmppStream.cpp
// XMPP - an XML stream over UltraNet's WebSocket (RFC 7395), with the
// process-wide WebSocket callbacks shared with whoever installed them before
// Version: 0.1.0
// Last Modified: 2026-11-17
// Author: UltraCanvas Framework

#include "XmppStream.h"

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
    std::list<Waiter*> waiters;
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

void Deliver(const std::shared_ptr<XmppStream::Shared>& shared, const std::string& text) {
    XmlElement element;
    if (!XmlParse(text, element)) return;
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

void SocketGone(const std::shared_ptr<XmppStream::Shared>& shared, UltraNetHandle handle, const std::string& why) {
    // (The handle stays: Close() hands it back to UltraNet, which keeps a
    // connection until it is closed.)
    Unroute(handle);
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

bool XmppStream::IsOpen() const {
    std::lock_guard<std::mutex> lock(shared_->mutex);
    return shared_->socket != UltraNetInvalidHandle && !shared_->ended && !shared_->stopping;
}

bool XmppStream::Send(const XmlElement& element) {
    UltraNetHandle handle;
    {
        std::lock_guard<std::mutex> lock(shared_->mutex);
        if (shared_->ended || shared_->stopping) return false;
        handle = shared_->socket;
    }
    if (handle == UltraNetInvalidHandle) return false;
    XmlElement copy = element;
    // RFC 7395: each top-level stanza carries its namespace.
    if (copy.ns.empty()) copy.ns = Ns::Client;
    return static_cast<bool>(UltraNet_WebSocketSendText(handle, copy.Serialize()));
}

bool XmppStream::Exchange(const XmlElement& element, const std::function<bool(const XmlElement&)>& match,
                          XmlElement& reply, int timeoutMs) {
    Shared::Waiter waiter;
    waiter.match = match;
    {
        std::lock_guard<std::mutex> lock(shared_->mutex);
        if (shared_->ended || shared_->stopping) return false;
        shared_->waiters.push_back(&waiter);
    }
    bool ok = Send(element);
    std::unique_lock<std::mutex> lock(shared_->mutex);
    if (ok) {
        shared_->changed.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                                  [&]() { return waiter.done || shared_->ended || shared_->stopping; });
        ok = waiter.done;
        if (ok) reply = std::move(waiter.got);
    }
    shared_->waiters.remove(&waiter);
    return ok;
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
    bool ended = false;
    {
        std::lock_guard<std::mutex> lock(shared_->mutex);
        if (shared_->stopping) return;
        shared_->stopping = true;
        ended = shared_->ended;
        handle = shared_->socket;
        shared_->socket = UltraNetInvalidHandle;
        shared_->changed.notify_all();
    }
    if (handle != UltraNetInvalidHandle) {
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
