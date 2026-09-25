// Tests/UltraNet/test_dns_timeout.cpp
// A synchronous resolve that misses its deadline has to come back, and has
// to leave the resolver usable for everyone else.
//
// Both were broken and nothing noticed: the c-ares backend called
// ares_cancel while still holding the lock its own wait_for had taken, so
// the cancel's callback re-entered that mutex on the same thread and every
// caller of UltraNet_DnsResolve whose lookup missed its deadline stopped
// there for good. ares_cancel also cancelled every other query in flight on
// the shared channel. A resolve is therefore never called directly here:
// it runs on a thread of its own under a watchdog, so a regression fails the
// suite instead of hanging it.
// Version: 0.2.0 - the deadline test asks a black-hole server, per call
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetDns.h>
#include <UltraNet/UltraNetSocket.h>

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;

struct Outcome {
    bool                ok   = false;
    UltraNetResultCode  code = UltraNetResultCode::Unknown;
    std::size_t         count = 0;
};

// Resolves on a thread of its own and waits `budget` for it. Everything the
// thread touches is shared state it owns a reference to, never this frame's
// locals: a resolve that never returns must not also be writing into a stack
// that has gone away. nullopt means it had not come back in time.
std::optional<Outcome> ResolveWithin(const std::string& host, int timeoutMs,
                                     std::chrono::milliseconds budget) {
    auto slot = std::make_shared<std::promise<Outcome>>();
    std::future<Outcome> answer = slot->get_future();
    std::thread([host, timeoutMs, slot]() {
        std::vector<std::string> addresses;
        const UltraNetResult r =
            UltraNet_DnsResolve(host, addresses, UltraNetDnsType::A, timeoutMs);
        slot->set_value(Outcome{bool(r), r.code, addresses.size()});
    }).detach();

    if (answer.wait_for(budget) != std::future_status::ready) return std::nullopt;
    return answer.get();
}

// There is no carrying on from a wedged resolver: the c-ares channel is a
// static whose destructor calls ares_destroy, which would block on the very
// lock the stuck thread holds, so a run that merely recorded the failure
// would hang at exit instead of reporting it. Say what happened and leave
// with a failing status.
[[noreturn]] void BailOut(const char* file, int line, const char* what) {
    std::fprintf(stderr, "\n    FAIL %s:%d  %s\n", file, line, what);
    std::fprintf(stderr, "    the resolver is deadlocked; ending the run here "
                         "so it fails rather than hangs\n");
    std::fflush(nullptr);
    std::_Exit(1);
}

#define BAIL_OUT(what) BailOut(__FILE__, __LINE__, (what))

bool HasDnsConnectivity() {
    auto probe = ResolveWithin("one.one.one.one", 4000, 30s);
    return probe && probe->ok && probe->count > 0;
}

const char* kUnresolvable = "ultranet-deadline.invalid";

// A name server that never answers: a UDP socket on the loopback that this
// process owns and never reads. The kernel accepts every query and nothing
// replies, so the deadline is the only way back - on every runner, offline.
// Naming it per call is what makes the deadline test deterministic: a local
// caching resolver that knows .invalid answered inside the millisecond on the
// macOS runners, and that answer was not a missed deadline.
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

std::optional<Outcome> ResolveWithinVia(const std::string& host, const std::string& server,
                                        int timeoutMs, std::chrono::milliseconds budget) {
    auto slot = std::make_shared<std::promise<Outcome>>();
    std::future<Outcome> answer = slot->get_future();
    std::thread([host, server, timeoutMs, slot]() {
        UltraNetDnsOptions options;
        options.servers   = {server};
        options.timeoutMs = timeoutMs;
        std::vector<std::string> addresses;
        const UltraNetResult r =
            UltraNet_DnsResolve(host, addresses, UltraNetDnsType::A, options);
        slot->set_value(Outcome{bool(r), r.code, addresses.size()});
    }).detach();
    if (answer.wait_for(budget) != std::future_status::ready) return std::nullopt;
    return answer.get();
}

} // namespace

// A deadline the lookup cannot possibly meet: one millisecond, against a
// server that never answers.
TEST(dns_resolve_honours_its_deadline) {
    UltraNet_Initialize();
    SilentServer silent;
    if (!silent.Ok()) SKIP("cannot open a loopback UDP socket");
    for (int i = 0; i < 8; ++i) {
        const std::string host = std::to_string(i) + "." + kUnresolvable;
        const auto outcome = ResolveWithinVia(host, silent.entry, 1, 30s);
        if (!outcome) BAIL_OUT("UltraNet_DnsResolve never returned from a 1 ms deadline");
        REQUIRE(!outcome->ok);
#ifdef ULTRANET_HAS_CARES
        // The c-ares backend honours the caller's deadline exactly: nothing
        // can answer from a black hole, so the only way back is Timeout.
        REQUIRE_EQ(outcome->code, UltraNetResultCode::Timeout);
#else
        // libresolv asks the port it was given, rounds the deadline up to a
        // whole second and reports TRY_AGAIN as Timeout; dnsapi asks port 53
        // only and refuses the entry. Nothing resolved, and the call came back.
        REQUIRE(outcome->code == UltraNetResultCode::Timeout
                || outcome->code == UltraNetResultCode::Unsupported);
#endif
    }
}

// The query that timed out is abandoned, not cancelled - cancelling took
// every other query on the shared channel down with it.
TEST(dns_timeout_leaves_the_resolver_usable) {
    UltraNet_Initialize();
    if (!HasDnsConnectivity()) SKIP("no DNS connectivity");

    for (int i = 0; i < 4; ++i) {
        const std::string host = "collateral-" + std::to_string(i) + "." + kUnresolvable;
        if (!ResolveWithin(host, 1, 30s)) {
            BAIL_OUT("UltraNet_DnsResolve never returned from a 1 ms deadline");
        }
    }

    // Past the cache, so this is a real query on the channel the timeouts
    // above left behind.
    UltraNet_DnsClearCache();
    const auto after = ResolveWithin("one.one.one.one", 4000, 30s);
    if (!after) BAIL_OUT("a normal lookup never returned after a timed-out one");
    REQUIRE(after->ok);
    CHECK(after->count > 0);
}

// The asynchronous path answers every query it accepts, including the ones
// that fail - it owns the pending state until it does.
TEST(dns_resolve_async_always_answers) {
    UltraNet_Initialize();

    struct Answered {
        std::mutex              mu;
        std::condition_variable cv;
        int                     count = 0;
    };
    // Shared with the callbacks: one that lands late must not write into a
    // test frame that has already returned.
    auto answered = std::make_shared<Answered>();
    const int queries = 4;

    for (int i = 0; i < queries; ++i) {
        const std::string host = "async-" + std::to_string(i) + "." + kUnresolvable;
        const auto started = UltraNet_DnsResolveAsync(
            host, UltraNetDnsType::A,
            [answered](const std::vector<std::string>&) {
                {
                    std::lock_guard<std::mutex> lk(answered->mu);
                    ++answered->count;
                }
                answered->cv.notify_all();
            });
        if (!started) SKIP("asynchronous DNS is not available in this build");
    }

    std::unique_lock<std::mutex> lk(answered->mu);
    const bool all = answered->cv.wait_for(
        lk, 30s, [&] { return answered->count == queries; });
    CHECK(all);
}
