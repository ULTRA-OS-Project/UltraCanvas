// Tests/UltraWebCookiesTest.cpp
// UltraWeb's page services (UltraWebPages) with a cookie jar, against a
// loopback server: a login's POST answered with a 303 that sets the session
// cookie, the GET it turns into sending it, a redirect's cookie going with
// the next hop, a request for another site's page sending none, and services
// without a jar sending none at all. The jar's own rules are tested in
// Tests/UltraNet/test_cookie_jar.cpp.
//
// The http part needs POSIX sockets and is skipped on Windows.
// Version: 0.2.0 - the jar's rules moved to UltraNet's suite
// Version: 0.1.0
// Last Modified: 2026-10-22
// Author: UltraCanvas Framework / ULTRA OS

#include "host/UltraWebCookies.h"
#include "host/UltraWebPages.h"

#include "UltraCanvasPathUtf8.h"
#include "UltraNet/UltraNetCore.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <arpa/inet.h>
#include <csignal>
#include <netinet/in.h>
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

#ifndef _WIN32
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

bool Pump(const std::function<bool()>& done, int seconds = 20) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (!done()) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> guard(g_lock);
            if (!g_wake.wait_until(guard, until, [] { return !g_tasks.empty(); })) return false;
            task = std::move(g_tasks.front());
            g_tasks.pop_front();
        }
        task();
    }
    return true;
}

// A loopback HTTP/1.0 server: POST /login answers 303 to /home with the
// session cookie, /home says whether the request carried it, /hop sets
// another and redirects to /home; every request line is recorded.
struct Server {
    int listener = -1;
    int port = 0;
    std::atomic<bool> stop{ false };
    std::thread thread;
    std::mutex lock;
    std::vector<std::string> requests;

    bool Start() {
        listener = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listener < 0) return false;
        int one = 1;
        ::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
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
            std::string request;
            char chunk[4096];
            // Headers (and a small body) arrive in one go on loopback; read
            // until the blank line.
            while (request.find("\r\n\r\n") == std::string::npos) {
                const ssize_t got = ::recv(client, chunk, sizeof(chunk), 0);
                if (got <= 0) break;
                request.append(chunk, static_cast<size_t>(got));
            }
            const std::string line = request.substr(0, request.find("\r\n"));
            std::string cookie;
            const size_t at = request.find("\r\nCookie: ");
            if (at != std::string::npos) cookie = request.substr(at + 10, request.find("\r\n", at + 2) - at - 10);
            {
                std::lock_guard<std::mutex> guard(lock);
                requests.push_back(line + " [" + cookie + "]");
            }
            std::string answer;
            if (line.rfind("POST /login ", 0) == 0) {
                answer = "HTTP/1.0 303 See Other\r\nLocation: /home\r\nSet-Cookie: session=abc; Path=/; HttpOnly\r\n"
                         "Content-Length: 0\r\n\r\n";
            } else if (line.rfind("GET /hop ", 0) == 0) {
                answer = "HTTP/1.0 302 Found\r\nLocation: /home\r\nSet-Cookie: hop=1; Path=/\r\nContent-Length: 0\r\n\r\n";
            } else if (line.rfind("GET /home ", 0) == 0) {
                const std::string body = "<html><head><title>" + (cookie.empty() ? std::string("nobody") : cookie) +
                                         "</title></head><body>hi</body></html>";
                answer = "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nContent-Length: " + std::to_string(body.size()) +
                         "\r\n\r\n" + body;
            } else {
                answer = "HTTP/1.0 404 Not Found\r\nContent-Length: 0\r\n\r\n";
            }
            ::send(client, answer.data(), answer.size(), MSG_NOSIGNAL);
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

std::string TitleOf(const HTML::FetchResult& result) {
    const std::string body(result.body.begin(), result.body.end());
    const size_t open = body.find("<title>");
    const size_t close = body.find("</title>");
    if (open == std::string::npos || close == std::string::npos) return std::string();
    return body.substr(open + 7, close - open - 7);
}

void TestServices() {
    std::printf("the page services keep and send cookies\n");
    Server server;
    if (!server.Start()) { Check(false, "loopback server started"); return; }
    const std::string base = "http://127.0.0.1:" + std::to_string(server.port);
    auto jar = std::make_shared<UltraNetCookieJar>();
    HTML::PageLoaderServices services = MakePageServices(Post, jar);

    bool done = false;
    HTML::FetchResult first;
    services.fetch(base + "/home", [&](HTML::FetchResult r) { first = std::move(r); done = true; });
    Check(Pump([&] { return done; }) && first.ok, "a page before logging in");
    Check(TitleOf(first) == "nobody", "carries no cookie");

    done = false;
    HTML::FetchResult login;
    services.post(base + "/login", "application/x-www-form-urlencoded", "user=a&pass=b",
                  [&](HTML::FetchResult r) { login = std::move(r); done = true; });
    Check(Pump([&] { return done; }) && login.ok, "the login form posted");
    Check(login.finalUrl == base + "/home", "the 303 was followed to /home");
    Check(TitleOf(login) == "session=abc", "the redirect's cookie went with the next hop");
    {
        std::lock_guard<std::mutex> guard(server.lock);
        Check(server.requests.size() >= 3 && server.requests[2].rfind("GET /home ", 0) == 0,
              "the 303 turned the POST into a GET");
    }

    done = false;
    HTML::FetchResult hop;
    services.fetch(base + "/hop", [&](HTML::FetchResult r) { hop = std::move(r); done = true; });
    Check(Pump([&] { return done; }) && hop.ok, "a redirecting page");
    Check(TitleOf(hop) == "session=abc; hop=1", "both cookies on the page it led to");

    done = false;
    HTML::FetchResult picture;
    services.fetchResource(base + "/home", "https://elsewhere.example/", [&](HTML::FetchResult r) {
        picture = std::move(r);
        done = true;
    });
    Check(Pump([&] { return done; }) && picture.ok, "a resource for another site's page");
    Check(TitleOf(picture) == "nobody", "a third-party request carries no cookie");

    HTML::PageLoaderServices without = MakePageServices(Post);
    done = false;
    HTML::FetchResult plain;
    without.fetch(base + "/hop", [&](HTML::FetchResult r) { plain = std::move(r); done = true; });
    Check(Pump([&] { return done; }) && plain.ok && TitleOf(plain) == "nobody", "without a jar no cookies at all");
    server.Stop();
}
#endif

} // namespace

int main() {
#ifndef _WIN32
    std::signal(SIGPIPE, SIG_IGN);
#endif
#ifndef _WIN32
    UltraNet_Initialize();
    TestServices();
    UltraNet_Shutdown();
#else
    std::printf("the page services: skipped (POSIX sockets)\n");
#endif
    std::printf(g_failures ? "\n%d check(s) failed\n" : "\nall checks passed\n", g_failures);
    return g_failures ? 1 : 0;
}
