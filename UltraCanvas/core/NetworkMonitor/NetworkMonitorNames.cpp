// core/NetworkMonitor/NetworkMonitorNames.cpp
// The name table, the source registry and its listeners, the reverse DNS
// source, and the system resolver lookup. The local DNS proxy is in
// NetworkMonitorDnsProxy.cpp; the Windows DNS client events under
// OS/MSWindows/, with a null factory here for every other platform.
//
// Locking: the table has one mutex, the registry another, the listeners a
// third; none is held while calling out (a source's Stop, a listener), so
// a source's thread can feed the table while the UI reads it and Stop can
// join that thread without a callback waiting on a lock Stop holds.
//
// Version: 0.4.0
// Last Modified: 2026-09-22
// Author: UltraCanvas Framework / ULTRA OS
#include "NetworkMonitor/NetworkMonitorNames.h"
#include "NetworkMonitor/NetworkMonitorBackend.h"
#include "NetworkMonitor/NetworkMonitorStore.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    #include <iphlpapi.h>
#else
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
#endif

namespace UltraCanvas {

#ifndef _WIN32
// Only Windows has resolver events a process may subscribe to without a
// kernel extension; Linux and macOS use the proxy.
std::unique_ptr<INameSource> NetworkMonitor_CreateSystemDnsSource() {
    return nullptr;
}
#endif

namespace {

int Strength(NameSource source) {
    return NetworkMonitor_NameIsObserved(source) ? 2 : (source == NameSource::None ? 0 : 1);
}

// ===== THE TABLE =====

struct NameTable {
    std::mutex mutex;
    std::unordered_map<std::string, NameRecord> records;
};

// Never destroyed: the name-source worker threads can still be running at exit, and a lazy
// static first built on it would be torn down under it.
NameTable& Table() {
    static NameTable* table = new NameTable;
    return *table;
}

void EvictIfOver(NameTable& table, int64_t now) {
    if (table.records.size() <= kNameTableCapacity) return;
    for (auto it = table.records.begin(); it != table.records.end();) {
        if (it->second.expiresAt <= now) it = table.records.erase(it); else ++it;
    }
    if (table.records.size() <= kNameTableCapacity) return;
    // Still over: drop the oldest tenth.
    std::vector<std::pair<int64_t, std::string>> byAge;
    byAge.reserve(table.records.size());
    for (const auto& [address, record] : table.records) byAge.emplace_back(record.observedAt, address);
    std::sort(byAge.begin(), byAge.end());
    const std::size_t drop = std::max<std::size_t>(1, byAge.size() / 10);
    for (std::size_t i = 0; i < drop; ++i) table.records.erase(byAge[i].second);
}

// ===== LISTENERS =====

struct Listeners {
    std::mutex mutex;
    std::map<NameListenerId, std::function<void(const DnsObservation&)>> entries;
    NameListenerId next = 1;
};

// Never destroyed: the name-source worker threads can still be running at exit, and a lazy
// static first built on it would be torn down under it.
Listeners& TheListeners() {
    static Listeners* listeners = new Listeners;
    return *listeners;
}

// ===== THE REGISTRY =====

struct RegisteredSource {
    std::unique_ptr<INameSource> source;
    std::atomic<int64_t> observations{0};
};

struct Registry {
    std::mutex mutex;
    std::vector<std::shared_ptr<RegisteredSource>> sources;
};

Registry& TheRegistry() {
    static Registry registry;
    return registry;
}

// ===== ADDRESS CLASSES =====

bool IsIPv4Text(const std::string& address, unsigned char out[4]) {
    return ::inet_pton(AF_INET, address.c_str(), out) == 1;
}

bool IsIPv6Text(const std::string& address, unsigned char out[16]) {
    return ::inet_pton(AF_INET6, address.c_str(), out) == 1;
}

// Addresses no PTR lookup can name usefully: wildcard, loopback, link-local,
// multicast; and, unless asked, the private ranges.
bool WorthReverseLookup(const std::string& address, bool includePrivate) {
    unsigned char v4[4];
    if (IsIPv4Text(address, v4)) {
        if (v4[0] == 0 || v4[0] == 127 || (v4[0] == 169 && v4[1] == 254) || v4[0] >= 224) return false;
        if (!includePrivate) {
            if (v4[0] == 10) return false;
            if (v4[0] == 172 && (v4[1] & 0xF0) == 16) return false;
            if (v4[0] == 192 && v4[1] == 168) return false;
            if (v4[0] == 100 && (v4[1] & 0xC0) == 64) return false;   // CGNAT
        }
        return true;
    }
    unsigned char v6[16];
    if (IsIPv6Text(address, v6)) {
        static const unsigned char zero[15] = {};
        if (std::memcmp(v6, zero, 15) == 0 && v6[15] <= 1) return false;   // :: and ::1
        if (v6[0] == 0xFF) return false;                                     // multicast
        if (v6[0] == 0xFE && (v6[1] & 0xC0) == 0x80) return false;           // link-local
        if (!includePrivate && (v6[0] & 0xFE) == 0xFC) return false;         // fc00::/7
        static const unsigned char mappedPrefix[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF};
        if (std::memcmp(v6, mappedPrefix, 12) == 0) {
            const std::string inner = std::to_string(v6[12]) + "." + std::to_string(v6[13]) + "." +
                                      std::to_string(v6[14]) + "." + std::to_string(v6[15]);
            return WorthReverseLookup(inner, includePrivate);
        }
        return true;
    }
    return false;
}

// ===== REVERSE DNS =====

class ReverseDnsSource : public INameSource {
public:
    explicit ReverseDnsSource(const ReverseDnsOptions& options) : options_(options) {}
    ~ReverseDnsSource() override { Stop(); }

