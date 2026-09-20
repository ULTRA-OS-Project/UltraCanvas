// UltraCanvas/core/UltraMessage/UltraMessageEndpoint.cpp
// The endpoint side of the channel and the UltraMsg_* API: connect-or-host
// election, one reader thread per endpoint, delivery through the UI
// dispatcher or the pending queue, subscriptions, the Wimp send set, and the
// journal calls forwarded to the broker as control RPCs.
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraMessageBroker.h"
#include "UltraMessageInternal.h"
#include "UltraMessageTransport.h"

#include <UltraDatabase/UltraDatabaseConnection.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

using namespace UltraMessage::Internal;
using UltraCanvas::JSONValue;

namespace {

constexpr const char* kVersion = "0.1.0";
constexpr int kControlTimeoutMs = 10000;
constexpr int kWelcomeTimeoutMs = 5000;
constexpr int kTimerTickMs = 100;

std::string Str(const JSONValue& json, const char* key) {
    const JSONValue* v = json.Find(key);
    return v ? v->GetString() : std::string();
}
int64_t Int(const JSONValue& json, const char* key, int64_t fallback = 0) {
    const JSONValue* v = json.Find(key);
    return v ? v->GetInteger(fallback) : fallback;
}
bool Bool(const JSONValue& json, const char* key, bool fallback = false) {
    const JSONValue* v = json.Find(key);
    return v ? v->GetBoolean(fallback) : fallback;
}
const JSONValue& Obj(const JSONValue& json, const char* key) {
    const JSONValue* v = json.Find(key);
    return v ? *v : JSONValue::NullValue();
}

struct EndpointRec;

struct SubscriptionRec {
    UltraMsgHandle handle = UltraMsgInvalidHandle;
    uint64_t brokerId = 0;
    std::string pattern;
    UltraMsgCallback callback;
    UltraMsgSubscribeOptions options;
    std::weak_ptr<EndpointRec> endpoint;
};

struct PendingControl {
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    JSONValue reply;
    // Runs on the reader thread before the waiter wakes (subscription
    // registration must precede the first delivery).
    std::function<void(const JSONValue& reply)> onReply;
};

struct PendingRequest {
    bool async = false;
    bool direct = false;                            // async: skip the UI dispatcher
    UltraMsgReplyCallback callback;                 // async
    std::shared_ptr<PendingControl> waiter;         // sync: reply stored in waiter->reply
    int64_t deadlineMs = 0;
};

struct EndpointRec : std::enable_shared_from_this<EndpointRec> {
    UltraMsgHandle handle = UltraMsgInvalidHandle;
    UltraMsgConnectOptions options;
    ConnectionPtr connection;
    std::thread reader;
    std::atomic<bool> connected{false};
    std::atomic<bool> closing{false};

    // Filled by the welcome frame.
    std::mutex welcomeMutex;
    std::condition_variable welcomeCv;
    bool welcomed = false;
    bool welcomeOk = false;
    std::string welcomeError;
    UltraMsgEndpointInfo info;
    UltraMsgBrokerInfo broker;

    std::mutex mutex;
    std::map<std::string, std::shared_ptr<PendingControl>> controls;   // by rid
    std::map<std::string, PendingRequest> requests;                    // by message id
    std::map<std::string, UltraMsgBounceCallback> bounces;             // by message id
    std::map<uint64_t, std::shared_ptr<SubscriptionRec>> subscriptions; // by broker id
    std::deque<std::function<void()>> pending;                         // UI queue
};
using EndpointPtr = std::shared_ptr<EndpointRec>;

struct Module {
    std::mutex mutex;
    std::map<UltraMsgHandle, EndpointPtr> endpoints;
    std::map<UltraMsgHandle, std::shared_ptr<SubscriptionRec>> subscriptions;
    UltraMsgHandle nextHandle = 1;
    UltraMsgDispatcher dispatcher;

    std::mutex brokerMutex;
    std::unique_ptr<Broker> broker;

