// Plugins/UltraIM/direct/DirectRelayClient.cpp
// ChatCube Direct - the relay's REST API and its WebSocket, over UltraNet
// Version: 0.1.1 - an address without a scheme is https://; a redirect is followed with its body
// Last Modified: 2026-10-22
// Author: UltraCanvas Framework

#include "DirectRelayClient.h"

#include "UltraNet/UltraNetHttp.h"
#include "UltraNet/UltraNetWebSocket.h"

#include <algorithm>
#include <chrono>
#include <map>

namespace UltraIMDirect {

// ============================================================================
// SHARED STATE
// Outlives the RelayClient for as long as a callback holds it; `alive` says
// whether the callback may still reach the client's handlers.
// ============================================================================

struct RelayClient::Shared {
    std::mutex mutex;
    std::condition_variable changed;
    bool alive = true;
    bool stopping = false;
    std::string baseUrl;
    std::string token;
    std::string pin;
    std::set<UltraNetHandle> requests;
    UltraNetHandle socket = UltraNetInvalidHandle;
    RelayClient* owner = nullptr;
    int inFlight = 0;           // callbacks running right now; Stop waits for them

    // Enters a callback if the client is still alive.
    bool Enter() {
        std::lock_guard<std::mutex> lock(mutex);
        if (!alive || stopping) return false;
        ++inFlight;
        return true;
    }
    void Leave() {
        std::lock_guard<std::mutex> lock(mutex);
        if (--inFlight == 0) changed.notify_all();
    }
};

namespace {

// ---- the process-wide WebSocket callbacks ------------------------------------
// UltraNet delivers every socket's frames through one callback bag. The
// relay clients install a router once; frames of a socket that is not
// theirs go on to whatever was installed before.

std::mutex g_routeMutex;
std::map<UltraNetHandle, std::weak_ptr<RelayClient::Shared>>& Routes() {
    static auto* routes = new std::map<UltraNetHandle, std::weak_ptr<RelayClient::Shared>>();
    return *routes;
}
UltraNetWebSocketCallbacks g_previous;
bool g_routerInstalled = false;
// The client connecting on this thread: onOpen arrives on the connecting
// thread before UltraNet_WebSocketConnect returns the handle.
thread_local std::weak_ptr<RelayClient::Shared> t_connecting;

std::shared_ptr<RelayClient::Shared> RouteFor(UltraNetHandle handle) {
    std::lock_guard<std::mutex> lock(g_routeMutex);
    auto it = Routes().find(handle);
    return it == Routes().end() ? nullptr : it->second.lock();
}

void InstallRouter();

} // namespace

// Called by the router; friend-free because Shared is a public nested type.
static void DeliverFrame(const std::shared_ptr<RelayClient::Shared>& shared, const std::string& text) {
    UltraCanvas::JSONParseResult parsed;
    UltraCanvas::JSONValue frame = UltraCanvas::JSON::Parse(text, &parsed);
    if (!parsed.success || !frame.IsObject()) return;
    if (!shared->Enter()) return;
    std::function<void(const UltraCanvas::JSONValue&)> handler;
    {
        std::lock_guard<std::mutex> lock(shared->mutex);
        if (shared->owner) handler = shared->owner->onFrame;
    }
    if (handler) handler(frame);
    shared->Leave();
}

static void SocketClosed(const std::shared_ptr<RelayClient::Shared>& shared, UltraNetHandle handle) {
    {
        std::lock_guard<std::mutex> lock(g_routeMutex);
        Routes().erase(handle);
    }
    std::lock_guard<std::mutex> lock(shared->mutex);
    if (shared->socket == handle) shared->socket = UltraNetInvalidHandle;
    shared->changed.notify_all();
}

namespace {

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
            DeliverFrame(shared, text);
            return;
        }
        if (g_previous.onText) g_previous.onText(handle, text);
    };
    router.onBinary = [](UltraNetHandle handle, const std::vector<uint8_t>& data) {
        if (RouteFor(handle)) return;
        if (g_previous.onBinary) g_previous.onBinary(handle, data);
    };
    router.onClose = [](UltraNetHandle handle, int code, const std::string& reason) {
        if (auto shared = RouteFor(handle)) {
            SocketClosed(shared, handle);
            return;
        }
        if (g_previous.onClose) g_previous.onClose(handle, code, reason);
    };
    router.onError = [](UltraNetHandle handle, const std::string& message) {
        if (auto shared = RouteFor(handle)) {
            SocketClosed(shared, handle);
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

UltraNetHttpMethod MethodFor(const std::string& method) {
    if (method == "POST") return UltraNetHttpMethod::Post;
    if (method == "PUT") return UltraNetHttpMethod::Put;
    if (method == "DELETE") return UltraNetHttpMethod::Delete;
    return UltraNetHttpMethod::Get;
}

std::string SocketUrlFor(const std::string& base) {
    if (base.rfind("https://", 0) == 0) return "wss://" + base.substr(8) + "/v1/ws";
    if (base.rfind("http://", 0) == 0) return "ws://" + base.substr(7) + "/v1/ws";
    return base + "/v1/ws";
}

} // namespace

// ============================================================================
// CLIENT
// ============================================================================

RelayClient::RelayClient() : shared_(std::make_shared<Shared>()) {
    shared_->owner = this;
}

RelayClient::~RelayClient() {
    Stop();
}

void RelayClient::SetBaseUrl(const std::string& url) {
    std::lock_guard<std::mutex> lock(shared_->mutex);
    // A host typed alone would be plain http:// to libcurl - and a relay
    // behind an HTTPS-only proxy answers that with a redirect.
    shared_->baseUrl = url.empty() || url.find("://") != std::string::npos ? url : "https://" + url;
    while (!shared_->baseUrl.empty() && shared_->baseUrl.back() == '/') shared_->baseUrl.pop_back();
}

std::string RelayClient::GetBaseUrl() const {
    std::lock_guard<std::mutex> lock(shared_->mutex);
    return shared_->baseUrl;
}

void RelayClient::SetToken(const std::string& token) {
    std::lock_guard<std::mutex> lock(shared_->mutex);
    shared_->token = token;
}

void RelayClient::SetPinnedPublicKey(const std::string& pin) {
    std::lock_guard<std::mutex> lock(shared_->mutex);
    shared_->pin = pin;
}

void RelayClient::Request(const std::string& method, const std::string& path,
                          const UltraCanvas::JSONValue* body, RelayCallback onDone) {
    Send(method, path, body ? std::make_shared<const UltraCanvas::JSONValue>(*body) : nullptr,
         std::move(onDone), 3);
}

void RelayClient::Send(const std::string& method, const std::string& path,
                       std::shared_ptr<const UltraCanvas::JSONValue> body, RelayCallback onDone,
                       int redirectsLeft) {
    UltraNetHttpRequest request;
    {
        std::lock_guard<std::mutex> lock(shared_->mutex);
        if (!shared_->alive || shared_->stopping) return;
        request.url = shared_->baseUrl + path;
        if (!shared_->token.empty()) request.headers.Set("Authorization", "Bearer " + shared_->token);
        if (!shared_->pin.empty()) request.options.pinnedPublicKey = shared_->pin;
    }
    request.method = MethodFor(method);
    request.headers.Set("Accept", "application/json");
    request.options.timeoutMs = 30000;
    // Redirects are followed below, not by libcurl: on a 301/302 it would
    // send the POST again without its body.
    request.options.followRedirects = false;
    if (body) {
        const std::string text = UltraCanvas::JSON::Serialize(*body);
        request.body.assign(text.begin(), text.end());
        request.headers.Set("Content-Type", "application/json");
    }

    std::weak_ptr<Shared> weak = shared_;
    auto handleBox = std::make_shared<std::atomic<UltraNetHandle>>(UltraNetInvalidHandle);
    auto done = [weak, method, path, body, onDone, redirectsLeft, handleBox](const UltraNetResponse& raw) {
        auto shared = weak.lock();
        if (!shared) return;
        {
            std::lock_guard<std::mutex> lock(shared->mutex);
            shared->requests.erase(handleBox->load());
        }
        if (!shared->Enter()) return;
        // Moved: the same endpoint at another address (http:// to https://,
        // a new host). The rest of the path is the relay's new base, kept
        // for the requests and the socket after this one.
        const std::string location = raw.headers.Get("Location");
        if (raw.transferError.empty() && raw.statusCode >= 300 && raw.statusCode < 400 &&
            redirectsLeft > 0 && location.size() > path.size() &&
            location.compare(location.size() - path.size(), path.size(), path) == 0 &&
            location.find("://") != std::string::npos) {
            RelayClient* owner = nullptr;
            {
                std::lock_guard<std::mutex> lock(shared->mutex);
                shared->baseUrl = location.substr(0, location.size() - path.size());
                owner = shared->owner;
            }
            if (owner) owner->Send(method, path, body, onDone, redirectsLeft - 1);
            shared->Leave();
            return;
        }
        RelayResponse response;
        if (!raw.transferError.empty()) {
            response.error = raw.transferError;
        } else {
            response.status = raw.statusCode;
            const std::string text = raw.GetBodyAsString();
            if (!text.empty()) {
                UltraCanvas::JSONParseResult parsed;
                response.body = UltraCanvas::JSON::Parse(text, &parsed);
            }
            if (!response.Ok()) {
                // The relay's own errors carry error_description; a request
                // it could not read (422) says why in detail[0].msg.
                std::string why = response.body["error_description"].GetString();
                if (why.empty()) why = response.body["detail"][size_t{0}]["msg"].GetString();
                if (why.empty() && !location.empty()) why = "the relay redirects to " + location;
                response.error = why.empty() ? "HTTP " + std::to_string(raw.statusCode) : why;
            }
        }
        if (onDone) onDone(response);
        shared->Leave();
    };
    const UltraNetHandle handle = UltraNet_HttpRequestAsync(request, done);
    handleBox->store(handle);
    if (handle != UltraNetInvalidHandle) {
        std::lock_guard<std::mutex> lock(shared_->mutex);
        if (UltraNet_IsRequestActive(handle)) shared_->requests.insert(handle);
    }
}

void RelayClient::StartSocket() {
    if (socketThread_.joinable()) return;
    InstallRouter();
    socketThread_ = std::thread([this]() { SocketLoop(); });
}

void RelayClient::SocketLoop() {
    int backoffMs = 1000;
    for (;;) {
        std::string url, token;
        {
            std::lock_guard<std::mutex> lock(shared_->mutex);
            if (shared_->stopping) return;
            url = SocketUrlFor(shared_->baseUrl);
            token = shared_->token;
        }
        UltraNetWebSocketOptions options;
        options.headers.Set("Authorization", "Bearer " + token);
        options.pingIntervalMs = 25000;
        t_connecting = shared_;
        const UltraNetHandle handle = UltraNet_WebSocketConnect(url, options);
        t_connecting.reset();

        if (handle != UltraNetInvalidHandle) {
            {
                std::lock_guard<std::mutex> lock(shared_->mutex);
                shared_->socket = handle;
            }
            socketOpen_ = true;
            backoffMs = 1000;
            if (onSocketOpen) onSocketOpen();
            std::unique_lock<std::mutex> lock(shared_->mutex);
            shared_->changed.wait(lock, [this, handle]() {
                return shared_->stopping || shared_->socket != handle;
            });
            const bool stopping = shared_->stopping;
            lock.unlock();
            socketOpen_ = false;
            if (stopping) {
                UltraNet_WebSocketClose(handle);
                std::lock_guard<std::mutex> routeLock(g_routeMutex);
                Routes().erase(handle);
                return;
            }
            if (onSocketClose) onSocketClose();
        }

        std::unique_lock<std::mutex> lock(shared_->mutex);
        shared_->changed.wait_for(lock, std::chrono::milliseconds(backoffMs),
                                  [this]() { return shared_->stopping; });
        if (shared_->stopping) return;
        backoffMs = std::min(backoffMs * 2, 30000);
    }
}

bool RelayClient::SendFrame(const UltraCanvas::JSONValue& frame) {
    UltraNetHandle handle;
    {
        std::lock_guard<std::mutex> lock(shared_->mutex);
        handle = shared_->socket;
    }
    if (handle == UltraNetInvalidHandle) return false;
    return static_cast<bool>(UltraNet_WebSocketSendText(handle, UltraCanvas::JSON::Serialize(frame)));
}

void RelayClient::Stop() {
    std::set<UltraNetHandle> requests;
    {
        std::lock_guard<std::mutex> lock(shared_->mutex);
        shared_->stopping = true;
        requests = shared_->requests;
        shared_->changed.notify_all();
    }
    for (UltraNetHandle handle : requests) UltraNet_CancelRequest(handle);
    if (socketThread_.joinable()) socketThread_.join();
    std::unique_lock<std::mutex> lock(shared_->mutex);
    shared_->changed.wait(lock, [this]() { return shared_->inFlight == 0; });
    shared_->alive = false;
    shared_->owner = nullptr;
    socketOpen_ = false;
}

} // namespace UltraIMDirect
