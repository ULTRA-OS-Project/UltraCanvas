// core/UltraNet/UltraNetDns.cpp
// The DNS entry points: routing between the c-ares backend (every record
// type, real async) and the system resolver fallback (getaddrinfo /
// getnameinfo for A / AAAA / PTR, the platform DNS library for the rest),
// the process-wide cache, and the per-call options. A lookup that names its
// own servers (UltraNetDnsOptions::servers) goes to the platform backend on
// every build - c-ares on its own channel for that list, libresolv / dnsapi
// with the resolver state pointed at the list - and never touches the cache.
//
// Async path without c-ares: a detached thread runs the sync resolver. Fine
// for typical app workloads; the curl_multi worker is reserved for HTTP.
// Version: 0.3.3 - a worker pool for threaded lookups; UltraNet_DnsReverseNameToAddress
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraNet/UltraNetDns.h"
#include "UltraNetDnsImpl.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef ULTRANET_HAS_CARES
namespace ultranet_dns_platform {
    // Defined in UltraNetDnsCares.cpp. Not part of UltraNetDnsImpl.h because
    // only the c-ares backend supports them — the per-platform libresolv /
    // dnsapi backends do not have a real async path.
    UltraNetResult ResolveAsyncCares(
        const std::string& hostname,
        UltraNetDnsType type,
        std::function<void(const std::vector<std::string>&)> onResult,
        const std::vector<std::string>& servers);
    void SetCustomServersCares(const std::vector<std::string>& servers);
}
#endif

#if defined(_WIN32) || defined(_WIN64)
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  using socklen_t = int;
#else
  #include <netdb.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <arpa/inet.h>
#endif

namespace {

#if defined(_WIN32) || defined(_WIN64)
struct WsaInit {
    WsaInit() { WSADATA d{}; WSAStartup(MAKEWORD(2, 2), &d); }
    ~WsaInit() { WSACleanup(); }
};
WsaInit g_wsaInit;
#endif

// Best-effort process-wide DNS cache. libcurl keeps its own per-handle cache;
// this one serves explicit UltraNet_DnsResolve calls so repeated lookups in
// the same app don't always hit the resolver.
struct CacheEntry {
    std::vector<std::string> addresses;
    std::chrono::steady_clock::time_point expiresAt;
};
constexpr std::chrono::seconds kCacheTtl{60};

std::mutex g_cacheMutex;
std::unordered_map<std::string, CacheEntry> g_cache;

std::mutex g_serversMutex;
std::vector<std::string> g_customServers;     // honored once c-ares lands

std::string IpV4ToString(const sockaddr_in* sin) {
    char buf[INET_ADDRSTRLEN]{};
    inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof buf);
    return buf;
}
std::string IpV6ToString(const sockaddr_in6* sin6) {
    char buf[INET6_ADDRSTRLEN]{};
    inet_ntop(AF_INET6, &sin6->sin6_addr, buf, sizeof buf);
    return buf;
}

bool LookupCache(const std::string& key, std::vector<std::string>& out) {
    std::lock_guard<std::mutex> lk(g_cacheMutex);
    auto it = g_cache.find(key);
    if (it == g_cache.end()) return false;
    if (std::chrono::steady_clock::now() > it->second.expiresAt) {
        g_cache.erase(it);
        return false;
    }
    out = it->second.addresses;
    return true;
}

void StoreCache(const std::string& key, const std::vector<std::string>& addrs) {
    std::lock_guard<std::mutex> lk(g_cacheMutex);
    g_cache[key] = {addrs, std::chrono::steady_clock::now() + kCacheTtl};
}

// Every entry of a per-call server list must parse; the backends rely on it.
UltraNetResult ValidateServers(const std::vector<std::string>& servers) {
    for (const std::string& spec : servers) {
        std::string address; int port = 0;
        if (!UltraNet_DnsParseServer(spec, address, port)) {
            return UltraNetResult::Error(UltraNetResultCode::InvalidUrl,
                                         "not a name server address: " + spec);
        }
    }
    return UltraNetResult::Ok();
}

