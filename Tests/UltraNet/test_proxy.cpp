// Tests/UltraNet/test_proxy.cpp
// The proxy a connection goes through (UltraNet_ResolveProxy): the setting,
// the request's own, the exceptions, the machine itself always direct, the
// operating system's proxy read from the environment (Linux), the global
// source hook - and an HTTP request that really goes through a proxy: a
// one-shot fake proxy on 127.0.0.1 answers it.
#include "test_framework.h"

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetHttp.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

// Puts the module-wide setting back when a test ends.
struct GlobalProxyGuard {
    UltraNetProxyConfig saved = UltraNet_GetGlobalProxy();
    ~GlobalProxyGuard() {
        UltraNet_SetGlobalProxy(saved);
        UltraNet_ForgetSystemProxy();
    }
};

UltraNetProxyConfig Manual(UltraNetProxyType type, const std::string& host, int port) {
    UltraNetProxyConfig p;
    p.type = type;
    p.host = host;
    p.port = port;
    return p;
}

} // namespace

TEST(proxy_no_proxy_patterns) {
    const std::vector<std::string> list = {"*.example.com", "192.168.*", ".corp.test", "intranet",
                                           "10.0.0.0/8", "[fd00::1]", "files.test:8080"};
    REQUIRE(UltraNet_HostMatchesNoProxy("www.example.com", list));
    REQUIRE(UltraNet_HostMatchesNoProxy("example.com", list));        // *.x covers x itself
    REQUIRE(UltraNet_HostMatchesNoProxy("WWW.EXAMPLE.COM", list));
    REQUIRE(!UltraNet_HostMatchesNoProxy("example.org", list));
    REQUIRE(UltraNet_HostMatchesNoProxy("192.168.1.20", list));
    REQUIRE(!UltraNet_HostMatchesNoProxy("192.169.1.20", list));
    REQUIRE(UltraNet_HostMatchesNoProxy("a.corp.test", list));
    REQUIRE(UltraNet_HostMatchesNoProxy("corp.test", list));
    REQUIRE(UltraNet_HostMatchesNoProxy("intranet", list));
    REQUIRE(UltraNet_HostMatchesNoProxy("wiki.intranet", list));     // a name covers its subdomains
    REQUIRE(!UltraNet_HostMatchesNoProxy("myintranet", list));
    REQUIRE(UltraNet_HostMatchesNoProxy("10.20.30.40", list));
    REQUIRE(!UltraNet_HostMatchesNoProxy("11.0.0.1", list));
    REQUIRE(UltraNet_HostMatchesNoProxy("fd00::1", list));
    REQUIRE(UltraNet_HostMatchesNoProxy("files.test", list));        // the port is not part of the match
    REQUIRE(UltraNet_HostMatchesNoProxy("anything", {"*"}));
    REQUIRE(UltraNet_HostMatchesNoProxy("printer", {"<local>"}));
    REQUIRE(!UltraNet_HostMatchesNoProxy("printer.example.com", {"<local>"}));
    REQUIRE(!UltraNet_HostMatchesNoProxy("example.com", {}));
}

TEST(proxy_no_proxy_list_parse) {
    const std::vector<std::string> parsed = UltraNet_ParseNoProxyList(" *.example.com, 192.168.*;localhost  intranet ,,");
    REQUIRE_EQ(parsed.size(), std::size_t(4));
    REQUIRE_EQ(parsed[0], std::string("*.example.com"));
    REQUIRE_EQ(parsed[1], std::string("192.168.*"));
    REQUIRE_EQ(parsed[2], std::string("localhost"));
    REQUIRE_EQ(parsed[3], std::string("intranet"));
}

TEST(proxy_describe) {
    REQUIRE_EQ(UltraNet_DescribeProxy(Manual(UltraNetProxyType::Http, "proxy.test", 3128)),
               std::string("http://proxy.test:3128"));
    UltraNetProxyConfig socks = Manual(UltraNetProxyType::Socks5, "localhost", 10808);
    REQUIRE_EQ(UltraNet_DescribeProxy(socks), std::string("socks5h://localhost:10808"));
    socks.remoteDns = false;
    REQUIRE_EQ(UltraNet_DescribeProxy(socks), std::string("socks5://localhost:10808"));
    REQUIRE_EQ(UltraNet_DescribeProxy(Manual(UltraNetProxyType::Socks4, "::1", 1080)),
               std::string("socks4a://[::1]:1080"));
    REQUIRE_EQ(UltraNet_DescribeProxy(UltraNetProxyConfig::FromSystem()), std::string());
    REQUIRE_EQ(UltraNet_DescribeProxy(UltraNetProxyConfig()), std::string());
}