    std::string Name() const override { return "reverse DNS"; }
    NameSource  Kind() const override { return NameSource::ReverseDns; }
    bool        IsRunning() const override { return running_.load(); }

    NetworkMonitorResult Start(std::function<void(const DnsObservation&)> onObservation) override {
        if (running_.load()) return NetworkMonitorResult::Ok();
#ifdef _WIN32
        WSADATA data;
        if (::WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            return NetworkMonitorResult::Error(NetworkMonitorResultCode::IoError, "WSAStartup failed.");
        }
#endif
        onObservation_ = std::move(onObservation);
        stop_ = false;
        running_ = true;
        worker_ = std::thread([this] { Run(); });
        return NetworkMonitorResult::Ok();
    }

    void Stop() override {
        if (!running_.load()) return;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        wake_.notify_all();
        if (worker_.joinable()) worker_.join();
        running_ = false;
    }

    void NoteAddress(const std::string& address) override {
        if (!running_.load() || !WorthReverseLookup(address, options_.includePrivateRanges)) return;
        const int64_t now = NetworkMonitor_Now();
        std::lock_guard<std::mutex> lock(mutex_);
        auto tried = tried_.find(address);
        if (tried != tried_.end() && tried->second > now) return;
        if (queued_.count(address)) return;
        if (static_cast<int>(queue_.size()) >= options_.maxQueueLength) return;
        queue_.push_back(address);
        queued_.insert(address);
        wake_.notify_one();
    }

    bool WaitIdle(int timeoutMs) override {
        std::unique_lock<std::mutex> lock(mutex_);
        return idle_.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                              [this] { return queue_.empty() && !busy_; });
    }

private:
    void Run() {
        while (true) {
            std::string address;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock, [this] { return stop_ || !queue_.empty(); });
                if (stop_) return;
                address = queue_.front();
                queue_.pop_front();
                queued_.erase(address);
                busy_ = true;
            }
            std::string name;
            const bool found = Lookup(address, name);
            const int64_t now = NetworkMonitor_Now();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                tried_[address] = now + (found ? kNameMinimumLifetimeSeconds : options_.negativeCacheSeconds);
                if (tried_.size() > 100000) tried_.clear();
            }
            if (found && onObservation_) {
                DnsObservation observation;
                observation.queryName = name;
                observation.addresses.push_back(address);
                observation.source = NameSource::ReverseDns;
                observation.observedAt = now;
                onObservation_(observation);
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                busy_ = false;
            }
            idle_.notify_all();
        }
    }

    static bool Lookup(const std::string& address, std::string& name) {
        sockaddr_storage storage{};
        socklen_t length = 0;
        unsigned char v4[4];
        unsigned char v6[16];
        if (IsIPv4Text(address, v4)) {
            auto* in = reinterpret_cast<sockaddr_in*>(&storage);
            in->sin_family = AF_INET;
            std::memcpy(&in->sin_addr, v4, 4);
            length = sizeof(sockaddr_in);
        } else if (IsIPv6Text(address, v6)) {
            auto* in6 = reinterpret_cast<sockaddr_in6*>(&storage);
            in6->sin6_family = AF_INET6;
            std::memcpy(&in6->sin6_addr, v6, 16);
            length = sizeof(sockaddr_in6);
        } else {
            return false;
        }
        char host[NI_MAXHOST];
        const int rc = ::getnameinfo(reinterpret_cast<sockaddr*>(&storage), length, host, sizeof host,
                                     nullptr, 0, NI_NAMEREQD);
        if (rc != 0 || host[0] == '\0') return false;
        name = host;
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        while (!name.empty() && name.back() == '.') name.pop_back();
        // A "name" that is the address again is what some resolvers return
        // for a missing PTR; it names nothing.
        return !name.empty() && name != address;
    }

    ReverseDnsOptions options_;
    std::function<void(const DnsObservation&)> onObservation_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable idle_;
    bool stop_ = false;
    bool busy_ = false;
    std::deque<std::string> queue_;
    std::set<std::string> queued_;
    std::unordered_map<std::string, int64_t> tried_;   // address -> not before
};

} // namespace