const char* DnsTypeName(UltraNetDnsType t) {
    switch (t) {
        case UltraNetDnsType::A:     return "A";
        case UltraNetDnsType::AAAA:  return "AAAA";
        case UltraNetDnsType::MX:    return "MX";
        case UltraNetDnsType::TXT:   return "TXT";
        case UltraNetDnsType::SRV:   return "SRV";
        case UltraNetDnsType::PTR:   return "PTR";
        case UltraNetDnsType::NS:    return "NS";
        case UltraNetDnsType::CNAME: return "CNAME";
        case UltraNetDnsType::SOA:   return "SOA";
    }
    return "?";
}

} // namespace

UltraNetResult UltraNet_DnsResolve(const std::string& hostname,
                                   std::vector<std::string>& outAddresses,
                                   UltraNetDnsType type,
                                   int timeoutMs) {
    UltraNetDnsOptions options;
    options.timeoutMs = timeoutMs;
    return UltraNet_DnsResolve(hostname, outAddresses, type, options);
}

UltraNetResult UltraNet_DnsResolve(const std::string& hostname,
                                   std::vector<std::string>& outAddresses,
                                   UltraNetDnsType type,
                                   const UltraNetDnsOptions& options) {
    outAddresses.clear();
    if (hostname.empty()) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidUrl,
                                     "hostname is empty");
    }
    const int timeoutMs = options.timeoutMs > 0 ? options.timeoutMs : 5000;

    // PTR takes an address, and the reverse lookup owns that path - the
    // deadline, the servers, the hosts file - so it is one call whichever
    // backend and whatever the options.
    if (type == UltraNetDnsType::PTR) {
        std::string host;
        UltraNetResult r = UltraNet_DnsReverseLookup(hostname, host, options);
        if (r) outAddresses.push_back(host);
        return r;
    }

    // ---- A lookup with servers of its own ---------------------------------
    // Bypasses the cache and the getaddrinfo path, which has no way to name a
    // server: everything goes to the platform backend.
    if (!options.servers.empty()) {
        if (UltraNetResult v = ValidateServers(options.servers); !v) return v;
        return ultranet_dns_platform::Resolve(hostname, type, outAddresses,
                                              timeoutMs, options.servers);
    }
    // Never destroyed: detached async lookups can still run at exit.
    static const std::vector<std::string>& kNoServers = *new std::vector<std::string>;

#ifdef ULTRANET_HAS_CARES
    // c-ares handles every forward record type uniformly (PTR went to the
    // reverse lookup above, whose getnameinfo path also reads the hosts
    // file). Route every forward query through the c-ares Resolve()
    // implementation; the per-platform libresolv / dnsapi path is unused.
    {
        const std::string cacheKey =
            std::string{DnsTypeName(type)} + "|" + hostname;
        if (LookupCache(cacheKey, outAddresses)) {
            return UltraNetResult::Ok();
        }
        UltraNetResult r = ultranet_dns_platform::Resolve(
            hostname, type, outAddresses, timeoutMs, kNoServers);
        if (r) StoreCache(cacheKey, outAddresses);
        return r;
    }
