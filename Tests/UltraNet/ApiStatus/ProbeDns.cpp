// Tests/UltraNet/ApiStatus/ProbeDns.cpp
// Probes for UltraNet/UltraNetDns.h.
//
// A/AAAA/PTR resolve through the system resolver (or c-ares when it was found
// at configure time) and can be checked offline against loopback and
// /etc/hosts. The remaining record types and custom-server support need a
// reachable DNS server, so they report IMPLEMENTED unless --network is given.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "ApiStatus.h"

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetDns.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>

using namespace ultranet_apistatus;

namespace {

constexpr const char* kArea = "DNS";

#ifdef ULTRANET_HAS_CARES
constexpr bool kHasCares = true;
constexpr const char* kBackend = "c-ares";
#else
constexpr bool kHasCares = false;
constexpr const char* kBackend = "system resolver (getaddrinfo/libresolv/dnsapi)";
#endif

bool Contains(const std::vector<std::string>& v, const std::string& needle) {
    return std::find(v.begin(), v.end(), needle) != v.end();
}

std::string Join(const std::vector<std::string>& v) {
    std::string out;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i) out += ", ";
        out += v[i];
    }
    return out;
}

} // namespace

ULTRANET_PROBE(kArea, UltraNet_DnsResolve) {
    std::vector<std::string> ignored;
    const UltraNetResult empty = UltraNet_DnsResolve("", ignored);
    PROBE_EXPECT(!empty && empty.code == UltraNetResultCode::InvalidUrl);

    // A numeric address always resolves to itself, with or without a network.
    std::vector<std::string> numeric;
    const UltraNetResult rNumeric = UltraNet_DnsResolve("127.0.0.1", numeric);
    PROBE_EXPECT_MSG(static_cast<bool>(rNumeric), rNumeric.message);
    PROBE_EXPECT(Contains(numeric, "127.0.0.1"));

    // /etc/hosts (or the platform equivalent) covers "localhost" offline.
    std::vector<std::string> local;
    const UltraNetResult rLocal = UltraNet_DnsResolve("localhost", local);
    if (!rLocal || local.empty()) {
        return Implemented(std::string("numeric A lookup verified via ") + kBackend +
                           ", but \"localhost\" did not resolve (" + rLocal.message +
                           ") so name resolution itself is unverified");
    }
    PROBE_EXPECT(Contains(local, "127.0.0.1"));

    // A non-resolvable name must report HostNotFound, not success.
    std::vector<std::string> bogus;
    const UltraNetResult rBogus =
        UltraNet_DnsResolve("ultranet-probe-does-not-exist.invalid", bogus, UltraNetDnsType::A, 3000);
    PROBE_EXPECT_MSG(!rBogus, "a .invalid name resolved successfully");

    return Working(std::string("A records via ") + kBackend +
                   ": localhost -> " + Join(local) +
                   "; numeric passthrough and empty/unknown names rejected");
}

ULTRANET_PROBE_NAMED(kArea, "UltraNet_DnsResolve (AAAA)", DnsResolveAaaa) {
    std::vector<std::string> v6;
    const UltraNetResult r = UltraNet_DnsResolve("localhost", v6, UltraNetDnsType::AAAA, 3000);
    if (!r || v6.empty()) {
        return Implemented("AAAA path exercised but this host has no IPv6 "
                           "loopback record to check against (" + r.message + ")");
    }
    PROBE_EXPECT(Contains(v6, "::1"));
    return Working("localhost AAAA -> " + Join(v6));
}

ULTRANET_PROBE_NAMED(kArea, "UltraNet_DnsResolve (MX/TXT/SRV/NS/CNAME/SOA)",
                     DnsResolveRecordTypes) {
    if (!NetworkProbesEnabled()) {
        return Implemented(std::string("record-type backend present (") + kBackend +
                           "); these queries need a reachable DNS server — "
                           "re-run with --network to verify them");
    }
    const std::string domain = EnvOverride("ULTRANET_PROBE_DNS_DOMAIN").empty()
                               ? "example.com"
                               : EnvOverride("ULTRANET_PROBE_DNS_DOMAIN");

    std::vector<std::string> soa;
    const UltraNetResult rSoa = UltraNet_DnsResolve(domain, soa, UltraNetDnsType::SOA, 5000);
    if (!rSoa) {
        if (rSoa.code == UltraNetResultCode::UnsupportedScheme) {
            return NotImplemented("SOA/MX/TXT/... return UnsupportedScheme in "
                                  "this build");
        }
        return Implemented("SOA query for " + domain + " failed (" + rSoa.message +
                           "); DNS may be filtered on this network");
    }
    PROBE_EXPECT(!soa.empty());

    std::vector<std::string> ns;
    const UltraNetResult rNs = UltraNet_DnsResolve(domain, ns, UltraNetDnsType::NS, 5000);
    PROBE_EXPECT_MSG(static_cast<bool>(rNs) && !ns.empty(),
                     "NS query returned nothing for " + domain);
    return Working("live SOA and NS records retrieved for " + domain +
                   " via " + kBackend);
}

