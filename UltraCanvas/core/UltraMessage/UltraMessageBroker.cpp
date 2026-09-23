// UltraCanvas/core/UltraMessage/UltraMessageBroker.cpp
// The broker: one reader and one writer thread per session, routing under
// one mutex so a sender's messages stay in order, journal writes before
// fan-out, and a housekeeping thread that bounces unacknowledged recorded
// notices and applies journal retention.
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraMessageBroker.h"

#include <algorithm>
#include <chrono>

namespace UltraMessage {
namespace Internal {

namespace {

constexpr size_t kMaxOutboundFrames = 4096;
constexpr int kDefaultRecordedTtlSeconds = 5;
constexpr int kHousekeepingTickMs = 200;
constexpr int64_t kRetentionIntervalMs = 10 * 60 * 1000;

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

struct ThreadGuard {
    std::atomic<int>& counter;
    std::mutex& mutex;
    std::condition_variable& cv;
    ThreadGuard(std::atomic<int>& c, std::mutex& m, std::condition_variable& v) : counter(c), mutex(m), cv(v) {
        ++counter;
    }
    ~ThreadGuard() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            --counter;
        }
        cv.notify_all();
    }
};

} // namespace

Broker::Broker() = default;

Broker::~Broker() { Stop(); }

UltraMsgSender Broker::BrokerSender() const {
    UltraMsgSender s;
    s.appId = kAppId;
    s.instanceId = "broker";
    s.processId = CurrentProcessId();
    s.verified = true;
    s.displayName = "UltraMessage";
    return s;
}

UltraMsgEndpointInfo Broker::InfoOf(const Session& session) const {
    UltraMsgEndpointInfo info;
    info.appId = session.appId;
    info.instanceId = session.instanceId;
    info.displayName = session.displayName;
    info.iconPath = session.iconPath;
    info.processId = session.processId;
    info.verified = session.verified;
    info.connectedAtMs = session.connectedAtMs;
    return info;
}

UltraMsgBrokerInfo Broker::Info() const {
    UltraMsgBrokerInfo info;
    info.inProcess = true;
    info.busPath = config_.busPath;
    info.journalPath = journalOpen_ ? journal_.Path() : std::string();
    info.hostProcessId = CurrentProcessId();
    info.startedAtMs = startedAtMs_;
    info.version = UltraMsg_GetVersion();
    std::lock_guard<std::mutex> lock(mutex_);
    int count = 0;
    for (const auto& s : sessions_) if (s->greeted) ++count;
    info.endpointCount = count;
    return info;
}

// ===========================================================================
// Lifecycle
// ===========================================================================

UltraMsgResult Broker::Start(const Config& config, bool& alreadyInUse) {
    alreadyInUse = false;
    if (running_.load()) return UltraMsgResult::Ok();
    config_ = config;
    if (config_.busPath.empty()) config_.busPath = DefaultBusPath();
    if (config_.journalPath.empty()) config_.journalPath = DefaultJournalPath();

    std::string error;
    listener_ = Listener::Create(config_.busPath, error, alreadyInUse);
    if (!listener_) {
        return UltraMsgResult::Error(alreadyInUse ? UltraMsgResultCode::BrokerUnavailable
                                                  : UltraMsgResultCode::Internal,
                                     error);
    }

    UltraMsgResult journalResult = journal_.Open(config_.journalPath);
    journalOpen_ = journalResult.ok;
    journalError_ = journalResult.message;
    if (journalOpen_) journal_.ApplyRetention();

    startedAtMs_ = NowMs();
    running_.store(true);
    acceptThread_ = std::thread([this] { AcceptLoop(); });
    housekeepingThread_ = std::thread([this] { HousekeepingLoop(); });
    StartAdapters();
    return UltraMsgResult::Ok();
}