    std::thread timer;
    std::atomic<bool> timerRunning{false};
};

Module& GetModule() {
    static Module* module = [] {
        // Touch UltraDatabase first so its registry outlives this module at
        // exit: the atexit handler below then runs before that registry is
        // destroyed, and the broker's journal closes on a live manager.
        UltraDb_HasConnection("ultramessage.bootstrap");
        auto* m = new Module();
        std::atexit([] { UltraMsg_Shutdown(); });
        return m;
    }();
    return *module;
}

UltraMsgResult NotConnected() {
    return UltraMsgResult::Error(UltraMsgResultCode::NotConnected, "endpoint is not connected");
}

EndpointPtr FindEndpoint(UltraMsgHandle handle) {
    Module& module = GetModule();
    std::lock_guard<std::mutex> lock(module.mutex);
    auto it = module.endpoints.find(handle);
    return it == module.endpoints.end() ? nullptr : it->second;
}

// ---------------------------------------------------------------------------
// Delivery
// ---------------------------------------------------------------------------

void Dispatch(const EndpointPtr& endpoint, bool onWorkerThread, std::function<void()> task) {
    if (!task) return;
    if (onWorkerThread || !endpoint->options.deliverOnUIThread) {
        task();
        return;
    }
    UltraMsgDispatcher dispatcher;
    {
        Module& module = GetModule();
        std::lock_guard<std::mutex> lock(module.mutex);
        dispatcher = module.dispatcher;
    }
    if (dispatcher) {
        dispatcher(std::move(task));
        return;
    }
    std::lock_guard<std::mutex> lock(endpoint->mutex);
    endpoint->pending.push_back(std::move(task));
}

bool SendFrame(const EndpointPtr& endpoint, const JSONValue& frame, UltraMsgResult* error = nullptr) {
    if (!endpoint->connected.load()) {
        if (error) *error = NotConnected();
        return false;
    }
    std::string message;
    if (!endpoint->connection->SendFrame(frame, message)) {
        if (error) *error = UltraMsgResult::Error(UltraMsgResultCode::NotConnected, message);
        return false;
    }
    return true;
}

void SendAck(const EndpointPtr& endpoint, const std::string& id) {
    JSONValue frame = MakeFrame(Frame::Ack);
    frame.Set("id", id);
    SendFrame(endpoint, frame);
}

UltraMsgResult ResultFromReplyBody(const UltraMsgMessage& reply) {
    if (reply.body.IsObject()) {
        if (const JSONValue* error = reply.body.Find("error"); error && error->IsObject()) {
            JSONValue asResult = JSONValue::MakeObject();
            asResult.Set("ok", false);
            asResult.Set("code", Str(*error, "code"));
            asResult.Set("message", Str(*error, "message"));
            return ResultFromJson(asResult);
        }
    }
    return UltraMsgResult::Ok();
}

void CompleteRequest(const EndpointPtr& endpoint, const std::string& id, const UltraMsgResult& result,
                     const UltraMsgMessage& reply) {
    PendingRequest pending;
    {
        std::lock_guard<std::mutex> lock(endpoint->mutex);
        auto it = endpoint->requests.find(id);
        if (it == endpoint->requests.end()) return;
        pending = std::move(it->second);
        endpoint->requests.erase(it);
    }
    if (pending.async) {
        UltraMsgReplyCallback callback = std::move(pending.callback);
        Dispatch(endpoint, pending.direct, [callback, result, reply] { callback(result, reply); });
    } else if (pending.waiter) {
        {
            std::lock_guard<std::mutex> lock(pending.waiter->mutex);
            pending.waiter->reply = MessageToJson(reply);
            pending.waiter->reply.Set("__result", ResultToJson(result));
            pending.waiter->done = true;
        }
        pending.waiter->cv.notify_all();
    }
}

void HandleDeliver(const EndpointPtr& endpoint, const JSONValue& frame) {
    UltraMsgMessage message;
    if (!MessageFromJson(Obj(frame, "message"), message)) return;
    const uint64_t subscriptionId = static_cast<uint64_t>(Int(frame, "sub"));

    if (message.envelope.kind == UltraMsgKind::Reply) {
        CompleteRequest(endpoint, message.envelope.correlationId, ResultFromReplyBody(message), message);
        return;
    }

    std::shared_ptr<SubscriptionRec> subscription;
    {
        std::lock_guard<std::mutex> lock(endpoint->mutex);
        auto it = endpoint->subscriptions.find(subscriptionId);
        if (it != endpoint->subscriptions.end()) subscription = it->second;
    }
    if (!subscription) return;

    const bool autoAck = message.envelope.kind == UltraMsgKind::RecordedNotice &&
                         !subscription->options.manualAck;
    std::weak_ptr<EndpointRec> weak = endpoint;
    Dispatch(endpoint, subscription->options.onWorkerThread,
             [subscription, message, autoAck, weak] {
                 if (subscription->callback) subscription->callback(message);
                 if (autoAck) {
                     if (EndpointPtr ep = weak.lock()) SendAck(ep, message.envelope.id);
                 }
             });
}

void HandleBounce(const EndpointPtr& endpoint, const JSONValue& frame) {
    UltraMsgMessage original;
    if (!MessageFromJson(Obj(frame, "message"), original)) return;
    UltraMsgBounceCallback callback;
    {
        std::lock_guard<std::mutex> lock(endpoint->mutex);
        auto it = endpoint->bounces.find(original.envelope.id);
        if (it == endpoint->bounces.end()) return;
        callback = std::move(it->second);
        endpoint->bounces.erase(it);
    }
    if (callback) {
        original.envelope.kind = UltraMsgKind::Bounce;
        Dispatch(endpoint, false, [callback, original] { callback(original); });
    }
}

void HandleControlReply(const EndpointPtr& endpoint, const JSONValue& frame) {
    std::shared_ptr<PendingControl> pending;
    {
        std::lock_guard<std::mutex> lock(endpoint->mutex);
        auto it = endpoint->controls.find(Str(frame, "rid"));
        if (it == endpoint->controls.end()) return;
        pending = it->second;
        endpoint->controls.erase(it);
    }
    if (pending->onReply) pending->onReply(frame);
    {
        std::lock_guard<std::mutex> lock(pending->mutex);
        pending->reply = frame;
        pending->done = true;
    }
    pending->cv.notify_all();
}

void HandleWelcome(const EndpointPtr& endpoint, const JSONValue& frame) {
    {
        std::lock_guard<std::mutex> lock(endpoint->welcomeMutex);
        endpoint->welcomeOk = Bool(frame, "ok", false);
        endpoint->welcomeError = Str(frame, "error");
        endpoint->info.instanceId = Str(frame, "instance");
        endpoint->info.processId = static_cast<int>(Int(frame, "pid"));
        endpoint->info.verified = Bool(frame, "verified");
        endpoint->info.connectedAtMs = NowMs();
        BrokerInfoFromJson(Obj(frame, "broker"), endpoint->broker);
        endpoint->welcomed = true;
    }
    endpoint->welcomeCv.notify_all();
}

void FailEverythingPending(const EndpointPtr& endpoint) {
    std::map<std::string, std::shared_ptr<PendingControl>> controls;
    std::map<std::string, PendingRequest> requests;
    {
        std::lock_guard<std::mutex> lock(endpoint->mutex);
        controls.swap(endpoint->controls);
        requests.swap(endpoint->requests);
        endpoint->bounces.clear();
    }
    for (auto& [rid, pending] : controls) {
        {
            std::lock_guard<std::mutex> lock(pending->mutex);
            pending->reply = MakeFrame(Frame::Ctlr);
            pending->reply.Set("ok", false);
            pending->reply.Set("error", ResultToJson(NotConnected()));
            pending->done = true;
        }
        pending->cv.notify_all();
    }
    for (auto& [id, pending] : requests) {
        UltraMsgMessage empty;
        empty.envelope.kind = UltraMsgKind::Reply;
        empty.envelope.correlationId = id;
        if (pending.async && pending.callback) {
            UltraMsgReplyCallback callback = pending.callback;
            Dispatch(endpoint, pending.direct, [callback, empty] { callback(NotConnected(), empty); });
        } else if (pending.waiter) {
            {
                std::lock_guard<std::mutex> lock(pending.waiter->mutex);
                pending.waiter->reply = MessageToJson(empty);
                pending.waiter->reply.Set("__result", ResultToJson(NotConnected()));
                pending.waiter->done = true;
            }
            pending.waiter->cv.notify_all();
        }
    }
    {
        std::lock_guard<std::mutex> lock(endpoint->welcomeMutex);
        if (!endpoint->welcomed) {
            endpoint->welcomed = true;
            endpoint->welcomeOk = false;
            if (endpoint->welcomeError.empty()) endpoint->welcomeError = "connection closed before welcome";
        }
    }
    endpoint->welcomeCv.notify_all();
}

void ReaderLoop(EndpointPtr endpoint) {
    JSONValue frame;
    std::string error;
    while (!endpoint->closing.load() && endpoint->connection->ReceiveFrame(frame, error)) {
        const std::string type = Str(frame, "t");
        if (type == Frame::Deliver) HandleDeliver(endpoint, frame);
        else if (type == Frame::Ctlr) HandleControlReply(endpoint, frame);
        else if (type == Frame::Bounce) HandleBounce(endpoint, frame);
        else if (type == Frame::Welcome) HandleWelcome(endpoint, frame);
        else if (type == Frame::Ping) { JSONValue pong = MakeFrame(Frame::Pong); SendFrame(endpoint, pong); }
        // overflow, pong and unknown frames are ignored
    }
    endpoint->connected.store(false);
    FailEverythingPending(endpoint);
}

// Expires asynchronous requests; one thread for the process.
void TimerLoop() {
    Module& module = GetModule();
    while (module.timerRunning.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kTimerTickMs));
        std::vector<EndpointPtr> endpoints;
        {
            std::lock_guard<std::mutex> lock(module.mutex);
            for (auto& [handle, endpoint] : module.endpoints) endpoints.push_back(endpoint);
        }
        const int64_t now = NowMs();
        for (const auto& endpoint : endpoints) {
            std::vector<std::string> expired;
            {
                std::lock_guard<std::mutex> lock(endpoint->mutex);
                for (const auto& [id, pending] : endpoint->requests)
                    if (pending.async && pending.deadlineMs > 0 && pending.deadlineMs <= now) expired.push_back(id);
            }
            for (const auto& id : expired) {
                UltraMsgMessage empty;
                empty.envelope.kind = UltraMsgKind::Reply;
                empty.envelope.correlationId = id;
                CompleteRequest(endpoint, id,
                                UltraMsgResult::Error(UltraMsgResultCode::Timeout, "no reply within the timeout"),
                                empty);
            }
        }
    }
}

