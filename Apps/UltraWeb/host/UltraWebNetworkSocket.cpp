// Apps/UltraWeb/host/UltraWebNetworkSocket.cpp
// An app's WebSockets on UltraNet (MakeNetworkSockets in UltraWebSocket.h).
//
// UltraNet_WebSocketConnect blocks for the handshake, so each socket
// connects on a thread of its own. UltraNet reports every socket of the
// process through one set of callbacks; the router below installs itself in
// front of whatever set was there, takes the events of UltraWeb's sockets
// and hands the rest on. A socket is known to the router from the moment its
// handshake ends: UltraNet calls onOpen on the connecting thread before its
// receiver starts, and the router recognises the socket that thread is
// connecting - so no message can arrive for a handle it has not seen.
// Version: 0.1.0
// Last Modified: 2026-11-11
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraWebSocket.h"

#include "UltraNet/UltraNetWebSocket.h"

#include <atomic>
#include <map>
#include <mutex>
#include <thread>

namespace UltraWeb {

namespace {

class NetSocket;

// The socket this thread is connecting, while UltraNet_WebSocketConnect runs.
thread_local NetSocket* t_connecting = nullptr;

struct Router {
    std::mutex mutex;
    std::map<UltraNetHandle, std::weak_ptr<NetSocket>> sockets;
    UltraNetWebSocketCallbacks previous;
    bool installed = false;

    static Router& Get() {
        static Router* router = new Router();   // outlives every socket thread
        return *router;
    }
    std::shared_ptr<NetSocket> Find(UltraNetHandle handle) {
        std::lock_guard<std::mutex> lock(mutex);
        auto at = sockets.find(handle);
        return at == sockets.end() ? nullptr : at->second.lock();
    }
    void Add(UltraNetHandle handle, const std::shared_ptr<NetSocket>& socket) {
        std::lock_guard<std::mutex> lock(mutex);
        sockets[handle] = socket;
    }
    void Drop(UltraNetHandle handle) {
        std::lock_guard<std::mutex> lock(mutex);
        sockets.erase(handle);
    }
    void Install();
};

class NetSocket : public SocketConnection, public std::enable_shared_from_this<NetSocket> {
public:
    NetSocket(std::function<void(std::function<void()>)> post, SocketEvents events)
        : post_(std::move(post)), events_(std::move(events)) {}

    void Start(const SocketRequest& request) {
        auto self = shared_from_this();
        std::thread([self, request]() { self->Connect(request); }).detach();
    }

    bool Send(const std::vector<uint8_t>& data, bool binary) override {
        const UltraNetHandle handle = handle_.load();
        if (closed_ || !open_ || handle == UltraNetInvalidHandle) return false;
        const UltraNetResult result = binary ? UltraNet_WebSocketSendBinary(handle, data)
                                             : UltraNet_WebSocketSendText(handle, std::string(data.begin(), data.end()));
        return result.success;
    }

    void Close(int code, const std::string& reason) override {
        if (closed_.exchange(true)) return;
        open_ = false;
        // Still connecting: the connecting thread closes it once it has a handle.
        const UltraNetHandle handle = handle_.exchange(UltraNetInvalidHandle);
        if (handle == UltraNetInvalidHandle) return;
        Router::Get().Drop(handle);
        UltraNet_WebSocketClose(handle, code, reason);
    }

    // From UltraNet's threads (the router).
    void Opened(UltraNetHandle handle) {
        handle_ = handle;
        open_ = true;
        Router::Get().Add(handle, shared_from_this());
        Post([](NetSocket& s) { if (s.events_.onOpen) s.events_.onOpen(); });
    }
    void Received(std::vector<uint8_t> data, bool binary) {
        Post([data = std::move(data), binary](NetSocket& s) mutable {
            if (s.events_.onMessage) s.events_.onMessage(std::move(data), binary);
        });
    }
    void Ended(bool failed, int code, std::string text) {
        open_ = false;
        Post([failed, code, text = std::move(text)](NetSocket& s) {
            s.Release();
            if (failed) {
                const bool tooLarge = text.find("a message larger than") != std::string::npos;
                if (s.events_.onError) s.events_.onError(text, tooLarge);
            } else if (s.events_.onClose) {
                s.events_.onClose(code, text);
            }
        });
    }
    void SetConnectError(const std::string& error) { connectError_ = error; }

private:
    void Connect(const SocketRequest& request) {
        UltraNetWebSocketOptions options;
        options.headers.Set("Origin", request.origin);
        options.subprotocols = request.protocols;
        options.connectTimeoutMs = SocketRules::kConnectTimeoutMs;
        options.pingIntervalMs = SocketRules::kPingIntervalMs;
        options.maxMessageBytes = SocketRules::kMaxMessageBytes;
        Router::Get().Install();
        t_connecting = this;
        const UltraNetHandle handle = UltraNet_WebSocketConnect(request.url, options);
        t_connecting = nullptr;
        if (handle == UltraNetInvalidHandle) {
            const std::string error = connectError_.empty() ? "could not connect to " + request.url
                                                            : "could not connect to " + request.url + ": " + connectError_;
            Post([error](NetSocket& s) { if (s.events_.onError) s.events_.onError(error, false); });
            return;
        }
        // Closed while it connected: nobody wants it any more.
        if (closed_) {
            handle_ = UltraNetInvalidHandle;
            Router::Get().Drop(handle);
            UltraNet_WebSocketClose(handle, 1000, std::string());
        }
    }

