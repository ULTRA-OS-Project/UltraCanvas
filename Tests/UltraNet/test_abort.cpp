// Tests/UltraNet/test_abort.cpp
// Calling a running transfer off from outside it (UltraNet_SetThreadAbortCheck).
// UltraFiler's drive worker runs an upload on its own thread; when the window
// is closed and the user chooses to quit, the worker has to give the thread
// back now - not when a large upload has finished, minutes later, with no
// window left on screen. Both transfers here go to a socket that listens and
// never answers, so only the abort check can end them before their timeouts.
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetFtp.h>
#include <UltraNet/UltraNetHttp.h>
#include <UltraNet/UltraNetSocket.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include "../../UltraCanvas/include/UltraCanvasPathUtf8.h"

namespace {

// A port that takes connections (the system's backlog accepts them) and
// never says a word.
struct SilentServer {
    UltraNetHandle listener = UltraNetInvalidHandle;
    int port = 0;
    SilentServer() {
        UltraNetSocketOptions options;
        options.bindAddress  = "127.0.0.1";
        options.reuseAddress = true;
        listener = UltraNet_TcpListen(0, options);
        UltraNetEndpoint local;
        if (listener != UltraNetInvalidHandle && UltraNet_SocketLocalEndpoint(listener, local))
            port = local.port;
    }
    ~SilentServer() { if (listener != UltraNetInvalidHandle) UltraNet_SocketClose(listener); }
};

// The calling thread's abort check for as long as it lives: says "stop" once
// `after` has passed.
struct AbortAfter {
    std::function<bool()> previous;
    explicit AbortAfter(std::chrono::milliseconds after) {
        const auto deadline = std::chrono::steady_clock::now() + after;
        previous = UltraNet_SetThreadAbortCheck(
                [deadline] { return std::chrono::steady_clock::now() >= deadline; });
    }
    ~AbortAfter() { UltraNet_SetThreadAbortCheck(std::move(previous)); }
};

double SecondsSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

} // namespace

TEST(abort_check_ends_a_waiting_http_request_as_cancelled) {
    UltraNet_Initialize();
    SilentServer server;
    REQUIRE(server.port > 0);
    UltraNetHttpOptions options = UltraNetHttpOptions::Default();
    options.timeoutMs = 30000;
    const auto start = std::chrono::steady_clock::now();
    UltraNetResponse response;
    UltraNetResult r;
    {
        AbortAfter abort(std::chrono::milliseconds(300));
        r = UltraNet_HttpGet("http://127.0.0.1:" + std::to_string(server.port) + "/big", response,
                             options);
    }
    REQUIRE(!r);
    REQUIRE(r.code == UltraNetResultCode::Cancelled);
    REQUIRE(SecondsSince(start) < 5.0);
}

TEST(abort_check_ends_a_waiting_ftp_upload_as_cancelled) {
    UltraNet_Initialize();
    SilentServer server;
    REQUIRE(server.port > 0);
    const std::filesystem::path local =
            std::filesystem::temp_directory_path() / "ultranet-abort-upload.bin";
    std::ofstream(local, std::ios::binary) << std::string(1 << 20, 'x');
    UltraNetFtpOptions options = UltraNetFtpOptions::Default();
    options.connectTimeoutMs = 30000;
    options.inactivityTimeoutMs = 30000;
    const auto start = std::chrono::steady_clock::now();
    UltraNetResult r;
    {
        AbortAfter abort(std::chrono::milliseconds(300));
        r = UltraNet_FtpUpload(UltraCanvas::PathToUtf8(local),
                               "ftp://127.0.0.1:" + std::to_string(server.port) + "/up.bin",
                               options);
    }
    std::error_code ec;
    std::filesystem::remove(local, ec);
    REQUIRE(!r);
    REQUIRE(r.code == UltraNetResultCode::Cancelled);
    REQUIRE(SecondsSince(start) < 5.0);
}

TEST(abort_check_that_never_fires_changes_nothing) {
    // The check is asked and says no: the request ends the way it always
    // did - here, its own timeout.
    UltraNet_Initialize();
    SilentServer server;
    REQUIRE(server.port > 0);
    UltraNetHttpOptions options = UltraNetHttpOptions::Default();
    options.timeoutMs = 1500;
    std::function<bool()> previous = UltraNet_SetThreadAbortCheck([] { return false; });
    UltraNetResponse response;
    const UltraNetResult r = UltraNet_HttpGet(
            "http://127.0.0.1:" + std::to_string(server.port) + "/", response, options);
    UltraNet_SetThreadAbortCheck(std::move(previous));
    REQUIRE(!r);
    REQUIRE(r.code != UltraNetResultCode::Cancelled);
    REQUIRE(!UltraNet_ThreadAbortRequested());
}