ULTRANET_PROBE(kArea, UltraNet_DnsResolveAsync) {
    const UltraNetResult noCallback =
        UltraNet_DnsResolveAsync("localhost", UltraNetDnsType::A, nullptr);
    PROBE_EXPECT(!noCallback && noCallback.code == UltraNetResultCode::InvalidState);

    std::mutex m;
    std::condition_variable cv;
    bool done = false;
    std::vector<std::string> addresses;

    const UltraNetResult r = UltraNet_DnsResolveAsync(
        "localhost", UltraNetDnsType::A,
        [&](const std::vector<std::string>& result) {
            std::lock_guard<std::mutex> lk(m);
            addresses = result;
            done = true;
            cv.notify_all();
        });
    PROBE_EXPECT_MSG(static_cast<bool>(r), r.message);

    std::unique_lock<std::mutex> lk(m);
    const bool fired = cv.wait_for(lk, std::chrono::seconds(10), [&] { return done; });
    PROBE_EXPECT_MSG(fired, "onResult never fired within 10s");
    if (addresses.empty()) {
        return Implemented("callback fired but returned no addresses for "
                           "localhost, so the result path is unverified");
    }
    PROBE_EXPECT(Contains(addresses, "127.0.0.1"));
    return Working(std::string("async localhost lookup delivered ") +
                   Join(addresses) + " via " + kBackend +
                   "; missing callback rejected");
}

ULTRANET_PROBE(kArea, UltraNet_DnsReverseLookup) {
    std::string host;
    const UltraNetResult empty = UltraNet_DnsReverseLookup("", host);
    PROBE_EXPECT(!empty && empty.code == UltraNetResultCode::InvalidUrl);

    const UltraNetResult notAnIp = UltraNet_DnsReverseLookup("not-an-ip", host);
    PROBE_EXPECT(!notAnIp && notAnIp.code == UltraNetResultCode::InvalidUrl);

    const UltraNetResult r = UltraNet_DnsReverseLookup("127.0.0.1", host);
    if (!r) {
        return Implemented("input validation verified, but 127.0.0.1 has no "
                           "reverse record on this host (" + r.message + ")");
    }
    PROBE_EXPECT(!host.empty());
    return Working("127.0.0.1 -> \"" + host + "\"; empty and non-IP input "
                   "rejected as InvalidUrl");
}

ULTRANET_PROBE(kArea, UltraNet_DnsClearCache) {
    // Populate, clear, and confirm lookups still work afterwards. The cache
    // itself has no accessor, so "the entry really went away" is not
    // observable through the public API — hence IMPLEMENTED, not WORKING.
    std::vector<std::string> before;
    const UltraNetResult r1 = UltraNet_DnsResolve("127.0.0.1", before);
    PROBE_EXPECT_MSG(static_cast<bool>(r1), r1.message);

    UltraNet_DnsClearCache();
    UltraNet_DnsClearCache();   // idempotent

    std::vector<std::string> after;
    const UltraNetResult r2 = UltraNet_DnsResolve("127.0.0.1", after);
    PROBE_EXPECT_MSG(static_cast<bool>(r2), r2.message);
    PROBE_EXPECT(before == after);
    return Implemented("runs without error and lookups keep working, but the "
                       "in-process cache has no public accessor, so the "
                       "eviction itself cannot be observed from the API");
}

// Declared last in this file on purpose: with c-ares the servers it installs
// are process-wide and there is no getter to restore the previous list, so the
// destructive form only runs when the caller opted into network probes.
ULTRANET_PROBE(kArea, UltraNet_DnsSetServers) {
    if (!kHasCares) {
        UltraNet_DnsSetServers({"192.0.2.1"});   // stored, then ignored
        return NotImplemented("documented no-op without c-ares: the list is "
                              "stored internally but the system resolver "
                              "(getaddrinfo/libresolv/dnsapi) cannot be pointed "
                              "at it. Build with libcares-dev to enable.");
    }
    if (!NetworkProbesEnabled()) {
        return Implemented("c-ares backend present, so the list is forwarded to "
                           "ares_set_servers_csv; verifying it means pointing "
                           "the resolver at a blackhole address, which is "
                           "process-wide and irreversible (no getter to restore "
                           "the previous servers) — re-run with --network");
    }

    // With --network: a name that resolves normally must stop resolving once
    // the resolver points at TEST-NET-1 (RFC 5737), which blackholes queries.
    const std::string name = EnvOverride("ULTRANET_PROBE_DNS_DOMAIN").empty()
                             ? "example.com"
                             : EnvOverride("ULTRANET_PROBE_DNS_DOMAIN");
    std::vector<std::string> baseline;
    const UltraNetResult before = UltraNet_DnsResolve(name, baseline, UltraNetDnsType::A, 5000);
    if (!before) {
        return Implemented("cannot verify: " + name + " does not resolve even "
                           "with the system servers (" + before.message + ")");
    }

    UltraNet_DnsSetServers({"192.0.2.1"});
    UltraNet_DnsClearCache();
    std::vector<std::string> blackholed;
    const UltraNetResult after =
        UltraNet_DnsResolve(name, blackholed, UltraNetDnsType::A, 2000);

    UltraNet_DnsClearCache();

    if (after) {
        return Broken("resolution still succeeded after pointing the resolver "
                      "at a blackhole server, so the custom server list was "
                      "not applied");
    }
    return Working("pointing the resolver at 192.0.2.1 stopped " + name +
                   " from resolving (" + after.message + "), proving the "
                   "custom server list reaches c-ares. NOTE: the process "
                   "resolver stays pointed at the test server for the rest of "
                   "this run.");
}
