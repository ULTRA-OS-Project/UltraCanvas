// include/UltraNet/UltraNetDns.h
// DNS resolution and reverse lookup. Stage 2 uses the system resolver
// (getaddrinfo / getnameinfo) for A / AAAA / PTR record types; the
// remaining record types (MX / TXT / SRV / NS / CNAME / SOA) need a real
// DNS library and arrive with the c-ares backend in Stage 3.
// Version: 0.2.0 (Stage 2)
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

UltraNetResult UltraNet_DnsResolve(
    const std::string& hostname,
    std::vector<std::string>& outAddresses,
    UltraNetDnsType type = UltraNetDnsType::A,
    int timeoutMs = 5000);

UltraNetResult UltraNet_DnsResolveAsync(
    const std::string& hostname,
    UltraNetDnsType type,
    std::function<void(const std::vector<std::string>&)> onResult);

UltraNetResult UltraNet_DnsReverseLookup(
    const std::string& ipAddress,
    std::string& outHostname,
    int timeoutMs = 5000);

// In-process DNS cache lives inside UltraNet (libcurl maintains its own
// pool too). These act on the UltraNet-level cache; libcurl's pool stays
// intact across calls until the share / easy handle is destroyed.
void UltraNet_DnsClearCache();

// Stub for now — full custom-server support requires c-ares (Stage 3).
// Stores the list internally so Stage 3 picks it up without an API change.
void UltraNet_DnsSetServers(const std::vector<std::string>& servers);