    // UltraNet keeps a connection until it is closed, also one the other
    // side ended; the UI thread lets it go.
    void Release() {
        const UltraNetHandle handle = handle_.exchange(UltraNetInvalidHandle);
        if (handle == UltraNetInvalidHandle) return;
        Router::Get().Drop(handle);
        UltraNet_WebSocketClose(handle, 1000, std::string());
    }

    // Runs `task` on the UI thread unless the socket was closed by then.
    template <class Task> void Post(Task task) {
        std::weak_ptr<NetSocket> weak = weak_from_this();
        post_([weak, task = std::move(task)]() mutable {
            auto self = weak.lock();
            if (self && !self->closed_) task(*self);
        });
    }

    std::function<void(std::function<void()>)> post_;
    SocketEvents events_;                       // used on the UI thread only
    std::atomic<bool> closed_{false};
    std::atomic<bool> open_{false};
    std::atomic<UltraNetHandle> handle_{UltraNetInvalidHandle};
    std::string connectError_;                  // the connecting thread's
};

void Router::Install() {
    std::lock_guard<std::mutex> lock(mutex);
    if (installed) return;
    installed = true;
    UltraNetWebSocketCallbacks router;
    router.onOpen = [](UltraNetHandle handle) {
        if (NetSocket* socket = t_connecting) { socket->Opened(handle); return; }
        auto& r = Get();
        if (r.previous.onOpen) r.previous.onOpen(handle);
    };
    router.onText = [](UltraNetHandle handle, const std::string& text) {
        if (auto socket = Get().Find(handle)) { socket->Received(std::vector<uint8_t>(text.begin(), text.end()), false); return; }
        if (Get().previous.onText) Get().previous.onText(handle, text);
    };
    router.onBinary = [](UltraNetHandle handle, const std::vector<uint8_t>& data) {
        if (auto socket = Get().Find(handle)) { socket->Received(data, true); return; }
        if (Get().previous.onBinary) Get().previous.onBinary(handle, data);
    };
    router.onClose = [](UltraNetHandle handle, int code, const std::string& reason) {
        if (auto socket = Get().Find(handle)) { socket->Ended(false, code, reason); return; }
        if (Get().previous.onClose) Get().previous.onClose(handle, code, reason);
    };
    router.onError = [](UltraNetHandle handle, const std::string& message) {
        // During the handshake the connecting thread's own socket failed.
        if (NetSocket* socket = t_connecting) { socket->SetConnectError(message); return; }
        if (auto socket = Get().Find(handle)) { socket->Ended(true, 1006, message); return; }
        if (Get().previous.onError) Get().previous.onError(handle, message);
    };
    router.onPing = [](UltraNetHandle handle, const std::vector<uint8_t>& data) {
        if (!Get().Find(handle) && Get().previous.onPing) Get().previous.onPing(handle, data);
    };
    router.onPong = [](UltraNetHandle handle, const std::vector<uint8_t>& data) {
        if (!Get().Find(handle) && Get().previous.onPong) Get().previous.onPong(handle, data);
    };
    previous = UltraNet_WebSocketSetCallbacks(router);
}

} // namespace

SocketService MakeNetworkSockets(std::function<void(std::function<void()>)> post) {
    return [post](const SocketRequest& request, SocketEvents events) -> std::shared_ptr<SocketConnection> {
        auto socket = std::make_shared<NetSocket>(post, std::move(events));
        socket->Start(request);
        return socket;
    };
}

} // namespace UltraWeb