// ===== PUBLIC: SOURCES =====

std::unique_ptr<INameSource> NetworkMonitor_CreateReverseDnsSource(const ReverseDnsOptions& options) {
    return std::make_unique<ReverseDnsSource>(options);
}

std::string NetworkMonitor_SystemResolver() {
#ifdef _WIN32
    ULONG size = 0;
    if (::GetNetworkParams(nullptr, &size) != ERROR_BUFFER_OVERFLOW || size == 0) return std::string();
    std::vector<unsigned char> buffer(size);
    auto* info = reinterpret_cast<FIXED_INFO*>(buffer.data());
    if (::GetNetworkParams(info, &size) != NO_ERROR) return std::string();
    for (const IP_ADDR_STRING* server = &info->DnsServerList; server; server = server->Next) {
        const std::string address = server->IpAddress.String;
        if (!address.empty() && address != "0.0.0.0") return address;
    }
    return std::string();
#else
    std::ifstream file("/etc/resolv.conf");
    std::string line;
    while (std::getline(file, line)) {
        if (line.rfind("nameserver", 0) != 0) continue;
        std::size_t pos = 10;
        while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;
        std::size_t end = pos;
        while (end < line.size() && line[end] != ' ' && line[end] != '\t' && line[end] != '#' &&
               line[end] != '\r' && line[end] != '\n') ++end;
        std::string address = line.substr(pos, end - pos);
        // A scoped IPv6 address ("fe80::1%eth0") is not a peer the proxy
        // can reach without the scope; keep the plain ones.
        if (const std::size_t scope = address.find('%'); scope != std::string::npos) address.erase(scope);
        unsigned char bytes[16];
        if (IsIPv4Text(address, bytes) || IsIPv6Text(address, bytes)) return address;
    }
    return std::string();
#endif
}

// ===== PUBLIC: THE REGISTRY =====

NetworkMonitorResult NetworkMonitor_RegisterNameSource(std::unique_ptr<INameSource> source) {
    if (!source) {
        return NetworkMonitorResult::Error(NetworkMonitorResultCode::InvalidArgument, "No source given.");
    }
    auto entry = std::make_shared<RegisteredSource>();
    entry->source = std::move(source);
    std::weak_ptr<RegisteredSource> weak = entry;
    const NetworkMonitorResult started = entry->source->Start([weak](const DnsObservation& observation) {
        if (auto strong = weak.lock()) ++strong->observations;
        NetworkMonitor_ObserveName(observation);
    });
    if (!started) return started;
    std::lock_guard<std::mutex> lock(TheRegistry().mutex);
    TheRegistry().sources.push_back(std::move(entry));
    return NetworkMonitorResult::Ok();
}

void NetworkMonitor_ListNameSources(std::vector<NameSourceStatus>& out) {
    out.clear();
    // Asked outside the registry lock, so a source's method may reach the
    // capabilities (which ask the registry) without deadlocking.
    std::vector<std::shared_ptr<RegisteredSource>> sources;
    {
        std::lock_guard<std::mutex> lock(TheRegistry().mutex);
        sources = TheRegistry().sources;
    }
    for (const auto& entry : sources) {
        NameSourceStatus status;
        status.name = entry->source->Name();
        status.kind = entry->source->Kind();
        status.running = entry->source->IsRunning();
        status.reportsProcess = entry->source->ReportsProcess();
        status.lastError = entry->source->LastError();
        status.observations = entry->observations.load();
        out.push_back(std::move(status));
    }
}

void NetworkMonitor_StopNameSources() {
    std::vector<std::shared_ptr<RegisteredSource>> sources;
    {
        std::lock_guard<std::mutex> lock(TheRegistry().mutex);
        sources.swap(TheRegistry().sources);
    }
    // Outside the lock: Stop joins the source's thread, which may be in
    // the middle of a callback that counts on the entry.
    for (auto& entry : sources) entry->source->Stop();
    sources.clear();
}

bool NetworkMonitor_WaitForNames(int timeoutMs) {
    std::vector<std::shared_ptr<RegisteredSource>> sources;
    {
        std::lock_guard<std::mutex> lock(TheRegistry().mutex);
        sources = TheRegistry().sources;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    bool idle = true;
    for (auto& entry : sources) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        idle = entry->source->WaitIdle(static_cast<int>(std::max<long long>(0, left))) && idle;
    }
    return idle;
}

