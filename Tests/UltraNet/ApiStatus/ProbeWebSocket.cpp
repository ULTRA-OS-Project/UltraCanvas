// Tests/UltraNet/ApiStatus/ProbeWebSocket.cpp
// Probes for UltraNet/UltraNetWebSocket.h against the in-process WebSocket
// echo endpoint (/ws) on the loopback origin — no Python `websockets` package
// and no external service required.
//
// WebSocket support is a property of the linked libcurl (curl >= 7.86 built
// with --enable-websockets). When it is missing, UltraNet_WebSocketConnect
// says so through onError and every probe here reports NOT IMPLEMENTED with
// that reason rather than pretending the surface is merely unverified.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "ApiStatus.h"
#include "ProbeServer.h"

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetWebSocket.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>

using namespace ultranet_apistatus;

namespace {

constexpr const char* kArea = "WebSocket";

// Collects everything the global callback bag reports, with a condition
// variable so probes can wait for a specific event instead of sleeping.
struct Sink {
    std::mutex              mutex;
    std::condition_variable cv;

    int                     openCount = 0;
    std::vector<std::string> texts;
    std::vector<std::vector<uint8_t>> binaries;
    std::vector<std::vector<uint8_t>> pongs;
    bool                    closed = false;
    int                     closeCode = 0;
    std::string             closeReason;
    std::string             lastError;

    UltraNetWebSocketCallbacks Bag() {
        UltraNetWebSocketCallbacks cb;
        cb.onOpen = [this](UltraNetHandle) {
            std::lock_guard<std::mutex> lk(mutex); ++openCount; cv.notify_all();
        };
        cb.onText = [this](UltraNetHandle, const std::string& t) {
            std::lock_guard<std::mutex> lk(mutex); texts.push_back(t); cv.notify_all();
        };
        cb.onBinary = [this](UltraNetHandle, const std::vector<uint8_t>& b) {
            std::lock_guard<std::mutex> lk(mutex); binaries.push_back(b); cv.notify_all();
        };
        cb.onPong = [this](UltraNetHandle, const std::vector<uint8_t>& p) {
            std::lock_guard<std::mutex> lk(mutex); pongs.push_back(p); cv.notify_all();
        };
        cb.onClose = [this](UltraNetHandle, int code, const std::string& reason) {
            std::lock_guard<std::mutex> lk(mutex);
            closed = true; closeCode = code; closeReason = reason; cv.notify_all();
        };
        cb.onError = [this](UltraNetHandle, const std::string& e) {
            std::lock_guard<std::mutex> lk(mutex); lastError = e; cv.notify_all();
        };
        return cb;
    }

    template <typename Pred>
    bool WaitFor(Pred pred, int ms = 5000) {
        std::unique_lock<std::mutex> lk(mutex);
        return cv.wait_for(lk, std::chrono::milliseconds(ms), pred);
    }
};

// Installs a sink for the duration of a probe and restores the previous bag.
struct CallbackScope {
    UltraNetWebSocketCallbacks previous;
    explicit CallbackScope(Sink& sink) {
        previous = UltraNet_WebSocketSetCallbacks(sink.Bag());
    }
    ~CallbackScope() { UltraNet_WebSocketSetCallbacks(previous); }
    CallbackScope(const CallbackScope&) = delete;
    CallbackScope& operator=(const CallbackScope&) = delete;
};

// Closes the connection when the probe returns, however it returns.
struct ConnectionGuard {
    UltraNetHandle handle = UltraNetInvalidHandle;
    ~ConnectionGuard() {
        if (handle != UltraNetInvalidHandle) UltraNet_WebSocketClose(handle);
    }
};

bool ErrorMeansNoWsSupport(const std::string& error) {
    return error.find("without WebSocket support") != std::string::npos;
}

// Shared preamble: needs an origin, needs libcurl WebSockets. Returns an
// Outcome to hand straight back when the connection could not be made.
bool Connect(Sink& sink, ConnectionGuard& conn, Outcome& outFailure) {
    LoopbackServer& server = Loopback();
    if (!server.Running()) {
        outFailure = Implemented("no loopback origin to connect to: " + server.Error());
        return false;
    }
    conn.handle = UltraNet_WebSocketConnect(server.WsUrl("/ws"));
    if (conn.handle == UltraNetInvalidHandle) {
        std::string error;
        {
            std::lock_guard<std::mutex> lk(sink.mutex);
            error = sink.lastError;
        }
        if (ErrorMeansNoWsSupport(error)) {
            outFailure = NotImplemented(
                "inert in this build: UltraNet_WebSocketConnect refuses because "
                "curl_version_info() does not list ws/wss in its protocols. "
                "NOTE: some libcurl builds (e.g. curl 8.5, where WebSocket was "
                "still experimental) export curl_ws_send/curl_ws_recv without "
                "advertising the protocol, so the CMake symbol probe and this "
                "runtime probe can disagree");
        } else {
            outFailure = Implemented("connect to the loopback /ws endpoint "
                                     "failed (" + (error.empty() ? "no error reported"
                                                                 : error) + ")");
        }
        return false;
    }
    return true;
}

} // namespace

