// Tests/UltraWebCookiesTest.cpp
// UltraWeb's cookie jar (Apps/UltraWeb/host/UltraWebCookies): Set-Cookie
// parsing and the rules for what is stored and what is sent - domain, path,
// Secure, SameSite, the __Secure- / __Host- prefixes, expiry, Max-Age over
// Expires, no third-party cookies, a Domain naming a whole suffix refused -
// the cookie dates servers send, the sites hosts belong to, and the file the
// cookies with an expiry are kept in across runs. Then the page services
// (UltraWebPages) against a loopback server: a login's POST answered with a
// 303 that sets the session cookie, the GET it turns into sending it, and a
// page that never set one getting none.
//
// The http part needs POSIX sockets and is skipped on Windows.
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

// 2026-10-22 12:00:00 UTC.
constexpr int64_t kNow = 1792670400;

void TestDates() {
    std::printf("cookie dates\n");
    int64_t t = 0;
    Check(CookieJar::ParseCookieDate("Thu, 22 Oct 2026 12:00:00 GMT", t) && t == kNow, "RFC 1123 date");
    Check(CookieJar::ParseCookieDate("Thursday, 22-Oct-26 12:00:00 GMT", t) && t == kNow, "RFC 850 date, two-digit year");
    Check(CookieJar::ParseCookieDate("Thu Oct 22 12:00:00 2026", t) && t == kNow, "asctime date");
    Check(CookieJar::ParseCookieDate("22 oct 2026 12:00:00", t) && t == kNow, "no weekday, lower-case month");
    Check(CookieJar::ParseCookieDate("Thu, 01 Jan 1970 00:00:00 GMT", t) && t == 0, "the epoch");
    Check(!CookieJar::ParseCookieDate("tomorrow", t), "not a date");
    Check(!CookieJar::ParseCookieDate("Thu, 32 Oct 2026 12:00:00 GMT", t), "day 32 refused");
    Check(!CookieJar::ParseCookieDate("Thu, 22 Oct 2026 GMT", t), "no time refused");
}

void TestSites() {
    std::printf("sites\n");
    Check(CookieJar::SiteOf("www.example.com") == "example.com", "www.example.com -> example.com");
    Check(CookieJar::SiteOf("a.b.example.co.uk") == "example.co.uk", "a.b.example.co.uk -> example.co.uk");
    Check(CookieJar::SiteOf("shop.example.com.au") == "example.com.au", "shop.example.com.au -> example.com.au");
    Check(CookieJar::SiteOf("alice.github.io") == "alice.github.io", "alice.github.io is a site of its own");
    Check(CookieJar::SiteOf("192.168.1.10") == "192.168.1.10", "an IP address is its own site");
    Check(CookieJar::SiteOf("localhost") == "localhost", "localhost");
    Check(CookieJar::Classify("https://cdn.example.com/a.png", "https://www.example.com/") == CookieRequest::SameSite,
          "a picture from the page's own site is same-site");
    Check(CookieJar::Classify("https://tracker.net/p.gif", "https://www.example.com/") == CookieRequest::CrossSite,
          "a picture from another site is cross-site");
    Check(CookieJar::Classify("https://www.example.com/", "") == CookieRequest::Navigation, "the page itself");
}

