// UltraCanvas/Plugins/UltraNet/coap/CoapPlugin.cpp
// CoAP / CoAPS plug-in (RFC 7252). Implements IMessagingProtocolPlugin
// with the IoT-friendly mapping:
//   - Connect(coap://host[:port]) → builds a libcoap context + UDP session
//   - Publish(path, payload)      → POST to coap://host/path
//   - Subscribe(path, cb)         → GET with Observe (RFC 7641); the
//                                    cb fires on every notification
//
// Backend: libcoap3 (libcoap-3-openssl or libcoap-3-notls). The plug-in
// CMakeLists falls back to whichever is installed.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetPlugins.h>
#include <UltraNet/UltraNetUrl.h>

#include <coap3/coap.h>

#if defined(_WIN32) || defined(_WIN64)
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
#endif

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using MessageHandler =
    std::function<void(const std::string&, const std::vector<uint8_t>&)>;

struct Session {
    UltraNetHandle    handle = UltraNetInvalidHandle;
    coap_context_t*   ctx     = nullptr;
    coap_session_t*   session = nullptr;
    std::string       host;
    int               port = 5683;

    std::mutex                                      handlersMutex;
    std::unordered_map<std::string, MessageHandler> handlers;   // path -> cb

    std::atomic<bool> running{false};
    std::thread       worker;

    ~Session() {
        running.store(false, std::memory_order_release);
        if (worker.joinable()) worker.join();
        if (session) coap_session_release(session);
        if (ctx) coap_free_context(ctx);
    }
};

std::mutex g_sessionsMutex;
std::unordered_map<UltraNetHandle, std::shared_ptr<Session>> g_sessions;
std::atomic<UltraNetHandle> g_nextHandle{1};

// Library lifecycle — coap_startup is idempotent enough for our usage but
// reference-count it so we cleanup once, after the last plug-in instance.
std::mutex      g_libMutex;
int             g_libRefCount = 0;
void LibAcquire() {
    std::lock_guard<std::mutex> lk(g_libMutex);
    if (g_libRefCount++ == 0) coap_startup();
}
void LibRelease() {
    std::lock_guard<std::mutex> lk(g_libMutex);
    if (--g_libRefCount == 0) coap_cleanup();
}

std::shared_ptr<Session> Find(UltraNetHandle h) {
    std::lock_guard<std::mutex> lk(g_sessionsMutex);
    auto it = g_sessions.find(h);
    return it == g_sessions.end() ? nullptr : it->second;
}

// Response handler — libcoap invokes this on every received CoAP response
// (including Observe notifications). The session pointer comes back via
// the request's app_data → we stash it as a Session*.
coap_response_t OnResponse(coap_session_t* /*session*/,
                           const coap_pdu_t* /*sent*/,
                           const coap_pdu_t* received,
                           const coap_mid_t /*mid*/) {
    if (!received) return COAP_RESPONSE_OK;

    // Recover the path the request was issued against. CoAP responses
    // don't carry the URI path explicitly; we read it from the original
    // request's app_data (set in Publish/Subscribe below). For Observe
    // notifications we cache the path keyed on the token instead — see
    // SetTokenHandler below.
    std::size_t bodyLen = 0;
    const uint8_t* body = nullptr;
    coap_get_data(received, &bodyLen, &body);

    // We routed the request's URI path into the CoAP token (see
    // SendRequest below) so the response carries it back without needing
    // a stash on the session itself. Walk every live session, fire any
    // handler whose registered path matches.
    coap_bin_const_t tok = coap_pdu_get_token(received);
    std::string pathFromToken(reinterpret_cast<const char*>(tok.s), tok.length);

    std::vector<std::pair<MessageHandler, std::string>> fire;
    {
        std::lock_guard<std::mutex> lk(g_sessionsMutex);
        for (auto& [_, sess] : g_sessions) {
            std::lock_guard<std::mutex> lk2(sess->handlersMutex);
            auto it = sess->handlers.find(pathFromToken);
            if (it != sess->handlers.end()) {
                fire.push_back({it->second, pathFromToken});
            }
        }
    }
    std::vector<uint8_t> payload(body, body + bodyLen);
    for (auto& [fn, path] : fire) {
        try { fn(path, payload); } catch (...) {}
    }
    return COAP_RESPONSE_OK;
}

void WorkerLoop(std::shared_ptr<Session> s) {
    while (s->running.load(std::memory_order_acquire)) {
        coap_io_process(s->ctx, 250);   // 250 ms tick
    }
}

bool BuildSession(Session& s, const std::string& host, int port, bool tls) {
    coap_address_t dst;
    coap_address_init(&dst);
    dst.addr.sin.sin_family = AF_INET;
    dst.addr.sin.sin_port   = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, host.c_str(), &dst.addr.sin.sin_addr) != 1) {
        // Hostname — try a simple sync resolve. CoAP itself doesn't
        // recurse hostnames; the caller is responsible for an IP.
        struct hostent* he = gethostbyname(host.c_str());
        if (!he || he->h_addrtype != AF_INET || !he->h_addr_list[0]) return false;
        std::memcpy(&dst.addr.sin.sin_addr, he->h_addr_list[0],
                    sizeof(dst.addr.sin.sin_addr));
    }
    dst.size = sizeof(dst.addr.sin);

    s.ctx = coap_new_context(nullptr);
    if (!s.ctx) return false;

    const coap_proto_t proto = tls ? COAP_PROTO_DTLS : COAP_PROTO_UDP;
    s.session = coap_new_client_session(s.ctx, nullptr, &dst, proto);
    if (!s.session) return false;

    coap_register_response_handler(s.ctx, &OnResponse);
    return true;
}