void Broker::Stop() {
    if (!running_.exchange(false)) return;
    StopAdapters();
    if (listener_) listener_->Close();
    housekeepingCv_.notify_all();
    if (acceptThread_.joinable()) acceptThread_.join();
    if (housekeepingThread_.joinable()) housekeepingThread_.join();

    std::vector<SessionPtr> sessions;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sessions = sessions_;
    }
    for (const auto& session : sessions) {
        {
            std::lock_guard<std::mutex> lock(session->outMutex);
            session->outClosed = true;
        }
        session->outCv.notify_all();
        session->connection->Close();
    }
    {
        std::unique_lock<std::mutex> lock(threadsMutex_);
        threadsCv_.wait_for(lock, std::chrono::seconds(5), [this] { return activeThreads_.load() == 0; });
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sessions_.clear();
        pendingRecords_.clear();
        pendingRequests_.clear();
    }
    listener_.reset();
    journal_.Close();
    journalOpen_ = false;
}

void Broker::AcceptLoop() {
    ThreadGuard guard(activeThreads_, threadsMutex_, threadsCv_);
    while (running_.load()) {
        ConnectionPtr connection = listener_->Accept();
        if (!connection) break;
        auto session = std::make_shared<Session>();
        session->connection = connection;
        session->connectedAtMs = NowMs();
        session->processId = connection->PeerProcessId();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            sessions_.push_back(session);
        }
        std::thread([this, session] { WriterLoop(session); }).detach();
        std::thread([this, session] { ReaderLoop(session); }).detach();
    }
}

void Broker::ReaderLoop(SessionPtr session) {
    ThreadGuard guard(activeThreads_, threadsMutex_, threadsCv_);
    JSONValue frame;
    std::string error;
    while (running_.load() && session->connection->ReceiveFrame(frame, error)) {
        HandleFrame(session, frame);
    }
    RemoveSession(session);
}

void Broker::WriterLoop(SessionPtr session) {
    ThreadGuard guard(activeThreads_, threadsMutex_, threadsCv_);
    for (;;) {
        std::string bytes;
        {
            std::unique_lock<std::mutex> lock(session->outMutex);
            session->outCv.wait(lock, [&] { return session->outClosed || !session->outbound.empty(); });
            if (session->outbound.empty()) return;
            bytes = std::move(session->outbound.front());
            session->outbound.pop_front();
        }
        std::string error;
        if (!session->connection->SendEncoded(bytes, error)) {
            session->connection->Close();
            return;
        }
    }
}

void Broker::HousekeepingLoop() {
    ThreadGuard guard(activeThreads_, threadsMutex_, threadsCv_);
    int64_t lastRetention = NowMs();
    std::mutex waitMutex;
    while (running_.load()) {
        {
            std::unique_lock<std::mutex> lock(waitMutex);
            housekeepingCv_.wait_for(lock, std::chrono::milliseconds(kHousekeepingTickMs),
                                     [this] { return !running_.load(); });
        }
        if (!running_.load()) break;

        std::vector<std::pair<SessionPtr, UltraMsgMessage>> bounces;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const int64_t now = NowMs();
            for (auto it = pendingRecords_.begin(); it != pendingRecords_.end();) {
                if (it->second.deadlineMs <= now) {
                    bounces.emplace_back(it->second.sender, it->second.original);
                    it = pendingRecords_.erase(it);
                } else {
                    ++it;
                }
            }
        }
        for (auto& [session, original] : bounces) SendBounce(session, original);

        if (journalOpen_ && NowMs() - lastRetention >= kRetentionIntervalMs) {
            journal_.ApplyRetention();
            lastRetention = NowMs();
        }
    }
}

// ===========================================================================
// Outbound
// ===========================================================================

void Broker::EnqueueEncoded(const SessionPtr& session, const std::string& bytes) {
    bool overflow = false;
    {
        std::lock_guard<std::mutex> lock(session->outMutex);
        if (session->outClosed) return;
        if (session->outbound.size() >= kMaxOutboundFrames) {
            ++session->dropped;
            overflow = session->dropped == 1 || (session->dropped % 100) == 0;
        } else {
            session->outbound.push_back(bytes);
        }
    }
    session->outCv.notify_one();
    if (overflow) {
        JSONValue frame = MakeFrame(Frame::Overflow);
        frame.Set("dropped", static_cast<int64_t>(session->dropped));
        // Bypasses the queue on purpose: the notice must reach a receiver
        // whose queue is full; it is tiny and rare.
        std::string error;
        session->connection->SendFrame(frame, error);
    }
}

