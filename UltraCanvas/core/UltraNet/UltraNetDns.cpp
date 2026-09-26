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
// Version: 0.3.0 - per-call name servers (UltraNetDnsOptions)
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraNet/UltraNetDns.h"
#include "UltraNetDnsImpl.h"

#include <chrono>
#include <cstring>
#include <functional>
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

    // ---- A lookup with servers of its own ---------------------------------
    // Bypasses the cache and the getaddrinfo / getnameinfo paths, which have
    // no way to name a server: everything goes to the platform backend, PTR
    // as a query for the reverse name.
    if (!options.servers.empty()) {
        if (UltraNetResult v = ValidateServers(options.servers); !v) return v;
        std::string name = hostname;
        if (type == UltraNetDnsType::PTR && !UltraNet_DnsReverseName(hostname, name)) {
            return UltraNetResult::Error(UltraNetResultCode::InvalidUrl,
                                         "not a valid IPv4/IPv6 address");
        }
        return ultranet_dns_platform::Resolve(name, type, outAddresses,
                                              timeoutMs, options.servers);
    }
    // Never destroyed: detached async lookups can still run at exit.
    static const std::vector<std::string>& kNoServers = *new std::vector<std::string>;

#ifdef ULTRANET_HAS_CARES
    // c-ares handles every record type uniformly (including PTR — but the
    // caller-facing reverse-lookup API takes an IP, so PTR still flows
    // through UltraNet_DnsReverseLookup for that ergonomics). Route every
    // forward query through the c-ares Resolve() implementation; the
    // per-platform libresolv / dnsapi path becomes unused.
    if (type != UltraNetDnsType::PTR) {
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

    if (type == UltraNetDnsType::PTR) {
        std::string host;
        UltraNetResult r = UltraNet_DnsReverseLookup(hostname, host, timeoutMs);
        if (r) outAddresses.push_back(host);
        return r;
    }
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
#ifdef ULTRANET_HAS_CARES
    // c-ares gives us real non-blocking async — no thread-per-call.
    // PTR is the one exception: we go through the reverse-lookup path
    // (still on a detached thread) since the c-ares ParsePtr helper
    // needs the queried IP as well.
    if (type != UltraNetDnsType::PTR) {
        return ultranet_dns_platform::ResolveAsyncCares(
            hostname, type, std::move(onResult), options.servers);
    }
#endif
    std::thread([hostname, type, options, cb = std::move(onResult)]() {
        std::vector<std::string> addrs;
        UltraNet_DnsResolve(hostname, addrs, type, options);
        cb(addrs);
    }).detach();
    return UltraNetResult::Ok();
}

UltraNetResult UltraNet_DnsReverseLookup(const std::string& ipAddress,
                                         std::string& outHostname,
                                         int /*timeoutMs*/) {
    // getnameinfo has no deadline and no server of its own; a caller that
    // needs either asks for a PTR through UltraNet_DnsResolve with options.
    outHostname.clear();
    if (ipAddress.empty()) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidUrl,
                                     "ipAddress is empty");
    }

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

    char host[NI_MAXHOST]{};
    int rc = ::getnameinfo(sa, sal, host, sizeof host, nullptr, 0, NI_NAMEREQD);
    if (rc != 0) {
        return UltraNetResult::Error(UltraNetResultCode::HostNotFound,
                                     gai_strerror(rc));
    }
    outHostname.assign(host);
    return UltraNetResult::Ok();
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
