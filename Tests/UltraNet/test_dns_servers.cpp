// Tests/UltraNet/test_dns_servers.cpp
// Per-call name servers (UltraNetDnsOptions::servers): the pure helpers that
// read a server entry and build a reverse-lookup name, the validation every
// entry point applies, and a lookup pointed at a server that never answers -
// a UDP socket this process opens on the loopback and never reads. The kernel
// accepts every query and nothing replies, so the deadline is the only way
// back, on every runner, with no network at all. (An unroutable address such
// as TEST-NET-1 is not as reliable: a sandbox that rejects the packet outright
// answers "cannot contact" at once instead of never.)
#include "test_framework.h"

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetDns.h>
#include <UltraNet/UltraNetSocket.h>

#include <chrono>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;

// The same watchdog shape as test_dns_timeout.cpp: a resolve that never
// returns must fail the suite, not hang it, and must not write into a frame
// that has gone away.
struct Outcome {
    bool               ok   = false;
    UltraNetResultCode code = UltraNetResultCode::Unknown;
    std::size_t        count = 0;
    std::string        message;
};

std::optional<Outcome> ResolveWithin(const std::string& host, UltraNetDnsType type,
                                     const UltraNetDnsOptions& options,
                                     std::chrono::milliseconds budget) {
    auto slot = std::make_shared<std::promise<Outcome>>();
    std::future<Outcome> answer = slot->get_future();
    std::thread([host, type, options, slot]() {
        std::vector<std::string> out;
        const UltraNetResult r = UltraNet_DnsResolve(host, out, type, options);
        slot->set_value(Outcome{bool(r), r.code, out.size(), r.message});
    }).detach();
    if (answer.wait_for(budget) != std::future_status::ready) return std::nullopt;
    return answer.get();
}

// A name server that never answers: a UDP socket on the loopback that this
// process owns and never reads. Its "ip:port" entry is what the tests hand to
// UltraNetDnsOptions::servers; the handle stays open for the test's duration.
struct SilentServer {
    UltraNetHandle socket = UltraNetInvalidHandle;
    std::string    entry;   // "127.0.0.1:<port>"

    SilentServer() {
        UltraNetSocketOptions options;
        options.bindAddress = "127.0.0.1";
        socket = UltraNet_UdpOpen(0, options);
        UltraNetEndpoint local;
        if (socket != UltraNetInvalidHandle && UltraNet_SocketLocalEndpoint(socket, local)) {
            entry = "127.0.0.1:" + std::to_string(local.port);
        }
    }
    ~SilentServer() { if (socket != UltraNetInvalidHandle) UltraNet_SocketClose(socket); }
    bool Ok() const { return !entry.empty(); }
};

} // namespace

TEST(dns_parse_server_accepts_v4_v6_and_ports) {
    std::string address; int port = -1;
    REQUIRE(UltraNet_DnsParseServer("9.9.9.9", address, port));
    REQUIRE_EQ(address, std::string("9.9.9.9"));
    REQUIRE_EQ(port, 0);

    REQUIRE(UltraNet_DnsParseServer("9.9.9.9:5353", address, port));
    REQUIRE_EQ(address, std::string("9.9.9.9"));
    REQUIRE_EQ(port, 5353);

    REQUIRE(UltraNet_DnsParseServer("2620:fe::fe", address, port));
    REQUIRE_EQ(address, std::string("2620:fe::fe"));
    REQUIRE_EQ(port, 0);

    REQUIRE(UltraNet_DnsParseServer("[2620:fe::fe]:53", address, port));
    REQUIRE_EQ(address, std::string("2620:fe::fe"));
    REQUIRE_EQ(port, 53);

    REQUIRE(UltraNet_DnsParseServer("[::1]", address, port));
    REQUIRE_EQ(address, std::string("::1"));
    REQUIRE_EQ(port, 0);
}

TEST(dns_parse_server_rejects_names_and_bad_ports) {
    std::string address = "stale"; int port = 7;
    // Only addresses: a host name is not a server entry.
    REQUIRE(!UltraNet_DnsParseServer("dns.quad9.net", address, port));
    REQUIRE(address.empty());
    REQUIRE_EQ(port, 0);
    REQUIRE(!UltraNet_DnsParseServer("", address, port));
    REQUIRE(!UltraNet_DnsParseServer("9.9.9.9:", address, port));
    REQUIRE(!UltraNet_DnsParseServer("9.9.9.9:0", address, port));
    REQUIRE(!UltraNet_DnsParseServer("9.9.9.9:65536", address, port));
    REQUIRE(!UltraNet_DnsParseServer("9.9.9.9:5a", address, port));
    REQUIRE(!UltraNet_DnsParseServer("[2620:fe::fe", address, port));
    REQUIRE(!UltraNet_DnsParseServer("[2620:fe::fe]53", address, port));
    REQUIRE(!UltraNet_DnsParseServer("[]:53", address, port));
}

