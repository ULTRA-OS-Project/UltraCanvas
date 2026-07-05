// core/UltraNet/UltraNetDnsCares.cpp
// c-ares-backed DNS resolution: fully async, cross-platform, supports
// every record type (A / AAAA / MX / TXT / SRV / NS / CNAME / SOA / PTR)
// uniformly. Replaces the system getaddrinfo + libresolv path when c-ares
// is available at build time (ULTRANET_HAS_CARES).
//
// Architecture: one process-wide c-ares channel built with
// ARES_OPT_EVENT_THREAD so c-ares manages its own internal worker
// thread; ares_query / ares_gethostbyname are callable from any thread,
// callbacks fire on c-ares's worker. Sync entry points block the caller
// on a condition variable; async entry points return immediately.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#ifdef ULTRANET_HAS_CARES

#include "UltraNet/UltraNetDns.h"
#include "UltraNetDnsImpl.h"

#include <ares.h>
#include <ares_nameser.h>   // ns_t_a / ns_t_aaaa / ns_c_in / etc.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32) || defined(_WIN64)
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  #include <arpa/inet.h>
  #include <netdb.h>          // struct hostent
  #include <netinet/in.h>
  #include <sys/socket.h>
#endif

// `Bool` / `Status` come in from X11 headers via UltraCanvas transitively;
// they would collide with c-ares enums on some configurations, but our
// UltraNetCore.h already #undef's them at include time.

namespace ultranet_dns_platform {
namespace {

// =====================================================================
// Channel singleton (ARES_OPT_EVENT_THREAD)
// =====================================================================
struct Channel {
    ares_channel_t* handle = nullptr;
    bool valid = false;

    Channel() {
        if (ares_library_init(ARES_LIB_INIT_ALL) != ARES_SUCCESS) return;
        ares_options opts{};
        int mask = ARES_OPT_EVENT_THREAD;
        opts.evsys = ARES_EVSYS_DEFAULT;
        if (ares_init_options(&handle, &opts, mask) == ARES_SUCCESS) {
            valid = true;
        }
    }
    ~Channel() {
        if (handle) ares_destroy(handle);
        ares_library_cleanup();
    }
};

Channel& Chan() {
    static Channel c;
    return c;
}

// =====================================================================
// One pending query — sync waits on `done`, async invokes `notify`.
// =====================================================================
struct Pending {
    std::mutex                mu;
    std::condition_variable   cv;
    bool                      done   = false;
    int                       status = ARES_ENOTFOUND;
    std::vector<std::string>  results;

