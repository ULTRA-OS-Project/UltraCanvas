// Tests/UltraNet/test_body_file.cpp
// A request body sent from a file as it goes (UltraNetHttpRequest::
// bodyFilePath / bodyFileOffset / bodyFileLength): what an upload of a large
// file needs, which until now had to read the file into memory first. A
// small echo server on loopback answers with the body it received, so the
// test sees exactly the bytes that went over the wire - the whole file, or
// one range of it (an upload session's chunk). POSIX only: it is a socket
// loop on a thread.
// Version: 1.1.0 - the async path streams the file too
// Version: 1.0.0
// Last Modified: 2026-10-20
// Author: UltraCanvas Framework
#include "test_framework.h"

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetHttp.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include "../../UltraCanvas/include/UltraCanvasPathUtf8.h"

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#if !defined(_WIN32) && !defined(MSG_NOSIGNAL)
#define MSG_NOSIGNAL 0
#endif

#if !defined(_WIN32)
namespace {

// Serves connections until stopped: reads a request's headers and its
// Content-Length bytes, and answers 200 with those bytes as the body and the
// method in an X-Method header.
class EchoServer {
public:
    ~EchoServer() { Stop(); }

    bool Start() {
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0) return false;
        int yes = 1;
        ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 ||
            ::listen(fd_, 4) != 0)
            return false;
        socklen_t len = sizeof addr;
        ::getsockname(fd_, reinterpret_cast<sockaddr*>(&addr), &len);
        port_ = ntohs(addr.sin_port);
        thread_ = std::thread([this]() { Serve(); });
        return true;
    }

    void Stop() {
        stop_.store(true);
        if (thread_.joinable()) thread_.join();
        if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
    }

    std::string Url() const { return "http://127.0.0.1:" + std::to_string(port_) + "/up"; }
    int Requests() const { return requests_.load(); }

private:
    bool Readable(int fd, int ms) {
        pollfd p{fd, POLLIN, 0};
        return ::poll(&p, 1, ms) > 0;
    }

    void Serve() {
        while (!stop_.load()) {
            if (!Readable(fd_, 100)) continue;
            const int c = ::accept(fd_, nullptr, nullptr);
            if (c < 0) continue;
            Session(c);
            ::close(c);
        }
    }

    void Session(int c) {
        std::string in;
        char buf[4096];
        std::size_t headerEnd = std::string::npos;
        while (headerEnd == std::string::npos) {
            if (!Readable(c, 3000)) return;
            const ssize_t n = ::recv(c, buf, sizeof buf, 0);
            if (n <= 0) return;
            in.append(buf, static_cast<std::size_t>(n));
            headerEnd = in.find("\r\n\r\n");
        }
        const std::string head = in.substr(0, headerEnd);
        std::string lower = head;
        for (char& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        std::size_t length = 0;
        const std::size_t cl = lower.find("content-length:");
        if (cl != std::string::npos) length = std::strtoul(lower.c_str() + cl + 15, nullptr, 10);
        if (lower.find("expect: 100-continue") != std::string::npos) {
            const std::string cont = "HTTP/1.1 100 Continue\r\n\r\n";
            ::send(c, cont.data(), cont.size(), MSG_NOSIGNAL);
        }
        std::string body = in.substr(headerEnd + 4);
        while (body.size() < length) {
            if (!Readable(c, 3000)) return;
            const ssize_t n = ::recv(c, buf, sizeof buf, 0);
            if (n <= 0) return;
            body.append(buf, static_cast<std::size_t>(n));
        }
        ++requests_;
        const std::string method = head.substr(0, head.find(' '));
        const std::string answer = "HTTP/1.1 200 OK\r\nContent-Length: " +
                                   std::to_string(body.size()) + "\r\nX-Method: " + method +
                                   "\r\nConnection: close\r\n\r\n" + body;
        ::send(c, answer.data(), answer.size(), MSG_NOSIGNAL);
    }

    int fd_ = -1;
    int port_ = 0;
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::atomic<int> requests_{0};
};

// 3000 bytes that differ from one position to the next.
std::filesystem::path MakeFile() {
    const std::filesystem::path p = std::filesystem::temp_directory_path() / "ultranet_body_file.bin";
    std::ofstream out(p, std::ios::binary);
    for (int i = 0; i < 3000; ++i) out.put(static_cast<char>((i * 7 + i / 256) & 0xff));
    return p;
}

std::string Slice(const std::filesystem::path& p, std::size_t from, std::size_t length) {
    std::ifstream in(p, std::ios::binary);
    std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return all.substr(from, length);
}

std::string Text(const UltraNetResponse& r) { return std::string(r.body.begin(), r.body.end()); }

} // namespace

TEST(body_file_sends_the_whole_file) {
    UltraNet_Initialize();
    EchoServer server;
    if (!server.Start()) SKIP("cannot listen on loopback");
    const std::filesystem::path file = MakeFile();

    UltraNetHttpRequest req;
    req.url = server.Url();
    req.method = UltraNetHttpMethod::Put;
    req.bodyFilePath = UltraCanvas::PathToUtf8(file);
    req.body = {'i', 'g', 'n', 'o', 'r', 'e', 'd'};   // the file wins
    UltraNetResponse resp;
    const UltraNetResult r = UltraNet_HttpRequest(req, resp);
    server.Stop();

    REQUIRE(r.success);
    REQUIRE_EQ(resp.statusCode, 200);
    REQUIRE_EQ(resp.headers.Get("X-Method"), std::string("PUT"));   // still a PUT
    REQUIRE_EQ(Text(resp), Slice(file, 0, 3000));
    std::filesystem::remove(file);
}

