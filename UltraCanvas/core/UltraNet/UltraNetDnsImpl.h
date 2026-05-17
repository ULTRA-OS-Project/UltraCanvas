// core/UltraNet/UltraNetDnsImpl.h
// Per-platform DNS-extra-records backend. Used for MX/TXT/SRV/NS/CNAME/SOA
// (everything beyond what getaddrinfo / getnameinfo natively support).
//
//   Linux   - OS/Linux/UltraNetDnsImpl.cpp     (res_nquery + dn_expand)
//   macOS   - OS/MacOS/UltraNetDnsImpl.mm      (same libresolv API as Linux)
//   Windows - OS/MSWindows/UltraNetDnsImpl.cpp (DnsQuery_A / dnsapi.dll)
//
// `outRecords` receives one string per DNS answer, formatted to make the
// result usable directly. The exact format per record type:
//
//   MX    "10 mail.example.com"          (preference + exchange)
//   TXT   "v=spf1 include:_spf.example.com ~all"
//   SRV   "10 5 5060 sip.example.com"    (priority weight port target)
//   NS    "ns1.example.com"
//   CNAME "canonical.example.com"
//   SOA   "ns1.example.com hostmaster.example.com 2024010101 7200 ..."
// Version: 0.3.1 (Stage 3 hardening)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraNet/UltraNetCore.h"
#include "UltraNet/UltraNetDns.h"

#include <string>
#include <vector>

namespace ultranet_dns_platform {

    UltraNetResult Resolve(const std::string& hostname,
                           UltraNetDnsType type,
                           std::vector<std::string>& outRecords,
                           int timeoutMs);

} // namespace ultranet_dns_platform
