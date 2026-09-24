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
//
// A lookup that names its own servers (UltraNetDnsOptions::servers) runs on
// a channel of its own, one per distinct list, kept for the life of the
// process like the default one: a channel is never destroyed while a query
// may still be in flight on it, and a list that is used again reuses it.
// Version: 0.2.0 - one channel per per-call server list; ports honoured
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
#include <map>
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
// Channels (ARES_OPT_EVENT_THREAD). The default one serves every lookup
// that names no servers; a lookup with servers of its own gets the channel
// for that exact list, created on first use and kept.
// =====================================================================
std::string JoinServers(const std::vector<std::string>& servers) {
    std::string csv;
    for (std::size_t i = 0; i < servers.size(); ++i) {
        if (i) csv += ',';
        csv += servers[i];
    }
    return csv;
}

struct Channel {
    ares_channel_t* handle = nullptr;
    bool valid = false;
    std::string failure;   // why `valid` is false, for the error message

    // `serversCsv` empty = the system configuration; otherwise the list the
    // channel asks, "ip[:port]" entries joined with commas, as
    // ares_set_servers_ports_csv reads them.
    explicit Channel(const std::string& serversCsv = std::string()) {
        if (ares_library_init(ARES_LIB_INIT_ALL) != ARES_SUCCESS) {
            failure = "c-ares library init failed";
            return;
        }
        ares_options opts{};
        int mask = ARES_OPT_EVENT_THREAD;
        opts.evsys = ARES_EVSYS_DEFAULT;
        if (ares_init_options(&handle, &opts, mask) != ARES_SUCCESS) {
            failure = "c-ares channel not initialised";
            return;
        }
        if (!serversCsv.empty()) {
            const int rc = ares_set_servers_ports_csv(handle, serversCsv.c_str());
            if (rc != ARES_SUCCESS) {
                failure = std::string("c-ares rejected the server list: ")
                        + ares_strerror(rc);
                return;
            }
        }
        valid = true;
    }
    ~Channel() {
        if (handle) ares_destroy(handle);
        ares_library_cleanup();
    }
    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;
};

Channel& Chan() {
    static Channel c;
    return c;
}

// The channel for a per-call server list. Channels are leaked on purpose:
// destroying one while a query may still be in flight on it (an abandoned
// timeout, an async caller) is exactly the hazard the default channel
// avoids by living for the process, and a list that comes back reuses its
// channel instead of paying ares_init_options and a new event thread.
Channel& ChanFor(const std::vector<std::string>& servers) {
    if (servers.empty()) return Chan();
    static std::mutex mu;
    static std::map<std::string, std::unique_ptr<Channel>> channels;
    const std::string key = JoinServers(servers);
    std::lock_guard<std::mutex> lk(mu);
    auto it = channels.find(key);
    if (it == channels.end()) {
        it = channels.emplace(key, std::make_unique<Channel>(key)).first;
    }
    return *it->second;
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

// A query outlives the call that issued it: c-ares answers on its own worker
// thread, and there is no way to withdraw one query from a shared channel
// (ares_cancel takes down every query in flight, including other threads').
// So the state is shared — the caller drops its reference when it stops
// waiting, the callback drops the query's, and whichever comes last frees it.
using PendingPtr = std::shared_ptr<Pending>;

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
    // Takes back the reference Issue() handed to c-ares; it is released when
    // this function returns, however it returns.
    const std::unique_ptr<PendingPtr> owner(static_cast<PendingPtr*>(arg));
    Pending* p = owner->get();
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
    if (cb) cb(results);
}

void OnQueryCallback(void* arg, int status, int /*timeouts*/,
                     unsigned char* abuf, int alen) {
    const std::unique_ptr<PendingPtr> owner(static_cast<PendingPtr*>(arg));
    Pending* p = owner->get();
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
    if (cb) cb(results);
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
        case ARES_EREFUSED:       return UltraNetResultCode::ConnectionRefused;  // the server said REFUSED
        case ARES_ECONNREFUSED:   return UltraNetResultCode::ConnectionRefused;  // no server could be contacted
        case ARES_ESERVFAIL:      return UltraNetResultCode::Unknown;
        default:                  return UltraNetResultCode::Unknown;
    }
}

