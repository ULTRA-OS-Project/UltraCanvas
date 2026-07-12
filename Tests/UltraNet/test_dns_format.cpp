// Tests/UltraNet/test_dns_format.cpp
// DNS extra record types — these go through the libresolv backend; we test
// against well-known public records that should always exist.
// Skips when there's no DNS connectivity (sandbox / CI offline scenarios).
#include "test_framework.h"

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetDns.h>

#include <string>
#include <vector>

namespace {
bool HasDnsConnectivity() {
    std::vector<std::string> v;
    auto r = UltraNet_DnsResolve("one.one.one.one", v, UltraNetDnsType::A, 2000);
    return r && !v.empty();
}
}

TEST(dns_a_one_one_one_one) {
    UltraNet_Initialize();
    if (!HasDnsConnectivity()) SKIP("no DNS connectivity");
    std::vector<std::string> v;
    auto r = UltraNet_DnsResolve("one.one.one.one", v, UltraNetDnsType::A);
    REQUIRE(bool(r));
    CHECK(!v.empty());
}

TEST(dns_ptr_reverse) {
    if (!HasDnsConnectivity()) SKIP("no DNS connectivity");
    std::string host;
    auto r = UltraNet_DnsReverseLookup("1.1.1.1", host);
    REQUIRE(bool(r));
    CHECK(host.find("cloudflare") != std::string::npos ||
          host.find("one.one")    != std::string::npos);
}

TEST(dns_mx_record_format) {
    if (!HasDnsConnectivity()) SKIP("no DNS connectivity");
    std::vector<std::string> v;
    auto r = UltraNet_DnsResolve("gmail.com", v, UltraNetDnsType::MX);
    REQUIRE(bool(r));
    CHECK(!v.empty());
    // Each MX record should be "<priority> <host>"
    for (const auto& rec : v) {
        auto space = rec.find(' ');
        CHECK(space != std::string::npos);
        if (space != std::string::npos) {
            const std::string prio = rec.substr(0, space);
            for (char c : prio) CHECK(c >= '0' && c <= '9');
        }
    }
}

TEST(dns_cname_record_format) {
    if (!HasDnsConnectivity()) SKIP("no DNS connectivity");
    std::vector<std::string> v;
    auto r = UltraNet_DnsResolve("www.github.com", v, UltraNetDnsType::CNAME);
    if (!r) SKIP("CNAME not present for www.github.com");
    REQUIRE_EQ(v.size(), static_cast<std::size_t>(1));
}

TEST(dns_invalid_host_returns_error) {
    if (!HasDnsConnectivity()) SKIP("no DNS connectivity");
    std::vector<std::string> v;
    auto r = UltraNet_DnsResolve(
        "nonexistent-host-for-ultranet-test-99999.invalid", v, UltraNetDnsType::A);
    CHECK(!bool(r));
}

TEST(dns_cache_returns_same_addresses_quickly) {
    if (!HasDnsConnectivity()) SKIP("no DNS connectivity");
    std::vector<std::string> v1, v2;
    UltraNet_DnsClearCache();
    UltraNet_DnsResolve("one.one.one.one", v1, UltraNetDnsType::A);
    UltraNet_DnsResolve("one.one.one.one", v2, UltraNetDnsType::A);
    REQUIRE_EQ(v1.size(), v2.size());
    for (std::size_t i = 0; i < v1.size(); ++i) REQUIRE_EQ(v1[i], v2[i]);
}
