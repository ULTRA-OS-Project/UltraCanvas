// Tests/UltraNet/test_download.cpp
// The download features: a Range resume into a file (206, a server that
// ignores the range and answers 200, a file already complete answered 416),
// resumeFromOffset on a plain request, progress that counts the resumed
// bytes, https-only redirects, onHeaders before the body and as a veto, the
// stall timeout, and pause / resume of an async transfer.
//
// The origin is a small Python server spawned by the test binary (python3
// -c), because python3 -m http.server ignores Range requests. Tests SKIP
// when Python is not on PATH, and on Windows, where the harness does not
// fork.
#include "test_framework.h"

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetHttp.h>
#include <UltraNet/UltraNetSocket.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "../../UltraCanvas/include/UltraCanvasPathUtf8.h"

#if !defined(_WIN32)
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

constexpr int  kPort        = 18422;
constexpr int  kFileBytes   = 8192;
constexpr auto kStartTimeout = std::chrono::seconds(3);

// Routes:
//   /range.bin    8192-byte ramp (i & 0xff), Range honoured: 206 with
//                 Content-Range, 416 past the end
//   /norange.bin  the same bytes, Range ignored (always 200, whole body)
//   /to-http      302 to http://127.0.0.1:<port>/range.bin
//   /slow         16384 bytes in 256-byte pieces, one every 30 ms
//   /stall        headers, then nothing for 20 s
const char* kServer = R"PY(
import sys, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
PORT = int(sys.argv[1])
DATA = bytes(i & 0xff for i in range(8192))
class H(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'
    def log_message(self, *a): pass
    def send_bytes(self, body, status=200, extra=()):
        self.send_response(status)
        self.send_header('Content-Type', 'application/octet-stream')
        self.send_header('Content-Length', str(len(body)))
        for k, v in extra: self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)
    def do_GET(self):
        p = self.path.split('?')[0]
        if p == '/range.bin':
            r = self.headers.get('Range')
            if r and r.startswith('bytes='):
                start = int(r[6:].split('-')[0])
                if start >= len(DATA):
                    self.send_bytes(b'', 416, [('Content-Range', 'bytes */%d' % len(DATA))])
                    return
                self.send_bytes(DATA[start:], 206,
                                [('Content-Range', 'bytes %d-%d/%d' % (start, len(DATA) - 1, len(DATA)))])
                return
            self.send_bytes(DATA)
        elif p == '/norange.bin':
            self.send_bytes(DATA)
        elif p == '/to-http':
            self.send_response(302)
            self.send_header('Location', 'http://127.0.0.1:%d/range.bin' % PORT)
            self.send_header('Content-Length', '0')
            self.end_headers()
        elif p == '/slow':
            self.send_response(200)
            self.send_header('Content-Type', 'application/octet-stream')
            self.send_header('Content-Length', '16384')
            self.end_headers()
            for i in range(64):
                try:
                    self.wfile.write(b'x' * 256); self.wfile.flush()
                except Exception:
                    return
                time.sleep(0.03)
        elif p == '/stall':
            self.send_response(200)
            self.send_header('Content-Type', 'application/octet-stream')
            self.send_header('Content-Length', '1000')
            self.end_headers()
            time.sleep(20)
        else:
            self.send_bytes(b'not found', 404)
ThreadingHTTPServer(('127.0.0.1', PORT), H).serve_forever()
)PY";

struct RangeServer {
    long pid = -1;
    bool started = false;
    std::string baseUrl;

    ~RangeServer() { Stop(); }

    bool Start() {
#if defined(_WIN32)
        return false;
#else
        if (started) return true;
        if (std::system("command -v python3 > /dev/null 2>&1") != 0) return false;
        const pid_t p = fork();
        if (p < 0) return false;
        if (p == 0) {
            std::freopen("/dev/null", "w", stdout);
            std::freopen("/dev/null", "w", stderr);
            const std::string portStr = std::to_string(kPort);
            execlp("python3", "python3", "-c", kServer, portStr.c_str(), nullptr);
            _exit(127);
        }
        pid = p;
        baseUrl = "http://127.0.0.1:" + std::to_string(kPort);
        const auto deadline = std::chrono::steady_clock::now() + kStartTimeout;
        while (std::chrono::steady_clock::now() < deadline) {
            UltraNetSocketOptions o;
            o.connectTimeoutMs = 200;
            UltraNetHandle h = UltraNet_TcpConnect("127.0.0.1", kPort, o);
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
            kill(static_cast<pid_t>(pid), SIGTERM);
            int status = 0;
            waitpid(static_cast<pid_t>(pid), &status, 0);
            pid = -1;
        }
        started = false;
#endif
    }
};

