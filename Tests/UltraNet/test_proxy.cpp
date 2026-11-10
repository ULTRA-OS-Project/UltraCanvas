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