TEST(proxy_default_is_the_system) {
    REQUIRE(UltraNetConfig::Default().proxy.type == UltraNetProxyType::System);
}

TEST(proxy_resolve_manual_and_exceptions) {
    GlobalProxyGuard guard;
    UltraNetProxyConfig socks = Manual(UltraNetProxyType::Socks5, "proxy.test", 10808);
    socks.noProxyHosts = {"*.internal.test"};
    UltraNet_SetGlobalProxy(socks);

    UltraNetProxyConfig r = UltraNet_ResolveProxy("https://api.example.com/v1/chat");
    REQUIRE(r.type == UltraNetProxyType::Socks5);
    REQUIRE_EQ(r.host, std::string("proxy.test"));
    REQUIRE_EQ(r.port, 10808);
    // Every protocol goes through a proxy the person chose
    REQUIRE(UltraNet_ResolveProxy("imaps://mail.example.com:993/INBOX").type == UltraNetProxyType::Socks5);
    // An exception, the machine itself, a request's Direct: direct
    REQUIRE(!UltraNet_ResolveProxy("https://git.internal.test/").IsEnabled());
    REQUIRE(!UltraNet_ResolveProxy("http://localhost:11434/api/tags").IsEnabled());
    REQUIRE(!UltraNet_ResolveProxy("http://127.0.0.1:8080/").IsEnabled());
    REQUIRE(!UltraNet_ResolveProxy("http://[::1]:8080/").IsEnabled());
    REQUIRE(!UltraNet_ResolveProxy("https://api.example.com/", UltraNetProxyConfig::DirectConnection()).IsEnabled());
    // A request's own proxy wins over the global one
    r = UltraNet_ResolveProxy("https://api.example.com/", Manual(UltraNetProxyType::Http, "other.test", 0));
    REQUIRE(r.type == UltraNetProxyType::Http);
    REQUIRE_EQ(r.host, std::string("other.test"));
    REQUIRE_EQ(r.port, 80);   // the type's own port when none is given

    UltraNet_DisableProxy();
    REQUIRE(!UltraNet_ResolveProxy("https://api.example.com/").IsEnabled());
}

TEST(proxy_global_source) {
    GlobalProxyGuard guard;
    std::atomic<int> asked{0};
    UltraNet_SetGlobalProxySource([&asked]() {
        ++asked;
        return Manual(UltraNetProxyType::Http, "from-source.test", 8080);
    });
    REQUIRE_EQ(UltraNet_GetGlobalProxy().host, std::string("from-source.test"));
    REQUIRE_EQ(UltraNet_ResolveProxy("https://example.com/").host, std::string("from-source.test"));
    REQUIRE(asked.load() >= 2);
    // Setting the global proxy drops the source
    UltraNet_SetGlobalProxy(Manual(UltraNetProxyType::Http, "set.test", 1));
    REQUIRE_EQ(UltraNet_GetGlobalProxy().host, std::string("set.test"));
}

