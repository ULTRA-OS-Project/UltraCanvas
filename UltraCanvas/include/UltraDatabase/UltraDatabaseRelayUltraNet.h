// include/UltraDatabase/UltraDatabaseRelayUltraNet.h
// The relay's transport on a desktop (UltraDatabaseRelay.h): each relay
// connection a WebSocket of its own through UltraNet (UltraNetWebSocket.h),
// requests sent as text, answers matched to them by their id. One call at
// start-up, and every "relay" connection reaches its relay.
//
// Header-only, as UltraDatabaseSshTunnel.h is: UltraDatabase links no network
// library, and only a program that includes this - and links UltraNet -
// carries it. UltraNet's WebSocket callbacks are one set for the process:
// this takes them over and passes on every event that is not a relay's to
// the set that was there before.
//
// doc-check: example
//   #include <UltraDatabase/UltraDatabaseRelayUltraNet.h>
//
//   UltraDb_UseUltraNetRelay();
//
//   UltraDbConnectionConfig cfg;
//   cfg.name = "crm";
//   cfg.driver = "relay";
//   cfg.host = "wss://db.example.com/relay";
//   cfg.database = "crm";                     // as the relay names it
//   cfg.options["relay_token"] = "the user's token";
//   UltraDb_RegisterConnection(cfg);
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraDatabaseRelay.h"

#include <UltraNet/UltraNetWebSocket.h>

#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace ultradb_relay_net {

class Socket;

// Routes UltraNet's process-wide WebSocket callbacks to the sockets open.
class Hub {
public:
    static Hub& Get() {
        static Hub hub;
        return hub;
    }
    void Add(UltraNetHandle handle, const std::shared_ptr<Socket>& socket) {
        std::lock_guard<std::mutex> lock(m_);
        Install();
        sockets_[handle] = socket;
    }
    void Remove(UltraNetHandle handle) {
        std::lock_guard<std::mutex> lock(m_);
        sockets_.erase(handle);
    }
    std::shared_ptr<Socket> Find(UltraNetHandle handle) {
        std::lock_guard<std::mutex> lock(m_);
        const auto it = sockets_.find(handle);
        return it == sockets_.end() ? nullptr : it->second.lock();
    }
    const UltraNetWebSocketCallbacks& Previous() const { return previous_; }

private:
    void Install();

    std::mutex m_;
    bool installed_ = false;
    UltraNetWebSocketCallbacks previous_;
    std::map<UltraNetHandle, std::weak_ptr<Socket>> sockets_;
};

// One WebSocket and the answers waited for on it.
class Socket {
public:
    UltraNetHandle handle = UltraNetInvalidHandle;

    void Answer(const std::string& text) {
        const int64_t id = UltraDb_RelayAnswerId(text);
        std::lock_guard<std::mutex> lock(m_);
        answers_[id] = text;
        cv_.notify_all();
    }
    void Gone(const std::string& why) {
        std::lock_guard<std::mutex> lock(m_);
        closed_ = true;
        why_ = why;
        cv_.notify_all();
    }
    void Opened() {
        std::lock_guard<std::mutex> lock(m_);
        open_ = true;
        cv_.notify_all();
    }
    bool WaitOpen(int timeoutMs) {
        std::unique_lock<std::mutex> lock(m_);
        cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this] { return open_ || closed_; });
        return open_ && !closed_;
    }
    // The answer to `id`, or false when the socket closed first.
    bool Wait(int64_t id, std::string& out, std::string& why) {
        std::unique_lock<std::mutex> lock(m_);
        cv_.wait(lock, [&] { return answers_.count(id) > 0 || closed_; });
        const auto it = answers_.find(id);
        if (it == answers_.end()) {
            why = why_.empty() ? std::string("the relay closed the connection") : why_;
            return false;
        }
        out = std::move(it->second);
        answers_.erase(it);
        return true;
    }

