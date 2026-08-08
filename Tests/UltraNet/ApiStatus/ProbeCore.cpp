// Tests/UltraNet/ApiStatus/ProbeCore.cpp
// Probes for UltraNet/UltraNetCore.h — lifecycle, configuration, version and
// backend reporting, proxy helpers, cancellation/stats, transfer callbacks.
// Probe order follows the header.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "ApiStatus.h"
#include "ProbeServer.h"

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetHttp.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>

using namespace ultranet_apistatus;

namespace {

constexpr const char* kArea = "Core";

} // namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

// Runs first in the report, and deliberately so: it tears the module down and
// brings it back up while nothing else is in flight.
ULTRANET_PROBE(kArea, UltraNet_Initialize) {
    const UltraNetConfig saved = UltraNet_GetConfig();

    UltraNet_Shutdown();
    PROBE_EXPECT(!UltraNet_IsInitialized());

    UltraNetConfig cfg = UltraNetConfig::Default();
    cfg.userAgent = "UltraNetApiStatus/0.1";
    const UltraNetResult r = UltraNet_Initialize(cfg);
    PROBE_EXPECT_MSG(static_cast<bool>(r), r.message);
    PROBE_EXPECT(UltraNet_IsInitialized());
    PROBE_EXPECT(UltraNet_GetConfig().userAgent == cfg.userAgent);

    UltraNet_SetConfig(saved);
    return Working("shutdown -> initialize round trip; config applied");
}

ULTRANET_PROBE(kArea, UltraNet_Shutdown) {
    const UltraNetConfig saved = UltraNet_GetConfig();
    UltraNet_Shutdown();
    PROBE_EXPECT(!UltraNet_IsInitialized());
    PROBE_EXPECT(static_cast<bool>(UltraNet_Initialize(saved)));
    return Working("clears initialized state; module restored afterwards");
}