ULTRANET_PROBE(kArea, UltraNet_WebSocketConnect) {
    PROBE_EXPECT(UltraNet_WebSocketConnect("") == UltraNetInvalidHandle);

    Sink sink;
    CallbackScope scope(sink);
    ConnectionGuard conn;
    Outcome failure;
    if (!Connect(sink, conn, failure)) return failure;

    PROBE_EXPECT_MSG(sink.WaitFor([&] { return sink.openCount > 0; }),
                     "onOpen never fired after a successful connect");
    PROBE_EXPECT(UltraNet_WebSocketGetState(conn.handle) == UltraNetWebSocketState::Open);
    return Working("upgrade handshake completed against the loopback echo "
                   "endpoint; onOpen fired; empty URL rejected");
}

ULTRANET_PROBE(kArea, UltraNet_WebSocketSendText) {
    UltraNetResult bad = UltraNet_WebSocketSendText(UltraNetInvalidHandle, "x");
    PROBE_EXPECT(!bad && bad.code == UltraNetResultCode::InvalidHandle);

    Sink sink;
    CallbackScope scope(sink);
    ConnectionGuard conn;
    Outcome failure;
    if (!Connect(sink, conn, failure)) return failure;

    const std::string message = R"({"probe":"text","utf8":"äöü"})";
    const UltraNetResult r = UltraNet_WebSocketSendText(conn.handle, message);
    PROBE_EXPECT_MSG(static_cast<bool>(r), r.message);
    PROBE_EXPECT_MSG(sink.WaitFor([&] { return !sink.texts.empty(); }),
                     "the echoed text frame never arrived");
    {
        std::lock_guard<std::mutex> lk(sink.mutex);
        PROBE_EXPECT(sink.texts[0] == message);
    }
    return Working("text frame round-tripped through the echo endpoint "
                   "(UTF-8 preserved); invalid handle rejected");
}

ULTRANET_PROBE(kArea, UltraNet_WebSocketSendBinary) {
    UltraNetResult bad = UltraNet_WebSocketSendBinary(UltraNetInvalidHandle, {1, 2, 3});
    PROBE_EXPECT(!bad && bad.code == UltraNetResultCode::InvalidHandle);

    Sink sink;
    CallbackScope scope(sink);
    ConnectionGuard conn;
    Outcome failure;
    if (!Connect(sink, conn, failure)) return failure;

    std::vector<uint8_t> payload(1024);
    for (std::size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<uint8_t>(i & 0xff);
    }
    const UltraNetResult r = UltraNet_WebSocketSendBinary(conn.handle, payload);
    PROBE_EXPECT_MSG(static_cast<bool>(r), r.message);
    PROBE_EXPECT_MSG(sink.WaitFor([&] { return !sink.binaries.empty(); }),
                     "the echoed binary frame never arrived");
    {
        std::lock_guard<std::mutex> lk(sink.mutex);
        PROBE_EXPECT(sink.binaries[0] == payload);
    }
    return Working("1 KiB binary frame round-tripped byte-for-byte; invalid "
                   "handle rejected");
}

ULTRANET_PROBE(kArea, UltraNet_WebSocketPing) {
    UltraNetResult bad = UltraNet_WebSocketPing(UltraNetInvalidHandle, {});
    PROBE_EXPECT(!bad && bad.code == UltraNetResultCode::InvalidHandle);

    Sink sink;
    CallbackScope scope(sink);
    ConnectionGuard conn;
    Outcome failure;
    if (!Connect(sink, conn, failure)) return failure;

    const std::vector<uint8_t> payload = {'p', 'i', 'n', 'g'};
    const UltraNetResult r = UltraNet_WebSocketPing(conn.handle, payload);
    PROBE_EXPECT_MSG(static_cast<bool>(r), r.message);
    if (!sink.WaitFor([&] { return !sink.pongs.empty(); })) {
        return Implemented("ping frame was sent without error, but no pong "
                           "reached onPong within 5s");
    }
    {
        std::lock_guard<std::mutex> lk(sink.mutex);
        PROBE_EXPECT(sink.pongs[0] == payload);
    }
    return Working("ping sent and the server's pong delivered to onPong with "
                   "the payload intact");
}