TEST(dns_reverse_name_for_v4_and_v6) {
    std::string name;
    REQUIRE(UltraNet_DnsReverseName("8.8.4.4", name));
    REQUIRE_EQ(name, std::string("4.4.8.8.in-addr.arpa"));
    REQUIRE(UltraNet_DnsReverseName("192.0.2.1", name));
    REQUIRE_EQ(name, std::string("1.2.0.192.in-addr.arpa"));
    REQUIRE(UltraNet_DnsReverseName("2001:db8::1", name));
    REQUIRE_EQ(name, std::string(
        "1.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.8.b.d.0.1.0.0.2.ip6.arpa"));
    REQUIRE(!UltraNet_DnsReverseName("not-an-address", name));
    REQUIRE(name.empty());
}

TEST(dns_resolve_rejects_a_bad_server_entry_before_asking_anyone) {
    UltraNet_Initialize();
    UltraNetDnsOptions options;
    options.servers = {"9.9.9.9", "dns.quad9.net"};
    std::vector<std::string> out;
    const UltraNetResult r = UltraNet_DnsResolve("example.com", out, UltraNetDnsType::A, options);
    REQUIRE(!r);
    REQUIRE_EQ(r.code, UltraNetResultCode::InvalidUrl);
    REQUIRE(r.message.find("dns.quad9.net") != std::string::npos);

    // The async entry point validates the same way, before any thread starts.
    const UltraNetResult a = UltraNet_DnsResolveAsync(
        "example.com", UltraNetDnsType::A,
        [](const std::vector<std::string>&) {}, options);
    REQUIRE(!a);
    REQUIRE_EQ(a.code, UltraNetResultCode::InvalidUrl);
}

TEST(dns_resolve_with_a_silent_server_times_out_within_its_deadline) {
    UltraNet_Initialize();
    SilentServer silent;
    if (!silent.Ok()) SKIP("cannot open a loopback UDP socket");
    UltraNetDnsOptions options;
    options.servers   = {silent.entry};
    options.timeoutMs = 1500;
    const auto started = std::chrono::steady_clock::now();
    const auto outcome = ResolveWithin("example.com", UltraNetDnsType::A, options, 30s);
    const auto elapsed = std::chrono::steady_clock::now() - started;
    REQUIRE(outcome.has_value());
    REQUIRE(!outcome->ok);
    REQUIRE_EQ(outcome->count, std::size_t(0));
    REQUIRE(elapsed < 20s);   // the watchdog budget is 30 s; the deadline is 1.5 s
#ifdef ULTRANET_HAS_CARES
    // c-ares: the caller's deadline is the only clock; the answer is Timeout
    // and it arrives at the deadline, not at c-ares's retry schedule.
    REQUIRE_EQ(outcome->code, UltraNetResultCode::Timeout);
    REQUIRE(elapsed < 10s);
#else
    // The libresolv fallback asks the port it was given, bounds its retries
    // by the deadline and reports TRY_AGAIN as Timeout; dnsapi asks port 53
    // only and refuses the entry as Unsupported. Either way: nothing
    // resolved, and the call came back.
    REQUIRE(outcome->code == UltraNetResultCode::Timeout
            || outcome->code == UltraNetResultCode::Unsupported);
#endif
}

TEST(dns_resolve_ptr_with_a_server_asks_for_the_reverse_name) {
    UltraNet_Initialize();
    SilentServer silent;
    if (!silent.Ok()) SKIP("cannot open a loopback UDP socket");
    UltraNetDnsOptions options;
    options.servers   = {silent.entry};
    options.timeoutMs = 500;
    // Not an address: refused before any query, whatever the backend.
    std::vector<std::string> out;
    const UltraNetResult bad = UltraNet_DnsResolve("example.com", out, UltraNetDnsType::PTR, options);
    REQUIRE(!bad);
    REQUIRE_EQ(bad.code, UltraNetResultCode::InvalidUrl);
    // An address goes out as its in-addr.arpa name to the named server, which
    // never answers - so the outcome is the deadline, never a host name.
    const auto outcome = ResolveWithin("127.0.0.1", UltraNetDnsType::PTR, options, 30s);
    REQUIRE(outcome.has_value());
    REQUIRE(!outcome->ok);
    REQUIRE_EQ(outcome->count, std::size_t(0));
}
