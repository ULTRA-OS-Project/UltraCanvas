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
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetDns.h>

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

} // namespace

// A deadline the lookup cannot possibly meet: one millisecond, against a
// name in a TLD reserved never to resolve.
TEST(dns_resolve_honours_its_deadline) {
    UltraNet_Initialize();
    for (int i = 0; i < 8; ++i) {
        const std::string host = std::to_string(i) + "." + kUnresolvable;
        const auto outcome = ResolveWithin(host, 1, 30s);
        if (!outcome) BAIL_OUT("UltraNet_DnsResolve never returned from a 1 ms deadline");
        REQUIRE(!outcome->ok);
#ifdef ULTRANET_HAS_CARES
        // Only the c-ares backend honours the deadline; the getaddrinfo
        // fallback takes as long as the system resolver does and reports
        // whatever it found out.
        REQUIRE_EQ(outcome->code, UltraNetResultCode::Timeout);
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