void Broker::Enqueue(const SessionPtr& session, const JSONValue& frame) {
    EnqueueEncoded(session, EncodeFrame(frame));
}

void Broker::Deliver(const SessionPtr& session, uint64_t subscriptionId, const UltraMsgMessage& message) {
    JSONValue frame = MakeFrame(Frame::Deliver);
    frame.Set("sub", static_cast<int64_t>(subscriptionId));
    frame.Set("message", MessageToJson(message));
    Enqueue(session, frame);
}

void Broker::SendBounce(const SessionPtr& session, const UltraMsgMessage& original) {
    JSONValue frame = MakeFrame(Frame::Bounce);
    frame.Set("message", MessageToJson(original));
    Enqueue(session, frame);
}

void Broker::SendErrorReply(const SessionPtr& requester, const UltraMsgMessage& request,
                            UltraMsgResultCode code, const std::string& message) {
    UltraMsgMessage reply;
    reply.envelope.id = GenerateUlid();
    reply.envelope.kind = UltraMsgKind::Reply;
    reply.envelope.topic = request.envelope.topic;
    reply.envelope.from = BrokerSender();
    reply.envelope.to = requester->instanceId;
    reply.envelope.correlationId = request.envelope.id;
    reply.envelope.timestampMs = NowMs();
    JSONValue error = JSONValue::MakeObject();
    error.Set("code", UltraMsg_ResultCodeName(code));
    error.Set("message", message);
    reply.body = JSONValue::MakeObject();
    reply.body.Set("error", std::move(error));
    Deliver(requester, 0, reply);
}

void Broker::PublishLifecycle(const char* topic, const SessionPtr& session) {
    UltraMsgMessage notice;
    notice.envelope.id = GenerateUlid();
    notice.envelope.kind = UltraMsgKind::Notice;
    notice.envelope.topic = topic;
    notice.envelope.from = BrokerSender();
    notice.envelope.to = "*";
    notice.envelope.timestampMs = NowMs();
    notice.body = JSONValue::MakeObject();
    notice.body.Set("appId", session->appId);
    notice.body.Set("instanceId", session->instanceId);
    notice.body.Set("displayName", session->displayName);
    notice.body.Set("commands", false);
    Route(nullptr, notice);
}

// ===========================================================================
// Sessions
// ===========================================================================

void Broker::RemoveSession(const SessionPtr& session) {
    std::vector<std::pair<SessionPtr, UltraMsgMessage>> orphanedRequests;
    bool wasGreeted = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::find(sessions_.begin(), sessions_.end(), session);
        if (it == sessions_.end()) return;
        sessions_.erase(it);
        wasGreeted = session->greeted;
        for (auto p = pendingRequests_.begin(); p != pendingRequests_.end();) {
            if (p->second.target == session) {
                UltraMsgMessage request;
                request.envelope.id = p->first;
                request.envelope.topic = p->second.topic;
                orphanedRequests.emplace_back(p->second.requester, request);
                p = pendingRequests_.erase(p);
            } else if (p->second.requester == session) {
                p = pendingRequests_.erase(p);
            } else {
                ++p;
            }
        }
        for (auto r = pendingRecords_.begin(); r != pendingRecords_.end();) {
            if (r->second.sender == session) r = pendingRecords_.erase(r);
            else ++r;
        }
    }
    for (auto& [requester, request] : orphanedRequests)
        SendErrorReply(requester, request, UltraMsgResultCode::NoSuchTarget, "target disconnected");
    {
        std::lock_guard<std::mutex> lock(session->outMutex);
        session->outClosed = true;
    }
    session->outCv.notify_all();
    session->connection->Close();
    if (wasGreeted && running_.load()) PublishLifecycle(UltraMsgTopics::AppLifecycleStopping, session);
}

Broker::SessionPtr Broker::ResolveTargetLocked(const std::string& target) const {
    SessionPtr best;
    for (const auto& s : sessions_) {
        if (!s->greeted) continue;
        if (s->instanceId == target) return s;
        if (s->appId == target && (!best || s->connectedAtMs < best->connectedAtMs)) best = s;
    }
    return best;
}

