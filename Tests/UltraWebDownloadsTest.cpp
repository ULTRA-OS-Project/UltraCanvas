// Tests/UltraWebDownloadsTest.cpp
// UltraWeb's downloads (Apps/UltraWeb/host/UltraWebDownloads): bytes saved
// through a part file, a local file copied, sizes shown dot-decimal, and an
// http download streamed to disk from a loopback server - whole, a 404 that
// leaves no file, and a cancelled one that leaves no part file either.
// The http part needs POSIX sockets and is skipped on Windows.
// Version: 1.0.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework

#include "host/UltraWebDownloads.h"

#include "UltraCanvasPathUtf8.h"
#include "UltraNet/UltraNetCore.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <csignal>
#include <sys/socket.h>
#include <unistd.h>
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0      // macOS: SIGPIPE is ignored in main instead
#endif
#endif

using namespace UltraWeb;
using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool cond, const std::string& what) {
    std::printf("  %s %s\n", cond ? "ok:  " : "FAIL:", what.c_str());
    if (!cond) ++g_failures;
}

// The "UI thread": tasks posted from anywhere, run by Pump on this thread.
std::mutex g_lock;
std::condition_variable g_wake;
std::deque<std::function<void()>> g_tasks;

void Post(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> guard(g_lock);
        g_tasks.push_back(std::move(task));
    }
    g_wake.notify_all();
}

// Runs posted tasks until `done` holds or `seconds` pass.
bool Pump(const std::function<bool()>& done, int seconds = 20) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (!done()) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> guard(g_lock);
            if (!g_wake.wait_until(guard, until, [] { return !g_tasks.empty(); })) return done();
            task = std::move(g_tasks.front());
            g_tasks.pop_front();
        }
        task();
    }
    return true;
}

std::string ReadAll(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void TestSaveBytesAndCopy(const std::filesystem::path& dir) {
    std::printf("saving bytes, copying a local file\n");
    const std::string path = PathToUtf8(dir / "saved \xC3\xA9.bin");
    std::string error;
    const std::vector<uint8_t> bytes = { 'a', 0, 'b', 0xFF };
    Check(SaveBytes(path, bytes, error), "SaveBytes");
    Check(ReadAll(PathFromUtf8(path)) == std::string("a\0b\xFF", 4), "the bytes, NULs and all, under a UTF-8 name");
    Check(!std::filesystem::exists(PathFromUtf8(path + ".part")), "no part file left");
    Check(SaveBytes(path, { 'z' }, error) && ReadAll(PathFromUtf8(path)) == "z", "a second save replaces the file");
    Check(!SaveBytes(PathToUtf8(dir / "no-such-dir" / "x"), bytes, error) && !error.empty(), "a bad folder fails, saying why");

    const std::string source = PathToUtf8(dir / "source.txt");
    { std::ofstream(PathFromUtf8(source), std::ios::binary) << "local file"; }
    const std::string copy = PathToUtf8(dir / "copy.txt");
    bool finished = false, ok = false;
    StartDownload("file://" + source, copy, Post, nullptr, [&](bool good, const std::string&) { finished = true; ok = good; });
    Pump([&] { return finished; });
    Check(ok && ReadAll(PathFromUtf8(copy)) == "local file", "a file:// download copies the file");

    Check(ByteSize(12) == "12 bytes" && ByteSize(1) == "1 byte", "bytes");
    Check(ByteSize(2048) == "2 KB", "kilobytes");
    Check(ByteSize(int64_t(1536) * 1024) == "1.5 MB", "megabytes, dot-decimal");
    Check(ByteSize(int64_t(3) * 1024 * 1024 * 1024) == "3.0 GB", "gigabytes");
}

#ifndef _WIN32
// A loopback HTTP/1.0 server: /file answers `body` (slowly when asked),
// anything else 404.
struct Server {
    int listener = -1;
    int port = 0;
    std::string body;
    std::atomic<bool> slow{ false };
    std::atomic<bool> stop{ false };
    std::thread thread;

    bool Start() {
        listener = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listener < 0) return false;
        int one = 1;
        ::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (::bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) return false;
        socklen_t len = sizeof(addr);
        ::getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &len);
        port = ntohs(addr.sin_port);
        ::listen(listener, 8);
        thread = std::thread([this] { Run(); });
        return true;
    }
    void Run() {
        while (!stop) {
            const int client = ::accept(listener, nullptr, nullptr);
            if (client < 0) break;
            char request[2048];
            const ssize_t got = ::recv(client, request, sizeof(request) - 1, 0);
            const std::string line = got > 0 ? std::string(request, static_cast<size_t>(got)) : std::string();
            if (line.rfind("GET /file ", 0) == 0) {
                const std::string head = "HTTP/1.0 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: " +
                                         std::to_string(body.size()) + "\r\n\r\n";
                ::send(client, head.data(), head.size(), MSG_NOSIGNAL);
                for (size_t at = 0; at < body.size() && !stop;) {
                    const size_t n = std::min<size_t>(64 * 1024, body.size() - at);
                    if (::send(client, body.data() + at, n, MSG_NOSIGNAL) <= 0) break;
                    at += n;
                    if (slow) std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
            } else {
                const std::string head = "HTTP/1.0 404 Not Found\r\nContent-Length: 0\r\n\r\n";
                ::send(client, head.data(), head.size(), MSG_NOSIGNAL);
            }
            ::close(client);
        }
    }
    void Stop() {
        stop = true;
        ::shutdown(listener, SHUT_RDWR);
        ::close(listener);
        if (thread.joinable()) thread.join();
    }
};

