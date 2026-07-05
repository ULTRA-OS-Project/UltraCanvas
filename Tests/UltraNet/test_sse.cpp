// Tests/UltraNet/test_sse.cpp
// SSE parser unit tests + a loopback streaming test that hits a Python
// SSE fixture spawned by the test binary.
#include "test_framework.h"

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetHttp.h>
#include <UltraNet/UltraNetSocket.h>
#include <UltraNet/UltraNetSse.h>

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

std::vector<uint8_t> Bytes(const char* s) {
    return std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(s),
                                reinterpret_cast<const uint8_t*>(s) + std::strlen(s));
}

std::vector<UltraNetSseEvent> Collect(UltraNetSseParser& p, const char* s) {
    std::vector<UltraNetSseEvent> events;
    p.Feed(Bytes(s),
           [&](const UltraNetSseEvent& e) { events.push_back(e); });
    return events;
}

} // namespace

// ===== parser unit tests =====
TEST(sse_parse_single_event) {
    UltraNetSseParser p;
    auto events = Collect(p, "data: hello\n\n");
    REQUIRE_EQ(events.size(), static_cast<std::size_t>(1));
    REQUIRE_EQ(events[0].data, std::string{"hello"});
    CHECK(events[0].event.empty());
}

TEST(sse_parse_multiline_data) {
    UltraNetSseParser p;
    auto events = Collect(p, "data: line1\ndata: line2\ndata: line3\n\n");
    REQUIRE_EQ(events.size(), static_cast<std::size_t>(1));
    REQUIRE_EQ(events[0].data, std::string{"line1\nline2\nline3"});
}

TEST(sse_parse_event_type) {
    UltraNetSseParser p;
    auto events = Collect(p, "event: token\ndata: \"hi\"\n\n");
    REQUIRE_EQ(events.size(), static_cast<std::size_t>(1));
    REQUIRE_EQ(events[0].event, std::string{"token"});
    REQUIRE_EQ(events[0].data, std::string{"\"hi\""});
}

TEST(sse_parse_id_and_retry) {
    UltraNetSseParser p;
    auto events = Collect(p, "id: 42\nretry: 5000\ndata: x\n\n");
    REQUIRE_EQ(events.size(), static_cast<std::size_t>(1));
    REQUIRE_EQ(events[0].id, std::string{"42"});
    REQUIRE_EQ(events[0].retry, 5000);
}

TEST(sse_parse_comments_ignored) {
    UltraNetSseParser p;
    auto events = Collect(p, ": this is a comment\ndata: real\n\n");
    REQUIRE_EQ(events.size(), static_cast<std::size_t>(1));
    REQUIRE_EQ(events[0].data, std::string{"real"});
}

TEST(sse_parse_crlf_line_endings) {
    UltraNetSseParser p;
    auto events = Collect(p, "data: one\r\ndata: two\r\n\r\n");
    REQUIRE_EQ(events.size(), static_cast<std::size_t>(1));
    REQUIRE_EQ(events[0].data, std::string{"one\ntwo"});
}

TEST(sse_parse_chunked_across_feeds) {
    UltraNetSseParser p;
    std::vector<UltraNetSseEvent> events;
    auto sink = [&](const UltraNetSseEvent& e) { events.push_back(e); };
    p.Feed(Bytes("data: hel"), sink);
    CHECK(events.empty());
    p.Feed(Bytes("lo\n"), sink);
    CHECK(events.empty());
    p.Feed(Bytes("\n"), sink);
    REQUIRE_EQ(events.size(), static_cast<std::size_t>(1));
    REQUIRE_EQ(events[0].data, std::string{"hello"});
}

TEST(sse_parse_multiple_events_in_one_chunk) {
    UltraNetSseParser p;
    auto events = Collect(p,
        "data: first\n\n"
        "data: second\n\n"
        "event: bye\ndata: third\n\n");
    REQUIRE_EQ(events.size(), static_cast<std::size_t>(3));
    REQUIRE_EQ(events[0].data, std::string{"first"});
    REQUIRE_EQ(events[1].data, std::string{"second"});
    REQUIRE_EQ(events[2].event, std::string{"bye"});
    REQUIRE_EQ(events[2].data,  std::string{"third"});
}

TEST(sse_parse_leading_space_in_value_stripped) {
    UltraNetSseParser p;
    auto events = Collect(p, "data:no-space\n\ndata: with-space\n\n");
    REQUIRE_EQ(events.size(), static_cast<std::size_t>(2));
    REQUIRE_EQ(events[0].data, std::string{"no-space"});
    REQUIRE_EQ(events[1].data, std::string{"with-space"});
}

