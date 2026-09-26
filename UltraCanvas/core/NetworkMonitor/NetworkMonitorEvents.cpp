// core/NetworkMonitor/NetworkMonitorEvents.cpp
// The event registry: the sources it runs, the listeners, the ring of
// recent events, the naming and best-effort attribution every event gets
// on its way through, and the snapshot differ - the one source that runs
// everywhere. The platform sources live under OS/<Platform>/; the null
// factory here covers every platform without one.
//
// Attribution: a source that reports no process (conntrack) hands the
// registry a bare tuple. The registry looks it up in a socket table it
// refreshes at most a few times a second, in either orientation - a tuple
// whose "source" is the remote side is a connection a listener here
// accepted - and remembers what it found, so the Closed event that follows
// a matched Opened is attributed too, though the socket is gone by then.
//
// Version: 0.8.0
// Last Modified: 2026-09-23
// Author: UltraCanvas Framework / ULTRA OS
#include "NetworkMonitor/NetworkMonitorEvents.h"
#include "NetworkMonitor/NetworkMonitorBackend.h"
#include "NetworkMonitor/NetworkMonitorNames.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace UltraCanvas {

#if !defined(__linux__) && !defined(_WIN32)
// macOS has no connection events without a Network Extension; the BSDs
// and everything else have no backend at all. The differ covers them.
std::unique_ptr<IConnectionEventSource> NetworkMonitor_CreateSystemEventSource() {
    return nullptr;
}
#endif

// ===== NetworkConnectionEvent =====

bool NetworkConnectionEvent::IsLoopback() const {
    auto loopback = [](const std::string& address) {
        return address.rfind("127.", 0) == 0 || address == "::1" ||
               address.rfind("::ffff:127.", 0) == 0;
    };
    return loopback(localAddress) || loopback(remoteAddress);
}

std::string NetworkConnectionEvent::LocalEndpoint() const {
    return NetworkMonitor_FormatEndpoint(localAddress, localPort);
}

std::string NetworkConnectionEvent::RemoteEndpoint() const {
    return NetworkMonitor_FormatEndpoint(remoteAddress, remotePort);
}

