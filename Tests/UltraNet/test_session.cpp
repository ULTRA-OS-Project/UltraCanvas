// Tests/UltraNet/test_session.cpp
// UltraNet sessions (UltraNetCookies.h): one cookie store for every request
// of a session, from any thread, and connections kept open between requests.
//
// A session used to keep its connections in its libcurl share
// (CURL_LOCK_DATA_CONNECT), which libcurl does not support between threads
// that transfer at the same time. They now live in the session's easy
// handles, each used by one request at a time. Checked here against a small
// keep-alive HTTP/1.1 server on loopback that counts the connections it is
// given (POSIX only - it is a socket loop on threads, like the scripted FTP
// server in test_ftp_log.cpp).
#include "test_framework.h"

#include <UltraNet/UltraNetCookies.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

// As in test_ftp_log.cpp: no SIGPIPE from a client that hangs up mid-reply.
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace {

// Just enough HTTP/1.1 for GET: keeps each connection open for the next
// request, serves each on a thread of its own, and answers
//   /setcookie   Set-Cookie: probe=chocolate
//   /cookie      "cookie=<the probe cookie sent>" or "cookie=none"
//   anything     "hello"
class KeepAliveHttpServer {
public:
    ~KeepAliveHttpServer() { Stop(); }

    bool Start() {
        listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listenFd_ < 0) return false;
        int yes = 1;
        ::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 ||
            ::listen(listenFd_, 16) != 0) {
            ::close(listenFd_);
            listenFd_ = -1;
            return false;
        }
        socklen_t len = sizeof addr;
        ::getsockname(listenFd_, reinterpret_cast<sockaddr*>(&addr), &len);
        port_ = ntohs(addr.sin_port);
        acceptThread_ = std::thread([this]() { AcceptLoop(); });
        return true;
    }

    void Stop() {
        stop_.store(true);
        if (acceptThread_.joinable()) acceptThread_.join();
        std::vector<std::thread> sessions;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            sessions.swap(sessions_);
        }
        for (std::thread& t : sessions) t.join();
        if (listenFd_ >= 0) { ::close(listenFd_); listenFd_ = -1; }
    }

    std::string Url(const std::string& path) const {
        return "http://127.0.0.1:" + std::to_string(port_) + path;
    }
    int Connections() const { return connections_.load(); }

private:
    bool WaitReadable(int fd, int ms) {
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (!stop_.load()) {
            pollfd p{fd, POLLIN, 0};
            if (::poll(&p, 1, 50) > 0) return true;
            if (std::chrono::steady_clock::now() >= until) return false;
        }
        return false;
    }

    void AcceptLoop() {
        while (!stop_.load()) {
            if (!WaitReadable(listenFd_, 200)) continue;
            const int fd = ::accept(listenFd_, nullptr, nullptr);
            if (fd < 0) continue;
            connections_.fetch_add(1);
            std::lock_guard<std::mutex> lk(mutex_);
            sessions_.emplace_back([this, fd]() { Serve(fd); });
        }
    }

    // The value of the "probe" cookie in a request head, or "none".
    static std::string ProbeCookie(const std::string& head) {
        const std::size_t at = head.find("probe=");
        if (at == std::string::npos) return "none";
        const std::size_t end = head.find_first_of(";\r\n", at);
        return head.substr(at + 6, end == std::string::npos ? std::string::npos
                                                            : end - at - 6);
    }

    void Serve(int fd) {
        std::string buffer;
        char chunk[4096];
        while (!stop_.load()) {
            std::size_t endOfHead;
            while ((endOfHead = buffer.find("\r\n\r\n")) == std::string::npos) {
                if (!WaitReadable(fd, 5000)) { ::close(fd); return; }
                const ssize_t n = ::recv(fd, chunk, sizeof chunk, 0);
                if (n <= 0) { ::close(fd); return; }
                buffer.append(chunk, static_cast<std::size_t>(n));
            }
            const std::string head = buffer.substr(0, endOfHead);
            buffer.erase(0, endOfHead + 4);

            const std::size_t pathStart = head.find(' ') + 1;
            const std::string path = head.substr(pathStart, head.find(' ', pathStart) - pathStart);
            std::string extra, body = "hello";
            if (path == "/setcookie") {
                extra = "Set-Cookie: probe=chocolate; Path=/\r\n";
                body = "ok";
            } else if (path == "/cookie") {
                body = "cookie=" + ProbeCookie(head);
            }
            const std::string reply = "HTTP/1.1 200 OK\r\n"
                                      "Content-Type: text/plain\r\n" + extra +
                                      "Content-Length: " + std::to_string(body.size()) +
                                      "\r\n\r\n" + body;
            (void)::send(fd, reply.data(), reply.size(), MSG_NOSIGNAL);
        }
        ::close(fd);
    }

    int listenFd_ = -1;
    int port_ = 0;
    std::atomic<bool> stop_{false};
    std::atomic<int> connections_{0};
    std::thread acceptThread_;
    std::mutex mutex_;
    std::vector<std::thread> sessions_;
};

