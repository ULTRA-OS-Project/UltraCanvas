// Apps/UltraDBAdmin/web/DbAdminRelayWeb.cpp
// Version: 0.2.0 - StartRelaySignIn, TakeRelayTicket
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminRelayWeb.h"

#include <UltraDatabase/UltraDatabase.h>

#include <emscripten.h>
#include <emscripten/websocket.h>

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>

namespace UltraDBAdmin {
namespace {

// What the socket's callbacks - on the main thread - share with the workers
// that wait.
struct SocketState {
    std::mutex m;
    std::condition_variable cv;
    std::map<int64_t, std::string> answers;
    bool open = false;
    bool closed = false;
    std::string why;
};

EM_BOOL OnOpen(int, const EmscriptenWebSocketOpenEvent*, void* user) {
    auto* s = static_cast<SocketState*>(user);
    std::lock_guard<std::mutex> lock(s->m);
    s->open = true;
    s->cv.notify_all();
    return EM_TRUE;
}

EM_BOOL OnMessage(int, const EmscriptenWebSocketMessageEvent* event, void* user) {
    if (!event->isText) return EM_TRUE;
    auto* s = static_cast<SocketState*>(user);
    const char* text = reinterpret_cast<const char*>(event->data);
    std::string answer(text, strnlen(text, event->numBytes));
    const int64_t id = UltraDb_RelayAnswerId(answer);
    std::lock_guard<std::mutex> lock(s->m);
    s->answers[id] = std::move(answer);
    s->cv.notify_all();
    return EM_TRUE;
}

EM_BOOL OnClose(int, const EmscriptenWebSocketCloseEvent* event, void* user) {
    auto* s = static_cast<SocketState*>(user);
    std::lock_guard<std::mutex> lock(s->m);
    s->closed = true;
    if (s->why.empty())
        s->why = std::string("the relay closed the connection") + (event && event->reason[0] ? std::string(": ") + event->reason : "");
    s->cv.notify_all();
    return EM_TRUE;
}

EM_BOOL OnError(int, const EmscriptenWebSocketErrorEvent*, void* user) {
    auto* s = static_cast<SocketState*>(user);
    std::lock_guard<std::mutex> lock(s->m);
    s->closed = true;
    if (s->why.empty()) s->why = "the relay is not reached";
    s->cv.notify_all();
    return EM_TRUE;
}

class BrowserSocket : public IUltraDbRelayTransport {
public:
    BrowserSocket(EMSCRIPTEN_WEBSOCKET_T socket, std::unique_ptr<SocketState> state)
        : socket_(socket), state_(std::move(state)) {}
    ~BrowserSocket() override {
        // Deleted on the main thread, after which no callback reaches the state.
        emscripten_websocket_close(socket_, 1000, "done");
        emscripten_websocket_delete(socket_);
    }
    UltraDbResult Call(int64_t id, const std::string& request, std::string& response) override {
        if (emscripten_websocket_send_utf8_text(socket_, request.c_str()) != EMSCRIPTEN_RESULT_SUCCESS)
            return UltraDbResult::Error(UltraDbResultCode::ConnectionFailed, "the relay is not reached", "relay");
        std::unique_lock<std::mutex> lock(state_->m);
        state_->cv.wait(lock, [&] { return state_->answers.count(id) > 0 || state_->closed; });
        const auto it = state_->answers.find(id);
        if (it == state_->answers.end())
            return UltraDbResult::Error(UltraDbResultCode::ConnectionFailed, state_->why, "relay");
        response = std::move(it->second);
        state_->answers.erase(it);
        return UltraDbResult::Ok();
    }

private:
    EMSCRIPTEN_WEBSOCKET_T socket_;
    std::unique_ptr<SocketState> state_;
};

std::unique_ptr<IUltraDbRelayTransport> Connect(const std::string& url, UltraDbResult& error) {
    if (!emscripten_websocket_is_supported()) {
        error = UltraDbResult::Error(UltraDbResultCode::Unsupported, "this browser has no WebSocket", "relay");
        return nullptr;
    }
    EmscriptenWebSocketCreateAttributes attributes;
    emscripten_websocket_init_create_attributes(&attributes);
    attributes.url = url.c_str();
    attributes.createOnMainThread = EM_TRUE;
    const EMSCRIPTEN_WEBSOCKET_T socket = emscripten_websocket_new(&attributes);
    if (socket <= 0) {
        error = UltraDbResult::Error(UltraDbResultCode::ConnectionFailed, "the relay at " + url + " is not reached", "relay");
        return nullptr;
    }
    auto state = std::make_unique<SocketState>();
    SocketState* s = state.get();
    // The callbacks run on the page's main thread, which never waits here.
    const pthread_t main = EM_CALLBACK_THREAD_CONTEXT_MAIN_RUNTIME_THREAD;
    emscripten_websocket_set_onopen_callback_on_thread(socket, s, OnOpen, main);
    emscripten_websocket_set_onmessage_callback_on_thread(socket, s, OnMessage, main);
    emscripten_websocket_set_onclose_callback_on_thread(socket, s, OnClose, main);
    emscripten_websocket_set_onerror_callback_on_thread(socket, s, OnError, main);
    {
        std::unique_lock<std::mutex> lock(s->m);
        s->cv.wait_for(lock, std::chrono::seconds(15), [s] { return s->open || s->closed; });
        if (!s->open) {
            const std::string why = s->why.empty() ? "the relay at " + url + " did not answer" : s->why;
            lock.unlock();
            emscripten_websocket_delete(socket);
            error = UltraDbResult::Error(UltraDbResultCode::ConnectionFailed, why, "relay");
            return nullptr;
        }
    }
    return std::make_unique<BrowserSocket>(socket, std::move(state));
}

} // namespace

void UseBrowserRelay() {
    UltraDb_SetRelayConnector(&Connect);
}

std::string DefaultRelayUrl() {
    char* url = static_cast<char*>(EM_ASM_PTR({
        const named = new URLSearchParams(location.search).get('relay');
        const url = named || ((location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host + '/relay');
        return stringToNewUTF8(url);
    }));
    std::string out = url ? url : "";
    free(url);
    return out;
}

void StartRelaySignIn(const std::string& relayUrl) {
    MAIN_THREAD_EM_ASM({
        const relay = UTF8ToString($0).replace(/^ws(s?):/, 'http$1:');
        const back = location.href.split('#')[0];
        location.assign(relay + '/signin?return=' + encodeURIComponent(back));
    }, relayUrl.c_str());
}

std::string TakeRelayTicket() {
    char* ticket = static_cast<char*>(MAIN_THREAD_EM_ASM_PTR({
        const m = /[#&]relay_ticket=([A-Za-z0-9_-]+)/.exec(location.hash);
        if (!m) return 0;
        history.replaceState(null, '', location.href.split('#')[0]);
        return stringToNewUTF8(m[1]);
    }));
    std::string out = ticket ? ticket : "";
    free(ticket);
    return out;
}

} // namespace UltraDBAdmin
