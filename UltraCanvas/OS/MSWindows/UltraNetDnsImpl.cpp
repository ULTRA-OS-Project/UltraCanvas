// OS/MSWindows/UltraNetDnsImpl.cpp
// DnsQuery_A-backed DNS-record queries for non-A/AAAA types on Windows.
// Linked against dnsapi (via CMake).
// Version: 0.4.0 - per-call servers (IP4_ARRAY), A / AAAA, ERROR_TIMEOUT maps to Timeout
// Last Modified: 2026-09-24
// Author: UltraCanvas Framework / ULTRA OS

#include "../../core/UltraNet/UltraNetDnsImpl.h"

#if defined(_WIN32) || defined(_WIN64)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <windns.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "dnsapi.lib")
#pragma comment(lib, "ws2_32.lib")

namespace ultranet_dns_platform {
namespace {

WORD ToDnsType(UltraNetDnsType t) {
    switch (t) {
        case UltraNetDnsType::A:     return DNS_TYPE_A;
        case UltraNetDnsType::AAAA:  return DNS_TYPE_AAAA;
        case UltraNetDnsType::MX:    return DNS_TYPE_MX;
        case UltraNetDnsType::TXT:   return DNS_TYPE_TEXT;
        case UltraNetDnsType::SRV:   return DNS_TYPE_SRV;
        case UltraNetDnsType::NS:    return DNS_TYPE_NS;
        case UltraNetDnsType::CNAME: return DNS_TYPE_CNAME;
        case UltraNetDnsType::SOA:   return DNS_TYPE_SOA;
        case UltraNetDnsType::PTR:   return DNS_TYPE_PTR;
    }
    return DNS_TYPE_A;
}

// DnsQuery_A returns ANSI records (char* fields). Under -DUNICODE the DNS_RECORD
// macro resolves to DNS_RECORDW (wchar_t* fields), so we operate on the explicit
// ANSI record type to match what the _A query actually hands back.
bool FormatRecord(const DNS_RECORDA* r, UltraNetDnsType type, std::string& out) {
    if (!r) return false;
    switch (type) {
        case UltraNetDnsType::A: {
            if (r->wType != DNS_TYPE_A) return false;
            in_addr a{};
            a.s_addr = r->Data.A.IpAddress;
            char text[INET_ADDRSTRLEN]{};
            if (!inet_ntop(AF_INET, &a, text, sizeof text)) return false;
            out = text;
            return true;
        }
        case UltraNetDnsType::AAAA: {
            if (r->wType != DNS_TYPE_AAAA) return false;
            char text[INET6_ADDRSTRLEN]{};
            if (!inet_ntop(AF_INET6, &r->Data.AAAA.Ip6Address, text, sizeof text)) return false;
            out = text;
            return true;
        }
        case UltraNetDnsType::MX: {
            if (r->wType != DNS_TYPE_MX) return false;
            std::ostringstream os;
            os << r->Data.MX.wPreference << ' ' << r->Data.MX.pNameExchange;
            out = os.str();
            return true;
        }
        case UltraNetDnsType::TXT: {
            if (r->wType != DNS_TYPE_TEXT) return false;
            std::string acc;
            for (DWORD i = 0; i < r->Data.TXT.dwStringCount; ++i) {
                if (r->Data.TXT.pStringArray[i]) {
                    acc += r->Data.TXT.pStringArray[i];
                }
            }
            out = std::move(acc);
            return true;
        }
        case UltraNetDnsType::SRV: {
            if (r->wType != DNS_TYPE_SRV) return false;
            std::ostringstream os;
            os << r->Data.SRV.wPriority << ' '
               << r->Data.SRV.wWeight   << ' '
               << r->Data.SRV.wPort     << ' '
               << r->Data.SRV.pNameTarget;
            out = os.str();
            return true;
        }
        case UltraNetDnsType::NS:
            if (r->wType != DNS_TYPE_NS) return false;
            out = r->Data.NS.pNameHost ? r->Data.NS.pNameHost : "";
            return !out.empty();
        case UltraNetDnsType::CNAME:
            if (r->wType != DNS_TYPE_CNAME) return false;
            out = r->Data.CNAME.pNameHost ? r->Data.CNAME.pNameHost : "";
            return !out.empty();
        case UltraNetDnsType::PTR:
            if (r->wType != DNS_TYPE_PTR) return false;
            out = r->Data.PTR.pNameHost ? r->Data.PTR.pNameHost : "";
            return !out.empty();
        case UltraNetDnsType::SOA: {
            if (r->wType != DNS_TYPE_SOA) return false;
            std::ostringstream os;
            os << (r->Data.SOA.pNamePrimaryServer
                       ? r->Data.SOA.pNamePrimaryServer : "")
               << ' '
               << (r->Data.SOA.pNameAdministrator
                       ? r->Data.SOA.pNameAdministrator : "")
               << ' '
               << r->Data.SOA.dwSerialNo  << ' '
               << r->Data.SOA.dwRefresh   << ' '
               << r->Data.SOA.dwRetry     << ' '
               << r->Data.SOA.dwExpire    << ' '
               << r->Data.SOA.dwDefaultTtl;
            out = os.str();
            return true;
        }
        default:
            return false;
    }
}

} // namespace

// The per-call server list as DnsQuery takes it: an IP4_ARRAY of IPv4
// servers on port 53 (the API has no port and no IPv6 form; c-ares takes
// both). The array is variable-length, so it lives in `storage`.
UltraNetResult BuildServerArray(const std::vector<std::string>& servers,
                                std::vector<unsigned char>& storage,
                                PIP4_ARRAY& outArray) {
    outArray = nullptr;
    if (servers.empty()) return UltraNetResult::Ok();
    storage.assign(sizeof(IP4_ARRAY) + servers.size() * sizeof(IP4_ADDRESS), 0);
    auto* arr = reinterpret_cast<PIP4_ARRAY>(storage.data());
    arr->AddrCount = 0;
    for (const std::string& spec : servers) {
        std::string address; int port = 0;
        if (!UltraNet_DnsParseServer(spec, address, port)) {
            return UltraNetResult::Error(UltraNetResultCode::InvalidUrl,
                                         "not a name server address: " + spec);
        }
        in_addr a{};
        if (inet_pton(AF_INET, address.c_str(), &a) != 1) {
            return UltraNetResult::Error(UltraNetResultCode::Unsupported,
                "the dnsapi backend takes IPv4 name servers only: " + spec);
        }
        if (port != 0 && port != 53) {
            return UltraNetResult::Error(UltraNetResultCode::Unsupported,
                "the dnsapi backend asks name servers on port 53 only: " + spec);
        }
        arr->AddrArray[arr->AddrCount++] = a.s_addr;
    }
    outArray = arr;
    return UltraNetResult::Ok();
}

UltraNetResult Resolve(const std::string& hostname,
                       UltraNetDnsType type,
                       std::vector<std::string>& outRecords,
                       int /*timeoutMs*/,   // DnsQuery has no per-call deadline
                       const std::vector<std::string>& servers) {
    outRecords.clear();
    std::vector<unsigned char> serverStorage;
    PIP4_ARRAY serverArray = nullptr;
    if (UltraNetResult r = BuildServerArray(servers, serverStorage, serverArray); !r) {
        return r;
    }
    // A named server is asked directly: no resolver cache, no hosts file.
    const DWORD queryOptions = serverArray
        ? (DNS_QUERY_STANDARD | DNS_QUERY_BYPASS_CACHE | DNS_QUERY_NO_HOSTS_FILE)
        : DNS_QUERY_STANDARD;
    PDNS_RECORD records = nullptr;
    DNS_STATUS s = DnsQuery_A(hostname.c_str(),
                              ToDnsType(type),
                              queryOptions,
                              serverArray, &records, nullptr);
    if (s != 0 || !records) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "DnsQuery_A failed (status %lu)",
                      static_cast<unsigned long>(s));
        const UltraNetResultCode code =
            (s == ERROR_TIMEOUT) ? UltraNetResultCode::Timeout
                                 : UltraNetResultCode::HostNotFound;
        return UltraNetResult::Error(code, buf);
    }
    for (PDNS_RECORD r = records; r; r = r->pNext) {
        std::string formatted;
        if (FormatRecord(reinterpret_cast<const DNS_RECORDA*>(r), type, formatted)) {
            outRecords.push_back(std::move(formatted));
        }
    }
    DnsRecordListFree(records, DnsFreeRecordList);
    if (outRecords.empty()) {
        return UltraNetResult::Error(UltraNetResultCode::HostNotFound,
                                     "no records of requested type");
    }
    return UltraNetResult::Ok();
}

} // namespace ultranet_dns_platform

#endif // _WIN32 || _WIN64