// ===========================================================================
// Frames
// ===========================================================================

void Broker::HandleFrame(const SessionPtr& session, const JSONValue& frame) {
    const std::string type = Str(frame, "t");
    if (type == Frame::Hello) HandleHello(session, frame);
    else if (type == Frame::Ctl) HandleControl(session, frame);
    else if (type == Frame::Msg) HandleMessage(session, frame);
    else if (type == Frame::Ack) HandleAck(session, frame);
    else if (type == Frame::Ping) Enqueue(session, MakeFrame(Frame::Pong));
    else if (type == Frame::Bye) session->connection->Close();
    // Anything else is ignored: forward compatibility with newer endpoints.
}

void Broker::HandleHello(const SessionPtr& session, const JSONValue& frame) {
    const std::string appId = Str(frame, "app");
    JSONValue welcome = MakeFrame(Frame::Welcome);
    if (!IsValidAppId(appId)) {
        welcome.Set("ok", false);
        welcome.Set("error", "invalid app id: " + appId);
        Enqueue(session, welcome);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        session->appId = appId;
        session->displayName = Str(frame, "name");
        session->iconPath = Str(frame, "icon");
        session->instanceId = "i-" + GenerateToken(8);
        const int claimedPid = static_cast<int>(Int(frame, "pid"));
        const int peerPid = session->connection->PeerProcessId();
        if (peerPid > 0) {
            session->processId = peerPid;
            session->verified = claimedPid == 0 || claimedPid == peerPid;
        } else {
            session->processId = claimedPid;
            session->verified = false;
        }
        session->greeted = true;
    }
    welcome.Set("ok", true);
    welcome.Set("instance", session->instanceId);
    welcome.Set("pid", static_cast<int64_t>(session->processId));
    welcome.Set("verified", session->verified);
    welcome.Set("broker", BrokerInfoToJson(Info()));
    Enqueue(session, welcome);
    PublishLifecycle(UltraMsgTopics::AppLifecycleStarted, session);
}

void Broker::HandleAck(const SessionPtr&, const JSONValue& frame) {
    const std::string id = Str(frame, "id");
    std::lock_guard<std::mutex> lock(mutex_);
    pendingRecords_.erase(id);
}

void Broker::HandleControl(const SessionPtr& session, const JSONValue& frame) {
    const std::string rid = Str(frame, "rid");
    const std::string op = Str(frame, "op");
    UltraMsgResult result = UltraMsgResult::Ok();
    std::function<void()> after;
    JSONValue payload = RunControl(session, op, Obj(frame, "args"), result, after);
    JSONValue reply = MakeFrame(Frame::Ctlr);
    reply.Set("rid", rid);
    reply.Set("ok", result.ok);
    if (!result.ok) reply.Set("error", ResultToJson(result));
    reply.Set("result", std::move(payload));
    Enqueue(session, reply);
    if (after) after();
}