std::mutex g_mutex;
RangeServer& Server() {
    static RangeServer s;
    return s;
}

bool EnsureServer() {
    UltraNet_Initialize();
    std::lock_guard<std::mutex> lk(g_mutex);
    if (Server().started) return true;
    return Server().Start();
}

std::string Base() {
    std::lock_guard<std::mutex> lk(g_mutex);
    return Server().baseUrl;
}

std::vector<uint8_t> Ramp(int from, int to) {
    std::vector<uint8_t> v;
    for (int i = from; i < to; ++i) v.push_back(static_cast<uint8_t>(i & 0xff));
    return v;
}

std::string ScratchFile(const char* name) {
    return (std::filesystem::temp_directory_path() / name).string();
}

void WriteFile(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(UltraCanvas::PathFromUtf8(path), std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
}

std::vector<uint8_t> ReadFile(const std::string& path) {
    std::ifstream in(UltraCanvas::PathFromUtf8(path), std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
}

void RemoveFile(const std::string& path) {
    std::error_code ec;
    std::filesystem::remove(UltraCanvas::PathFromUtf8(path), ec);
}

template <typename Pred>
bool WaitFor(Pred pred, std::chrono::milliseconds limit) {
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return pred();
}

} // namespace

// A partial file plus resumeOutputFile: the server answers 206 with the rest
// and the file ends up byte-exact, with the progress figures of the whole file.
TEST(download_resumes_a_partial_file_with_range) {
    if (!EnsureServer()) SKIP("python3 not available");
    const std::string path = ScratchFile("ultranet_resume.bin");
    WriteFile(path, Ramp(0, 3000));

    std::atomic<int64_t> firstNow{-1}, lastNow{0}, lastTotal{0};
    UltraNetHttpRequest req;
    req.url = Base() + "/range.bin";
    req.options.outputFilePath   = path;
    req.options.resumeOutputFile = true;
    req.onDownloadProgress = [&](int64_t now, int64_t total) {
        if (firstNow.load() < 0) firstNow = now;
        lastNow = now; lastTotal = total;
    };
    UltraNetResponse r;
    auto res = UltraNet_HttpRequest(req, r);
    REQUIRE(bool(res));
    REQUIRE_EQ(r.statusCode, 206);
    CHECK(r.body.empty());
    CHECK(r.headers.Get("Content-Range") == "bytes 3000-8191/8192");
    REQUIRE(ReadFile(path) == Ramp(0, kFileBytes));
    CHECK(firstNow.load() >= 3000);
    CHECK(lastNow.load() == kFileBytes);
    CHECK(lastTotal.load() == kFileBytes);
    RemoveFile(path);
}

// The same through UltraNet_HttpDownloadFile, which is a GET into the file.
TEST(download_file_helper_resumes) {
    if (!EnsureServer()) SKIP("python3 not available");
    const std::string path = ScratchFile("ultranet_resume_helper.bin");
    WriteFile(path, Ramp(0, 5000));
    UltraNetHttpOptions opt;
    opt.resumeOutputFile = true;
    auto res = UltraNet_HttpDownloadFile(Base() + "/range.bin", path, opt);
    REQUIRE(bool(res));
    REQUIRE_EQ(res.httpStatus, 206);
    REQUIRE(ReadFile(path) == Ramp(0, kFileBytes));
    RemoveFile(path);
}

// A server that ignores the range answers 200 with everything: the partial
// file (here deliberately wrong bytes) is replaced, not appended to.
TEST(download_starts_over_when_the_server_ignores_the_range) {
    if (!EnsureServer()) SKIP("python3 not available");
    const std::string path = ScratchFile("ultranet_norange.bin");
    WriteFile(path, std::vector<uint8_t>(3000, 0xEE));

    UltraNetHttpRequest req;
    req.url = Base() + "/norange.bin";
    req.options.outputFilePath   = path;
    req.options.resumeOutputFile = true;
    UltraNetResponse r;
    auto res = UltraNet_HttpRequest(req, r);
    REQUIRE(bool(res));
    REQUIRE_EQ(r.statusCode, 200);
    REQUIRE(ReadFile(path) == Ramp(0, kFileBytes));
    RemoveFile(path);
}

// A file that is already complete: 416 with the file's size as the total is
// success, and the file is left alone.
TEST(download_of_a_complete_file_is_already_complete) {
    if (!EnsureServer()) SKIP("python3 not available");
    const std::string path = ScratchFile("ultranet_complete.bin");
    WriteFile(path, Ramp(0, kFileBytes));
    UltraNetHttpOptions opt;
    opt.resumeOutputFile = true;
    auto res = UltraNet_HttpDownloadFile(Base() + "/range.bin", path, opt);
    REQUIRE(bool(res));
    REQUIRE_EQ(res.httpStatus, 416);
    CHECK(res.message == "already complete");
    REQUIRE(ReadFile(path) == Ramp(0, kFileBytes));
    RemoveFile(path);
}

// resumeFromOffset on a plain request: the body is the tail.
TEST(request_resume_from_offset_returns_the_tail) {
    if (!EnsureServer()) SKIP("python3 not available");
    UltraNetHttpOptions opt;
    opt.resumeFromOffset = 8000;
    UltraNetResponse r;
    auto res = UltraNet_HttpGet(Base() + "/range.bin", r, opt);
    REQUIRE(bool(res));
    REQUIRE_EQ(r.statusCode, 206);
    REQUIRE(r.body == Ramp(8000, kFileBytes));
}

// httpsOnlyRedirects: a redirect to http:// is refused with the 3xx kept.
TEST(https_only_redirects_refuse_a_redirect_to_http) {
    if (!EnsureServer()) SKIP("python3 not available");
    UltraNetHttpOptions opt;
    opt.httpsOnlyRedirects = true;
    UltraNetResponse r;
    auto res = UltraNet_HttpGet(Base() + "/to-http", r, opt);
    REQUIRE(!bool(res));
    REQUIRE_EQ(res.code, UltraNetResultCode::RedirectRefused);
    REQUIRE_EQ(r.statusCode, 302);
    CHECK(res.message.find("/range.bin") != std::string::npos);
    CHECK(r.body.empty());

    UltraNetResponse followed;
    auto ok = UltraNet_HttpGet(Base() + "/to-http", followed);
    REQUIRE(bool(ok));
    REQUIRE_EQ(followed.statusCode, 200);
    REQUIRE_EQ(followed.body.size(), static_cast<std::size_t>(kFileBytes));
}

// onHeaders fires once the headers are in and before any body; the redirect
// being followed does not fire it.
TEST(on_headers_fires_before_the_body) {
    if (!EnsureServer()) SKIP("python3 not available");
    int fired = 0;
    int statusSeen = 0;
    int64_t lengthSeen = -2;
    std::size_t bodyAtHeaders = 1;
    UltraNetHttpRequest req;
    req.url = Base() + "/to-http";
    req.onHeaders = [&](const UltraNetResponse& head) {
        ++fired;
        statusSeen = head.statusCode;
        lengthSeen = head.contentLength;
        bodyAtHeaders = head.body.size();
        return true;
    };
    UltraNetResponse r;
    auto res = UltraNet_HttpRequest(req, r);
    REQUIRE(bool(res));
    REQUIRE_EQ(fired, 1);
    REQUIRE_EQ(statusSeen, 200);
    REQUIRE_EQ(lengthSeen, static_cast<int64_t>(kFileBytes));
    REQUIRE_EQ(bodyAtHeaders, static_cast<std::size_t>(0));
    REQUIRE_EQ(r.body.size(), static_cast<std::size_t>(kFileBytes));
}

// onHeaders returning false ends the transfer: Cancelled, no body read.
TEST(on_headers_can_veto_the_body) {
    if (!EnsureServer()) SKIP("python3 not available");
    UltraNetHttpRequest req;
    req.url = Base() + "/range.bin";
    req.onHeaders = [](const UltraNetResponse& head) {
        return head.contentLength < 1000;   // too large for this caller
    };
    UltraNetResponse r;
    auto res = UltraNet_HttpRequest(req, r);
    REQUIRE(!bool(res));
    REQUIRE_EQ(res.code, UltraNetResultCode::Cancelled);
    CHECK(res.message.find("onHeaders") != std::string::npos);
    REQUIRE_EQ(r.statusCode, 200);
    REQUIRE(r.body.empty());
    CHECK(!r.IsComplete());
}

// A stall timeout ends a transfer that stops moving, without a whole-transfer
// limit being set.
TEST(stall_timeout_ends_a_transfer_that_stops) {
    if (!EnsureServer()) SKIP("python3 not available");
    UltraNetHttpOptions opt;
    opt.timeoutMs      = -1;
    opt.stallTimeoutMs = 1000;
    UltraNetResponse r;
    const auto start = std::chrono::steady_clock::now();
    auto res = UltraNet_HttpGet(Base() + "/stall", r, opt);
    const auto took = std::chrono::steady_clock::now() - start;
    REQUIRE(!bool(res));
    REQUIRE_EQ(res.code, UltraNetResultCode::Timeout);
    CHECK(took < std::chrono::seconds(8));
    REQUIRE_EQ(r.statusCode, 200);
    CHECK(!r.IsComplete());
}

// Pause stops the bytes, IsRequestPaused says so, Resume completes the
// transfer; the stall timeout does not fire while paused.
TEST(pause_and_resume_an_async_download) {
    if (!EnsureServer()) SKIP("python3 not available");
    std::atomic<int64_t> bytes{0};
    std::atomic<bool> done{false};
    std::atomic<int> status{0};
    std::atomic<std::size_t> bodySize{0};
    std::string error;
    std::mutex errorLock;

    UltraNetHttpRequest req;
    req.url = Base() + "/slow";
    req.options.timeoutMs      = -1;
    req.options.stallTimeoutMs = 1000;
    req.onDownloadProgress = [&](int64_t now, int64_t) { bytes = now; };
    UltraNetHandle h = UltraNet_HttpRequestAsync(req, [&](const UltraNetResponse& resp) {
        status = resp.statusCode;
        bodySize = resp.body.size();
        { std::lock_guard<std::mutex> lk(errorLock); error = resp.transferError; }
        done = true;
    });
    REQUIRE(h != UltraNetInvalidHandle);
    CHECK(!UltraNet_IsRequestPaused(h));

    REQUIRE(WaitFor([&] { return bytes.load() > 0; }, std::chrono::seconds(5)));
    REQUIRE(bool(UltraNet_PauseRequest(h)));
    REQUIRE(WaitFor([&] { return UltraNet_IsRequestPaused(h); }, std::chrono::seconds(2)));
    // Let what was already in flight land, then nothing more may arrive -
    // for longer than the stall timeout, which must not fire while paused.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const int64_t atPause = bytes.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    CHECK(bytes.load() == atPause);
    CHECK(atPause < 16384);
    CHECK(!done.load());
    CHECK(UltraNet_IsRequestActive(h));

    REQUIRE(bool(UltraNet_ResumeRequest(h)));
    REQUIRE(WaitFor([&] { return done.load(); }, std::chrono::seconds(10)));
    CHECK(!UltraNet_IsRequestPaused(h));
    REQUIRE_EQ(status.load(), 200);
    { std::lock_guard<std::mutex> lk(errorLock); CHECK(error.empty()); }
    REQUIRE_EQ(bodySize.load(), static_cast<std::size_t>(16384));
    REQUIRE_EQ(bytes.load(), static_cast<int64_t>(16384));

    auto gone = UltraNet_PauseRequest(h);
    CHECK(!gone && gone.code == UltraNetResultCode::InvalidHandle);
    auto bad = UltraNet_ResumeRequest(UltraNetInvalidHandle);
    CHECK(!bad && bad.code == UltraNetResultCode::InvalidHandle);
}

// An async download into a file: the file is complete and closed when
// onComplete fires, and the body stays empty.
TEST(async_download_into_an_output_file) {
    if (!EnsureServer()) SKIP("python3 not available");
    const std::string path = ScratchFile("ultranet_async_out.bin");
    RemoveFile(path);
    std::atomic<bool> done{false};
    std::vector<uint8_t> fileAtComplete;
    std::size_t bodyAtComplete = 1;
    int status = 0;

    UltraNetHttpRequest req;
    req.url = Base() + "/range.bin";
    req.options.outputFilePath = path;
    UltraNetHandle h = UltraNet_HttpRequestAsync(req, [&](const UltraNetResponse& resp) {
        status = resp.statusCode;
        bodyAtComplete = resp.body.size();
        fileAtComplete = ReadFile(path);
        done = true;
    });
    REQUIRE(h != UltraNetInvalidHandle);
    REQUIRE(WaitFor([&] { return done.load(); }, std::chrono::seconds(5)));
    REQUIRE_EQ(status, 200);
    REQUIRE_EQ(bodyAtComplete, static_cast<std::size_t>(0));
    REQUIRE(fileAtComplete == Ramp(0, kFileBytes));
    RemoveFile(path);
}