ULTRANET_PROBE(kArea, UltraNet_IsInitialized) {
    PROBE_EXPECT(UltraNet_IsInitialized());
    UltraNet_Shutdown();
    PROBE_EXPECT(!UltraNet_IsInitialized());
    PROBE_EXPECT(static_cast<bool>(UltraNet_Initialize()));
    PROBE_EXPECT(UltraNet_IsInitialized());
    return Working("tracks both states across a shutdown/initialize cycle");
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

ULTRANET_PROBE(kArea, UltraNet_GetConfig) {
    const UltraNetConfig saved = UltraNet_GetConfig();
    UltraNetConfig cfg = saved;
    cfg.maxRedirects     = 7;
    cfg.defaultTimeoutMs = 12345;
    UltraNet_SetConfig(cfg);

    const UltraNetConfig readBack = UltraNet_GetConfig();
    UltraNet_SetConfig(saved);

    PROBE_EXPECT(readBack.maxRedirects == 7);
    PROBE_EXPECT(readBack.defaultTimeoutMs == 12345);
    return Working("returns the live config, including values just written");
}

ULTRANET_PROBE(kArea, UltraNet_SetConfig) {
    const UltraNetConfig saved = UltraNet_GetConfig();

    // Prove the setter reaches the request path, not just the struct: a 1 ms
    // timeout must make a real request fail with Timeout.
    LoopbackServer& server = Loopback();
    if (!server.Running()) {
        UltraNetConfig cfg = saved;
        cfg.userAgent = "probe-set-config";
        UltraNet_SetConfig(cfg);
        const bool ok = UltraNet_GetConfig().userAgent == "probe-set-config";
        UltraNet_SetConfig(saved);
        PROBE_EXPECT(ok);
        return Implemented("value stored, but the loopback origin could not "
                           "start so the effect on a live request is unverified: " +
                           server.Error());
    }

    UltraNetConfig cfg = saved;
    cfg.defaultTimeoutMs = 1;
    UltraNet_SetConfig(cfg);
    UltraNetResponse slow;
    const UltraNetResult timedOut = UltraNet_HttpGet(server.Url("/slow"), slow);
    UltraNet_SetConfig(saved);

    PROBE_EXPECT(!timedOut);
    PROBE_EXPECT(timedOut.code == UltraNetResultCode::Timeout ||
                 timedOut.code == UltraNetResultCode::ConnectionTimeout);
    return Working("config change observed by a live request "
                   "(1 ms defaultTimeoutMs -> Timeout)");
}

// ---------------------------------------------------------------------------
// Introspection
// ---------------------------------------------------------------------------

ULTRANET_PROBE(kArea, UltraNet_GetVersion) {
    const std::string v = UltraNet_GetVersion();
    PROBE_EXPECT(!v.empty());
    PROBE_EXPECT(v.find('.') != std::string::npos);
    return Working("returns \"" + v + "\"");
}

ULTRANET_PROBE(kArea, UltraNet_GetBackendInfo) {
    const std::string info = UltraNet_GetBackendInfo();
    PROBE_EXPECT(!info.empty());
    PROBE_EXPECT(info.find("libcurl/") != std::string::npos);
    return Working(info);
}

ULTRANET_PROBE(kArea, UltraNet_HasHttp3) {
    const bool has = UltraNet_HasHttp3();
    // The answer is a property of the linked libcurl, not of UltraNet — both
    // answers are correct, so what is verified is that the probe agrees with
    // the backend string.
    const std::string backend = UltraNet_GetBackendInfo();
    const bool backendSaysHttp3 = backend.find("+HTTP3") != std::string::npos;
    PROBE_EXPECT(has == backendSaysHttp3);
    return Working(has ? "linked libcurl reports HTTP/3 (QUIC) support"
                       : "linked libcurl has no HTTP/3; UltraNetConfig::enableHttp3 "
                         "would fall back to HTTP/2");
}

// ---------------------------------------------------------------------------
// Proxy helpers
// ---------------------------------------------------------------------------

ULTRANET_PROBE(kArea, UltraNet_DetectSystemProxy) {
#if defined(_WIN32) || defined(_WIN64)
    UltraNetProxyConfig detected;
    UltraNet_DetectSystemProxy(detected);
    return Implemented("WinHTTP-backed detection ran (found: " +
                       std::string(detected.IsEnabled() ? detected.host : "no proxy") +
                       "); the probe cannot force a known system setting to "
                       "check the parse against");
#else
    // Linux/macOS read the *_proxy environment variables at call time, so the
    // probe can plant a known value and check the parse.
    const char* previous = std::getenv("https_proxy");
    const std::string saved = previous ? previous : "";
    const bool hadPrevious = previous != nullptr;

    ::setenv("https_proxy", "http://user:secret@proxy.probe.invalid:3128", 1);
    ::setenv("no_proxy", "localhost,127.0.0.1", 1);

    UltraNetProxyConfig detected;
    UltraNet_DetectSystemProxy(detected);

    if (hadPrevious) ::setenv("https_proxy", saved.c_str(), 1);
    else             ::unsetenv("https_proxy");
    ::unsetenv("no_proxy");

    PROBE_EXPECT(detected.type == UltraNetProxyType::Http);
    PROBE_EXPECT(detected.host == "proxy.probe.invalid");
    PROBE_EXPECT(detected.port == 3128);
    PROBE_EXPECT(detected.credentials.username == "user");
    PROBE_EXPECT(detected.credentials.password == "secret");
    PROBE_EXPECT(detected.noProxyHosts.size() == 2);
    return Working("parsed a planted https_proxy URL: scheme, host, port, "
                   "credentials and no_proxy list");
#endif
}

ULTRANET_PROBE(kArea, UltraNet_SetGlobalProxy) {
    const UltraNetProxyConfig saved = UltraNet_GetGlobalProxy();

    UltraNetProxyConfig p;
    p.type = UltraNetProxyType::Socks5;
    p.host = "127.0.0.1";
    p.port = 1080;
    UltraNet_SetGlobalProxy(p);
    const UltraNetProxyConfig readBack = UltraNet_GetGlobalProxy();
    const UltraNetConfig cfg = UltraNet_GetConfig();

    UltraNet_SetGlobalProxy(saved);

    PROBE_EXPECT(readBack.type == UltraNetProxyType::Socks5);
    PROBE_EXPECT(readBack.host == "127.0.0.1" && readBack.port == 1080);
    PROBE_EXPECT(cfg.proxy.type == UltraNetProxyType::Socks5);
    return Working("writes through to UltraNetConfig::proxy");
}

ULTRANET_PROBE(kArea, UltraNet_GetGlobalProxy) {
    const UltraNetProxyConfig saved = UltraNet_GetGlobalProxy();

    UltraNetProxyConfig p;
    p.type = UltraNetProxyType::Https;
    p.host = "proxy.example";
    p.port = 8443;
    UltraNet_SetGlobalProxy(p);
    const UltraNetProxyConfig readBack = UltraNet_GetGlobalProxy();
    UltraNet_SetGlobalProxy(saved);

    PROBE_EXPECT(readBack.type == UltraNetProxyType::Https);
    PROBE_EXPECT(readBack.host == "proxy.example" && readBack.port == 8443);
    PROBE_EXPECT(readBack.IsEnabled());
    return Working("round-trips the value written by UltraNet_SetGlobalProxy");
}

ULTRANET_PROBE(kArea, UltraNet_DisableProxy) {
    const UltraNetProxyConfig saved = UltraNet_GetGlobalProxy();

    UltraNetProxyConfig p;
    p.type = UltraNetProxyType::Http;
    p.host = "proxy.example";
    p.port = 3128;
    UltraNet_SetGlobalProxy(p);
    UltraNet_DisableProxy();
    const UltraNetProxyConfig after = UltraNet_GetGlobalProxy();

    UltraNet_SetGlobalProxy(saved);

    PROBE_EXPECT(after.type == UltraNetProxyType::None);
    PROBE_EXPECT(!after.IsEnabled());
    return Working("clears a previously set global proxy");
}

// ---------------------------------------------------------------------------
// Cancellation, activity and stats (async request registry)
// ---------------------------------------------------------------------------

ULTRANET_PROBE(kArea, UltraNet_CancelRequest) {
    LoopbackServer& server = Loopback();
    if (!server.Running()) {
        const UltraNetResult bad = UltraNet_CancelRequest(UltraNetInvalidHandle);
        PROBE_EXPECT(!bad && bad.code == UltraNetResultCode::InvalidHandle);
        return Implemented("rejects the invalid handle correctly, but no "
                           "loopback origin to cancel a live request against: " +
                           server.Error());
    }

    std::mutex m;
    std::condition_variable cv;
    bool done = false;
    std::string statusMessage;

    UltraNetHttpRequest req;
    req.url = server.Url("/slow");
    const UltraNetHandle h = UltraNet_HttpRequestAsync(
        req, [&](const UltraNetResponse& resp) {
            std::lock_guard<std::mutex> lk(m);
            statusMessage = resp.statusMessage;
            done = true;
            cv.notify_all();
        });
    PROBE_EXPECT(h != UltraNetInvalidHandle);

    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const UltraNetResult cancelled = UltraNet_CancelRequest(h);
    PROBE_EXPECT_MSG(static_cast<bool>(cancelled), cancelled.message);

    std::unique_lock<std::mutex> lk(m);
    const bool fired = cv.wait_for(lk, std::chrono::seconds(5), [&] { return done; });
    PROBE_EXPECT_MSG(fired, "onComplete never fired after cancellation");
    PROBE_EXPECT(statusMessage == "Cancelled");

    const UltraNetResult bad = UltraNet_CancelRequest(UltraNetInvalidHandle);
    PROBE_EXPECT(!bad && bad.code == UltraNetResultCode::InvalidHandle);
    return Working("cancelled an in-flight async GET; onComplete reported "
                   "\"Cancelled\"; invalid handle rejected");
}

ULTRANET_PROBE(kArea, UltraNet_IsRequestActive) {
    LoopbackServer& server = Loopback();
    PROBE_EXPECT(!UltraNet_IsRequestActive(UltraNetInvalidHandle));
    if (!server.Running()) {
        return Implemented("invalid handle answered correctly; no loopback "
                           "origin for the active case: " + server.Error());
    }

    std::atomic<bool> done{false};
    UltraNetHttpRequest req;
    req.url = server.Url("/slow");
    const UltraNetHandle h = UltraNet_HttpRequestAsync(
        req, [&](const UltraNetResponse&) { done.store(true); });
    PROBE_EXPECT(h != UltraNetInvalidHandle);

    bool sawActive = false;
    for (int i = 0; i < 50 && !sawActive; ++i) {
        sawActive = UltraNet_IsRequestActive(h);
        if (!sawActive) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    UltraNet_CancelRequest(h);
    for (int i = 0; i < 100 && !done.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    PROBE_EXPECT_MSG(sawActive, "request never reported active while in flight");
    PROBE_EXPECT(!UltraNet_IsRequestActive(h));
    return Working("true while an async request is in flight, false after it ends");
}

ULTRANET_PROBE(kArea, UltraNet_GetTransferStats) {
    LoopbackServer& server = Loopback();
    if (!server.Running()) {
        const UltraNetTransferStats none = UltraNet_GetTransferStats(UltraNetInvalidHandle);
        PROBE_EXPECT(none.bytesReceived == 0);
        return Implemented("zero-initialised stats for an invalid handle; no "
                           "loopback origin for a live transfer: " + server.Error());
    }

    std::atomic<bool> done{false};
    UltraNetHttpRequest req;
    req.url = server.Url("/slow");
    const UltraNetHandle h = UltraNet_HttpRequestAsync(
        req, [&](const UltraNetResponse&) { done.store(true); });
    PROBE_EXPECT(h != UltraNetInvalidHandle);

    UltraNetTransferStats stats;
    for (int i = 0; i < 100; ++i) {
        stats = UltraNet_GetTransferStats(h);
        if (stats.bytesReceived > 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    UltraNet_CancelRequest(h);
    for (int i = 0; i < 100 && !done.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    PROBE_EXPECT_MSG(stats.bytesReceived > 0,
                     "no bytesReceived reported for an in-flight download");
    return Working("reported " + std::to_string(stats.bytesReceived) +
                   " bytes received for an in-flight async transfer");
}

// ---------------------------------------------------------------------------
// Global transfer callbacks
// ---------------------------------------------------------------------------

ULTRANET_PROBE(kArea, UltraNet_SetTransferCallbacks) {
    LoopbackServer& server = Loopback();

    std::atomic<int> downloadCalls{0};
    std::atomic<long long> lastBytes{0};

    UltraNetTransferCallbacks cb;
    cb.onDownloadProgress = [&](int64_t now, int64_t) {
        downloadCalls.fetch_add(1);
        lastBytes.store(now);
    };
    const UltraNetTransferCallbacks previous = UltraNet_SetTransferCallbacks(cb);

    bool fired = false;
    std::string note;
    if (server.Running()) {
        UltraNetResponse resp;
        const UltraNetResult r = UltraNet_HttpGet(server.Url("/bytes/262144"), resp);
        fired = static_cast<bool>(r) && downloadCalls.load() > 0;
        note = fired ? ("onDownloadProgress fired " +
                        std::to_string(downloadCalls.load()) + " times, last at " +
                        std::to_string(lastBytes.load()) + " bytes")
                     : "callbacks installed but never fired during a live download";
    }

    // The returned bag must be the one that was installed before this call.
    const UltraNetTransferCallbacks roundTrip = UltraNet_SetTransferCallbacks(previous);
    const bool returnedOurs = static_cast<bool>(roundTrip.onDownloadProgress);

    PROBE_EXPECT_MSG(returnedOurs,
                     "the setter did not return the previously installed bag");
    if (!server.Running()) {
        return Implemented("bag installed and returned on replacement, but no "
                           "loopback origin to see it fire: " + server.Error());
    }
    PROBE_EXPECT_MSG(fired, note);
    return Working(note + "; previous bag returned on replacement");
}

ULTRANET_PROBE(kArea, UltraNet_GetTransferCallbacks) {
    const UltraNetTransferCallbacks saved = UltraNet_GetTransferCallbacks();

    UltraNetTransferCallbacks cb;
    cb.onUploadProgress = [](int64_t, int64_t) {};
    cb.onTransferStats  = [](int64_t, int64_t, double) {};
    UltraNet_SetTransferCallbacks(cb);

    const UltraNetTransferCallbacks readBack = UltraNet_GetTransferCallbacks();
    UltraNet_SetTransferCallbacks(saved);

    PROBE_EXPECT(static_cast<bool>(readBack.onUploadProgress));
    PROBE_EXPECT(static_cast<bool>(readBack.onTransferStats));
    PROBE_EXPECT(!readBack.onDownloadProgress);
    return Working("returns exactly the bag last installed");
}