int64_t NetworkMonitor_NowMs() {
    return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

const char* NetworkMonitor_EventKindName(NetworkEventKind kind) {
    switch (kind) {
        case NetworkEventKind::Opened:   return "opened";
        case NetworkEventKind::Accepted: return "accepted";
        case NetworkEventKind::Closed:   return "closed";
        default:                         return "unknown";
    }
}

namespace {

using Clock = std::chrono::steady_clock;

std::string FlowKey(NetworkTransport transport, NetworkAddressFamily family, const std::string& local,
                    uint16_t localPort, const std::string& remote, uint16_t remotePort) {
    return std::to_string(static_cast<int>(transport)) + '|' + std::to_string(static_cast<int>(family)) + '|' +
           local + '|' + std::to_string(localPort) + '|' + remote + '|' + std::to_string(remotePort);
}

std::string EventKey(const NetworkConnectionEvent& e) {
    return FlowKey(e.transport, e.family, e.localAddress, e.localPort, e.remoteAddress, e.remotePort);
}

// ===== ATTRIBUTION =====

// What an Opened or Accepted told about a flow, kept so its Closed gets
// the same although the socket is gone by then.
struct Remembered {
    std::optional<ProcessIdentity> process;
    LoopbackRole                   loopbackRole = LoopbackRole::None;
    std::string                    localPeer;
    std::vector<std::string>       forProcesses;
};

struct Attribution {
    std::mutex mutex;
    std::vector<NetworkConnection> table;   // decoded: ListConnections ran DecodeLoopback
    Clock::time_point taken{};
    std::unordered_map<std::string, Remembered> remembered;
    std::deque<std::string> rememberedOrder;
};

// Never destroyed: the event-source worker threads can still be running at exit, and a lazy
// static first built on it would be torn down under it.
Attribution& TheAttribution() {
    static Attribution* attribution = new Attribution;
    return *attribution;
}

constexpr auto kTableMaxAge = std::chrono::milliseconds(300);
// A tuple the table lacks is often a connection younger than the table:
// one re-read for it, at most this often.
constexpr auto kTableMinAgeOnMiss = std::chrono::milliseconds(20);
constexpr std::size_t kRememberedCapacity = 20000;

void Remember(Attribution& a, const std::string& key, const NetworkConnectionEvent& e) {
    Remembered kept;
    kept.process = e.process;
    kept.loopbackRole = e.loopbackRole;
    kept.localPeer = e.localPeer;
    kept.forProcesses = e.forProcesses;
    if (a.remembered.insert_or_assign(key, std::move(kept)).second) {
        a.rememberedOrder.push_back(key);
        while (a.rememberedOrder.size() > kRememberedCapacity) {
            a.remembered.erase(a.rememberedOrder.front());
            a.rememberedOrder.pop_front();
        }
    }
}

void TakeChain(NetworkConnectionEvent& e, const NetworkConnection& c) {
    e.loopbackRole = c.loopbackRole;
    e.localPeer = c.localPeer ? c.localPeer->Label() : std::string();
    e.forProcesses = c.forProcesses;
    e.chainDecoded = true;
}

// The process, where the source did not know it, and the loopback chain,
// where the source's table was not decoded: both from the socket table,
// refreshed at most a few times a second, for an Opened or Accepted; from
// what that Opened told, for a Closed.
void Attribute(NetworkConnectionEvent& e) {
    if (e.process && e.chainDecoded) return;
    Attribution& a = TheAttribution();
    std::lock_guard<std::mutex> lock(a.mutex);

    if (e.kind == NetworkEventKind::Closed) {
        auto found = a.remembered.find(EventKey(e));
        if (found == a.remembered.end()) {
            // Maybe it was opened the other way round.
            found = a.remembered.find(FlowKey(e.transport, e.family, e.remoteAddress, e.remotePort,
                                              e.localAddress, e.localPort));
            if (found != a.remembered.end()) {
                std::swap(e.localAddress, e.remoteAddress);
                std::swap(e.localPort, e.remotePort);
            }
        }
        if (found != a.remembered.end()) {
            if (!e.process) e.process = found->second.process;
            if (!e.chainDecoded) {
                e.loopbackRole = found->second.loopbackRole;
                e.localPeer = found->second.localPeer;
                e.forProcesses = found->second.forProcesses;
                e.chainDecoded = true;
            }
            a.remembered.erase(found);
        }
        return;
    }

    auto refresh = [&a]() {
        NetworkMonitorOptions options;
        options.resolveNames = false;
        if (NetworkMonitor_ListConnections(a.table, options)) a.taken = Clock::now();
    };
    if (a.table.empty() || Clock::now() - a.taken > kTableMaxAge) refresh();
    const NetworkConnection* listener = nullptr;
    bool found = false;
    for (int attempt = 0; attempt < 2 && !found; ++attempt) {
        if (attempt == 1) {
            // Not in the table: a connection younger than it, as often as
            // not. Read again, unless the table is fresher than the limit.
            if (Clock::now() - a.taken < kTableMinAgeOnMiss) break;
            refresh();
            listener = nullptr;
        }
        for (const auto& c : a.table) {
            if (c.transport != e.transport) continue;
            const bool same = c.localAddress == e.localAddress && c.localPort == e.localPort &&
                              c.remoteAddress == e.remoteAddress && c.remotePort == e.remotePort;
            const bool reversed = c.localAddress == e.remoteAddress && c.localPort == e.remotePort &&
                                  c.remoteAddress == e.localAddress && c.remotePort == e.localPort;
            if (same || reversed) {
                if (reversed && !e.process) {
                    std::swap(e.localAddress, e.remoteAddress);
                    std::swap(e.localPort, e.remotePort);
                    if (e.kind == NetworkEventKind::Opened) e.kind = NetworkEventKind::Accepted;
                }
                // The chain of this socket: the event's own (same), or the
                // socket the event now describes after the swap above.
                const bool describesThisSocket = same || !e.process;
                if (!e.process) e.process = c.process;
                if (!e.chainDecoded && describesThisSocket) TakeChain(e, c);
                found = true;
                break;
            }
            // A listener on the tuple's destination port: the connection
            // came to us, and the listener's owner is the best process we have.
            if (c.IsListening() && c.localPort == e.remotePort && !listener) listener = &c;
        }
    }
    if (!e.process && listener) {
        std::swap(e.localAddress, e.remoteAddress);
        std::swap(e.localPort, e.remotePort);
        if (e.kind == NetworkEventKind::Opened) e.kind = NetworkEventKind::Accepted;
        e.process = listener->process;
    }
    if (e.process || e.chainDecoded) Remember(a, EventKey(e), e);
}

// ===== THE REGISTRY =====

struct RegisteredEventSource {
    std::unique_ptr<IConnectionEventSource> source;
    std::atomic<int64_t> events{0};
};

struct EventRegistry {
    std::mutex mutex;
    std::vector<std::shared_ptr<RegisteredEventSource>> sources;
};

EventRegistry& TheRegistry() {
    static EventRegistry registry;
    return registry;
}

struct EventListeners {
    std::mutex mutex;
    std::map<EventListenerId, std::function<void(const NetworkConnectionEvent&)>> entries;
    EventListenerId next = 1;
};

// Never destroyed: the event-source worker threads can still be running at exit, and a lazy
// static first built on it would be torn down under it.
EventListeners& TheListeners() {
    static EventListeners* listeners = new EventListeners;
    return *listeners;
}

struct RecentRing {
    std::mutex mutex;
    std::deque<NetworkConnectionEvent> events;   // newest at the back
};

// Never destroyed: the event-source worker threads can still be running at exit, and a lazy
// static first built on it would be torn down under it.
RecentRing& TheRing() {
    static RecentRing* ring = new RecentRing;
    return *ring;
}

// ===== THE SNAPSHOT DIFFER =====

class SnapshotDiffSource : public IConnectionEventSource {
public:
    explicit SnapshotDiffSource(const SnapshotDiffOptions& options) : options_(options) {
        if (options_.intervalMs < 50) options_.intervalMs = 50;
    }
    ~SnapshotDiffSource() override { Stop(); }

    std::string Name() const override { return "snapshot differ (" + std::to_string(options_.intervalMs) + " ms)"; }
    // What the backend can put in a snapshot, read once at Start: these are
    // called by the registry under its own lock, and the capabilities
    // consult that lock, so they must not go back there.
    bool ReportsProcess() const override { return reportsProcess_; }
    bool ReportsBytes() const override { return reportsBytes_; }
    bool IsRunning() const override { return running_.load(); }
    std::string LastError() const override {
        std::lock_guard<std::mutex> lock(errorMutex_);
        return lastError_;
    }

    NetworkMonitorResult Start(std::function<void(const NetworkConnectionEvent&)> onEvent) override {
        if (running_.load()) return NetworkMonitorResult::Ok();
        if (!NetworkMonitor_IsAvailable()) {
            return NetworkMonitorResult::Error(NetworkMonitorResultCode::NotSupported,
                                               "No socket table on this platform to diff.");
        }
        const NetworkMonitorCapabilities caps = NetworkMonitor_GetCapabilities();
        reportsProcess_ = caps.processAttribution;
        reportsBytes_ = caps.perConnectionBytes;
        onEvent_ = std::move(onEvent);
        stop_ = false;
        running_ = true;
        worker_ = std::thread([this] { Run(); });
        return NetworkMonitorResult::Ok();
    }

    void Stop() override {
        if (!running_.load()) return;
        stop_ = true;
        if (worker_.joinable()) worker_.join();
        running_ = false;
    }

private:
    void Run() {
        std::map<std::string, NetworkConnection> previous;
        std::map<uint16_t, bool> listeners;   // TCP ports listened on, last snapshot
        bool seeded = false;
        while (!stop_.load()) {
            const auto started = Clock::now();
            std::vector<NetworkConnection> table;
            NetworkMonitorOptions options;
            options.resolveNames = false;
            options.includeListening = true;
            const NetworkMonitorResult read = NetworkMonitor_ListConnections(table, options);
            if (!read) {
                std::lock_guard<std::mutex> lock(errorMutex_);
                lastError_ = read.message;
            } else {
                std::map<std::string, NetworkConnection> current;
                std::map<uint16_t, bool> nowListening;
                for (auto& c : table) {
                    const bool unbound = c.IsListening() || c.state == NetworkConnectionState::Unconnected;
                    if (c.IsListening() && c.transport == NetworkTransport::Tcp) nowListening[c.localPort] = true;
                    if (unbound && !options_.includeListening) continue;
                    if (!options_.includeLoopback && c.IsLoopback()) continue;
                    std::string key = FlowKey(c.transport, c.family, c.localAddress, c.localPort,
                                              c.remoteAddress, c.remotePort);
                    // A connection closing loses its chain before it goes: the
                    // peer's socket lingers ownerless (TIME_WAIT), so the
                    // decoder pairs it with no process. Keep what an earlier
                    // read told, so the Closed names the peer too.
                    if (auto earlier = previous.find(key); earlier != previous.end()) {
                        const NetworkConnection& was = earlier->second;
                        if (!c.localPeer && was.localPeer) {
                            c.localPeer = was.localPeer;
                            if (c.loopbackRole == LoopbackRole::None) c.loopbackRole = was.loopbackRole;
                        }
                        if (c.forProcesses.empty() && !was.forProcesses.empty()) c.forProcesses = was.forProcesses;
                    }
                    current.emplace(std::move(key), std::move(c));
                }
                if (seeded) {
                    const int64_t now = NetworkMonitor_NowMs();
                    for (const auto& [key, c] : current) {
                        if (previous.count(key)) continue;
                        NetworkConnectionEvent e = From(c, now);
                        e.kind = (!c.IsListening() && c.transport == NetworkTransport::Tcp &&
                                  listeners.count(c.localPort))
                                     ? NetworkEventKind::Accepted : NetworkEventKind::Opened;
                        if (onEvent_) onEvent_(e);
                    }
                    for (const auto& [key, c] : previous) {
                        if (current.count(key)) continue;
                        NetworkConnectionEvent e = From(c, now);
                        e.kind = NetworkEventKind::Closed;
                        e.bytesSent = c.bytesSent;
                        e.bytesReceived = c.bytesReceived;
                        if (onEvent_) onEvent_(e);
                    }
                }
                previous = std::move(current);
                listeners = std::move(nowListening);
                seeded = true;
            }
            while (!stop_.load() && Clock::now() - started < std::chrono::milliseconds(options_.intervalMs)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
            }
        }
    }

    NetworkConnectionEvent From(const NetworkConnection& c, int64_t now) const {
        NetworkConnectionEvent e;
        e.transport = c.transport;
        e.family = c.family;
        e.localAddress = c.localAddress;
        e.localPort = c.localPort;
        e.remoteAddress = c.remoteAddress;
        e.remotePort = c.remotePort;
        e.process = c.process;
        e.observedAtMs = now;
        e.sourceName = Name();
        TakeChain(e, c);   // the table ListConnections gave is decoded
        return e;
    }

    SnapshotDiffOptions options_;
    bool reportsProcess_ = false;
    bool reportsBytes_ = false;
    std::function<void(const NetworkConnectionEvent&)> onEvent_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_{false};
    mutable std::mutex errorMutex_;
    std::string lastError_;
};

} // namespace

// ===== PUBLIC =====

std::unique_ptr<IConnectionEventSource> NetworkMonitor_CreateSnapshotDiffEventSource(const SnapshotDiffOptions& options) {
    return std::make_unique<SnapshotDiffSource>(options);
}

void NetworkMonitor_ReportEvent(const NetworkConnectionEvent& event) {
    NetworkConnectionEvent e = event;
    if (e.observedAtMs == 0) e.observedAtMs = NetworkMonitor_NowMs();
    Attribute(e);
    if (e.remoteName.empty()) {
        NameRecord record;
        if (NetworkMonitor_LookupName(e.remoteAddress, record)) {
            e.remoteName = record.name;
            e.nameSource = record.source;
        } else if (!e.IsLoopback()) {
            NetworkMonitor_NoteUnnamedAddress(e.remoteAddress);
        }
    }
    {
        RecentRing& ring = TheRing();
        std::lock_guard<std::mutex> lock(ring.mutex);
        ring.events.push_back(e);
        while (ring.events.size() > kRecentEventsCapacity) ring.events.pop_front();
    }
    std::vector<std::function<void(const NetworkConnectionEvent&)>> listeners;
    {
        std::lock_guard<std::mutex> lock(TheListeners().mutex);
        for (const auto& [id, fn] : TheListeners().entries) listeners.push_back(fn);
    }
    for (const auto& fn : listeners) fn(e);
}

NetworkMonitorResult NetworkMonitor_RegisterEventSource(std::unique_ptr<IConnectionEventSource> source) {
    if (!source) {
        return NetworkMonitorResult::Error(NetworkMonitorResultCode::InvalidArgument, "No source given.");
    }
    auto entry = std::make_shared<RegisteredEventSource>();
    entry->source = std::move(source);
    std::weak_ptr<RegisteredEventSource> weak = entry;
    const NetworkMonitorResult started = entry->source->Start([weak](const NetworkConnectionEvent& event) {
        if (auto strong = weak.lock()) ++strong->events;
        NetworkMonitor_ReportEvent(event);
    });
    if (!started) return started;
    std::lock_guard<std::mutex> lock(TheRegistry().mutex);
    TheRegistry().sources.push_back(std::move(entry));
    return NetworkMonitorResult::Ok();
}

void NetworkMonitor_ListEventSources(std::vector<EventSourceStatus>& out) {
    out.clear();
    // The sources are asked outside the registry lock: a source may read
    // the capabilities, and those ask the registry.
    std::vector<std::shared_ptr<RegisteredEventSource>> sources;
    {
        std::lock_guard<std::mutex> lock(TheRegistry().mutex);
        sources = TheRegistry().sources;
    }
    for (const auto& entry : sources) {
        EventSourceStatus status;
        status.name = entry->source->Name();
        status.running = entry->source->IsRunning();
        status.reportsProcess = entry->source->ReportsProcess();
        status.reportsBytes = entry->source->ReportsBytes();
        status.lastError = entry->source->LastError();
        status.events = entry->events.load();
        out.push_back(std::move(status));
    }
}

void NetworkMonitor_StopEventSources() {
    std::vector<std::shared_ptr<RegisteredEventSource>> sources;
    {
        std::lock_guard<std::mutex> lock(TheRegistry().mutex);
        sources.swap(TheRegistry().sources);
    }
    for (auto& entry : sources) entry->source->Stop();
    sources.clear();
}

bool NetworkMonitor_AnyEventSourceRunning() {
    std::vector<std::shared_ptr<RegisteredEventSource>> sources;
    {
        std::lock_guard<std::mutex> lock(TheRegistry().mutex);
        sources = TheRegistry().sources;
    }
    for (const auto& entry : sources) {
        if (entry->source->IsRunning()) return true;
    }
    return false;
}

EventListenerId NetworkMonitor_AddEventListener(std::function<void(const NetworkConnectionEvent&)> listener) {
    if (!listener) return 0;
    std::lock_guard<std::mutex> lock(TheListeners().mutex);
    const EventListenerId id = TheListeners().next++;
    TheListeners().entries.emplace(id, std::move(listener));
    return id;
}

void NetworkMonitor_RemoveEventListener(EventListenerId id) {
    std::lock_guard<std::mutex> lock(TheListeners().mutex);
    TheListeners().entries.erase(id);
}

void NetworkMonitor_RecentEvents(std::vector<NetworkConnectionEvent>& out, std::size_t max) {
    out.clear();
    RecentRing& ring = TheRing();
    std::lock_guard<std::mutex> lock(ring.mutex);
    const std::size_t count = std::min(max, ring.events.size());
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) out.push_back(ring.events[ring.events.size() - 1 - i]);
}

void NetworkMonitor_ClearRecentEvents() {
    RecentRing& ring = TheRing();
    std::lock_guard<std::mutex> lock(ring.mutex);
    ring.events.clear();
}

} // namespace UltraCanvas