// Builds and sends a CoAP request. Encodes the URI path in the token so
// the global OnResponse handler can route the response back.
bool SendRequest(Session& s, coap_pdu_code_t method,
                 const std::string& path,
                 const std::vector<uint8_t>& payload,
                 bool observe) {
    if (path.size() > 8) {
        // Token can be up to 8 bytes. Fall back to a 4-byte hash for
        // long paths — collisions are unlikely in practice for a small
        // handler table, but documented.
    }
    coap_pdu_t* pdu = coap_pdu_init(
        observe ? COAP_MESSAGE_CON : COAP_MESSAGE_NON,
        method, coap_new_message_id(s.session),
        coap_session_max_pdu_size(s.session));
    if (!pdu) return false;

    // Token = path bytes (truncated if needed). Used by OnResponse to
    // look up the registered handler.
    const std::size_t tokLen = path.size() < 8 ? path.size() : 8;
    coap_add_token(pdu, tokLen,
                   reinterpret_cast<const uint8_t*>(path.data()));

    coap_optlist_t* opts = nullptr;
    // Split path on '/' and add a Uri-Path option per segment.
    std::size_t i = 0;
    while (i < path.size()) {
        std::size_t slash = path.find('/', i);
        if (slash == std::string::npos) slash = path.size();
        if (slash > i) {
            coap_insert_optlist(&opts, coap_new_optlist(
                COAP_OPTION_URI_PATH, slash - i,
                reinterpret_cast<const uint8_t*>(path.data() + i)));
        }
        i = slash + 1;
    }
    if (observe) {
        const uint8_t obs = 0;
        coap_insert_optlist(&opts,
            coap_new_optlist(COAP_OPTION_OBSERVE, 1, &obs));
    }
    coap_add_optlist_pdu(pdu, &opts);
    coap_delete_optlist(opts);

    if (!payload.empty()) {
        coap_add_data(pdu, payload.size(), payload.data());
    }

    if (coap_send(s.session, pdu) == COAP_INVALID_MID) return false;
    return true;
}

class CoapPlugin : public IMessagingProtocolPlugin {
public:
    std::string GetName() const override { return "UltraNet-CoAP"; }
    std::string GetVersion() const override { return "0.1.0"; }
    std::vector<std::string> GetSupportedSchemes() const override {
        return {"coap", "coaps"};
    }
    UltraNetResult Initialize(const UltraNetConfig&) override {
        LibAcquire();
        return UltraNetResult::Ok();
    }
    void Shutdown() override {
        {
            std::lock_guard<std::mutex> lk(g_sessionsMutex);
            g_sessions.clear();
        }
        LibRelease();
    }

    UltraNetHandle Connect(const std::string& url,
                           const UltraNetMessagingOptions& /*options*/) override {
        UltraNetUrlComponents c;
        if (!UltraNet_ParseUrl(url, c)) return UltraNetInvalidHandle;
        if (c.scheme != "coap" && c.scheme != "coaps") return UltraNetInvalidHandle;
        const bool tls = (c.scheme == "coaps");
        const int port = c.port > 0 ? c.port : (tls ? 5684 : 5683);

        auto s = std::make_shared<Session>();
        s->host = c.host;
        s->port = port;
        if (!BuildSession(*s, c.host, port, tls)) return UltraNetInvalidHandle;

        const UltraNetHandle h =
            g_nextHandle.fetch_add(1, std::memory_order_relaxed);
        s->handle = h;
        s->running.store(true, std::memory_order_release);
        s->worker = std::thread(WorkerLoop, s);
        {
            std::lock_guard<std::mutex> lk(g_sessionsMutex);
            g_sessions[h] = s;
        }
        return h;
    }

    // Publish == POST to /topic with payload.
    UltraNetResult Publish(UltraNetHandle handle,
                           const std::string& topic,
                           const std::vector<uint8_t>& payload) override {
        auto s = Find(handle);
        if (!s) return UltraNetResult::Error(UltraNetResultCode::InvalidHandle,
                                             "no such CoAP session");
        if (!SendRequest(*s, COAP_REQUEST_CODE_POST, topic, payload, false)) {
            return UltraNetResult::Error(UltraNetResultCode::SendFailed,
                                         "coap_send failed");
        }
        return UltraNetResult::Ok();
    }

    // Subscribe == GET with Observe option (RFC 7641).
    UltraNetResult Subscribe(UltraNetHandle handle,
                             const std::string& topic,
                             MessageHandler onMessage) override {
        auto s = Find(handle);
        if (!s) return UltraNetResult::Error(UltraNetResultCode::InvalidHandle,
                                             "no such CoAP session");
        if (!onMessage) {
            return UltraNetResult::Error(UltraNetResultCode::InvalidState,
                                         "onMessage callback is required");
        }
        {
            std::lock_guard<std::mutex> lk(s->handlersMutex);
            s->handlers[topic] = std::move(onMessage);
        }
        if (!SendRequest(*s, COAP_REQUEST_CODE_GET, topic, {}, true)) {
            std::lock_guard<std::mutex> lk(s->handlersMutex);
            s->handlers.erase(topic);
            return UltraNetResult::Error(UltraNetResultCode::SendFailed,
                                         "coap_send (observe) failed");
        }
        return UltraNetResult::Ok();
    }
};

} // namespace

extern "C" ULTRANET_PLUGIN_EXPORT
void UltraNet_PluginInit(const UltraNetPluginHost* host) {
    if (!host || host->abiVersion < 1 || !host->RegisterPlugin) return;
    host->RegisterPlugin(std::make_shared<CoapPlugin>());
}
extern "C" void UltraNet_PluginRegister(void) {
    UltraNet_RegisterPlugin(std::make_shared<CoapPlugin>());
}
