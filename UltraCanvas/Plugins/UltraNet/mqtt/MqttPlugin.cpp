// UltraCanvas/Plugins/UltraNet/mqtt/MqttPlugin.cpp
// MQTT / MQTTS plug-in. Implements IMessagingProtocolPlugin
// (Connect / Publish / Subscribe) on Eclipse Paho's async C client
// (libpaho-mqtt3as) — paho-mqtt-c.
//
// Build: produces libultranet_mqtt.{so,dylib,dll}. Skipped at configure
// time when paho-mqtt-c isn't available (libpaho-mqtt-dev on Debian
// derivatives, paho-mqtt-c on brew, mingw-w64-x86_64-libpaho-mqtt on
// MSYS2).
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetPlugins.h>
#include <UltraNet/UltraNetUrl.h>

#include <MQTTAsync.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using MessageHandler =
    std::function<void(const std::string&, const std::vector<uint8_t>&)>;

// ============================================================================
// Session = one MQTTAsync client + topic→handler map
// ============================================================================
struct Session {
    UltraNetHandle handle = UltraNetInvalidHandle;
    MQTTAsync      client = nullptr;
    int            qos    = 0;
    std::string    serverUri;       // tcp://host:port or ssl://host:port

    std::mutex                                      handlersMutex;
    std::unordered_map<std::string, MessageHandler> handlers;

    // Connect synchronisation — Connect() blocks until the async connect
    // callback fires (paho async API; we wrap it sync for the UltraNet
    // interface ergonomics).
    std::mutex              connectMutex;
    std::condition_variable connectCv;
    bool                    connectDone   = false;
    int                     connectResult = -1;

    ~Session() {
        if (client) {
            MQTTAsync_disconnectOptions opts = MQTTAsync_disconnectOptions_initializer;
            opts.timeout = 1000;
            MQTTAsync_disconnect(client, &opts);
            MQTTAsync_destroy(&client);
        }
    }
};

std::mutex g_sessionsMutex;
std::unordered_map<UltraNetHandle, std::shared_ptr<Session>> g_sessions;
std::atomic<UltraNetHandle> g_nextHandle{1};

std::shared_ptr<Session> Find(UltraNetHandle h) {
    std::lock_guard<std::mutex> lk(g_sessionsMutex);
    auto it = g_sessions.find(h);
    return it == g_sessions.end() ? nullptr : it->second;
}

// ============================================================================
// Topic filter matching (MQTT spec § 4.7) — '+' matches one level,
// '#' (must be last) matches zero or more trailing levels. Used to route
// inbound messages to the right handler when an app subscribed with a
// wildcard topic filter.
// ============================================================================
bool TopicMatches(const std::string& filter, const std::string& topic) {
    std::size_t fp = 0, tp = 0;
    while (fp < filter.size() && tp <= topic.size()) {
        // End of filter, end of topic -> match.
        if (fp == filter.size() && tp == topic.size()) return true;

        // Find next '/' in each.
        std::size_t fSlash = filter.find('/', fp);
        std::size_t tSlash = topic.find('/', tp);
        if (fSlash == std::string::npos) fSlash = filter.size();
        if (tSlash == std::string::npos) tSlash = topic.size();

        const std::string fSeg = filter.substr(fp, fSlash - fp);

        if (fSeg == "#") return true;             // tail wildcard
        if (tp >= topic.size()) return false;     // ran out of topic
        const std::string tSeg = topic.substr(tp, tSlash - tp);

        if (fSeg != "+" && fSeg != tSeg) return false;

        fp = fSlash + 1;
        tp = tSlash + 1;
        if (fSlash == filter.size()) fp = filter.size();
        if (tSlash == topic.size())  tp = topic.size();
    }
    return fp >= filter.size() && tp >= topic.size();
}