TEST(sse_parse_unknown_field_ignored) {
    UltraNetSseParser p;
    auto events = Collect(p, "unknown: foo\ndata: only\n\n");
    REQUIRE_EQ(events.size(), static_cast<std::size_t>(1));
    REQUIRE_EQ(events[0].data, std::string{"only"});
}

TEST(sse_flush_dispatches_pending_event_without_blank_line) {
    UltraNetSseParser p;
    std::vector<UltraNetSseEvent> events;
    auto sink = [&](const UltraNetSseEvent& e) { events.push_back(e); };
    p.Feed(Bytes("data: hanging\n"), sink);
    REQUIRE_EQ(events.size(), static_cast<std::size_t>(0));
    p.Flush(sink);
    REQUIRE_EQ(events.size(), static_cast<std::size_t>(1));
    REQUIRE_EQ(events[0].data, std::string{"hanging"});
}

// ===== loopback integration test =====
namespace {

#if !defined(_WIN32)
constexpr int kSsePort = 18532;
long g_ssePid = -1;
std::filesystem::path g_sseScript;
bool g_sseStarted = false;
std::mutex g_sseMutex;

bool StartSseServer() {
    std::lock_guard<std::mutex> lk(g_sseMutex);
    if (g_sseStarted) return true;
    if (std::system("command -v python3 > /dev/null 2>&1") != 0) return false;

    auto dir = std::filesystem::temp_directory_path() / "ultranet_sse_test";
    std::filesystem::create_directories(dir);
    g_sseScript = dir / "sse_server.py";
    {
        std::ofstream out(g_sseScript);
        out <<
R"PY(
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import time

class H(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.0'
    def log_message(self, *a, **k): pass
    def do_GET(self):
        self.send_response(200)
        self.send_header('Content-Type', 'text/event-stream')
        self.send_header('Cache-Control', 'no-cache')
        self.end_headers()
        try:
            for i in range(5):
                self.wfile.write(f"event: token\nid: {i}\ndata: chunk-{i}\n\n".encode())
                self.wfile.flush()
                time.sleep(0.02)
            self.wfile.write(b"event: done\ndata: bye\n\n")
            self.wfile.flush()
        except BrokenPipeError:
            pass

ThreadingHTTPServer(('127.0.0.1', )PY" << kSsePort << R"PY(), H).serve_forever()
)PY";
    }
    const long p = fork();
    if (p < 0) return false;
    if (p == 0) {
        std::freopen("/dev/null", "w", stdout);
        std::freopen("/dev/null", "w", stderr);
        execlp("python3", "python3", g_sseScript.c_str(), nullptr);
        _exit(127);
    }
    g_ssePid = p;
    // Probe for readiness via TCP connect — issuing an HTTP request here
    // would consume the SSE stream and confuse the test that follows.
    for (int i = 0; i < 30; ++i) {
        UltraNetSocketOptions o;
        o.connectTimeoutMs = 200;
        UltraNetHandle h = UltraNet_TcpConnect("127.0.0.1", kSsePort, o);
        if (h != UltraNetInvalidHandle) {
            UltraNet_SocketClose(h);
            g_sseStarted = true;
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return false;
}

void StopSseServer() {
    std::lock_guard<std::mutex> lk(g_sseMutex);
    if (g_ssePid > 0) {
        kill(static_cast<pid_t>(g_ssePid), SIGTERM);
        int s; waitpid(static_cast<pid_t>(g_ssePid), &s, 0);
        g_ssePid = -1;
    }
    if (!g_sseScript.empty()) {
        std::error_code ec;
        std::filesystem::remove_all(g_sseScript.parent_path(), ec);
    }
    g_sseStarted = false;
}

struct SseCleanup {
    ~SseCleanup() { StopSseServer(); }
};
SseCleanup g_cleanup;
#endif

} // namespace

TEST(sse_loopback_stream_receives_all_events) {
#if defined(_WIN32)
    SKIP("loopback SSE server is POSIX-only");
#else
    UltraNet_Initialize();
    if (!StartSseServer()) SKIP("python3 not available");

    std::vector<UltraNetSseEvent> events;
    auto r = UltraNet_SseStream(
        "http://127.0.0.1:" + std::to_string(kSsePort) + "/events",
        [&](const UltraNetSseEvent& e) { events.push_back(e); });

    REQUIRE(bool(r));
    REQUIRE_EQ(events.size(), static_cast<std::size_t>(6));
    REQUIRE_EQ(events[0].event, std::string{"token"});
    REQUIRE_EQ(events[0].data,  std::string{"chunk-0"});
    REQUIRE_EQ(events[0].id,    std::string{"0"});
    REQUIRE_EQ(events[5].event, std::string{"done"});
    REQUIRE_EQ(events[5].data,  std::string{"bye"});
#endif
}
