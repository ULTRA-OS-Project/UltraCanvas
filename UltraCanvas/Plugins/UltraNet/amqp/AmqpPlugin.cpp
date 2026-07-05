// UltraCanvas/Plugins/UltraNet/amqp/AmqpPlugin.cpp
// AMQP / AMQPS plug-in. Implements IMessagingProtocolPlugin
// (Connect / Publish / Subscribe) on rabbitmq-c (librabbitmq).
//
// AMQP 0-9-1 is a richer model than MQTT — exchanges + routing keys vs
// topics, mandatory queue declarations, channels. For the unified
// UltraNet IMessagingProtocolPlugin shape we map:
//   - Publish(topic, payload) → basic.publish to the default exchange
//                                with `topic` as the routing key
//   - Subscribe(topic, cb)    → declare a queue named `topic`, bind
//                                it to the default exchange, basic.consume
//                                on it. Receiver thread loops on
//                                amqp_consume_message and dispatches.
// Apps wanting advanced AMQP semantics (custom exchanges, headers,
// confirms) can extend this plug-in or use librabbitmq directly.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetPlugins.h>
#include <UltraNet/UltraNetUrl.h>

#include <amqp.h>
#include <amqp_ssl_socket.h>
#include <amqp_tcp_socket.h>

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
    UltraNetHandle      handle = UltraNetInvalidHandle;
    amqp_connection_state_t conn = nullptr;
    amqp_channel_t      channel = 1;
    int                 qos = 0;
    std::string         vhost = "/";

    std::mutex                                      handlersMutex;
    std::unordered_map<std::string, MessageHandler> handlers;

    // One receiver thread per session — librabbitmq has no native async
    // delivery, so we block on amqp_consume_message in a loop.
    std::atomic<bool>   running{false};
    std::thread         receiver;

    ~Session() {
        running.store(false, std::memory_order_release);
        if (receiver.joinable()) receiver.join();
        if (conn) {
            amqp_channel_close(conn, channel, AMQP_REPLY_SUCCESS);
            amqp_connection_close(conn, AMQP_REPLY_SUCCESS);
            amqp_destroy_connection(conn);
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

amqp_bytes_t StrToBytes(const std::string& s) {
    amqp_bytes_t b;
    b.len   = s.size();
    b.bytes = const_cast<char*>(s.data());
    return b;
}

std::string BytesToStr(const amqp_bytes_t& b) {
    return std::string(static_cast<const char*>(b.bytes), b.len);
}

void ReceiverLoop(std::shared_ptr<Session> s) {
    while (s->running.load(std::memory_order_acquire)) {
        amqp_envelope_t env;
        amqp_maybe_release_buffers(s->conn);

        timeval tv{1, 0};   // 1 s
        amqp_rpc_reply_t r = amqp_consume_message(s->conn, &env, &tv, 0);
        if (r.reply_type != AMQP_RESPONSE_NORMAL) {
            if (r.reply_type == AMQP_RESPONSE_LIBRARY_EXCEPTION &&
                r.library_error == AMQP_STATUS_TIMEOUT) continue;
            // Something else — connection lost / channel closed. Stop.
            break;
        }
        const std::string topic   = BytesToStr(env.routing_key);
        const std::string bodyStr = BytesToStr(env.message.body);
        std::vector<uint8_t> payload(bodyStr.begin(), bodyStr.end());

        std::vector<MessageHandler> toFire;
        {
            std::lock_guard<std::mutex> lk(s->handlersMutex);
            // AMQP routing key matches the exact subscription key here
            // (the default-exchange "queue == routing key" model). Apps
            // that bind to fanout / topic exchanges with wildcards live
            // outside this plug-in's scope.
            auto it = s->handlers.find(topic);
            if (it != s->handlers.end()) toFire.push_back(it->second);
        }
        for (auto& fn : toFire) {
            try { fn(topic, payload); } catch (...) {}
        }
        amqp_destroy_envelope(&env);
    }
}

class AmqpPlugin : public IMessagingProtocolPlugin {
public:
    std::string GetName() const override { return "UltraNet-AMQP"; }
    std::string GetVersion() const override { return "0.1.0"; }
    std::vector<std::string> GetSupportedSchemes() const override {
        return {"amqp", "amqps"};
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
        UltraNetUrlComponents c;
        if (!UltraNet_ParseUrl(url, c)) return UltraNetInvalidHandle;
        if (c.scheme != "amqp" && c.scheme != "amqps") return UltraNetInvalidHandle;
        const bool tls = (c.scheme == "amqps");
        const int  port = c.port > 0 ? c.port : (tls ? 5671 : 5672);

        auto s = std::make_shared<Session>();
        s->qos   = options.qos;
        s->vhost = c.path.empty() || c.path == "/" ? "/" : c.path;

        s->conn = amqp_new_connection();
        if (!s->conn) return UltraNetInvalidHandle;
        amqp_socket_t* sock = tls ? amqp_ssl_socket_new(s->conn)
                                  : amqp_tcp_socket_new(s->conn);
        if (!sock) return UltraNetInvalidHandle;

        if (amqp_socket_open(sock, c.host.c_str(), port) != AMQP_STATUS_OK) {
            return UltraNetInvalidHandle;
        }

        // Credentials precedence: explicit options first, URL userinfo
        // second, "guest"/"guest" last-resort default.
        const std::string user = !options.credentials.username.empty()
                                     ? options.credentials.username
                                     : (!c.username.empty() ? c.username : "guest");
        const std::string pass = !options.credentials.password.empty()
                                     ? options.credentials.password
                                     : (!c.password.empty() ? c.password : "guest");

        amqp_rpc_reply_t login = amqp_login(
            s->conn, s->vhost.c_str(), 0, 131072,
            options.keepAliveSeconds > 0 ? options.keepAliveSeconds : 60,
            AMQP_SASL_METHOD_PLAIN, user.c_str(), pass.c_str());
        if (login.reply_type != AMQP_RESPONSE_NORMAL) return UltraNetInvalidHandle;

        amqp_channel_open(s->conn, s->channel);
        if (amqp_get_rpc_reply(s->conn).reply_type != AMQP_RESPONSE_NORMAL) {
            return UltraNetInvalidHandle;
        }

        const UltraNetHandle h =
            g_nextHandle.fetch_add(1, std::memory_order_relaxed);
        s->handle = h;
        s->running.store(true, std::memory_order_release);
        s->receiver = std::thread(ReceiverLoop, s);
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
        if (!s) return UltraNetResult::Error(UltraNetResultCode::InvalidHandle,
                                             "no such AMQP session");
        amqp_basic_properties_t props{};
        props._flags = AMQP_BASIC_CONTENT_TYPE_FLAG | AMQP_BASIC_DELIVERY_MODE_FLAG;
        props.content_type  = amqp_cstring_bytes("application/octet-stream");
        props.delivery_mode = 2;   // persistent

        amqp_bytes_t body;
        body.len   = payload.size();
        body.bytes = const_cast<uint8_t*>(payload.data());

        // Default exchange: routing_key == queue name. Same shape we tell
        // Subscribe() to use, so a publish/subscribe pair on the same
        // topic round-trips end-to-end.
        const int rc = amqp_basic_publish(
            s->conn, s->channel,
            amqp_empty_bytes,                      // default exchange
            StrToBytes(topic),                     // routing key
            0, 0,                                  // mandatory/immediate
            &props, body);
        if (rc != AMQP_STATUS_OK) {
            return UltraNetResult::Error(UltraNetResultCode::SendFailed,
                                         "amqp_basic_publish failed");
        }
        return UltraNetResult::Ok();
    }

    UltraNetResult Subscribe(UltraNetHandle handle,
                             const std::string& topic,
                             MessageHandler onMessage) override {
        auto s = Find(handle);
        if (!s) return UltraNetResult::Error(UltraNetResultCode::InvalidHandle,
                                             "no such AMQP session");
        if (!onMessage) {
            return UltraNetResult::Error(UltraNetResultCode::InvalidState,
                                         "onMessage callback is required");
        }
        // Declare a queue with the topic name; durable, not exclusive.
        amqp_queue_declare(s->conn, s->channel, StrToBytes(topic),
                           0, 1, 0, 0, amqp_empty_table);
        if (amqp_get_rpc_reply(s->conn).reply_type != AMQP_RESPONSE_NORMAL) {
            return UltraNetResult::Error(UltraNetResultCode::Unknown,
                                         "amqp_queue_declare failed");
        }
        // Start consuming. no_ack=1 (auto-ack) for simplicity.
        amqp_basic_consume(s->conn, s->channel, StrToBytes(topic),
                           amqp_empty_bytes, 0, 1, 0, amqp_empty_table);
        if (amqp_get_rpc_reply(s->conn).reply_type != AMQP_RESPONSE_NORMAL) {
            return UltraNetResult::Error(UltraNetResultCode::Unknown,
                                         "amqp_basic_consume failed");
        }
        {
            std::lock_guard<std::mutex> lk(s->handlersMutex);
            s->handlers[topic] = std::move(onMessage);
        }
        return UltraNetResult::Ok();
    }
};

} // namespace

extern "C" ULTRANET_PLUGIN_EXPORT
void UltraNet_PluginInit(const UltraNetPluginHost* host) {
    if (!host || host->abiVersion < 1 || !host->RegisterPlugin) return;
    host->RegisterPlugin(std::make_shared<AmqpPlugin>());
}
extern "C" void UltraNet_PluginRegister(void) {
    UltraNet_RegisterPlugin(std::make_shared<AmqpPlugin>());
}