JSONValue Broker::RunControl(const SessionPtr& session, const std::string& op, const JSONValue& args,
                             UltraMsgResult& result, std::function<void()>& after) {
    JSONValue out = JSONValue::MakeObject();
    auto journalRequired = [&]() -> bool {
        if (journalOpen_) return true;
        result = UltraMsgResult::Error(UltraMsgResultCode::JournalError,
                                       journalError_.empty() ? "no journal" : journalError_);
        return false;
    };

    if (op == Op::Subscribe) {
        const std::string pattern = Str(args, "pattern");
        if (!IsValidPattern(pattern)) {
            result = UltraMsgResult::Error(UltraMsgResultCode::InvalidArgument, "invalid pattern: " + pattern);
            return out;
        }
        Subscription subscription;
        subscription.pattern = pattern;
        subscription.includeOwn = Bool(args, "includeOwn");
        subscription.manualAck = Bool(args, "manualAck");
        {
            std::lock_guard<std::mutex> lock(mutex_);
            subscription.id = nextSubscriptionId_++;
            session->subscriptions.push_back(subscription);
        }
        out.Set("sub", static_cast<int64_t>(subscription.id));
        const int64_t replaySince = Int(args, "replaySince");
        if (replaySince > 0 && journalOpen_) {
            const uint64_t subscriptionId = subscription.id;
            after = [this, session, pattern, replaySince, subscriptionId] {
                UltraMsgQuery query;
                query.topics = {pattern};
                query.sinceMs = replaySince;
                query.limit = 1000;
                std::vector<UltraMsgMessage> messages;
                if (journal_.Query(query, messages)) {
                    // Oldest first, so the subscriber sees history in order.
                    for (auto it = messages.rbegin(); it != messages.rend(); ++it)
                        Deliver(session, subscriptionId, *it);
                }
            };
        }
        return out;
    }
    if (op == Op::Unsubscribe) {
        const uint64_t id = static_cast<uint64_t>(Int(args, "sub"));
        std::lock_guard<std::mutex> lock(mutex_);
        auto& subs = session->subscriptions;
        subs.erase(std::remove_if(subs.begin(), subs.end(),
                                  [id](const Subscription& s) { return s.id == id; }),
                   subs.end());
        return out;
    }
    if (op == Op::ListEndpoints) {
        JSONValue list = JSONValue::MakeArray();
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& s : sessions_) if (s->greeted) list.Append(EndpointInfoToJson(InfoOf(*s)));
        out.Set("endpoints", std::move(list));
        return out;
    }
    if (op == Op::ResolveApp) {
        const std::string appId = Str(args, "app");
        std::vector<std::string> instances;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            std::vector<SessionPtr> matches;
            for (const auto& s : sessions_) if (s->greeted && s->appId == appId) matches.push_back(s);
            std::sort(matches.begin(), matches.end(),
                      [](const SessionPtr& a, const SessionPtr& b) { return a->connectedAtMs < b->connectedAtMs; });
            for (const auto& s : matches) instances.push_back(s->instanceId);
        }
        out.Set("instances", StringsToJson(instances));
        return out;
    }
    if (op == Op::BrokerInfo) {
        return BrokerInfoToJson(Info());
    }
    if (op == Op::JournalQuery) {
        if (!journalRequired()) return out;
        UltraMsgQuery query;
        QueryFromJson(Obj(args, "query"), query);
        std::vector<UltraMsgMessage> messages;
        result = journal_.Query(query, messages);
        JSONValue list = JSONValue::MakeArray();
        for (const auto& m : messages) list.Append(MessageToJson(m));
        out.Set("messages", std::move(list));
        return out;
    }
    if (op == Op::JournalCount) {
        if (!journalRequired()) return out;
        UltraMsgQuery query;
        QueryFromJson(Obj(args, "query"), query);
        int64_t count = 0;
        result = journal_.Count(query, count);
        out.Set("count", count);
        return out;
    }
    if (op == Op::JournalGet) {
        if (!journalRequired()) return out;
        UltraMsgMessage message;
        result = journal_.Get(Str(args, "id"), message);
        if (result.ok) out.Set("message", MessageToJson(message));
        return out;
    }
    if (op == Op::JournalMarkRead || op == Op::JournalMarkUnread) {
        if (!journalRequired()) return out;
        result = journal_.SetRead(StringsFromJson(Obj(args, "ids")), op == Op::JournalMarkRead);
        return out;
    }
    if (op == Op::JournalDismiss) {
        if (!journalRequired()) return out;
        result = journal_.Dismiss(StringsFromJson(Obj(args, "ids")));
        return out;
    }
    if (op == Op::JournalDelete) {
        if (!journalRequired()) return out;
        result = journal_.Delete(StringsFromJson(Obj(args, "ids")));
        return out;
    }
    if (op == Op::JournalConversations) {
        if (!journalRequired()) return out;
        UltraMsgQuery query;
        QueryFromJson(Obj(args, "query"), query);
        std::vector<UltraMsgConversation> conversations;
        result = journal_.ListConversations(query, conversations);
        JSONValue list = JSONValue::MakeArray();
        for (const auto& c : conversations) list.Append(ConversationToJson(c));
        out.Set("conversations", std::move(list));
        return out;
    }
    if (op == Op::JournalRetention) {
        if (!journalRequired()) return out;
        result = journal_.SetRetention(Str(args, "pattern"), static_cast<int>(Int(args, "days")),
                                       Int(args, "maxRows"));
        return out;
    }
    if (op == Op::JournalExport) {
        if (!journalRequired()) return out;
        UltraMsgQuery query;
        QueryFromJson(Obj(args, "query"), query);
        int64_t count = 0;
        result = journal_.Export(query, Str(args, "path"), count);
        out.Set("count", count);
        return out;
    }
    if (op == Op::AdaptersList) {
        JSONValue list = JSONValue::MakeArray();
        for (const auto& info : ListAdapters()) list.Append(AdapterInfoToJson(info));
        out.Set("adapters", std::move(list));
        return out;
    }
    if (op == Op::AdaptersEnable) {
        result = EnableAdapter(Str(args, "name"), Bool(args, "enabled", true));
        return out;
    }
    if (op == Op::AdaptersState) {
        UltraMsgAdapterState state;
        if (!GetAdapterState(Str(args, "name"), state)) {
            result = UltraMsgResult::Error(UltraMsgResultCode::InvalidArgument, "no adapter " + Str(args, "name"));
            return out;
        }
        out.Set("state", AdapterStateToJson(state));
        return out;
    }
    result = UltraMsgResult::Error(UltraMsgResultCode::InvalidArgument, "unknown control op: " + op);
    return out;
}

