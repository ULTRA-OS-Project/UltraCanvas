// Tests/UltraNet/test_loopback.cpp
// Loopback HTTP tests against a Python http.server spawned by the test
// binary itself. No external network required; ideal for CI.
//
// The harness starts python3 -m http.server on 127.0.0.1:<ephemeral> once
// for the whole test binary, serving from a known root that contains a
// few fixture files. Tests SKIP if Python isn't available on PATH.
#include "test_framework.h"

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetCookies.h>
#include <UltraNet/UltraNetHttp.h>
#include <UltraNet/UltraNetSocket.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <sys/types.h>
#include <thread>

#if !defined(_WIN32)
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

constexpr int  kLoopbackPort = 18421;
constexpr auto kStartTimeout = std::chrono::seconds(3);

struct LoopbackServer {
    std::filesystem::path root;
    pid_t   pid = -1;
    bool    started = false;
    std::string baseUrl;

    ~LoopbackServer() { Stop(); }

    bool Start() {
#if defined(_WIN32)
        return false;
#else
        if (started) return true;
        if (std::system("command -v python3 > /dev/null 2>&1") != 0) {
            return false;
        }
        root = std::filesystem::temp_directory_path() / "ultranet_loopback";
        std::filesystem::create_directories(root);
        {
            std::ofstream hello(root / "hello.txt");
            hello << "hello from ultranet loopback\n";
        }
        {
            std::ofstream big(root / "big.bin", std::ios::binary);
            for (int i = 0; i < 4096; ++i) big.put(static_cast<char>(i & 0xff));
        }

        pid_t p = fork();
        if (p < 0) return false;
        if (p == 0) {
            // child: become the server, silenced.
            std::freopen("/dev/null", "w", stdout);
            std::freopen("/dev/null", "w", stderr);
            std::string portStr = std::to_string(kLoopbackPort);
            std::string dir     = root.string();
            execlp("python3", "python3",
                   "-m", "http.server", portStr.c_str(),
                   "--bind", "127.0.0.1",
                   "--directory", dir.c_str(),
                   nullptr);
            _exit(127);
        }
        pid = p;
        // Wait for the server to bind. Probe with a tiny TCP connect.
        baseUrl = "http://127.0.0.1:" + std::to_string(kLoopbackPort);
        const auto deadline = std::chrono::steady_clock::now() + kStartTimeout;
        while (std::chrono::steady_clock::now() < deadline) {
            UltraNetSocketOptions o;
            o.connectTimeoutMs = 200;
            UltraNetHandle h = UltraNet_TcpConnect("127.0.0.1", kLoopbackPort, o);
            if (h != UltraNetInvalidHandle) {
                UltraNet_SocketClose(h);
                started = true;
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        Stop();
        return false;
#endif
    }

    void Stop() {
#if !defined(_WIN32)
        if (pid > 0) {
            kill(pid, SIGTERM);
            int status = 0;
            waitpid(pid, &status, 0);
            pid = -1;
        }
        if (!root.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(root, ec);
        }
        started = false;
#endif
    }
};

std::mutex g_serverMutex;
LoopbackServer& Server() {
    static LoopbackServer s;
    return s;
}

bool EnsureServer() {
    std::lock_guard<std::mutex> lk(g_serverMutex);
    if (Server().started) return true;
    return Server().Start();
}

std::string Base() {
    std::lock_guard<std::mutex> lk(g_serverMutex);
    return Server().baseUrl;
}

} // namespace

TEST(loopback_http_get_text_file) {
    UltraNet_Initialize();
    if (!EnsureServer()) SKIP("python3 not available");

    UltraNetResponse r;
    auto res = UltraNet_HttpGet(Base() + "/hello.txt", r);
    REQUIRE(bool(res));
    REQUIRE_EQ(r.statusCode, 200);
    CHECK(r.GetBodyAsString().find("hello from ultranet") != std::string::npos);
}

TEST(loopback_http_404_status_code) {
    if (!EnsureServer()) SKIP("python3 not available");
    UltraNetResponse r;
    auto res = UltraNet_HttpGet(Base() + "/does-not-exist", r);
    CHECK(!bool(res));                   // failed result (HTTP 404)
    REQUIRE_EQ(r.statusCode, 404);
    REQUIRE_EQ(res.code, UltraNetResultCode::HttpError);
}

TEST(loopback_http_head_returns_no_body) {
    if (!EnsureServer()) SKIP("python3 not available");
    UltraNetResponse r;
    auto res = UltraNet_HttpHead(Base() + "/hello.txt", r);
    REQUIRE(bool(res));
    REQUIRE_EQ(r.statusCode, 200);
    REQUIRE_EQ(r.body.size(), static_cast<std::size_t>(0));
}

TEST(loopback_http_download_streams_to_disk) {
    if (!EnsureServer()) SKIP("python3 not available");
    const std::string path = std::string(std::filesystem::temp_directory_path()) +
                             "/ultranet_loopback_dl.bin";
    std::filesystem::remove(path);
    auto res = UltraNet_HttpDownloadFile(Base() + "/big.bin", path);
    REQUIRE(bool(res));
    REQUIRE(std::filesystem::exists(path));
    REQUIRE_EQ(std::filesystem::file_size(path), static_cast<std::uintmax_t>(4096));
    std::filesystem::remove(path);
}

TEST(loopback_session_reuses_connection) {
    if (!EnsureServer()) SKIP("python3 not available");
    UltraNetHandle s = UltraNet_CreateSession();
    REQUIRE(s != UltraNetInvalidHandle);

    UltraNetResponse r1, r2;
    auto a = UltraNet_SessionHttpGet(s, Base() + "/hello.txt", r1);
    auto b = UltraNet_SessionHttpGet(s, Base() + "/hello.txt", r2);
    CHECK(bool(a));
    CHECK(bool(b));
    REQUIRE_EQ(r1.statusCode, 200);
    REQUIRE_EQ(r2.statusCode, 200);

    UltraNet_DestroySession(s);
}

TEST(loopback_async_get_callback_fires) {
    if (!EnsureServer()) SKIP("python3 not available");
    std::atomic<bool> done{false};
    std::atomic<int>  capturedStatus{-1};

    UltraNetHttpRequest req;
    req.url    = Base() + "/hello.txt";
    req.method = UltraNetHttpMethod::Get;
    UltraNetHandle h = UltraNet_HttpRequestAsync(req,
        [&](const UltraNetResponse& resp) {
            capturedStatus = resp.statusCode;
            done = true;
        });
    REQUIRE(h != UltraNetInvalidHandle);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!done.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(done.load());
    REQUIRE_EQ(capturedStatus.load(), 200);
}

TEST(loopback_progress_callbacks_fire) {
    if (!EnsureServer()) SKIP("python3 not available");
    std::atomic<int>     events{0};
    std::atomic<int64_t> lastBytes{0};

    UltraNetTransferCallbacks cb;
    cb.onDownloadProgress = [&](int64_t now, int64_t /*total*/) {
        ++events; lastBytes = now;
    };
    auto prev = UltraNet_SetTransferCallbacks(cb);

    UltraNetResponse r;
    auto res = UltraNet_HttpGet(Base() + "/big.bin", r);
    UltraNet_SetTransferCallbacks(prev);   // restore so other tests aren't affected

    REQUIRE(bool(res));
    CHECK(events.load() > 0);
    CHECK(lastBytes.load() >= 4096);
}
