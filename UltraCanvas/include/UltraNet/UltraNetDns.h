// include/UltraNet/UltraNetDns.h
// DNS resolution and reverse lookup. With c-ares (ULTRANET_HAS_CARES) every
// record type resolves through one asynchronous backend; without it A / AAAA
// / PTR use the system resolver (getaddrinfo / getnameinfo) and the other
// record types (MX / TXT / SRV / NS / CNAME / SOA) the platform's DNS library
// (libresolv, dnsapi). A lookup can name the servers it asks, for that call
// only (UltraNetDnsOptions::servers).
// Version: 0.3.0 - per-call name servers (UltraNetDnsOptions)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraNetCore.h"

#include <functional>
#include <string>
#include <vector>

enum class UltraNetDnsType {
    A,
    AAAA,
    MX,
    TXT,
    SRV,
    PTR,
    NS,
    CNAME,
    SOA
};

// Options for one lookup. A default-constructed value is the process default,
// so the overloads below that take one behave exactly like the ones that do
// not.
struct UltraNetDnsOptions {
    // The name servers to ask, for this call only. Each entry is an IP with an
    // optional port: "9.9.9.9", "9.9.9.9:5353", "2620:fe::fe", "[2620:fe::fe]:53".
    // Port 0 or none means 53. Empty = the process list: the system's, or what
    // UltraNet_DnsSetServers installed. A lookup with servers of its own never
    // reads or writes the UltraNet cache - a different server may answer
    // differently - and every backend honours it: c-ares keeps one channel per
    // distinct list; the libresolv and dnsapi fallbacks take IPv4 servers on
    // port 53 (anything else is Unsupported), and there the getaddrinfo path
    // is bypassed so A / AAAA go to the named server too.
    std::vector<std::string> servers;
    // Deadline for the answer. The call returns Timeout when it passes, and
    // the backends bound their own retries by it where they can.
    int timeoutMs = 5000;
};

UltraNetResult UltraNet_DnsResolve(
    const std::string& hostname,
    std::vector<std::string>& outAddresses,
    UltraNetDnsType type = UltraNetDnsType::A,
    int timeoutMs = 5000);

// The same lookup with per-call options (servers, deadline).
UltraNetResult UltraNet_DnsResolve(
    const std::string& hostname,
    std::vector<std::string>& outAddresses,
    UltraNetDnsType type,
    const UltraNetDnsOptions& options);

UltraNetResult UltraNet_DnsResolveAsync(
    const std::string& hostname,
    UltraNetDnsType type,
    std::function<void(const std::vector<std::string>&)> onResult);

// The same, with per-call options. `onResult` receives an empty list on any
// failure, as the overload above does.
UltraNetResult UltraNet_DnsResolveAsync(
    const std::string& hostname,
    UltraNetDnsType type,
    std::function<void(const std::vector<std::string>&)> onResult,
    const UltraNetDnsOptions& options);

// Split a server entry as UltraNetDnsOptions::servers takes it into its
// address and port (0 when none was given). False, with the outputs cleared,
// when the entry is not an IPv4 or IPv6 address with an optional port. Pure.
bool UltraNet_DnsParseServer(const std::string& spec,
                             std::string& outAddress, int& outPort);

// The reverse-lookup name of an IP address: "4.4.8.8.in-addr.arpa" for
// 8.8.4.4, the 32-nibble "...ip6.arpa" form for IPv6. False when `ipAddress`
// is not an address. Pure.
bool UltraNet_DnsReverseName(const std::string& ipAddress, std::string& outName);

UltraNetResult UltraNet_DnsReverseLookup(
    const std::string& ipAddress,
    std::string& outHostname,
    int timeoutMs = 5000);

// In-process DNS cache lives inside UltraNet (libcurl maintains its own
// pool too). These act on the UltraNet-level cache; libcurl's pool stays
// intact across calls until the share / easy handle is destroyed.
void UltraNet_DnsClearCache();

// The process-wide server list, in the same "ip[:port]" form as
// UltraNetDnsOptions::servers. Honoured by the c-ares backend for every later
// lookup that names no servers of its own; stored but ignored by the system
// resolver fallback, which cannot be pointed at a server (use the per-call
// option there, which every backend honours).
void UltraNet_DnsSetServers(const std::vector<std::string>& servers);