ULTRANET_PROBE(kArea, UltraNet_WebSocketClose) {
    UltraNetResult bad = UltraNet_WebSocketClose(UltraNetInvalidHandle);
    PROBE_EXPECT(!bad && bad.code == UltraNetResultCode::InvalidHandle);

    Sink sink;
    CallbackScope scope(sink);
    ConnectionGuard conn;
    Outcome failure;
    if (!Connect(sink, conn, failure)) return failure;

    const UltraNetHandle handle = conn.handle;
    const UltraNetResult r = UltraNet_WebSocketClose(handle, 1000, "probe done");
    conn.handle = UltraNetInvalidHandle;   // ownership transferred to Close
    PROBE_EXPECT_MSG(static_cast<bool>(r), r.message);

    // After Close the handle is retired: state reads Closed and sends fail.
    PROBE_EXPECT(UltraNet_WebSocketGetState(handle) == UltraNetWebSocketState::Closed);
    PROBE_EXPECT(!UltraNet_WebSocketIsOpen(handle));
    const UltraNetResult afterClose = UltraNet_WebSocketSendText(handle, "too late");
    PROBE_EXPECT(!afterClose && afterClose.code == UltraNetResultCode::InvalidHandle);
    return Working("close frame sent, receiver thread joined, handle retired "
                   "(later sends report InvalidHandle)");
}

ULTRANET_PROBE(kArea, UltraNet_WebSocketIsOpen) {
    PROBE_EXPECT(!UltraNet_WebSocketIsOpen(UltraNetInvalidHandle));

    Sink sink;
    CallbackScope scope(sink);
    ConnectionGuard conn;
    Outcome failure;
    if (!Connect(sink, conn, failure)) return failure;

    PROBE_EXPECT(UltraNet_WebSocketIsOpen(conn.handle));
    const UltraNetHandle handle = conn.handle;
    UltraNet_WebSocketClose(handle);
    conn.handle = UltraNetInvalidHandle;
    PROBE_EXPECT(!UltraNet_WebSocketIsOpen(handle));
    return Working("true while connected, false for a closed and for an "
                   "invalid handle");
}

ULTRANET_PROBE(kArea, UltraNet_WebSocketGetState) {
    PROBE_EXPECT(UltraNet_WebSocketGetState(UltraNetInvalidHandle) ==
                 UltraNetWebSocketState::Closed);

    Sink sink;
    CallbackScope scope(sink);
    ConnectionGuard conn;
    Outcome failure;
    if (!Connect(sink, conn, failure)) return failure;

    PROBE_EXPECT(UltraNet_WebSocketGetState(conn.handle) == UltraNetWebSocketState::Open);
    const UltraNetHandle handle = conn.handle;
    UltraNet_WebSocketClose(handle);
    conn.handle = UltraNetInvalidHandle;
    PROBE_EXPECT(UltraNet_WebSocketGetState(handle) == UltraNetWebSocketState::Closed);
    return Working("Open while connected, Closed after close and for unknown "
                   "handles");
}

ULTRANET_PROBE(kArea, UltraNet_WebSocketSetCallbacks) {
    UltraNetWebSocketCallbacks first;
    first.onText = [](UltraNetHandle, const std::string&) {};
    const UltraNetWebSocketCallbacks original = UltraNet_WebSocketSetCallbacks(first);

    UltraNetWebSocketCallbacks second;
    second.onBinary = [](UltraNetHandle, const std::vector<uint8_t>&) {};
    const UltraNetWebSocketCallbacks returned = UltraNet_WebSocketSetCallbacks(second);

    // The returned bag must be the one installed immediately before.
    const bool returnedFirst = static_cast<bool>(returned.onText) &&
                               !returned.onBinary;

    const UltraNetWebSocketCallbacks live = UltraNet_WebSocketGetCallbacks();
    const bool secondIsLive = static_cast<bool>(live.onBinary) && !live.onText;

    UltraNet_WebSocketSetCallbacks(original);

    PROBE_EXPECT(returnedFirst);
    PROBE_EXPECT(secondIsLive);
    return Working("installs the new bag and returns the previous one");
}

ULTRANET_PROBE(kArea, UltraNet_WebSocketGetCallbacks) {
    const UltraNetWebSocketCallbacks original = UltraNet_WebSocketGetCallbacks();

    UltraNetWebSocketCallbacks cb;
    cb.onOpen  = [](UltraNetHandle) {};
    cb.onClose = [](UltraNetHandle, int, const std::string&) {};
    UltraNet_WebSocketSetCallbacks(cb);

    const UltraNetWebSocketCallbacks readBack = UltraNet_WebSocketGetCallbacks();
    UltraNet_WebSocketSetCallbacks(original);

    PROBE_EXPECT(static_cast<bool>(readBack.onOpen));
    PROBE_EXPECT(static_cast<bool>(readBack.onClose));
    PROBE_EXPECT(!readBack.onText);
    return Working("returns a snapshot of the currently installed bag");
}
