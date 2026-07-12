// Tests/UltraNet/test_websocket.cpp
// Loopback WebSocket tests against a Python `websockets`-package server
// spawned by the test binary. Exercises the parts of UltraNetWebSocket.cpp
// that aren't covered by the existing "unsupported by libcurl" runtime
// detection: receiver thread, send mutex, text + binary frame round-trips,
// IsOpen / GetState transitions, and clean close handshake.
//
// Two preconditions must be satisfied or the tests SKIP cleanly:
//   1. libcurl built with --enable-websockets (Ubuntu 24.04's stock build
//      does NOT have this; vcpkg curl[websockets], brew curl, and modern
//      MSYS2 builds do).
//   2. The Python `websockets` package is installed on PATH python3.
#include "test_framework.h"

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetHttp.h>
#include <UltraNet/UltraNetSocket.h>
#include <UltraNet/UltraNetWebSocket.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

#if !defined(_WIN32)
constexpr int  kWsPort  = 18641;

long g_wsPid = -1;
std::filesystem::path g_wsScript;
bool g_wsStarted = false;
std::mutex g_wsMutex;

bool PythonHasWebsockets() {
    return std::system(
        "python3 -c \"import websockets\" > /dev/null 2>&1") == 0;
}
// Note: libcurl WS support is detected per-test by attempting a real
// connect against the live server and checking for InvalidHandle.

bool StartWsServer() {
    std::lock_guard<std::mutex> lk(g_wsMutex);
    if (g_wsStarted) return true;
    if (std::system("command -v python3 > /dev/null 2>&1") != 0) return false;
    if (!PythonHasWebsockets()) return false;

    auto dir = std::filesystem::temp_directory_path() / "ultranet_ws_test";
    std::filesystem::create_directories(dir);
    g_wsScript = dir / "ws_server.py";
    {
        std::ofstream out(g_wsScript);
        out <<
R"PY(
import asyncio
from websockets.asyncio.server import serve

async def handler(websocket):
    path = websocket.request.path if hasattr(websocket, 'request') else '/'
    if path == '/close':
        # Send one message then close cleanly.
        await websocket.send("about-to-close")
        await websocket.close(code=1000, reason="bye")
        return
    # /echo (default): echo every frame, preserving text vs binary.
    async for message in websocket:
        await websocket.send(message)

async def main():
    async with serve(handler, "127.0.0.1", )PY" << kWsPort << R"PY():
        await asyncio.Future()

asyncio.run(main())
)PY";
    }
    const long p = fork();
    if (p < 0) return false;
    if (p == 0) {
        std::freopen("/dev/null", "w", stdout);
        std::freopen("/dev/null", "w", stderr);
        execlp("python3", "python3", g_wsScript.c_str(), nullptr);
        _exit(127);
    }
    g_wsPid = p;
    // TCP-connect probe for readiness.
    for (int i = 0; i < 50; ++i) {
        UltraNetSocketOptions o; o.connectTimeoutMs = 100;
        UltraNetHandle h = UltraNet_TcpConnect("127.0.0.1", kWsPort, o);
        if (h != UltraNetInvalidHandle) {
            UltraNet_SocketClose(h);
            g_wsStarted = true;
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return false;
}

void StopWsServer() {
    std::lock_guard<std::mutex> lk(g_wsMutex);
    if (g_wsPid > 0) {
        kill(static_cast<pid_t>(g_wsPid), SIGTERM);
        int s; waitpid(static_cast<pid_t>(g_wsPid), &s, 0);
        g_wsPid = -1;
    }
    if (!g_wsScript.empty()) {
        std::error_code ec;
        std::filesystem::remove_all(g_wsScript.parent_path(), ec);
    }
    g_wsStarted = false;
}

struct WsCleanup {
    ~WsCleanup() { StopWsServer(); }
};
WsCleanup g_cleanup;

std::string EchoUrl() {
    return "ws://127.0.0.1:" + std::to_string(kWsPort) + "/echo";
}
std::string CloseUrl() {
    return "ws://127.0.0.1:" + std::to_string(kWsPort) + "/close";
}
#endif // !_WIN32

} // namespace

TEST(ws_loopback_connect_open_close) {
#if defined(_WIN32)
    SKIP("loopback WS server is POSIX-only");
#else
    UltraNet_Initialize();
    if (!StartWsServer()) SKIP("python3 + websockets package not available");

    // Probe libcurl WS support: a real connect against the live server. If
    // libcurl was built without --enable-websockets, Connect returns
    // InvalidHandle synchronously.
    UltraNetHandle h = UltraNet_WebSocketConnect(EchoUrl());
    if (h == UltraNetInvalidHandle) {
        SKIP("libcurl built without --enable-websockets");
    }
    CHECK(UltraNet_WebSocketIsOpen(h));
    REQUIRE_EQ(UltraNet_WebSocketGetState(h),
               UltraNetWebSocketState::Open);

    auto r = UltraNet_WebSocketClose(h, 1000, "test-done");
    CHECK(bool(r));
    CHECK(!UltraNet_WebSocketIsOpen(h));
#endif
}

