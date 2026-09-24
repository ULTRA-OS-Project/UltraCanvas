// OS/MacOS/UltraNetDnsImpl.mm
// libresolv-backed DNS-record queries on macOS. Same API surface as Linux.
// Version: 0.4.0 - per-call servers, A / AAAA, the deadline bounds the retries
// Author: UltraCanvas Framework / ULTRA OS

#include "../../core/UltraNet/UltraNetDnsImpl.h"

#if defined(__APPLE__)

#include <arpa/inet.h>
#include <arpa/nameser.h>
#include <netdb.h>
#include <netinet/in.h>
#include <resolv.h>

#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace ultranet_dns_platform {
namespace {

int ToNsType(UltraNetDnsType t) {
    switch (t) {
        case UltraNetDnsType::A:     return ns_t_a;
        case UltraNetDnsType::AAAA:  return ns_t_aaaa;
        case UltraNetDnsType::MX:    return ns_t_mx;
        case UltraNetDnsType::TXT:   return ns_t_txt;
        case UltraNetDnsType::SRV:   return ns_t_srv;
        case UltraNetDnsType::NS:    return ns_t_ns;
        case UltraNetDnsType::CNAME: return ns_t_cname;
        case UltraNetDnsType::SOA:   return ns_t_soa;
        case UltraNetDnsType::PTR:   return ns_t_ptr;
    }
    return ns_t_a;
}

bool FormatRr(ns_msg msg, ns_rr rr, UltraNetDnsType type, std::string& out) {
    const unsigned char* rdata = ns_rr_rdata(rr);
    const std::size_t    rdlen = ns_rr_rdlen(rr);
    char name[NS_MAXDNAME];

    auto expand = [&](const unsigned char* p, char* dst) -> int {
        return dn_expand(ns_msg_base(msg), ns_msg_end(msg), p, dst, NS_MAXDNAME);
    };

    switch (type) {
        case UltraNetDnsType::A: {
            if (rdlen != 4) return false;
            char text[INET_ADDRSTRLEN]{};
            if (!inet_ntop(AF_INET, rdata, text, sizeof text)) return false;
            out = text;
            return true;
        }
        case UltraNetDnsType::AAAA: {
            if (rdlen != 16) return false;
            char text[INET6_ADDRSTRLEN]{};
            if (!inet_ntop(AF_INET6, rdata, text, sizeof text)) return false;
            out = text;
            return true;
        }
        case UltraNetDnsType::MX: {
            if (rdlen < 3) return false;
            uint16_t pref = (rdata[0] << 8) | rdata[1];
            if (expand(rdata + 2, name) < 0) return false;
            std::ostringstream os; os << pref << ' ' << name;
            out = os.str();
            return true;
        }
        case UltraNetDnsType::TXT: {
            std::string acc;
            std::size_t i = 0;
            while (i < rdlen) {
                std::size_t l = rdata[i++];
                if (i + l > rdlen) return false;
                acc.append(reinterpret_cast<const char*>(rdata + i), l);
                i += l;
            }
            out = std::move(acc);
            return true;
        }
        case UltraNetDnsType::SRV: {
            if (rdlen < 7) return false;
            uint16_t prio = (rdata[0] << 8) | rdata[1];
            uint16_t wt   = (rdata[2] << 8) | rdata[3];
            uint16_t port = (rdata[4] << 8) | rdata[5];
            if (expand(rdata + 6, name) < 0) return false;
            std::ostringstream os; os << prio << ' ' << wt << ' '
                                      << port << ' ' << name;
            out = os.str();
            return true;
        }
        case UltraNetDnsType::NS:
        case UltraNetDnsType::CNAME:
        case UltraNetDnsType::PTR: {
            if (expand(rdata, name) < 0) return false;
            out = name;
            return true;
        }
        case UltraNetDnsType::SOA: {
            char mname[NS_MAXDNAME]{}, rname[NS_MAXDNAME]{};
            int n  = expand(rdata, mname);            if (n  < 0) return false;
            int n2 = expand(rdata + n, rname);        if (n2 < 0) return false;
            const unsigned char* p = rdata + n + n2;
            if (p + 20 > rdata + rdlen) return false;
            uint32_t serial = (p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3];
            uint32_t refresh= (p[4]<<24)|(p[5]<<16)|(p[6]<<8)|p[7];
            uint32_t retry  = (p[8]<<24)|(p[9]<<16)|(p[10]<<8)|p[11];
            uint32_t expire = (p[12]<<24)|(p[13]<<16)|(p[14]<<8)|p[15];
            uint32_t mn     = (p[16]<<24)|(p[17]<<16)|(p[18]<<8)|p[19];
            std::ostringstream os;
            os << mname << ' ' << rname << ' ' << serial << ' '
               << refresh << ' ' << retry << ' ' << expire << ' ' << mn;
            out = os.str();
            return true;
        }
        default:
            return false;
    }
}

} // namespace