#if defined(__linux__)
TEST(proxy_system_reads_the_environment_per_scheme) {
    GlobalProxyGuard guard;
    const char* names[] = {"http_proxy", "HTTP_PROXY", "https_proxy", "HTTPS_PROXY", "all_proxy",
                           "ALL_PROXY", "ftp_proxy", "FTP_PROXY", "no_proxy", "NO_PROXY"};
    std::vector<std::pair<std::string, std::string>> saved;
    for (const char* n : names) {
        if (const char* v = std::getenv(n)) saved.emplace_back(n, v);
        ::unsetenv(n);
    }
    ::setenv("https_proxy", "http://user:p%40ss@web.proxy.test:3128", 1);
    ::setenv("no_proxy", "skip.test", 1);
    UltraNet_SetGlobalProxy(UltraNetProxyConfig::FromSystem());

    UltraNetProxyConfig r = UltraNet_ResolveProxy("https://example.com/");
    const bool webOk = r.type == UltraNetProxyType::Http && r.host == "web.proxy.test" && r.port == 3128 &&
                       r.credentials.username == "user" && r.credentials.password == "p@ss";
    // An https_proxy is the web's: not http://, not mail
    const bool httpDirect = !UltraNet_ResolveProxy("http://example.com/").IsEnabled();
    const bool mailDirect = !UltraNet_ResolveProxy("imaps://mail.example.com/").IsEnabled();
    const bool exceptionDirect = !UltraNet_ResolveProxy("https://www.skip.test/").IsEnabled();

    ::setenv("all_proxy", "socks5://socks.proxy.test:1080", 1);
    UltraNet_ForgetSystemProxy();
    const UltraNetProxyConfig mail = UltraNet_ResolveProxy("imaps://mail.example.com/");
    const bool mailSocks = mail.type == UltraNetProxyType::Socks5 && mail.host == "socks.proxy.test" &&
                           !mail.remoteDns;

    for (const char* n : names) ::unsetenv(n);
    for (const auto& kv : saved) ::setenv(kv.first.c_str(), kv.second.c_str(), 1);

    REQUIRE(webOk);
    REQUIRE(httpDirect);
    REQUIRE(mailDirect);
    REQUIRE(exceptionDirect);
    REQUIRE(mailSocks);
}
#endif

#if !defined(_WIN32)
namespace {

// A proxy that answers one request: records the request line it was sent
// and replies 200 "proxied".
struct OneShotProxy {
    int listener = -1;
    int port = 0;
    std::string requestLine;
    std::thread worker;

    bool Start() {
        listener = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listener < 0) return false;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (::bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) return false;
        socklen_t len = sizeof addr;
        if (::getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &len) != 0) return false;
        port = ntohs(addr.sin_port);
        if (::listen(listener, 1) != 0) return false;
        worker = std::thread([this]() {
            const int c = ::accept(listener, nullptr, nullptr);
            if (c < 0) return;
            std::string got;
            char buf[2048];
            while (got.find("\r\n\r\n") == std::string::npos) {
                const ssize_t n = ::recv(c, buf, sizeof buf, 0);
                if (n <= 0) break;
                got.append(buf, static_cast<std::size_t>(n));
            }
            requestLine = got.substr(0, got.find("\r\n"));
            const char reply[] = "HTTP/1.1 200 OK\r\nContent-Length: 7\r\nConnection: close\r\n\r\nproxied";
            ::send(c, reply, sizeof reply - 1, 0);
            ::close(c);
        });
        return true;
    }
    ~OneShotProxy() {
        if (listener >= 0) ::shutdown(listener, SHUT_RDWR);
        if (worker.joinable()) worker.join();
        if (listener >= 0) ::close(listener);
    }
};

} // namespace

TEST(proxy_http_request_goes_through_the_proxy) {
    GlobalProxyGuard guard;
    OneShotProxy proxy;
    REQUIRE(proxy.Start());
    UltraNet_SetGlobalProxy(Manual(UltraNetProxyType::Http, "127.0.0.1", proxy.port));

    UltraNetResponse response;
    UltraNetHttpOptions options;
    options.timeoutMs = 5000;
    const UltraNetResult result = UltraNet_HttpGet("http://only-the-proxy-knows.invalid/path", response, options);
    REQUIRE(result.success);
    REQUIRE_EQ(response.statusCode, 200);
    REQUIRE_EQ(std::string(response.body.begin(), response.body.end()), std::string("proxied"));
    if (proxy.worker.joinable()) proxy.worker.join();
    REQUIRE_EQ(proxy.requestLine, std::string("GET http://only-the-proxy-knows.invalid/path HTTP/1.1"));
}
#endif

// ---- Raw TCP through a proxy (UltraNet_TcpOpen) -------------------------------
#if !defined(_WIN32)
#include <UltraNet/UltraNetSocket.h>
#include <functional>

namespace {

// A server on 127.0.0.1 that runs `script` on the one connection it accepts.
struct ScriptedServer {
    int listener = -1;
    int port = 0;
    std::thread worker;

    bool Start(std::function<void(int)> script) {
        listener = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listener < 0) return false;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) return false;
        socklen_t len = sizeof addr;
        if (::getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &len) != 0) return false;
        port = ntohs(addr.sin_port);
        if (::listen(listener, 1) != 0) return false;
        worker = std::thread([this, script]() {
            const int c = ::accept(listener, nullptr, nullptr);
            if (c < 0) return;
            script(c);
            ::close(c);
        });
        return true;
    }
    void Join() { if (worker.joinable()) worker.join(); }
    ~ScriptedServer() {
        if (listener >= 0) ::shutdown(listener, SHUT_RDWR);
        Join();
        if (listener >= 0) ::close(listener);
    }
};