private:
    std::mutex m_;
    std::condition_variable cv_;
    std::map<int64_t, std::string> answers_;
    bool open_ = false;
    bool closed_ = false;
    std::string why_;
};

inline void Hub::Install() {
    if (installed_) return;
    installed_ = true;
    UltraNetWebSocketCallbacks mine = UltraNet_WebSocketGetCallbacks();
    previous_ = mine;
    mine.onOpen = [](UltraNetHandle h) {
        if (auto s = Hub::Get().Find(h)) s->Opened();
        else if (Hub::Get().Previous().onOpen) Hub::Get().Previous().onOpen(h);
    };
    mine.onText = [](UltraNetHandle h, const std::string& text) {
        if (auto s = Hub::Get().Find(h)) s->Answer(text);
        else if (Hub::Get().Previous().onText) Hub::Get().Previous().onText(h, text);
    };
    mine.onClose = [](UltraNetHandle h, int code, const std::string& reason) {
        if (auto s = Hub::Get().Find(h)) s->Gone(reason.empty() ? "the relay closed the connection (" + std::to_string(code) + ")" : reason);
        else if (Hub::Get().Previous().onClose) Hub::Get().Previous().onClose(h, code, reason);
    };
    mine.onError = [](UltraNetHandle h, const std::string& error) {
        if (auto s = Hub::Get().Find(h)) s->Gone(error);
        else if (Hub::Get().Previous().onError) Hub::Get().Previous().onError(h, error);
    };
    UltraNet_WebSocketSetCallbacks(mine);
}

class Transport : public IUltraDbRelayTransport {
public:
    explicit Transport(std::shared_ptr<Socket> socket) : socket_(std::move(socket)) {}
    ~Transport() override {
        UltraNet_WebSocketClose(socket_->handle);
        Hub::Get().Remove(socket_->handle);
    }
    UltraDbResult Call(int64_t id, const std::string& request, std::string& response) override {
        const UltraNetResult sent = UltraNet_WebSocketSendText(socket_->handle, request);
        if (!sent)
            return UltraDbResult::Error(UltraDbResultCode::ConnectionFailed, "the relay is not reached: " + sent.message,
                                        "relay");
        std::string why;
        if (!socket_->Wait(id, response, why))
            return UltraDbResult::Error(UltraDbResultCode::ConnectionFailed, why, "relay");
        return UltraDbResult::Ok();
    }

private:
    std::shared_ptr<Socket> socket_;
};

inline std::unique_ptr<IUltraDbRelayTransport> Connect(const std::string& url, UltraDbResult& error) {
    auto socket = std::make_shared<Socket>();
    // Registered before the connect returns, so its open is not missed: the
    // handle is the next one UltraNet gives, which is known only after.
    UltraNetWebSocketOptions options;
    options.connectTimeoutMs = 10000;
    socket->handle = UltraNet_WebSocketConnect(url, options);
    if (socket->handle == UltraNetInvalidHandle) {
        error = UltraDbResult::Error(UltraDbResultCode::ConnectionFailed, "the relay at " + url + " is not reached",
                                     "relay");
        return nullptr;
    }
    Hub::Get().Add(socket->handle, socket);
    // The connect may have opened it before the hub knew the handle.
    if (UltraNet_WebSocketIsOpen(socket->handle)) socket->Opened();
    if (!socket->WaitOpen(options.connectTimeoutMs)) {
        UltraNet_WebSocketClose(socket->handle);
        Hub::Get().Remove(socket->handle);
        error = UltraDbResult::Error(UltraDbResultCode::ConnectionFailed, "the relay at " + url + " did not answer",
                                     "relay");
        return nullptr;
    }
    return std::make_unique<Transport>(socket);
}

} // namespace ultradb_relay_net

// Every "relay" connection through a WebSocket of UltraNet's.
inline void UltraDb_UseUltraNetRelay() {
    UltraDb_SetRelayConnector(&ultradb_relay_net::Connect);
}