// Point a resolver state at a per-call server list. libresolv's classic
// nsaddr_list carries IPv4 servers on their own port; the extended IPv6 list
// is glibc-private, so IPv6 servers are Unsupported here (c-ares takes them).
// A list longer than MAXNS is cut to its first MAXNS entries.
UltraNetResult ApplyServers(struct __res_state& state,
                            const std::vector<std::string>& servers) {
    int n = 0;
    for (const std::string& spec : servers) {
        if (n >= MAXNS) break;
        std::string address; int port = 0;
        if (!UltraNet_DnsParseServer(spec, address, port)) {
            return UltraNetResult::Error(UltraNetResultCode::InvalidUrl,
                                         "not a name server address: " + spec);
        }
        sockaddr_in sin{};
        sin.sin_family = AF_INET;
        sin.sin_port   = htons(static_cast<uint16_t>(port > 0 ? port : 53));
        if (inet_pton(AF_INET, address.c_str(), &sin.sin_addr) != 1) {
            return UltraNetResult::Error(UltraNetResultCode::Unsupported,
                "the libresolv backend takes IPv4 name servers only: " + spec);
        }
        state.nsaddr_list[n++] = sin;
    }
    state.nscount = n;
    return UltraNetResult::Ok();
}

UltraNetResultCode MapHError(int herr) {
    switch (herr) {
        case HOST_NOT_FOUND: return UltraNetResultCode::HostNotFound;
        case NO_DATA:        return UltraNetResultCode::HostNotFound;
        case TRY_AGAIN:      return UltraNetResultCode::Timeout;   // no answer in time, or SERVFAIL
        default:             return UltraNetResultCode::Unknown;
    }
}

UltraNetResult Resolve(const std::string& hostname,
                       UltraNetDnsType type,
                       std::vector<std::string>& outRecords,
                       int timeoutMs,
                       const std::vector<std::string>& servers) {
    outRecords.clear();
    struct __res_state state{};
    if (res_ninit(&state) != 0) {
        return UltraNetResult::Error(UltraNetResultCode::Unknown,
                                     "res_ninit failed");
    }
    if (!servers.empty()) {
        if (UltraNetResult r = ApplyServers(state, servers); !r) {
            res_nclose(&state);
            return r;
        }
    }
    // Bound the resolver's own retries by the deadline: one round per server
    // of at most the deadline (whole seconds, at least one), instead of the
    // default 5 s x 2 tries x every server.
    if (timeoutMs > 0) {
        state.retrans = (timeoutMs + 999) / 1000;
        state.retry   = 1;
    }
    unsigned char buf[NS_PACKETSZ * 4];
    int n = res_nquery(&state, hostname.c_str(), ns_c_in,
                       ToNsType(type), buf, sizeof buf);
    const int herr = h_errno;
    res_nclose(&state);
    if (n < 0) {
        return UltraNetResult::Error(MapHError(herr), hstrerror(herr));
    }
    ns_msg msg;
    if (ns_initparse(buf, n, &msg) < 0) {
        return UltraNetResult::Error(UltraNetResultCode::Unknown,
                                     "ns_initparse failed");
    }
    const uint16_t count = ns_msg_count(msg, ns_s_an);
    for (uint16_t i = 0; i < count; ++i) {
        ns_rr rr;
        if (ns_parserr(&msg, ns_s_an, i, &rr) < 0) continue;
        if (ns_rr_type(rr) != ToNsType(type)) continue;
        std::string formatted;
        if (FormatRr(msg, rr, type, formatted)) {
            outRecords.push_back(std::move(formatted));
        }
    }
    if (outRecords.empty()) {
        return UltraNetResult::Error(UltraNetResultCode::HostNotFound,
                                     "no records of requested type");
    }
    return UltraNetResult::Ok();
}

} // namespace ultranet_dns_platform

#endif // __APPLE__