#endif

    if (type != UltraNetDnsType::A && type != UltraNetDnsType::AAAA) {
        const std::string cacheKey =
            std::string{DnsTypeName(type)} + "|" + hostname;
        if (LookupCache(cacheKey, outAddresses)) {
            return UltraNetResult::Ok();
        }
        // MX/TXT/SRV/NS/CNAME/SOA: hand off to the platform DNS backend
        // (libresolv on Linux/macOS, dnsapi.dll on Windows).
        UltraNetResult r = ultranet_dns_platform::Resolve(
            hostname, type, outAddresses, timeoutMs, kNoServers);
        if (r) StoreCache(cacheKey, outAddresses);
        return r;
    }

    const std::string cacheKey =
        std::string{DnsTypeName(type)} + "|" + hostname;
    if (LookupCache(cacheKey, outAddresses)) {
        return UltraNetResult::Ok();
    }

    addrinfo hints{};
    hints.ai_family   = (type == UltraNetDnsType::AAAA) ? AF_INET6 : AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* res = nullptr;
    int rc = ::getaddrinfo(hostname.c_str(), nullptr, &hints, &res);
    if (rc != 0 || !res) {
        if (res) ::freeaddrinfo(res);
        return UltraNetResult::Error(UltraNetResultCode::HostNotFound,
                                     gai_strerror(rc));
    }
    for (addrinfo* p = res; p; p = p->ai_next) {
        if (p->ai_family == AF_INET && type == UltraNetDnsType::A) {
            outAddresses.push_back(IpV4ToString(
                reinterpret_cast<sockaddr_in*>(p->ai_addr)));
        } else if (p->ai_family == AF_INET6 && type == UltraNetDnsType::AAAA) {
            outAddresses.push_back(IpV6ToString(
                reinterpret_cast<sockaddr_in6*>(p->ai_addr)));
        }
    }
    ::freeaddrinfo(res);

    if (outAddresses.empty()) {
        return UltraNetResult::Error(UltraNetResultCode::HostNotFound,
                                     "no records of requested type");
    }
    StoreCache(cacheKey, outAddresses);
    return UltraNetResult::Ok();
}

namespace {

// The threads behind the asynchronous lookups that need one: PTR on every
// backend, every type on the system backends. A small fixed pool, grown to
// its limit on demand and never shrunk, so a burst of calls queues instead
// of starting a thread each. Workers are detached and the pool is never
// destroyed: a lookup may still be running at exit, and a worker blocked in
// the resolver cannot be joined.
class DnsWorkerPool {
public:
    DnsWorkerPool()
        : limit_(std::clamp(std::thread::hardware_concurrency(), 2u, 8u)) {}

    void Post(std::function<void()> job) {
        {
            std::lock_guard<std::mutex> lk(mu_);
            jobs_.push_back(std::move(job));
            if (idle_ == 0 && threads_ < limit_) {
                ++threads_;
                std::thread([this] { Run(); }).detach();
            }
        }
        cv_.notify_one();
    }

private:
    void Run() {
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lk(mu_);
                ++idle_;
                cv_.wait(lk, [&] { return !jobs_.empty(); });
                --idle_;
                job = std::move(jobs_.front());
                jobs_.pop_front();
            }
            job();
        }
    }

    std::mutex                        mu_;
    std::condition_variable           cv_;
    std::deque<std::function<void()>> jobs_;
    unsigned                          threads_ = 0;
    unsigned                          idle_    = 0;
    const unsigned                    limit_;
};

DnsWorkerPool& DnsWorkers() {
    static DnsWorkerPool& pool = *new DnsWorkerPool;   // never destroyed
    return pool;
}

// Queues `lookup` with the caller's options on the pool and delivers its
// answer to `onResult`. The deadline counts from now, not from when a worker
// picks the job up: the budget a job spent queued is taken off, and one that
// spent all of it is answered empty at once, without a query.
void PostDnsLookup(UltraNetDnsOptions options,
                   std::function<std::vector<std::string>(const UltraNetDnsOptions&)> lookup,
                   std::function<void(const std::vector<std::string>&)> onResult) {
    const int timeoutMs = options.timeoutMs > 0 ? options.timeoutMs : 5000;
    const auto deadline = std::chrono::steady_clock::now()
                        + std::chrono::milliseconds(timeoutMs);
    DnsWorkers().Post([options, deadline, lookup = std::move(lookup),
                       cb = std::move(onResult)]() mutable {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        if (remaining <= 0) {
            cb({});
            return;
        }
        options.timeoutMs = static_cast<int>(remaining);
        cb(lookup(options));
    });
}

} // namespace

UltraNetResult UltraNet_DnsResolveAsync(
    const std::string& hostname,
    UltraNetDnsType type,
    std::function<void(const std::vector<std::string>&)> onResult) {
    return UltraNet_DnsResolveAsync(hostname, type, std::move(onResult),
                                    UltraNetDnsOptions{});
}