std::string ReadN(int fd, std::size_t n) {
    std::string out;
    char buf[512];
    while (out.size() < n) {
        const ssize_t got = ::recv(fd, buf, std::min(sizeof buf, n - out.size()), 0);
        if (got <= 0) break;
        out.append(buf, static_cast<std::size_t>(got));
    }
    return out;
}

std::string ReadUntil(int fd, const std::string& end) {
    std::string out;
    char c;
    while (out.size() < 8192 && (out.size() < end.size() || out.compare(out.size() - end.size(), end.size(), end) != 0)) {
        if (::recv(fd, &c, 1, 0) != 1) break;
        out += c;
    }
    return out;
}

void SendText(int fd, const std::string& s) { ::send(fd, s.data(), s.size(), 0); }

std::string ReceiveText(UltraNetHandle h) {
    std::vector<uint8_t> data;
    UltraNet_TcpReceive(h, data, 64, 5000);
    return std::string(data.begin(), data.end());
}

UltraNetSocketOptions Through(UltraNetProxyType type, int port) {
    UltraNetSocketOptions o;
    o.connectTimeoutMs = 5000;
    o.proxy = Manual(type, "127.0.0.1", port);
    return o;
}

} // namespace

TEST(proxy_tcp_socks5_by_name) {
    std::string request;
    ScriptedServer socks;
    REQUIRE(socks.Start([&](int c) {
        if (ReadN(c, 3) != std::string("\x05\x01\x00", 3)) return;
        SendText(c, std::string("\x05\x00", 2));
        request = ReadN(c, 4 + 1 + 14 + 2);           // name "target.invalid"
        SendText(c, std::string("\x05\x00\x00\x01\x00\x00\x00\x00\x00\x00", 10));
        if (ReadN(c, 4) == "ping") SendText(c, "pong");
    }));
    UltraNetHandle h = UltraNetInvalidHandle;
    const UltraNetResult r = UltraNet_TcpOpen("target.invalid", 4242, Through(UltraNetProxyType::Socks5, socks.port), h);
    REQUIRE(r.success);
    int sent = 0;
    UltraNet_TcpSend(h, std::vector<uint8_t>{'p', 'i', 'n', 'g'}, sent);
    REQUIRE_EQ(ReceiveText(h), std::string("pong"));
    UltraNet_SocketClose(h);
    socks.Join();
    // CONNECT, a domain name (the proxy looks it up), port 4242
    REQUIRE_EQ(request, std::string("\x05\x01\x00\x03\x0etarget.invalid\x10\x92", 21));
}

TEST(proxy_tcp_socks5_signs_in) {
    std::string signIn;
    ScriptedServer socks;
    REQUIRE(socks.Start([&](int c) {
        if (ReadN(c, 4) != std::string("\x05\x02\x00\x02", 4)) return;
        SendText(c, std::string("\x05\x02", 2));
        signIn = ReadN(c, 1 + 1 + 5 + 1 + 6);
        SendText(c, std::string("\x01\x00", 2));
        ReadN(c, 4 + 4 + 2);                            // an IPv4 literal
        SendText(c, std::string("\x05\x00\x00\x01\x00\x00\x00\x00\x00\x00", 10));
        SendText(c, "ok");
    }));
    UltraNetSocketOptions o = Through(UltraNetProxyType::Socks5, socks.port);
    o.proxy.credentials.username = "erika";
    o.proxy.credentials.password = "secret";
    UltraNetHandle h = UltraNetInvalidHandle;
    REQUIRE(UltraNet_TcpOpen("192.0.2.7", 22, o, h).success);
    REQUIRE_EQ(ReceiveText(h), std::string("ok"));
    UltraNet_SocketClose(h);
    socks.Join();
    REQUIRE_EQ(signIn, std::string("\x01\x05" "erika" "\x06" "secret"));
}

