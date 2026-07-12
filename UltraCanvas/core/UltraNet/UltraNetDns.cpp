// core/UltraNet/UltraNetDns.cpp
// System-resolver DNS (getaddrinfo / getnameinfo) for A / AAAA / PTR.
// Other record types (MX / TXT / SRV / NS / CNAME / SOA) return
// UnsupportedScheme until the c-ares backend lands in Stage 3.
//
// Async path: spawn a detached thread that runs the sync resolver. This is
// fine for typical app workloads; the curl_multi worker is reserved for HTTP.
// Version: 0.2.0 (Stage 2)
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
        std::function<void(const std::vector<std::string>&)> onResult);
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
    outAddresses.clear();
    if (hostname.empty()) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidUrl,
                                     "hostname is empty");
    }

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
            hostname, type, outAddresses,
            timeoutMs > 0 ? timeoutMs : 5000);
        if (r) StoreCache(cacheKey, outAddresses);
        return r;
    }
#endif

    if (type == UltraNetDnsType::PTR) {
        std::string host;
        UltraNetResult r = UltraNet_DnsReverseLookup(hostname, host, 5000);
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
        UltraNetResult r =
            ultranet_dns_platform::Resolve(hostname, type, outAddresses, 5000);
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
    if (!onResult) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidState,
                                     "onResult callback is required");
    }
#ifdef ULTRANET_HAS_CARES
    // c-ares gives us real non-blocking async — no thread-per-call.
    // PTR is the one exception: we go through the reverse-lookup path
    // (still on a detached thread) since the c-ares ParsePtr helper
    // needs the queried IP as well.
    if (type != UltraNetDnsType::PTR) {
        return ultranet_dns_platform::ResolveAsyncCares(
            hostname, type, std::move(onResult));
    }
#endif
    std::thread([hostname, type, cb = std::move(onResult)]() {
        std::vector<std::string> addrs;
        UltraNet_DnsResolve(hostname, addrs, type, 5000);
        cb(addrs);
    }).detach();
    return UltraNetResult::Ok();
}

UltraNetResult UltraNet_DnsReverseLookup(const std::string& ipAddress,
                                         std::string& outHostname,
                                         int /*timeoutMs*/) {
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
    // c-ares accepts a comma-separated server list at any time; the next
    // ares_query will use them. Without c-ares this remains a no-op
    // against the system resolver.
    ultranet_dns_platform::SetCustomServersCares(servers);
#endif
}