void EnsureTimer() {
    Module& module = GetModule();
    bool expected = false;
    if (module.timerRunning.compare_exchange_strong(expected, true)) {
        module.timer = std::thread(TimerLoop);
    }
}

// ---------------------------------------------------------------------------
// Control RPC
// ---------------------------------------------------------------------------

JSONValue Control(const EndpointPtr& endpoint, const char* op, JSONValue args, UltraMsgResult& result,
                  std::function<void(const JSONValue& reply)> onReply = nullptr) {
    JSONValue frame = MakeFrame(Frame::Ctl);
    const std::string rid = GenerateToken(12);
    frame.Set("rid", rid);
    frame.Set("op", op);
    frame.Set("args", std::move(args));

    auto pending = std::make_shared<PendingControl>();
    pending->onReply = std::move(onReply);
    {
        std::lock_guard<std::mutex> lock(endpoint->mutex);
        endpoint->controls[rid] = pending;
    }
    if (!SendFrame(endpoint, frame, &result)) {
        std::lock_guard<std::mutex> lock(endpoint->mutex);
        endpoint->controls.erase(rid);
        return JSONValue::MakeObject();
    }
    {
        std::unique_lock<std::mutex> lock(pending->mutex);
        if (!pending->cv.wait_for(lock, std::chrono::milliseconds(kControlTimeoutMs),
                                  [&] { return pending->done; })) {
            result = UltraMsgResult::Error(UltraMsgResultCode::Timeout, "the broker did not answer");
            std::lock_guard<std::mutex> guard(endpoint->mutex);
            endpoint->controls.erase(rid);
            return JSONValue::MakeObject();
        }
    }
    if (!Bool(pending->reply, "ok", false)) {
        result = ResultFromJson(Obj(pending->reply, "error"));
        if (result.ok) result = UltraMsgResult::Error(UltraMsgResultCode::Internal, "control call failed");
        return JSONValue::MakeObject();
    }
    result = UltraMsgResult::Ok();
    return Obj(pending->reply, "result");
}

// ---------------------------------------------------------------------------
// Sending
// ---------------------------------------------------------------------------