// =====================================================================
// Issues an A/AAAA gethostbyname; other types go through raw ares_query.
// The query is given a reference of its own to `p`, which its callback
// releases — so the state stays alive even if the caller has already given
// up waiting for it.
// =====================================================================
void Issue(Channel& ch, const PendingPtr& p, const std::string& host,
           UltraNetDnsType type) {
    p->type = type;
    auto* owner = new PendingPtr(p);
    if (type == UltraNetDnsType::A || type == UltraNetDnsType::AAAA) {
        const int family = (type == UltraNetDnsType::AAAA) ? AF_INET6 : AF_INET;
        ares_gethostbyname(ch.handle, host.c_str(), family,
                           &OnHostCallback, owner);
    } else {
        ares_query(ch.handle, host.c_str(), ns_c_in,
                   ToAresType(type), &OnQueryCallback, owner);
    }
}

} // namespace

// =====================================================================
// Public surface (per UltraNetDnsImpl.h interface)
// =====================================================================
UltraNetResult Resolve(const std::string& hostname,
                       UltraNetDnsType type,
                       std::vector<std::string>& outRecords,
                       int timeoutMs,
                       const std::vector<std::string>& servers) {
    outRecords.clear();
    Channel& ch = ChanFor(servers);
    if (!ch.valid) {
        return UltraNetResult::Error(UltraNetResultCode::Unknown, ch.failure);
    }

    auto p = std::make_shared<Pending>();
    Issue(ch, p, hostname, type);

    std::unique_lock<std::mutex> lk(p->mu);
    const auto deadline = std::chrono::milliseconds(
        timeoutMs > 0 ? timeoutMs : 5000);
    if (!p->cv.wait_for(lk, deadline, [&] { return p->done; })) {
        // The query is abandoned, not cancelled: ares_cancel would take down
        // every other query in flight on the shared channel, and the answer
        // is no longer wanted anyway. The callback still fires on c-ares's
        // worker and writes into the state, which is why the state is shared
        // — this frame's reference goes away here, the query's when it
        // answers.
        return UltraNetResult::Error(UltraNetResultCode::Timeout,
                                     "DNS query timed out");
    }
    if (p->status != ARES_SUCCESS) {
        return UltraNetResult::Error(MapAresStatus(p->status),
                                     ares_strerror(p->status));
    }
    if (p->results.empty()) {
        return UltraNetResult::Error(UltraNetResultCode::HostNotFound,
                                     "no records of requested type");
    }
    outRecords = std::move(p->results);
    return UltraNetResult::Ok();
}

// Optional second entry point that c-ares makes very cheap: real async.
// The platform interface only requires Resolve(); apps that want
// non-blocking DNS go through UltraNet_DnsResolveAsync, which UltraNetDns
// will route here when c-ares is enabled.
UltraNetResult ResolveAsyncCares(
    const std::string& hostname,
    UltraNetDnsType type,
    std::function<void(const std::vector<std::string>&)> onResult,
    const std::vector<std::string>& servers) {
    if (!onResult) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidState,
                                     "onResult callback is required");
    }
    Channel& ch = ChanFor(servers);
    if (!ch.valid) {
        return UltraNetResult::Error(UltraNetResultCode::Unknown, ch.failure);
    }
    auto p = std::make_shared<Pending>();
    p->asyncCb = std::move(onResult);
    Issue(ch, p, hostname, type);
    return UltraNetResult::Ok();
}

void SetCustomServersCares(const std::vector<std::string>& servers) {
    Channel& ch = Chan();
    if (!ch.valid) return;
    if (servers.empty()) return;
    // The ports variant: "ip:port" and "[v6]:port" entries keep their port
    // (ares_set_servers_csv would silently drop it).
    ares_set_servers_ports_csv(ch.handle, JoinServers(servers).c_str());
}

} // namespace ultranet_dns_platform

#endif // ULTRANET_HAS_CARES
