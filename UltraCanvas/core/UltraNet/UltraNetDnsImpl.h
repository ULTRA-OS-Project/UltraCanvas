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
//   A     "93.184.216.34"                 (the platform backends answer these
//   AAAA  "2606:2800:220:1:248:1893:25c8:1946"  too, for a lookup that names
//                                          its servers and cannot use getaddrinfo)
//
// `servers` is the per-call list from UltraNetDnsOptions, already validated by
// UltraNet_DnsParseServer: empty means the backend's own configuration. The
// libresolv and dnsapi backends take IPv4 servers on port 53 and report
// Unsupported for anything else; c-ares takes any entry.
// Version: 0.4.0 - per-call servers, timeouts bound the platform backends
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
                           int timeoutMs,
                           const std::vector<std::string>& servers);

} // namespace ultranet_dns_platform