TEST(proxy_tcp_socks5_refused_sign_in) {
    ScriptedServer socks;
    REQUIRE(socks.Start([&](int c) {
        ReadN(c, 3);
        SendText(c, std::string("\x05\xFF", 2));
    }));
    UltraNetHandle h = UltraNetInvalidHandle;
    const UltraNetResult r = UltraNet_TcpOpen("target.invalid", 80, Through(UltraNetProxyType::Socks5, socks.port), h);
    REQUIRE(!r.success);
    REQUIRE(r.code == UltraNetResultCode::AuthenticationRequired);
    REQUIRE(h == UltraNetInvalidHandle);
}

TEST(proxy_tcp_socks4a) {
    std::string request;
    ScriptedServer socks;
    REQUIRE(socks.Start([&](int c) {
        request = ReadN(c, 8 + 1 + 15);                 // no user id, "target.invalid\0"
        SendText(c, std::string("\x00\x5A\x00\x00\x00\x00\x00\x00", 8));
        SendText(c, "hi");
    }));
    UltraNetHandle h = UltraNetInvalidHandle;
    REQUIRE(UltraNet_TcpOpen("target.invalid", 80, Through(UltraNetProxyType::Socks4, socks.port), h).success);
    REQUIRE_EQ(ReceiveText(h), std::string("hi"));
    UltraNet_SocketClose(h);
    socks.Join();
    REQUIRE_EQ(request, std::string("\x04\x01\x00\x50\x00\x00\x00\x01\x00target.invalid\x00", 24));
}

TEST(proxy_tcp_http_connect) {
    std::string request;
    ScriptedServer proxy;
    REQUIRE(proxy.Start([&](int c) {
        request = ReadUntil(c, "\r\n\r\n");
        // The server's first bytes right behind the answer: none may be lost.
        SendText(c, "HTTP/1.1 200 Connection established\r\n\r\nSSH-2.0-test\r\n");
    }));
    UltraNetSocketOptions o = Through(UltraNetProxyType::Http, proxy.port);
    o.proxy.credentials.username = "erika";
    o.proxy.credentials.password = "pw";
    UltraNetHandle h = UltraNetInvalidHandle;
    REQUIRE(UltraNet_TcpOpen("target.invalid", 22, o, h).success);
    REQUIRE_EQ(ReceiveText(h), std::string("SSH-2.0-test\r\n"));
    UltraNet_SocketClose(h);
    proxy.Join();
    REQUIRE(request.rfind("CONNECT target.invalid:22 HTTP/1.1\r\n", 0) == 0);
    REQUIRE(request.find("Proxy-Authorization: Basic ZXJpa2E6cHc=\r\n") != std::string::npos);
}

TEST(proxy_tcp_http_connect_407) {
    ScriptedServer proxy;
    REQUIRE(proxy.Start([&](int c) {
        ReadUntil(c, "\r\n\r\n");
        SendText(c, "HTTP/1.1 407 Proxy Authentication Required\r\nContent-Length: 0\r\n\r\n");
    }));
    UltraNetHandle h = UltraNetInvalidHandle;
    const UltraNetResult r = UltraNet_TcpOpen("target.invalid", 22, Through(UltraNetProxyType::Http, proxy.port), h);
    REQUIRE(!r.success);
    REQUIRE(r.code == UltraNetResultCode::AuthenticationRequired);
}

TEST(proxy_tcp_direct_by_default_and_detach) {
    GlobalProxyGuard guard;
    // A setting that would fail if used: a raw socket does not use it unless asked.
    UltraNet_SetGlobalProxy(Manual(UltraNetProxyType::Socks5, "127.0.0.1", 1));
    ScriptedServer server;
    REQUIRE(server.Start([&](int c) { SendText(c, "direct"); }));
    UltraNetSocketOptions o;
    o.connectTimeoutMs = 5000;
    UltraNetHandle h = UltraNetInvalidHandle;
    REQUIRE(UltraNet_TcpOpen("127.0.0.1", server.port, o, h).success);
    const std::intptr_t fd = UltraNet_SocketDetach(h);
    REQUIRE(fd >= 0);
    std::vector<uint8_t> data;
    REQUIRE(!UltraNet_TcpReceive(h, data, 16, 100).success);   // no longer UltraNet's
    char buf[16] = {};
    const ssize_t n = ::recv(static_cast<int>(fd), buf, sizeof buf, 0);
    REQUIRE_EQ(std::string(buf, n > 0 ? static_cast<std::size_t>(n) : 0), std::string("direct"));
    ::close(static_cast<int>(fd));
    server.Join();
}
#endif
