// OS/MSWindows/UltraNetDnsImpl.cpp
// DnsQuery_A-backed DNS-record queries for non-A/AAAA types on Windows.
// Linked against dnsapi (via CMake).
// Version: 0.3.2 (UNICODE/ANSI DNS record fix)
// Last Modified: 2026-07-05
// Author: UltraCanvas Framework / ULTRA OS

#include "../../core/UltraNet/UltraNetDnsImpl.h"

#if defined(_WIN32) || defined(_WIN64)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windns.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "dnsapi.lib")

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

UltraNetResult Resolve(const std::string& hostname,
                       UltraNetDnsType type,
                       std::vector<std::string>& outRecords,
                       int /*timeoutMs*/) {
    outRecords.clear();
    PDNS_RECORD records = nullptr;
    DNS_STATUS s = DnsQuery_A(hostname.c_str(),
                              ToDnsType(type),
                              DNS_QUERY_STANDARD,
                              nullptr, &records, nullptr);
    if (s != 0 || !records) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "DnsQuery_A failed (status %lu)",
                      static_cast<unsigned long>(s));
        return UltraNetResult::Error(UltraNetResultCode::HostNotFound, buf);
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