void Broker::HandleMessage(const SessionPtr& session, const JSONValue& frame) {
    if (!session->greeted) return;
    UltraMsgMessage message;
    if (!MessageFromJson(Obj(frame, "message"), message)) return;
    if (!IsValidTopic(message.envelope.topic) || IsControlTopic(message.envelope.topic)) return;
    Route(session, message);
}

// ===========================================================================
// Routing
// ===========================================================================

void Broker::Route(const SessionPtr& from, UltraMsgMessage& message) {
    UltraMsgEnvelope& e = message.envelope;
    if (from) {
        e.from.appId = from->appId;
        e.from.instanceId = from->instanceId;
        e.from.processId = from->processId;
        e.from.verified = from->verified;
        e.from.displayName = from->displayName;
    }
    if (e.id.empty()) e.id = GenerateUlid();
    if (e.timestampMs == 0) e.timestampMs = NowMs();
    if (e.to.empty()) e.to = "*";

    // Journal first (§7.4): what the feed shows is always something it can
    // find again.
    const bool journaled = journalOpen_ && !(e.flags & UltraMsgFlag_NoJournal) &&
                           (e.kind == UltraMsgKind::Notice || e.kind == UltraMsgKind::RecordedNotice) &&
                           ((e.flags & UltraMsgFlag_Persistent) || UltraMsg_IsPersistentTopic(e.topic));
    if (journaled) journal_.Store(message);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        RouteLocked(from, message);
    }
    // The feed acting on a notification an adapter produced (§9): outside the
    // routing lock, because the adapter may publish in response.
    if (e.kind == UltraMsgKind::Notice &&
        (e.topic == UltraMsgTopics::SystemNotificationAction ||
         e.topic == UltraMsgTopics::SystemNotificationDismissed))
        DispatchAction(message);
}

// ===========================================================================
// Adapters
// ===========================================================================

std::string Broker::AdapterHost::Publish(const std::string& adapterName, const std::string& topic,
                                         const JSONValue& body, const UltraMsgSendOptions& options) {
    if (!broker_.running_.load()) return std::string();
    if (!IsValidTopic(topic) || IsControlTopic(topic)) return std::string();
    if (!UltraMsg_Validate(topic, body)) return std::string();
    UltraMsgMessage message;
    message.envelope.id = GenerateUlid();
    message.envelope.kind = UltraMsgKind::Notice;
    message.envelope.topic = topic;
    message.envelope.from = AdapterSender(adapterName);
    message.envelope.to = options.to.empty() ? std::string("*") : options.to;
    message.envelope.conversation = options.conversation;
    message.envelope.timestampMs = NowMs();
    message.envelope.ttlSeconds = options.ttlSeconds;
    message.envelope.flags = options.flags;
    message.envelope.replaces = options.replaces;
    message.body = body.IsNull() ? JSONValue::MakeObject() : body;
    message.attachments = options.attachments;
    broker_.Route(nullptr, message);
    return message.envelope.id;
}

