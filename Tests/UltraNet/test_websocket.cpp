// Tests/UltraNet/test_websocket.cpp
// Loopback WebSocket tests against a Python `websockets`-package server
// spawned by the test binary. Exercises the parts of UltraNetWebSocket.cpp
// that aren't covered by the existing "unsupported by libcurl" runtime
// detection: receiver thread, send mutex, text + binary frame round-trips,
// IsOpen / GetState transitions, and clean close handshake - and keeping a
// connection alive: the server's pings answered (a server that closes after
// half a second without a pong), the client's own pings
// (pingIntervalMs), a peer that stops answering taken as gone, and sends
// from several threads while the receiver reads (one libcurl handle).
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
#include <functional>
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

# A peer that completes the handshake and then never says anything again -
# no pong, no data: a connection that died without a close.
async def silent(reader, writer):
    import base64, hashlib
    request = await reader.readuntil(b"\r\n\r\n")
    key = ""
    for line in request.decode().split("\r\n"):
        if line.lower().startswith("sec-websocket-key:"):
            key = line.split(":", 1)[1].strip()
    accept = base64.b64encode(hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest()).decode()
    writer.write(("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                  "Sec-WebSocket-Accept: " + accept + "\r\n\r\n").encode())
    await writer.drain()
    while await reader.read(4096):
        pass

async def main():
    # /echo as above; on the next port the same, but pinging every half
    # second and closing a client that has not answered within another half
    # - what uvicorn does with 20 s each; on the one after, the silent peer.
    async with serve(handler, "127.0.0.1", )PY" << kWsPort << R"PY():
        async with serve(handler, "127.0.0.1", )PY" << kWsPort + 1 << R"PY(, ping_interval=0.5, ping_timeout=0.5):
            server = await asyncio.start_server(silent, "127.0.0.1", )PY" << kWsPort + 2 << R"PY()
            async with server:
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
std::string StrictUrl() {
    return "ws://127.0.0.1:" + std::to_string(kWsPort + 1) + "/echo";
}

std::string SilentUrl() {
    return "ws://127.0.0.1:" + std::to_string(kWsPort + 2) + "/echo";
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

// A callback closing its own socket runs on the receiver thread. Close used
// to join that thread from itself: std::system_error out of the receiver,
// std::terminate.
TEST(ws_loopback_close_from_own_callback) {
#if defined(_WIN32)
    SKIP("loopback WS server is POSIX-only");
#else
    if (!StartWsServer()) SKIP("python3 + websockets package not available");

    std::atomic<bool> closedInCallback{false};
    std::atomic<bool> closeOk{false};
    UltraNetWebSocketCallbacks cb;
    cb.onText = [&](UltraNetHandle h, const std::string&) {
        closeOk = bool(UltraNet_WebSocketClose(h, 1000, "from-callback"));
        closedInCallback = true;
    };
    auto prev = UltraNet_WebSocketSetCallbacks(cb);

    UltraNetHandle h = UltraNet_WebSocketConnect(EchoUrl());
    if (h == UltraNetInvalidHandle) {
        UltraNet_WebSocketSetCallbacks(prev);
        SKIP("libcurl built without --enable-websockets");
    }
    REQUIRE(bool(UltraNet_WebSocketSendText(h, "close-me")));

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!closedInCallback.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    UltraNet_WebSocketSetCallbacks(prev);

    REQUIRE(closedInCallback.load());
    CHECK(closeOk.load());
    CHECK(!UltraNet_WebSocketIsOpen(h));
    REQUIRE_EQ(UltraNet_WebSocketGetState(h), UltraNetWebSocketState::Closed);
#endif
}

// UltraNet_Shutdown with a socket still open must stop its receiver before
// curl_global_cleanup; it used to leave the receiver running on libcurl.
TEST(ws_loopback_shutdown_closes_open_sockets) {
#if defined(_WIN32)
    SKIP("loopback WS server is POSIX-only");
#else
    if (!StartWsServer()) SKIP("python3 + websockets package not available");
    UltraNet_Initialize();

    UltraNetHandle h = UltraNet_WebSocketConnect(EchoUrl());
    if (h == UltraNetInvalidHandle) SKIP("libcurl built without --enable-websockets");
    REQUIRE(UltraNet_WebSocketIsOpen(h));

    UltraNet_Shutdown();
    CHECK(!UltraNet_WebSocketIsOpen(h));
    REQUIRE_EQ(UltraNet_WebSocketGetState(h), UltraNetWebSocketState::Closed);

    UltraNet_Initialize();   // the tests after this one expect UltraNet up
#endif
}

TEST(ws_loopback_invalid_handle_send_fails_cleanly) {
    auto r = UltraNet_WebSocketSendText(UltraNetInvalidHandle, "x");
    CHECK(!bool(r));
    REQUIRE_EQ(r.code, UltraNetResultCode::InvalidHandle);
}

// ---- keeping a connection alive ---------------------------------------------------

#if !defined(_WIN32)
namespace {
// What happened to one connection, from the global callbacks.
struct WsWatch {
    UltraNetHandle handle = UltraNetInvalidHandle;
    std::mutex mu;
    std::vector<std::string> texts;
    std::atomic<int> pongs{0};
    std::atomic<int> closes{0};
    std::atomic<int> errors{0};
    UltraNetWebSocketCallbacks previous;

    void Install() {
        UltraNetWebSocketCallbacks cb;
        cb.onText = [this](UltraNetHandle h, const std::string& m) {
            if (h != handle) return;
            std::lock_guard<std::mutex> lk(mu);
            texts.push_back(m);
        };
        cb.onPong = [this](UltraNetHandle h, const std::vector<uint8_t>&) { if (h == handle) ++pongs; };
        cb.onClose = [this](UltraNetHandle h, int, const std::string&) { if (h == handle) ++closes; };
        cb.onError = [this](UltraNetHandle h, const std::string&) { if (h == handle) ++errors; };
        previous = UltraNet_WebSocketSetCallbacks(cb);
    }
    void Uninstall() { UltraNet_WebSocketSetCallbacks(previous); }
    std::size_t Count() {
        std::lock_guard<std::mutex> lk(mu);
        return texts.size();
    }
};

bool WaitFor(const std::function<bool()>& done, int ms) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return done();
}
} // namespace
#endif

TEST(ws_answers_the_servers_pings) {
#if defined(_WIN32)
    SKIP("loopback WS server is POSIX-only");
#else
    if (!StartWsServer()) SKIP("python3 + websockets package not available");
    WsWatch watch;
    watch.Install();
    watch.handle = UltraNet_WebSocketConnect(StrictUrl());
    if (watch.handle == UltraNetInvalidHandle) {
        watch.Uninstall();
        SKIP("libcurl built without --enable-websockets");
    }
    // Idle for three seconds: six pings, each closing the connection after
    // half a second unanswered. Before 0.4.0 nothing answered them.
    std::this_thread::sleep_for(std::chrono::seconds(3));
    CHECK(UltraNet_WebSocketIsOpen(watch.handle));
    CHECK(watch.closes.load() == 0 && watch.errors.load() == 0);
    // And it still carries messages.
    CHECK(bool(UltraNet_WebSocketSendText(watch.handle, "still-here")));
    CHECK(WaitFor([&]() { return watch.Count() == 1; }, 2000));
    UltraNet_WebSocketClose(watch.handle, 1000);
    watch.Uninstall();
#endif
}

TEST(ws_pings_at_its_interval) {
#if defined(_WIN32)
    SKIP("loopback WS server is POSIX-only");
#else
    if (!StartWsServer()) SKIP("python3 + websockets package not available");
    WsWatch watch;
    watch.Install();
    UltraNetWebSocketOptions options;
    options.pingIntervalMs = 200;
    watch.handle = UltraNet_WebSocketConnect(EchoUrl(), options);
    if (watch.handle == UltraNetInvalidHandle) {
        watch.Uninstall();
        SKIP("libcurl built without --enable-websockets");
    }
    // The option was read by nothing before 0.4.0: no ping, so no pong.
    CHECK(WaitFor([&]() { return watch.pongs.load() >= 3; }, 3000));
    CHECK(UltraNet_WebSocketIsOpen(watch.handle));
    UltraNet_WebSocketClose(watch.handle, 1000);
    watch.Uninstall();
#endif
}

TEST(ws_takes_a_silent_peer_as_gone) {
#if defined(_WIN32)
    SKIP("loopback WS server is POSIX-only");
#else
    if (!StartWsServer()) SKIP("python3 + websockets package not available");
    WsWatch watch;
    watch.Install();
    UltraNetWebSocketOptions options;
    options.pingIntervalMs = 200;
    watch.handle = UltraNet_WebSocketConnect(SilentUrl(), options);
    if (watch.handle == UltraNetInvalidHandle) {
        watch.Uninstall();
        SKIP("libcurl built without --enable-websockets");
    }
    CHECK(UltraNet_WebSocketIsOpen(watch.handle));
    // No pong and nothing else for two intervals: gone, said once, so the
    // owner reconnects instead of waiting on a dead socket for good.
    CHECK(WaitFor([&]() { return watch.errors.load() > 0; }, 3000));
    CHECK(!UltraNet_WebSocketIsOpen(watch.handle));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    CHECK(watch.errors.load() == 1);
    UltraNet_WebSocketClose(watch.handle, 1000);
    watch.Uninstall();
#endif
}

TEST(ws_sends_from_many_threads_while_receiving) {
#if defined(_WIN32)
    SKIP("loopback WS server is POSIX-only");
#else
    if (!StartWsServer()) SKIP("python3 + websockets package not available");
    WsWatch watch;
    watch.Install();
    UltraNetWebSocketOptions options;
    options.pingIntervalMs = 100;
    watch.handle = UltraNet_WebSocketConnect(StrictUrl(), options);
    if (watch.handle == UltraNetInvalidHandle) {
        watch.Uninstall();
        SKIP("libcurl built without --enable-websockets");
    }
    // Four threads sending while the receiver reads the echoes and the
    // server's pings arrive: one libcurl handle, which is not thread-safe.
    constexpr int kThreads = 4, kEach = 100;
    std::atomic<int> sendFailures{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < kEach; ++i) {
                if (!UltraNet_WebSocketSendText(watch.handle, "t" + std::to_string(t) + "-" + std::to_string(i)))
                    ++sendFailures;
            }
        });
    }
    for (auto& thread : threads) thread.join();
    CHECK(sendFailures.load() == 0);
    CHECK(WaitFor([&]() { return watch.Count() == kThreads * kEach; }, 5000));
    CHECK(watch.Count() == static_cast<std::size_t>(kThreads * kEach));
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));      // idle a while, pinged
    CHECK(UltraNet_WebSocketIsOpen(watch.handle));
    CHECK(watch.closes.load() == 0 && watch.errors.load() == 0);
    UltraNet_WebSocketClose(watch.handle, 1000);
    watch.Uninstall();