UltraNetResult UltraNet_DnsResolveAsync(
    const std::string& hostname,
    UltraNetDnsType type,
    std::function<void(const std::vector<std::string>&)> onResult,
    const UltraNetDnsOptions& options) {
    if (!onResult) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidState,
                                     "onResult callback is required");
    }
    if (UltraNetResult v = ValidateServers(options.servers); !v) return v;

    // PTR takes an address, and the reverse lookup owns that path - the
    // deadline, the servers, the hosts file - so it is that call on the
    // worker pool, whichever backend. The address is checked here, before
    // anything is queued, the way the servers are: a non-address is refused
    // now, not reported as an empty answer later.
    if (type == UltraNetDnsType::PTR) {
        if (hostname.empty()) {
            return UltraNetResult::Error(UltraNetResultCode::InvalidUrl,
                                         "ipAddress is empty");
        }
        std::string reverseName;
        if (!UltraNet_DnsReverseName(hostname, reverseName)) {
            return UltraNetResult::Error(UltraNetResultCode::InvalidUrl,
                                         "not a valid IPv4/IPv6 address");
        }
        PostDnsLookup(options,
            [hostname](const UltraNetDnsOptions& o) {
                std::string host;
                std::vector<std::string> names;
                if (UltraNet_DnsReverseLookup(hostname, host, o)) names.push_back(host);
                return names;
            },
            std::move(onResult));
        return UltraNetResult::Ok();
    }

#ifdef ULTRANET_HAS_CARES
    // c-ares gives us real non-blocking async for every forward type — no
    // thread at all on this side.
    return ultranet_dns_platform::ResolveAsyncCares(
        hostname, type, std::move(onResult), options.servers);
#else
    PostDnsLookup(options,
        [hostname, type](const UltraNetDnsOptions& o) {
            std::vector<std::string> addrs;
            UltraNet_DnsResolve(hostname, addrs, type, o);
            return addrs;
        },
        std::move(onResult));
    return UltraNetResult::Ok();
#endif
}

UltraNetResult UltraNet_DnsReverseLookup(const std::string& ipAddress,
                                         std::string& outHostname,
                                         int timeoutMs) {
    UltraNetDnsOptions options;
    options.timeoutMs = timeoutMs;
    return UltraNet_DnsReverseLookup(ipAddress, outHostname, options);
}

namespace {

// getnameinfo has no deadline of its own, so it runs on a thread that owns
// its state; the caller waits up to the deadline and then walks away. The
// thread finishes on its own and frees the state - it never writes into the
// caller's frame, which may be long gone by then.
struct ReverseLookupState {
    std::mutex              mu;
    std::condition_variable cv;
    bool                    done = false;
    int                     rc   = 0;
    std::string             host;
};

UltraNetResult ReverseLookupSystem(const sockaddr* sa, socklen_t sal, int timeoutMs,
                                   std::string& outHostname) {
    auto state = std::make_shared<ReverseLookupState>();
    // A copy of the address for the thread: the caller's sockaddr is a local.
    std::vector<unsigned char> address(reinterpret_cast<const unsigned char*>(sa),
                                       reinterpret_cast<const unsigned char*>(sa) + sal);
    std::thread([state, address, sal]() {
        char host[NI_MAXHOST]{};
        const int rc = ::getnameinfo(reinterpret_cast<const sockaddr*>(address.data()), sal,
                                     host, sizeof host, nullptr, 0, NI_NAMEREQD);
        std::lock_guard<std::mutex> lk(state->mu);
        state->rc   = rc;
        state->host = host;
        state->done = true;
        state->cv.notify_all();
    }).detach();

    std::unique_lock<std::mutex> lk(state->mu);
    if (!state->cv.wait_for(lk, std::chrono::milliseconds(timeoutMs),
                            [&] { return state->done; })) {
        return UltraNetResult::Error(UltraNetResultCode::Timeout,
                                     "reverse lookup timed out");
    }
    if (state->rc != 0) {
        return UltraNetResult::Error(UltraNetResultCode::HostNotFound,
                                     gai_strerror(state->rc));
    }
    outHostname = state->host;
    return UltraNetResult::Ok();
}

} // namespace