void Broker::AdapterHost::ReportState(const std::string& adapterName, const UltraMsgAdapterState& state) {
    std::lock_guard<std::mutex> lock(broker_.adaptersMutex_);
    if (AdapterSlot* slot = broker_.FindAdapterLocked(adapterName)) slot->state = state;
}

Broker::AdapterSlot* Broker::FindAdapterLocked(const std::string& name) {
    for (auto& slot : adapters_) if (slot.adapter->Name() == name) return &slot;
    return nullptr;
}

void Broker::StartAdapters() {
    std::vector<std::unique_ptr<IAdapter>> created = CreateBuiltinAdapters();
    std::vector<IAdapter*> toStart;
    {
        std::lock_guard<std::mutex> lock(adaptersMutex_);
        adapters_.clear();
        for (auto& adapter : created) {
            AdapterSlot slot;
            slot.enabled = adapter->EnabledByDefault();
            if (journalOpen_) {
                bool enabled = false, found = false;
                if (journal_.GetAdapterEnabled(adapter->Name(), enabled, found) && found) slot.enabled = enabled;
            }
            slot.state.status = slot.enabled ? UltraMsgAdapterStatus::Starting : UltraMsgAdapterStatus::Disabled;
            slot.adapter = std::move(adapter);
            if (slot.enabled) toStart.push_back(slot.adapter.get());
            adapters_.push_back(std::move(slot));
        }
    }
    // Start outside the lock: an adapter may report its state while starting.
    for (IAdapter* adapter : toStart) {
        const UltraMsgAdapterState state = adapter->Start(adapterHost_);
        std::lock_guard<std::mutex> lock(adaptersMutex_);
        if (AdapterSlot* slot = FindAdapterLocked(adapter->Name())) slot->state = state;
    }
}

void Broker::StopAdapters() {
    std::vector<IAdapter*> running;
    {
        std::lock_guard<std::mutex> lock(adaptersMutex_);
        for (auto& slot : adapters_)
            if (slot.state.status != UltraMsgAdapterStatus::Disabled) running.push_back(slot.adapter.get());
    }
    for (IAdapter* adapter : running) adapter->Stop();
    std::lock_guard<std::mutex> lock(adaptersMutex_);
    adapters_.clear();
}

void Broker::DispatchAction(const UltraMsgMessage& message) {
    std::vector<IAdapter*> running;
    {
        std::lock_guard<std::mutex> lock(adaptersMutex_);
        for (auto& slot : adapters_)
            if (slot.state.status == UltraMsgAdapterStatus::Running) running.push_back(slot.adapter.get());
    }
    for (IAdapter* adapter : running)
        if (adapter->HandleAction(message)) break;
}

std::vector<UltraMsgAdapterInfo> Broker::ListAdapters() const {
    std::vector<UltraMsgAdapterInfo> out;
    std::lock_guard<std::mutex> lock(adaptersMutex_);
    for (const auto& slot : adapters_) {
        UltraMsgAdapterInfo info;
        info.name = slot.adapter->Name();
        info.description = slot.adapter->Description();
        info.platform = slot.adapter->Platform();
        info.enabled = slot.enabled;
        info.state = slot.state;
        if (slot.enabled && slot.state.status != UltraMsgAdapterStatus::Disabled) info.state = slot.adapter->State();
        out.push_back(std::move(info));
    }
    return out;
}

bool Broker::GetAdapterState(const std::string& name, UltraMsgAdapterState& out) const {
    std::lock_guard<std::mutex> lock(adaptersMutex_);
    for (const auto& slot : adapters_) {
        if (slot.adapter->Name() != name) continue;
        out = slot.enabled && slot.state.status != UltraMsgAdapterStatus::Disabled ? slot.adapter->State()
                                                                                    : slot.state;
        return true;
    }
    return false;
}