UltraMsgResult BuildMessage(const EndpointPtr& endpoint, UltraMsgKind kind, const std::string& topic,
                            const JSONValue& body, const UltraMsgSendOptions& options,
                            UltraMsgMessage& out) {
    if (!IsValidTopic(topic))
        return UltraMsgResult::Error(UltraMsgResultCode::InvalidArgument, "invalid topic: " + topic);
    if (IsControlTopic(topic))
        return UltraMsgResult::Error(UltraMsgResultCode::InvalidArgument, "control topics are reserved");
    // A reply answers on the request's topic but carries a result, not a
    // message of that topic; only what is published is checked.
    if (kind != UltraMsgKind::Reply) {
        if (UltraMsgResult validation = UltraMsg_Validate(topic, body); !validation) return validation;
    }
    for (const auto& a : options.attachments) {
        if (a.bytes.size() > UltraMsgMaxInlineAttachment)
            return UltraMsgResult::Error(UltraMsgResultCode::TooLarge,
                                         "attachment '" + a.name + "' exceeds the inline limit; pass a file path");
    }
    out.envelope.id = GenerateUlid();
    out.envelope.kind = kind;
    out.envelope.topic = topic;
    {
        std::lock_guard<std::mutex> lock(endpoint->welcomeMutex);
        out.envelope.from.appId = endpoint->info.appId;
        out.envelope.from.instanceId = endpoint->info.instanceId;
        out.envelope.from.processId = endpoint->info.processId;
        out.envelope.from.verified = endpoint->info.verified;
        out.envelope.from.displayName = endpoint->info.displayName;
    }
    out.envelope.to = options.to.empty() ? std::string("*") : options.to;
    out.envelope.conversation = options.conversation;
    out.envelope.timestampMs = NowMs();
    out.envelope.ttlSeconds = options.ttlSeconds;
    out.envelope.flags = options.flags;
    out.envelope.replaces = options.replaces;
    out.body = body.IsNull() ? JSONValue::MakeObject() : body;
    out.attachments = options.attachments;
    return UltraMsgResult::Ok();
}

UltraMsgResult SendMessage(const EndpointPtr& endpoint, const UltraMsgMessage& message) {
    JSONValue frame = MakeFrame(Frame::Msg);
    frame.Set("message", MessageToJson(message));
    UltraMsgResult result = UltraMsgResult::Ok();
    SendFrame(endpoint, frame, &result);
    return result;
}

} // namespace

// ===========================================================================
// Lifecycle
// ===========================================================================

UltraMsgResult UltraMsg_Initialize() {
    GetModule();
    return UltraMsgResult::Ok();
}

std::string UltraMsg_GetVersion() { return kVersion; }

void UltraMsg_Shutdown() {
    Module& module = GetModule();
    std::vector<UltraMsgHandle> handles;
    {
        std::lock_guard<std::mutex> lock(module.mutex);
        for (auto& [handle, endpoint] : module.endpoints) handles.push_back(handle);
    }
    for (UltraMsgHandle handle : handles) UltraMsg_Disconnect(handle);
    if (module.timerRunning.exchange(false)) {
        if (module.timer.joinable()) module.timer.join();
    }
    std::lock_guard<std::mutex> lock(module.brokerMutex);
    if (module.broker) {
        module.broker->Stop();
        module.broker.reset();
    }
}

bool UltraMsg_IsAvailable(const std::string& busPath) {
    const std::string bus = busPath.empty() ? DefaultBusPath() : busPath;
    {
        Module& module = GetModule();
        std::lock_guard<std::mutex> lock(module.brokerMutex);
        if (module.broker && module.broker->IsRunning() && module.broker->GetConfig().busPath == bus)
            return true;
    }
    std::string error;
    ConnectionPtr probe = ConnectToBus(bus, 500, error);
    if (!probe) return false;
    probe->Close();
    return true;
}

void UltraMsg_SetUIDispatcher(UltraMsgDispatcher dispatcher) {
    Module& module = GetModule();
    std::lock_guard<std::mutex> lock(module.mutex);
    module.dispatcher = std::move(dispatcher);
}

bool UltraMsg_HasUIDispatcher() {
    Module& module = GetModule();
    std::lock_guard<std::mutex> lock(module.mutex);
    return static_cast<bool>(module.dispatcher);
}

int UltraMsg_ProcessPending(UltraMsgHandle handle) {
    Module& module = GetModule();
    std::vector<EndpointPtr> endpoints;
    {
        std::lock_guard<std::mutex> lock(module.mutex);
        if (handle == UltraMsgInvalidHandle) {
            for (auto& [h, endpoint] : module.endpoints) endpoints.push_back(endpoint);
        } else if (auto it = module.endpoints.find(handle); it != module.endpoints.end()) {
            endpoints.push_back(it->second);
        }
    }
    int ran = 0;
    for (const auto& endpoint : endpoints) {
        std::deque<std::function<void()>> tasks;
        {
            std::lock_guard<std::mutex> lock(endpoint->mutex);
            tasks.swap(endpoint->pending);
        }
        for (auto& task : tasks) {
            task();
            ++ran;
        }
    }
    return ran;
}

// ===========================================================================
// Endpoint
// ===========================================================================