void TestHttp(const std::filesystem::path& dir) {
    std::printf("http downloads streamed to disk\n");
    Server server;
    server.body.resize(3 * 1024 * 1024 + 17);
    for (size_t i = 0; i < server.body.size(); ++i) server.body[i] = static_cast<char>((i * 31 + 7) & 0xFF);
    if (!server.Start()) { Check(false, "a loopback server"); return; }
    const std::string base = "http://127.0.0.1:" + std::to_string(server.port);

    const std::string path = PathToUtf8(dir / "big.bin");
    bool finished = false, ok = false;
    int progress = 0;
    std::string error;
    auto download = StartDownload(base + "/file", path, Post,
                                  [&](int64_t, int64_t) { ++progress; },
                                  [&](bool good, const std::string& why) { finished = true; ok = good; error = why; });
    Check(Pump([&] { return finished; }), "it finishes");
    Check(ok, "whole (" + error + ")");
    Check(ReadAll(PathFromUtf8(path)) == server.body, "every byte, in order (3 MB)");
    Check(!std::filesystem::exists(PathFromUtf8(path + ".part")), "the part file became the file");

    finished = false;
    const std::string missing = PathToUtf8(dir / "missing.bin");
    StartDownload(base + "/nothing", missing, Post, nullptr,
                  [&](bool good, const std::string& why) { finished = true; ok = good; error = why; });
    Pump([&] { return finished; });
    Check(!ok && error.find("404") != std::string::npos, "a 404 fails, saying so");
    Check(!std::filesystem::exists(PathFromUtf8(missing)) && !std::filesystem::exists(PathFromUtf8(missing + ".part")),
          "and leaves no file");

    server.slow = true;
    finished = false;
    const std::string cancelled = PathToUtf8(dir / "cancelled.bin");
    auto slow = StartDownload(base + "/file", cancelled, Post, nullptr,
                              [&](bool good, const std::string& why) { finished = true; ok = good; error = why; });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    slow->Cancel();
    Pump([&] { return finished; });
    Check(finished && !ok, "a cancelled download ends, not saved");
    Check(!std::filesystem::exists(PathFromUtf8(cancelled)) && !std::filesystem::exists(PathFromUtf8(cancelled + ".part")),
          "and leaves no part file");
    server.Stop();
}
#endif

} // namespace

int main() {
#ifndef _WIN32
    std::signal(SIGPIPE, SIG_IGN);   // a client that hangs up must not end the test
#endif
    UltraNet_Initialize();
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ultraweb-downloads-test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    TestSaveBytesAndCopy(dir);
#ifndef _WIN32
    TestHttp(dir);
#else
    std::printf("http downloads: skipped (POSIX sockets)\n");
#endif
    std::filesystem::remove_all(dir);
    UltraNet_Shutdown();
    if (g_failures) {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("all checks passed\n");
    return 0;
}