// The core asks every registered source, through this, to look at an
// address a snapshot could not name (declared in NetworkMonitorBackend.h).
void NetworkMonitor_NoteUnnamedAddress(const std::string& address) {
    std::vector<std::shared_ptr<RegisteredSource>> sources;
    {
        std::lock_guard<std::mutex> lock(TheRegistry().mutex);
        sources = TheRegistry().sources;
    }
    for (const auto& entry : sources) entry->source->NoteAddress(address);
}

bool NetworkMonitor_AnySourceReportsProcess() {
    std::vector<std::shared_ptr<RegisteredSource>> sources;
    {
        std::lock_guard<std::mutex> lock(TheRegistry().mutex);
        sources = TheRegistry().sources;
    }
    for (const auto& entry : sources) {
        if (entry->source->IsRunning() && entry->source->ReportsProcess()) return true;
    }
    return false;
}

// ===== PUBLIC: LISTENERS =====

NameListenerId NetworkMonitor_AddNameListener(std::function<void(const DnsObservation&)> listener) {
    if (!listener) return 0;
    std::lock_guard<std::mutex> lock(TheListeners().mutex);
    const NameListenerId id = TheListeners().next++;
    TheListeners().entries.emplace(id, std::move(listener));
    return id;
}

void NetworkMonitor_RemoveNameListener(NameListenerId id) {
    std::lock_guard<std::mutex> lock(TheListeners().mutex);
    TheListeners().entries.erase(id);
}

// ===== PUBLIC: THE TABLE =====

void NetworkMonitor_ObserveName(const DnsObservation& observation) {
    if (observation.queryName.empty() || observation.addresses.empty() ||
        observation.source == NameSource::None) {
        return;
    }
    DnsObservation stamped = observation;
    if (stamped.observedAt == 0) stamped.observedAt = NetworkMonitor_Now();
    const int64_t lifetime = std::max<int64_t>(stamped.ttlSeconds, kNameMinimumLifetimeSeconds);
    {
        NameTable& table = Table();
        std::lock_guard<std::mutex> lock(table.mutex);
        for (const auto& address : stamped.addresses) {
            if (address.empty()) continue;
            auto found = table.records.find(address);
            if (found != table.records.end()) {
                const NameRecord& old = found->second;
                const bool expired = old.expiresAt <= stamped.observedAt;
                const int oldStrength = Strength(old.source);
                const int newStrength = Strength(stamped.source);
                const bool replace = expired || newStrength > oldStrength ||
                                     (newStrength == oldStrength && stamped.observedAt >= old.observedAt);
                if (!replace) continue;
            }
            NameRecord record;
            record.address = address;
            record.name = stamped.queryName;
            record.source = stamped.source;
            record.observedAt = stamped.observedAt;
            record.expiresAt = stamped.observedAt + lifetime;
            record.process = stamped.process;
            table.records[address] = std::move(record);
        }
        EvictIfOver(table, stamped.observedAt);
    }
    // Listeners after the table and outside its lock, so a listener that
    // looks a name up sees it.
    std::vector<std::function<void(const DnsObservation&)>> listeners;
    {
        std::lock_guard<std::mutex> lock(TheListeners().mutex);
        for (const auto& [id, fn] : TheListeners().entries) listeners.push_back(fn);
    }
    for (const auto& fn : listeners) fn(stamped);
}

bool NetworkMonitor_LookupName(const std::string& address, NameRecord& out) {
    out = NameRecord();
    NameTable& table = Table();
    std::lock_guard<std::mutex> lock(table.mutex);
    auto found = table.records.find(address);
    if (found == table.records.end()) return false;
    if (found->second.expiresAt <= NetworkMonitor_Now()) {
        table.records.erase(found);
        return false;
    }
    out = found->second;
    return true;
}

void NetworkMonitor_ListNames(std::vector<NameRecord>& out) {
    out.clear();
    const int64_t now = NetworkMonitor_Now();
    NameTable& table = Table();
    std::lock_guard<std::mutex> lock(table.mutex);
    out.reserve(table.records.size());
    for (auto it = table.records.begin(); it != table.records.end();) {
        if (it->second.expiresAt <= now) { it = table.records.erase(it); continue; }
        out.push_back(it->second);
        ++it;
    }
    std::sort(out.begin(), out.end(), [](const NameRecord& a, const NameRecord& b) {
        if (a.observedAt != b.observedAt) return a.observedAt > b.observedAt;
        return a.address < b.address;
    });
}

void NetworkMonitor_ClearNames() {
    NameTable& table = Table();
    std::lock_guard<std::mutex> lock(table.mutex);
    table.records.clear();
}

} // namespace UltraCanvas