UltraNetResult UltraNet_DnsReverseLookup(const std::string& ipAddress,
                                         std::string& outHostname,
                                         const UltraNetDnsOptions& options) {
    outHostname.clear();
    if (ipAddress.empty()) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidUrl,
                                     "ipAddress is empty");
    }
    const int timeoutMs = options.timeoutMs > 0 ? options.timeoutMs : 5000;

    sockaddr_in  v4{};
    sockaddr_in6 v6{};
    sockaddr*    sa  = nullptr;
    socklen_t    sal = 0;

    if (inet_pton(AF_INET, ipAddress.c_str(), &v4.sin_addr) == 1) {
        v4.sin_family = AF_INET;
        sa  = reinterpret_cast<sockaddr*>(&v4);
        sal = sizeof v4;
    } else if (inet_pton(AF_INET6, ipAddress.c_str(), &v6.sin6_addr) == 1) {
        v6.sin6_family = AF_INET6;
        sa  = reinterpret_cast<sockaddr*>(&v6);
        sal = sizeof v6;
    } else {
        return UltraNetResult::Error(UltraNetResultCode::InvalidUrl,
                                     "not a valid IPv4/IPv6 address");
    }

    // Servers of its own: a PTR query for the reverse name at those servers,
    // through the platform backend, which honours the deadline itself.
    if (!options.servers.empty()) {
        if (UltraNetResult v = ValidateServers(options.servers); !v) return v;
        std::string reverseName;
        UltraNet_DnsReverseName(ipAddress, reverseName);   // an address: cannot fail here
        std::vector<std::string> names;
        UltraNetResult r = ultranet_dns_platform::Resolve(
            reverseName, UltraNetDnsType::PTR, names, timeoutMs, options.servers);
        if (r && names.empty()) {
            return UltraNetResult::Error(UltraNetResultCode::HostNotFound,
                                         "no PTR record for " + reverseName);
        }
        if (r) outHostname = names.front();
        return r;
    }

    // The system's resolver, which also answers from the hosts file, under
    // the deadline.
    return ReverseLookupSystem(sa, sal, timeoutMs, outHostname);
}

bool UltraNet_DnsParseServer(const std::string& spec,
                             std::string& outAddress, int& outPort) {
    outAddress.clear();
    outPort = 0;
    std::string address, portText;
    if (!spec.empty() && spec.front() == '[') {
        // "[v6]" or "[v6]:port"
        const std::size_t close = spec.find(']');
        if (close == std::string::npos) return false;
        address = spec.substr(1, close - 1);
        if (close + 1 < spec.size()) {
            if (spec[close + 1] != ':') return false;
            portText = spec.substr(close + 2);
            if (portText.empty()) return false;   // "[v6]:" names no port
        }
    } else if (spec.find(':') != std::string::npos
               && spec.find(':') == spec.rfind(':')) {
        // exactly one colon: "v4:port" (a bare v6 has at least two)
        address  = spec.substr(0, spec.find(':'));
        portText = spec.substr(spec.find(':') + 1);
        if (portText.empty()) return false;   // "9.9.9.9:" names no port
    } else {
        address = spec;   // bare v4 or bare v6
    }
    if (address.empty()) return false;
    in_addr  v4{};
    in6_addr v6{};
    if (inet_pton(AF_INET, address.c_str(), &v4) != 1
        && inet_pton(AF_INET6, address.c_str(), &v6) != 1) {
        return false;
    }
    int port = 0;
    if (!portText.empty()) {
        if (portText.size() > 5) return false;
        for (char c : portText) {
            if (c < '0' || c > '9') return false;
            port = port * 10 + (c - '0');
        }
        if (port < 1 || port > 65535) return false;
    }
    outAddress = address;
    outPort    = port;
    return true;
}