#endif
}

TEST(ws_delivers_a_large_frame_whole) {
#if defined(_WIN32)
    SKIP("loopback WS server is POSIX-only");
#else
    if (!StartWsServer()) SKIP("python3 + websockets package not available");
    WsWatch watch;
    watch.Install();
    watch.handle = UltraNet_WebSocketConnect(EchoUrl());
    if (watch.handle == UltraNetInvalidHandle) {
        watch.Uninstall();
        SKIP("libcurl built without --enable-websockets");
    }
    // Larger than a read: libcurl hands one frame over in parts (bytesleft),
    // which are not fragments (CURLWS_CONT). Before 0.4.0 each part went out
    // as a message of its own - a relay's 40 KB envelope as five broken ones.
    std::string big(50000, 'x');
    for (std::size_t i = 0; i < big.size(); i += 997) big[i] = static_cast<char>('a' + (i % 26));
    CHECK(bool(UltraNet_WebSocketSendText(watch.handle, big)));
    CHECK(bool(UltraNet_WebSocketSendText(watch.handle, "small-after")));
    CHECK(WaitFor([&]() { return watch.Count() >= 2; }, 3000));
    {
        std::lock_guard<std::mutex> lk(watch.mu);
        REQUIRE_EQ(watch.texts.size(), static_cast<std::size_t>(2));
        CHECK(watch.texts[0] == big);
        CHECK(watch.texts[1] == "small-after");
    }
    UltraNet_WebSocketClose(watch.handle, 1000);
    watch.Uninstall();
#endif
}