UltraMsgResult Broker::EnableAdapter(const std::string& name, bool enabled) {
    IAdapter* adapter = nullptr;
    bool wasEnabled = false;
    {
        std::lock_guard<std::mutex> lock(adaptersMutex_);
        AdapterSlot* slot = FindAdapterLocked(name);
        if (!slot) return UltraMsgResult::Error(UltraMsgResultCode::InvalidArgument, "no adapter " + name);
        adapter = slot->adapter.get();
        wasEnabled = slot->enabled;
        slot->enabled = enabled;
        if (enabled && !wasEnabled) slot->state.status = UltraMsgAdapterStatus::Starting;
    }
    if (journalOpen_) journal_.SetAdapterEnabled(name, enabled);
    if (enabled && !wasEnabled) {
        const UltraMsgAdapterState state = adapter->Start(adapterHost_);
        std::lock_guard<std::mutex> lock(adaptersMutex_);
        if (AdapterSlot* slot = FindAdapterLocked(name)) slot->state = state;
    } else if (!enabled && wasEnabled) {
        adapter->Stop();
        std::lock_guard<std::mutex> lock(adaptersMutex_);
        if (AdapterSlot* slot = FindAdapterLocked(name)) {
            slot->state = UltraMsgAdapterState{};
            slot->state.status = UltraMsgAdapterStatus::Disabled;
        }
    }
    return UltraMsgResult::Ok();
}

void Broker::RouteLocked(const SessionPtr& from, UltraMsgMessage& message) {
    UltraMsgEnvelope& e = message.envelope;
    switch (e.kind) {
        case UltraMsgKind::Notice:
        case UltraMsgKind::RecordedNotice: {
            size_t delivered = 0;
            for (const auto& session : sessions_) {
                if (!session->greeted) continue;
                if (e.to != "*" && e.to != session->appId && e.to != session->instanceId) continue;
                for (const auto& sub : session->subscriptions) {
                    if (session == from && !sub.includeOwn) continue;
                    if (!TopicMatches(sub.pattern, e.topic)) continue;
                    Deliver(session, sub.id, message);
                    ++delivered;
                }
            }
            if (e.kind == UltraMsgKind::RecordedNotice && from) {
                if (delivered == 0) {
                    SendBounce(from, message);
                } else {
                    const int ttl = e.ttlSeconds > 0 ? e.ttlSeconds : kDefaultRecordedTtlSeconds;
                    PendingRecord record;
                    record.original = message;
                    record.sender = from;
                    record.deadlineMs = NowMs() + static_cast<int64_t>(ttl) * 1000;
                    pendingRecords_[e.id] = std::move(record);
                }
            }
            break;
        }
        case UltraMsgKind::Request: {
            if (!from) break;
            if (e.to == "*") {
                SendErrorReply(from, message, UltraMsgResultCode::InvalidArgument,
                               "a request needs one target");
                break;
            }
            SessionPtr target = ResolveTargetLocked(e.to);
            if (!target) {
                SendErrorReply(from, message, UltraMsgResultCode::NoSuchTarget, "no endpoint " + e.to);
                break;
            }
            const Subscription* handler = nullptr;
            for (const auto& sub : target->subscriptions) {
                if (TopicMatches(sub.pattern, e.topic)) {
                    handler = &sub;
                    break;
                }
            }
            if (!handler) {
                SendErrorReply(from, message, UltraMsgResultCode::NotHandled,
                               target->appId + " has no subscription for " + e.topic);
                break;
            }
            pendingRequests_[e.id] = PendingRequest{from, target, e.topic};
            Deliver(target, handler->id, message);
            break;
        }
        case UltraMsgKind::Reply: {
            auto it = pendingRequests_.find(e.correlationId);
            if (it == pendingRequests_.end()) break;   // late reply; the requester gave up
            SessionPtr requester = it->second.requester;
            pendingRequests_.erase(it);
            e.to = requester->instanceId;
            Deliver(requester, 0, message);
            break;
        }
        case UltraMsgKind::Bounce:
            break;   // only the broker produces bounces
    }
}

} // namespace Internal
} // namespace UltraMessage