UltraMsgHandle UltraMsg_Connect(const UltraMsgConnectOptions& options, UltraMsgResult* error) {
    auto fail = [&](UltraMsgResultCode code, const std::string& message) {
        if (error) *error = UltraMsgResult::Error(code, message);
        return UltraMsgInvalidHandle;
    };
    if (!IsValidAppId(options.appId)) return fail(UltraMsgResultCode::InvalidArgument, "invalid app id: " + options.appId);

    Module& module = GetModule();
    const std::string bus = options.busPath.empty() ? DefaultBusPath() : options.busPath;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(options.connectTimeoutMs > 0 ? options.connectTimeoutMs : 2000);

    ConnectionPtr connection;
    std::string connectError;
    for (;;) {
        connection = ConnectToBus(bus, 500, connectError);
        if (connection) break;
        if (!options.startBrokerIfAbsent) return fail(UltraMsgResultCode::BrokerUnavailable, connectError);
        bool alreadyInUse = false;
        {
            std::lock_guard<std::mutex> lock(module.brokerMutex);
            if (!module.broker || !module.broker->IsRunning()) {
                auto broker = std::make_unique<Broker>();
                Broker::Config config;
                config.busPath = bus;
                config.journalPath = options.journalPath;
                UltraMsgResult started = broker->Start(config, alreadyInUse);
                if (started.ok) {
                    module.broker = std::move(broker);
                } else if (!alreadyInUse) {
                    return fail(UltraMsgResultCode::BrokerUnavailable, "cannot start a broker: " + started.message);
                }
            }
        }
        if (alreadyInUse) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        if (std::chrono::steady_clock::now() > deadline)
            return fail(UltraMsgResultCode::BrokerUnavailable, "no broker reachable on " + bus);
    }

    auto endpoint = std::make_shared<EndpointRec>();
    endpoint->options = options;
    endpoint->connection = connection;
    endpoint->connected.store(true);
    endpoint->info.appId = options.appId;
    endpoint->info.displayName = options.displayName;
    endpoint->info.iconPath = options.iconPath;
    endpoint->reader = std::thread([endpoint] { ReaderLoop(endpoint); });

    JSONValue hello = MakeFrame(Frame::Hello);
    hello.Set("app", options.appId);
    hello.Set("name", options.displayName);
    hello.Set("icon", options.iconPath);
    hello.Set("pid", static_cast<int64_t>(CurrentProcessId()));
    hello.Set("version", kVersion);
    UltraMsgResult sendResult;
    if (!SendFrame(endpoint, hello, &sendResult)) {
        endpoint->closing.store(true);
        connection->Close();
        endpoint->reader.join();
        return fail(UltraMsgResultCode::BrokerUnavailable, sendResult.message);
    }
    {
        std::unique_lock<std::mutex> lock(endpoint->welcomeMutex);
        endpoint->welcomeCv.wait_for(lock, std::chrono::milliseconds(kWelcomeTimeoutMs),
                                     [&] { return endpoint->welcomed; });
        if (!endpoint->welcomed || !endpoint->welcomeOk) {
            const std::string why = endpoint->welcomeError.empty() ? "the broker did not welcome us"
                                                                     : endpoint->welcomeError;
            lock.unlock();
            endpoint->closing.store(true);
            connection->Close();
            endpoint->reader.join();
            return fail(UltraMsgResultCode::BrokerUnavailable, why);
        }
    }
    {
        std::lock_guard<std::mutex> lock(module.mutex);
        endpoint->handle = module.nextHandle++;
        module.endpoints[endpoint->handle] = endpoint;
    }
    EnsureTimer();
    if (error) *error = UltraMsgResult::Ok();
    return endpoint->handle;
}

UltraMsgResult UltraMsg_Disconnect(UltraMsgHandle handle) {
    Module& module = GetModule();
    EndpointPtr endpoint;
    std::vector<UltraMsgHandle> subscriptionHandles;
    {
        std::lock_guard<std::mutex> lock(module.mutex);
        auto it = module.endpoints.find(handle);
        if (it == module.endpoints.end()) return NotConnected();
        endpoint = it->second;
        module.endpoints.erase(it);
        for (auto s = module.subscriptions.begin(); s != module.subscriptions.end();) {
            if (s->second->endpoint.lock() == endpoint) s = module.subscriptions.erase(s);
            else ++s;
        }
    }
    if (endpoint->connected.load()) {
        JSONValue bye = MakeFrame(Frame::Bye);
        SendFrame(endpoint, bye);
    }
    endpoint->closing.store(true);
    endpoint->connection->Close();
    if (endpoint->reader.joinable()) {
        if (endpoint->reader.get_id() == std::this_thread::get_id()) endpoint->reader.detach();
        else endpoint->reader.join();
    }
    {
        std::lock_guard<std::mutex> lock(endpoint->mutex);
        endpoint->subscriptions.clear();
        endpoint->pending.clear();
    }
    return UltraMsgResult::Ok();
}

bool UltraMsg_IsConnected(UltraMsgHandle handle) {
    EndpointPtr endpoint = FindEndpoint(handle);
    return endpoint && endpoint->connected.load();
}

UltraMsgResult UltraMsg_GetBrokerInfo(UltraMsgHandle handle, UltraMsgBrokerInfo& out) {
    EndpointPtr endpoint = FindEndpoint(handle);
    if (!endpoint) return NotConnected();
    UltraMsgResult result;
    JSONValue info = Control(endpoint, Op::BrokerInfo, JSONValue::MakeObject(), result);
    if (!result) return result;
    BrokerInfoFromJson(info, out);
    return UltraMsgResult::Ok();
}

UltraMsgResult UltraMsg_GetEndpointInfo(UltraMsgHandle handle, UltraMsgEndpointInfo& out) {
    EndpointPtr endpoint = FindEndpoint(handle);
    if (!endpoint) return NotConnected();
    std::lock_guard<std::mutex> lock(endpoint->welcomeMutex);
    out = endpoint->info;
    return UltraMsgResult::Ok();
}