TEST(ws_loopback_text_round_trip) {
#if defined(_WIN32)
    SKIP("loopback WS server is POSIX-only");
#else
    if (!StartWsServer()) SKIP("python3 + websockets package not available");

    std::mutex mu;
    std::vector<std::string> received;
    std::atomic<int> closeFires{0};

    UltraNetWebSocketCallbacks cb;
    cb.onText  = [&](UltraNetHandle, const std::string& m) {
        std::lock_guard<std::mutex> lk(mu);
        received.push_back(m);
    };
    cb.onClose = [&](UltraNetHandle, int, const std::string&) { ++closeFires; };
    auto prev = UltraNet_WebSocketSetCallbacks(cb);

    UltraNetHandle h = UltraNet_WebSocketConnect(EchoUrl());
    if (h == UltraNetInvalidHandle) {
        UltraNet_WebSocketSetCallbacks(prev);
        SKIP("libcurl built without --enable-websockets");
    }

    auto sent = UltraNet_WebSocketSendText(h, "hello-ultranet");
    REQUIRE(bool(sent));

    // Wait up to 2s for the echo to come back.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        {
            std::lock_guard<std::mutex> lk(mu);
            if (!received.empty()) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    UltraNet_WebSocketClose(h, 1000);
    UltraNet_WebSocketSetCallbacks(prev);

    std::lock_guard<std::mutex> lk(mu);
    REQUIRE_EQ(received.size(), static_cast<std::size_t>(1));
    REQUIRE_EQ(received[0], std::string{"hello-ultranet"});
#endif
}

TEST(ws_loopback_binary_round_trip) {
#if defined(_WIN32)
    SKIP("loopback WS server is POSIX-only");
#else
    if (!StartWsServer()) SKIP("python3 + websockets package not available");

    std::mutex mu;
    std::vector<std::vector<uint8_t>> received;

    UltraNetWebSocketCallbacks cb;
    cb.onBinary = [&](UltraNetHandle, const std::vector<uint8_t>& d) {
        std::lock_guard<std::mutex> lk(mu);
        received.push_back(d);
    };
    auto prev = UltraNet_WebSocketSetCallbacks(cb);

    UltraNetHandle h = UltraNet_WebSocketConnect(EchoUrl());
    if (h == UltraNetInvalidHandle) {
        UltraNet_WebSocketSetCallbacks(prev);
        SKIP("libcurl built without --enable-websockets");
    }
    std::vector<uint8_t> payload = {0x01, 0x02, 0xff, 0x00, 0x7e, 0x80};
    auto sent = UltraNet_WebSocketSendBinary(h, payload);
    REQUIRE(bool(sent));

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        {
            std::lock_guard<std::mutex> lk(mu);
            if (!received.empty()) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    UltraNet_WebSocketClose(h, 1000);
    UltraNet_WebSocketSetCallbacks(prev);

    std::lock_guard<std::mutex> lk(mu);
    REQUIRE_EQ(received.size(), static_cast<std::size_t>(1));
    REQUIRE_EQ(received[0].size(), payload.size());
    for (std::size_t i = 0; i < payload.size(); ++i) {
        REQUIRE_EQ(received[0][i], payload[i]);
    }
#endif
}

TEST(ws_loopback_server_initiated_close_fires_callback) {
#if defined(_WIN32)
    SKIP("loopback WS server is POSIX-only");
#else
    if (!StartWsServer()) SKIP("python3 + websockets package not available");

    std::atomic<int> closeCode{-1};
    std::mutex mu; std::string closeReason;
    std::atomic<bool> closed{false};

    UltraNetWebSocketCallbacks cb;
    cb.onClose = [&](UltraNetHandle, int code, const std::string& reason) {
        closeCode = code;
        { std::lock_guard<std::mutex> lk(mu); closeReason = reason; }
        closed = true;
    };
    auto prev = UltraNet_WebSocketSetCallbacks(cb);

    UltraNetHandle h = UltraNet_WebSocketConnect(CloseUrl());
    if (h == UltraNetInvalidHandle) {
        UltraNet_WebSocketSetCallbacks(prev);
        SKIP("libcurl built without --enable-websockets");
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!closed.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    UltraNet_WebSocketSetCallbacks(prev);

    REQUIRE(closed.load());
    REQUIRE_EQ(closeCode.load(), 1000);
    std::lock_guard<std::mutex> lk(mu);
    REQUIRE_EQ(closeReason, std::string{"bye"});
#endif
}

TEST(ws_loopback_invalid_handle_send_fails_cleanly) {
    auto r = UltraNet_WebSocketSendText(UltraNetInvalidHandle, "x");
    CHECK(!bool(r));
    REQUIRE_EQ(r.code, UltraNetResultCode::InvalidHandle);
}
