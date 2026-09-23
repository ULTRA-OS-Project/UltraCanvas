// UltraCanvas/core/UltraMessage/UltraMessageBroker.h
// The per-user broker (§7): owns the bus, the endpoint directory, the
// subscriptions, delivery semantics (notice, recorded notice with bounce,
// request/reply) and the journal. Hosted in-process by whichever endpoint
// found no broker running, or by the ULTRA OS shell.
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMessageAdapter.h"
#include "UltraMessageInternal.h"
#include "UltraMessageJournal.h"
#include "UltraMessageTransport.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace UltraMessage {
namespace Internal {

class Broker {
public:
    struct Config {
        std::string busPath;
        std::string journalPath;     // ":memory:" for a RAM journal; empty = platform default
    };

    static constexpr const char* kAppId = "org.ultraos.ultramessage.broker";

    Broker();
    ~Broker();
    Broker(const Broker&) = delete;
    Broker& operator=(const Broker&) = delete;

    // `alreadyInUse` reports another broker on the bus (connect to it instead).
    UltraMsgResult Start(const Config& config, bool& alreadyInUse);
    void Stop();
    bool IsRunning() const { return running_.load(); }
    UltraMsgBrokerInfo Info() const;
    const Config& GetConfig() const { return config_; }

    // Adapters (§9): the switch is persisted in the journal.
    std::vector<UltraMsgAdapterInfo> ListAdapters() const;
    UltraMsgResult EnableAdapter(const std::string& name, bool enabled);
    bool GetAdapterState(const std::string& name, UltraMsgAdapterState& out) const;

private:
    // What the adapters see of the broker.
    class AdapterHost final : public IAdapterHost {
    public:
        explicit AdapterHost(Broker& broker) : broker_(broker) {}
        std::string Publish(const std::string& adapterName, const std::string& topic,
                            const JSONValue& body, const UltraMsgSendOptions& options) override;
        void ReportState(const std::string& adapterName, const UltraMsgAdapterState& state) override;
    private:
        Broker& broker_;
    };

    struct AdapterSlot {
        std::unique_ptr<IAdapter> adapter;
        bool enabled = false;
        UltraMsgAdapterState state;
    };

    void StartAdapters();
    void StopAdapters();
    void DispatchAction(const UltraMsgMessage& message);
    AdapterSlot* FindAdapterLocked(const std::string& name);

    struct Subscription {
        uint64_t id = 0;
        std::string pattern;
        bool includeOwn = false;
        bool manualAck = false;
    };

    struct Session {
        ConnectionPtr connection;
        std::string appId;
        std::string instanceId;
        std::string displayName;
        std::string iconPath;
        int processId = 0;
        bool verified = false;
        bool greeted = false;
        int64_t connectedAtMs = 0;
        std::vector<Subscription> subscriptions;

        std::mutex outMutex;
        std::condition_variable outCv;
        std::deque<std::string> outbound;
        bool outClosed = false;
        uint64_t dropped = 0;
    };
    using SessionPtr = std::shared_ptr<Session>;

    struct PendingRecord {
        UltraMsgMessage original;
        SessionPtr sender;
        int64_t deadlineMs = 0;
    };

    struct PendingRequest {
        SessionPtr requester;
        SessionPtr target;
        std::string topic;
    };

    void AcceptLoop();
    void ReaderLoop(SessionPtr session);
    void WriterLoop(SessionPtr session);
    void HousekeepingLoop();

    void HandleFrame(const SessionPtr& session, const JSONValue& frame);
    void HandleHello(const SessionPtr& session, const JSONValue& frame);
    void HandleControl(const SessionPtr& session, const JSONValue& frame);
    void HandleMessage(const SessionPtr& session, const JSONValue& frame);
    void HandleAck(const SessionPtr& session, const JSONValue& frame);
    // `after` runs once the reply frame is queued (a replay must follow the
    // subscription id the endpoint registers from that reply).
    JSONValue RunControl(const SessionPtr& session, const std::string& op, const JSONValue& args,
                         UltraMsgResult& result, std::function<void()>& after);

    void Route(const SessionPtr& from, UltraMsgMessage& message);
    void RouteLocked(const SessionPtr& from, UltraMsgMessage& message);
    void RemoveSession(const SessionPtr& session);

    void Enqueue(const SessionPtr& session, const JSONValue& frame);
    void EnqueueEncoded(const SessionPtr& session, const std::string& bytes);
    void Deliver(const SessionPtr& session, uint64_t subscriptionId, const UltraMsgMessage& message);
    void SendBounce(const SessionPtr& session, const UltraMsgMessage& original);
    void SendErrorReply(const SessionPtr& requester, const UltraMsgMessage& request,
                        UltraMsgResultCode code, const std::string& message);
    void PublishLifecycle(const char* topic, const SessionPtr& session);

    SessionPtr ResolveTargetLocked(const std::string& target) const;
    UltraMsgSender BrokerSender() const;
    UltraMsgEndpointInfo InfoOf(const Session& session) const;

    Config config_;
    std::atomic<bool> running_{false};
    std::atomic<int> activeThreads_{0};
    std::mutex threadsMutex_;
    std::condition_variable threadsCv_;

    std::unique_ptr<Listener> listener_;
    std::thread acceptThread_;
    std::thread housekeepingThread_;
    std::condition_variable housekeepingCv_;

    mutable std::mutex mutex_;
    std::vector<SessionPtr> sessions_;
    std::map<std::string, PendingRecord> pendingRecords_;
    std::map<std::string, PendingRequest> pendingRequests_;
    uint64_t nextSubscriptionId_ = 1;
    int64_t startedAtMs_ = 0;

    Journal journal_;
    bool journalOpen_ = false;
    std::string journalError_;

    AdapterHost adapterHost_{*this};
    mutable std::mutex adaptersMutex_;
    std::vector<AdapterSlot> adapters_;
};

} // namespace Internal
} // namespace UltraMessage