UltraMsgResult UltraMsg_ListEndpoints(UltraMsgHandle handle, std::vector<UltraMsgEndpointInfo>& out) {
    out.clear();
    EndpointPtr endpoint = FindEndpoint(handle);
    if (!endpoint) return NotConnected();
    UltraMsgResult result;
    JSONValue reply = Control(endpoint, Op::ListEndpoints, JSONValue::MakeObject(), result);
    if (!result) return result;
    const JSONValue& list = Obj(reply, "endpoints");
    if (list.IsArray()) {
        for (const JSONValue& item : list.GetElements()) {
            UltraMsgEndpointInfo info;
            if (EndpointInfoFromJson(item, info)) out.push_back(std::move(info));
        }
    }
    return UltraMsgResult::Ok();
}

UltraMsgResult UltraMsg_ResolveApp(UltraMsgHandle handle, const std::string& appId,
                                   std::vector<std::string>& outInstanceIds) {
    outInstanceIds.clear();
    EndpointPtr endpoint = FindEndpoint(handle);
    if (!endpoint) return NotConnected();
    JSONValue args = JSONValue::MakeObject();
    args.Set("app", appId);
    UltraMsgResult result;
    JSONValue reply = Control(endpoint, Op::ResolveApp, std::move(args), result);
    if (!result) return result;
    outInstanceIds = StringsFromJson(Obj(reply, "instances"));
    return UltraMsgResult::Ok();
}

// ===========================================================================
// Sending
// ===========================================================================

UltraMsgResult UltraMsg_Post(UltraMsgHandle handle, const std::string& topic, const JSONValue& body,
                             const UltraMsgSendOptions& options, std::string* outMessageId) {
    EndpointPtr endpoint = FindEndpoint(handle);
    if (!endpoint) return NotConnected();
    UltraMsgMessage message;
    UltraMsgResult result = BuildMessage(endpoint, UltraMsgKind::Notice, topic, body, options, message);
    if (!result) return result;
    if (outMessageId) *outMessageId = message.envelope.id;
    return SendMessage(endpoint, message);
}

UltraMsgResult UltraMsg_PostRecorded(UltraMsgHandle handle, const std::string& topic, const JSONValue& body,
                                     const UltraMsgSendOptions& options, UltraMsgBounceCallback onBounce,
                                     std::string* outMessageId) {
    EndpointPtr endpoint = FindEndpoint(handle);
    if (!endpoint) return NotConnected();
    UltraMsgMessage message;
    UltraMsgResult result = BuildMessage(endpoint, UltraMsgKind::RecordedNotice, topic, body, options, message);
    if (!result) return result;
    if (outMessageId) *outMessageId = message.envelope.id;
    if (onBounce) {
        std::lock_guard<std::mutex> lock(endpoint->mutex);
        endpoint->bounces[message.envelope.id] = std::move(onBounce);
    }
    result = SendMessage(endpoint, message);
    if (!result) {
        std::lock_guard<std::mutex> lock(endpoint->mutex);
        endpoint->bounces.erase(message.envelope.id);
    }
    return result;
}

UltraMsgResult UltraMsg_Request(UltraMsgHandle handle, const std::string& target, const std::string& topic,
                                const JSONValue& body, int timeoutMs, UltraMsgMessage& outReply,
                                const UltraMsgSendOptions& options) {
    EndpointPtr endpoint = FindEndpoint(handle);
    if (!endpoint) return NotConnected();
    if (target.empty() || target == "*")
        return UltraMsgResult::Error(UltraMsgResultCode::InvalidArgument, "a request needs one target");
    UltraMsgSendOptions targeted = options;
    targeted.to = target;
    UltraMsgMessage message;
    UltraMsgResult result = BuildMessage(endpoint, UltraMsgKind::Request, topic, body, targeted, message);
    if (!result) return result;

    auto waiter = std::make_shared<PendingControl>();
    {
        std::lock_guard<std::mutex> lock(endpoint->mutex);
        PendingRequest pending;
        pending.async = false;
        pending.waiter = waiter;
        endpoint->requests[message.envelope.id] = std::move(pending);
    }
    result = SendMessage(endpoint, message);
    if (!result) {
        std::lock_guard<std::mutex> lock(endpoint->mutex);
        endpoint->requests.erase(message.envelope.id);
        return result;
    }
    {
        std::unique_lock<std::mutex> lock(waiter->mutex);
        const int wait = timeoutMs > 0 ? timeoutMs : 30000;
        if (!waiter->cv.wait_for(lock, std::chrono::milliseconds(wait), [&] { return waiter->done; })) {
            lock.unlock();
            std::lock_guard<std::mutex> guard(endpoint->mutex);
            endpoint->requests.erase(message.envelope.id);
            return UltraMsgResult::Error(UltraMsgResultCode::Timeout, "no reply within the timeout");
        }
    }
    MessageFromJson(waiter->reply, outReply);
    return ResultFromJson(Obj(waiter->reply, "__result"));
}

UltraMsgResult UltraMsg_RequestAsync(UltraMsgHandle handle, const std::string& target, const std::string& topic,
                                     const JSONValue& body, int timeoutMs, UltraMsgReplyCallback onReply,
                                     const UltraMsgSendOptions& options) {
    EndpointPtr endpoint = FindEndpoint(handle);
    if (!endpoint) return NotConnected();
    if (target.empty() || target == "*")
        return UltraMsgResult::Error(UltraMsgResultCode::InvalidArgument, "a request needs one target");
    if (!onReply) return UltraMsgResult::Error(UltraMsgResultCode::InvalidArgument, "a reply callback is required");
    UltraMsgSendOptions targeted = options;
    targeted.to = target;
    UltraMsgMessage message;
    UltraMsgResult result = BuildMessage(endpoint, UltraMsgKind::Request, topic, body, targeted, message);
    if (!result) return result;
    {
        std::lock_guard<std::mutex> lock(endpoint->mutex);
        PendingRequest pending;
        pending.async = true;
        pending.direct = options.replyOnWorkerThread;
        pending.callback = std::move(onReply);
        pending.deadlineMs = NowMs() + (timeoutMs > 0 ? timeoutMs : 30000);
        endpoint->requests[message.envelope.id] = std::move(pending);
    }
    result = SendMessage(endpoint, message);
    if (!result) {
        std::lock_guard<std::mutex> lock(endpoint->mutex);
        endpoint->requests.erase(message.envelope.id);
    }
    return result;
}