void TestStoreAndSend() {
    std::printf("storing and sending\n");
    CookieJar jar;
    const auto nav = CookieRequest::Navigation;
    Check(jar.Store("https://www.example.com/account/login", "sid=1; Path=/; Secure; HttpOnly", nav, kNow), "a session cookie");
    Check(jar.HeaderFor("https://www.example.com/", nav, kNow) == "sid=1", "sent back to its host");
    Check(jar.HeaderFor("http://www.example.com/", nav, kNow).empty(), "a Secure cookie never over http");
    Check(jar.HeaderFor("https://shop.example.com/", nav, kNow).empty(), "a host-only cookie not to a sibling");

    // Default path: the request's directory.
    jar.Store("https://www.example.com/docs/page.html", "lang=en", nav, kNow);
    Check(jar.HeaderFor("https://www.example.com/docs/other", nav, kNow).find("lang=en") != std::string::npos,
          "the default path covers its directory");
    Check(jar.HeaderFor("https://www.example.com/docsx", nav, kNow).find("lang=en") == std::string::npos,
          "/docs does not match /docsx");
    Check(jar.HeaderFor("https://www.example.com/docs/a", nav, kNow) == "lang=en; sid=1", "the longer path first");

    // Domain: to the subdomains too, but never a whole suffix or another site.
    Check(jar.Store("https://www.example.com/", "pref=dark; Domain=.example.com; Max-Age=3600", nav, kNow), "Domain=.example.com");
    Check(jar.HeaderFor("http://shop.example.com/", nav, kNow) == "pref=dark", "sent to a subdomain");
    Check(!jar.Store("https://www.example.com/", "bad=1; Domain=com", nav, kNow), "Domain=com refused");
    Check(!jar.Store("https://www.example.co.uk/", "bad=1; Domain=co.uk", nav, kNow), "Domain=co.uk refused");
    Check(!jar.Store("https://www.example.com/", "bad=1; Domain=other.com", nav, kNow), "another site's Domain refused");

    // Third-party: a cross-site picture neither sets nor sends.
    Check(!jar.Store("https://tracker.net/p.gif", "id=42; Max-Age=86400", CookieRequest::CrossSite, kNow),
          "a cross-site answer sets nothing");
    jar.Store("https://tracker.net/", "id=42; Max-Age=86400; SameSite=None; Secure", nav, kNow);
    Check(jar.HeaderFor("https://tracker.net/p.gif", CookieRequest::CrossSite, kNow).empty(),
          "a cross-site request sends nothing, SameSite=None or not");
    Check(jar.HeaderFor("https://tracker.net/p.gif", CookieRequest::SameSite, kNow) == "id=42", "same-site sends it");

    // Secure rules and the prefixes.
    Check(!jar.Store("http://www.example.com/", "s=1; Secure", nav, kNow), "Secure set over http refused");
    Check(!jar.Store("https://www.example.com/", "n=1; SameSite=None", nav, kNow), "SameSite=None without Secure refused");
    Check(!jar.Store("http://www.example.com/", "sid=2; Path=/", nav, kNow), "http cannot overwrite a Secure cookie");
    Check(!jar.Store("https://www.example.com/", "__Secure-a=1", nav, kNow), "__Secure- without Secure refused");
    Check(!jar.Store("https://www.example.com/", "__Host-a=1; Secure; Path=/; Domain=example.com", nav, kNow),
          "__Host- with a Domain refused");
    Check(jar.Store("https://www.example.com/", "__Host-a=1; Secure; Path=/", nav, kNow), "__Host- as it should be");

    // Expiry: Max-Age wins over Expires; the past deletes; 400 days at most.
    Check(jar.Store("https://www.example.com/", "t=1; Expires=Thu, 01 Jan 2099 00:00:00 GMT; Max-Age=60", nav, kNow),
          "Max-Age with Expires");
    Check(jar.HeaderFor("https://www.example.com/", nav, kNow + 61).find("t=1") == std::string::npos,
          "gone after its Max-Age, whatever Expires said");
    jar.Store("https://www.example.com/", "long=1; Max-Age=999999999", nav, kNow);
    for (const Cookie& c : jar.List(kNow))
        if (c.name == "long") Check(c.expires == kNow + CookieJar::kMaxLifetimeSeconds, "capped at 400 days");
    Check(jar.Store("https://www.example.com/", "lang=x; Path=/docs; Max-Age=0", nav, kNow), "Max-Age=0 deletes");
    Check(jar.HeaderFor("https://www.example.com/docs/a", nav, kNow).find("lang=") == std::string::npos, "deleted");
    jar.Store("https://www.example.com/", "pref=light; Domain=example.com; Max-Age=3600", nav, kNow);
    Check(jar.HeaderFor("https://shop.example.com/", nav, kNow) == "pref=light", "a cookie replaced by name, domain and path");

    // Malformed.
    Check(!jar.Store("https://www.example.com/", "novalue", nav, kNow), "no '=' refused");
    Check(!jar.Store("https://www.example.com/", "=x", nav, kNow), "no name refused");
    Check(!jar.Store("https://www.example.com/", std::string("a=") + std::string(5000, 'x'), nav, kNow), "over 4 KB refused");

    Check(jar.RemoveSite("www.example.com") > 0 && jar.HeaderFor("https://www.example.com/", nav, kNow).empty(),
          "RemoveSite drops a site's cookies");
    Check(jar.HeaderFor("https://tracker.net/", nav, kNow) == "id=42", "and only that site's");
    jar.Clear();
    Check(jar.Count() == 0, "Clear empties the jar");
}

void TestLimits() {
    std::printf("limits\n");
    CookieJar jar;
    for (int i = 0; i < 60; ++i)
        jar.Store("https://a.example/", "c" + std::to_string(i) + "=1", CookieRequest::Navigation, kNow + i);
    Check(jar.Count() == CookieJar::kMaxPerDomain, "50 cookies per domain");
    const std::string header = jar.HeaderFor("https://a.example/", CookieRequest::Navigation, kNow + 100);
    Check(header.find("c0=1") == std::string::npos && header.find("c59=1") != std::string::npos, "the oldest went first");
}

void TestFile(const std::filesystem::path& dir) {
    std::printf("kept across runs\n");
    const std::string path = PathToUtf8(dir / "cookies.json");
    const int64_t now = CookieJar::Now();
    {
        CookieJar jar(path);
        jar.Store("https://www.example.com/", "keep=1; Max-Age=3600", CookieRequest::Navigation, now);
        jar.Store("https://www.example.com/", "session=1", CookieRequest::Navigation, now);
        Check(jar.TakeFlushRequest(), "a change asks for a flush");
        Check(!jar.TakeFlushRequest(), "once per batch");
        Check(jar.Flush(), "written");
    }
    CookieJar again(path);
    const std::string header = again.HeaderFor("https://www.example.com/", CookieRequest::Navigation, now);
    Check(header == "keep=1", "the persistent cookie came back, the session one did not");
    again.Clear();
    Check(again.Flush() && !std::filesystem::exists(PathFromUtf8(path)), "an empty jar removes its file");
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
    auto jar = std::make_shared<CookieJar>();
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
    std::error_code ec;
    const std::filesystem::path dir = std::filesystem::temp_directory_path(ec) / "UltraWebCookiesTest";
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);

    TestDates();
    TestSites();
    TestStoreAndSend();
    TestLimits();
    TestFile(dir);
#ifndef _WIN32
    UltraNet_Initialize();
    TestServices();
    UltraNet_Shutdown();
#else
    std::printf("the page services: skipped (POSIX sockets)\n");
#endif
    std::filesystem::remove_all(dir, ec);
    std::printf(g_failures ? "\n%d check(s) failed\n" : "\nall checks passed\n", g_failures);
    return g_failures ? 1 : 0;
}