// ============================================================================
// paho async callbacks
// ============================================================================
int OnMessageArrived(void* ctx, char* topicName, int /*topicLen*/,
                     MQTTAsync_message* m) {
    auto* s = static_cast<Session*>(ctx);
    const std::string topic = topicName ? std::string(topicName) : std::string{};
    std::vector<uint8_t> payload;
    if (m && m->payloadlen > 0 && m->payload) {
        const auto* p = static_cast<const uint8_t*>(m->payload);
        payload.assign(p, p + m->payloadlen);
    }

    // Dispatch to every matching subscription. Copy handlers under lock
    // then invoke unlocked so a callback re-entering Subscribe doesn't
    // self-deadlock.
    std::vector<MessageHandler> toFire;
    {
        std::lock_guard<std::mutex> lk(s->handlersMutex);
        for (auto& [filter, fn] : s->handlers) {
            if (TopicMatches(filter, topic)) toFire.push_back(fn);
        }
    }
    for (auto& fn : toFire) {
        try { fn(topic, payload); } catch (...) {}
    }

    MQTTAsync_freeMessage(&m);
    MQTTAsync_free(topicName);
    return 1;   // 1 = we handled it
}

void OnConnectSuccess(void* ctx, MQTTAsync_successData* /*resp*/) {
    auto* s = static_cast<Session*>(ctx);
    {
        std::lock_guard<std::mutex> lk(s->connectMutex);
        s->connectDone   = true;
        s->connectResult = 0;
    }
    s->connectCv.notify_all();
}

void OnConnectFailure(void* ctx, MQTTAsync_failureData* resp) {
    auto* s = static_cast<Session*>(ctx);
    {
        std::lock_guard<std::mutex> lk(s->connectMutex);
        s->connectDone   = true;
        s->connectResult = resp ? resp->code : -1;
    }
    s->connectCv.notify_all();
}

// ============================================================================
// URL parsing — "mqtt://[user:pass@]host[:port]" → paho serverURI
// "tcp://host:port" / "ssl://host:port" (paho's native form).
// ============================================================================
bool BuildServerUri(const std::string& url,
                    std::string& outServerUri,
                    std::string& outUser, std::string& outPass) {
    UltraNetUrlComponents c;
    if (!UltraNet_ParseUrl(url, c)) return false;
    const bool tls = (c.scheme == "mqtts" || c.scheme == "ssl");
    if (c.scheme != "mqtt" && c.scheme != "mqtts" &&
        c.scheme != "tcp"  && c.scheme != "ssl") {
        return false;
    }
    const int port = c.port > 0 ? c.port : (tls ? 8883 : 1883);
    std::ostringstream os;
    os << (tls ? "ssl://" : "tcp://") << c.host << ':' << port;
    outServerUri = os.str();
    outUser = c.username;
    outPass = c.password;
    return true;
}

std::string GenerateClientId() {
    std::random_device rd;
    std::ostringstream os;
    os << "ultranet-" << std::hex << rd() << rd();
    return os.str();
}

// ============================================================================
// MqttPlugin
// ============================================================================
class MqttPlugin : public IMessagingProtocolPlugin {
public:
    std::string GetName() const override   { return "UltraNet-MQTT"; }
    std::string GetVersion() const override { return "0.1.0"; }
    std::vector<std::string> GetSupportedSchemes() const override {
        return {"mqtt", "mqtts"};
    }
    UltraNetResult Initialize(const UltraNetConfig&) override {
        return UltraNetResult::Ok();
    }
    void Shutdown() override {
        std::lock_guard<std::mutex> lk(g_sessionsMutex);
        g_sessions.clear();
    }