bool UltraNet_DnsReverseName(const std::string& ipAddress, std::string& outName) {
    outName.clear();
    in_addr v4{};
    if (inet_pton(AF_INET, ipAddress.c_str(), &v4) == 1) {
        const unsigned char* b = reinterpret_cast<const unsigned char*>(&v4);
        outName = std::to_string(b[3]) + '.' + std::to_string(b[2]) + '.'
                + std::to_string(b[1]) + '.' + std::to_string(b[0]) + ".in-addr.arpa";
        return true;
    }
    in6_addr v6{};
    if (inet_pton(AF_INET6, ipAddress.c_str(), &v6) == 1) {
        static const char kHex[] = "0123456789abcdef";
        const unsigned char* b = reinterpret_cast<const unsigned char*>(&v6);
        std::string name;
        for (int i = 15; i >= 0; --i) {
            name += kHex[b[i] & 0x0f]; name += '.';
            name += kHex[b[i] >> 4];   name += '.';
        }
        outName = name + "ip6.arpa";
        return true;
    }
    return false;
}

bool UltraNet_DnsReverseNameToAddress(const std::string& name, std::string& outAddress) {
    outAddress.clear();
    std::string n = name;
    if (!n.empty() && n.back() == '.') n.pop_back();
    for (char& c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    // The labels in front of the suffix, in the order written (least
    // significant first).
    auto labelsBefore = [&](const char* suffix, std::vector<std::string>& labels) {
        const std::size_t sl = std::strlen(suffix);
        if (n.size() <= sl || n.compare(n.size() - sl, sl, suffix) != 0) return false;
        std::string head = n.substr(0, n.size() - sl);   // "d.c.b.a" - no trailing dot
        std::size_t start = 0;
        for (;;) {
            const std::size_t dot = head.find('.', start);
            const std::string label = head.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
            if (label.empty()) return false;
            labels.push_back(label);
            if (dot == std::string::npos) break;
            start = dot + 1;
        }
        return true;
    };

    std::vector<std::string> labels;
    if (labelsBefore(".in-addr.arpa", labels)) {
        if (labels.size() != 4) return false;
        std::string text;
        for (std::size_t k = labels.size(); k-- > 0;) {
            const std::string& l = labels[k];
            if (l.size() > 3) return false;
            int v = 0;
            for (char c : l) {
                if (c < '0' || c > '9') return false;
                v = v * 10 + (c - '0');
            }
            if (v > 255) return false;
            if (!text.empty()) text += '.';
            text += std::to_string(v);
        }
        in_addr v4{};
        if (inet_pton(AF_INET, text.c_str(), &v4) != 1) return false;
        outAddress = text;
        return true;
    }
    labels.clear();
    if (labelsBefore(".ip6.arpa", labels)) {
        if (labels.size() != 32) return false;
        unsigned char bytes[16]{};
        for (std::size_t k = 0; k < 32; ++k) {
            const std::string& l = labels[31 - k];   // most significant nibble first
            if (l.size() != 1) return false;
            const char c = l[0];
            int v;
            if      (c >= '0' && c <= '9') v = c - '0';
            else if (c >= 'a' && c <= 'f') v = 10 + (c - 'a');
            else return false;
            bytes[k / 2] = static_cast<unsigned char>((bytes[k / 2] << 4) | v);
        }
        char text[INET6_ADDRSTRLEN]{};
        if (!inet_ntop(AF_INET6, bytes, text, sizeof text)) return false;
        outAddress = text;
        return true;
    }
    return false;
}

void UltraNet_DnsClearCache() {
    std::lock_guard<std::mutex> lk(g_cacheMutex);
    g_cache.clear();
}

void UltraNet_DnsSetServers(const std::vector<std::string>& servers) {
    {
        std::lock_guard<std::mutex> lk(g_serversMutex);
        g_customServers = servers;
    }
#ifdef ULTRANET_HAS_CARES
    // c-ares accepts a comma-separated server list (with ports) at any time;
    // the next query on the default channel uses them. Without c-ares this
    // remains a no-op against the system resolver - a lookup that must reach
    // a given server names it in UltraNetDnsOptions::servers instead.
    ultranet_dns_platform::SetCustomServersCares(servers);
#endif
}