std::string BodyOf(const UltraNetResponse& r) {
    return std::string(r.body.begin(), r.body.end());
}

} // namespace

TEST(session_requests_one_after_another_share_one_connection) {
    KeepAliveHttpServer server;
    if (!server.Start()) SKIP("cannot listen on loopback");
    const UltraNetHandle s = UltraNet_CreateSession();
    REQUIRE(s != UltraNetInvalidHandle);

    bool allOk = true;
    for (int i = 0; i < 5; ++i) {
        UltraNetResponse r;
        allOk = allOk && UltraNet_SessionHttpGet(s, server.Url("/hello"), r) &&
                r.statusCode == 200 && BodyOf(r) == "hello";
    }
    UltraNet_DestroySession(s);
    server.Stop();

    REQUIRE(allOk);
    REQUIRE_EQ(server.Connections(), 1);
}

// The case libcurl does not support with a shared connection pool: requests
// on one session from several threads at once. Every one gets its answer,
// the cookie one of them was given reaches all the others, and no more
// connections are made than there were requests in flight at once.
TEST(session_requests_from_several_threads_share_the_cookies) {
    KeepAliveHttpServer server;
    if (!server.Start()) SKIP("cannot listen on loopback");
    const UltraNetHandle s = UltraNet_CreateSession();
    REQUIRE(s != UltraNetInvalidHandle);

    UltraNetResponse primed;
    REQUIRE(bool(UltraNet_SessionHttpGet(s, server.Url("/setcookie"), primed)));

    constexpr int kThreads = 4;
    constexpr int kRequests = 10;
    std::atomic<int> answered{0};
    std::atomic<int> withCookie{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < kRequests; ++i) {
                UltraNetResponse r;
                if (!UltraNet_SessionHttpGet(s, server.Url("/cookie"), r) ||
                    r.statusCode != 200)
                    continue;
                answered.fetch_add(1);
                if (BodyOf(r) == "cookie=chocolate") withCookie.fetch_add(1);
            }
        });
    }
    for (std::thread& t : threads) t.join();
    UltraNet_DestroySession(s);
    server.Stop();

    REQUIRE_EQ(answered.load(), kThreads * kRequests);
    REQUIRE_EQ(withCookie.load(), kThreads * kRequests);
    // One per request in flight at once at most - the priming request's
    // connection taken up again by one of the threads - not one per request.
    REQUIRE(server.Connections() <= kThreads + 1);
}

TEST(session_without_connection_reuse_connects_for_every_request) {
    KeepAliveHttpServer server;
    if (!server.Start()) SKIP("cannot listen on loopback");
    UltraNetSessionOptions options;
    options.reuseConnections = false;
    const UltraNetHandle s = UltraNet_CreateSession(options);
    REQUIRE(s != UltraNetInvalidHandle);

    for (int i = 0; i < 3; ++i) {
        UltraNetResponse r;
        REQUIRE(bool(UltraNet_SessionHttpGet(s, server.Url("/hello"), r)));
    }
    UltraNet_DestroySession(s);
    server.Stop();

    REQUIRE_EQ(server.Connections(), 3);
}

#endif // !_WIN32