    // Async only: type we requested (so the raw-packet callback can
    // dispatch parsing) and the user's completion callback.
    UltraNetDnsType type = UltraNetDnsType::A;
    std::function<void(const std::vector<std::string>&)> asyncCb;
};

// =====================================================================
// Response parsers — c-ares ships per-record-type helpers that walk the
// reply packet. We format each record into a single string for the public
// UltraNet API.
// =====================================================================
void FormatA(const struct hostent* he, std::vector<std::string>& out) {
    if (!he || !he->h_addr_list) return;
    for (char** a = he->h_addr_list; *a; ++a) {
        char buf[INET6_ADDRSTRLEN]{};
        ::inet_ntop(he->h_addrtype, *a, buf, sizeof buf);
        out.emplace_back(buf);
    }
}

void ParseMx(const unsigned char* buf, int len, std::vector<std::string>& out) {
    struct ares_mx_reply* mx = nullptr;
    if (ares_parse_mx_reply(buf, len, &mx) != ARES_SUCCESS) return;
    for (auto* cur = mx; cur; cur = cur->next) {
        std::ostringstream os;
        os << cur->priority << ' ' << (cur->host ? cur->host : "");
        out.push_back(os.str());
    }
    ares_free_data(mx);
}

void ParseTxt(const unsigned char* buf, int len, std::vector<std::string>& out) {
    struct ares_txt_reply* txt = nullptr;
    if (ares_parse_txt_reply(buf, len, &txt) != ARES_SUCCESS) return;
    for (auto* cur = txt; cur; cur = cur->next) {
        std::string acc(reinterpret_cast<const char*>(cur->txt), cur->length);
        out.push_back(std::move(acc));
    }
    ares_free_data(txt);
}

void ParseSrv(const unsigned char* buf, int len, std::vector<std::string>& out) {
    struct ares_srv_reply* srv = nullptr;
    if (ares_parse_srv_reply(buf, len, &srv) != ARES_SUCCESS) return;
    for (auto* cur = srv; cur; cur = cur->next) {
        std::ostringstream os;
        os << cur->priority << ' ' << cur->weight << ' ' << cur->port
           << ' ' << (cur->host ? cur->host : "");
        out.push_back(os.str());
    }
    ares_free_data(srv);
}

void ParseNs(const unsigned char* buf, int len, std::vector<std::string>& out) {
    struct hostent* he = nullptr;
    if (ares_parse_ns_reply(buf, len, &he) != ARES_SUCCESS) return;
    if (he && he->h_aliases) {
        for (char** a = he->h_aliases; *a; ++a) out.emplace_back(*a);
    }
    if (he) ares_free_hostent(he);
}

void ParseCname(const unsigned char* buf, int len, std::vector<std::string>& out) {
    // Reuse the A parser — CNAME comes back through the same path.
    struct hostent* he = nullptr;
    int             ttl = 0;
    struct ares_addrttl addrs[1]{};
    int n = 1;
    (void)addrs; (void)n; (void)ttl;
    // Easier: there's no dedicated ares_parse_cname_reply. The CNAME shows
    // up as he->h_name when we parse_a_reply on the response. Reuse:
    if (ares_parse_a_reply(buf, len, &he, nullptr, nullptr) != ARES_SUCCESS) return;
    if (he && he->h_name) out.emplace_back(he->h_name);
    if (he) ares_free_hostent(he);
}

void ParsePtr(const unsigned char* buf, int len, std::vector<std::string>& out) {
    struct hostent* he = nullptr;
    // PTR parse needs the queried IP, but we just want the result name.
    // ares_parse_ptr_reply takes the wire IP bytes which we don't have at
    // this layer; pass nullptr/0 — c-ares tolerates that and still fills
    // h_name with the resolved hostname.
    if (ares_parse_ptr_reply(buf, len, nullptr, 0, AF_INET, &he) != ARES_SUCCESS) return;
    if (he && he->h_name) out.emplace_back(he->h_name);
    if (he) ares_free_hostent(he);
}

void ParseSoa(const unsigned char* buf, int len, std::vector<std::string>& out) {
    struct ares_soa_reply* soa = nullptr;
    if (ares_parse_soa_reply(buf, len, &soa) != ARES_SUCCESS) return;
    if (soa) {
        std::ostringstream os;
        os << (soa->nsname ? soa->nsname : "") << ' '
           << (soa->hostmaster ? soa->hostmaster : "") << ' '
           << soa->serial  << ' ' << soa->refresh << ' '
           << soa->retry   << ' ' << soa->expire  << ' '
           << soa->minttl;
        out.push_back(os.str());
        ares_free_data(soa);
    }
}

// =====================================================================
// Callbacks (run on c-ares's event-thread)
// =====================================================================
void OnHostCallback(void* arg, int status, int /*timeouts*/,
                    struct hostent* he) {
    auto* p = static_cast<Pending*>(arg);
    std::vector<std::string> results;
    if (status == ARES_SUCCESS && he) FormatA(he, results);