UltraMsgResult UltraMsg_Reply(UltraMsgHandle handle, const UltraMsgMessage& request, const JSONValue& body,
                              const std::vector<UltraMsgAttachment>& attachments) {
    EndpointPtr endpoint = FindEndpoint(handle);
    if (!endpoint) return NotConnected();
    if (request.envelope.kind != UltraMsgKind::Request || request.envelope.id.empty())
        return UltraMsgResult::Error(UltraMsgResultCode::InvalidArgument, "not a request");
    UltraMsgSendOptions options;
    options.to = request.envelope.from.instanceId;
    options.attachments = attachments;
    UltraMsgMessage reply;
    UltraMsgResult result = BuildMessage(endpoint, UltraMsgKind::Reply, request.envelope.topic, body, options, reply);
    if (!result) return result;
    reply.envelope.correlationId = request.envelope.id;
    return SendMessage(endpoint, reply);
}

UltraMsgResult UltraMsg_ReplyError(UltraMsgHandle handle, const UltraMsgMessage& request,
                                   const std::string& code, const std::string& message) {
    JSONValue error = JSONValue::MakeObject();
    error.Set("code", code);
    error.Set("message", message);
    JSONValue body = JSONValue::MakeObject();
    body.Set("error", std::move(error));
    return UltraMsg_Reply(handle, request, body);
}

UltraMsgResult UltraMsg_Acknowledge(UltraMsgHandle handle, const UltraMsgMessage& message) {
    EndpointPtr endpoint = FindEndpoint(handle);
    if (!endpoint) return NotConnected();
    if (message.envelope.id.empty())
        return UltraMsgResult::Error(UltraMsgResultCode::InvalidArgument, "message without an id");
    JSONValue frame = MakeFrame(Frame::Ack);
    frame.Set("id", message.envelope.id);
    UltraMsgResult result = UltraMsgResult::Ok();
    SendFrame(endpoint, frame, &result);
    return result;
}

// ===========================================================================
// Receiving
// ===========================================================================

UltraMsgHandle UltraMsg_Subscribe(UltraMsgHandle handle, const std::string& topicPattern, UltraMsgCallback callback,
                                  const UltraMsgSubscribeOptions& options, UltraMsgResult* error) {
    auto fail = [&](const UltraMsgResult& r) {
        if (error) *error = r;
        return UltraMsgInvalidHandle;
    };
    EndpointPtr endpoint = FindEndpoint(handle);
    if (!endpoint) return fail(NotConnected());
    if (!IsValidPattern(topicPattern))
        return fail(UltraMsgResult::Error(UltraMsgResultCode::InvalidArgument, "invalid pattern: " + topicPattern));
    if (!callback) return fail(UltraMsgResult::Error(UltraMsgResultCode::InvalidArgument, "a callback is required"));

    auto subscription = std::make_shared<SubscriptionRec>();
    subscription->pattern = topicPattern;
    subscription->callback = std::move(callback);
    subscription->options = options;
    subscription->endpoint = endpoint;

    JSONValue args = JSONValue::MakeObject();
    args.Set("pattern", topicPattern);
    args.Set("manualAck", options.manualAck);
    args.Set("includeOwn", options.includeOwn);
    args.Set("replaySince", options.replaySinceMs);
    UltraMsgResult result;
    Control(endpoint, Op::Subscribe, std::move(args), result, [&](const JSONValue& reply) {
        // On the reader thread, before any delivery for this subscription.
        if (!Bool(reply, "ok", false)) return;
        subscription->brokerId = static_cast<uint64_t>(Int(Obj(reply, "result"), "sub"));
        std::lock_guard<std::mutex> lock(endpoint->mutex);
        endpoint->subscriptions[subscription->brokerId] = subscription;
    });
    if (!result) return fail(result);
    Module& module = GetModule();
    std::lock_guard<std::mutex> lock(module.mutex);
    subscription->handle = module.nextHandle++;
    module.subscriptions[subscription->handle] = subscription;
    if (error) *error = UltraMsgResult::Ok();
    return subscription->handle;
}

UltraMsgResult UltraMsg_Unsubscribe(UltraMsgHandle subscriptionHandle) {
    Module& module = GetModule();
    std::shared_ptr<SubscriptionRec> subscription;
    {
        std::lock_guard<std::mutex> lock(module.mutex);
        auto it = module.subscriptions.find(subscriptionHandle);
        if (it == module.subscriptions.end())
            return UltraMsgResult::Error(UltraMsgResultCode::InvalidArgument, "unknown subscription");
        subscription = it->second;
        module.subscriptions.erase(it);
    }
    EndpointPtr endpoint = subscription->endpoint.lock();
    if (!endpoint) return UltraMsgResult::Ok();
    {
        std::lock_guard<std::mutex> lock(endpoint->mutex);
        endpoint->subscriptions.erase(subscription->brokerId);
    }
    if (!endpoint->connected.load()) return UltraMsgResult::Ok();
    JSONValue args = JSONValue::MakeObject();
    args.Set("sub", static_cast<int64_t>(subscription->brokerId));
    UltraMsgResult result;
    Control(endpoint, Op::Unsubscribe, std::move(args), result);
    return result;
}