TEST(ws_refuses_a_message_over_its_limit) {
#if defined(_WIN32)
    SKIP("loopback WS server is POSIX-only");
#else
    if (!StartWsServer()) SKIP("python3 + websockets package not available");
    WsWatch watch;
    watch.Install();
    UltraNetWebSocketOptions options;
    options.maxMessageBytes = 10000;
    watch.handle = UltraNet_WebSocketConnect(EchoUrl(), options);
    if (watch.handle == UltraNetInvalidHandle) {
        watch.Uninstall();
        SKIP("libcurl built without --enable-websockets");
    }
    // Under the limit: delivered.
    CHECK(bool(UltraNet_WebSocketSendText(watch.handle, std::string(5000, 'a'))));
    CHECK(WaitFor([&]() { return watch.Count() == 1; }, 2000));
    // Over it: not assembled - the connection ends (1009, message too big),
    // said once, and the message never reaches onText.
    CHECK(bool(UltraNet_WebSocketSendText(watch.handle, std::string(50000, 'b'))));
    CHECK(WaitFor([&]() { return watch.errors.load() > 0; }, 3000));
    CHECK(!UltraNet_WebSocketIsOpen(watch.handle));
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK(watch.errors.load() == 1);
    CHECK(watch.Count() == 1);
    UltraNet_WebSocketClose(watch.handle, 1000);
    watch.Uninstall();
#endif
}