    decltype(p->asyncCb) cb;
    {
        std::lock_guard<std::mutex> lk(p->mu);
        p->status  = status;
        p->results = results;
        p->done    = true;
        cb         = std::move(p->asyncCb);
    }
    p->cv.notify_all();
    if (cb) {
        cb(results);
        delete p;             // async path — we own the Pending
    }
}

void OnQueryCallback(void* arg, int status, int /*timeouts*/,
                     unsigned char* abuf, int alen) {
    auto* p = static_cast<Pending*>(arg);
    std::vector<std::string> results;
    if (status == ARES_SUCCESS && abuf && alen > 0) {
        switch (p->type) {
            case UltraNetDnsType::MX:    ParseMx(abuf, alen, results);    break;
            case UltraNetDnsType::TXT:   ParseTxt(abuf, alen, results);   break;
            case UltraNetDnsType::SRV:   ParseSrv(abuf, alen, results);   break;
            case UltraNetDnsType::NS:    ParseNs(abuf, alen, results);    break;
            case UltraNetDnsType::CNAME: ParseCname(abuf, alen, results); break;
            case UltraNetDnsType::PTR:   ParsePtr(abuf, alen, results);   break;
            case UltraNetDnsType::SOA:   ParseSoa(abuf, alen, results);   break;
            default: break;
        }
    }
    decltype(p->asyncCb) cb;
    {
        std::lock_guard<std::mutex> lk(p->mu);
        p->status  = status;
        p->results = results;
        p->done    = true;
        cb         = std::move(p->asyncCb);
    }
    p->cv.notify_all();
    if (cb) {
        cb(results);
        delete p;
    }
}

int ToAresType(UltraNetDnsType t) {
    switch (t) {
        case UltraNetDnsType::A:     return ns_t_a;
        case UltraNetDnsType::AAAA:  return ns_t_aaaa;
        case UltraNetDnsType::MX:    return ns_t_mx;
        case UltraNetDnsType::TXT:   return ns_t_txt;
        case UltraNetDnsType::SRV:   return ns_t_srv;
        case UltraNetDnsType::NS:    return ns_t_ns;
        case UltraNetDnsType::CNAME: return ns_t_cname;
        case UltraNetDnsType::PTR:   return ns_t_ptr;
        case UltraNetDnsType::SOA:   return ns_t_soa;
    }
    return ns_t_a;
}

UltraNetResultCode MapAresStatus(int s) {
    switch (s) {
        case ARES_SUCCESS:        return UltraNetResultCode::Success;
        case ARES_ENOTFOUND:      return UltraNetResultCode::HostNotFound;
        case ARES_ENODATA:        return UltraNetResultCode::HostNotFound;
        case ARES_ETIMEOUT:       return UltraNetResultCode::Timeout;
        case ARES_ENOMEM:         return UltraNetResultCode::InsufficientMemory;
        case ARES_ECANCELLED:     return UltraNetResultCode::Cancelled;
        case ARES_EBADNAME:       return UltraNetResultCode::InvalidUrl;
        case ARES_EREFUSED:       return UltraNetResultCode::ConnectionRefused;
        default:                  return UltraNetResultCode::Unknown;
    }
}

// =====================================================================
// Issues an A/AAAA gethostbyname; other types go through raw ares_query.
// `pending` is the heap-allocated state; on the sync path the caller
// retains ownership and waits, on the async path the callback frees.
// =====================================================================
void Issue(Channel& ch, Pending* p, const std::string& host,
           UltraNetDnsType type) {
    p->type = type;
    if (type == UltraNetDnsType::A || type == UltraNetDnsType::AAAA) {
        const int family = (type == UltraNetDnsType::AAAA) ? AF_INET6 : AF_INET;
        ares_gethostbyname(ch.handle, host.c_str(), family,
                           &OnHostCallback, p);
    } else {
        ares_query(ch.handle, host.c_str(), ns_c_in,
                   ToAresType(type), &OnQueryCallback, p);
    }
}

} // namespace

// =====================================================================
// Public surface (per UltraNetDnsImpl.h interface)
// =====================================================================
UltraNetResult Resolve(const std::string& hostname,
                       UltraNetDnsType type,
                       std::vector<std::string>& outRecords,
                       int timeoutMs) {
    outRecords.clear();
    Channel& ch = Chan();
    if (!ch.valid) {
        return UltraNetResult::Error(UltraNetResultCode::Unknown,
                                     "c-ares channel not initialised");
    }

    Pending p{};
    Issue(ch, &p, hostname, type);

    std::unique_lock<std::mutex> lk(p.mu);
    const auto deadline = std::chrono::milliseconds(
        timeoutMs > 0 ? timeoutMs : 5000);
    if (!p.cv.wait_for(lk, deadline, [&] { return p.done; })) {
        // Best-effort cancel — the channel callback may still fire on a
        // future event-thread tick, but with `cancelled` status (harmless).
        ares_cancel(ch.handle);
        return UltraNetResult::Error(UltraNetResultCode::Timeout,
                                     "DNS query timed out");
    }
    if (p.status != ARES_SUCCESS) {
        return UltraNetResult::Error(MapAresStatus(p.status),
                                     ares_strerror(p.status));
    }
    if (p.results.empty()) {
        return UltraNetResult::Error(UltraNetResultCode::HostNotFound,
                                     "no records of requested type");
    }
    outRecords = std::move(p.results);
    return UltraNetResult::Ok();
}

// Optional second entry point that c-ares makes very cheap: real async.
// The platform interface only requires Resolve(); apps that want
// non-blocking DNS go through UltraNet_DnsResolveAsync, which UltraNetDns
// will route here when c-ares is enabled.
UltraNetResult ResolveAsyncCares(
    const std::string& hostname,
    UltraNetDnsType type,
    std::function<void(const std::vector<std::string>&)> onResult) {
    if (!onResult) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidState,
                                     "onResult callback is required");
    }
    Channel& ch = Chan();
    if (!ch.valid) {
        return UltraNetResult::Error(UltraNetResultCode::Unknown,
                                     "c-ares channel not initialised");
    }
    auto* p = new Pending{};
    p->asyncCb = std::move(onResult);
    Issue(ch, p, hostname, type);
    return UltraNetResult::Ok();
}

void SetCustomServersCares(const std::vector<std::string>& servers) {
    Channel& ch = Chan();
    if (!ch.valid) return;
    if (servers.empty()) return;
    std::string csv;
    for (std::size_t i = 0; i < servers.size(); ++i) {
        if (i) csv += ',';
        csv += servers[i];
    }
    ares_set_servers_csv(ch.handle, csv.c_str());
}

} // namespace ultranet_dns_platform

#endif // ULTRANET_HAS_CARES