// ===========================================================================
// Journal
// ===========================================================================

namespace {

UltraMsgResult JournalIds(UltraMsgHandle handle, const char* op, const std::vector<std::string>& ids) {
    EndpointPtr endpoint = FindEndpoint(handle);
    if (!endpoint) return NotConnected();
    JSONValue args = JSONValue::MakeObject();
    args.Set("ids", StringsToJson(ids));
    UltraMsgResult result;
    Control(endpoint, op, std::move(args), result);
    return result;
}

} // namespace

UltraMsgResult UltraMsg_Query(UltraMsgHandle handle, const UltraMsgQuery& query, std::vector<UltraMsgMessage>& out) {
    out.clear();
    EndpointPtr endpoint = FindEndpoint(handle);
    if (!endpoint) return NotConnected();
    JSONValue args = JSONValue::MakeObject();
    args.Set("query", QueryToJson(query));
    UltraMsgResult result;
    JSONValue reply = Control(endpoint, Op::JournalQuery, std::move(args), result);
    if (!result) return result;
    const JSONValue& list = Obj(reply, "messages");
    if (list.IsArray()) {
        for (const JSONValue& item : list.GetElements()) {
            UltraMsgMessage message;
            if (MessageFromJson(item, message)) out.push_back(std::move(message));
        }
    }
    return UltraMsgResult::Ok();
}

UltraMsgResult UltraMsg_Count(UltraMsgHandle handle, const UltraMsgQuery& query, int64_t& out) {
    out = 0;
    EndpointPtr endpoint = FindEndpoint(handle);
    if (!endpoint) return NotConnected();
    JSONValue args = JSONValue::MakeObject();
    args.Set("query", QueryToJson(query));
    UltraMsgResult result;
    JSONValue reply = Control(endpoint, Op::JournalCount, std::move(args), result);
    if (!result) return result;
    out = Int(reply, "count");
    return UltraMsgResult::Ok();
}

UltraMsgResult UltraMsg_GetMessage(UltraMsgHandle handle, const std::string& id, UltraMsgMessage& out) {
    EndpointPtr endpoint = FindEndpoint(handle);
    if (!endpoint) return NotConnected();
    JSONValue args = JSONValue::MakeObject();
    args.Set("id", id);
    UltraMsgResult result;
    JSONValue reply = Control(endpoint, Op::JournalGet, std::move(args), result);
    if (!result) return result;
    if (!MessageFromJson(Obj(reply, "message"), out))
        return UltraMsgResult::Error(UltraMsgResultCode::Internal, "malformed journal row");
    return UltraMsgResult::Ok();
}

UltraMsgResult UltraMsg_MarkRead(UltraMsgHandle handle, const std::vector<std::string>& ids) {
    return JournalIds(handle, Op::JournalMarkRead, ids);
}

UltraMsgResult UltraMsg_MarkUnread(UltraMsgHandle handle, const std::vector<std::string>& ids) {
    return JournalIds(handle, Op::JournalMarkUnread, ids);
}

UltraMsgResult UltraMsg_Dismiss(UltraMsgHandle handle, const std::vector<std::string>& ids) {
    return JournalIds(handle, Op::JournalDismiss, ids);
}

UltraMsgResult UltraMsg_Delete(UltraMsgHandle handle, const std::vector<std::string>& ids) {
    return JournalIds(handle, Op::JournalDelete, ids);
}

UltraMsgResult UltraMsg_ListConversations(UltraMsgHandle handle, const UltraMsgQuery& query,
                                          std::vector<UltraMsgConversation>& out) {
    out.clear();
    EndpointPtr endpoint = FindEndpoint(handle);
    if (!endpoint) return NotConnected();
    JSONValue args = JSONValue::MakeObject();
    args.Set("query", QueryToJson(query));
    UltraMsgResult result;
    JSONValue reply = Control(endpoint, Op::JournalConversations, std::move(args), result);
    if (!result) return result;
    const JSONValue& list = Obj(reply, "conversations");
    if (list.IsArray()) {
        for (const JSONValue& item : list.GetElements()) {
            UltraMsgConversation conversation;
            if (ConversationFromJson(item, conversation)) out.push_back(std::move(conversation));
        }
    }
    return UltraMsgResult::Ok();
}

UltraMsgResult UltraMsg_SetRetention(UltraMsgHandle handle, const std::string& topicPattern, int days,
                                     int64_t maxRows) {
    EndpointPtr endpoint = FindEndpoint(handle);
    if (!endpoint) return NotConnected();
    JSONValue args = JSONValue::MakeObject();
    args.Set("pattern", topicPattern);
    args.Set("days", static_cast<int64_t>(days));
    args.Set("maxRows", maxRows);
    UltraMsgResult result;
    Control(endpoint, Op::JournalRetention, std::move(args), result);
    return result;
}

UltraMsgResult UltraMsg_Export(UltraMsgHandle handle, const UltraMsgQuery& query, const std::string& path,
                               int64_t* outCount) {
    EndpointPtr endpoint = FindEndpoint(handle);
    if (!endpoint) return NotConnected();
    JSONValue args = JSONValue::MakeObject();
    args.Set("query", QueryToJson(query));
    args.Set("path", path);
    UltraMsgResult result;
    JSONValue reply = Control(endpoint, Op::JournalExport, std::move(args), result);
    if (!result) return result;
    if (outCount) *outCount = Int(reply, "count");
    return UltraMsgResult::Ok();
}