    UltraNetHandle Connect(const std::string& url,
                           const UltraNetMessagingOptions& options) override {
        std::string serverUri, urlUser, urlPass;
        if (!BuildServerUri(url, serverUri, urlUser, urlPass)) {
            return UltraNetInvalidHandle;
        }
        const std::string clientId = options.clientId.empty()
                                         ? GenerateClientId()
                                         : options.clientId;

        auto s = std::make_shared<Session>();
        s->serverUri = serverUri;
        s->qos       = options.qos;

        const int rc = MQTTAsync_create(
            &s->client, serverUri.c_str(), clientId.c_str(),
            MQTTCLIENT_PERSISTENCE_NONE, nullptr);
        if (rc != MQTTASYNC_SUCCESS) return UltraNetInvalidHandle;

        MQTTAsync_setCallbacks(s->client, s.get(),
                               /*connlost*/ nullptr,
                               &OnMessageArrived,
                               /*delivered*/ nullptr);

        MQTTAsync_connectOptions copts = MQTTAsync_connectOptions_initializer;
        copts.keepAliveInterval  = options.keepAliveSeconds > 0
                                       ? options.keepAliveSeconds : 60;
        copts.cleansession       = options.cleanSession ? 1 : 0;
        copts.connectTimeout     = options.connectTimeoutMs > 0
                                       ? options.connectTimeoutMs / 1000
                                       : 10;
        copts.context            = s.get();
        copts.onSuccess          = &OnConnectSuccess;
        copts.onFailure          = &OnConnectFailure;

        // Credentials precedence: explicit options first, URL userinfo second.
        const std::string user = !options.credentials.username.empty()
                                     ? options.credentials.username : urlUser;
        const std::string pass = !options.credentials.password.empty()
                                     ? options.credentials.password : urlPass;
        if (!user.empty()) copts.username = user.c_str();
        if (!pass.empty()) copts.password = pass.c_str();

        MQTTAsync_SSLOptions sslOpts = MQTTAsync_SSLOptions_initializer;
        if (options.useTls || serverUri.rfind("ssl://", 0) == 0) {
            copts.ssl = &sslOpts;
        }

        if (MQTTAsync_connect(s->client, &copts) != MQTTASYNC_SUCCESS) {
            return UltraNetInvalidHandle;
        }
        // Block until the success/failure callback fires.
        {
            std::unique_lock<std::mutex> lk(s->connectMutex);
            const auto wait = std::chrono::milliseconds(
                options.connectTimeoutMs > 0 ? options.connectTimeoutMs : 10000);
            if (!s->connectCv.wait_for(lk, wait, [&] { return s->connectDone; })) {
                return UltraNetInvalidHandle;
            }
            if (s->connectResult != 0) return UltraNetInvalidHandle;
        }

        const UltraNetHandle h =
            g_nextHandle.fetch_add(1, std::memory_order_relaxed);
        s->handle = h;
        {
            std::lock_guard<std::mutex> lk(g_sessionsMutex);
            g_sessions[h] = s;
        }
        return h;
    }

    UltraNetResult Publish(UltraNetHandle handle,
                           const std::string& topic,
                           const std::vector<uint8_t>& payload) override {
        auto s = Find(handle);
        if (!s) {
            return UltraNetResult::Error(UltraNetResultCode::InvalidHandle,
                                         "no such MQTT session");
        }
        MQTTAsync_message msg = MQTTAsync_message_initializer;
        msg.payload    = const_cast<void*>(static_cast<const void*>(payload.data()));
        msg.payloadlen = static_cast<int>(payload.size());
        msg.qos        = s->qos;
        msg.retained   = 0;

        MQTTAsync_responseOptions ropts = MQTTAsync_responseOptions_initializer;
        const int rc = MQTTAsync_sendMessage(s->client, topic.c_str(),
                                             &msg, &ropts);
        if (rc != MQTTASYNC_SUCCESS) {
            return UltraNetResult::Error(UltraNetResultCode::SendFailed,
                                         "MQTTAsync_sendMessage failed");
        }
        return UltraNetResult::Ok();
    }

    UltraNetResult Subscribe(UltraNetHandle handle,
                             const std::string& topic,
                             MessageHandler onMessage) override {
        auto s = Find(handle);
        if (!s) {
            return UltraNetResult::Error(UltraNetResultCode::InvalidHandle,
                                         "no such MQTT session");
        }
        if (!onMessage) {
            return UltraNetResult::Error(UltraNetResultCode::InvalidState,
                                         "onMessage callback is required");
        }
        {
            std::lock_guard<std::mutex> lk(s->handlersMutex);
            s->handlers[topic] = std::move(onMessage);
        }
        MQTTAsync_responseOptions ropts = MQTTAsync_responseOptions_initializer;
        const int rc = MQTTAsync_subscribe(s->client, topic.c_str(),
                                           s->qos, &ropts);
        if (rc != MQTTASYNC_SUCCESS) {
            std::lock_guard<std::mutex> lk(s->handlersMutex);
            s->handlers.erase(topic);
            return UltraNetResult::Error(UltraNetResultCode::Unknown,
                                         "MQTTAsync_subscribe failed");
        }
        return UltraNetResult::Ok();
    }
};

} // namespace

extern "C" ULTRANET_PLUGIN_EXPORT
void UltraNet_PluginInit(const UltraNetPluginHost* host) {
    if (!host || host->abiVersion < 1 || !host->RegisterPlugin) return;
    host->RegisterPlugin(std::make_shared<MqttPlugin>());
}
extern "C" void UltraNet_PluginRegister(void) {
    UltraNet_RegisterPlugin(std::make_shared<MqttPlugin>());
}