TEST(body_file_sends_one_range_of_it) {
    UltraNet_Initialize();
    EchoServer server;
    if (!server.Start()) SKIP("cannot listen on loopback");
    const std::filesystem::path file = MakeFile();

    UltraNetHttpRequest req;
    req.url = server.Url();
    req.method = UltraNetHttpMethod::Post;
    req.bodyFilePath = UltraCanvas::PathToUtf8(file);
    req.bodyFileOffset = 1000;
    req.bodyFileLength = 1500;
    UltraNetResponse resp;
    const UltraNetResult r = UltraNet_HttpRequest(req, resp);
    server.Stop();

    REQUIRE(r.success);
    REQUIRE_EQ(resp.headers.Get("X-Method"), std::string("POST"));
    REQUIRE_EQ(Text(resp), Slice(file, 1000, 1500));
    std::filesystem::remove(file);
}

// Over a megabyte libcurl asks the server first (Expect: 100-continue) and
// sends in many reads of the file: still every byte, in order.
TEST(body_file_sends_a_large_file_whole) {
    UltraNet_Initialize();
    EchoServer server;
    if (!server.Start()) SKIP("cannot listen on loopback");
    const std::filesystem::path file =
            std::filesystem::temp_directory_path() / "ultranet_body_file_large.bin";
    {
        std::ofstream out(file, std::ios::binary);
        for (int i = 0; i < 3 * 1024 * 1024; ++i) out.put(static_cast<char>((i * 31 + i / 4096) & 0xff));
    }
    UltraNetHttpRequest req;
    req.url = server.Url();
    req.method = UltraNetHttpMethod::Put;
    req.bodyFilePath = UltraCanvas::PathToUtf8(file);
    UltraNetResponse resp;
    const UltraNetResult r = UltraNet_HttpRequest(req, resp);
    server.Stop();

    REQUIRE(r.success);
    REQUIRE_EQ(resp.body.size(), static_cast<std::size_t>(3 * 1024 * 1024));
    REQUIRE(Text(resp) == Slice(file, 0, 3 * 1024 * 1024));
    std::filesystem::remove(file);
}

// A file that is not there, or a range past its end, fails before anything
// is sent: no request half made with a body that cannot be read.
TEST(body_file_that_cannot_be_read_sends_nothing) {
    UltraNet_Initialize();
    EchoServer server;
    if (!server.Start()) SKIP("cannot listen on loopback");
    const std::filesystem::path file = MakeFile();

    UltraNetHttpRequest missing;
    missing.url = server.Url();
    missing.method = UltraNetHttpMethod::Put;
    missing.bodyFilePath = UltraCanvas::PathToUtf8(file) + ".not-there";
    UltraNetResponse resp;
    const UltraNetResult r1 = UltraNet_HttpRequest(missing, resp);

    UltraNetHttpRequest pastEnd = missing;
    pastEnd.bodyFilePath = UltraCanvas::PathToUtf8(file);
    pastEnd.bodyFileOffset = 2500;
    pastEnd.bodyFileLength = 1000;
    const UltraNetResult r2 = UltraNet_HttpRequest(pastEnd, resp);
    server.Stop();

    REQUIRE(!r1.success);
    REQUIRE(r1.code == UltraNetResultCode::NotFound);
    REQUIRE(!r2.success);
    REQUIRE_EQ(server.Requests(), 0);
    std::filesystem::remove(file);
}

// The async path streams the file the same way, from the multi worker: the
// request keeps the file open for as long as the transfer runs.
TEST(body_file_streams_on_the_async_path_too) {
    UltraNet_Initialize();
    EchoServer server;
    if (!server.Start()) SKIP("cannot listen on loopback");
    const std::filesystem::path file = MakeFile();

    UltraNetHttpRequest req;
    req.url = server.Url();
    req.method = UltraNetHttpMethod::Put;
    req.bodyFilePath = UltraCanvas::PathToUtf8(file);
    req.bodyFileOffset = 500;
    req.bodyFileLength = 2000;

    std::mutex m;
    std::condition_variable cv;
    bool done = false;
    UltraNetResponse got;
    const UltraNetHandle h = UltraNet_HttpRequestAsync(req, [&](const UltraNetResponse& r) {
        std::lock_guard<std::mutex> lk(m);
        got = r;
        done = true;
        cv.notify_all();
    });
    {
        std::unique_lock<std::mutex> lk(m);
        cv.wait_for(lk, std::chrono::seconds(10), [&]() { return done; });
    }
    server.Stop();

    REQUIRE(h != UltraNetInvalidHandle);
    REQUIRE(done);
    REQUIRE_EQ(got.statusCode, 200);
    REQUIRE_EQ(got.headers.Get("X-Method"), std::string("PUT"));
    REQUIRE_EQ(Text(got), Slice(file, 500, 2000));
    std::filesystem::remove(file);
}

// A file the async path cannot open answers at once, with no handle and
// nothing sent.
TEST(body_file_the_async_path_cannot_open_sends_nothing) {
    UltraNet_Initialize();
    EchoServer server;
    if (!server.Start()) SKIP("cannot listen on loopback");

    UltraNetHttpRequest req;
    req.url = server.Url();
    req.method = UltraNetHttpMethod::Put;
    req.bodyFilePath = UltraCanvas::PathToUtf8(
            std::filesystem::temp_directory_path() / "ultranet_body_file_not_there.bin");
    bool answered = false;
    std::string error;
    const UltraNetHandle h = UltraNet_HttpRequestAsync(req, [&](const UltraNetResponse& r) {
        answered = true;
        error = r.transferError;
    });
    server.Stop();

    REQUIRE(h == UltraNetInvalidHandle);
    REQUIRE(answered);
    REQUIRE(!error.empty());
    REQUIRE_EQ(server.Requests(), 0);
}
#endif // !_WIN32
